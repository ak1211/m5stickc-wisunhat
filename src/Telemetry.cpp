// Copyright (c) 2022 Akihiro Yamamoto.
// Licensed under the MIT License <https://spdx.org/licenses/MIT.html>
// See LICENSE file in the project root for full license information.
//
#include "Telemetry.hpp"
#include "Gui.hpp"
#include "StringBufWithDialogue.hpp"
#include <ArduinoJson.h>
#include <chrono>
#include <future>
#include <sstream>

using namespace std::chrono;

//
template <>
std::string Telemetry::to_json_message(const DeviceId &deviceId,
                                       const SensorId &sensorId,
                                       const MessageId &messageId,
                                       PayloadInstantAmpere in) {
  using SmartElectricEnergyMeter::Ampere;
  auto &[timept, a] = in;
  JsonDocument doc;
  doc["message_id"] = messageId;
  doc["device_id"] = deviceId.get();
  doc["sensor_id"] = sensorId.get();
  doc["measured_at"] = iso8601formatUTC(timept);
  doc["instant_ampere_R"] = duration_cast<Ampere>(a.ampereR).count();
  doc["instant_ampere_T"] = duration_cast<Ampere>(a.ampereT).count();
  std::string output;
  serializeJson(doc, output);
  return output;
}

//
template <>
std::string
Telemetry::to_json_message(const DeviceId &deviceId, const SensorId &sensorId,
                           const MessageId &messageId, PayloadInstantWatt in) {
  auto &[timept, w] = in;
  JsonDocument doc;
  doc["message_id"] = messageId;
  doc["device_id"] = deviceId.get();
  doc["sensor_id"] = sensorId.get();
  doc["measured_at"] = iso8601formatUTC(timept);
  doc["instant_watt"] = w.watt.count();
  std::string output;
  serializeJson(doc, output);
  return output;
}

//
template <>
std::string Telemetry::to_json_message(const DeviceId &deviceId,
                                       const SensorId &sensorId,
                                       const MessageId &messageId,
                                       PayloadCumlativeWattHour in) {
  namespace M = SmartElectricEnergyMeter;
  auto &[cwh, coeff, unit] = in;
  JsonDocument doc;
  doc["message_id"] = messageId;
  doc["device_id"] = deviceId.get();
  doc["sensor_id"] = sensorId.get();
  // 時刻をISO8601形式で得る
  auto opt_iso8601 = cwh.get_iso8601_datetime();
  if (opt_iso8601.has_value()) {
    doc["measured_at"] = opt_iso8601.value();
  }
  // 積算電力量(kwh)
  M::KiloWattHour kwh =
      EchonetLite::cumlative_kilo_watt_hour(cwh, coeff, unit);
  doc["cumlative_kwh"] = kwh.count();
  std::string output;
  serializeJson(doc, output);
  return output;
}

// AWS IoTへ接続確立を試みる
bool Telemetry::begin(std::ostream &os, std::chrono::seconds timeout) {
  //
  _https_client.reset(new WiFiClientSecure);
  if (_https_client) {
    _mqtt_client.reset(new PubSubClient{*_https_client});
  }
  // guard
  if (!_https_client || !_mqtt_client) {
    os << "out of memory" << std::endl;
    M5_LOGE("out of memory");
    return false;
  }
  assert(_https_client);
  assert(_mqtt_client);

  // HTTP over SSL/TLS
  _https_client->setCACert(_root_ca.get().c_str());
  _https_client->setCertificate(_certificate.get().c_str());
  _https_client->setPrivateKey(_private_key.get().c_str());
  _https_client->setTimeout(duration_cast<seconds>(SOCKET_TIMEOUT).count());

  // MQTT over SSL/TLS
  _mqtt_client->setServer(_endpoint.get().c_str(), MQTT_PORT);
  _mqtt_client->setSocketTimeout(
      duration_cast<seconds>(SOCKET_TIMEOUT).count());
  _mqtt_client->setKeepAlive(duration_cast<seconds>(KEEP_ALIVE).count());
  _mqtt_client->setCallback(message_arrival_callback);

  // MQTT接続待ち
  const auto TIMEOVER{steady_clock::now() + timeout};
  bool success{false};
  do {
    success = _mqtt_client->connect(_deviceId.get().c_str(), nullptr,
                                    QUARITY_OF_SERVICE, false, "");
    if (success) {
      break;
    } else {
      os << "waiting for MQTT connection" << std::endl;
      std::this_thread::sleep_for(500ms);
    }
  } while (steady_clock::now() < TIMEOVER);

  if (!_mqtt_client->connected()) {
    // MQTT接続失敗
    std::ostringstream ss;
    ss << "connect fail to AWS IoT, state: " << strMqttState(*_mqtt_client)
       << ", reason: " + httpsLastError(*_https_client);
    os << ss.str() << std::endl;
    M5_LOGE("%s", ss.str().c_str());
    return false;
  }

  // MQTT接続確認後に購読を開始する
  os << "MQTT connected" << std::endl;
  if (_mqtt_client->subscribe(_subscribe_topic.c_str(), QUARITY_OF_SERVICE)) {
    std::ostringstream ss;
    ss << "topic:" << _subscribe_topic << " subscribed.";
    os << ss.str() << std::endl;
    M5_LOGI("%s", ss.str().c_str());
    return true;
  } else {
    std::ostringstream ss;
    ss << "topic:" << _subscribe_topic << " failed.";
    os << ss.str() << std::endl;
    M5_LOGE("%s", ss.str().c_str());
    return false;
  }
}

