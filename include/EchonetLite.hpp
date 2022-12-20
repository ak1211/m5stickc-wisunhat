// Copyright (c) 2022 Akihiro Yamamoto.
// Licensed under the MIT License <https://spdx.org/licenses/MIT.html>
// See LICENSE file in the project root for full license information.
//
#pragma once
#include <array>
#include <chrono>
#include <cstdint>
#include <ctime>
#include <iomanip>
#include <queue>
#include <ratio>
#include <sstream>
#include <string>
#include <tuple>
#include <variant>
#include <vector>

#include "Application.hpp"

using namespace std::literals::string_literals;
using namespace std::literals::string_view_literals;

// ECHONET Lite の UDP ポート番号
constexpr std::string_view EchonetLiteUdpPort{"0E1A"};

// ECHONET Lite 電文ヘッダー
struct EchonetLiteEHeader final {
  uint8_t u8[2];
};
static_assert(sizeof(EchonetLiteEHeader) == 2);
//
bool operator==(const EchonetLiteEHeader &left,
                const EchonetLiteEHeader &right) {
  return std::equal(std::begin(left.u8), std::end(left.u8),  //
                    std::begin(right.u8), std::end(right.u8) //
  );
}
//
std::string to_string(EchonetLiteEHeader ehd) {
  std::ostringstream oss;
  oss << std::uppercase << std::hex;
  oss << std::setfill('0') << std::setw(2) << +ehd.u8[0];
  oss << std::setfill('0') << std::setw(2) << +ehd.u8[1];
  return oss.str();
}

// ECHONET Lite 電文ヘッダー
// 0x1081で固定
// EHD1: 0x10 (ECHONET Lite規格)
// EHD2: 0x81 (規定電文形式)
constexpr EchonetLiteEHeader EchonetLiteEHD{0x10, 0x81};

// ECHONET Lite オブジェクト指定
union EchonetLiteObjectCode {
  uint8_t u8[3]; // 3バイト表現(デフォルトコンストラクタ)
  struct {
    uint8_t class_group;   // byte.1 クラスグループコード
    uint8_t class_code;    // byte.2 クラスコード
    uint8_t instance_code; // byte.3 インスタンスコード
  };
};
static_assert(sizeof(EchonetLiteObjectCode) == 3);
//
bool operator==(const EchonetLiteObjectCode &left,
                const EchonetLiteObjectCode &right) {
  return std::equal(std::begin(left.u8), std::end(left.u8),  //
                    std::begin(right.u8), std::end(right.u8) //
  );
}
//
std::string to_string(EchonetLiteObjectCode eoj) {
  std::ostringstream oss;
  oss << std::uppercase << std::hex;
  oss << std::setfill('0') << std::setw(2) << +eoj.u8[0];
  oss << std::setfill('0') << std::setw(2) << +eoj.u8[1];
  oss << std::setfill('0') << std::setw(2) << +eoj.u8[2];
  return oss.str();
}

// トランザクションID
struct EchonetLiteTransactionId final {
  uint8_t u8[2];
};
static_assert(sizeof(EchonetLiteTransactionId) == 2);
//
std::string to_string(EchonetLiteTransactionId tid) {
  std::ostringstream oss;
  oss << std::uppercase << std::hex;
  oss << std::setfill('0') << std::setw(2) << +tid.u8[0];
  oss << std::setfill('0') << std::setw(2) << +tid.u8[1];
  return oss.str();
}

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
static_assert(sizeof(EchonetLiteESV) == 1);

std::string to_string(EchonetLiteESV esv) {
  std::ostringstream oss;
  oss << std::uppercase << std::hex;
  oss << std::setfill('0') << std::setw(2) << +static_cast<uint8_t>(esv);
  return oss.str();
}

// ECHONET Liteプロパティ
struct EchonetLiteProp final {
  uint8_t epc;   // ECHONET Liteプロパティ
  uint8_t pdc;   // EDTのバイト数
  uint8_t edt[]; // プロパティ値データ
};
static_assert(sizeof(EchonetLiteProp) == 2);

