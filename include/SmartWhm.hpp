// Copyright (c) 2022 Akihiro Yamamoto.
// Licensed under the MIT License <https://spdx.org/licenses/MIT.html>
// See LICENSE file in the project root for full license information.
//
#pragma once
#include <optional>

#include "Application.hpp"
#include "EchonetLite.hpp"

//
// 接続相手のスマートメーター
//
struct SmartWhm {
  std::optional<SmartElectricEnergyMeter::Coefficient>
      whm_coefficient; // 乗数(無い場合の乗数は1)
  std::optional<SmartElectricEnergyMeter::Unit> whm_unit; // 単位
  //  std::optional<uint8_t> day_for_which_the_historcal; // 積算履歴収集日
  std::optional<SmartElectricEnergyMeter::InstantWatt> instant_watt; // 瞬時電力
  std::optional<SmartElectricEnergyMeter::InstantAmpere>
      instant_ampere; // 瞬時電流
  std::optional<SmartElectricEnergyMeter::CumulativeWattHour>
      cumlative_watt_hour; // 定時積算電力量
};
