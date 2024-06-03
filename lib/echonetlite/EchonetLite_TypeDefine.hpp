// Copyright (c) 2022 Akihiro Yamamoto.
// Licensed under the MIT License <https://spdx.org/licenses/MIT.html>
// See LICENSE file in the project root for full license information.
//
#pragma once
#include "TypeDefine.hpp"
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <ctime>
#include <iomanip>
#include <iterator>
#include <ratio>
#include <string>
#include <variant>
#include <vector>

// ECHONET Lite の UDP ポート番号
constexpr auto EchonetLiteUdpPort = HexedU16{0x0E1A};

// ECHONET Lite 電文ヘッダー
struct EchonetLiteEHeader final {
  std::array<uint8_t, 2> u8;
  //
  friend std::ostream &operator<<(std::ostream &, const EchonetLiteEHeader &);
  constexpr EchonetLiteEHeader(std::array<uint8_t, 2> init = {}) : u8{init} {}
  operator std::string() {
    std::ostringstream oss;
    oss << *this;
    return oss.str();
  }
  bool operator==(const EchonetLiteEHeader &rhs) { return this->u8 == rhs.u8; }
  bool operator!=(const EchonetLiteEHeader &rhs) { return !(*this == rhs); }
};
static_assert(sizeof(EchonetLiteEHeader) == 2);
inline std::ostream &operator<<(std::ostream &os,
                                const EchonetLiteEHeader &in) {
  auto save = os.flags();
  os << std::uppercase << std::hex                      //
     << std::setfill('0') << std::setw(2) << +in.u8[0]  //
     << std::setfill('0') << std::setw(2) << +in.u8[1]; //
  os.flags(save);
  return os;
}

// ECHONET Lite 電文ヘッダー
// 0x1081で固定
// EHD1: 0x10 (ECHONET Lite規格)
// EHD2: 0x81 (規定電文形式)
constexpr auto EchonetLiteEHD(EchonetLiteEHeader({0x10, 0x81}));

// ECHONET Lite オブジェクト指定
union EchonetLiteObjectCode {
  std::array<uint8_t, 3> u8; // 3バイト表現(デフォルトコンストラクタ)
  struct {
    uint8_t class_group;   // byte.1 クラスグループコード
    uint8_t class_code;    // byte.2 クラスコード
    uint8_t instance_code; // byte.3 インスタンスコード
  };
  constexpr EchonetLiteObjectCode(std::array<uint8_t, 3> init = {})
      : u8{init} {}
  operator std::string();
};
static_assert(sizeof(EchonetLiteObjectCode) == 3);
//
inline bool operator==(const EchonetLiteObjectCode &left,
                       const EchonetLiteObjectCode &right) {
  return left.u8 == right.u8;
}
inline bool operator!=(const EchonetLiteObjectCode &left,
                       const EchonetLiteObjectCode &right) {
  return !(left.u8 == right.u8);
}
inline std::ostream &operator<<(std::ostream &os,
                                const EchonetLiteObjectCode &in) {
  auto save = os.flags();
  os << std::uppercase << std::hex                     //
     << std::setfill('0') << std::setw(2) << +in.u8[0] //
     << std::setfill('0') << std::setw(2) << +in.u8[1] //
     << std::setfill('0') << std::setw(2) << +in.u8[2];
  return os;
}
inline EchonetLiteObjectCode::operator std::string() {
  std::ostringstream oss;
  oss << *this;
  return oss.str();
}

// トランザクションID
struct EchonetLiteTransactionId final {
  std::array<uint8_t, 2> u8;
  //
  friend std::ostream &operator<<(std::ostream &,
                                  const EchonetLiteTransactionId &);
  constexpr EchonetLiteTransactionId(std::array<uint8_t, 2> init = {})
      : u8{init} {}
  operator std::string() {
    std::ostringstream oss;
    oss << *this;
    return oss.str();
  }
};
static_assert(sizeof(EchonetLiteTransactionId) == 2);
inline std::ostream &operator<<(std::ostream &os,
                                const EchonetLiteTransactionId &in) {
  auto save = os.flags();
  os << std::uppercase << std::hex                     //
     << std::setfill('0') << std::setw(2) << +in.u8[0] //
     << std::setfill('0') << std::setw(2) << +in.u8[1];
  return os;
}

