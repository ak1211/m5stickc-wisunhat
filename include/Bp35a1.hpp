// Copyright (c) 2022 Akihiro Yamamoto.
// Licensed under the MIT License <https://spdx.org/licenses/MIT.html>
// See LICENSE file in the project root for full license information.
//
#pragma once
#include <M5StickCPlus.h>
#undef min
#include <SmartWhm.hpp>
#include <cinttypes>
#include <cstring>
#include <iterator>
#include <map>
#include <optional>
#include <sstream>
#include <string>

using namespace std::literals::string_literals;
using namespace std::literals::string_view_literals;

//
// template <class T> std::optional<T> from_string(const std::string &in);

// 2桁の16進数
struct HexedU8 final {
  uint8_t u8;
  explicit HexedU8(uint8_t in = 0U) { u8 = in; }
  operator std::string() const;
};
std::istream &operator>>(std::istream &is, HexedU8 &v) {
  auto save = is.flags();
  uint32_t u32;
  is >> std::setw(2) >> std::hex >> u32;
  v = HexedU8{static_cast<uint8_t>(u32)};
  is.flags(save);
  return is;
}
std::ostream &operator<<(std::ostream &os, const HexedU8 &v) {
  auto save = os.flags();
  os << std::setfill('0') << std::setw(2) << std::hex << std::uppercase
     << +v.u8;
  os.flags(save);
  return os;
}
std::optional<HexedU8> makeHexedU8(const std::string &in) {
  HexedU8 v;
  std::istringstream iss{in};
  iss >> v;
  return (iss.good()) ? std::make_optional(v) : std::nullopt;
}
HexedU8::operator std::string() const {
  std::ostringstream oss;
  oss << *this;
  return oss.str();
}
/*
//
template <> std::optional<HexedU8> from_string(const std::string &in) {
  HexedU8 result;
  if (std::sscanf(in.c_str(), "%02hhX", &result.u8) == 1) {
    return std::make_optional(result);
  } else {
    return std::nullopt;
  }
}
//
std::string to_string(HexedU8 in) {
  std::string buff(10, '\0');
  auto n = std::snprintf(buff.data(), buff.size(), "%02X", in.u8);
  if (n < 0) {
    n = 0;
  }
  buff.resize(n);
  return buff;
}
*/

// 4桁の16進数
struct HexedU16 final {
  uint16_t u16;
  explicit HexedU16(uint16_t in = 0U) { u16 = in; }
  operator std::string() const;
};
std::istream &operator>>(std::istream &is, HexedU16 &v) {
  auto save = is.flags();
  is >> std::setw(4) >> std::hex >> v.u16;
  is.flags(save);
  return is;
}
std::ostream &operator<<(std::ostream &os, const HexedU16 &v) {
  auto save = os.flags();
  os << std::setfill('0') << std::setw(4) << std::hex << std::uppercase
     << +v.u16;
  os.flags(save);
  return os;
}
std::optional<HexedU16> makeHexedU16(const std::string &in) {
  HexedU16 v;
  std::istringstream iss{in};
  iss >> v;
  return (iss.good()) ? std::make_optional(v) : std::nullopt;
}
HexedU16::operator std::string() const {
  std::ostringstream oss;
  oss << *this;
  return oss.str();
}
/*
//
template <> std::optional<HexedU16> from_string(const std::string &in) {
  HexedU16 result;
  if (std::sscanf(in.c_str(), "%04hX", &result.u16) == 1) {
    return std::make_optional(result);
  } else {
    return std::nullopt;
  }
}
//
std::string to_string(HexedU16 in) {
  std::string buff(10, '\0');
  auto n = std::snprintf(buff.data(), buff.size(), "%04X", in.u16);
  if (n < 0) {
    n = 0;
  }
  buff.resize(n);
  return buff;
}
*/

// 16桁の16進数
struct HexedU64 final {
  uint64_t u64;
  explicit HexedU64(uint64_t in = 0U) { u64 = in; }
  operator std::string() const;
};
std::istream &operator>>(std::istream &is, HexedU64 &v) {
  auto save = is.flags();
  is >> std::setw(16) >> std::hex >> v.u64;
  is.flags(save);
  return is;
}
std::ostream &operator<<(std::ostream &os, const HexedU64 &v) {
  auto save = os.flags();
  os << std::setfill('0') << std::setw(16) << std::hex << std::uppercase
     << +v.u64;
  os.flags(save);
  return os;
}
std::optional<HexedU64> makeHexedU64(const std::string &in) {
  HexedU64 v;
  std::istringstream iss{in};
  iss >> v;
  return (iss.good()) ? std::make_optional(v) : std::nullopt;
}
HexedU64::operator std::string() const {
  std::ostringstream oss;
  oss << *this;
  return oss.str();
}
/*
//
template <> std::optional<HexedU64> from_string(const std::string &in) {
  HexedU64 result;
  if (std::sscanf(in.c_str(), "%016" SCNu64, &result.u64) == 1) {
    return std::make_optional(result);
  } else {
    return std::nullopt;
  }
}
//
std::string to_string(HexedU64 in) {
  std::string buff(20, '\0');
  auto n = std::snprintf(buff.data(), buff.size(), "%016" PRIu64, in.u64);
  if (n < 0) {
    n = 0;
  }
  buff.resize(n);
  return buff;
}
*/

