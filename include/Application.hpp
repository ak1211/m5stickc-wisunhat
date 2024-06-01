// Copyright (c) 2022 Akihiro Yamamoto.
// Licensed under the MIT License <https://spdx.org/licenses/MIT.html>
// See LICENSE file in the project root for full license information.
//
#pragma once
#include "EchonetLite.hpp"
#include "Repository.hpp"
#include "Telemetry.hpp"
#include <chrono>
#include <memory>
#include <optional>
#include <tuple>

//
//
//
class Application final {
public:
  static Repository::ElectricPowerData &getElectricPowerData() {
    return _electric_power_data;
  }
  static Telemetry &getTelemetry();

private:
  static Repository::ElectricPowerData _electric_power_data;
};