//
// ECHONET Liteサービス(ESV)
//
enum class EchonetLiteESV : uint8_t {
  //
  // 要求用ESVコード一覧表
  //
  SetI = 0x60,    // プロパティ値書き込み要求（応答不要）
  SetC = 0x61,    // プロパティ値書き込み要求（応答要）
  Get = 0x62,     // プロパティ値読み出し要求
  INF_REQ = 0x63, // プロパティ値通知要求
  SetGet = 0x6E,  // プロパティ値書き込み・読み出し要求
  //
  // 応答・通知用ESVコード一覧表
  //
  Set_Res = 0x71,    // プロパティ値書き込み応答
  Get_Res = 0x72,    // プロパティ値読み出し応答
  INF = 0x73,        // プロパティ値通知
  INFC = 0x74,       // プロパティ値通知（応答要）
  INFC_Res = 0x7A,   // プロパティ値通知応答
  SetGet_Res = 0x7E, // プロパティ値書き込み・読み出し応答
  //
  // 不可応答用ESVコード一覧表
  //
  SetI_SNA = 0x50,  // プロパティ値書き込み要求不可応答
  SetC_SNA = 0x51,  // プロパティ値書き込み要求不可応答
  Get_SNA = 0x52,   // プロパティ値読み出し不可応答
  INF_SNA = 0x53,   // プロパティ値通知不可応答
  SetGetSNA = 0x5E, // プロパティ値書き込み・読み出し不可応答
  //
};
static_assert(sizeof(EchonetLiteESV) == 1);

inline std::string to_string(EchonetLiteESV esv) {
  std::ostringstream oss;
  oss << std::uppercase << std::hex;
  oss << std::setfill('0') << std::setw(2) << +static_cast<uint8_t>(esv);
  return oss.str();
}

//
// ECHONET Lite機器オブジェクトスーパークラス規定プロパティ
//
enum class EchonetLiteEPC : uint8_t {
  Operation_status = 0x80,      // 動作状態
  Installation_location = 0x81, // 設置場所
  Fault_status = 0x88,          // 異常発生状態
  Manufacturer_code = 0x8A,     // メーカーコード
};
static_assert(sizeof(EchonetLiteEPC) == 1);

// 送信元ECHONET Liteオブジェクト指定
struct EchonetLiteSEOJ {
  EchonetLiteObjectCode s;
  constexpr EchonetLiteSEOJ(EchonetLiteObjectCode in = {}) : s{in} {};
};
// 相手元ECHONET Liteオブジェクト指定
struct EchonetLiteDEOJ {
  EchonetLiteObjectCode d;
  constexpr EchonetLiteDEOJ(EchonetLiteObjectCode in = {}) : d{in} {};
};

// ECHONET Liteプロパティ
struct EchonetLiteProp final {
  uint8_t epc;              // ECHONET Liteプロパティ
  uint8_t pdc;              // EDTのバイト数
  std::vector<uint8_t> edt; // プロパティ値データ
};

// ECHONET Lite データ (EDATA)
struct EchonetLiteData final {
  EchonetLiteSEOJ seoj; // 送信元ECHONET Liteオブジェクト指定
  EchonetLiteDEOJ deoj; // 相手元ECHONET Liteオブジェクト指定
  EchonetLiteESV esv;   // ECHONET Liteサービス
  uint8_t opc;          // 処理プロパティ数
  std::vector<EchonetLiteProp> props; // ECHONET Liteプロパティ
};

// ECHONET Lite フレーム
struct EchonetLiteFrame final {
  EchonetLiteEHeader ehd;       // ECHONET Lite 電文ヘッダー
  EchonetLiteTransactionId tid; // トランザクションID
  EchonetLiteData edata;        // ECHONET Lite データ (EDATA)
};

