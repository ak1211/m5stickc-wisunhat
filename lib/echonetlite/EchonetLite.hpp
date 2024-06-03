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
  static std::variant<DeserializeOk, DeserializeError>
  deserializeToEchonetLiteFrame(EchonetLiteFrame &destination,
                                const std::vector<uint8_t> &data);
  // スマートメーターから受信した値
  using ElectricityMeterData = std::variant<
      ElectricityMeter::Coefficient, ElectricityMeter::EffectiveDigits,
      ElectricityMeter::Unit, ElectricityMeter::InstantWatt,
      ElectricityMeter::InstantAmpere, ElectricityMeter::CumulativeWattHour>;
  //
  struct PickupOk {
    ElectricityMeterData data;
    PickupOk(ElectricityMeterData in) : data{in} {};
  };
  struct PickupIgnored {
    std::string message;
    PickupIgnored(std::string in) : message{in} {}
  };
  struct PickupError {
    std::string reason;
    PickupError(std::string in) : reason{in} {}
  };
  // ECHONET Lite フレームから低圧スマート電力量計クラスのデーターを得る
  static std::variant<PickupOk, PickupIgnored, PickupError>
  pickup_electricity_meter_data(const EchonetLiteProp &prop);
  // 積算電力量
  static ElectricityMeter::KiloWattHour
  cumlative_kilo_watt_hour(ElectricityMeter::CumulativeWattHour cwh,
                           ElectricityMeter::Coefficient coeff,
                           ElectricityMeter::Unit unit);
  // 電力量
  static std::string to_string_cumlative_kilo_watt_hour(
      ElectricityMeter::CumulativeWattHour cwh,
      std::optional<ElectricityMeter::Coefficient> opt_coeff,
      ElectricityMeter::Unit unit);
};