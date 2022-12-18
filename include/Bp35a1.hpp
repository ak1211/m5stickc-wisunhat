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
#include <variant>

namespace Bp35a1 {
using namespace std::literals::string_literals;
using namespace std::literals::string_view_literals;

static constexpr std::size_t RETRY_LIMITS{100};

// メッセージを表示する関数型
typedef void (*DisplayMessageT)(const char *);

// 受信メッセージを破棄する
void clear_read_buffer(Stream &commport) {
  while (commport.available() > 0) {
    commport.read();
  }
}

// ストリームからsepで区切られたトークンを得る
std::pair<std::string, std::string> get_token(Stream &commport, int sep) {
  constexpr std::size_t LINE_BUFFER_SIZE{512};
  std::string separator{};
  std::string token{};
  for (auto count = 0; count < LINE_BUFFER_SIZE; ++count) {
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
  separator.shrink_to_fit();
  token.shrink_to_fit();
  return {token, separator};
}

// CRLFを付けてストリームに1行書き込む関数
void write_with_crln(Stream &commport, const std::string &line) {
  ESP_LOGD(SEND, "%s", line.c_str());
  commport.write(line.c_str(), line.length());
  commport.write("\r\n");
  // メッセージ送信完了待ち
  commport.flush();
}

// 成功ならtrue, それ以外ならfalse
bool has_ok(Stream &commport) {
  for (auto retry = 0; retry < RETRY_LIMITS; ++retry) {
    if (auto [token, sep] = get_token(commport, '\n'); token.length() > 0) {
      auto begin = token.begin();
      // OKで始まるかFAILで始まるか
      if (std::equal(begin, std::next(begin, 2), "OK")) {
        ESP_LOGD(MAIN, "OK");
        return true;
      } else if (std::equal(begin, std::next(begin, 4), "FAIL")) {
        ESP_LOGD(MAIN, "%s", token.c_str());
        return false;
      }
    } else {
      delay(100);
    }
  }
  return false;
}

// 2桁の16進数
struct HexedU8 final {
  uint8_t u8;
  HexedU8(uint8_t in = 0U) { u8 = in; }
  operator std::string() const;
};
std::istream &operator>>(std::istream &is, HexedU8 &v) {
  auto save = is.flags();
  int int_value;
  is >> std::setw(2) >> std::hex >> int_value;
  v = HexedU8{static_cast<uint8_t>(int_value)};
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
  return (iss.fail()) ? std::nullopt : std::make_optional(v);
}
HexedU8::operator std::string() const {
  std::ostringstream oss;
  oss << *this;
  return oss.str();
}

// 4桁の16進数
struct HexedU16 final {
  uint16_t u16;
  HexedU16(uint16_t in = 0U) { u16 = in; }
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
  return (iss.fail()) ? std::nullopt : std::make_optional(v);
}
HexedU16::operator std::string() const {
  std::ostringstream oss;
  oss << *this;
  return oss.str();
}

// 16桁の16進数
struct HexedU64 final {
  uint64_t u64;
  HexedU64(uint64_t in = 0U) { u64 = in; }
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
  return (iss.fail()) ? std::nullopt : std::make_optional(v);
}
HexedU64::operator std::string() const {
  std::ostringstream oss;
  oss << *this;
  return oss.str();
}

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
  is >> v.hex[0] >> colon  //
      >> v.hex[1] >> colon //
      >> v.hex[2] >> colon //
      >> v.hex[3] >> colon //
      >> v.hex[4] >> colon //
      >> v.hex[5] >> colon //
      >> v.hex[6] >> colon //
      >> v.hex[7];         //
  is.flags(save);
  return is;
}
std::ostream &operator<<(std::ostream &os, const IPv6Addr &v) {
  auto save = os.flags();
  auto colon = ":"s;
  os << v.hex[0] << colon //
     << v.hex[1] << colon //
     << v.hex[2] << colon //
     << v.hex[3] << colon //
     << v.hex[4] << colon //
     << v.hex[5] << colon //
     << v.hex[6] << colon //
     << v.hex[7];         //
  os.flags(save);
  return os;
}
std::optional<IPv6Addr> makeIPv6Addr(const std::string &in) {
  IPv6Addr v;
  std::istringstream iss{in};
  iss >> v;
  return (iss.fail()) ? std::nullopt : std::make_optional(v);
}
IPv6Addr::operator std::string() const {
  std::ostringstream oss;
  oss << *this;
  return oss.str();
}

//
// EVENTメッセージの値
//
struct ResEvent final {
  std::string to_string() const {
    std::ostringstream oss;
    oss << "num:" << num        //
        << ",sender:" << sender //
        << ",param:";
    if (param.has_value()) {
      oss << param.value();
    } else {
      oss << "\"N/A\"";
    }
    return oss.str();
  }
  HexedU8 num;     // イベント番号
  IPv6Addr sender; // イベントのトリガーとなったメッセージの発信元アドレス
  std::optional<HexedU8> param; // イベント固有の引数
};

//
// EPANDESCメッセージの値
//
struct ResEpandesc final {
  std::string to_string() const {
    std::ostringstream oss;
    oss << "channel:" << channel            //
        << ",channel_page:" << channel_page //
        << ",pan_id:" << pan_id             //
        << ",addr:" << addr                 //
        << ",lqi:" << lqi                   //
        << ",pairid:" << std::quoted(pairid);
    return oss.str();
  }
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
struct ResErxudp final {
  std::string to_string() const {
    std::ostringstream oss;
    oss << "sender:"sv << sender                   //
        << ",dest:"sv << dest                      //
        << ",rport:" << rport                      //
        << ",lport:" << lport                      //
        << ",senderlla:" << std::quoted(senderlla) //
        << ",secured:" << secured                  //
        << ",datalen:" << datalen                  //
        << ",data:\"";
    std::copy(data.begin(), data.end(), std::ostream_iterator<HexedU8>(oss));
    oss << "\"";
    return oss.str();
  }
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

//
// BP35A1から受け取ったイベント
//
using Response = std::variant<ResEvent, ResEpandesc, ResErxudp>;

//
// ipv6 アドレスを受け取る関数
//
std::optional<IPv6Addr> get_ipv6_address(Stream &commport,
                                         const std::string &addr) {
  // 返答を受け取る前にクリアしておく
  clear_read_buffer(commport);
  // ipv6 アドレス要求を送る
  write_with_crln(commport, "SKLL64 "s + addr);
  // ipv6 アドレスを受け取る
  for (auto retry = 1; retry <= RETRY_LIMITS; ++retry) {
    // いったん止める
    delay(100);
    //
    auto [token, sep] = get_token(commport, '\n');
    ESP_LOGD(MAIN, "token:\"%s\",sep\"%s\"", token.c_str(), sep.c_str());
    if (auto result = makeIPv6Addr(token); result.has_value()) {
      return result;
    }
  }
  return std::nullopt;
}
//
// 受信
//
std::optional<Response> receive_response(Stream &commport) {
  // EVENTを受信する
  auto rx_event = [&commport](const std::string &) -> ResEvent {
    ResEvent ev;
    std::vector<std::string> tokens;
    constexpr std::size_t N{3};
    tokens.reserve(N);
    for (auto i = 0; i < N; ++i) {
      auto [x, sep] = get_token(commport, ' ');
      tokens.push_back(x);
      if (sep.compare("\r\n") == 0) {
        break;
      }
    }
    if (tokens.size() == 2) {
      ev.num = makeHexedU8(tokens[0]).value_or(HexedU8{});
      ev.sender = makeIPv6Addr(tokens[1]).value_or(IPv6Addr{});
      // 3番目のパラメータがない場合がある
    } else if (tokens.size() == 3) {
      ev.num = makeHexedU8(tokens[0]).value_or(HexedU8{});
      ev.sender = makeIPv6Addr(tokens[1]).value_or(IPv6Addr{});
      ev.param = makeHexedU8(tokens[2]);
    }
    return ev;
  };
  // EPANDESCを受信する
  auto rx_epandesc = [&commport](const std::string &) -> ResEpandesc {
    ResEpandesc ev;
    std::vector<std::pair<std::string, std::string>> tokens;
    constexpr std::size_t N{6};
    tokens.reserve(N);
    for (auto i = 0; i < N; ++i) {
      auto [left, sep1] = get_token(commport, ':');
      auto [right, sep2] = get_token(commport, ' ');
      tokens.push_back({left, right});
    }
    for (const auto [left, right] : tokens) {
      // 先頭空白をスキップする
      std::string::const_iterator it;
      for (it = left.begin(); it != left.end(); ++it) {
        if (*it != ' ') {
          break;
        }
      }
      if (std::equal(it, left.end(), "Channel")) {
        ev.channel = makeHexedU8(right).value_or(HexedU8{});
      } else if (std::equal(it, left.end(), "Channel Page")) {
        ev.channel_page = makeHexedU8(right).value_or(HexedU8{});
      } else if (std::equal(it, left.end(), "Pan ID")) {
        ev.pan_id = makeHexedU16(right).value_or(HexedU16{});
      } else if (std::equal(it, left.end(), "Addr")) {
        ev.addr = makeHexedU64(right).value_or(HexedU64{});
      } else if (std::equal(it, left.end(), "LQI")) {
        ev.lqi = makeHexedU8(right).value_or(HexedU8{});
      } else if (std::equal(it, left.end(), "PairID")) {
        ev.pairid = right;
      } else {
        ESP_LOGE(MAIN, "Unexpected input. \"%s\":\"%s\"", left, right);
      }
    }
    return ev;
  };
  // ERXUDPを受信する
  auto rx_erxudp = [&commport](const std::string &) -> ResErxudp {
    ResErxudp ev;
    std::vector<std::string> tokens;
    constexpr std::size_t N{7};
    tokens.reserve(N);
    for (auto i = 0; i < N; ++i) {
      auto [x, sep] = get_token(commport, ' ');
      tokens.push_back(x);
      if (sep.compare("\r\n") == 0) {
        break;
      }
    }
    if (tokens.size() >= 7) {
      ev.sender = makeIPv6Addr(tokens[0]).value_or(IPv6Addr{});
      ev.dest = makeIPv6Addr(tokens[1]).value_or(IPv6Addr{});
      ev.rport = makeHexedU16(tokens[2]).value_or(HexedU16{});
      ev.lport = makeHexedU16(tokens[3]).value_or(HexedU16{});
      ev.senderlla = tokens[4];
      ev.secured = makeHexedU8(tokens[5]).value_or(HexedU8{});
      ev.datalen = makeHexedU16(tokens[6]).value_or(HexedU16{});
      //
      // データ(ペイロード)を読みこむ
      //
      // メモリーを確保して
      ev.data.resize(ev.datalen.u16);
      // データの長さ分読み込む
      commport.readBytes(ev.data.data(), ev.data.size());
      // 残ったCRLFを読み捨てる
      get_token(commport, ' ');
    }
    return ev;
  };
  // よくわからないイベントを受信する
  auto rx_fallthrough =
      [&commport](const std::string &token) -> std::optional<Response> {
    ESP_LOGD(MAIN, "Unknown event: \"%s\"", token.c_str());
    return std::nullopt;
  };
  //
  //
  //
  auto [token, sep] = get_token(commport, ' ');
  return (token.length() == 0)    ? std::nullopt
         : (token == "EVENT"s)    ? rx_event(token)
         : (token == "EPANDESC"s) ? rx_epandesc(token)
         : (token == "ERXUDP"s)   ? rx_erxudp(token)
                                  : rx_fallthrough(token);
}

//
// スマートメーターの識別子
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

// 要求を送る
void send_request(Stream &commport,
                  const SmartMeterIdentifier &smart_meter_ident,
                  EchonetLiteTransactionId tid,
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
}

// 接続(PANA認証)要求を送る
bool connect(Stream &commport, const SmartMeterIdentifier &smart_meter_ident,
             DisplayMessageT message) {
  //
  ESP_LOGD(MAIN, "%s", to_string(smart_meter_ident).c_str());

  // 通信チャネルを設定する
  message("Set Channel\n");
  write_with_crln(commport,
                  "SKSREG S2 "s + std::string{smart_meter_ident.channel});
  if (!has_ok(commport)) {
    return false;
  }
  // Pan IDを設定する
  message("Set Pan ID\n");
  write_with_crln(commport,
                  "SKSREG S3 "s + std::string{smart_meter_ident.pan_id});
  if (!has_ok(commport)) {
    return false;
  }
  // 返答を受け取る前にクリアしておく
  clear_read_buffer(commport);

  // PANA認証要求
  message("Connecting...\n");
  write_with_crln(commport,
                  "SKJOIN "s + std::string{smart_meter_ident.ipv6_address});
  if (!has_ok(commport)) {
    return false;
  }
  // PANA認証要求結果を受け取る
  for (auto retry = 0; retry <= RETRY_LIMITS; ++retry) {
    // いったん止める
    delay(100);
    //
    if (auto opt_res = receive_response(commport); opt_res.has_value()) {
      // 何か受け取ったみたい
      Response &resp = opt_res.value();
      std::visit(
          [](const auto &x) { ESP_LOGD(MAIN, "%s", x.to_string().c_str()); },
          resp);
      if (std::holds_alternative<ResEvent>(resp)) {
        ResEvent &event = std::get<ResEvent>(resp);
        // イベント番号
        switch (event.num.u8) {
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

// アクティブスキャンを実行する
std::optional<ResEpandesc> do_active_scan(Stream &commport,
                                          DisplayMessageT message) {
  // スマートメーターからの返答を待ち受ける関数
  auto got_respond =
      [&commport](uint8_t duration) -> std::optional<ResEpandesc> {
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
    std::optional<ResEpandesc> target_Whm = std::nullopt;
    for (auto u = 0; u <= all_scan_millis; u += single_ch_scan_millis) {
      // いったん止める
      delay(single_ch_scan_millis);
      // 結果を受け取る
      if (auto opt_res = receive_response(commport); opt_res.has_value()) {
        // 何か受け取ったみたい
        Response &resp = opt_res.value();
        std::visit(
            [](const auto &x) { ESP_LOGD(MAIN, "%s", x.to_string().c_str()); },
            resp);
        if (std::holds_alternative<ResEvent>(resp)) {
          ResEvent &event = std::get<ResEvent>(resp);
          // イベント番号
          if (event.num.u8 == 0x22) {
            // EVENT 22
            // つまりアクティブスキャンの完了報告を確認しているのでスキャン結果を返す
            return target_Whm;
          }
        } else if (std::holds_alternative<ResEpandesc>(resp)) {
          // 接続対象のスマートメータを発見した
          target_Whm = std::get<ResEpandesc>(resp);
        }
      }
    }
    // EVENT 22がやってこなかったようだね
    return std::nullopt;
  };
  //
  std::optional<ResEpandesc> found{std::nullopt};
  message("Active Scan");
  // 接続対象のスマートメータをスキャンする
  for (uint8_t duration : {4, 5, 6, 7, 8}) {
    message(".");
    // スキャン要求を出す
    write_with_crln(commport, "SKSCAN 2 FFFFFFFF "s + std::to_string(duration));
    if (!has_ok(commport)) {
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

// BP35A1を起動して接続する
std::optional<SmartMeterIdentifier> startup_and_find_meter(
    Stream &commport,
    std::pair<std::string_view, std::string_view> b_route_id_password,
    DisplayMessageT display_message) {
  // 一旦セッションを切る
  write_with_crln(commport, "SKTERM"s);

  delay(1000);
  clear_read_buffer(commport);

  // エコーバック抑制
  write_with_crln(commport, "SKSREG SFE 0"s);
  if (!has_ok(commport)) {
    return std::nullopt;
  }

  const auto [bid, bpass] = b_route_id_password;
  // パスワード設定
  display_message("Set password\n");
  write_with_crln(commport, "SKSETPWD C "s + std::string{bpass});
  if (!has_ok(commport)) {
    return std::nullopt;
  }

  // ID設定
  display_message("Set ID\n");
  write_with_crln(commport, "SKSETRBID "s + std::string{bid});
  if (!has_ok(commport)) {
    return std::nullopt;
  }

  // アクティブスキャン実行
  std::optional<ResEpandesc> conn_target =
      do_active_scan(commport, display_message);
  // アクティブスキャン結果を確認
  if (!conn_target.has_value()) {
    // 接続対象のスマートメーターが見つからなかった
    display_message("smart meter not found.");
    ESP_LOGD(MAIN, "smart meter not found.");
    return std::nullopt;
  }

  // アクティブスキャン結果をもとにしてipv6アドレスを得る
  display_message("Fetch ipv6 address\n");
  if (auto opt_ipv6_address =
          get_ipv6_address(commport, conn_target.value().addr);
      opt_ipv6_address.has_value()) {
    IPv6Addr &addr = opt_ipv6_address.value();
    // アクティブスキャン結果
    SmartMeterIdentifier ident;
    ident.ipv6_address = makeIPv6Addr(addr).value_or(IPv6Addr{});
    ident.channel = conn_target.value().channel;
    ident.pan_id = conn_target.value().pan_id;
    return std::make_optional(ident);
  } else {
    display_message("get ipv6 address fail.");
    ESP_LOGD(MAIN, "get ipv6 address fail.");
    return std::nullopt;
  }
}
} // namespace Bp35a1