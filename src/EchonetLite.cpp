// Copyright (c) 2022 Akihiro Yamamoto.
// Licensed under the MIT License <https://spdx.org/licenses/MIT.html>
// See LICENSE file in the project root for full license information.
//
#include "EchonetLite.hpp"
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <ctime>
#include <iomanip>
#include <iterator>
#include <memory>
#include <queue>
#include <ratio>
#include <sstream>
#include <string>
#include <tuple>
#include <variant>
#include <vector>

// ECHONET Lite フレームからペイロードを作る
std::variant<EchonetLite::SerializeOk, EchonetLite::SerializeError>
EchonetLite::serializeFromEchonetLiteFrame(std::vector<uint8_t> &destination,
                                           const EchonetLiteFrame &frame) {
  std::vector<uint8_t> octets;
  // bytes#1 and bytes#2
  // EHD: ECHONET Lite 電文ヘッダー
  octets.push_back(frame.ehd.u8[0]);
  octets.push_back(frame.ehd.u8[1]);
  // bytes#3 and bytes#4
  // TID: トランザクションID
  octets.push_back(frame.tid.u8[0]);
  octets.push_back(frame.tid.u8[1]);
  //
  // EDATA
  //
  // bytes#5 and bytes#6 and bytes#7
  // SEOJ: メッセージの送り元
  octets.push_back(frame.edata.seoj.s.u8[0]);
  octets.push_back(frame.edata.seoj.s.u8[1]);
  octets.push_back(frame.edata.seoj.s.u8[2]);
  // bytes#8 and bytes#9 and bytes#10
  // DEOJ: メッセージの行き先
  octets.push_back(frame.edata.deoj.d.u8[0]);
  octets.push_back(frame.edata.deoj.d.u8[1]);
  octets.push_back(frame.edata.deoj.d.u8[2]);
  // bytes#11
  // ESV : ECHONET Lite サービスコード
  octets.push_back(static_cast<uint8_t>(frame.edata.esv));
  // bytes#12
  // OPC: 処理プロパティ数
  octets.push_back(static_cast<uint8_t>(frame.edata.props.size()));
  if (frame.edata.opc != frame.edata.props.size()) {
    std::ostringstream ss;
    ss << "size mismatched: " << "OPC:" << +frame.edata.opc
       << ", SIZE():" << +frame.edata.props.size();
    return SerializeError{ss.str()};
  }
  //
  // 以降,ECHONET Liteプロパティ
  //
  // EPC, PDC, EDTを繰り返す
  //
  for (const auto &prop : frame.edata.props) {
    // EPC: ECHONET Liteプロパティ
    octets.push_back(prop.epc);
    // PDC: EDTのバイト数
    octets.push_back(prop.edt.size());
    if (prop.pdc != prop.edt.size()) {
      std::ostringstream ss;
      ss << "size mismatched: " << "PDC:" << +prop.pdc
         << ", SIZE():" << +prop.edt.size();
      return SerializeError{ss.str()};
    }
    // EDT: データ
    std::copy(prop.edt.cbegin(), prop.edt.cend(), std::back_inserter(octets));
  }
  //
  destination.swap(octets);
  return SerializeOk{};
}

