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
#include <string>
#include <vector>

// ECHONET Lite の UDP ポート番号
const std::string_view EchonetLiteUdpPort{"0E1A"};

// ECHONET Lite 電文ヘッダー 1,2
// 0x1081で固定
// EHD1: 0x10 (ECHONET Lite規格)
// EHD2: 0x81 (規定電文形式)
const uint8_t EchonetLiteEHD1{0x10};
const uint8_t EchonetLiteEHD2{0x81};

//
// ECHONET Lite オブジェクト指定
//
struct EchonetLiteObjectCode {
  uint8_t class_group;   // byte.1 クラスグループコード
  uint8_t class_code;    // byte.2 クラスコード
  uint8_t instance_code; // byte.3 インスタンスコード
};

//
// 自分自身(このプログラム)のECHONET Liteオブジェクト
//
struct HomeController {
  // グループコード 0x05 (管理・操作関連機器クラス)
  // クラスコード 0xFF (コントローラ)
  // インスタンスコード 0x01 (インスタンスコード1)
  static std::array<uint8_t, 3> EchonetLiteEOJ() { return {0x05, 0xFF, 0x01}; }
};

//
// スマートメータに接続時に送られてくるECHONET Liteオブジェクト
//
class NodeProfileClass {
public:
  // グループコード 0x0E (ノードプロファイルクラス)
  // クラスコード 0xF0
  // インスタンスコード 0x01 (一般ノード)
  static std::array<uint8_t, 3> EchonetLiteEOJ() { return {0x0E, 0xF0, 0x01}; }
};

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

// ECHONET Lite フレーム
struct EchonetLiteFrame {
  uint8_t ehd1;            // ECHONET Lite 電文ヘッダー 1
  uint8_t ehd2;            // ECHONET Lite 電文ヘッダー 2
  uint8_t tid1;            // トランザクションID
  uint8_t tid2;            // トランザクションID
  struct EchonetLiteData { // ECHONET Lite データ (EDATA)
    uint8_t seoj[3];       // 送信元ECHONET Liteオブジェクト指定
    uint8_t deoj[3];       // 相手元ECHONET Liteオブジェクト指定
    uint8_t esv;           // ECHONET Liteサービス
    uint8_t opc;           // 処理プロパティ数
    uint8_t epc;           // ECHONET Liteプロパティ
    uint8_t pdc;           // EDTのバイト数
    uint8_t edt[];         // プロパティ値データ
  } edata;
};

//
// 接続相手のスマートメーター
//
class SmartWhm {
public:
  // 低圧スマート電力量メータクラス規定より
  // スマートメーターのECHONET Liteオブジェクト
  // クラスグループコード 0x02
  // クラスコード 0x88
  // インスタンスコード 0x01
  static std::array<uint8_t, 3> EchonetLiteEOJ() { return {0x02, 0x88, 0x01}; }

  // ECHONET Liteプロパティ
  enum class EchonetLiteEPC : uint8_t {
    Operation_status = 0x80,           // 動作状態
    Coefficient = 0xD3,                // 係数
    Number_of_effective_digits = 0xD7, // 積算電力量有効桁数
    Measured_cumulative_amount = 0xE0, // 積算電力量計測値 (正方向計測値)
    Unit_for_cumulative_amounts = 0xE1, // 積算電力量単位 (正方向、逆方向計測値)
    Historical_measured_cumulative_amount =
        0xE2, //積算電力量計測値履歴１ (正方向計測値)
    Day_for_which_the_historcal_data_1 = 0xE5, // 積算履歴収集日１
    Measured_instantaneous_power = 0xE7,       // 瞬時電力計測値
    Measured_instantaneous_currents = 0xE8,    // 瞬時電流計測値
    Cumulative_amounts_of_electric_energy_measured_at_fixed_time =
        0xEA, // 定時積算電力量計測値
              // (正方向計測値)
  };

  // 通信用のフレームを作る
  static std::vector<uint8_t> make_get_frame(uint8_t tid1, uint8_t tid2,
                                             EchonetLiteESV esv,
                                             EchonetLiteEPC epc) {
    EchonetLiteFrame frame;
    frame.ehd1 = EchonetLiteEHD1; // ECHONET Lite 電文ヘッダー 1
    frame.ehd2 = EchonetLiteEHD2; // ECHONET Lite 電文ヘッダー 2
    frame.tid1 = tid1;
    frame.tid2 = tid2;
    // メッセージの送り元(sender : 自分自身)
    std::array<uint8_t, 3> sender = HomeController::EchonetLiteEOJ();
    std::copy(sender.begin(), sender.end(), frame.edata.seoj);
    // メッセージの行き先(destination : スマートメーター)
    std::array<uint8_t, 3> destination = SmartWhm::EchonetLiteEOJ();
    std::copy(destination.begin(), destination.end(), frame.edata.deoj);
    //
    frame.edata.esv = static_cast<uint8_t>(esv);
    frame.edata.opc = 1; // 要求は1つのみ
    frame.edata.epc = static_cast<uint8_t>(epc);
    frame.edata.pdc = 0; // この後に続くEDTはないので0

    // vectorへ変換して返却する
    uint8_t *begin = reinterpret_cast<uint8_t *>(&frame);
    uint8_t *end = begin + sizeof(frame);
    return std::vector<uint8_t>{begin, end};
  }

