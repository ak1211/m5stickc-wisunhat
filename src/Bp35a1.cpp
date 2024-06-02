// Copyright (c) 2022 Akihiro Yamamoto.
// Licensed under the MIT License <https://spdx.org/licenses/MIT.html>
// See LICENSE file in the project root for full license information.
//
#include "Bp35a1.hpp"

using namespace std::chrono;
using namespace std::chrono_literals;

// 受信メッセージを破棄する
void Bp35a1::clear_read_buffer(Stream &commport) {
  while (commport.available() > 0) {
    commport.read();
  }
}

// ストリームからsepで区切られたトークンを得る
std::pair<std::string, std::string> Bp35a1::get_token(Stream &commport,
                                                      int sep) {
  constexpr std::size_t LINE_BUFFER_SIZE{512};
  std::string separator;
  std::string token;
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
  return {token, separator};
}

// CRLFを付けてストリームに1行書き込む関数
void Bp35a1::write_with_crln(Stream &commport, const std::string &line) {
  M5_LOGD("%s", line.c_str());
  commport.write(line.c_str(), line.length());
  commport.write("\r\n");
  // メッセージ送信完了待ち
  commport.flush();
}

// 成功ならtrue, それ以外ならfalse
bool Bp35a1::has_ok(Stream &commport, std::chrono::seconds timeout) {
  const time_point tp = system_clock::now() + timeout;
  do {
    if (auto [token, sep] = get_token(commport, '\n'); token.length() > 0) {
      M5_LOGV("\"%s\"", token.c_str());
      // OKで始まるかFAILで始まるか
      if (token.find("OK") == 0) {
        return true;
      } else if (token.find("FAIL") == 0) {
        return false;
      }
    } else {
      delay(100);
    }
  } while (system_clock::now() < tp);

  return false;
}

//
// ipv6 アドレスを受け取る関数
//
std::optional<Bp35a1::IPv6Addr>
Bp35a1::get_ipv6_address(Stream &commport, std::chrono::seconds timeout,
                         const std::string &addr) {
  const time_point tp = system_clock::now() + timeout;
  // 返答を受け取る前にクリアしておく
  clear_read_buffer(commport);
  // ipv6 アドレス要求を送る
  write_with_crln(commport, "SKLL64 "s + addr);
  // ipv6 アドレスを受け取る
  std::string token{};
  token.reserve(100);
  do {
    auto [x, sep] = get_token(commport, '\n');
    token += x;
    if (sep.compare("\r\n") == 0) {
      break;
    }
  } while (system_clock::now() < tp);

  return makeIPv6Addr(token);
}