// IPV6アドレス
struct IPv6Addr {
  std::array<HexedU16, 8> hex;
  explicit IPv6Addr(std::array<HexedU16, 8> in = {
                        HexedU16{},
                        HexedU16{},
                        HexedU16{},
                        HexedU16{},
                        HexedU16{},
                        HexedU16{},
                        HexedU16{},
                        HexedU16{},
                    }) {
    std::copy(std::begin(in), std::end(in), std::begin(hex));
  }
  operator std::string() const;
};
std::istream &operator>>(std::istream &is, IPv6Addr &v) {
  auto save = is.flags();
  char colon;
  is >> v.hex[0] >> colon;
  is >> v.hex[1] >> colon;
  is >> v.hex[2] >> colon;
  is >> v.hex[3] >> colon;
  is >> v.hex[4] >> colon;
  is >> v.hex[5] >> colon;
  is >> v.hex[6] >> colon;
  is >> v.hex[7];
  is.flags(save);
  return is;
}
std::ostream &operator<<(std::ostream &os, const IPv6Addr &v) {
  auto save = os.flags();
  auto colon = ":"s;
  os << v.hex[0] << colon;
  os << v.hex[1] << colon;
  os << v.hex[2] << colon;
  os << v.hex[3] << colon;
  os << v.hex[4] << colon;
  os << v.hex[5] << colon;
  os << v.hex[6] << colon;
  os << v.hex[7];
  os.flags(save);
  return os;
}
std::optional<IPv6Addr> makeIPv6Addr(const std::string &in) {
  IPv6Addr v;
  std::istringstream iss{in};
  iss >> v;
  return (iss.good()) ? std::make_optional(v) : std::nullopt;
}
IPv6Addr::operator std::string() const {
  std::ostringstream oss;
  oss << *this;
  return oss.str();
}
/*
//
template <> std::optional<IPv6Addr> from_string(const std::string &in) {
  IPv6Addr result;
  if (std::sscanf(in.c_str(),
                  "%04hX:" // 0
                  "%04hX:" // 1
                  "%04hX:" // 2
                  "%04hX:" // 3
                  "%04hX:" // 4
                  "%04hX:" // 5
                  "%04hX:" // 6
                  "%04hX", // 7
                  &result.hex[0].u16, &result.hex[1].u16, &result.hex[2].u16,
                  &result.hex[3].u16, &result.hex[4].u16, &result.hex[5].u16,
                  &result.hex[6].u16, &result.hex[7].u16) == 8) {
    return std::make_optional(result);
  } else {
    return std::nullopt;
  }
}
//
std::string to_string(IPv6Addr in) {
  const auto colon = ":"s;
  return to_string(in.hex[0]) + colon + // 0
         to_string(in.hex[1]) + colon + // 1
         to_string(in.hex[2]) + colon + // 2
         to_string(in.hex[3]) + colon + // 3
         to_string(in.hex[4]) + colon + // 4
         to_string(in.hex[5]) + colon + // 5
         to_string(in.hex[6]) + colon + // 6
         to_string(in.hex[7]);          // 7
}
*/

//
// EVENTメッセージの値
//
struct Bp35a1EVENT final {
  HexedU8 num;     // イベント番号
  IPv6Addr sender; // イベントのトリガーとなったメッセージの発信元アドレス
  std::optional<HexedU8> param; // イベント固有の引数
};

//
// EPANDESCメッセージの値
//
struct Bp35a1EPANDESC final {
  HexedU8 channel; // 発見したPANの周波数(論理チャンネル番号)
  HexedU8 channel_page; // 発見したPANのチャンネルページ
  HexedU16 pan_id;      //  発見したPANのPAN ID
  HexedU64 addr;        // アクティブスキャン応答元のアドレス
  HexedU8 lqi;          // 受信したビーコンの受信ED値(RSSI)
  std::string pairid; // (IEが含まれる場合)相手から受信したPairingID
};

