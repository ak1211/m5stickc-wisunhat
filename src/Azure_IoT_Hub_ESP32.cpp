// Copyright (c) Microsoft Corporation. All rights reserved.
// SPDX-License-Identifier: MIT

/*
 * This is an Arduino-based Azure IoT Hub sample for ESPRESSIF ESP32 boards.
 * It uses our Azure Embedded SDK for C to help interact with Azure IoT.
 * For reference, please visit https://github.com/azure/azure-sdk-for-c.
 *
 * To connect and work with Azure IoT Hub you need an MQTT client, connecting,
 * subscribing and publishing to specific topics to use the messaging features
 * of the hub. Our azure-sdk-for-c is an MQTT client support library, helping
 * composing and parsing the MQTT topic names and messages exchanged with the
 * Azure IoT Hub.
 *
 * This sample performs the following tasks:
 * - Synchronize the device clock with a NTP server;
 * - Initialize our "az_iot_hub_client" (struct for data, part of our
 * azure-sdk-for-c);
 * - Initialize the MQTT client (here we use ESPRESSIF's esp_mqtt_client, which
 * also handle the tcp connection and TLS);
 * - Connect the MQTT client (using server-certificate validation, SAS-tokens
 * for client authentication);
 * - Periodically send telemetry data to the Azure IoT Hub.
 *
 * To properly connect to your Azure IoT Hub, please fill the information in the
 * `iot_configs.h` file.
 */

//
#include <Azure_IoT_Hub_ESP32>
// ログ出し用
#define TELEMETRY "TELEMETRY"

// C99 libraries
#include <cstdlib>
#include <cstring>
#include <ctime>

// Libraries for MQTT client and WiFi connection
#include <WiFi.h>
#include <esp_sntp.h>
#include <mqtt_client.h>

// Azure IoT SDK for C includes
extern "C" {
#include <az_core.h>
#include <az_iot.h>
#include <azure_ca.h>
}

// Additional sample headers
#include "AzIoTSasToken.h"

// When developing for your own Arduino-based platform,
// please follow the format '(ard;<platform>)'.
#define AZURE_SDK_CLIENT_USER_AGENT "c%2F" AZ_SDK_VERSION_STRING "(ard;esp32)"

// Utility macros and defines
#define sizeofarray(a) (sizeof(a) / sizeof(a[0]))
#define MQTT_QOS1 1
#define DO_NOT_RETAIN_MSG 0
#define SAS_TOKEN_DURATION_IN_MINUTES 60
#define UNIX_TIME_NOV_13_2017 1510592825

#define TZ_TIME_ZONE "JST-9"

// Translate iot_configs.h defines into variables used by the sample
static const std::string ssid = std::string(IOT_CONFIG_WIFI_SSID);
static const std::string password = std::string(IOT_CONFIG_WIFI_PASSWORD);
static const std::string host = std::string(IOT_CONFIG_IOTHUB_FQDN);
static const std::string mqtt_broker_uri =
    std::string("mqtts://" + std::string(IOT_CONFIG_IOTHUB_FQDN));
static const std::string device_id = std::string(IOT_CONFIG_DEVICE_ID);
static const std::string device_key = std::string(IOT_CONFIG_DEVICE_KEY);
static const int mqtt_port = AZ_IOT_DEFAULT_MQTT_CONNECT_PORT;

// Memory allocated for the sample's variables and structures.
static esp_mqtt_client_handle_t mqtt_client;
static az_iot_hub_client client;

static char mqtt_client_id[128];
static char mqtt_username[128];
static char mqtt_password[200];
static uint8_t sas_signature_buffer[256];
static unsigned long next_telemetry_send_time_ms = 0;
static char telemetry_topic[128];
static uint8_t telemetry_payload[256];

#define INCOMING_DATA_BUFFER_SIZE 128
static char incoming_data[INCOMING_DATA_BUFFER_SIZE];

// Auxiliary functions
#ifndef IOT_CONFIG_USE_X509_CERT
static AzIoTSasToken sasToken(&client,
                              az_span_create((uint8_t *)device_key.c_str(),
                                             device_key.length()),
                              AZ_SPAN_FROM_BUFFER(sas_signature_buffer),
                              AZ_SPAN_FROM_BUFFER(mqtt_password));
#endif // IOT_CONFIG_USE_X509_CERT

static void connectToWiFi() {
  ESP_LOGI(TELEMETRY, "Connecting to WIFI SSID %s", ssid.c_str());

  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid.c_str(), password.c_str());
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  Serial.println("");

  ESP_LOGI(TELEMETRY, "WiFi connected, IP address: %s",
           WiFi.localIP().toString().c_str());
}