// ペイロードからECHONET Lite フレームを取り出す
std::variant<EchonetLite::DeserializeOk, EchonetLite::DeserializeError>
EchonetLite::deserializeToEchonetLiteFrame(EchonetLiteFrame &destination,
                                           const std::vector<uint8_t> &data) {
  EchonetLiteFrame frame;
  auto it = data.cbegin();
  //
  if (data.size() < 12) {
    goto insufficient_inputs;
  }
  // bytes#1 and bytes#2
  // EHD: ECHONET Lite 電文ヘッダー
  frame.ehd = EchonetLiteEHeader({*it++, *it++});
  if (frame.ehd != EchonetLiteEHD) {
    // ECHONET Lite 電文形式でないので
    std::ostringstream ss;
    ss << "Unknown EHD: " << std::string(frame.ehd);
    return DeserializeError{ss.str()};
  }
  // bytes#3 and bytes#4
  // TID: トランザクションID
  frame.tid = EchonetLiteTransactionId({*it++, *it++});
  //
  // EDATA
  //
  // bytes#5 and bytes#6 and bytes#7
  // SEOJ: メッセージの送り元
  frame.edata.seoj =
      EchonetLiteSEOJ(EchonetLiteObjectCode({*it++, *it++, *it++}));
  // bytes#8 and bytes#9 and bytes#10
  // DEOJ: メッセージの行き先
  frame.edata.deoj =
      EchonetLiteDEOJ(EchonetLiteObjectCode({*it++, *it++, *it++}));
  // bytes#11
  // ESV : ECHONET Lite サービスコード
  frame.edata.esv = static_cast<EchonetLiteESV>(*it++);
  // bytes#12
  // OPC: 処理プロパティ数
  frame.edata.opc = *it++;
  //
  // 以降,ECHONET Liteプロパティ
  //
  // EPC, PDC, EDTを繰り返す
  //
  frame.edata.props.reserve(frame.edata.opc);
  for (auto i = 0; i < frame.edata.opc; ++i) {
    EchonetLiteProp prop;
    if (std::distance(it, data.cend()) < 2) {
      goto insufficient_inputs;
    }
    // EPC: ECHONET Liteプロパティ
    prop.epc = *it++;
    // PDC: 続くEDTのバイト数
    prop.pdc = *it++;
    // EDT: ECHONET Liteプロパティ値データ
    if (std::distance(it, data.cend()) < prop.pdc) {
      goto insufficient_inputs;
    }
    // EDTのバイト数ぶんコピーする
    prop.edt.reserve(prop.pdc);
    std::copy_n(it, prop.pdc, std::back_inserter(prop.edt));
    it = std::next(it, prop.pdc);
    //
    frame.edata.props.push_back(std::move(prop));
  }
  //
  destination = frame;
  return DeserializeOk{};

insufficient_inputs:
  std::ostringstream ss;
  ss << "insufficient input. This is " << data.size() << " bytes.";
  return DeserializeError{ss.str()};
}

// ECHONET Lite プロパティから低圧スマート電力量計クラスのデーターを得る
std::variant<EchonetLite::PickupOk, EchonetLite::PickupIgnored,
             EchonetLite::PickupError>
