// Copyright (c) 2022 Akihiro Yamamoto.
// Licensed under the MIT License <https://spdx.org/licenses/MIT.html>
// See LICENSE file in the project root for full license information.
//
#pragma once
#include "Application.hpp"
#include <ArduinoJson.h>
#include <array>
#include <cstdint>
#include <ctime>
#include <map>
#include <queue>
#include <string>
#include <vector>

// ECHONET Lite の UDP ポート番号
static constexpr std::string_view EchonetLiteUdpPort{"0E1A"};

// ECHONET Lite 電文ヘッダー
union EchonetLiteEHeader {
  uint8_t u8[2]; // 2バイト表現(デフォルトコンストラクタ)
  struct {
    uint8_t ehd1;
    uint8_t ehd2;
  };
};

//
bool operator==(const EchonetLiteEHeader &left,
                const EchonetLiteEHeader &right) {
  return std::equal(std::begin(left.u8), std::end(left.u8),  /**/
                    std::begin(right.u8), std::end(right.u8) /**/
  );
}

namespace std {
//
string to_string(EchonetLiteEHeader ehd) {
  char buffer[10]{'\0'};
  std::snprintf(buffer, std::size(buffer), "%02X%02X", ehd.u8[0], ehd.u8[1]);
  return string(buffer);
}
} // namespace std

// ECHONET Lite 電文ヘッダー
// 0x1081で固定
// EHD1: 0x10 (ECHONET Lite規格)
// EHD2: 0x81 (規定電文形式)
static constexpr EchonetLiteEHeader EchonetLiteEHD{0x10, 0x81};

// ECHONET Lite オブジェクト指定
union EchonetLiteObjectCode {
  uint8_t u8[3]; // 3バイト表現(デフォルトコンストラクタ)
  struct {
    uint8_t class_group;   // byte.1 クラスグループコード
    uint8_t class_code;    // byte.2 クラスコード
    uint8_t instance_code; // byte.3 インスタンスコード
  };
};

//
bool operator==(const EchonetLiteObjectCode &left,
                const EchonetLiteObjectCode &right) {
  return std::equal(std::begin(left.u8), std::end(left.u8),  /**/
                    std::begin(right.u8), std::end(right.u8) /**/
  );
}

namespace std {
//
string to_string(EchonetLiteObjectCode eoj) {
  char buffer[100]{'\0'};
  std::snprintf(buffer, std::size(buffer), "%02X%02X%02X", eoj.class_group,
                eoj.class_code, eoj.instance_code);
  return string(buffer);
}
} // namespace std

// ECHONET Liteサービス
enum class EchonetLiteESV : uint8_t {
  SetC_SNA = 0x51, // プロパティ値書き込み要求不可応答
  Get_SNA = 0x52,  // プロパティ値読み出し不可応答
  SetC = 0x61,     // プロパティ値書き込み要求（応答要）
  Get = 0x62,      // プロパティ値読み出し要求
  Set_Res = 0x71,  // プロパティ値書き込み応答
  Get_Res = 0x72,  // プロパティ値読み出し応答
  INF = 0x73,      // プロパティ値通知
  INFC = 0x74,     // プロパティ値通知（応答要）
  INFC_Res = 0x7A, // プロパティ値通知応答
};

// トランザクションID
union EchonetLiteTransactionId {
  uint16_t u16;  // 2バイト表現(デフォルトコンストラクタ)
  uint8_t u8[2]; // 1バイトの配列表現
};

namespace std {
//
string to_string(EchonetLiteTransactionId tid) {
  char buffer[10]{'\0'};
  std::snprintf(buffer, std::size(buffer), "%02X%02X", tid.u8[0], tid.u8[1]);
  return string(buffer);
}
} // namespace std

// ECHONET Lite フレーム
struct EchonetLiteFrame {
  EchonetLiteEHeader ehd;       // ECHONET Lite 電文ヘッダー
  EchonetLiteTransactionId tid; // トランザクションID
  struct EchonetLiteData {      // ECHONET Lite データ (EDATA)
    EchonetLiteObjectCode seoj; // 送信元ECHONET Liteオブジェクト指定
    EchonetLiteObjectCode deoj; // 相手元ECHONET Liteオブジェクト指定
    uint8_t esv;                // ECHONET Liteサービス
    uint8_t opc;                // 処理プロパティ数
    struct EchonetLiteProp {
      uint8_t epc;   // ECHONET Liteプロパティ
      uint8_t pdc;   // EDTのバイト数
      uint8_t edt[]; // プロパティ値データ
    } props[1];
  } edata;
};
using EchonetLiteData = EchonetLiteFrame::EchonetLiteData;
using EchonetLiteProp = EchonetLiteFrame::EchonetLiteData::EchonetLiteProp;

