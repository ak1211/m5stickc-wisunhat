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
using PInstantAmpere =
    std::tuple<MessageId, std::chrono::system_clock::time_point,
               SmartElectricEnergyMeter::InstantAmpere>;
//
using PInstantWatt =
    std::tuple<MessageId, std::chrono::system_clock::time_point,
               SmartElectricEnergyMeter::InstantWatt>;
//
using PCumlativeWattHour =
    std::tuple<MessageId, SmartElectricEnergyMeter::CumulativeWattHour,
               SmartElectricEnergyMeter::Coefficient,
               SmartElectricEnergyMeter::Unit>;

//
// 送信用:
//
using Payload = std::variant<PInstantAmpere, PInstantWatt, PCumlativeWattHour>;

//
std::string iso8601formatUTC(std::time_t utctime) {
  struct tm tm;
  gmtime_r(&utctime, &tm);
  char buffer[30]{'\0'};
  std::strftime(buffer, std::size(buffer), "%Y-%m-%dT%H:%M:%SZ", &tm);
  return std::string(buffer);
}

constexpr std::size_t Capacity{JSON_OBJECT_SIZE(100)};

// 送信用メッセージに変換する
std::string to_json_message(PInstantAmpere in) {
  namespace M = SmartElectricEnergyMeter;
  using std::chrono::duration_cast;
  auto &[messageId, timept, a] = in;
  StaticJsonDocument<Capacity> doc;
  doc["message_id"] = messageId;
  doc["device_id"] = AWS_IOT_DEVICE_ID;
  doc["sensor_id"] = SENSOR_ID;
  doc["measured_at"] =
      iso8601formatUTC(std::chrono::system_clock::to_time_t(timept));
  doc["instant_ampere_R"] = duration_cast<M::Ampere>(a.ampereR).count();
  doc["instant_ampere_T"] = duration_cast<M::Ampere>(a.ampereT).count();
  std::string output;
  serializeJson(doc, output);
  return output;
}

// 送信用メッセージに変換する
std::string to_json_message(PInstantWatt in) {
  auto &[messageId, timept, w] = in;
  StaticJsonDocument<Capacity> doc;
  doc["message_id"] = messageId;
  doc["device_id"] = AWS_IOT_DEVICE_ID;
  doc["sensor_id"] = SENSOR_ID;
  doc["measured_at"] =
      iso8601formatUTC(std::chrono::system_clock::to_time_t(timept));
  doc["instant_watt"] = w.watt.count();
  std::string output;
  serializeJson(doc, output);
  return output;
}

// 送信用メッセージに変換する
std::string to_json_message(PCumlativeWattHour in) {
  namespace M = SmartElectricEnergyMeter;
  auto &[messageId, cwh, coeff, unit] = in;
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
  M::KiloWattHour kwh =
      M::cumlative_kilo_watt_hour(std::make_tuple(cwh, coeff, unit));
  doc["cumlative_kwh"] = kwh.count();
  std::string output;
  serializeJson(doc, output);
  return output;
}

//
// MQTT通信
//
class Mqtt final {
  WiFiClientSecure https_client;
  PubSubClient mqtt_client;
  const std::string mqtt_topic;
  // IoT Core送信用バッファ
  std::queue<Payload> sending_fifo_queue;
  // IoT Coreにメッセージを送信しようとした時間
  std::chrono::system_clock::time_point attempt_send_time;

public:
  static constexpr uint16_t AWS_IOT_MQTT_PORT{8883};
  //
  Mqtt()
      : https_client{},
        mqtt_client{https_client},
        mqtt_topic{"device/"s + std::string{AWS_IOT_DEVICE_ID} + "/data"s},
        sending_fifo_queue{},
        attempt_send_time{} {}
  //
  // AWS IoTへ接続確立を試みる
  //
  bool connectToAwsIot(std::size_t retry_count = 100) {
    //
    https_client.setCACert(AWS_IOT_ROOT_CA.data());
    https_client.setCertificate(AWS_IOT_CERTIFICATE.data());
    https_client.setPrivateKey(AWS_IOT_PRIVATE_KEY.data());
    //
    mqtt_client.setServer(AWS_IOT_ENDPOINT.data(), AWS_IOT_MQTT_PORT);
    //
    for (auto retry = 0; retry < retry_count; ++retry) {
      if (mqtt_client.connect(AWS_IOT_DEVICE_ID.data())) {
        ESP_LOGI(TELEMETRY, "MQTT Connected!");
        return true;
      }
      delay(100);
    }
    return false;
  }
  //
  // AWS IoTへ送信する
  //
  bool send_mqtt(const std::string &string_telemetry) {
    if (mqtt_client.connected()) {
      ESP_LOGD(TELEMETRY, "%s", string_telemetry.c_str());
      return mqtt_client.publish(mqtt_topic.c_str(), string_telemetry.c_str());
    } else {
      ESP_LOGD(TELEMETRY, "MQTT is NOT connected.");
      return false;
    }
  }
  //
  // 送信用キューに積む
  //
  void push_queue(const Payload &v) { sending_fifo_queue.push(v); }
  //
  // MQTT接続検査
  //
  bool check_mqtt(std::chrono::seconds timeout) {
    using namespace std::chrono;
    time_point tp = system_clock::now() + timeout;

    do {
      if (mqtt_client.connected()) {
        return true;
      }
      // MQTT再接続シーケンス
      ESP_LOGD(TELEMETRY, "MQTT reconnect");
      mqtt_client.connect(AWS_IOT_DEVICE_ID.data());
    } while (system_clock::now() < tp);
    return mqtt_client.connected();
  }
  //
  // MQTT送受信
  //
  bool loop_mqtt() {
    if (mqtt_client.connected()) {
      using namespace std::chrono;
      // 送信するべき測定値があればIoT Coreへ送信する(1秒以上の間隔をあけて)
      if (auto tp = system_clock::now();
          tp - attempt_send_time >= seconds{1} && !sending_fifo_queue.empty()) {
        std::string msg{};
        auto &item = sending_fifo_queue.front();
        if (std::holds_alternative<PInstantAmpere>(item)) {
          msg = to_json_message(std::get<PInstantAmpere>(item));
        } else if (std::holds_alternative<PInstantWatt>(item)) {
          msg = to_json_message(std::get<PInstantWatt>(item));
        } else if (std::holds_alternative<PCumlativeWattHour>(item)) {
          msg = to_json_message(std::get<PCumlativeWattHour>(item));
        }
        // MQTT送信
        if (msg.length() > 0 && send_mqtt(msg)) {
          // IoT Coreへ送信したメッセージをFIFOから消す
          sending_fifo_queue.pop();
        }
        // 成功失敗によらずIoT Coreへの送信時間を記録する
        attempt_send_time = tp;
      }
      // MQTT受信
      return mqtt_client.loop();
    } else {
      return false;
    }
  }
};
} // namespace Telemetry
