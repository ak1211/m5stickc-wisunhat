// Copyright (c) 2022 Akihiro Yamamoto.
// Licensed under the MIT License <https://spdx.org/licenses/MIT.html>
// See LICENSE file in the project root for full license information.
//
#pragma once
#include <ArduinoJson.h>
#include <PubSubClient.h>
#include <WiFi.h>
#include <WiFiClient.h>
#include <WiFiClientSecure.h>
#include <chrono>
#include <ctime>
#include <queue>
#include <string>
#include <tuple>
#include <variant>

#include "Application.hpp"
#include "EchonetLite.hpp"

using namespace std::literals::string_literals;

namespace Telemetry {
using MessageId = uint32_t;
//
using PayloadInstantAmpere = std::pair<std::chrono::system_clock::time_point,
                                       SmartElectricEnergyMeter::InstantAmpere>;
//
using PayloadInstantWatt = std::pair<std::chrono::system_clock::time_point,
                                     SmartElectricEnergyMeter::InstantWatt>;
//
using PayloadCumlativeWattHour =
    std::tuple<SmartElectricEnergyMeter::CumulativeWattHour,
               SmartElectricEnergyMeter::Coefficient,
               SmartElectricEnergyMeter::Unit>;
// 送信用
using Payload = std::variant<PayloadInstantAmpere, PayloadInstantWatt,
                             PayloadCumlativeWattHour>;

//
std::string iso8601formatUTC(std::chrono::system_clock::time_point utctimept) {
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

constexpr std::size_t Capacity{JSON_OBJECT_SIZE(100)};

// 送信用メッセージに変換する
std::string to_json_message(MessageId messageId, PayloadInstantAmpere in) {
  namespace M = SmartElectricEnergyMeter;
  using std::chrono::duration_cast;
  auto &[timept, a] = in;
  StaticJsonDocument<Capacity> doc;
  doc["message_id"] = messageId;
  doc["device_id"] = AWS_IOT_DEVICE_ID;
  doc["sensor_id"] = SENSOR_ID;
  doc["measured_at"] = iso8601formatUTC(timept);
  doc["instant_ampere_R"] = duration_cast<M::Ampere>(a.ampereR).count();
  doc["instant_ampere_T"] = duration_cast<M::Ampere>(a.ampereT).count();
  std::string output;
  serializeJson(doc, output);
  return output;
}

// 送信用メッセージに変換する
std::string to_json_message(MessageId messageId, PayloadInstantWatt in) {
  auto &[timept, w] = in;
  StaticJsonDocument<Capacity> doc;
  doc["message_id"] = messageId;
  doc["device_id"] = AWS_IOT_DEVICE_ID;
  doc["sensor_id"] = SENSOR_ID;
  doc["measured_at"] = iso8601formatUTC(timept);
  doc["instant_watt"] = w.watt.count();
  std::string output;
  serializeJson(doc, output);
  return output;
}

// 送信用メッセージに変換する
std::string to_json_message(MessageId messageId, PayloadCumlativeWattHour in) {
  namespace M = SmartElectricEnergyMeter;
  auto &[cwh, coeff, unit] = in;
  StaticJsonDocument<Capacity> doc;
  doc["message_id"] = messageId;
  doc["device_id"] = AWS_IOT_DEVICE_ID;
  doc["sensor_id"] = SENSOR_ID;
  // 時刻をISO8601形式で得る
  auto opt_iso8601 = cwh.get_iso8601_datetime();
  if (opt_iso8601.has_value()) {
    doc["measured_at"] = opt_iso8601.value();
  }
  // 積算電力量(kwh)
  M::KiloWattHour kwh = M::cumlative_kilo_watt_hour({cwh, coeff, unit});
  doc["cumlative_kwh"] = kwh.count();
  std::string output;
  serializeJson(doc, output);
  return output;
}

//
// MQTT通信
//
class Mqtt final {
  //
  WiFiClientSecure https_client;
  PubSubClient mqtt_client;
  // IoT Core送信用バッファ
  std::queue<Payload> sending_fifo_queue;
  // メッセージIDカウンタ(IoT Core用)
  MessageId messageId;

public:
  constexpr static auto AWS_IOT_MQTT_PORT = uint16_t{8883};
  constexpr static auto SOCKET_TIMEOUT = uint16_t{90};
  constexpr static auto QuarityOfService = uint8_t{1};
  const std::string pub_topic;
  const std::string sub_topic;
  //
  Mqtt()
      : https_client{},
        mqtt_client{https_client},
        pub_topic{"device/"s + std::string{AWS_IOT_DEVICE_ID} + "/data"s},
        sub_topic{"device/"s + std::string{AWS_IOT_DEVICE_ID} + "/sub"s},
        sending_fifo_queue{},
        messageId{0} {}
  //
  static void callback(char *topic, uint8_t *payload, uint32_t length) {
    std::vector<char> buffer;
    buffer.reserve(length + 1);
    std::copy_n(payload, length, std::back_inserter(buffer));
    buffer.push_back('\0');
    ESP_LOGI(TELEMETRY, "%s:%s", topic, buffer.data());
  }
  //
  std::string_view strMqttState() {
    switch (mqtt_client.state()) {
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
  //
  // AWS IoTへ接続確立を試みる
  //
  bool connectToAwsIot(std::chrono::seconds timeout) {
    using namespace std::chrono;
    const time_point tp = system_clock::now() + timeout;
    //
    https_client.setCACert(AWS_IOT_ROOT_CA.data());
    https_client.setCertificate(AWS_IOT_CERTIFICATE.data());
    https_client.setPrivateKey(AWS_IOT_PRIVATE_KEY.data());
    //
    mqtt_client.setServer(AWS_IOT_ENDPOINT.data(), AWS_IOT_MQTT_PORT);
    mqtt_client.setSocketTimeout(SOCKET_TIMEOUT);
    mqtt_client.setCallback(callback);
    //
    do {
      if (mqtt_client.connect(AWS_IOT_DEVICE_ID.data(), nullptr,
                              QuarityOfService, false, "")) {
        mqtt_client.subscribe(sub_topic.c_str(), QuarityOfService);
        return true;
      }
      delay(100);
    } while (system_clock::now() < tp);
    ESP_LOGE(TELEMETRY, "connect fail to AWS IoT, reason: %s",
             strMqttState().data());
    return false;
  }
  //
  // 送信用キューに積む
  //
  void push_queue(const Payload &&in) { sending_fifo_queue.push(in); }
  //
  // MQTT接続検査
  //
  bool check_mqtt(std::chrono::seconds timeout) {
    if (mqtt_client.connected()) {
      return true;
    }
    // MQTT再接続シーケンス
    ESP_LOGD(TELEMETRY, "MQTT reconnect, reason: %s", strMqttState().data());
    return connectToAwsIot(timeout);
  }
  //
  // MQTT送受信
  //
  bool loop_mqtt() {
    using namespace std::literals::chrono_literals;
    using namespace std::chrono;
    // 送信するべき測定値があればIoT Coreへ送信する
    std::string msg{};
    if (!sending_fifo_queue.empty()) {
      auto item = sending_fifo_queue.front();
      if (std::holds_alternative<PayloadInstantAmpere>(item)) {
        msg = to_json_message(messageId, std::get<PayloadInstantAmpere>(item));
      } else if (std::holds_alternative<PayloadInstantWatt>(item)) {
        msg = to_json_message(messageId, std::get<PayloadInstantWatt>(item));
      } else if (std::holds_alternative<PayloadCumlativeWattHour>(item)) {
        msg = to_json_message(messageId,
                              std::get<PayloadCumlativeWattHour>(item));
      } else {
        ESP_LOGE(TELEMETRY, "message is BROKEN");
      }
    }
    // MQTT送信
    if (msg.length() > 0 && mqtt_client.connected()) {
      ESP_LOGD(TELEMETRY, "%s", msg.c_str());
      if (mqtt_client.publish(pub_topic.c_str(), msg.c_str())) {
        messageId++;
        // IoT Coreへ送信した測定値をFIFOから消す
        sending_fifo_queue.pop();
      }
    }
    // MQTT受信
    return mqtt_client.loop();
  }
};
} // namespace Telemetry