//
// ERXUDPメッセージの値
//
struct Bp35a1ERXUDP final {
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

using Bp35a1Res = std::variant<Bp35a1EVENT, Bp35a1EPANDESC, Bp35a1ERXUDP>;

// ストリームからsepで区切られたトークンを得る
std::pair<std::string, std::string> get_token(Stream &commport,
                                              char sep = ' ') {
  constexpr std::size_t LINE_BUFFER_SIZE{512};
  std::string separator{2, '\0'};
  std::string token{};
  for (auto count = 0; count < LINE_BUFFER_SIZE; ++count) {
    if (!commport.available()) {
      // ストリームの最後まで読み込んだので脱出する
      break;
    }
    // 1文字読み込んで
    auto ch = commport.read();
    if (ch < 0) {
      // ストリームの最後まで読み込んだので脱出する
      break;
    } else if (ch == sep) {
      // sepを見つけたので脱出する
      separator.push_back(ch);
      break;
    } else if (ch == '\r') { // CarriageReturn
      separator.push_back(ch);
      if (commport.peek() == '\n') { // CR + LF
        auto next = commport.read();
        separator.push_back(next);
      }
      break;
    } else if (ch == '\n') { // LineFeed
      separator.push_back(ch);
      break;
    } else {
      // 読み込んだ文字はバッファへ追加する
      token.push_back(static_cast<char>(ch));
    }
  }
  token.shrink_to_fit();
  return {token, separator};
}

//
// 受信
//
std::optional<Bp35a1Res> receive_response(Stream &commport) {
  // EVENTを受信する
  auto rx_event = [&commport](const std::string &) -> Bp35a1EVENT {
    Bp35a1EVENT ev;
    std::array<std::string, 3> tokens;
    for (auto &token : tokens) {
      auto [x, sep] = get_token(commport);
      token = x;
      if (sep != " "s) {
        break;
      }
    }
    ev.num = makeHexedU8(tokens[0]).value_or(HexedU8{});
    ev.sender = makeIPv6Addr(tokens[1]).value_or(IPv6Addr{});
    ev.param = makeHexedU8(tokens[2]);
    return ev;
  };
  // EPANDESCを受信する
  auto rx_epandesc = [&commport](const std::string &) -> Bp35a1EPANDESC {
    Bp35a1EPANDESC ev;
    std::array<std::pair<std::string, std::string>, 6> tokens;
    for (auto &token : tokens) {
      auto [left, sep1] = get_token(commport, ':');
      auto [right, sep2] = get_token(commport, ':');
      token = {left, right};
    }

    for (const auto [left, right] : tokens) {
      // 先頭に空白があるからここではfindで確認する
      if (left.find("Channel") != std::string::npos) {
        ev.channel = makeHexedU8(right).value_or(HexedU8{});
      } else if (left.find("Channel Page") != std::string::npos) {
        ev.channel_page = makeHexedU8(right).value_or(HexedU8{});
      } else if (left.find("Pan ID") != std::string::npos) {
        ev.pan_id = makeHexedU16(right).value_or(HexedU16{});
      } else if (left.find("Addr") != std::string::npos) {
        ev.addr = makeHexedU64(right).value_or(HexedU64{});
      } else if (left.find("LQI") != std::string::npos) {
        ev.lqi = makeHexedU8(right).value_or(HexedU8{});
      } else if (left.find("PairID") != std::string::npos) {
        ev.pairid = right;
      } else {
        ESP_LOGE(MAIN, "Unexpected input. \"%s\":\"%s\"", left, right);
      }
    }
    return ev;
  };
  // ERXUDPを受信する
  auto rx_erxudp = [&commport](const std::string &) -> Bp35a1ERXUDP {
    Bp35a1ERXUDP ev;
    std::array<std::string, 7> tokens;
    for (auto &token : tokens) {
      auto [x, sep] = get_token(commport);
      token = x;
      if (sep != " ") {
        break;
      }
    }
    ev.sender = makeIPv6Addr(tokens[0]).value_or(IPv6Addr{});
    ev.dest = makeIPv6Addr(tokens[1]).value_or(IPv6Addr{});
    ev.rport = makeHexedU16(tokens[2]).value_or(HexedU16{});
    ev.lport = makeHexedU16(tokens[3]).value_or(HexedU16{});
    ev.senderlla = tokens[4];
    ev.secured = makeHexedU8(tokens[5]).value_or(HexedU8{});
    ev.datalen = makeHexedU16(tokens[6]).value_or(HexedU16{});
    // メモリーを確保して
    ev.data.resize(ev.datalen.u16);
    // データの長さ分読み込む
    commport.readBytes(ev.data.data(), ev.data.size());
    return ev;
  };
  // よくわからないイベントを受信する
  auto rx_fallthrough =
      [&commport](const std::string &token) -> std::optional<Bp35a1Res> {
    ESP_LOGD(MAIN, "Unknown event: \"%s\"", token.c_str());
    return std::nullopt;
  };
  //
  //
  //
  auto [token, sep] = get_token(commport);
  return (token == "EVENT"sv)      ? rx_event(token)
         : (token == "EPANDESC"sv) ? rx_epandesc(token)
         : (token == "ERXUDP"sv)   ? rx_erxudp(token)
                                   : rx_fallthrough(token);
}
/*
// 受信
std::optional<Response> watch_response() {
 std::optional<std::string> opt_tag = get_token();
 // 受信したメッセージを解析する
 if (!opt_tag.has_value()) {
   // 何も受け取れなかった場合
   return std::nullopt;
 } else if (opt_tag.value() == "EVENT"sv) {
   //
   // EVENTを受け取った
   //
   return [this]() -> std::optional<Response> {
     // EVENTメッセージの値
     constexpr std::array<std::string_view, 3> keys = {
         "NUM"sv,    // イベント番号
         "SENDER"sv, // イベントのトリガーとなったメッセージの発信元アドレス
         "PARAM"sv,  // イベント固有の引数
     };
     //
     std::map<std::string, std::string> kv{};
     for (const auto &key : keys) {
       if (auto opt_token = get_token(); opt_token.has_value()) {
         // 受け取った値をkey-valueストアへ
         kv.insert(std::make_pair(key, opt_token.value()));
       } else {
         // 値が不足しているが
         // 3番目のパラメータがないことがあるので
         // とくに何もしません
       }
     }
     return Response{std::time(nullptr), Response::Tag::EVENT,
                     std::move(kv)};
   }();
 } else if (opt_tag.value() == "EPANDESC"sv) {
   //
   // EPANDESCを受け取った
   //
   return [this]() -> std::optional<Response> {
     // EPANDESCメッセージの各行ごとの値
     // この順番どおりに読み込む
     constexpr std::array<std::string_view, 6> keys = {
         "Channel"sv, // 発見したPANの周波数(論理チャンネル番号)
         "Channel Page"sv, // 発見したPANのチャンネルページ
         "Pan ID"sv, //  発見したPANのPAN ID
         "Addr"sv, // アクティブスキャン応答元のアドレス
         "LQI"sv, // 受信したビーコンの受信ED値(RSSI)
         "PairID"sv, // (IEが含まれる場合)相手から受信したPairingID
     };
     //
     std::map<std::string, std::string> kv{};
     for (const auto &key : keys) {
       std::optional<std::string> line{};
       for (auto retry = 1; retry <= retry_limits; ++retry) {
         // 次の行を得る
         if (line = read_line_without_crln(); line.has_value()) {
           break;
         }
         delay(10);
       }
       // ':'の位置でleftとrightに分ける
       auto str{line.value_or("")};
       auto pos = str.find(":");
       if (pos == std::string::npos) {
         // 予想と違う行が入ってきたので,今ある値を返す
         return Response{std::time(nullptr), Response::Tag::EPANDESC,
                         std::move(kv)};
       }
       std::string left{str, 0, pos};
       std::string right{str, pos + 1, std::string::npos};
       // 先頭に空白があるからここではfindで確認する
       // キーが一致したらmapに入れる
       if (left.find(key) != std::string::npos) {
         kv.insert(std::make_pair(key, right));
       }
     }
     // key-value数の一致確認
     if (kv.size() != std::size(keys)) {
       ESP_LOGE(MAIN, "Mismatched size : %d, %d", kv.size(),
                std::size(keys));
     }
     return Response{std::time(nullptr), Response::Tag::EPANDESC,
                     std::move(kv)};
   }();
 } else if (opt_tag.value() == "ERXUDP"sv) {
   //
   // ERXUDPを受け取った
   //
   return [this]() -> std::optional<Response> {
     // ERXUDPメッセージの値
     constexpr std::array<std::string_view, 7> keys = {
         // 送信元IPv6アドレス
         "SENDER"sv,
         // 送信先IPv6アドレス
         "DEST"sv,
         // 送信元ポート番号
         "RPORT"sv,
         // 送信元ポート番号
         "LPORT"sv,
         // 送信元のMACアドレス
         "SENDERLLA"sv,
         // SECURED=1 : MACフレームが暗号化されていた
         // SECURED=0 : MACフレームが暗号化されていなかった
         "SECURED"sv,
         // データの長さ
         "DATALEN"sv,
         // ここからデータなんだけどバイナリ形式だから特別扱いする。
     };
     //
     std::map<std::string, std::string> kv;
     for (const auto &key : keys) {
       if (auto opt_token = get_token(); opt_token.has_value()) {
         // 値を得る
         kv.insert(std::make_pair(key, opt_token.value()));
       } else {
         // 値が不足している
         return std::nullopt;
       }
     }
     //
     // データはバイナリで送られてくるので,
     // テキスト形式に変換する。
     //
     std::size_t datalen = std::stol(kv.at("DATALEN"s), nullptr, 16);
     // メモリーを確保して
     std::vector<uint8_t> vect{};
     vect.resize(datalen);
     // データの長さ分読み込む
     commport.readBytes(vect.data(), vect.size());
     // バイナリからテキスト形式に変換する
     std::string textformat = binary_to_text(vect);
     // key-valueストアに入れる
     kv.insert(std::make_pair("DATA"s, textformat));
     return Response{std::time(nullptr), Response::Tag::ERXUDP,
                     std::move(kv)};
   }();
 } else {
   //
   // 何か来たみたい。
   //
   ESP_LOGD(MAIN, "Unknown event: \"%s\"", opt_tag.value().c_str());
 }
 return std::nullopt;
}
// ストリームから空白で区切られたトークンを得る
std::optional<std::string> get_token() {
 std::string result{};
 for (auto count = 0; count < LINE_BUFFER_SIZE; ++count) {
   // 1文字読み込んで
   int ch = commport.read();
   if (ch < 0) {
     // ストリームの最後まで読み込んだので脱出する
     break;
   } else if (std::isspace(ch)) {
     // 空白を見つけたので脱出する
     // 読み込んだ空白文字は捨てる
     break;
   } else {
     // 読み込んだ文字はバッファへ追加する
     result.push_back(static_cast<char>(ch));
   }
 }
 //
 // 空白またはCRまたはLFで始まっていたのでトークンがなかった場合はnullopt
 return (result.length() > 0) ? std::make_optional(result) : std::nullopt;
}
*/

//
//
//
struct Bp35a1Response final {
  // 受信した時間
  std::time_t created_at;
  // 受信したイベントの種類
  enum class Tag { EVENT, EPANDESC, ERXUDP, UNKNOWN } tag;
  // key-valueストア
  std::map<std::string, std::string> keyval;
};

//
std::string to_string(const Bp35a1Response &response) {
  std::ostringstream oss;
  switch (response.tag) {
  case Bp35a1Response::Tag::EVENT:
    oss << "EVENT "sv;
    break;
  case Bp35a1Response::Tag::EPANDESC:
    oss << "EPANDESC "sv;
    break;
  case Bp35a1Response::Tag::ERXUDP:
    oss << "ERXUDP "sv;
    break;
  case Bp35a1Response::Tag::UNKNOWN:
    oss << "UNKNOWN "sv;
    break;
  default:
    oss << "??? "sv;
    break;
  }
  if (response.keyval.empty()) {
    return oss.str();
  }
  //
  auto mapping =
      [](const std::pair<std::string, std::string> &kv) -> std::string {
    std::ostringstream s;
    s << std::quoted(kv.first) << ":"sv << std::quoted(kv.second);
    return s.str();
  };
  std::transform(std::begin(response.keyval), std::end(response.keyval),
                 std::ostream_iterator<std::string>(oss, ","), mapping);
  return oss.str(); // 最終カンマに対してなにもしない
}

// バイナリからテキスト形式に変換する
std::string binary_to_text(const std::vector<uint8_t> &binaries) {
  std::ostringstream oss;
  oss << std::uppercase << std::hex;
  std::for_each(std::begin(binaries), std::end(binaries),
                [&oss](uint8_t octet) -> void {
                  oss << std::setfill('0') << std::setw(2) << +octet;
                });
  return oss.str();
}

// テキスト形式からバイナリに戻す
std::vector<uint8_t> text_to_binary(std::string_view text) {
  std::vector<uint8_t> binary;
  std::string s;
  for (auto itr = text.begin(); itr != text.end();) {
    s.clear();
    // 8ビットは16進数2文字なので2文字毎に変換する
    s.push_back(*itr++);
    if (itr != text.end()) {
      s.push_back(*itr++);
    }
    binary.push_back(std::stoi(s, nullptr, 16));
  }
  return binary;
}

//
//
//
struct SmartMeterIdentifier final {
  IPv6Addr ipv6_address;
  HexedU8 channel;
  HexedU16 pan_id;
  explicit SmartMeterIdentifier() : ipv6_address{}, channel{}, pan_id{} {}
};
//
std::string to_string(const SmartMeterIdentifier &ident) {
  std::ostringstream oss;
  oss << "ipv6_address: \"" << ident.ipv6_address //
      << "\",channel: \""s << ident.channel       //
      << "\",pan_id: \""s << ident.pan_id << "\"";
  return oss.str();
}

//
//
//
class Bp35a1 {
  const std::size_t retry_limits;
  Stream &commport;
  std::pair<std::string_view, std::string_view> b_route_id_password;
  SmartMeterIdentifier smart_meter_ident;

public:
  static constexpr std::size_t LINE_BUFFER_SIZE{512};
  using Response = Bp35a1Response;
  // メッセージを表示する関数型
  typedef void (*DisplayMessageT)(const char *);
  //
  Bp35a1(Stream &stream,
         std::pair<std::string_view, std::string_view> b_id_password,
         size_t limits = 100)
      : retry_limits(limits),
        commport{stream},
        b_route_id_password{b_id_password},
        smart_meter_ident{} {}
  // BP35A1を起動して接続する
  bool boot(DisplayMessageT display_message) {
    // 一旦セッションを切る
    write_with_crln("SKTERM"sv);

    // エコーバック抑制
    write_with_crln("SKSREG SFE 0"sv);
    delay(1000);
    clear_read_buffer();

    const auto [bid, bpass] = b_route_id_password;
    // パスワード設定
    display_message("Set password\n");
    write_with_crln("SKSETPWD C "s + std::string{bpass});
    if (!has_ok()) {
      return false;
    }

    // ID設定
    display_message("Set ID\n");
    write_with_crln("SKSETRBID "s + std::string{bid});
    if (!has_ok()) {
      return false;
    }

    // アクティブスキャン実行
    std::optional<Response> conn_target = do_active_scan(display_message);
    // アクティブスキャン結果を確認
    if (!conn_target.has_value()) {
      // 接続対象のスマートメーターが見つからなかった
      display_message("smart meter not found.");
      ESP_LOGD(MAIN, "smart meter not found.");
      return false;
    }

    // アクティブスキャン結果をもとにしてipv6アドレスを得る
    Response r = conn_target.value();
    auto opt_ipv6_address =
        get_ipv6_address(r.keyval.at("Addr"s), display_message);

    if (!opt_ipv6_address.has_value()) {
      display_message("get ipv6 address fail.");
      ESP_LOGD(MAIN, "get ipv6 address fail.");
      return false;
    }

    // アクティブスキャン結果をインスタンス変数にセットする
    std::istringstream{opt_ipv6_address.value()} >>
        smart_meter_ident.ipv6_address;
    std::istringstream{r.keyval.at("Channel"s)} >> smart_meter_ident.channel;
    std::istringstream{r.keyval.at("Pan ID"s)} >> smart_meter_ident.pan_id;
    /*
    smart_meter_ident.ipv6_address =
        from_string<IPv6Addr>(opt_ipv6_address.value()).value_or(IPv6Addr{});
    smart_meter_ident.channel =
        from_string<HexedU8>(r.keyval.at("Channel"s)).value_or(HexedU8{});
    smart_meter_ident.pan_id =
        from_string<HexedU16>(r.keyval.at("Pan ID"s)).value_or(HexedU16{});
        */

    // 見つかったスマートメーターに接続要求を送る
    if (!connect(display_message)) {
      // 接続失敗
      return false;
    }

    // 接続成功
    ESP_LOGD(MAIN, "connection successful");

    return true;
  }
  // ipv6 アドレスを受け取る関数
  std::optional<std::string> get_ipv6_address(const std::string &addr,
                                              DisplayMessageT message) {
    // 返答を受け取る前にクリアしておく
    clear_read_buffer();
    // ipv6 アドレス要求を送る
    message("Fetch ipv6 address\n");
    write_with_crln("SKLL64 "s + addr);
    // ipv6 アドレスを受け取る
    for (auto retry = 1; retry <= retry_limits; ++retry) {
      // いったん止める
      delay(100);
      //
      if (auto line = read_line_without_crln(); line.has_value()) {
        return line;
      }
    }
    return std::nullopt;
  }
  // アクティブスキャンを実行する
  std::optional<Response> do_active_scan(DisplayMessageT message) {
    // スマートメーターからの返答を待ち受ける関数
    auto got_respond = [this](uint8_t duration) -> std::optional<Response> {
      // スキャン対象のチャンネル番号
      // CHANNEL_MASKがFFFFFFFF つまり 11111111 11111111
      // 11111111 11111111なので
      // 最下位ビットがチャンネル33なので
      //             60,59,58,57,
      // 56,55,54,53,52,51,50,49,
      // 48,47,46,45,44,43,42,41,
      // 40,39,38,37,36,35,34,33
      // チャンネルをスキャンするみたい
      const uint32_t total_ch = std::abs(33 - 60) + 1; // 33ch ～ 60ch
      // 一回のスキャンでかかる時間
      const uint32_t single_ch_scan_millis = 10 * (1 << duration) + 1;
      const uint32_t all_scan_millis = total_ch * single_ch_scan_millis;
      // 結果報告用
      std::optional<Response> target_Whm = std::nullopt;
      for (auto u = 0; u <= all_scan_millis; u += single_ch_scan_millis) {
        // いったん止める
        delay(single_ch_scan_millis);
        // 結果を受け取る
        std::optional<Response> opt_res = watch_response();
        if (!opt_res.has_value()) {
          continue;
        }
        // 何か受け取ったみたい
        Response r = opt_res.value();
        ESP_LOGD(MAIN, "%s", to_string(r).c_str());
        if (r.tag == Response::Tag::EVENT) {
          // イベント番号
          auto num = std::stol(r.keyval.at("NUM"s), nullptr, 16);
          if (num == 0x22) {
            // EVENT 22
            // つまりアクティブスキャンの完了報告を確認しているのでスキャン結果を返す
            return target_Whm;
          }
        } else if (r.tag == Response::Tag::EPANDESC) {
          // 接続対象のスマートメータを発見した
          target_Whm = r;
        }
      }
      // EVENT 22がやってこなかったようだね
      return std::nullopt;
    };
    //
    std::optional<Response> found = std::nullopt;
    message("Active Scan");
    // 接続対象のスマートメータをスキャンする
    for (uint8_t duration : {4, 5, 6, 7, 8}) {
      message(".");
      // スキャン要求を出す
      write_with_crln("SKSCAN 2 FFFFFFFF "s + std::to_string(duration));
      if (!has_ok()) {
        break;
      }
      found = got_respond(duration);
      if (found.has_value()) {
        // 接続対象のスマートメータを発見したら脱出する
        break;
      }
    }
    message("\n");
    return found;
  }
  // 接続(PANA認証)要求を送る
  bool connect(DisplayMessageT message) {
    //
    ESP_LOGD(MAIN, "%s", to_string(smart_meter_ident).c_str());

    // 通信チャネルを設定する
    message("Set Channel\n");
    write_with_crln("SKSREG S2 "s + std::string{smart_meter_ident.channel});
    if (!has_ok()) {
      return false;
    }
    // Pan IDを設定する
    message("Set Pan ID\n");
    write_with_crln("SKSREG S3 "s + std::string{smart_meter_ident.pan_id});
    if (!has_ok()) {
      return false;
    }
    // 返答を受け取る前にクリアしておく
    clear_read_buffer();

    // PANA認証要求
    message("Connecting...\n");
    write_with_crln("SKJOIN "s + std::string{smart_meter_ident.ipv6_address});
    if (!has_ok()) {
      return false;
    }
    // PANA認証要求結果を受け取る
    for (auto retry = 0; retry <= retry_limits; ++retry) {
      // いったん止める
      delay(100);
      //
      std::optional<Response> opt_res = watch_response();
      if (opt_res.has_value()) {
        // 何か受け取った
        Response r = opt_res.value();
        if (r.tag == Response::Tag::EVENT) {
          switch (std::stol(r.keyval.at("NUM"s), nullptr, 16)) {
          case 0x24: {
            // EVENT 24 :
            // PANAによる接続過程でエラーが発生した(接続が完了しなかった)
            ESP_LOGD(MAIN, "Fail to connect");
            message("Fail to connect\n");
            return false;
          }
          case 0x25: {
            // EVENT 25 : PANAによる接続が完了した
            ESP_LOGD(MAIN, "Connected");
            message("Connected\n");
            return true;
          }
          default:
            break;
          }
        }
      }
    }
    //
    return false;
  }
  // 受信メッセージを破棄する
  void clear_read_buffer() {
    while (commport.available() > 0) {
      commport.read();
    }
  }
  // 成功ならtrue, それ以外ならfalse
  bool has_ok() {
    for (auto retry = 0; retry < retry_limits; ++retry) {
      if (auto line = read_line_without_crln(); line.has_value()) {
        auto begin = line->begin();
        // OKで始まるかFAILで始まるか
        if (std::equal(begin, std::next(begin, 2), "OK")) {
          return true;
        } else if (std::equal(begin, std::next(begin, 4), "FAIL")) {
          ESP_LOGD(MAIN, "%s", line->c_str());
          return false;
        }
      } else {
        delay(100);
      }
    }
    return false;
  }
  // 要求を送る
  bool
  send_request(EchonetLiteTransactionId tid,
               std::vector<SmartElectricEnergyMeter::EchonetLiteEPC> epcs) {
    std::vector<uint8_t> frame =
        SmartElectricEnergyMeter::make_echonet_lite_frame(
            tid, EchonetLiteESV::Get, epcs);
    //
    std::ostringstream oss;
    oss << "SKSENDTO "sv;
    oss << "1 "sv;                                  // HANDLE
    oss << smart_meter_ident.ipv6_address << " "sv; // IPADDR
    oss << EchonetLiteUdpPort << " "sv;             // PORT
    oss << "1 "sv;                                  // SEC
    oss << std::uppercase << std::hex;              //
    oss << std::setfill('0') << std::setw(4);       //
    oss << frame.size() << " "sv;                   // DATALEN
    //
    auto line{oss.str()};
    ESP_LOGD(MAIN, "%s", line.c_str());
    // 送信(ここはテキスト)
    commport.write(line.c_str(), line.length());
    // つづけてEchonet Liteフレーム (バイナリ)を送る
    commport.write(frame.data(), frame.size());
    // CRLFは不要
    return has_ok();
  }
  // 受信
  std::optional<Response> watch_response() {
    std::optional<std::string> opt_tag = get_token();
    // 受信したメッセージを解析する
    if (!opt_tag.has_value()) {
      // 何も受け取れなかった場合
      return std::nullopt;
    } else if (opt_tag.value() == "EVENT"sv) {
      //
      // EVENTを受け取った
      //
      return [this]() -> std::optional<Response> {
        // EVENTメッセージの値
        constexpr std::array<std::string_view, 3> keys = {
            "NUM"sv,    // イベント番号
            "SENDER"sv, // イベントのトリガーとなったメッセージの発信元アドレス
            "PARAM"sv,  // イベント固有の引数
        };
        //
        std::map<std::string, std::string> kv{};
        for (const auto &key : keys) {
          if (auto opt_token = get_token(); opt_token.has_value()) {
            // 受け取った値をkey-valueストアへ
            kv.insert(std::make_pair(key, opt_token.value()));
          } else {
            // 値が不足しているが
            // 3番目のパラメータがないことがあるので
            // とくに何もしません
          }
        }
        return Response{std::time(nullptr), Response::Tag::EVENT,
                        std::move(kv)};
      }();
    } else if (opt_tag.value() == "EPANDESC"sv) {
      //
      // EPANDESCを受け取った
      //
      return [this]() -> std::optional<Response> {
        // EPANDESCメッセージの各行ごとの値
        // この順番どおりに読み込む
        constexpr std::array<std::string_view, 6> keys = {
            /**/ "Channel"sv, // 発見したPANの周波数(論理チャンネル番号)
            "Channel Page"sv, // 発見したPANのチャンネルページ
            /***/ "Pan ID"sv, //  発見したPANのPAN ID
            /*****/ "Addr"sv, // アクティブスキャン応答元のアドレス
            /*****/ "LQI"sv, // 受信したビーコンの受信ED値(RSSI)
            /**/ "PairID"sv, // (IEが含まれる場合)相手から受信したPairingID
        };
        //
        std::map<std::string, std::string> kv{};
        for (const auto &key : keys) {
          std::optional<std::string> line{};
          for (auto retry = 1; retry <= retry_limits; ++retry) {
            // 次の行を得る
            if (line = read_line_without_crln(); line.has_value()) {
              break;
            }
            delay(10);
          }
          // ':'の位置でleftとrightに分ける
          auto str{line.value_or("")};
          auto pos = str.find(":");
          if (pos == std::string::npos) {
            // 予想と違う行が入ってきたので,今ある値を返す
            return Response{std::time(nullptr), Response::Tag::EPANDESC,
                            std::move(kv)};
          }
          std::string left{str, 0, pos};
          std::string right{str, pos + 1, std::string::npos};
          // 先頭に空白があるからここではfindで確認する
          // キーが一致したらmapに入れる
          if (left.find(key) != std::string::npos) {
            kv.insert(std::make_pair(key, right));
          }
        }
        // key-value数の一致確認
        if (kv.size() != std::size(keys)) {
          ESP_LOGE(MAIN, "Mismatched size : %d, %d", kv.size(),
                   std::size(keys));
        }
        return Response{std::time(nullptr), Response::Tag::EPANDESC,
                        std::move(kv)};
      }();
    } else if (opt_tag.value() == "ERXUDP"sv) {
      //
      // ERXUDPを受け取った
      //
      return [this]() -> std::optional<Response> {
        // ERXUDPメッセージの値
        constexpr std::array<std::string_view, 7> keys = {
            // 送信元IPv6アドレス
            "SENDER"sv,
            // 送信先IPv6アドレス
            "DEST"sv,
            // 送信元ポート番号
            "RPORT"sv,
            // 送信元ポート番号
            "LPORT"sv,
            // 送信元のMACアドレス
            "SENDERLLA"sv,
            // SECURED=1 : MACフレームが暗号化されていた
            // SECURED=0 : MACフレームが暗号化されていなかった
            "SECURED"sv,
            // データの長さ
            "DATALEN"sv,
            // ここからデータなんだけどバイナリ形式だから特別扱いする。
        };
        //
        std::map<std::string, std::string> kv;
        for (const auto &key : keys) {
          if (auto opt_token = get_token(); opt_token.has_value()) {
            // 値を得る
            kv.insert(std::make_pair(key, opt_token.value()));
          } else {
            // 値が不足している
            return std::nullopt;
          }
        }
        //
        // データはバイナリで送られてくるので,
        // テキスト形式に変換する。
        //
        std::size_t datalen = std::stol(kv.at("DATALEN"s), nullptr, 16);
        // メモリーを確保して
        std::vector<uint8_t> vect{};
        vect.resize(datalen);
        // データの長さ分読み込む
        commport.readBytes(vect.data(), vect.size());
        // バイナリからテキスト形式に変換する
        std::string textformat = binary_to_text(vect);
        // key-valueストアに入れる
        kv.insert(std::make_pair("DATA"s, textformat));
        return Response{std::time(nullptr), Response::Tag::ERXUDP,
                        std::move(kv)};
      }();
    } else {
      //
      // 何か来たみたい。
      //
      ESP_LOGD(MAIN, "Unknown event: \"%s\"", opt_tag.value().c_str());
    }
    return std::nullopt;
  }
  // ストリームから空白で区切られたトークンを得る
  std::optional<std::string> get_token() {
    std::string result{};
    for (auto count = 0; count < LINE_BUFFER_SIZE; ++count) {
      // 1文字読み込んで
      int ch = commport.read();
      if (ch < 0) {
        // ストリームの最後まで読み込んだので脱出する
        break;
      } else if (std::isspace(ch)) {
        // 空白を見つけたので脱出する
        // 読み込んだ空白文字は捨てる
        break;
      } else {
        // 読み込んだ文字はバッファへ追加する
        result.push_back(static_cast<char>(ch));
      }
    }
    //
    // 空白またはCRまたはLFで始まっていたのでトークンがなかった場合はnullopt
    return (result.length() > 0) ? std::make_optional(result) : std::nullopt;
  }
  // ストリームから読み込んでCRLFを捨てた行を得る関数
  std::optional<std::string> read_line_without_crln() {
    std::string buffer(LINE_BUFFER_SIZE, '\0');
    std::size_t length =
        commport.readBytesUntil('\n', buffer.data(), LINE_BUFFER_SIZE);
    if (length >= 1) {
      buffer.resize(length);
      if (buffer.back() == '\r') {
        buffer.resize(buffer.size() - 1);
      }
      if (buffer.empty()) {
        return std::nullopt;
      } else {
        ESP_LOGD(RECEIVE, "%s", buffer.c_str());
        return std::make_optional(buffer);
      }
    }
    return std::nullopt;
  }
  // CRLFを付けてストリームに1行書き込む関数
  void write_with_crln(const std::string &line) {
    ESP_LOGD(SEND, "%s", line.c_str());
    commport.write(line.c_str(), line.length());
    commport.write("\r\n");
    // メッセージ送信完了待ち
    commport.flush();
  }
  inline void write_with_crln(std::string_view line) {
    write_with_crln(std::string{line});
  }
};