// 送信用キューに積む
void Telemetry::enqueue(const Payload &&in) {
  if (_sending_fifo_queue.size() >= MAXIMUM_QUEUE_SIZE) {
    M5_LOGI("MAXIMUM_QUEUE_SIZE reached.");
    do {
      // 溢れた測定値をFIFOから消す
      _sending_fifo_queue.pop();
    } while (_sending_fifo_queue.size() >= MAXIMUM_QUEUE_SIZE);
  }
  _sending_fifo_queue.push(in);
}

// MQTT送受信
bool Telemetry::task_handler() {
  if (!isConnected()) {
    // 再接続
    StringBufWithDialogue buf{"Reconnect MQTT"};
    std::ostream ostream(&buf);
    ostream << "MQTT reconnect" << std::endl;
    M5_LOGI("MQTT reconnect");
    return begin(ostream, RECONNECT_TIMEOUT);
  }
  // MQTT受信
  if (!_mqtt_client->loop()) {
    return false;
  }
  // 送信するべき測定値があればIoT Coreへ送信する
  if (_sending_fifo_queue.empty()) {
    return true;
  } else {
    // MQTT送信
    std::string msg = std::visit(
        [this](auto x) -> std::string {
          return to_json_message(_deviceId, _sensorId, _message_id_counter, x);
        },
        _sending_fifo_queue.front());
    // MQTT送信
    M5_LOGD("%s", msg.c_str());
    bool result = _mqtt_client->publish(_publish_topic.c_str(), msg.c_str());
    if (result) {
      _message_id_counter++;
      // IoT Coreへ送信した測定値をFIFOから消す
      _sending_fifo_queue.pop();
    }
    return result;
  }
}

//
void Telemetry::message_arrival_callback(char *topic, uint8_t *payload,
                                         unsigned int length) {
  std::string s(length + 1, '\0');
  std::copy_n(payload, length, s.begin());
  s.shrink_to_fit();
  M5_LOGI("New message arrival. topic:\"%s\", payload:\"%s\"", topic,
          s.c_str());
}

//
std::string
Telemetry::iso8601formatUTC(std::chrono::system_clock::time_point utctimept) {
  auto utc = std::chrono::system_clock::to_time_t(utctimept);
  struct tm tm;
  gmtime_r(&utc, &tm);
  constexpr char format[] = "%Y-%m-%dT%H:%M:%SZ";
  constexpr std::size_t SIZE = std::size(format) * 2;
  std::string buffer(SIZE, '\0');
  std::size_t len = std::strftime(buffer.data(), SIZE, format, &tm);
  buffer.resize(len);
  return buffer;
}

//
std::string Telemetry::httpsLastError(WiFiClientSecure &client) {
  constexpr std::size_t N{256};
  std::string buff(N, '\0');
  client.lastError(buff.data(), N);
  buff.shrink_to_fit();
  return buff;
}

//
std::string Telemetry::strMqttState(PubSubClient &client) {
  switch (client.state()) {
  case MQTT_CONNECTION_TIMEOUT:
    return "MQTT_CONNECTION_TIMEOUT";
  case MQTT_CONNECTION_LOST:
    return "MQTT_CONNECTION_LOST";
  case MQTT_CONNECT_FAILED:
    return "MQTT_CONNECT_FAILED";
  case MQTT_DISCONNECTED:
    return "MQTT_DISCONNECTED";
  case MQTT_CONNECTED:
    return "MQTT_CONNECTED";
  case MQTT_CONNECT_BAD_PROTOCOL:
    return "MQTT_CONNECT_BAD_PROTOCOL";
  case MQTT_CONNECT_BAD_CLIENT_ID:
    return "MQTT_CONNECT_BAD_CLIENT_ID";
  case MQTT_CONNECT_UNAVAILABLE:
    return "MQTT_CONNECT_UNAVAILABLE";
  case MQTT_CONNECT_BAD_CREDENTIALS:
    return "MQTT_CONNECT_BAD_CREDENTIALS";
  case MQTT_CONNECT_UNAUTHORIZED:
    return "MQTT_CONNECT_UNAUTHORIZED";
  default:
    return "MQTT_STATE_UNKNOWN";
  }
}