// EDATAからECHONET Liteプロパティを取り出す
std::vector<std::vector<uint8_t>>
splitToEchonetLiteData(const EchonetLiteData &edata) {
  std::vector<std::vector<uint8_t>> result;
  auto props = reinterpret_cast<const uint8_t *>(&edata.props[0]);
  for (auto idx = 0; idx < edata.opc; ++idx) {
    std::vector<uint8_t> v;
    uint8_t epc = *props++;
    uint8_t pdc = *props++;
    v.push_back(epc);
    v.push_back(pdc);
    std::copy_n(props, pdc, std::back_inserter(v));
    props += pdc;
    result.emplace_back(std::move(v));
  }

  return result;
}

namespace std {
//
string to_string(const EchonetLiteFrame &frame) {
  auto convert_byte = [](uint8_t b) -> std::string {
    char buffer[100]{'\0'};
    std::sprintf(buffer, "%02X", b);
    return std::string{buffer};
  };
  std::string s;
  s += "EHD:" + std::to_string(frame.ehd) + ",";
  s += "TID:" + std::to_string(frame.tid) + ",";
  s += "SEOJ:" + std::to_string(frame.edata.seoj) + ",";
  s += "DEOJ:" + std::to_string(frame.edata.deoj) + ",";
  s += "ESV:" + convert_byte(frame.edata.esv) + ",";
  s += "OPC:" + convert_byte(frame.edata.opc) + ",";
  //
  std::vector<std::vector<uint8_t>> vv = splitToEchonetLiteData(frame.edata);
  for (const auto &v : vv) {
    s += "[";
    auto prop = reinterpret_cast<const EchonetLiteProp *>(v.data());
    s += "EPC:" + convert_byte(prop->epc) + ",";
    s += "PDC:" + convert_byte(prop->pdc) + ",";
    if (prop->pdc >= 1) {
      s += "EDT:";
      for (auto idx = 0; idx < prop->pdc; ++idx) {
        s += convert_byte(prop->edt[idx]);
      }
    }
    s += "] ";
  }
  return s;
}
} // namespace std

//
// 自分自身(このプログラム)のECHONET Liteオブジェクト
//
namespace HomeController {
// グループコード 0x05 (管理・操作関連機器クラス)
// クラスコード 0xFF (コントローラ)
// インスタンスコード 0x01 (インスタンスコード1)
constexpr EchonetLiteObjectCode EchonetLiteEOJ{0x05, 0xFF, 0x01};
} // namespace HomeController

//
// スマートメータに接続時に送られてくるECHONET Liteオブジェクト
//
namespace NodeProfileClass {
// グループコード 0x0E (ノードプロファイルクラス)
// クラスコード 0xF0
// インスタンスコード 0x01 (一般ノード)
constexpr EchonetLiteObjectCode EchonetLiteEOJ{0x0E, 0xF0, 0x01};
} // namespace NodeProfileClass

