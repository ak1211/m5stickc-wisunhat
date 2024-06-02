// Copyright (c) 2022 Akihiro Yamamoto.
// Licensed under the MIT License <https://spdx.org/licenses/MIT.html>
// See LICENSE file in the project root for full license information.
//
#pragma once
#include "EchonetLite.hpp"
#include "TypeDefine.hpp"
#include <chrono>
#include <cinttypes>
#include <cstring>
#include <functional>
#include <future>
#include <iterator>
#include <numeric>
#include <optional>
#include <ostream>
#include <sstream>
#include <string>
#include <variant>

#include <M5Unified.h>

namespace Bp35a1 {
using namespace std::literals::string_literals;
using namespace std::literals::string_view_literals;

constexpr static auto RETRY_TIMEOUT = std::chrono::seconds{10};

// メッセージを表示する関数の型
using DisplayMessageT =
    std::function<void(const std::string &message, void *user_data)>;

// 受信メッセージを破棄する
extern void clear_read_buffer(Stream &commport);

// ストリームからsepで区切られたトークンを得る
extern std::pair<std::string, std::string> get_token(Stream &commport, int sep);

// CRLFを付けてストリームに1行書き込む関数
extern void write_with_crln(Stream &commport, const std::string &line);

// 成功ならtrue, それ以外ならfalse
extern bool has_ok(Stream &commport, std::chrono::seconds timeout);

// IPV6アドレス
struct IPv6Addr {
  std::array<HexedU16, 8> fields;
  explicit IPv6Addr(const std::array<HexedU16, 8> &init =
                        {
                            HexedU16{},
                            HexedU16{},
                            HexedU16{},
                            HexedU16{},
                            HexedU16{},
                            HexedU16{},
                            HexedU16{},
                            HexedU16{},
                        })
      : fields{init} {}
  operator std::string() const;
};
inline std::istream &operator>>(std::istream &is, IPv6Addr &v) {
  auto save = is.flags();
  char colon;
  is >> v.fields[0] >> colon  //
      >> v.fields[1] >> colon //
      >> v.fields[2] >> colon //
      >> v.fields[3] >> colon //
      >> v.fields[4] >> colon //
      >> v.fields[5] >> colon //
      >> v.fields[6] >> colon //
      >> v.fields[7];         //
  is.flags(save);
  return is;
}
inline std::ostream &operator<<(std::ostream &os, const IPv6Addr &v) {
  auto save = os.flags();
  auto colon = ":"s;
  os << v.fields[0] << colon //
     << v.fields[1] << colon //
     << v.fields[2] << colon //
     << v.fields[3] << colon //
     << v.fields[4] << colon //
     << v.fields[5] << colon //
     << v.fields[6] << colon //
     << v.fields[7];         //
  os.flags(save);
  return os;
}
inline std::optional<IPv6Addr> makeIPv6Addr(const std::string &in) {
  IPv6Addr v;
  std::istringstream iss{in};
  iss >> v;
  return (iss.fail()) ? std::nullopt : std::make_optional(v);
}
inline IPv6Addr::operator std::string() const {
  std::ostringstream oss;
  oss << *this;
  return oss.str();
}

//
// EVENTメッセージの値
//
struct ResEvent final {
  HexedU8 num;     // イベント番号
  IPv6Addr sender; // イベントのトリガーとなったメッセージの発信元アドレス
  std::optional<HexedU8> param; // イベント固有の引数
};
inline std::ostream &operator<<(std::ostream &os, const ResEvent &in) {
  auto save = os.flags();
  os << "num:" << in.num //
     << ",sender:" << in.sender;
  if (in.param.has_value()) {
    os << ",param:" << in.param.value();
  } else {
    os << ",param:NA";
  }
  os.flags(save);
  return os;
}
inline std::string to_string(const ResEvent &in) {
  std::ostringstream oss;
  oss << in;
  return oss.str();
}

//
// EPANDESCメッセージの値
//
struct ResEpandesc final {
  HexedU8 channel; // 発見したPANの周波数(論理チャンネル番号)
  HexedU8 channel_page; // 発見したPANのチャンネルページ
  HexedU16 pan_id;      //  発見したPANのPAN ID
  HexedU64 addr;        // アクティブスキャン応答元のアドレス
  HexedU8 lqi;          // 受信したビーコンの受信ED値(RSSI)
  std::string pairid; // (IEが含まれる場合)相手から受信したPairingID
};
inline std::ostream &operator<<(std::ostream &os, const ResEpandesc &in) {
  auto save = os.flags();
  os << "channel:" << in.channel            //
     << ",channel_page:" << in.channel_page //
     << ",pan_id:" << in.pan_id             //
     << ",addr:" << in.addr                 //
     << ",lqi:" << in.lqi                   //
     << ",pairid:" << in.pairid;
  os.flags(save);
  return os;
}
inline std::string to_string(const ResEpandesc &in) {
  std::ostringstream oss;
  oss << in;
  return oss.str();
}

//
// ERXUDPメッセージの値
//
struct ResErxudp final {
  IPv6Addr sender;       // 送信元IPv6アドレス
  IPv6Addr dest;         // 送信先IPv6アドレス
  HexedU16 rport;        // 送信元ポート番号
  HexedU16 lport;        // 送信元ポート番号
  std::string senderlla; // 送信元のMACアドレス
  HexedU8 secured;       // MACフレームが暗号化されていた または
                         // MACフレームが暗号化されていなかった
  HexedU16 datalen;      // データの長さ
  std::vector<uint8_t> data; // データ
};
inline std::ostream &operator<<(std::ostream &os, const ResErxudp &in) {
  auto save = os.flags();
  os << "sender:" << in.sender        //
     << ",dest:" << in.dest           //
     << ",rport:" << in.rport         //
     << ",lport:" << in.lport         //
     << ",senderlla:" << in.senderlla //
     << ",secured:" << in.secured     //
     << ",datalen:" << in.datalen     //
     << ",data:";
  std::copy(in.data.begin(), in.data.end(), std::ostream_iterator<HexedU8>(os));
  os.flags(save);
  return os;
}
inline std::string to_string(const ResErxudp &in) {
  std::ostringstream oss;
  oss << in;
  return oss.str();
}

//
// BP35A1から受け取ったイベント
//
using Response = std::variant<ResEvent, ResEpandesc, ResErxudp>;

//
// ipv6 アドレスを受け取る関数
//
extern std::optional<IPv6Addr> get_ipv6_address(Stream &commport,
                                                std::chrono::seconds timeout,
                                                const std::string &addr);

//
// 受信
//
extern std::optional<Response> receive_response(Stream &commport);

//
// スマートメーターの識別子
//
struct SmartMeterIdentifier final {
  IPv6Addr ipv6_address;
  HexedU8 channel;
  HexedU16 pan_id;
};
inline std::ostream &operator<<(std::ostream &os,
                                const SmartMeterIdentifier &in) {
  auto save = os.flags();
  os << "ipv6_address:" << in.ipv6_address //
     << ",channel:"s << in.channel         //
     << ",pan_id:"s << in.pan_id;
  os.flags(save);
  return os;
}
inline std::string to_string(const SmartMeterIdentifier &in) {
  std::ostringstream oss;
  oss << in;
  return oss.str();
}

// 要求を送る
extern bool
send_request(Stream &commport, const SmartMeterIdentifier &smart_meter_ident,
             EchonetLiteTransactionId tid,
             const std::vector<SmartElectricEnergyMeter::EchonetLiteEPC> epcs);

// 接続(PANA認証)要求を送る
extern bool connect(std::ostream &os, std::chrono::seconds timeout,
                    Stream &commport, SmartMeterIdentifier smart_meter_ident);

// アクティブスキャンを実行する
extern std::optional<ResEpandesc> do_active_scan(std::ostream &os,
                                                 Stream &commport,
                                                 std::chrono::seconds timeout);

// BP35A1を起動してアクティブスキャンを開始する
extern std::optional<SmartMeterIdentifier> startup_and_find_meter(
    std::ostream &os, Stream &commport, const std::string &route_b_id,
    const std::string &route_b_password, std::chrono::seconds timeout);
} // namespace Bp35a1