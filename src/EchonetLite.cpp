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

#include <M5Unified.h>

// ECHONET Lite フレームからペイロードを作る
std::vector<uint8_t>
serializeFromEchonetLiteFrame(const EchonetLiteFrame &frame) {
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
    M5_LOGD("size mismatched: OPC:%d, SIZE():%d", frame.edata.opc,
            frame.edata.props.size());
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
      M5_LOGD("size mismatched: PDC:%d, SIZE():%d", prop.pdc, prop.edt.size());
    }
    // EDT: データ
    std::copy(prop.edt.cbegin(), prop.edt.cend(), std::back_inserter(octets));
  }
  return octets;
}

// ペイロードからECHONET Lite フレームを取り出す
std::optional<EchonetLiteFrame>
deserializeToEchonetLiteFrame(const std::vector<uint8_t> &data) {
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
    M5_LOGD("Unknown EHD: %s", std::string(frame.ehd).c_str());
    return std::nullopt;
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
  return std::make_optional(frame);
insufficient_inputs:
  M5_LOGD("insufficient input. This is %d bytes.", data.size());
  return std::nullopt;
}

//
// 低圧スマート電力量計クラスのイベントを処理する
//
std::vector<SmartElectricEnergyMeter::ReceivedMessage>
process_echonet_lite_frame(const EchonetLiteFrame &frame) {
  std::vector<SmartElectricEnergyMeter::ReceivedMessage> result{};
  // EDATAは複数送られてくる
  for (const EchonetLiteProp &prop : frame.edata.props) {
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
          M5_LOGD("operation status : ON");
          break;
        case OpStatus::OFF:
          M5_LOGD("operation status : OFF");
          break;
        }
      } else {
        M5_LOGD("pdc is should be 1 bytes, this is "
                "%d bytes.",
                prop.edt.size());
      }
    } break;
    case 0x81: {                  // 設置場所
      if (prop.edt.size() == 1) { // 1バイト
        uint8_t a = prop.edt[0];
        M5_LOGD("installation location: 0x%02X", a);
      } else if (prop.edt.size() == 17) { // 17バイト
        M5_LOGD("installation location");
      } else {
        M5_LOGD("pdc is should be 1 or 17 bytes, "
                "this is %d bytes.",
                prop.edt.size());
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
          M5_LOGD("FaultStatus::FaultOccurred");
          break;
        case FaultStatus::NoFault:
          M5_LOGD("FaultStatus::NoFault");
          break;
        }
      } else {
        M5_LOGD("pdc is should be 1 bytes, this is "
                "%d bytes.",
                prop.edt.size());
      }
    } break;
    case 0x8A: {                  // メーカーコード
      if (prop.edt.size() == 3) { // 3バイト
        uint8_t a = prop.edt[0];
        uint8_t b = prop.edt[1];
        uint8_t c = prop.edt[2];
        M5_LOGD("Manufacturer: 0x%02X%02X%02X", a, b, c);
      } else {
        M5_LOGD("pdc is should be 3 bytes, this is "
                "%d bytes.",
                prop.edt.size());
      }
    } break;
    case 0xD3: { // 係数
      auto coeff = SmartElectricEnergyMeter::Coefficient{};
      if (prop.edt.size() == 4) { // 4バイト
        coeff = SmartElectricEnergyMeter::Coefficient(
            {prop.edt[0], prop.edt[1], prop.edt[2], prop.edt[3]});
      } else {
        // 係数が無い場合は1倍となる
        coeff = SmartElectricEnergyMeter::Coefficient{};
      }
      M5_LOGD("coefficient the %d", +coeff.coefficient);
      result.push_back(coeff);
    } break;
    case 0xD7: {                  // 積算電力量有効桁数
      if (prop.edt.size() == 1) { // 1バイト
        auto digits = SmartElectricEnergyMeter::EffectiveDigits(prop.edt[0]);
        M5_LOGD("%d effective digits.", +digits.digits);
        result.push_back(digits);
      } else {
        M5_LOGD("pdc is should be 1 bytes, this is "
                "%d bytes.",
                prop.edt.size());
      }
    } break;
    case 0xE1: { // 積算電力量単位 (正方向、逆方向計測値)
      if (prop.edt.size() == 1) { // 1バイト
        auto unit = SmartElectricEnergyMeter::Unit(prop.edt[0]);
        if (auto desc = unit.get_description()) {
          M5_LOGD("value %s", desc->c_str());
        } else {
          M5_LOGD("invalid unit.");
        }
        result.push_back(unit);
      } else {
        M5_LOGD("pdc is should be 1 bytes, this is "
                "%d bytes.",
                prop.edt.size());
      }
    } break;
    case 0xE5: {                  // 積算履歴収集日１
      if (prop.edt.size() == 1) { // 1バイト
        uint8_t day = prop.edt[0];
        M5_LOGD("day of historical 1: (%d)", day);
      } else {
        M5_LOGD("pdc is should be 1 bytes, this is "
                "%d bytes.",
                prop.edt.size());
      }
    } break;
    case 0xE7: {                  // 瞬時電力値
      if (prop.edt.size() == 4) { // 4バイト
        auto watt = SmartElectricEnergyMeter::InstantWatt(
            {prop.edt[0], prop.edt[1], prop.edt[2], prop.edt[3]});
        M5_LOGD("%s", to_string(watt).c_str());
        result.push_back(watt);
      } else {
        M5_LOGD("pdc is should be 4 bytes, this is "
                "%d bytes.",
                prop.edt.size());
      }
    } break;
    case 0xE8: {                  // 瞬時電流値
      if (prop.edt.size() == 4) { // 4バイト
        auto ampere = SmartElectricEnergyMeter::InstantAmpere(
            {prop.edt[0], prop.edt[1], prop.edt[2], prop.edt[3]});
        M5_LOGD("%s", to_string(ampere).c_str());
        result.push_back(ampere);
      } else {
        M5_LOGD("pdc is should be 4 bytes, this is "
                "%d bytes.",
                prop.edt.size());
      }
    } break;
    case 0xEA: {                   // 定時積算電力量
      if (prop.edt.size() == 11) { // 11バイト
        std::array<uint8_t, 11> memory;
        std::copy_n(prop.edt.begin(), memory.size(), memory.begin());
        //
        auto cwh = SmartElectricEnergyMeter::CumulativeWattHour(memory);
        M5_LOGD("%s", to_string(cwh).c_str());
        result.push_back(cwh);
      } else {
        M5_LOGD("pdc is should be 11 bytes, this is "
                "%d bytes.",
                prop.edt.size());
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
        M5_LOGD("day of historical 2: "
                "[%02X,%02X,%02X,%02X,%02X,%02X,%02X]",
                a, b, c, d, e, f, g);
      } else {
        M5_LOGD("pdc is should be 7 bytes, this is "
                "%d bytes.",
                prop.edt.size());
      }
    } break;
    default:
      M5_LOGD("unknown epc: 0x%X", prop.epc);
      break;
    }
  }
  return result;
}

// 積算電力量
SmartElectricEnergyMeter::KiloWattHour cumlative_kilo_watt_hour(
    std::tuple<SmartElectricEnergyMeter::CumulativeWattHour,
               SmartElectricEnergyMeter::Coefficient,
               SmartElectricEnergyMeter::Unit>
        in) {
  auto &[cwh, coeff, unit] = in;
  // 係数
  auto powers_of_10 = unit.get_powers_of_10().value_or(0);
  // KWhへの乗数
  auto multiplier = std::pow(10, powers_of_10);
  return SmartElectricEnergyMeter::KiloWattHour{
      coeff.coefficient * cwh.raw_cumlative_watt_hour() * multiplier};
}

// 電力量
std::string to_string_cumlative_kilo_watt_hour(
    SmartElectricEnergyMeter::CumulativeWattHour cwh,
    std::optional<SmartElectricEnergyMeter::Coefficient> opt_coeff,
    SmartElectricEnergyMeter::Unit unit) {
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
