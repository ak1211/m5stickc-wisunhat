// Copyright (c) 2022 Akihiro Yamamoto.
// Licensed under the MIT License <https://spdx.org/licenses/MIT.html>
// See LICENSE file in the project root for full license information.
//
#pragma once

#include "Application.hpp"
#include <PubSubClient.h>
#include <WiFi.h>
#include <WiFiClient.h>
#include <WiFiClientSecure.h>
#include <chrono>
#include <ctime>
#include <queue>
#include <string>

using namespace std::literals::string_literals;

//
// MQTT通信
//
class Telemetry final {
  WiFiClientSecure https_client;
  PubSubClient mqtt_client;
  const std::string mqtt_topic;
  // IoT Hub送信用バッファ
  std::queue<std::string> sending_fifo_queue{};

public:
  static constexpr uint16_t AWS_IOT_MQTT_PORT{8883};
  //
  Telemetry()
      : https_client{},
        mqtt_client{https_client},
        mqtt_topic{"device/"s + std::string{AWS_IOT_DEVICE_ID} + "/data"s},
        sending_fifo_queue{} {}
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
  void emplace_queue(std::string &&s) { sending_fifo_queue.emplace(s); }
  //
  //
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
      mqtt_client.disconnect();
      delay(10);
      mqtt_client.connect(AWS_IOT_DEVICE_ID.data());
    } while (system_clock::now() < tp);
    return mqtt_client.connected();
  }
  //
  //
  //
  bool loop_mqtt() { return mqtt_client.loop(); }
};