// ECHONET Lite データ (EDATA)
struct EchonetLiteData final {
  EchonetLiteObjectCode seoj; // 送信元ECHONET Liteオブジェクト指定
  EchonetLiteObjectCode deoj; // 相手元ECHONET Liteオブジェクト指定
  EchonetLiteESV esv;         // ECHONET Liteサービス
  uint8_t opc;                // 処理プロパティ数
  EchonetLiteProp props[];    // ECHONET Liteプロパティ
};
static_assert(sizeof(EchonetLiteData) == 8);

// ECHONET Lite フレーム
struct EchonetLiteFrame final {
  EchonetLiteEHeader ehd;       // ECHONET Lite 電文ヘッダー
  EchonetLiteTransactionId tid; // トランザクションID
  EchonetLiteData edata;        // ECHONET Lite データ (EDATA)
};
static_assert(sizeof(EchonetLiteFrame) == 12);

// EDATAからECHONET Liteプロパティを取り出す
std::vector<std::vector<uint8_t>>
splitToEchonetLiteData(const EchonetLiteData &edata) {
  std::vector<std::vector<uint8_t>> result;
  auto ptr = reinterpret_cast<const uint8_t *>(&edata.props[0]);
  for (auto k = 0; k < edata.opc; ++k) {
    std::vector<uint8_t> v;
    uint8_t epc = *ptr++;
    uint8_t pdc = *ptr++;
    v.push_back(epc);
    v.push_back(pdc);
    std::copy_n(ptr, pdc, std::back_inserter(v));
    ptr += pdc;
    result.emplace_back(std::move(v));
  }
  return result;
}

//
std::string to_string(const EchonetLiteFrame &frame) {
  std::ostringstream oss;
  oss << "EHD:"s << to_string(frame.ehd)          //
      << ",TID:"s << to_string(frame.tid)         //
      << ",SEOJ:"s << to_string(frame.edata.seoj) //
      << ",DEOJ:"s << to_string(frame.edata.deoj) //
      << ",ESV:"s << to_string(frame.edata.esv)   //
      << std::uppercase << std::hex               //
      << ",OPC:"s << std::setfill('0') << std::setw(2) << +frame.edata.opc;
  //
  const uint8_t *p = reinterpret_cast<const uint8_t *>(&frame.edata.props[0]);
  for (auto i = 0; i < frame.edata.opc; ++i) {
    uint8_t epc = *p++;
    uint8_t pdc = *p++;
    oss << ",[EPC:"s << std::setfill('0') << std::setw(2) << +epc //
        << ",PDC:"s << std::setfill('0') << std::setw(2) << +pdc;
    if (pdc >= 1) {
      oss << ",EDT:"s;
      for (auto k = 0; k < pdc; ++k) {
        uint8_t octet = *p++;
        oss << std::setfill('0') << std::setw(2) << +octet;
      }
    }
    oss << "]"s;
  }
  return oss.str();
}

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

