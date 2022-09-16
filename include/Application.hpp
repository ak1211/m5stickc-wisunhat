// Copyright (c) 2022 Akihiro Yamamoto.
// Licensed under the MIT License <https://spdx.org/licenses/MIT.html>
// See LICENSE file in the project root for full license information.
//
#pragma once
#include <string>

// Wifi
extern std::string_view WIFI_SSID;
extern std::string_view WIFI_PASSWORD;
// AWS IoT
extern std::string_view AWS_IOT_DEVICE_ID;
extern std::string_view AWS_IOT_ROOT_CA;
extern std::string_view AWS_IOT_CERTIFICATE;
extern std::string_view AWS_IOT_PRIVATE_KEY;
extern std::string_view AWS_IOT_ENDPOINT;
extern const uint8_t AWS_IOT_QOS;

// データーベースのパーティションキーであるセンサーＩＤ
const std::string_view SENSOR_ID{"smartmeter"};

// ログ出し用
const char MAIN[] = "MAIN";
const char SEND[] = "SEND";
const char RECEIVE[] = "RECEIVE";
const char TELEMETRY[] = "TELEMETRY";