EchonetLite::pickup_electricity_meter_data(const EchonetLiteProp &prop) {
  // EchonetLiteプロパティ
  switch (prop.epc) {
  case 0x80: {                  // 動作状態
    if (prop.edt.size() == 1) { // 1バイト
      enum class OpStatus : uint8_t {
        ON = 0x30,
        OFF = 0x31,
      };
      switch (static_cast<OpStatus>(prop.edt[0])) {
      case OpStatus::ON:
        return PickupIgnored{"operation status : ON"};
      case OpStatus::OFF:
        return PickupIgnored{"operation status : OFF"};
      }
    } else {
      std::ostringstream ss;
      ss << "pdc is should be 1 bytes, this is " << +prop.edt.size()
         << " bytes.";
      return PickupError{ss.str()};
    }
  } break;
  case 0x81: {                  // 設置場所
    if (prop.edt.size() == 1) { // 1バイト
      uint8_t a = prop.edt[0];
      std::ostringstream ss;
      ss << std::hex << "installation location: 0x" << a;
      return PickupIgnored{ss.str()};
    } else if (prop.edt.size() == 17) { // 17バイト
      return PickupIgnored{"installation location"};
    } else {
      std::ostringstream ss;
      ss << "pdc is should be 1 or 17 bytes, this is " << +prop.edt.size()
         << " bytes.";
      return PickupError{ss.str()};
    }
  } break;
  case 0x88: {                  // 異常発生状態
    if (prop.edt.size() == 1) { // 1バイト
      enum class FaultStatus : uint8_t {
        FaultOccurred = 0x41,
        NoFault = 0x42,
      };
      switch (static_cast<FaultStatus>(prop.edt[0])) {
      case FaultStatus::FaultOccurred:
        return PickupIgnored{"FaultStatus::FaultOccurred"};
      case FaultStatus::NoFault:
        return PickupIgnored{"FaultStatus::NoFault"};
      }
    } else {
      std::ostringstream ss;
      ss << "pdc is should be 1 bytes, this is " << +prop.edt.size()
         << " bytes.";
      return PickupError{ss.str()};
    }
  } break;
  case 0x8A: {                  // メーカーコード
    if (prop.edt.size() == 3) { // 3バイト
      uint8_t a = prop.edt[0];
      uint8_t b = prop.edt[1];
      uint8_t c = prop.edt[2];
      std::ostringstream ss;
      ss << "Manufacturer: 0x";
      ss << std::hex << std::setw(2) << a;
      ss << std::hex << std::setw(2) << b;
      ss << std::hex << std::setw(2) << c;
      return PickupIgnored{ss.str()};
    } else {
      std::ostringstream ss;
      ss << "pdc is should be 3 bytes, this is " << +prop.edt.size()
         << " bytes.";
      return PickupError{ss.str()};
    }
  } break;
  case 0xD3: { // 係数
    auto coeff = ElectricityMeter::Coefficient{};
    if (prop.edt.size() == 4) { // 4バイト
      coeff = ElectricityMeter::Coefficient(
          {prop.edt[0], prop.edt[1], prop.edt[2], prop.edt[3]});
    } else {
      // 係数が無い場合は1倍となる
      coeff = ElectricityMeter::Coefficient{};
    }
    return PickupOk{coeff};
  } break;
  case 0xD7: {                  // 積算電力量有効桁数
    if (prop.edt.size() == 1) { // 1バイト
      auto digits = ElectricityMeter::EffectiveDigits(prop.edt[0]);
      return PickupOk{digits};
    } else {
      std::ostringstream ss;
      ss << "pdc is should be 1 bytes, this is " << +prop.edt.size()
         << " bytes.";
      return PickupError{ss.str()};
    }
  } break;
  case 0xE1: { // 積算電力量単位 (正方向、逆方向計測値)
    if (prop.edt.size() == 1) { // 1バイト
      auto unit = ElectricityMeter::Unit(prop.edt[0]);
      if (auto desc = unit.get_description()) {
        /* nothing to do */
      } else {
        return PickupError{"invalid unit."};
      }
      return PickupOk{unit};
    } else {
      std::ostringstream ss;
      ss << "pdc is should be 1 bytes, this is " << +prop.edt.size()
         << " bytes.";
      return PickupError{ss.str()};
    }
  } break;
  case 0xE5: {                  // 積算履歴収集日１
    if (prop.edt.size() == 1) { // 1バイト
      uint8_t day = prop.edt[0];
      std::ostringstream ss;
      ss << "day of historical 1: " << +day;
      return PickupIgnored{ss.str()};
    } else {
      std::ostringstream ss;
      ss << "pdc is should be 1 bytes, this is " << +prop.edt.size()
         << " bytes.";
      return PickupError{ss.str()};
    }
  } break;
  case 0xE7: {                  // 瞬時電力値
    if (prop.edt.size() == 4) { // 4バイト
      auto watt = ElectricityMeter::InstantWatt(
          {prop.edt[0], prop.edt[1], prop.edt[2], prop.edt[3]});
      return PickupOk{watt};
    } else {
      std::ostringstream ss;
      ss << "pdc is should be 4 bytes, this is " << +prop.edt.size()
         << " bytes.";
      return PickupError{ss.str()};
    }
  } break;
  case 0xE8: {                  // 瞬時電流値
    if (prop.edt.size() == 4) { // 4バイト
      auto ampere = ElectricityMeter::InstantAmpere(
          {prop.edt[0], prop.edt[1], prop.edt[2], prop.edt[3]});
      return PickupOk{ampere};
    } else {
      std::ostringstream ss;
      ss << "pdc is should be 4 bytes, this is " << +prop.edt.size()
         << " bytes.";
      return PickupError{ss.str()};
    }
  } break;
  case 0xEA: {                   // 定時積算電力量
    if (prop.edt.size() == 11) { // 11バイト
      std::array<uint8_t, 11> memory;
      std::copy_n(prop.edt.begin(), memory.size(), memory.begin());
      //
      auto cwh = ElectricityMeter::CumulativeWattHour(memory);
      return PickupOk{cwh};
    } else {
      std::ostringstream ss;
      ss << "pdc is should be 11 bytes, this is " << +prop.edt.size()
         << " bytes.";
      return PickupError{ss.str()};
    }
  } break;
  case 0xED: {                  // 積算履歴収集日２
    if (prop.edt.size() == 7) { // 7バイト
      uint8_t a = prop.edt[0];
      uint8_t b = prop.edt[1];
      uint8_t c = prop.edt[2];
      uint8_t d = prop.edt[3];
      uint8_t e = prop.edt[4];
      uint8_t f = prop.edt[5];
      uint8_t g = prop.edt[6];
      std::ostringstream ss;
      ss << "day of historical 2: [";
      ss << std::hex << std::setw(2) << a << ", ";
      ss << std::hex << std::setw(2) << b << ", ";
      ss << std::hex << std::setw(2) << c << ", ";
      ss << std::hex << std::setw(2) << d << ", ";
      ss << std::hex << std::setw(2) << e << ", ";
      ss << std::hex << std::setw(2) << f << ", ";
      ss << std::hex << std::setw(2) << g << "]";
      return PickupIgnored{ss.str()};
    } else {
      std::ostringstream ss;
      ss << "pdc is should be 7 bytes, this is " << +prop.edt.size()
         << " bytes.";
      return PickupError{ss.str()};
    }
  } break;
  default:
    std::ostringstream ss;
    ss << std::hex << "unknown epc: 0x" << +prop.epc;
    return PickupError{ss.str()};
    break;
  }
  //
  return PickupError{"unknown"};
}