static void initializeTime() {
  ESP_LOGI(TELEMETRY, "Setting time using SNTP");

  configTzTime(TZ_TIME_ZONE, "ntp.nict.jp", "time.google.com",
               "ntp.jst.mfeed.ad.jp");
  time_t now = time(NULL);
  while (now < UNIX_TIME_NOV_13_2017) {
    delay(500);
    Serial.print(".");
    now = time(nullptr);
  }
  Serial.println("");
  char buf[50];
  ESP_LOGI(TELEMETRY, "local time: \"%s\"", asctime_r(localtime(&now), buf));
  ESP_LOGI(TELEMETRY, "Time initialized!");
}

void receivedCallback(char *topic, byte *payload, unsigned int length) {
  ESP_LOGI(TELEMETRY, "Received [");
  ESP_LOGI(TELEMETRY, "%s", topic);
  ESP_LOGI(TELEMETRY, "]: ");
  for (int i = 0; i < length; i++) {
    Serial.print((char)payload[i]);
  }
  Serial.println("");
}

static esp_err_t mqtt_event_handler(esp_mqtt_event_handle_t event) {
  switch (event->event_id) {
    int i, r;

  case MQTT_EVENT_ERROR:
    ESP_LOGI(TELEMETRY, "MQTT event MQTT_EVENT_ERROR");
    break;
  case MQTT_EVENT_CONNECTED:
    ESP_LOGI(TELEMETRY, "MQTT event MQTT_EVENT_CONNECTED");

    r = esp_mqtt_client_subscribe(mqtt_client,
                                  AZ_IOT_HUB_CLIENT_C2D_SUBSCRIBE_TOPIC, 1);
    if (r == -1) {
      ESP_LOGE(TELEMETRY, "Could not subscribe for cloud-to-device messages.");
    } else {
      ESP_LOGI(TELEMETRY,
               "Subscribed for cloud-to-device messages; message id:%d", r);
    }

    break;
  case MQTT_EVENT_DISCONNECTED:
    ESP_LOGI(TELEMETRY, "MQTT event MQTT_EVENT_DISCONNECTED");
    break;
  case MQTT_EVENT_SUBSCRIBED:
    ESP_LOGI(TELEMETRY, "MQTT event MQTT_EVENT_SUBSCRIBED");
    break;
  case MQTT_EVENT_UNSUBSCRIBED:
    ESP_LOGI(TELEMETRY, "MQTT event MQTT_EVENT_UNSUBSCRIBED");
    break;
  case MQTT_EVENT_PUBLISHED:
    ESP_LOGI(TELEMETRY, "MQTT event MQTT_EVENT_PUBLISHED");
    break;
  case MQTT_EVENT_DATA:
    ESP_LOGI(TELEMETRY, "MQTT event MQTT_EVENT_DATA");

    for (i = 0; i < (INCOMING_DATA_BUFFER_SIZE - 1) && i < event->topic_len;
         i++) {
      incoming_data[i] = event->topic[i];
    }
    incoming_data[i] = '\0';
    ESP_LOGI(TELEMETRY, "Topic: %s", incoming_data);

    for (i = 0; i < (INCOMING_DATA_BUFFER_SIZE - 1) && i < event->data_len;
         i++) {
      incoming_data[i] = event->data[i];
    }
    incoming_data[i] = '\0';
    ESP_LOGI(TELEMETRY, "Data: %s", incoming_data);

    break;
  case MQTT_EVENT_BEFORE_CONNECT:
    ESP_LOGI(TELEMETRY, "MQTT event MQTT_EVENT_BEFORE_CONNECT");
    break;
  default:
    ESP_LOGE(TELEMETRY, "MQTT event UNKNOWN");
    break;
  }

  return ESP_OK;
}

static void initializeIoTHubClient() {
  az_iot_hub_client_options options = az_iot_hub_client_options_default();
  options.user_agent = AZ_SPAN_FROM_STR(AZURE_SDK_CLIENT_USER_AGENT);

  if (az_result_failed(az_iot_hub_client_init(
          &client, az_span_create((uint8_t *)host.c_str(), host.length()),
          az_span_create((uint8_t *)device_id.c_str(), device_id.length()),
          &options))) {
    ESP_LOGE(TELEMETRY, "Failed initializing Azure IoT Hub client");
    return;
  }

  size_t client_id_length;
  if (az_result_failed(az_iot_hub_client_get_client_id(
          &client, mqtt_client_id, sizeof(mqtt_client_id) - 1,
          &client_id_length))) {
    ESP_LOGE(TELEMETRY, "Failed getting client id");
    return;
  }

  if (az_result_failed(az_iot_hub_client_get_user_name(
          &client, mqtt_username, sizeofarray(mqtt_username), NULL))) {
    ESP_LOGE(TELEMETRY, "Failed to get MQTT clientId, return code");
    return;
  }

  ESP_LOGI(TELEMETRY, "Client ID: %s", mqtt_client_id);
  ESP_LOGI(TELEMETRY, "Username: %s", mqtt_username);
}