//
// 低圧スマート電力量メータクラス規定
//
namespace SmartElectricEnergyMeter {
// クラスグループコード 0x02
// クラスコード 0x88
// インスタンスコード 0x01
constexpr EchonetLiteObjectCode EchonetLiteEOJ{0x02, 0x88, 0x01};

// ECHONET Liteプロパティ
enum class EchonetLiteEPC : uint8_t {
  Operation_status = 0x80,           // 動作状態
  Installation_location = 0x81,      // 設置場所
  Fault_status = 0x88,               // 異常発生状態
  Manufacturer_code = 0x8A,          // メーカーコード
  Coefficient = 0xD3,                // 係数
  Number_of_effective_digits = 0xD7, // 積算電力量有効桁数
  Measured_cumulative_amount = 0xE0, // 積算電力量計測値 (正方向計測値)
  Unit_for_cumulative_amounts = 0xE1, // 積算電力量単位 (正方向、逆方向計測値)
  Historical_measured_cumulative_amount =
      0xE2, // 積算電力量計測値履歴１ (正方向計測値)
  Day_for_which_the_historcal_data_1 = 0xE5, // 積算履歴収集日１
  Measured_instantaneous_power = 0xE7,       // 瞬時電力計測値
  Measured_instantaneous_currents = 0xE8,    // 瞬時電流計測値
  Cumulative_amounts_of_electric_energy_measured_at_fixed_time =
      0xEA,                                  // 定時積算電力量計測値
                                             // (正方向計測値)
  Day_for_which_the_historcal_data_2 = 0xED, // 積算履歴収集日２
};

// 係数
struct Coefficient {
  uint32_t coefficient;
  //
  Coefficient(std::array<uint8_t, 4> array) {
    coefficient =
        (array[0] << 24) | (array[1] << 16) | (array[2] << 8) | array[3];
  }
  //
  std::string show() const { return std::to_string(coefficient); }
};

// 積算電力量有効桁数
struct EffectiveDigits {
  uint8_t digits;
  //
  EffectiveDigits(uint8_t init) : digits{init} {}
  //
  std::string show() const {
    return std::to_string(digits) + " effective digits.";
  }
};

// 積算電力量単位 (正方向、逆方向計測値)
struct Unit {
  uint8_t unit;
  // Key, Value
  using Key = uint8_t;
  struct Value {
    double mul;
    int8_t power10;
    std::string_view description;
  };
  // key-valueストア
  std::map<Key, Value> keyval;
  //
  Unit(uint8_t init) : unit{init} {
    // 0x00 : 1kWh      = 10^0
    // 0x01 : 0.1kWh    = 10^-1
    // 0x02 : 0.01kWh   = 10^-2
    // 0x03 : 0.001kWh  = 10^-3
    // 0x04 : 0.0001kWh = 10^-4
    // 0x0A : 10kWh     = 10^1
    // 0x0B : 100kWh    = 10^2
    // 0x0C : 1000kWh   = 10^3
    // 0x0D : 10000kWh  = 10^4
    keyval.insert(std::make_pair(0x00, Value{1.0, 0, "*1 kwh"}));
    keyval.insert(std::make_pair(0x01, Value{0.1, -1, "*0.1 kwh"}));
    keyval.insert(std::make_pair(0x02, Value{0.01, -2, "*0.01 kwh"}));
    keyval.insert(std::make_pair(0x03, Value{0.001, -3, "*0.001 kwh"}));
    keyval.insert(std::make_pair(0x04, Value{0.0001, -4, "*0.0001 kwh"}));
    keyval.insert(std::make_pair(0x0A, Value{10.0, 1, "*10 kwh"}));
    keyval.insert(std::make_pair(0x0B, Value{100.0, 2, "*100 kwh"}));
    keyval.insert(std::make_pair(0x0C, Value{1000.0, 3, "*1000 kwh"}));
    keyval.insert(std::make_pair(0x0D, Value{10000.0, 4, "*100000 kwh"}));
  }
  // 1kwhをベースとして, それに対して10のn乗を返す
  int32_t get_powers_of_10() {
    decltype(keyval)::iterator it = keyval.find(unit);
    if (it == keyval.end()) {
      return 0; // 見つからなかった
    }
    return it->second.power10;
  }
  //
  std::optional<double> get_kilowatt_hour(uint32_t v) {
    decltype(keyval)::iterator it = keyval.find(unit);
    if (it == keyval.end()) {
      return std::nullopt; // 見つからなかった
    }
    return it->second.mul * static_cast<double>(v);
  }
  //
  std::string show() {
    decltype(keyval)::iterator it = keyval.find(unit);
    if (it == keyval.end()) {
      return std::string("no multiplier");
    }
    return std::string(it->second.description);
  }
};

//
std::string iso8601formatUTC(std::time_t utctime) {
  struct tm tm;
  gmtime_r(&utctime, &tm);
  char buffer[30]{'\0'};
  std::strftime(buffer, std::size(buffer), "%Y-%m-%dT%H:%M:%SZ", &tm);
  return std::string(buffer);
}

//
// 測定値
//

// 瞬時電力
struct InstantWatt {
  int32_t watt;            // 瞬時電力(単位 W)
  std::time_t measured_at; // 測定時間
  //
  InstantWatt(std::array<uint8_t, 4> array, std::time_t at) : measured_at{at} {
    watt = (array[0] << 24) | (array[1] << 16) | (array[2] << 8) | array[3];
  }
  //
  bool equal_value(const InstantWatt &other) {
    return this->watt == other.watt;
  }
  //
  std::string show() const { return std::to_string(watt) + " W"; }
  // 送信用メッセージに変換する
  std::string watt_to_telemetry_message() const {
    std::string iso8601_at{iso8601formatUTC(measured_at)};
    constexpr std::size_t Capacity{JSON_OBJECT_SIZE(100)};
    StaticJsonDocument<Capacity> doc;
    doc["device_id"] = AWS_IOT_DEVICE_ID;
    doc["sensor_id"] = SENSOR_ID;
    doc["measured_at"] = iso8601_at;
    doc["instant_watt"] = watt;
    std::string output;
    serializeJson(doc, output);
    return output;
  }
};

// 瞬時電流
struct InstantAmpere {
  int16_t r_deciA;         // R相電流(単位 1/10A == 1 deci A)
  int16_t t_deciA;         // T相電流(単位 1/10A == 1 deci A)
  std::time_t measured_at; // 測定時間
  //
  InstantAmpere(std::array<uint8_t, 4> array, std::time_t at)
      : measured_at{at} {
    // R相電流
    uint16_t r_u16 = (array[0] << 8) | array[1];
    r_deciA = static_cast<int16_t>(r_u16);
    // T相電流
    uint16_t t_u16 = (array[2] << 8) | array[3];
    t_deciA = static_cast<int16_t>(t_u16);
    //
  }
  //
  bool equal_value(const InstantAmpere &other) {
    return (this->r_deciA == other.r_deciA) && (this->t_deciA == other.t_deciA);
  }
  //
  std::string show() const {
    // 整数部と小数部
    auto r = std::make_pair(r_deciA / 10, r_deciA % 10);
    auto t = std::make_pair(t_deciA / 10, t_deciA % 10);
    std::string s;
    s += "R: " + std::to_string(r.first) + "." + std::to_string(r.second) +
         " A, T: " + std::to_string(t.first) + "." + std::to_string(t.second) +
         " A";
    return s;
  }
  // 送信用メッセージに変換する
  std::string ampere_to_telemetry_message() const {
    std::string iso8601_at{iso8601formatUTC(measured_at)};
    constexpr std::size_t Capacity{JSON_OBJECT_SIZE(100)};
    StaticJsonDocument<Capacity> doc;
    doc["device_id"] = AWS_IOT_DEVICE_ID;
    doc["sensor_id"] = SENSOR_ID;
    doc["measured_at"] = iso8601_at;
    doc["instant_ampere_R"] = static_cast<double>(r_deciA) / 10.0;
    //
    doc["instant_ampere_T"] = static_cast<double>(t_deciA) / 10.0;
    std::string output;
    serializeJson(doc, output);
    return output;
  }

