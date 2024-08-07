// Copyright (c) 2022 Akihiro Yamamoto.
// Licensed under the MIT License <https://spdx.org/licenses/MIT.html>
// See LICENSE file in the project root for full license information.
//
#include "Bp35a1.hpp"
#include <future>
#include <numeric>

using namespace std::chrono;
using namespace std::chrono_literals;
using namespace std::string_literals;

// 受信メッセージを破棄する
void Bp35a1Class::clear_read_buffer() {
  while (_comm_port.available() > 0) {
    _comm_port.read();
  }
}

// ストリームからsepで区切られたトークンを得る
std::pair<std::string, std::string> Bp35a1Class::get_token(int sep) {
  constexpr std::size_t LINE_BUFFER_SIZE{512};
  std::string separator;
  std::string token;
  for (auto count = 0; count < LINE_BUFFER_SIZE; ++count) {
    // 1文字読み込んで
    auto ch = _comm_port.read();
    if (ch < 0) {
      // ストリームの最後まで読み込んだので脱出する
      break;
    } else if (ch == sep) {
      // sepを見つけたので脱出する
      separator.push_back(ch);
      break;
    } else if (ch == '\r') { // CarriageReturn
      separator.push_back(ch);
      if (_comm_port.peek() == '\n') { // CR + LF
        auto next = _comm_port.read();
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
  return {token, separator};
}

// CRLFを付けてストリームに1行書き込む関数
void Bp35a1Class::write_with_crln(const std::string &line) {
  M5_LOGD("%s", line.c_str());
  _comm_port.write(line.c_str(), line.length());
  _comm_port.write("\r\n");
  // メッセージ送信完了待ち
  _comm_port.flush();
}

// 成功ならtrue, それ以外ならfalse
bool Bp35a1Class::has_ok(std::chrono::seconds timeout) {
  const auto timeover = steady_clock::now() + timeout;
  do {
    if (auto [token, sep] = get_token('\n'); token.length() > 0) {
      M5_LOGV("\"%s\"", token.c_str());
      // OKで始まるかFAILで始まるか
      if (token.find("OK") == 0) {
        return true;
      } else if (token.find("FAIL") == 0) {
        return false;
      }
    } else {
      std::this_thread::sleep_for(100ms);
    }
  } while (steady_clock::now() < timeover);

  return false;
}

//
// ipv6 アドレスを受け取る関数
//
std::optional<Bp35a1::IPv6Addr>
Bp35a1Class::get_ipv6_address(const std::string &addr,
                              std::chrono::seconds timeout) {
  const auto timeover = steady_clock::now() + timeout;
  // 返答を受け取る前にクリアしておく
  clear_read_buffer();
  // ipv6 アドレス要求を送る
  write_with_crln("SKLL64 "s + addr);
  // ipv6 アドレスを受け取る
  std::string token{};
  token.reserve(100);
  do {
    auto [x, sep] = get_token('\n');
    token += x;
    if (sep.compare("\r\n") == 0) {
      break;
    }
  } while (steady_clock::now() < timeover);

  return Bp35a1::makeIPv6Addr(token);
}

//
// 受信
//
std::optional<Bp35a1Class::Response> Bp35a1Class::receive_response() {
  // EVENTを受信する
  auto rx_event =
      [&](const std::string &name) -> std::optional<Bp35a1::ResEvent> {
    std::vector<std::string> tokens;
    constexpr std::size_t N{3};
    // 必要なトークン数読み込む
    tokens.reserve(N);
    for (auto i = 0; i < N; ++i) {
      auto [x, sep] = get_token(' ');
      tokens.push_back(x);
      if (sep.compare("\r\n") == 0) {
        break;
      }
    }
    {
      std::string str = std::accumulate(
          tokens.begin(), tokens.end(), name,
          [](auto acc, auto x) -> std::string { return acc + " " + x; });
      M5_LOGD("%s", str.c_str());
    }
    if (tokens.size() == 2) { // 3番目のパラメータがない場合がある
      Bp35a1::ResEvent ev;
      ev.num = makeHexedU8(tokens[0]).value_or(HexedU8{});
      ev.sender = Bp35a1::makeIPv6Addr(tokens[1]).value_or(Bp35a1::IPv6Addr{});
      ev.param = std::nullopt;
      return std::make_optional(ev);
    } else if (tokens.size() == 3) {
      Bp35a1::ResEvent ev;
      ev.num = makeHexedU8(tokens[0]).value_or(HexedU8{});
      ev.sender = Bp35a1::makeIPv6Addr(tokens[1]).value_or(Bp35a1::IPv6Addr{});
      ev.param = makeHexedU8(tokens[2]);
      return std::make_optional(ev);
    }
    M5_LOGE("rx_event: Unexpected end of input.");
    return std::nullopt;
  };
  // EPANDESCを受信する
  auto rx_epandesc =
      [this](const std::string &name) -> std::optional<Bp35a1::ResEpandesc> {
    std::vector<std::pair<std::string, std::string>> tokens;
    constexpr std::size_t N{6};
    // 必要な行数読み込む
    tokens.reserve(N);
    for (auto i = 0; i < N; ++i) {
      auto [left, sep1] = get_token(':');
      auto [right, sep2] = get_token(' ');
      tokens.push_back({left, right});
    }
    {
      std::string str =
          std::accumulate(tokens.cbegin(), tokens.cend(), name,
                          [](auto acc, auto LandR) -> std::string {
                            auto [l, r] = LandR;
                            return acc + " [" + l + ":" + r + "],";
                          });
      M5_LOGD("%s", str.c_str());
    }
    Bp35a1::ResEpandesc ev;
    auto counter = 0;
    for (const auto [left, right] : tokens) {
      // 先頭空白をスキップする
      std::string::const_iterator it = std::find_if(
          left.cbegin(), left.cend(), [](auto c) { return c != ' '; });
      //
      if (std::equal(it, left.cend(), "Channel")) {
        ev.channel = makeHexedU8(right).value_or(HexedU8{});
        counter = counter + 1;
      } else if (std::equal(it, left.cend(), "Channel Page")) {
        ev.channel_page = makeHexedU8(right).value_or(HexedU8{});
        counter = counter + 1;
      } else if (std::equal(it, left.cend(), "Pan ID")) {
        ev.pan_id = makeHexedU16(right).value_or(HexedU16{});
        counter = counter + 1;
      } else if (std::equal(it, left.cend(), "Addr")) {
        ev.addr = makeHexedU64(right).value_or(HexedU64{});
        counter = counter + 1;
      } else if (std::equal(it, left.cend(), "LQI")) {
        ev.lqi = makeHexedU8(right).value_or(HexedU8{});
        counter = counter + 1;
      } else if (std::equal(it, left.cend(), "PairID")) {
        ev.pairid = right;
        counter = counter + 1;
      } else {
        M5_LOGE("rx_epandesc: Unexpected input. \"%s\":\"%s\"", //
                left, right);
      }
    }
    if (counter == N) {
      return std::make_optional(ev);
    } else {
      M5_LOGE("rx_epandesc: Unexpected end of input.");
      return std::nullopt;
    }
  };
  // ERXUDPを受信する
  auto rx_erxudp =
      [this](const std::string &name) -> std::optional<Bp35a1::ResErxudp> {
    std::vector<std::string> tokens;
    constexpr std::size_t N{7};
    // 必要なトークン数読み込む
    tokens.reserve(N);
    for (auto i = 0; i < N; ++i) {
      auto [x, sep] = get_token(' ');
      tokens.push_back(x);
      if (sep.compare("\r\n") == 0) {
        break;
      }
    }
    {
      std::string str = std::accumulate(
          tokens.begin(), tokens.end(), name,
          [](auto acc, auto x) -> std::string { return acc + " " + x; });
      M5_LOGD("%s", str.c_str());
    }
    if (tokens.size() >= 7) {
      Bp35a1::ResErxudp ev;
      ev.sender = Bp35a1::makeIPv6Addr(tokens[0]).value_or(Bp35a1::IPv6Addr{});
      ev.dest = Bp35a1::makeIPv6Addr(tokens[1]).value_or(Bp35a1::IPv6Addr{});
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
      _comm_port.readBytes(ev.data.data(), ev.data.size());
      // 残ったCRLFを読み捨てる
      auto [x, _sep] = get_token('\r');
      if (x.length() > 0) {
        // まだ何か残っている場合
        // ERXUDPイベントで指定されたデータの長さになるように前を削る(おそらく空白が入り込んでいる)
        ev.data.erase(ev.data.begin(), ev.data.begin() + x.length());
        // 後ろに追加する
        std::copy(x.begin(), x.end(), std::back_inserter(ev.data));
      }
      //
      return std::make_optional(ev);
    } else {
      M5_LOGE("rx_erxudp: Unexpected end of input.");
      return std::nullopt;
    }
  };
  // よくわからないイベントを受信する
  auto rx_fallthrough =
      [this](const std::string &token) -> std::optional<Response> {
    M5_LOGE("Unknown event: \"%s\"", token.c_str());
    return std::nullopt;
  };
  //
  //
  //
  auto [token, sep] = get_token(' ');
  return (token.length() == 0)    ? std::nullopt
         : (token == "EVENT"s)    ? rx_event(token)
         : (token == "EPANDESC"s) ? rx_epandesc(token)
         : (token == "ERXUDP"s)   ? rx_erxudp(token)
                                  : rx_fallthrough(token);
}

// 要求を送る
bool Bp35a1Class::send_request(
    const Bp35a1::SmartMeterIdentifier &smart_meter_ident,
    EchonetLiteTransactionId tid,
    const std::vector<ElectricityMeter::EchonetLiteEPC> epcs) {
  EchonetLiteFrame frame;
  // EHD: ECHONET Lite 電文ヘッダー
  frame.ehd = EchonetLiteEHD;
  // TID: トランザクションID
  frame.tid = tid;
  // SEOJ: メッセージの送り元(sender : 自分自身)
  frame.edata.seoj = EchonetLiteSEOJ(HomeController::EchonetLiteEOJ);
  // DEOJ: メッセージの行き先(destination : スマートメーター)
  frame.edata.deoj = EchonetLiteDEOJ(ElectricityMeter::EchonetLiteEOJ);
  // ESV : ECHONET Lite サービスコード
  frame.edata.esv = EchonetLiteESV::Get;
  // OPC: 処理プロパティ数
  frame.edata.opc = epcs.size();
  // ECHONET Liteプロパティ
  std::transform(
      epcs.cbegin(), epcs.cend(), std::back_inserter(frame.edata.props),
      [](const ElectricityMeter::EchonetLiteEPC v) -> EchonetLiteProp {
        EchonetLiteProp result;
        // EPC: ECHONET Liteプロパティ
        result.epc = static_cast<uint8_t>(v);
        // EDT: EDTはない
        result.edt = {};
        // PDC: EDTのバイト数
        result.pdc = result.edt.size();
        return result;
      });
  // ECHONET Lite フレームからペイロードを作る
  std::vector<uint8_t> payload;
  auto result = EchonetLite::serializeFromEchonetLiteFrame(payload, frame);
  if (auto *perror = std::get_if<EchonetLite::SerializeError>(&result)) {
    // エラー
    M5_LOGE("%s", perror->reason.c_str());
    return false;
  }
  //
  std::ostringstream oss;
  oss << "SKSENDTO"                            //
      << " " << 1                              // HANDLE
      << " " << smart_meter_ident.ipv6_address // IPADDR
      << " " << EchonetLiteUdpPort             // PORT
      << " " << 1                              // SEC
      << " " << HexedU16(payload.size())       // DATALEN
      << " ";
  //
  auto line{oss.str()};
  // 送信(ここはテキスト)
  _comm_port.write(line.c_str(), line.length());
  // つづけてEchonet Liteフレーム (バイナリ)を送る
  // CRLFは不要
  _comm_port.write(payload.data(), payload.size());
  // デバッグ用
  std::copy(payload.begin(), payload.end(),
            std::ostream_iterator<HexedU8>(oss));
  M5_LOGD("%s", oss.str().c_str());
  //
  return has_ok(RETRY_TIMEOUT);
}

// SKTERM要求を送る
bool Bp35a1Class::terminate(std::chrono::seconds timeout) {
  write_with_crln("SKTERM"s);
  if (!has_ok(timeout)) {
    return false;
  }
  return true;
}

// 接続(PANA認証)要求を送る
bool Bp35a1Class::connect(std::ostream &os,
                          Bp35a1::SmartMeterIdentifier smart_meter_ident,
                          std::chrono::seconds timeout) {
  //
  M5_LOGD("%s", to_string(smart_meter_ident).c_str());

  // 通信チャネルを設定する
  os << "Set Channel" << std::endl;
  write_with_crln("SKSREG S2 "s + std::string{smart_meter_ident.channel});
  if (!has_ok(timeout)) {
    return false;
  }
  // Pan IDを設定する
  os << "Set Pan ID" << std::endl;
  write_with_crln("SKSREG S3 "s + std::string{smart_meter_ident.pan_id});
  if (!has_ok(timeout)) {
    return false;
  }
  // 返答を受け取る前にクリアしておく
  clear_read_buffer();

  // PANA認証要求
  os << "Connecting..." << std::endl;
  write_with_crln("SKJOIN "s + std::string{smart_meter_ident.ipv6_address});
  if (!has_ok(timeout)) {
    return false;
  }
  // PANA認証要求結果を受け取る
  const auto timeover = steady_clock::now() + timeout;
  do {
    // いったん止める
    std::this_thread::sleep_for(100ms);
    //
    if (auto opt_res = receive_response(); opt_res) {
      // 何か受け取ったみたい
      const Response &resp = opt_res.value();
      std::visit([](const auto &x) { M5_LOGD("%s", to_string(x).c_str()); },
                 resp);
      if (const Bp35a1::ResEvent *pevent = std::get_if<Bp35a1::ResEvent>(&resp);
          pevent) {
        // イベント番号
        switch (pevent->num.u8) {
        case 0x24: {
          // EVENT 24 :
          // PANAによる接続過程でエラーが発生した(接続が完了しなかった)
          std::ostringstream ss;
          ss << "Fail to connect";
          os << ss.str() << std::endl;
          M5_LOGE("%s", ss.str().c_str());
          return false;
        }
        case 0x25: {
          // EVENT 25 : PANAによる接続が完了した
          std::ostringstream ss;
          ss << "Connected";
          os << ss.str() << std::endl;
          M5_LOGD("%s", ss.str().c_str());
          return true;
        }
        default:
          break;
        }
      }
    }
  } while (steady_clock::now() < timeover);
  //
  return false;
}

// アクティブスキャンを実行する
std::optional<Bp35a1::ResEpandesc>
Bp35a1Class::do_active_scan(std::ostream &os, std::chrono::seconds timeout) {
  // スマートメーターからの返答を待ち受ける関数
  auto got_respond =
      [this](uint8_t duration) -> std::optional<Bp35a1::ResEpandesc> {
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
    std::optional<Bp35a1::ResEpandesc> target_Whm = std::nullopt;
    for (auto u = 0; u <= all_scan_millis; u += single_ch_scan_millis) {
      // いったん止める
      std::this_thread::sleep_for(
          std::chrono::milliseconds{single_ch_scan_millis});
      // 結果を受け取る
      if (auto opt_res = receive_response()) {
        // 何か受け取ったみたい
        const Response &resp = opt_res.value();
        std::visit([](const auto &x) { M5_LOGD("%s", to_string(x).c_str()); },
                   resp);
        if (const auto *eventp = std::get_if<Bp35a1::ResEvent>(&resp)) {
          // イベント番号
          if (eventp->num.u8 == 0x22) {
            // EVENT 22
            // つまりアクティブスキャンの完了報告を確認しているのでスキャン結果を返す
            return target_Whm;
          }
        } else if (const auto *epandescp =
                       std::get_if<Bp35a1::ResEpandesc>(&resp)) {
          // 接続対象のスマートメータを発見した
          target_Whm = *epandescp;
        }
      }
    }
    // EVENT 22がやってこなかったようだね
    return std::nullopt;
  };
  //
  std::optional<Bp35a1::ResEpandesc> found{std::nullopt};
  os << "Active Scan" << std::endl;
  // 接続対象のスマートメータをスキャンする
  for (uint8_t duration : {5, 6, 7, 8}) {
    os << "Now on scanning..." << std::endl;
    // スキャン要求を出す
    write_with_crln("SKSCAN 2 FFFFFFFF "s + std::to_string(duration));
    if (!has_ok(timeout)) {
      break;
    }
    found = got_respond(duration);
    if (found.has_value()) {
      // 接続対象のスマートメータを発見したら脱出する
      break;
    }
  }
  os << "Active Scan Completed." << std::endl;
  return found;
}

// BP35A1を起動してアクティブスキャンを開始する
std::optional<Bp35a1::SmartMeterIdentifier> Bp35a1Class::startup_and_find_meter(
    std::ostream &os, const std::string &route_b_id,
    const std::string &route_b_password, std::chrono::seconds timeout) {
  // 一旦セッションを切る
  write_with_crln("SKTERM"s);

  std::this_thread::sleep_for(1s);
  clear_read_buffer();

  // エコーバック抑制
  write_with_crln("SKSREG SFE 0"s);
  if (!has_ok(timeout)) {
    return std::nullopt;
  }

  // パスワード設定
  os << "Set password" << std::endl;
  write_with_crln("SKSETPWD C "s + route_b_password);
  if (!has_ok(timeout)) {
    return std::nullopt;
  }

  // ID設定
  os << "Set ID" << std::endl;
  write_with_crln("SKSETRBID "s + route_b_id);
  if (!has_ok(timeout)) {
    return std::nullopt;
  }

  // アクティブスキャン実行
  std::optional<Bp35a1::ResEpandesc> conn_target = do_active_scan(os, timeout);
  // アクティブスキャン結果を確認
  if (!conn_target.has_value()) {
    // 接続対象のスマートメーターが見つからなかった
    std::ostringstream ss;
    ss << "smart meter not found.";
    os << ss.str() << std::endl;
    M5_LOGD("%s", ss.str().c_str());
    return std::nullopt;
  }

  // アクティブスキャン結果をもとにしてipv6アドレスを得る
  os << "get ipv6 address" << std::endl;
  auto str_addr = std::string(conn_target.value().addr);
  std::optional<Bp35a1::IPv6Addr> resp_ipv6_address =
      get_ipv6_address(str_addr, timeout);

  if (!resp_ipv6_address.has_value()) {
    std::ostringstream ss;
    ss << "get ipv6 address fail.";
    os << ss.str() << std::endl;
    M5_LOGD("%s", ss.str().c_str());
    return std::nullopt;
  }
  // アクティブスキャン結果
  auto addr = resp_ipv6_address.value();
  return Bp35a1::SmartMeterIdentifier{
      .ipv6_address = Bp35a1::makeIPv6Addr(addr).value_or(Bp35a1::IPv6Addr{}),
      .channel = conn_target.value().channel,
      .pan_id = conn_target.value().pan_id,
  };
}