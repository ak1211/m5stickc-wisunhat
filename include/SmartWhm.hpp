// Copyright (c) 2022 Akihiro Yamamoto.
// Licensed under the MIT License <https://spdx.org/licenses/MIT.html>
// See LICENSE file in the project root for full license information.
//
#pragma once
#include <ArduinoJson.h>
#include <array>
#include <chrono>
#include <cstdint>
#include <ctime>
#include <iomanip>
#include <map>
#include <queue>
#include <ratio>
#include <sstream>
#include <string>
#include <tuple>
#include <variant>
#include <vector>

#include "Application.hpp"
#include "EchonetLite.hpp"

using namespace std::literals::string_literals;
using namespace std::literals::string_view_literals;

//
// 接続相手のスマートメーター
//
struct SmartWhm {
  std::optional<SmartElectricEnergyMeter::Coefficient>
      whm_coefficient; // 乗数(無い場合の乗数は1)
  std::optional<SmartElectricEnergyMeter::Unit> whm_unit; // 単位
  std::optional<uint8_t> day_for_which_the_historcal; // 積算履歴収集日
  std::optional<SmartElectricEnergyMeter::InstantWatt> instant_watt; // 瞬時電力
  std::optional<SmartElectricEnergyMeter::InstantAmpere>
      instant_ampere; // 瞬時電流
  std::optional<SmartElectricEnergyMeter::CumulativeWattHour>
      cumlative_watt_hour; // 定時積算電力量
};