// 通信用のフレームを作る
std::vector<uint8_t> make_echonet_lite_frame(EchonetLiteTransactionId tid,    //
                                             EchonetLiteESV esv,              //
                                             std::vector<EchonetLiteEPC> epcs //
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

// 係数
struct Coefficient final {
  uint32_t coefficient;
  //
  explicit Coefficient() { coefficient = 1; }
  explicit Coefficient(std::array<uint8_t, 4> array) {
    coefficient =
        (array[0] << 24) | (array[1] << 16) | (array[2] << 8) | array[3];
  }
};
std::string to_string(const SmartElectricEnergyMeter::Coefficient &x) {
  return "coefficient the "s + std::to_string(x.coefficient);
}

// 積算電力量有効桁数
struct EffectiveDigits final {
  uint8_t digits;
  //
  explicit EffectiveDigits(uint8_t init) : digits{init} {}
};
std::string to_string(const SmartElectricEnergyMeter::EffectiveDigits &x) {
  return std::to_string(x.digits) + " effective digits."s;
}

// 積算電力量単位 (正方向、逆方向計測値)
struct Unit final {
  uint8_t unit;
  //
  struct Value final {
    int8_t power10;
    std::string_view description;
  };
  //
  explicit Unit(uint8_t init) : unit{init} {}
  //
  std::optional<Value> find() const {
    switch (unit) {
    // 0x00 : 1kWh      = 10^0
    // 0x01 : 0.1kWh    = 10^-1
    // 0x02 : 0.01kWh   = 10^-2
    // 0x03 : 0.001kWh  = 10^-3
    // 0x04 : 0.0001kWh = 10^-4
    // 0x0A : 10kWh     = 10^1
    // 0x0B : 100kWh    = 10^2
    // 0x0C : 1000kWh   = 10^3
    // 0x0D : 10000kWh  = 10^4
    case 0x00:
      return Value{+0, "*1 kwh"sv};
    case 0x01:
      return Value{-1, "*0.1 kwh"sv};
    case 0x02:
      return Value{-2, "*0.01 kwh"sv};
    case 0x03:
      return Value{-3, "*0.001 kwh"sv};
    case 0x04:
      return Value{-4, "*0.0001 kwh"sv};
    case 0x0A:
      return Value{+1, "*10 kwh"sv};
    case 0x0B:
      return Value{+2, "*100 kwh"sv};
    case 0x0C:
      return Value{+3, "*1000 kwh"sv};
    case 0x0D:
      return Value{+4, "*100000 kwh"sv};
    default:
      return std::nullopt;
    }
  }
  // 1kwhをベースとして, それに対して10のn乗を返す
  std::optional<int8_t> get_powers_of_10() const {
    auto v = find();
    return (v.has_value()) ? std::make_optional(v->power10) : std::nullopt;
  }
  //
  std::optional<std::string_view> get_description() const {
    auto v = find();
    return (v.has_value()) ? std::make_optional(v->description) : std::nullopt;
  }
};
std::string to_string(const SmartElectricEnergyMeter::Unit &x) {
  auto opt = x.get_description();
  if (opt.has_value()) {
    return std::string{opt.value()};
  } else {
    return "no multiplier"s;
  }
}
//
// 測定値
//

//
using Watt = std::chrono::duration<uint32_t>;
// 瞬時電力
struct InstantWatt final {
  Watt watt; // 瞬時電力(単位 W)
  //
  explicit InstantWatt(std::array<uint8_t, 4> init) {
    uint32_t W = (init[0] << 24) | (init[1] << 16) | (init[2] << 8) | init[3];
    watt = Watt{W};
  }
};
bool operator==(const InstantWatt &left, const InstantWatt &right) {
  return left.watt == right.watt;
}
bool operator!=(const InstantWatt &left, const InstantWatt &right) {
  return left.watt != right.watt;
}
std::string to_string(const SmartElectricEnergyMeter::InstantWatt &x) {
  return std::to_string(x.watt.count()) + " W"s;
}

//
using Ampere = std::chrono::duration<double>;
using DeciAmpere = std::chrono::duration<int32_t, std::deci>;
// 瞬時電流
struct InstantAmpere final {
  DeciAmpere ampereR; // R相電流(単位 1/10A == 1 deci A)
  DeciAmpere ampereT; // T相電流(単位 1/10A == 1 deci A)
  //
  explicit InstantAmpere(std::array<uint8_t, 4> init) {
    // R相電流
    uint16_t R = (init[0] << 8) | init[1];
    ampereR = DeciAmpere{R};
    // T相電流
    uint16_t T = (init[2] << 8) | init[3];
    ampereT = DeciAmpere{T};
  }
};
bool operator==(const InstantAmpere &left, const InstantAmpere &right) {
  return (left.ampereR == right.ampereR) && (left.ampereT == right.ampereT);
}
bool operator!=(const InstantAmpere &left, const InstantAmpere &right) {
  return (left.ampereR != right.ampereR) || (left.ampereT != right.ampereT);
}
std::string to_string(const SmartElectricEnergyMeter::InstantAmpere &x) {
  using namespace SmartElectricEnergyMeter;
  auto R = std::chrono::duration_cast<Ampere>(x.ampereR);
  auto T = std::chrono::duration_cast<Ampere>(x.ampereT);
  std::ostringstream oss;
  oss << std::fixed << std::setprecision(1) //
      << "R: "sv << R.count() << " A"sv     //
      << ", "sv                             //
      << "T: "sv << T.count() << " A"sv;
  return oss.str();
}

//
using KiloWattHour = std::chrono::duration<double>;
// 定時積算電力量
struct CumulativeWattHour final {
  // 生の受信値
  std::array<uint8_t, 11> originalPayload;
  //
  explicit CumulativeWattHour(const std::array<uint8_t, 11> &init) {
    std::copy(init.begin(), init.end(), originalPayload.begin());
  }
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
  // 日時が0xFFなどの異常値を送ってくることがあるので有効値か確認する
  bool valid() const {
    if (0 <= seconds() && seconds() <= 60) {
      return true;
    } else {
      // 秒が0xFFの応答を返してくることがあるが有効な値ではない
      return false;
    }
  }
  // 時刻をISO8601形式で得る
  std::optional<std::string> get_iso8601_datetime() const {
    if (valid()) {
      constexpr char format[] = "%04d-%02d-%02dT%02d:%02d:%02d+09:00";
      constexpr std::size_t SIZE{std::size(format) * 2};
      std::string buffer(SIZE, '\0');
      if (auto len = std::snprintf(buffer.data(), SIZE, format, year(), month(),
                                   day(), hour(), minutes(), seconds());
          len >= 0) {
        buffer.resize(len);
        return buffer;
      }
    }
    return std::nullopt;
  }
  // 日本標準時で時刻をstd::time_t形式で得る
  std::optional<std::time_t> get_time_t() const {
    if (valid()) {
      std::tm at{};
      at.tm_year = year() - 1900;
      at.tm_mon = month() - 1;
      at.tm_mday = day();
      at.tm_hour = hour();
      at.tm_min = minutes();
      at.tm_sec = seconds();
      return std::mktime(&at);
    } else {
      return std::nullopt;
    }
  }
};
bool operator==(const CumulativeWattHour &left,
                const CumulativeWattHour &right) {
  return left.originalPayload == right.originalPayload;
}
bool operator!=(const CumulativeWattHour &left,
                const CumulativeWattHour &right) {
  return left.originalPayload != right.originalPayload;
}
std::string to_string(const SmartElectricEnergyMeter::CumulativeWattHour &x) {
  std::ostringstream oss;
  oss << std::setw(4) << +(x.year()) << "/"sv;
  oss << std::setw(2) << +(x.month()) << "/"sv;
  oss << std::setw(2) << +(x.day()) << " "sv;
  oss << std::setfill('0') << std::setw(2) << +(x.hour()) << ":"sv;
  oss << std::setfill('0') << std::setw(2) << +(x.minutes()) << ":"sv;
  oss << std::setfill('0') << std::setw(2) << +(x.seconds()) << " "sv;
  oss << x.raw_cumlative_watt_hour();
  return oss.str();
}

// 積算電力量
KiloWattHour cumlative_kilo_watt_hour(std::tuple<CumulativeWattHour, //
                                                 Coefficient,        //
                                                 Unit>               //
                                          in) {
  auto &[cwh, coeff, unit] = in;
  // 係数
  // KWhへの乗数
  auto powers_of_10 = unit.get_powers_of_10().value_or(0);
  auto multiplier = std::pow(10, powers_of_10);
  return KiloWattHour{coeff.coefficient * cwh.raw_cumlative_watt_hour() *
                      multiplier};
}

// 電力量
std::string to_string_cumlative_kilo_watt_hour(
    CumulativeWattHour cwh, std::optional<Coefficient> opt_coeff, Unit unit) {
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

} // namespace SmartElectricEnergyMeter

namespace SmartElectricEnergyMeter {
//
// スマートメーターから受信した値
//
using ReceivedMessage =
    std::variant<Coefficient, EffectiveDigits, Unit, InstantWatt, InstantAmpere,
                 CumulativeWattHour>;

//
// 低圧スマート電力量計クラスのイベントを処理する
//
std::vector<ReceivedMessage>
process_echonet_lite_frame(const EchonetLiteFrame &frame) {
  std::vector<ReceivedMessage> result{};
  // EDATAは複数送られてくる
  for (const auto &v : splitToEchonetLiteData(frame.edata)) {
    // EchonetLiteプロパティ
    const EchonetLiteProp *prop =
        reinterpret_cast<const EchonetLiteProp *>(v.data());
    switch (prop->epc) {
    case 0x80: {               // 動作状態
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
    } break;
    case 0x81: {               // 設置場所
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
    } break;
    case 0x88: {               // 異常発生状態
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
    } break;
    case 0x8A: {               // メーカーコード
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
    } break;
    case 0xD3: { // 係数
      auto coeff = Coefficient{};
      if (prop->pdc == 0x04) { // 4バイト
        coeff = Coefficient(
            {prop->edt[0], prop->edt[1], prop->edt[2], prop->edt[3]});
      } else {
        // 係数が無い場合は1倍となる
        coeff = Coefficient{};
      }
      ESP_LOGD(MAIN, "%s", to_string(coeff).c_str());
      result.push_back(coeff);
    } break;
    case 0xD7: {               // 積算電力量有効桁数
      if (prop->pdc == 0x01) { // 1バイト
        auto digits = EffectiveDigits(prop->edt[0]);
        ESP_LOGD(MAIN, "%s", to_string(digits).c_str());
        result.push_back(digits);
      } else {
        ESP_LOGD(MAIN,
                 "pdc is should be 1 bytes, this is "
                 "%d bytes.",
                 prop->pdc);
      }
    } break;
    case 0xE1: { // 積算電力量単位 (正方向、逆方向計測値)
      if (prop->pdc == 0x01) { // 1バイト
        auto unit = Unit(prop->edt[0]);
        ESP_LOGD(MAIN, "%s", to_string(unit).c_str());
        result.push_back(unit);
      } else {
        ESP_LOGD(MAIN,
                 "pdc is should be 1 bytes, this is "
                 "%d bytes.",
                 prop->pdc);
      }
    } break;
    case 0xE5: {               // 積算履歴収集日１
      if (prop->pdc == 0x01) { // 1バイト
        uint8_t day = prop->edt[0];
        ESP_LOGD(MAIN, "day of historical 1: (%d)", day);
      } else {
        ESP_LOGD(MAIN,
                 "pdc is should be 1 bytes, this is "
                 "%d bytes.",
                 prop->pdc);
      }
    } break;
    case 0xE7: {               // 瞬時電力値
      if (prop->pdc == 0x04) { // 4バイト
        auto watt = InstantWatt(
            {prop->edt[0], prop->edt[1], prop->edt[2], prop->edt[3]});
        ESP_LOGD(MAIN, "%s", to_string(watt).c_str());
        result.push_back(watt);
      } else {
        ESP_LOGD(MAIN,
                 "pdc is should be 4 bytes, this is "
                 "%d bytes.",
                 prop->pdc);
      }
    } break;
    case 0xE8: {               // 瞬時電流値
      if (prop->pdc == 0x04) { // 4バイト
        auto ampere = InstantAmpere(
            {prop->edt[0], prop->edt[1], prop->edt[2], prop->edt[3]});
        ESP_LOGD(MAIN, "%s", to_string(ampere).c_str());
        result.push_back(ampere);
      } else {
        ESP_LOGD(MAIN,
                 "pdc is should be 4 bytes, this is "
                 "%d bytes.",
                 prop->pdc);
      }
    } break;
    case 0xEA: {               // 定時積算電力量
      if (prop->pdc == 0x0B) { // 11バイト
        // std::to_arrayの登場はC++20からなのでこんなことになった
        std::array<uint8_t, 11> memory;
        std::copy_n(prop->edt, memory.size(), memory.begin());
        //
        auto cwh = CumulativeWattHour(memory);
        ESP_LOGD(MAIN, "%s", to_string(cwh).c_str());
        result.push_back(cwh);
      } else {
        ESP_LOGD(MAIN,
                 "pdc is should be 11 bytes, this is "
                 "%d bytes.",
                 prop->pdc);
      }
    } break;
    case 0xED: {               // 積算履歴収集日２
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
    } break;
    default:
      ESP_LOGD(MAIN, "unknown epc: 0x%x", prop->epc);
      break;
    }
  }
  return result;
}
} // namespace SmartElectricEnergyMeter