static int initializeMqttClient() {
#ifndef IOT_CONFIG_USE_X509_CERT
  if (sasToken.Generate(SAS_TOKEN_DURATION_IN_MINUTES) != 0) {
    ESP_LOGE(TELEMETRY, "Failed generating SAS token");
    return 1;
  }
#endif

  esp_mqtt_client_config_t mqtt_config;
  memset(&mqtt_config, 0, sizeof(mqtt_config));
  mqtt_config.uri = mqtt_broker_uri.c_str();
  mqtt_config.port = mqtt_port;
  mqtt_config.client_id = mqtt_client_id;
  mqtt_config.username = mqtt_username;

#ifdef IOT_CONFIG_USE_X509_CERT
  ESP_LOGI(TELEMETRY, "MQTT client using X509 Certificate authentication");
  mqtt_config.client_cert_pem = IOT_CONFIG_DEVICE_CERT;
  mqtt_config.client_key_pem = IOT_CONFIG_DEVICE_CERT_PRIVATE_KEY;
#else // Using SAS key
  mqtt_config.password = (const char *)az_span_ptr(sasToken.Get());
#endif

  mqtt_config.keepalive = 30;
  mqtt_config.disable_clean_session = 0;
  mqtt_config.disable_auto_reconnect = false;
  mqtt_config.event_handle = mqtt_event_handler;
  mqtt_config.user_context = NULL;
  mqtt_config.cert_pem = (const char *)ca_pem;

  mqtt_client = esp_mqtt_client_init(&mqtt_config);

  if (mqtt_client == NULL) {
    ESP_LOGE(TELEMETRY, "Failed creating mqtt client");
    return 1;
  }

  esp_err_t start_result = esp_mqtt_client_start(mqtt_client);

  if (start_result != ESP_OK) {
    ESP_LOGE(TELEMETRY, "Could not start mqtt client; error code:%d",
             start_result);
    return 1;
  } else {
    ESP_LOGI(TELEMETRY, "MQTT client started");
    return 0;
  }
}

bool establishConnection() {
  connectToWiFi();
  initializeTime();
  initializeIoTHubClient();
  if (initializeMqttClient() == 0) {
    return true;
  }
  return false;
}

static void getTelemetryPayload(const std::string &string_telemetry,
                                az_span payload, az_span *out_payload) {
  az_span original_payload = payload;

  payload =
      az_span_copy(payload, az_span_create((uint8_t *)string_telemetry.c_str(),
                                           string_telemetry.length()));
  payload = az_span_copy_u8(payload, '\0');

  *out_payload =
      az_span_slice(original_payload, 0,
                    az_span_size(original_payload) - az_span_size(payload) - 1);
}

void sendTelemetry(const std::string &string_telemetry) {
  az_span telemetry = AZ_SPAN_FROM_BUFFER(telemetry_payload);

  ESP_LOGI(TELEMETRY, "Sending telemetry ...");

  // The topic could be obtained just once during setup,
  // however if properties are used the topic need to be generated again to
  // reflect the current values of the properties.
  if (az_result_failed(az_iot_hub_client_telemetry_get_publish_topic(
          &client, NULL, telemetry_topic, sizeof(telemetry_topic), NULL))) {
    ESP_LOGE(TELEMETRY, "Failed az_iot_hub_client_telemetry_get_publish_topic");
    return;
  }

  getTelemetryPayload(string_telemetry, telemetry, &telemetry);

  if (esp_mqtt_client_publish(
          mqtt_client, telemetry_topic, (const char *)az_span_ptr(telemetry),
          az_span_size(telemetry), MQTT_QOS1, DO_NOT_RETAIN_MSG) == 0) {
    ESP_LOGE(TELEMETRY, "Failed publishing");
  } else {
    ESP_LOGI(TELEMETRY, "Message published successfully");
  }
}

void telemetry_loop() {
  if (WiFi.status() != WL_CONNECTED) {
    connectToWiFi();
  }
#ifndef IOT_CONFIG_USE_X509_CERT
  else if (sasToken.IsExpired()) {
    ESP_LOGI(TELEMETRY, "SAS token expired; reconnecting with a new one.");
    (void)esp_mqtt_client_destroy(mqtt_client);
    initializeMqttClient();
  }
#endif
}