  //
  static std::string show(const EchonetLiteFrame &frame) {
    auto convert_byte = [](uint8_t b) -> std::string {
      char buffer[100]{'\0'};
      std::sprintf(buffer, "%02X", b);
      return std::string{buffer};
    };
    std::string s;
    s += "EHD1:" + convert_byte(frame.ehd1) + ",";
    s += "EHD2:" + convert_byte(frame.ehd2) + ",";
    s += "TID:" + convert_byte(frame.tid1) + convert_byte(frame.tid2) + ",";
    s += "SEOJ:" + convert_byte(frame.edata.seoj[0]) +
         convert_byte(frame.edata.seoj[1]) + convert_byte(frame.edata.seoj[2]) +
         ",";
    s += "DEOJ:" + convert_byte(frame.edata.deoj[0]) +
         convert_byte(frame.edata.deoj[1]) + convert_byte(frame.edata.deoj[2]) +
         ",";
    s += "ESV:" + convert_byte(frame.edata.esv) + ",";
    s += "OPC:" + convert_byte(frame.edata.opc) + ",";
    s += "EPC:" + convert_byte(frame.edata.epc) + ",";
    s += "PDC:" + convert_byte(frame.edata.pdc) + ",";
    s += "EDT:";
    for (std::size_t i = 0; i < frame.edata.pdc; ++i) {
      s += convert_byte(frame.edata.edt[i]);
    }
    return s;
  }

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
    double to_kilowatt_hour(uint32_t v) {
      decltype(keyval)::iterator it = keyval.find(unit);
      if (it == keyval.end()) {
        return 0.0; // 見つからなかった
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
  static std::string iso8601formatUTC(std::time_t utctime) {
    struct tm tm;
    gmtime_r(&utctime, &tm);
    std::array<char, 30> buffer;
    std::strftime(buffer.data(), buffer.size(), "%Y-%m-%dT%H:%M:%SZ", &tm);
    return std::string{buffer.data()};
  }

  //
  // 測定値
  //

  // 瞬時電力
  struct InstantWatt {
    int32_t watt;            // 瞬時電力(単位 W)
    std::time_t measured_at; // 測定時間
    //
    InstantWatt(std::array<uint8_t, 4> array, std::time_t at)
        : measured_at{at} {
      watt = (array[0] << 24) | (array[1] << 16) | (array[2] << 8) | array[3];
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
      doc["instant_watt"] = std::to_string(watt);
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
    std::string show() const {
      // 整数部と小数部
      auto r = std::make_pair(r_deciA / 10, r_deciA % 10);
      auto t = std::make_pair(t_deciA / 10, t_deciA % 10);
      std::string s;
      s += "R: " + std::to_string(r.first) + "." + std::to_string(r.second) +
           " A, T: " + std::to_string(t.first) + "." +
           std::to_string(t.second) + " A";
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
      // 整数部と小数部
      std::string ri{std::to_string(r_deciA / 10)};
      std::string rf{std::to_string(r_deciA % 10)};
      std::string r{ri + "." + rf};
      doc["instant_ampere_R"] = r;
      //
      std::string ti{std::to_string(t_deciA / 10)};
      std::string tf{std::to_string(t_deciA % 10)};
      std::string t{ti + "." + tf};
      doc["instant_ampere_T"] = t;
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
    std::optional<SmartWhm::Coefficient> opt_coefficient;
    // 単位
    std::optional<SmartWhm::Unit> opt_unit;
    //
    CumulativeWattHour(OriginalPayload array,
                       std::optional<SmartWhm::Coefficient> coeff,
                       std::optional<SmartWhm::Unit> unit)
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
    uint32_t cumlative_watt_hour() const {
      return (originalPayload[7] << 24) | (originalPayload[8] << 16) |
             (originalPayload[9] << 8) | originalPayload[10];
    }
    //
    std::string show() const {
      char buff[100]{'\0'};
      std::sprintf(buff, "%4d/%2d/%2d %02d:%02d:%02d %d", year(), month(),
                   day(), hour(), minutes(), seconds(), cumlative_watt_hour());
      return std::string(buff);
    }
    // 送信用メッセージに変換する
    std::string cwh_to_telemetry_message() const {
      constexpr std::size_t Capacity{JSON_OBJECT_SIZE(100)};
      StaticJsonDocument<Capacity> doc;
      doc["device_id"] = AWS_IOT_DEVICE_ID;
      doc["sensor_id"] = SENSOR_ID;
#if 0
    // 生の受信値
    JsonArray array = doc.createNestedArray("original_payload");
    for (auto &v : originalPayload) {
      array.add(v);
    }
#endif
      // 時刻をISO8601形式で得る
      std::optional<std::string> opt_iso8601 = get_iso8601();
      if (opt_iso8601.has_value()) {
        doc["measured_at"] = opt_iso8601.value();
      }
      // 積算電力量(kwh)
      std::optional<std::string> opt_cumlative_kwh = to_string_kwh();
      if (opt_cumlative_kwh.has_value()) {
        doc["cumlative_kwh"] = opt_cumlative_kwh.value();
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
      SmartWhm::Unit unit = opt_unit.value();
      uint32_t cwh = cumlative_watt_hour();
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
    // 日時が0xFFなどの異常値を送ってくることがあるので確認すること。
    std::optional<std::string> get_iso8601() const {
      // 秒が0xFFの応答を返してくることがあるが有効な値ではない
      if (seconds() == 0xFF) {
        return std::nullopt;
      }
      char buffer[sizeof("2022-08-01T00:00:00+09:00")]{};
      std::snprintf(buffer, sizeof(buffer),
                    "%04d-%02d-%02dT%02d:%02d:%02d+09:00", year(), month(),
                    day(), hour(), minutes(), seconds());
      //
      return std::string(buffer);
    }
  };
};
