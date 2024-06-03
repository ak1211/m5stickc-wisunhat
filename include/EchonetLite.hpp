// Copyright (c) 2022 Akihiro Yamamoto.
// Licensed under the MIT License <https://spdx.org/licenses/MIT.html>
// See LICENSE file in the project root for full license information.
//
#pragma once
#include "EchonetLite_TypeDefine.hpp"
#include <optional>
#include <vector>

// ECHONET Lite フレームからペイロードを作る
extern std::vector<uint8_t>
serializeFromEchonetLiteFrame(const EchonetLiteFrame &frame);

// ペイロードからECHONET Lite フレームを取り出す
extern std::optional<EchonetLiteFrame>
deserializeToEchonetLiteFrame(const std::vector<uint8_t> &data);

// 低圧スマート電力量計クラスのイベントを処理する
extern std::vector<SmartElectricEnergyMeter::ReceivedMessage>
process_echonet_lite_frame(const EchonetLiteFrame &frame);

// 積算電力量
extern SmartElectricEnergyMeter::KiloWattHour cumlative_kilo_watt_hour(
    std::tuple<SmartElectricEnergyMeter::CumulativeWattHour,
               SmartElectricEnergyMeter::Coefficient,
               SmartElectricEnergyMeter::Unit>
        in);

// 電力量
extern std::string to_string_cumlative_kilo_watt_hour(
    SmartElectricEnergyMeter::CumulativeWattHour cwh,
    std::optional<SmartElectricEnergyMeter::Coefficient> opt_coeff,
    SmartElectricEnergyMeter::Unit unit);
