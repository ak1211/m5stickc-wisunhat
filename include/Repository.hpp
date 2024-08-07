// Copyright (c) 2022 Akihiro Yamamoto.
// Licensed under the MIT License <https://spdx.org/licenses/MIT.html>
// See LICENSE file in the project root for full license information.
//
#pragma once
#include "EchonetLite.hpp"
#include <chrono>
#include <memory>
#include <optional>
#include <tuple>

namespace Repository {
using InstantAmpere = std::pair<std::chrono::system_clock::time_point,
                                ElectricityMeter::InstantAmpere>;
//
using InstantWatt = std::pair<std::chrono::system_clock::time_point,
                              ElectricityMeter::InstantWatt>;
//
using CumlativeWattHour =
    std::tuple<ElectricityMeter::CumulativeWattHour,
               ElectricityMeter::Coefficient, ElectricityMeter::Unit>;
//
struct ElectricPowerData {
  // 乗数(無い場合の乗数は1)
  std::optional<ElectricityMeter::Coefficient> whm_coefficient{};
  // 単位
  std::optional<ElectricityMeter::Unit> whm_unit{};
  // 積算履歴収集日
  std::optional<uint8_t> day_for_which_the_historcal{};
  // 瞬時電力
  std::optional<InstantWatt> instant_watt{};
  // 瞬時電流
  std::optional<InstantAmpere> instant_ampere{};
  // 定時積算電力量
  std::optional<CumlativeWattHour> cumlative_watt_hour{};
};
} // namespace Repository