#if 0
// 低圧スマート電力量計クラスのイベントを処理する
void process_echonet_lite_frame(
    SmartWhm whm,                                    //
    std::time_t at,                                  //
    const EchonetLiteFrame &frame,                   //
    std::queue<std::string> &to_sending_message_fifo //
) {
  std::vector<SmartElectricEnergyMeter::ReceivedMessage> result{};
  // EDATAは複数送られてくる
  for (const auto &v : splitToEchonetLiteData(frame.edata)) {
    // EchonetLiteプロパティ
    const EchonetLiteProp *prop =
        reinterpret_cast<const EchonetLiteProp *>(v.data());
    switch (prop->epc) {
    case 0x80:                 // 動作状態
      if (prop->pdc == 0x01) { // 1バイト
        enum class OpStatus : uint8_t {
          ON = 0x30,
          OFF = 0x31,
        };
        switch (static_cast<OpStatus>(prop->edt[0])) {
        case OpStatus::ON:
          ESP_LOGD(MAIN, "operation status : ON");
          break;
        case OpStatus::OFF:
          ESP_LOGD(MAIN, "operation status : OFF");
          break;
        }
      } else {
        ESP_LOGD(MAIN,
                 "pdc is should be 1 bytes, this is "
                 "%d bytes.",
                 prop->pdc);
      }
      break;
    case 0x81:                 // 設置場所
      if (prop->pdc == 0x01) { // 1バイト
        uint8_t a = prop->edt[0];
        ESP_LOGD(MAIN, "installation location: 0x%02x", a);
      } else if (prop->pdc == 0x11) { // 17バイト
        ESP_LOGD(MAIN, "installation location");
      } else {
        ESP_LOGD(MAIN,
                 "pdc is should be 1 or 17 bytes, "
                 "this is %d bytes.",
                 prop->pdc);
      }
      break;
    case 0x88:                 // 異常発生状態
      if (prop->pdc == 0x01) { // 1バイト
        enum class FaultStatus : uint8_t {
          FaultOccurred = 0x41,
          NoFault = 0x42,
        };
        switch (static_cast<FaultStatus>(prop->edt[0])) {
        case FaultStatus::FaultOccurred:
          ESP_LOGD(MAIN, "FaultStatus::FaultOccurred");
          break;
        case FaultStatus::NoFault:
          ESP_LOGD(MAIN, "FaultStatus::NoFault");
          break;
        }
      } else {
        ESP_LOGD(MAIN,
                 "pdc is should be 1 bytes, this is "
                 "%d bytes.",
                 prop->pdc);
      }
      break;
    case 0x8A:                 // メーカーコード
      if (prop->pdc == 0x03) { // 3バイト
        uint8_t a = prop->edt[0];
        uint8_t b = prop->edt[1];
        uint8_t c = prop->edt[2];
        ESP_LOGD(MAIN, "Manufacturer: 0x%02x%02x%02x", a, b, c);
      } else {
        ESP_LOGD(MAIN,
                 "pdc is should be 3 bytes, this is "
                 "%d bytes.",
                 prop->pdc);
      }
      break;
    case 0xD3:                 // 係数
      if (prop->pdc == 0x04) { // 4バイト
        auto coeff = SmartElectricEnergyMeter::Coefficient(
            {prop->edt[0], prop->edt[1], prop->edt[2], prop->edt[3]});
        ESP_LOGD(MAIN, "%s", to_string(coeff).c_str());
        whm.whm_coefficient = coeff;
      } else {
        // 係数が無い場合は１倍となる
        whm.whm_coefficient = std::nullopt;
        ESP_LOGD(MAIN, "no coefficient");
      }
      break;
    case 0xD7:                 // 積算電力量有効桁数
      if (prop->pdc == 0x01) { // 1バイト
        auto digits = SmartElectricEnergyMeter::EffectiveDigits(prop->edt[0]);
        ESP_LOGD(MAIN, "%s", to_string(digits).c_str());
      } else {
        ESP_LOGD(MAIN,
                 "pdc is should be 1 bytes, this is "
                 "%d bytes.",
                 prop->pdc);
      }
      break;
    case 0xE1: // 積算電力量単位 (正方向、逆方向計測値)
      if (prop->pdc == 0x01) { // 1バイト
        auto unit = SmartElectricEnergyMeter::Unit(prop->edt[0]);
        ESP_LOGD(MAIN, "%s", to_string(unit).c_str());
        whm.whm_unit = unit;
      } else {
        ESP_LOGD(MAIN,
                 "pdc is should be 1 bytes, this is "
                 "%d bytes.",
                 prop->pdc);
      }
      break;
    case 0xE5:                 // 積算履歴収集日１
      if (prop->pdc == 0x01) { // 1バイト
        uint8_t day = prop->edt[0];
        ESP_LOGD(MAIN, "day of historical 1: (%d)", day);
        whm.day_for_which_the_historcal = day;
      } else {
        ESP_LOGD(MAIN,
                 "pdc is should be 1 bytes, this is "
                 "%d bytes.",
                 prop->pdc);
      }
      break;
    case 0xE7:                 // 瞬時電力値
      if (prop->pdc == 0x04) { // 4バイト
        auto watt = SmartElectricEnergyMeter::InstantWatt(
            {prop->edt[0], prop->edt[1], prop->edt[2], prop->edt[3]});
        ESP_LOGD(MAIN, "%s", to_string(watt).c_str());
        // 送信バッファへ追加する
        auto msg = to_telemetry_message(std::make_pair(at, watt));
        to_sending_message_fifo.emplace(msg);
        //
        whm.instant_watt = watt;
      } else {
        ESP_LOGD(MAIN,
                 "pdc is should be 4 bytes, this is "
                 "%d bytes.",
                 prop->pdc);
      }
      break;
    case 0xE8:                 // 瞬時電流値
      if (prop->pdc == 0x04) { // 4バイト
        auto ampere = SmartElectricEnergyMeter::InstantAmpere(
            {prop->edt[0], prop->edt[1], prop->edt[2], prop->edt[3]});
        ESP_LOGD(MAIN, "%s", to_string(ampere).c_str());
        // 送信バッファへ追加する
        auto msg = to_telemetry_message(std::make_pair(at, ampere));
        to_sending_message_fifo.emplace(msg);
        //
        whm.instant_ampere = ampere;
      } else {
        ESP_LOGD(MAIN,
                 "pdc is should be 4 bytes, this is "
                 "%d bytes.",
                 prop->pdc);
      }
      break;
    case 0xEA:                 // 定時積算電力量
      if (prop->pdc == 0x0B) { // 11バイト
        // std::to_arrayの登場はC++20からなのでこんなことになった
        std::array<uint8_t, 11> memory;
        std::copy_n(prop->edt, memory.size(), memory.begin());
        //
        auto cwh = SmartElectricEnergyMeter::CumulativeWattHour2(
            memory, whm.whm_coefficient, whm.whm_unit);
        ESP_LOGD(MAIN, "%s", to_string(cwh).c_str());
        if (cwh.opt_unit.has_value()) {
          auto unit = cwh.opt_unit.value();
          // 送信バッファへ追加する
          auto msg = to_telemetry_message(
              std::make_tuple(cwh, cwh.opt_coefficient, unit));
          to_sending_message_fifo.emplace(msg);
        }
        //
        whm.cumlative_watt_hour = cwh;
      } else {
        ESP_LOGD(MAIN,
                 "pdc is should be 11 bytes, this is "
                 "%d bytes.",
                 prop->pdc);
      }
      break;
    case 0xED:                 // 積算履歴収集日２
      if (prop->pdc == 0x07) { // 7バイト
        uint8_t a = prop->edt[0];
        uint8_t b = prop->edt[1];
        uint8_t c = prop->edt[2];
        uint8_t d = prop->edt[3];
        uint8_t e = prop->edt[4];
        uint8_t f = prop->edt[5];
        uint8_t g = prop->edt[6];
        ESP_LOGD(MAIN,
                 "day of historical 2: "
                 "[%02x,%02x,%02x,%02x,%02x,%02x,%02x]",
                 a, b, c, d, e, f, g);
      } else {
        ESP_LOGD(MAIN,
                 "pdc is should be 7 bytes, this is "
                 "%d bytes.",
                 prop->pdc);
      }
      break;
    default:
      ESP_LOGD(MAIN, "unknown EPC: %02x", prop->epc);
      break;
    }
  }
}
#endif