//
// 受信
//
std::optional<Bp35a1::Response> Bp35a1::receive_response(Stream &commport) {
  // EVENTを受信する
  auto rx_event = [&](const std::string &name) -> std::optional<ResEvent> {
    std::vector<std::string> tokens;
    constexpr std::size_t N{3};
    // 必要なトークン数読み込む
    tokens.reserve(N);
    for (auto i = 0; i < N; ++i) {
      auto [x, sep] = get_token(commport, ' ');
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
      ResEvent ev;
      ev.num = makeHexedU8(tokens[0]).value_or(HexedU8{});
      ev.sender = makeIPv6Addr(tokens[1]).value_or(IPv6Addr{});
      ev.param = std::nullopt;
      return std::make_optional(ev);
    } else if (tokens.size() == 3) {
      ResEvent ev;
      ev.num = makeHexedU8(tokens[0]).value_or(HexedU8{});
      ev.sender = makeIPv6Addr(tokens[1]).value_or(IPv6Addr{});
      ev.param = makeHexedU8(tokens[2]);
      return std::make_optional(ev);
    }
    M5_LOGE("rx_event: Unexpected end of input.");
    return std::nullopt;
  };
  // EPANDESCを受信する
  auto rx_epandesc =
      [&commport](const std::string &name) -> std::optional<ResEpandesc> {
    std::vector<std::pair<std::string, std::string>> tokens;
    constexpr std::size_t N{6};
    // 必要な行数読み込む
    tokens.reserve(N);
    for (auto i = 0; i < N; ++i) {
      auto [left, sep1] = get_token(commport, ':');
      auto [right, sep2] = get_token(commport, ' ');
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
    ResEpandesc ev;
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
      [&commport](const std::string &name) -> std::optional<ResErxudp> {
    std::vector<std::string> tokens;
    constexpr std::size_t N{7};
    // 必要なトークン数読み込む
    tokens.reserve(N);
    for (auto i = 0; i < N; ++i) {
      auto [x, sep] = get_token(commport, ' ');
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
      ResErxudp ev;
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
      auto [x, _sep] = get_token(commport, '\r');
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
      [&commport](const std::string &token) -> std::optional<Response> {
    M5_LOGE("Unknown event: \"%s\"", token.c_str());
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

// 要求を送る
bool Bp35a1::send_request(
    Stream &commport, const SmartMeterIdentifier &smart_meter_ident,
    EchonetLiteTransactionId tid,
    const std::vector<SmartElectricEnergyMeter::EchonetLiteEPC> epcs) {
  EchonetLiteFrame frame;
  // EHD: ECHONET Lite 電文ヘッダー
  frame.ehd = EchonetLiteEHD;
  // TID: トランザクションID
  frame.tid = tid;
  // SEOJ: メッセージの送り元(sender : 自分自身)
  frame.edata.seoj = EchonetLiteSEOJ(HomeController::EchonetLiteEOJ);
  // DEOJ: メッセージの行き先(destination : スマートメーター)
  frame.edata.deoj = EchonetLiteDEOJ(SmartElectricEnergyMeter::EchonetLiteEOJ);
  // ESV : ECHONET Lite サービスコード
  frame.edata.esv = EchonetLiteESV::Get;
  // OPC: 処理プロパティ数
  frame.edata.opc = epcs.size();
  // ECHONET Liteプロパティ
  std::transform(
      epcs.cbegin(), epcs.cend(), std::back_inserter(frame.edata.props),
      [](const SmartElectricEnergyMeter::EchonetLiteEPC v) -> EchonetLiteProp {
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
  std::vector<uint8_t> payload = serializeFromEchonetLiteFrame(frame);
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
  commport.write(line.c_str(), line.length());
  // つづけてEchonet Liteフレーム (バイナリ)を送る
  // CRLFは不要
  commport.write(payload.data(), payload.size());
  // デバッグ用
  std::copy(payload.begin(), payload.end(),
            std::ostream_iterator<HexedU8>(oss));
  M5_LOGD("%s", oss.str().c_str());
  //
  return has_ok(commport, RETRY_TIMEOUT);
}

// 接続(PANA認証)要求を送る
bool Bp35a1::connect(Stream &commport,
                     const SmartMeterIdentifier &smart_meter_ident,
                     DisplayMessageT message, void *user_data) {
  //
  M5_LOGD("%s", to_string(smart_meter_ident).c_str());

  // 通信チャネルを設定する
  message("Set Channel", user_data);
  write_with_crln(commport,
                  "SKSREG S2 "s + std::string{smart_meter_ident.channel});
  if (!has_ok(commport, RETRY_TIMEOUT)) {
    return false;
  }
  // Pan IDを設定する
  message("Set Pan ID", user_data);
  write_with_crln(commport,
                  "SKSREG S3 "s + std::string{smart_meter_ident.pan_id});
  if (!has_ok(commport, RETRY_TIMEOUT)) {
    return false;
  }
  // 返答を受け取る前にクリアしておく
  clear_read_buffer(commport);

  // PANA認証要求
  message("Connecting...", user_data);
  write_with_crln(commport,
                  "SKJOIN "s + std::string{smart_meter_ident.ipv6_address});
  if (!has_ok(commport, RETRY_TIMEOUT)) {
    return false;
  }
  // PANA認証要求結果を受け取る
  const auto timeover = std::chrono::steady_clock::now() + RETRY_TIMEOUT;
  do {
    // いったん止める
    delay(100);
    //
    if (auto opt_res = receive_response(commport)) {
      // 何か受け取ったみたい
      const Response &resp = opt_res.value();
      std::visit([](const auto &x) { M5_LOGD("%s", to_string(x).c_str()); },
                 resp);
      if (const auto *eventp = std::get_if<ResEvent>(&resp)) {
        // イベント番号
        switch (eventp->num.u8) {
        case 0x24: {
          // EVENT 24 :
          // PANAによる接続過程でエラーが発生した(接続が完了しなかった)
          M5_LOGD("Fail to connect");
          message("Fail to connect", user_data);
          return false;
        }
        case 0x25: {
          // EVENT 25 : PANAによる接続が完了した
          M5_LOGD("Connected");
          message("Connected", user_data);
          return true;
        }
        default:
          break;
        }
      }
    }
  } while (std::chrono::steady_clock::now() < timeover);
  //
  return false;
}

// 接続(PANA認証)要求を送る
bool Bp35a1::connect2(std::ostream &os, std::chrono::seconds timeout,
                      Stream &commport,
                      SmartMeterIdentifier smart_meter_ident) {
  //
  M5_LOGD("%s", to_string(smart_meter_ident).c_str());

  // 通信チャネルを設定する
  os << "Set Channel" << std::endl;
  write_with_crln(commport,
                  "SKSREG S2 "s + std::string{smart_meter_ident.channel});
  if (!has_ok(commport, timeout)) {
    return false;
  }
  // Pan IDを設定する
  os << "Set Pan ID" << std::endl;
  write_with_crln(commport,
                  "SKSREG S3 "s + std::string{smart_meter_ident.pan_id});
  if (!has_ok(commport, timeout)) {
    return false;
  }
  // 返答を受け取る前にクリアしておく
  clear_read_buffer(commport);

  // PANA認証要求
  os << "Connecting..." << std::endl;
  write_with_crln(commport,
                  "SKJOIN "s + std::string{smart_meter_ident.ipv6_address});
  if (!has_ok(commport, timeout)) {
    return false;
  }
  // PANA認証要求結果を受け取る
  const auto timeover = std::chrono::steady_clock::now() + timeout;
  do {
    // いったん止める
    std::this_thread::sleep_for(100ms);
    //
    if (auto opt_res = receive_response(commport)) {
      // 何か受け取ったみたい
      const Response &resp = opt_res.value();
      std::visit([](const auto &x) { M5_LOGD("%s", to_string(x).c_str()); },
                 resp);
      if (const auto *eventp = std::get_if<ResEvent>(&resp)) {
        // イベント番号
        switch (eventp->num.u8) {
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
  } while (std::chrono::steady_clock::now() < timeover);
  //
  return false;
}

// アクティブスキャンを実行する
std::optional<Bp35a1::ResEpandesc>
Bp35a1::do_active_scan(Stream &commport, DisplayMessageT message,
                       void *user_data) {
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
      if (auto opt_res = receive_response(commport)) {
        // 何か受け取ったみたい
        const Response &resp = opt_res.value();
        std::visit([](const auto &x) { M5_LOGD("%s", to_string(x).c_str()); },
                   resp);
        if (const auto *eventp = std::get_if<ResEvent>(&resp)) {
          // イベント番号
          if (eventp->num.u8 == 0x22) {
            // EVENT 22
            // つまりアクティブスキャンの完了報告を確認しているのでスキャン結果を返す
            return target_Whm;
          }
        } else if (const auto *epandescp = std::get_if<ResEpandesc>(&resp)) {
          // 接続対象のスマートメータを発見した
          target_Whm = *epandescp;
        }
      }
    }
    // EVENT 22がやってこなかったようだね
    return std::nullopt;
  };
  //
  std::optional<ResEpandesc> found{std::nullopt};
  message("Active Scan", user_data);
  // 接続対象のスマートメータをスキャンする
  for (uint8_t duration : {5, 6, 7, 8}) {
    message("Now on scanning...", user_data);
    // スキャン要求を出す
    write_with_crln(commport, "SKSCAN 2 FFFFFFFF "s + std::to_string(duration));
    if (!has_ok(commport, RETRY_TIMEOUT)) {
      break;
    }
    found = got_respond(duration);
    if (found.has_value()) {
      // 接続対象のスマートメータを発見したら脱出する
      break;
    }
  }
  message("Active Scan Completed.", user_data);
  return found;
}

// アクティブスキャンを実行する
std::optional<Bp35a1::ResEpandesc>
Bp35a1::do_active_scan2(std::ostream &os, Stream &commport,
                        std::chrono::seconds timeout) {
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
      std::this_thread::sleep_for(
          std::chrono::milliseconds{single_ch_scan_millis});
      // 結果を受け取る
      if (auto opt_res = receive_response(commport)) {
        // 何か受け取ったみたい
        const Response &resp = opt_res.value();
        std::visit([](const auto &x) { M5_LOGD("%s", to_string(x).c_str()); },
                   resp);
        if (const auto *eventp = std::get_if<ResEvent>(&resp)) {
          // イベント番号
          if (eventp->num.u8 == 0x22) {
            // EVENT 22
            // つまりアクティブスキャンの完了報告を確認しているのでスキャン結果を返す
            return target_Whm;
          }
        } else if (const auto *epandescp = std::get_if<ResEpandesc>(&resp)) {
          // 接続対象のスマートメータを発見した
          target_Whm = *epandescp;
        }
      }
    }
    // EVENT 22がやってこなかったようだね
    return std::nullopt;
  };
  //
  std::optional<ResEpandesc> found{std::nullopt};
  os << "Active Scan" << std::endl;
  // 接続対象のスマートメータをスキャンする
  for (uint8_t duration : {5, 6, 7, 8}) {
    os << "Now on scanning..." << std::endl;
    // スキャン要求を出す
    write_with_crln(commport, "SKSCAN 2 FFFFFFFF "s + std::to_string(duration));
    if (!has_ok(commport, timeout)) {
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
std::optional<Bp35a1::SmartMeterIdentifier> Bp35a1::startup_and_find_meter(
    Stream &commport,
    std::pair<std::string_view, std::string_view> b_route_id_password,
    DisplayMessageT display_message, void *user_data) {
  // 一旦セッションを切る
  write_with_crln(commport, "SKTERM"s);

  delay(1000);
  clear_read_buffer(commport);

  // エコーバック抑制
  write_with_crln(commport, "SKSREG SFE 0"s);
  if (!has_ok(commport, RETRY_TIMEOUT)) {
    return std::nullopt;
  }

  const auto [bid, bpass] = b_route_id_password;
  // パスワード設定
  display_message("Set password", user_data);
  write_with_crln(commport, "SKSETPWD C "s + std::string{bpass});
  if (!has_ok(commport, RETRY_TIMEOUT)) {
    return std::nullopt;
  }

  // ID設定
  display_message("Set ID", user_data);
  write_with_crln(commport, "SKSETRBID "s + std::string{bid});
  if (!has_ok(commport, RETRY_TIMEOUT)) {
    return std::nullopt;
  }

  // アクティブスキャン実行
  std::optional<ResEpandesc> conn_target =
      do_active_scan(commport, display_message, user_data);
  // アクティブスキャン結果を確認
  if (!conn_target.has_value()) {
    // 接続対象のスマートメーターが見つからなかった
    display_message("smart meter not found.", user_data);
    M5_LOGD("smart meter not found.");
    return std::nullopt;
  }

  // アクティブスキャン結果をもとにしてipv6アドレスを得る
  display_message("get ipv6 address\n", user_data);
  auto str_addr = std::string(conn_target.value().addr);
  std::optional<IPv6Addr> resp_ipv6_address =
      get_ipv6_address(commport, RETRY_TIMEOUT, str_addr);

  if (!resp_ipv6_address.has_value()) {
    display_message("get ipv6 address fail.", user_data);
    M5_LOGD("get ipv6 address fail.");
    return std::nullopt;
  }
  // アクティブスキャン結果
  auto addr = resp_ipv6_address.value();
  return SmartMeterIdentifier{
      .ipv6_address = makeIPv6Addr(addr).value_or(IPv6Addr{}),
      .channel = conn_target.value().channel,
      .pan_id = conn_target.value().pan_id,
  };
}

// BP35A1を起動してアクティブスキャンを開始する
std::optional<Bp35a1::SmartMeterIdentifier> Bp35a1::startup_and_find_meter2(
    std::ostream &os, Stream &commport, const std::string &route_b_id,
    const std::string &route_b_password, std::chrono::seconds timeout) {
  // 一旦セッションを切る
  write_with_crln(commport, "SKTERM"s);

  std::this_thread::sleep_for(1s);
  clear_read_buffer(commport);

  // エコーバック抑制
  write_with_crln(commport, "SKSREG SFE 0"s);
  if (!has_ok(commport, timeout)) {
    return std::nullopt;
  }

  // パスワード設定
  os << "Set password" << std::endl;
  write_with_crln(commport, "SKSETPWD C "s + route_b_password);
  if (!has_ok(commport, timeout)) {
    return std::nullopt;
  }

  // ID設定
  os << "Set ID" << std::endl;
  write_with_crln(commport, "SKSETRBID "s + route_b_id);
  if (!has_ok(commport, timeout)) {
    return std::nullopt;
  }

  // アクティブスキャン実行
  std::optional<ResEpandesc> conn_target =
      do_active_scan2(os, commport, timeout);
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
  std::optional<IPv6Addr> resp_ipv6_address =
      get_ipv6_address(commport, timeout, str_addr);

  if (!resp_ipv6_address.has_value()) {
    std::ostringstream ss;
    ss << "get ipv6 address fail.";
    os << ss.str() << std::endl;
    M5_LOGD("%s", ss.str().c_str());
    return std::nullopt;
  }
  // アクティブスキャン結果
  auto addr = resp_ipv6_address.value();
  return SmartMeterIdentifier{
      .ipv6_address = makeIPv6Addr(addr).value_or(IPv6Addr{}),
      .channel = conn_target.value().channel,
      .pan_id = conn_target.value().pan_id,
  };
}