//
inline std::string to_string(const EchonetLiteFrame &frame) {
  using namespace std::literals::string_literals;
  std::ostringstream oss;
  oss << "EHD:"s << frame.ehd                   //
      << ",TID:"s << frame.tid                  //
      << ",SEOJ:"s << frame.edata.seoj.s        //
      << ",DEOJ:"s << frame.edata.deoj.d        //
      << ",ESV:"s << to_string(frame.edata.esv) //
      << ",OPC:"s << HexedU8(frame.edata.opc);
  for (const auto &prop : frame.edata.props) {
    oss << ",EPC:"s << HexedU8(prop.epc) //
        << ",PDC:"s << HexedU8(prop.pdc);
    if (prop.edt.size() >= 1) {
      oss << ",EDT:"s;
      std::copy(prop.edt.cbegin(), prop.edt.cend(),
                std::ostream_iterator<HexedU8>(oss));
    }
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
constexpr auto EchonetLiteEOJ(EchonetLiteObjectCode({0x05, 0xFF, 0x01}));
} // namespace HomeController

//
// スマートメータに接続時に送られてくるECHONET Liteオブジェクト
//
namespace NodeProfileClass {
// グループコード 0x0E (ノードプロファイルクラス)
// クラスコード 0xF0
// インスタンスコード 0x01 (一般ノード)
constexpr auto EchonetLiteEOJ(EchonetLiteObjectCode({0x0E, 0xF0, 0x01}));
} // namespace NodeProfileClass

//
// 低圧スマート電力量メータクラス規定
//
namespace ElectricityMeter {
// クラスグループコード 0x02
// クラスコード 0x88
// インスタンスコード 0x01
constexpr auto EchonetLiteEOJ(EchonetLiteObjectCode({0x02, 0x88, 0x01}));

//
// ECHONET Liteプロパティ
//
enum class EchonetLiteEPC : uint8_t {
  // スーパークラスより
  Operation_status = 0x80,      // 動作状態
  Installation_location = 0x81, // 設置場所
  Fault_status = 0x88,          // 異常発生状態
  Manufacturer_code = 0x8A,     // メーカーコード
  //
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
static_assert(sizeof(EchonetLiteEPC) == 1);

// 係数
struct Coefficient final {
  uint32_t coefficient;
  //
  explicit constexpr Coefficient() : coefficient{1} {}
  explicit Coefficient(std::array<uint8_t, 4> init) {
    coefficient = (init[0] << 24) | (init[1] << 16) | (init[2] << 8) | init[3];
  }
  bool operator==(const Coefficient &rhs) {
    return this->coefficient == rhs.coefficient;
  }
  bool operator!=(const Coefficient &rhs) { return !(*this == rhs); }
};

// 積算電力量有効桁数
struct EffectiveDigits final {
  uint8_t digits;
  //
  explicit EffectiveDigits(uint8_t init) : digits{init} {}
  bool operator==(const EffectiveDigits &rhs) {
    return this->digits == rhs.digits;
  }
  bool operator!=(const EffectiveDigits &rhs) { return !(*this == rhs); }
};

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
    using namespace std::literals::string_view_literals;
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
    if (auto v = find()) {
      return v->power10;
    } else {
      return std::nullopt;
    }
  }
  //
  std::optional<std::string> get_description() const {
    if (auto v = find()) {
      return std::string(v->description);
    } else {
      return std::nullopt;
    }
  }
  bool operator==(const Unit &rhs) { return this->unit == rhs.unit; }
  bool operator!=(const Unit &rhs) { return !(*this == rhs); }
};

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
  bool operator==(const InstantWatt &rhs) { return this->watt == rhs.watt; }
  bool operator!=(const InstantWatt &rhs) { return !(*this == rhs); }
};
inline std::string to_string(const ElectricityMeter::InstantWatt &x) {
  using namespace std::literals::string_literals;
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
  bool operator==(const InstantAmpere &rhs) {
    return (this->ampereR == rhs.ampereR) && (this->ampereT == rhs.ampereT);
  }
  bool operator!=(const InstantAmpere &rhs) { return !(*this == rhs); }
};
inline std::string to_string(const ElectricityMeter::InstantAmpere &x) {
  using namespace std::literals::string_view_literals;
  using namespace ElectricityMeter;
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
  explicit CumulativeWattHour(const std::array<uint8_t, 11> &init)
      : originalPayload{init} {}
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
  bool operator==(const CumulativeWattHour &rhs) {
    return this->originalPayload == rhs.originalPayload;
  }
  bool operator!=(const CumulativeWattHour &rhs) { return !(*this == rhs); }
};

inline std::string to_string(const ElectricityMeter::CumulativeWattHour &x) {
  using namespace std::literals::string_view_literals;
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

} // namespace ElectricityMeter
