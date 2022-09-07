// Copyright (c) 2022 Akihiro Yamamoto.
// Licensed under the MIT License <https://spdx.org/licenses/MIT.html>
// See LICENSE file in the project root for full license information.
//
#include <PubSubClient.h>
#include <Telemetry>
#include <WiFi.h>
#include <WiFiClient.h>
#include <WiFiClientSecure.h>
#include <ctime>
#include <esp_log.h>
#include <esp_sntp.h>

// ログ出し用
static constexpr char TELEMETRY[] = "TELEMETRY";

//
static constexpr uint16_t AWS_IOT_MQTT_PORT{8883};

// MQTT
static WiFiClientSecure https_client;
static PubSubClient mqtt_client(https_client);

static const std::string MQTT_TOPIC{"device/" + std::string{AWS_IOT_DEVICE_ID} +
                                    "/data"};

//
bool connectToAwsIot(std::size_t retry_count) {
  //
  https_client.setCACert(AWS_IOT_ROOT_CA.data());
  https_client.setCertificate(AWS_IOT_CERTIFICATE.data());
  https_client.setPrivateKey(AWS_IOT_PRIVATE_KEY.data());
  //
  mqtt_client.setServer(AWS_IOT_ENDPOINT.data(), AWS_IOT_MQTT_PORT);
  //
  for (std::size_t retry = 0; retry < retry_count; ++retry) {
    if (mqtt_client.connect(AWS_IOT_DEVICE_ID.data())) {
      ESP_LOGI(TELEMETRY, "MQTT Connected!");
      return true;
    }
    delay(1000);
  }
  return false;
}

//
void checkTelemetry() {
  if (!mqtt_client.connected()) {
    ESP_LOGI(TELEMETRY, "MQTT reconnect");
    connectToAwsIot();
  }
  mqtt_client.loop();
}

//
bool sendTelemetry(const std::string &string_telemetry) {
  ESP_LOGD(TELEMETRY, "%s", string_telemetry.c_str());
  return mqtt_client.publish(MQTT_TOPIC.c_str(), string_telemetry.c_str());
}