// 積算電力量
ElectricityMeter::KiloWattHour
EchonetLite::cumlative_kilo_watt_hour(ElectricityMeter::CumulativeWattHour cwh,
                                      ElectricityMeter::Coefficient coeff,
                                      ElectricityMeter::Unit unit) {
  // 係数
  auto powers_of_10 = unit.get_powers_of_10().value_or(0);
  // KWhへの乗数
  auto multiplier = std::pow(10, powers_of_10);
  return ElectricityMeter::KiloWattHour{
      coeff.coefficient * cwh.raw_cumlative_watt_hour() * multiplier};
}

// 電力量
std::string EchonetLite::to_string_cumlative_kilo_watt_hour(
    ElectricityMeter::CumulativeWattHour cwh,
    std::optional<ElectricityMeter::Coefficient> opt_coeff,
    ElectricityMeter::Unit unit) {
  // 係数(無い場合の係数は1)
  uint8_t coeff = (opt_coeff.has_value()) ? opt_coeff.value().coefficient : 1;
  //
  int32_t cumulative_watt_hour = coeff * cwh.raw_cumlative_watt_hour();
  // 文字にする
  std::string str_kwh = std::to_string(cumulative_watt_hour);
  // 1kwhの位置に小数点を移動する
  auto powers_of_10 = unit.get_powers_of_10().value_or(0);
  if (powers_of_10 > 0) { // 小数点を右に移動する
    // '0'を必要な数だけ用意して
    std::string zeros(powers_of_10, '0');
    // 必要な'0'を入れた後に小数点を入れる
    str_kwh += zeros + ".";
  } else if (powers_of_10 == 0) { // 10の0乗ってのは1だから
    // 小数点はここに入る
    str_kwh += ".";
  } else if (powers_of_10 < 0) { // 小数点を左に移動する
    // powers_of_10は負数だからend()に足し算すると減る
    auto it = str_kwh.end() + powers_of_10;
    // 小数点を挿入する
    str_kwh.insert(it, '.');
  }
  return str_kwh;
}
