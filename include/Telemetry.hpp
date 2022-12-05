// Copyright (c) 2022 Akihiro Yamamoto.
// Licensed under the MIT License <https://spdx.org/licenses/MIT.html>
// See LICENSE file in the project root for full license information.
//
#pragma once
#include <chrono>
#include <cstdint>
#include <string>

//
extern bool connectToAwsIot(std::size_t retry_count = 100);
extern bool sendTelemetry(const std::string &string_telemetry);
extern bool checkTelemetry(std::chrono::seconds timeout);
extern bool loopTelemetry();
