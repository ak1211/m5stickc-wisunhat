// Copyright (c) 2022 Akihiro Yamamoto.
// Licensed under the MIT License <https://spdx.org/licenses/MIT.html>
// See LICENSE file in the project root for full license information.
//
#pragma once
#include "EchonetLite.hpp"
#include <PubSubClient.h>
#include <WiFiClientSecure.h>
#include <chrono>
#include <memory>
#include <ostream>
#include <queue>
#include <string>
#include <tuple>
#include <variant>

//
// MQTT通信
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

public:
  constexpr static auto RECONNECT_TIMEOUT = std::chrono::seconds{30};
  constexpr static auto MAXIMUM_QUEUE_SIZE = size_t{100};
  constexpr static auto MQTT_PORT = uint16_t{8883};
  constexpr static auto SOCKET_TIMEOUT = std::chrono::seconds{90};
  constexpr static auto KEEP_ALIVE = std::chrono::seconds{60};
  constexpr static auto QUARITY_OF_SERVICE = uint8_t{1};
  //
  Telemetry(DeviceId deviceId, SensorId sensorId, AwsIotEndpoint endpoint,
            AwsIotRootCa root_ca, AwsIotCertificate certificate,
            AwsIotPrivateKey private_key)
      : _deviceId{deviceId},
        _sensorId{sensorId},
        _endpoint{endpoint},
        _root_ca{root_ca},
        _certificate{certificate},
        _private_key{private_key},
        _publish_topic{std::string{"device/"} + _deviceId.get() +
                       std::string{"/data"}},
        _subscribe_topic{std::string{"device/"} + _deviceId.get() +
                         std::string{"/cmd"}},
        _sending_fifo_queue{},
        _message_id_counter{0} {}
  //
  bool begin(std::ostream &os, std::chrono::seconds timeout);
  //
  bool isConnected() {
    return _mqtt_client ? _mqtt_client->connected() : false;
  }
  // 送信用キューに積む
  void enqueue(const Payload &&in);
  // MQTT送受信
  bool task_handler();

private:
  //
  std::unique_ptr<WiFiClientSecure> _https_client;
  std::unique_ptr<PubSubClient> _mqtt_client;
  // IoT Core送信用バッファ
  std::queue<Payload> _sending_fifo_queue;
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
  static void message_arrival_callback(char *topic, uint8_t *payload,
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