  // アンペア単位で得る
  float get_ampere_R() const { return static_cast<float>(r_deciA) / 10.0; }
  float get_ampere_T() const { return static_cast<float>(t_deciA) / 10.0; }
  // デシアンペア単位で得る
  int16_t get_deciampere_R() const { return r_deciA; }
  int16_t get_deciampere_T() const { return t_deciA; }
  // ミリアンペア単位で得る
  int32_t get_milliampere_R() const { return r_deciA * 1000 / 10; }
  int32_t get_milliampere_T() const { return t_deciA * 1000 / 10; }
};

// 定時積算電力量
struct CumulativeWattHour {
  // 生の受信値
  using OriginalPayload = std::array<uint8_t, 11>;
  OriginalPayload originalPayload;
  // 乗数(無い場合の乗数は1)
  std::optional<SmartElectricEnergyMeter::Coefficient> opt_coefficient;
  // 単位
  std::optional<SmartElectricEnergyMeter::Unit> opt_unit;
  //
  CumulativeWattHour(OriginalPayload array,
                     std::optional<SmartElectricEnergyMeter::Coefficient> coeff,
                     std::optional<SmartElectricEnergyMeter::Unit> unit)
      : originalPayload{array}, opt_coefficient{coeff}, opt_unit{unit} {}
  // 年
  uint16_t year() const {
    return (originalPayload[0] << 8) | originalPayload[1];
  }
  // 月
  uint8_t month() const { return originalPayload[2]; }
  // 日
  uint8_t day() const { return originalPayload[3]; }
  // 時
  uint8_t hour() const { return originalPayload[4]; }
  // 分
  uint8_t minutes() const { return originalPayload[5]; }
  // 秒
  uint8_t seconds() const { return originalPayload[6]; }
  // 積算電力量
  uint32_t raw_cumlative_watt_hour() const {
    return (originalPayload[7] << 24) | (originalPayload[8] << 16) |
           (originalPayload[9] << 8) | originalPayload[10];
  }
  // 積算電力量
  double cumlative_kwh(SmartElectricEnergyMeter::Unit unit) const {
    uint32_t cwh = raw_cumlative_watt_hour();
    // 係数(無い場合の係数は1)
    if (opt_coefficient.has_value()) {
      cwh = cwh * opt_coefficient.value().coefficient;
    }
    int32_t powers_of_10 = unit.get_powers_of_10();
    return cwh * std::pow(10, powers_of_10);
  }
  //
  bool equal_value(const CumulativeWattHour &other) {
    return std::equal(std::begin(this->originalPayload),
                      std::end(this->originalPayload),
                      std::begin(other.originalPayload));
  }
  //
  std::string show() const {
    char buff[100]{'\0'};
    std::sprintf(buff, "%4d/%2d/%2d %02d:%02d:%02d %d", year(), month(), day(),
                 hour(), minutes(), seconds(), raw_cumlative_watt_hour());
    return std::string(buff);
  }
  // 送信用メッセージに変換する
  std::string cwh_to_telemetry_message() const {
    constexpr std::size_t Capacity{JSON_OBJECT_SIZE(100)};
    StaticJsonDocument<Capacity> doc;
    doc["device_id"] = AWS_IOT_DEVICE_ID;
    doc["sensor_id"] = SENSOR_ID;
    if constexpr (false) {
      // 生の受信値
      JsonArray array = doc.createNestedArray("original_payload");
      for (auto &v : originalPayload) {
        array.add(v);
      }
    }
    // 時刻をISO8601形式で得る
    std::optional<std::string> opt_iso8601 = get_iso8601_datetime();
    if (opt_iso8601.has_value()) {
      doc["measured_at"] = opt_iso8601.value();
    }
    // 積算電力量(kwh)
    if (opt_unit.has_value()) {
      doc["cumlative_kwh"] = cumlative_kwh(opt_unit.value());
    }
    std::string output;
    serializeJson(doc, output);
    return output;
  }
  // 電力量
  std::optional<std::string> to_string_kwh() const {
    if (!opt_unit.has_value()) {
      ESP_LOGI(MAIN, "unspecificated unit of cumulative kilo-watt hour");
      return std::nullopt;
    }
    SmartElectricEnergyMeter::Unit unit = opt_unit.value();
    uint32_t cwh = raw_cumlative_watt_hour();
    // 係数(無い場合の係数は1)
    if (opt_coefficient.has_value()) {
      cwh = cwh * opt_coefficient.value().coefficient;
    }
    // 文字にする
    std::string str_kwh = std::to_string(cwh);
    // 1kwhの位置に小数点を移動する
    int powers_of_10 = unit.get_powers_of_10();
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
  // 時刻をISO8601形式で得る
  std::optional<std::string> get_iso8601_datetime() const {
    // 日時が0xFFなどの異常値を送ってくることがあるので確認する
    // 秒が0xFFの応答を返してくることがあるが有効な値ではない
    if (seconds() == 0xFF) {
      return std::nullopt;
    }
    char buffer[sizeof("2022-08-01T00:00:00+09:00")]{};
    std::snprintf(buffer, sizeof(buffer), "%04d-%02d-%02dT%02d:%02d:%02d+09:00",
                  year(), month(), day(), hour(), minutes(), seconds());
    //
    return std::string(buffer);
  }
  // 日本標準時で時刻をstd::time_t形式で得る
  std::optional<std::time_t> get_time_t() const {
    // 秒が0xFFの応答を返してくることがあるが有効な値ではない
    if (seconds() == 0xFF) {
      return std::nullopt;
    }
    //
    std::tm at{};
    at.tm_year = year() - 1900;
    at.tm_mon = month() - 1;
    at.tm_mday = day();
    at.tm_hour = hour();
    at.tm_min = minutes();
    at.tm_sec = seconds();
    std::time_t jst = std::mktime(&at);
    return jst;
  }
};

// 通信用のフレームを作る
std::vector<uint8_t> make_echonet_lite_frame(
    EchonetLiteTransactionId tid,                              /**/
    EchonetLiteESV esv,                                        /**/
    std::vector<SmartElectricEnergyMeter::EchonetLiteEPC> epcs /**/
) {
  //
  std::vector<uint8_t> echonet_lite_frame;
  // bytes#1 and bytes#2
  // EHD: ECHONET Lite 電文ヘッダー
  std::copy(std::begin(EchonetLiteEHD.u8), std::end(EchonetLiteEHD.u8),
            std::back_inserter(echonet_lite_frame));
  // bytes#3 and bytes#4
  // TID: トランザクションID
  std::copy(std::begin(tid.u8), std::end(tid.u8),
            std::back_inserter(echonet_lite_frame));
  //
  // EDATA
  //
  // bytes#5 and bytes#6 and bytes#7
  // SEOJ: メッセージの送り元(sender : 自分自身)
  std::copy(std::begin(HomeController::EchonetLiteEOJ.u8),
            std::end(HomeController::EchonetLiteEOJ.u8),
            std::back_inserter(echonet_lite_frame));
  // bytes#8 and bytes#9 adn bytes#10
  // DEOJ: メッセージの行き先(destination : スマートメーター)
  std::copy(std::begin(SmartElectricEnergyMeter::EchonetLiteEOJ.u8),
            std::end(SmartElectricEnergyMeter::EchonetLiteEOJ.u8),
            std::back_inserter(echonet_lite_frame));
  // bytes#11
  // ESV : ECHONET Lite サービスコード
  echonet_lite_frame.push_back(static_cast<uint8_t>(esv));
  // bytes#12
  // OPC: 処理プロパティ数
  echonet_lite_frame.push_back(static_cast<uint8_t>(epcs.size()));
  //
  // EPC, PDC, EDTを繰り返す
  //
  for (auto &epc : epcs) {
    // EPC: ECHONET Liteプロパティ
    echonet_lite_frame.push_back(static_cast<uint8_t>(epc));
    // PDC: EDTのバイト数
    echonet_lite_frame.push_back(0); // この後に続くEDTはないので0を入れる
  }
  return echonet_lite_frame;
}

} // namespace SmartElectricEnergyMeter

//
// 接続相手のスマートメーター
//
struct SmartWhm {
  // クラス変数
  std::optional<SmartElectricEnergyMeter::Coefficient>
      whm_coefficient; // 乗数(無い場合の乗数は1)
  std::optional<SmartElectricEnergyMeter::Unit> whm_unit; // 単位
  std::optional<uint8_t> day_for_which_the_historcal; // 積算履歴収集日
  std::optional<SmartElectricEnergyMeter::InstantWatt> instant_watt; // 瞬時電力
  std::optional<SmartElectricEnergyMeter::InstantAmpere>
      instant_ampere; // 瞬時電流
  std::optional<SmartElectricEnergyMeter::CumulativeWattHour>
      cumlative_watt_hour; // 定時積算電力量
  //
  SmartWhm()
      : whm_coefficient{std::nullopt},
        whm_unit{std::nullopt},
        day_for_which_the_historcal{std::nullopt} {}
  // 低圧スマート電力量計クラスのイベントを処理する
  void process_echonet_lite_frame(
      std::time_t at,                                  /**/
      const EchonetLiteFrame &frame,                   /**/
      std::queue<std::string> &to_sending_message_fifo /**/
  ) {
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
          ESP_LOGD(MAIN, "pdc is should be 1 bytes, this is %d bytes.",
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
          ESP_LOGD(MAIN, "pdc is should be 1 or 17 bytes, this is %d bytes.",
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
          ESP_LOGD(MAIN, "pdc is should be 1 bytes, this is %d bytes.",
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
          ESP_LOGD(MAIN, "pdc is should be 3 bytes, this is %d bytes.",
                   prop->pdc);
        }
        break;
      case 0xD3:                 // 係数
        if (prop->pdc == 0x04) { // 4バイト
          auto coeff = SmartElectricEnergyMeter::Coefficient(
              {prop->edt[0], prop->edt[1], prop->edt[2], prop->edt[3]});
          ESP_LOGD(MAIN, "%s", coeff.show().c_str());
          whm_coefficient = coeff;
        } else {
          // 係数が無い場合は１倍となる
          whm_coefficient = std::nullopt;
          ESP_LOGD(MAIN, "no coefficient");
        }
        break;
      case 0xD7:                 // 積算電力量有効桁数
        if (prop->pdc == 0x01) { // 1バイト
          auto digits = SmartElectricEnergyMeter::EffectiveDigits(prop->edt[0]);
          ESP_LOGD(MAIN, "%s", digits.show().c_str());
        } else {
          ESP_LOGD(MAIN, "pdc is should be 1 bytes, this is %d bytes.",
                   prop->pdc);
        }
        break;
      case 0xE1: // 積算電力量単位 (正方向、逆方向計測値)
        if (prop->pdc == 0x01) { // 1バイト
          auto unit = SmartElectricEnergyMeter::Unit(prop->edt[0]);
          ESP_LOGD(MAIN, "%s", unit.show().c_str());
          whm_unit = unit;
        } else {
          ESP_LOGD(MAIN, "pdc is should be 1 bytes, this is %d bytes.",
                   prop->pdc);
        }
        break;
      case 0xE5:                 // 積算履歴収集日１
        if (prop->pdc == 0x01) { // 1バイト
          uint8_t day = prop->edt[0];
          ESP_LOGD(MAIN, "day of historical 1: (%d)", day);
          day_for_which_the_historcal = day;
        } else {
          ESP_LOGD(MAIN, "pdc is should be 1 bytes, this is %d bytes.",
                   prop->pdc);
        }
        break;
      case 0xE7:                 // 瞬時電力値
        if (prop->pdc == 0x04) { // 4バイト
          auto watt = SmartElectricEnergyMeter::InstantWatt(
              {prop->edt[0], prop->edt[1], prop->edt[2], prop->edt[3]}, at);
          ESP_LOGD(MAIN, "%s", watt.show().c_str());
          // 送信バッファへ追加する
          to_sending_message_fifo.emplace(watt.watt_to_telemetry_message());
          //
          instant_watt = watt;
        } else {
          ESP_LOGD(MAIN, "pdc is should be 4 bytes, this is %d bytes.",
                   prop->pdc);
        }
        break;
      case 0xE8:                 // 瞬時電流値
        if (prop->pdc == 0x04) { // 4バイト
          auto ampere = SmartElectricEnergyMeter::InstantAmpere(
              {prop->edt[0], prop->edt[1], prop->edt[2], prop->edt[3]}, at);
          ESP_LOGD(MAIN, "%s", ampere.show().c_str());
          // 送信バッファへ追加する
          to_sending_message_fifo.emplace(ampere.ampere_to_telemetry_message());
          //
          instant_ampere = ampere;
        } else {
          ESP_LOGD(MAIN, "pdc is should be 4 bytes, this is %d bytes.",
                   prop->pdc);
        }
        break;
      case 0xEA:                 // 定時積算電力量
        if (prop->pdc == 0x0B) { // 11バイト
          // std::to_arrayの登場はC++20からなのでこんなことになった
          std::array<uint8_t, 11> memory;
          std::copy_n(prop->edt, memory.size(), memory.begin());
          //
          auto cwh = SmartElectricEnergyMeter::CumulativeWattHour(
              memory, whm_coefficient, whm_unit);
          ESP_LOGD(MAIN, "%s", cwh.show().c_str());
          // 送信バッファへ追加する
          to_sending_message_fifo.emplace(cwh.cwh_to_telemetry_message());
          //
          cumlative_watt_hour = cwh;
        } else {
          ESP_LOGD(MAIN, "pdc is should be 11 bytes, this is %d bytes.",
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
                   "day of historical 2: [%02x,%02x,%02x,%02x,%02x,%02x,%02x]",
                   a, b, c, d, e, f, g);
        } else {
          ESP_LOGD(MAIN, "pdc is should be 7 bytes, this is %d bytes.",
                   prop->pdc);
        }
        break;
      default:
        ESP_LOGD(MAIN, "unknown epc: 0x%x", prop->epc);
        break;
      }
    }
  }
};
