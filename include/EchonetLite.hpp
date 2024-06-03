// Copyright (c) 2022 Akihiro Yamamoto.
// Licensed under the MIT License <https://spdx.org/licenses/MIT.html>
// See LICENSE file in the project root for full license information.
//
#pragma once
#include "EchonetLite_TypeDefine.hpp"
#include <optional>
#include <variant>
#include <vector>

class EchonetLite {
public:
  //
  struct SerializeOk {};
  struct SerializeError {
    std::string reason;
    SerializeError(std::string in) : reason{in} {}
  };
  // ECHONET Lite フレームからペイロードを作る
  static std::variant<SerializeOk, SerializeError>
  serializeFromEchonetLiteFrame(std::vector<uint8_t> &destination,
                                const EchonetLiteFrame &frame);
  //
  struct DeserializeOk {};
  struct DeserializeError {
    std::string reason;
    DeserializeError(std::string in) : reason{in} {}
  };
  // ペイロードからECHONET Lite フレームを取り出す
  static std::variant<DeserializeOk, DeserializeError> deserializeToEchonetLiteFrame(EchonetLiteFrame &destination, const std::vector<uint8_t> &data);
  // 低圧スマート電力量計クラスのイベントを処理する
  static std::vector<SmartElectricEnergyMeter::ReceivedMessage>
  process_echonet_lite_frame(const EchonetLiteFrame &frame);
  // 積算電力量
  static SmartElectricEnergyMeter::KiloWattHour cumlative_kilo_watt_hour(
      std::tuple<SmartElectricEnergyMeter::CumulativeWattHour,
                 SmartElectricEnergyMeter::Coefficient,
                 SmartElectricEnergyMeter::Unit>
          in);
  // 電力量
  static std::string to_string_cumlative_kilo_watt_hour(
      SmartElectricEnergyMeter::CumulativeWattHour cwh,
      std::optional<SmartElectricEnergyMeter::Coefficient> opt_coeff,
      SmartElectricEnergyMeter::Unit unit);
};