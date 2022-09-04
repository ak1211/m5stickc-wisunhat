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

// time zone = Asia_Tokyo(UTC+9)
static constexpr char TZ_TIME_ZONE[] = "JST-9";

// MQTT
static WiFiClientSecure https_client;
static PubSubClient mqtt_client(https_client);

static const std::string MQTT_TOPIC{"device/" + std::string{AWS_IOT_DEVICE_ID} +
                                    "/data"};

//
static void connectToWiFi() {
  if (WiFi.status() == WL_CONNECTED) {
    ESP_LOGD(TELEMETRY, "WIFI connected, pass");
    return;
  }
  ESP_LOGI(TELEMETRY, "Connecting to WIFI SSID %s", WIFI_SSID.data());

  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID.data(), WIFI_PASSWORD.data());
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  Serial.println("");

  ESP_LOGI(TELEMETRY, "WiFi connected, IP address: %s",
           WiFi.localIP().toString().c_str());
}

//
static bool initializeTime(std::size_t retry_count = 100) {
  ESP_LOGI(TELEMETRY, "Setting time using SNTP");

  configTzTime(TZ_TIME_ZONE, "ntp.nict.jp", "time.google.com",
               "ntp.jst.mfeed.ad.jp");
  //
  for (std::size_t retry = 0; retry < retry_count; ++retry) {
    delay(500);
    if (sntp_get_sync_status() == SNTP_SYNC_STATUS_COMPLETED) {
      char buf[50];
      time_t now = time(nullptr);
      ESP_LOGI(TELEMETRY, "local time: \"%s\"",
               asctime_r(localtime(&now), buf));
      ESP_LOGI(TELEMETRY, "Time initialized!");
      return true;
    }
  }
  //
  ESP_LOGE(TELEMETRY, "SNTP sync failed");
  return false;
}

//
static bool connectToAwsIot(std::size_t retry_count = 100) {
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

bool establishConnection() {
  connectToWiFi();
  bool ok = true;
  ok = ok ? initializeTime() : ok;
  ok = ok ? connectToAwsIot() : ok;
  return ok;
}

//
void checkWiFi() {
  constexpr std::clock_t INTERVAL = 30 * CLOCKS_PER_SEC;
  static std::clock_t previous = 0L;
  std::clock_t current = std::clock();

  if (current - previous < INTERVAL) {
    return;
  }

  if (WiFi.status() != WL_CONNECTED) {
    ESP_LOGI(TELEMETRY, "WiFi reconnect");
    WiFi.disconnect();
    WiFi.reconnect();
    establishConnection();
  }
  previous = current;
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
