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

//
//
//
class Telemetry {
public:
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
  class AwsIotEndpoint final {
    std::string _endpoint;

  public:
    explicit AwsIotEndpoint(std::string in) : _endpoint{in} {}
    const std::string &get() const { return _endpoint; }
  };
  //
  class AwsIotRootCa final {
    std::string _root_ca;

  public:
    explicit AwsIotRootCa(std::string in) : _root_ca{in} {}
    const std::string &get() const { return _root_ca; }
  };
  //
  class AwsIotCertificate final {
    std::string _cert;

  public:
    explicit AwsIotCertificate(std::string in) : _cert{in} {}
    const std::string &get() const { return _cert; }
  };
  //
  class AwsIotPrivateKey final {
    std::string _key;

  public:
    explicit AwsIotPrivateKey(std::string in) : _key{in} {}
    const std::string &get() const { return _key; }
  };
  //
  using MessageId = uint32_t;
  //
  using PayloadInstantAmpere =
      std::pair<std::chrono::system_clock::time_point,
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
    Mqtt(DeviceId deviceId, SensorId sensorId, AwsIotEndpoint endpoint,
         AwsIotRootCa root_ca, AwsIotCertificate certificate,
         AwsIotPrivateKey private_key)
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
    bool connected() { return mqtt_client.connected(); }
    // AWS IoTへ接続確立を試みる
    bool connectToAwsIot(
        std::chrono::seconds timeout,
        std::function<void(const std::string &message, void *user_data)>
            display_message,
        void *user_data);
    // 送信用キューに積む
    void enqueue(const Payload &&in);
    // MQTT接続検査
    bool
    check_mqtt(std::chrono::seconds timeout,
               std::function<void(const std::string &message, void *user_data)>
                   display_message,
               void *user_data);
    // MQTT送受信
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
    const AwsIotEndpoint _endpoint;
    //
    const AwsIotRootCa _root_ca;
    //
    const AwsIotCertificate _certificate;
    //
    const AwsIotPrivateKey _private_key;
    //
    const std::string _publish_topic;
    //
    const std::string _subscribe_topic;
    //
    static void mqtt_callback(char *topic, uint8_t *payload,
                              unsigned int length);
    //
    static std::string
    iso8601formatUTC(std::chrono::system_clock::time_point utctimept);
    // 送信用メッセージに変換する
    template <typename T>
    static std::string to_json_message(const DeviceId &deviceId,
                                       const SensorId &sensorId,
                                       const MessageId &messageId, T in);
    //
    static std::string httpsLastError(WiFiClientSecure &client);
    //
    static std::string strMqttState(PubSubClient &client);
  };
};