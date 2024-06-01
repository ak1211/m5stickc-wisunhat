// Copyright (c) 2022 Akihiro Yamamoto.
// Licensed under the MIT License <https://spdx.org/licenses/MIT.html>
// See LICENSE file in the project root for full license information.
//
#pragma once
#include "Application.hpp"
#include "EchonetLite.hpp"
#include <ArduinoJson.h>
#include <PubSubClient.h>
#include <WiFi.h>
#include <WiFiClient.h>
#include <WiFiClientSecure.h>
#include <chrono>
#include <ctime>
#include <functional>
#include <future>
#include <queue>
#include <string>
#include <tuple>
#include <variant>

using namespace std::literals::string_literals;

namespace Telemetry {
//
class DeviceId final {
  std::string _id;

public:
  explicit DeviceId(std::string in) : _id{in} {}
  const std::string &get() const { return _id; }
};
//
class SensorId final {
  std::string _id;

public:
  explicit SensorId(std::string in) : _id{in} {}
  const std::string &get() const { return _id; }
};
//
namespace AwsIot {
//
class Endpoint final {
  std::string _endpoint;

public:
  explicit Endpoint(std::string in) : _endpoint{in} {}
  const std::string &get() const { return _endpoint; }
};
//
class RootCa final {
  std::string _root_ca;

public:
  explicit RootCa(std::string in) : _root_ca{in} {}
  const std::string &get() const { return _root_ca; }
};
//
class Certificate final {
  std::string _cert;

public:
  explicit Certificate(std::string in) : _cert{in} {}
  const std::string &get() const { return _cert; }
};
//
class PrivateKey final {
  std::string _key;

public:
  explicit PrivateKey(std::string in) : _key{in} {}
  const std::string &get() const { return _key; }
};
} // namespace AwsIot

//
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
// MQTT通信
//
class Mqtt final {
  constexpr static auto MAXIMUM_QUEUE_SIZE = 100;

public:
  constexpr static auto MQTT_PORT = uint16_t{8883};
  constexpr static auto SOCKET_TIMEOUT = uint16_t{90};
  constexpr static auto KEEP_ALIVE = uint16_t{60};
  constexpr static auto QUARITY_OF_SERVICE = uint8_t{1};
  //
  Mqtt(DeviceId deviceId, SensorId sensorId, AwsIot::Endpoint endpoint,
       AwsIot::RootCa root_ca, AwsIot::Certificate certificate,
       AwsIot::PrivateKey private_key)
      : https_client{},
        mqtt_client{https_client},
        _deviceId{deviceId},
        _sensorId{sensorId},
        _endpoint{endpoint},
        _root_ca{root_ca},
        _certificate{certificate},
        _private_key{private_key},
        _publish_topic{"device/"s + _deviceId.get() + "/data"s},
        _subscribe_topic{"device/"s + _deviceId.get() + "/cmd"s},
        sending_fifo_queue{},
        _message_id_counter{0} {}
  //
  static void mqtt_callback(char *topic, uint8_t *payload,
                            unsigned int length) {
    std::string s(length + 1, '\0');
    std::copy_n(payload, length, s.begin());
    s.shrink_to_fit();
    M5_LOGI("New message arrival. topic:\"%s\", payload:\"%s\"", topic,
            s.c_str());
  }
  //
  bool connected() { return mqtt_client.connected(); }
  //
  static std::string httpsLastError(WiFiClientSecure &client) {
    constexpr std::size_t N{256};
    std::string buff(N, '\0');
    client.lastError(buff.data(), N);
    buff.shrink_to_fit();
    return buff;
  }
  //
  static std::string strMqttState(PubSubClient &client) {
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
  //
  // AWS IoTへ接続確立を試みる
  //
  bool connectToAwsIot(
      std::chrono::seconds timeout,
      std::function<void(const std::string &message, void *user_data)>
          display_message,
      void *user_data) {

    using namespace std::chrono;
    const time_point tp = system_clock::now() + timeout;
    //
    display_message("protocol MQTT", user_data);
    //
    https_client.setCACert(_root_ca.get().c_str());
    https_client.setCertificate(_certificate.get().c_str());
    https_client.setPrivateKey(_private_key.get().c_str());
    https_client.setTimeout(SOCKET_TIMEOUT);
    //
    mqtt_client.setServer(_endpoint.get().c_str(), MQTT_PORT);
    mqtt_client.setSocketTimeout(SOCKET_TIMEOUT);
    mqtt_client.setKeepAlive(KEEP_ALIVE);
    mqtt_client.setCallback(mqtt_callback);
    //
    bool success{false};
    do {
      success = mqtt_client.connect(_deviceId.get().c_str(), nullptr,
                                    QUARITY_OF_SERVICE, false, "");
      std::this_thread::yield();
    } while (!success && system_clock::now() < tp);

    if (success) {
      display_message("connected.", user_data);
    } else {
      display_message(
          "connect fail to AWS IoT, state: " + strMqttState(mqtt_client) +
              ", reason: " + httpsLastError(https_client),
          user_data);
      return false;
    }

    success =
        mqtt_client.subscribe(_subscribe_topic.c_str(), QUARITY_OF_SERVICE);
    if (success) {
      display_message("topic:" + _subscribe_topic + " subscribed.", user_data);
    } else {
      display_message("topic:" + _subscribe_topic + " failed.", user_data);
      return false;
    }
    return true;
  }
  //
  // 送信用キューに積む
  //
  void enqueue(const Payload &&in) {

    if (sending_fifo_queue.size() >= MAXIMUM_QUEUE_SIZE) {
      M5_LOGE("MAXIMUM_QUEUE_SIZE reached.");
      do {
        // 溢れた測定値をFIFOから消す
        sending_fifo_queue.pop();
      } while (sending_fifo_queue.size() >= MAXIMUM_QUEUE_SIZE);
    }
    sending_fifo_queue.push(in);
  }
  //
  // MQTT接続検査
  //
  bool
  check_mqtt(std::chrono::seconds timeout,
             std::function<void(const std::string &message, void *user_data)>
                 display_message,
             void *user_data) {
    if (mqtt_client.connected()) {
      return true;
    }
    // MQTT再接続シーケンス
    M5_LOGE("MQTT reconnect, state: %s, reason: %s",
            strMqttState(mqtt_client).c_str(),
            httpsLastError(https_client).c_str());
    return connectToAwsIot(timeout, display_message, user_data);
  }
  //
  // MQTT送受信
  //
  bool loop_mqtt();

private:
  //
  WiFiClientSecure https_client;
  PubSubClient mqtt_client;
  // IoT Core送信用バッファ
  std::queue<Payload> sending_fifo_queue;
  // メッセージIDカウンタ(IoT Core用)
  MessageId _message_id_counter;
  //
  const DeviceId _deviceId;
  //
  const SensorId _sensorId;
  //
  const AwsIot::Endpoint _endpoint;
  //
  const AwsIot::RootCa _root_ca;
  //
  const AwsIot::Certificate _certificate;
  //
  const AwsIot::PrivateKey _private_key;
  //
  const std::string _publish_topic;
  //
  const std::string _subscribe_topic;
  //
  static std::string
  iso8601formatUTC(std::chrono::system_clock::time_point utctimept);
  // 送信用メッセージに変換する
  template <typename T>
  static std::string to_json_message(const DeviceId &deviceId,
                                     const SensorId &sensorId,
                                     const MessageId &messageId, T in);
};
} // namespace Telemetry
