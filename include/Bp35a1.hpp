// Copyright (c) 2022 Akihiro Yamamoto.
// Licensed under the MIT License <https://spdx.org/licenses/MIT.html>
// See LICENSE file in the project root for full license information.
//
#pragma once
#include <M5StickCPlus.h>
#undef min
#include <SmartWhm.hpp>
#include <cstring>
#include <map>
#include <optional>
#include <string>
#include <tuple>

//
//
//
struct SmartMeterIdentifier {
  std::string ipv6_address;
  std::string channel;
  std::string pan_id;
};

namespace std {
//
string to_string(SmartMeterIdentifier ident) {
  std::string s;
  s += "ipv6_address: \"" + ident.ipv6_address + "\",";
  s += "channel: \"" + ident.channel + "\",";
  s += "pan_id: \"" + ident.pan_id + "\"";
  return s;
}
} // namespace std

//
//
//
struct Bp35a1Response {
  // 受信した時間
  std::time_t created_at;
  // 受信したイベントの種類
  enum class Tag { EVENT, EPANDESC, ERXUDP, UNKNOWN };
  Tag tag;
  // key-valueストア
  std::map<std::string, std::string> keyval;
  //
  Bp35a1Response(std::time_t at, Tag t, std::map<std::string, std::string> &&kv)
      : created_at{at}, tag{t}, keyval{std::move(kv)} {}
};

//
namespace std {
string to_string(const Bp35a1Response &response) {
  std::string s;
  switch (response.tag) {
  case Bp35a1Response::Tag::EVENT:
    s += "EVENT ";
    break;
  case Bp35a1Response::Tag::EPANDESC:
    s += "EPANDESC ";
    break;
  case Bp35a1Response::Tag::ERXUDP:
    s += "ERXUDP ";
    break;
  case Bp35a1Response::Tag::UNKNOWN:
    s += "UNKNOWN";
    break;
  default:
    s += "??? ";
    break;
  }
  for (const auto &[key, val] : response.keyval) {
    s += "\"" + key + "\":\"" + val + "\"" + ",";
  }
  s.pop_back(); // 最後の,を削る
  return s;
}
} // namespace std

// バイナリからテキスト形式に変換する
std::string binary_to_text(const std::vector<uint8_t> &binaries) {
  std::string text{};
  std::for_each(std::begin(binaries), std::end(binaries),
                [&text](uint8_t octet) {
                  char work[10]{'\0'};
                  std::sprintf(work, "%02X", static_cast<int32_t>(octet));
                  text += std::string(work);
                });
  return text;
}

// テキスト形式からバイナリに戻す
std::vector<uint8_t> text_to_binary(std::string_view text) {
  std::vector<uint8_t> binary;
  for (auto itr = text.begin(); itr != text.end();) {
    char work[10]{'\0'};
    // 8ビットは16進数2文字なので2文字毎に変換する
    work[0] = *itr++;
    work[1] = (itr == text.end()) ? '\0' : *itr++;
    work[2] = '\0';
    binary.push_back(std::strtol(work, nullptr, 16));
  }
  return binary;
}

//
//
//
class Bp35a1 {
  const size_t retry_limits;
  Stream &commport;
  std::string_view b_route_id;
  std::string_view b_route_password;
  SmartMeterIdentifier smart_meter_ident;

public:
  using Response = Bp35a1Response;
  // メッセージを表示する関数型
  typedef void (*DisplayMessageT)(const char *);
  //
  Bp35a1(Stream &stream,
         std::pair<std::string_view, std::string_view> b_id_password,
         size_t limits = 100)
      : retry_limits(limits),
        commport{stream},
        b_route_id{b_id_password.first},
        b_route_password{b_id_password.second},
        smart_meter_ident{} {}
  // BP35A1を起動して接続する
  bool boot(DisplayMessageT display_message) {
    // エコーバック抑制
    write_with_crln("SKSREG SFE 0");

    // 一旦セッションを切る
    write_with_crln("SKTERM");
    delay(1000);
    clear_read_buffer();

    // パスワード設定
    display_message("Set password\n");
    write_with_crln("SKSETPWD C " + std::string{b_route_password});
    if (!has_ok()) {
      return false;
    }

    // ID設定
    display_message("Set ID\n");
    write_with_crln("SKSETRBID " + std::string{b_route_id});
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
    auto opt_ipv6_address = get_ipv6_address(r.keyval["Addr"], display_message);

    if (!opt_ipv6_address.has_value()) {
      display_message("get ipv6 address fail.");
      ESP_LOGD(MAIN, "get ipv6 address fail.");
      return false;
    }

    // アクティブスキャン結果をインスタンス変数にセットする
    smart_meter_ident.ipv6_address = opt_ipv6_address.value();
    smart_meter_ident.channel = r.keyval["Channel"];
    smart_meter_ident.pan_id = r.keyval["Pan ID"];

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
    write_with_crln("SKLL64 " + addr);
    // ipv6 アドレスを受け取る
    for (auto retry = 1; retry <= retry_limits; ++retry) {
      // いったん止める
      delay(100);
      //
      std::string str = read_line_without_crln<512>();
      if (!str.empty()) {
        return str;
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
        ESP_LOGD(MAIN, "%s", std::to_string(r).c_str());
        if (r.tag == Response::Tag::EVENT) {
          // イベント番号
          auto num = std::strtol(r.keyval["NUM"].c_str(), nullptr, 16);
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
      write_with_crln("SKSCAN 2 FFFFFFFF " + std::to_string(duration));
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
    ESP_LOGD(MAIN, "%s", std::to_string(smart_meter_ident).c_str());

    // 通信チャネルを設定する
    message("Set Channel\n");
    write_with_crln("SKSREG S2 " + smart_meter_ident.channel);
    if (!has_ok()) {
      return false;
    }
    // Pan IDを設定する
    message("Set Pan ID\n");
    write_with_crln("SKSREG S3 " + smart_meter_ident.pan_id);
    if (!has_ok()) {
      return false;
    }
    // 返答を受け取る前にクリアしておく
    clear_read_buffer();

    // PANA認証要求
    message("Connecting...\n");
    write_with_crln("SKJOIN " + smart_meter_ident.ipv6_address);
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
          switch (std::strtol(r.keyval["NUM"].c_str(), nullptr, 16)) {
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
      std::string str = read_line_without_crln<512>();
      if (str.empty()) {
        delay(100);
        continue;
      }
      const std::size_t length = str.length();
      const char *cstr = str.c_str();
      if (length >= 2 && cstr[0] == 'O' && cstr[1] == 'K') {
        return true;
      } else if (length >= 4 && cstr[0] == 'F' && cstr[1] == 'A' &&
                 cstr[2] == 'I' && cstr[3] == 'L') {
        ESP_LOGD(MAIN, "fail: \"%s\"", cstr);
        return false;
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
    auto to_string_hexd_u16 = [](uint16_t word) -> std::string {
      char buff[10]{'\0'};
      std::sprintf(buff, "%04X", word);
      return std::string(buff);
    };
    std::string line;
    line += "SKSENDTO ";
    line += "1 ";                                   // HANDLE
    line += smart_meter_ident.ipv6_address + " ";   // IPADDR
    line += std::string(EchonetLiteUdpPort) + " ";  // PORT
    line += "1 ";                                   // SEC
    line += to_string_hexd_u16(frame.size()) + " "; // DATALEN
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
    std::optional<std::string> opt_tag = get_token<512>();
    // 受信したメッセージを解析する
    if (!opt_tag.has_value()) {
      // 何も受け取れなかった場合
      return std::nullopt;
    } else if (opt_tag.value() == std::string_view("EVENT")) {
      //
      // EVENTを受け取った
      //
      return [this]() -> std::optional<Response> {
        // EVENTメッセージの値
        constexpr std::string_view keys[] = {
            "NUM",    // イベント番号
            "SENDER", // イベントのトリガーとなったメッセージの発信元アドレス
            "PARAM",  // イベント固有の引数
        };
        //
        std::map<std::string, std::string> kv{};
        for (const auto &key : keys) {
          auto opt_token = get_token<512>();
          if (opt_token.has_value()) {
            // 値を得る
            kv.insert(std::make_pair(key, opt_token.value()));
          } else {
            // 値が不足しているが
            // 3番目のパラメータがないことがあるので
            // とくに何もしません
          }
        }
        return Response(std::time(nullptr), Response::Tag::EVENT,
                        std::move(kv));
      }();
    } else if (opt_tag.value() == std::string_view("EPANDESC")) {
      //
      // EPANDESCを受け取った
      //
      return [this]() -> std::optional<Response> {
        // EPANDESCメッセージの各行ごとの値
        // この順番どおりに読み込む
        constexpr std::string_view items[] = {
            "Channel", // 発見したPANの周波数(論理チャンネル番号)
            "Channel Page", // 発見したPANのチャンネルページ
            "Pan ID",       //  発見したPANのPAN ID
            "Addr",   // アクティブスキャン応答元のアドレス
            "LQI",    // 受信したビーコンの受信ED値(RSSI)
            "PairID", // (IEが含まれる場合)相手から受信したPairing
                      // ID
        };
        //
        std::map<std::string, std::string> kv{};
        for (const auto &it : items) {
          std::string line;
          for (auto retry = 1; retry <= retry_limits; ++retry) {
            // 次の行を得る
            line = read_line_without_crln<512>();
            if (line.length() > 0) {
              break;
            }
            delay(10);
          }
          // ':'の位置でleftとrightに分ける
          std::string_view sv{line};
          size_t pos = sv.find(":");
          if (pos == std::string::npos) {
            // 予想と違う行が入ってきたので,今ある値を返す
            return Response(std::time(nullptr), Response::Tag::EPANDESC,
                            std::move(kv));
          }
          std::string left{sv, 0, pos};
          std::string right{sv, pos + 1, sv.length()};
          // 先頭に空白があるからここではfindで確認する
          // キーが一致したらmapに入れる
          if (left.find(it) != std::string::npos) {
            kv.insert(std::make_pair(it, right));
          }
        }
        // keyvalue数の一致確認
        if (kv.size() != std::size(items)) {
          ESP_LOGE(MAIN, "Mismatched size : %d, %d", kv.size(),
                   std::size(items));
        }
        return Response(std::time(nullptr), Response::Tag::EPANDESC,
                        std::move(kv));
      }();
    } else if (opt_tag.value() == std::string_view("ERXUDP")) {
      //
      // ERXUDPを受け取った
      //
      return [this]() -> std::optional<Response> {
        // ERXUDPメッセージの値
        constexpr std::string_view keys[] = {
            // 送信元IPv6アドレス
            "SENDER",
            // 送信先IPv6アドレス
            "DEST",
            // 送信元ポート番号
            "RPORT",
            // 送信元ポート番号
            "LPORT",
            // 送信元のMACアドレス
            "SENDERLLA",
            // SECURED=1 : MACフレームが暗号化されていた
            // SECURED=0 : MACフレームが暗号化されていなかった
            "SECURED",
            // データの長さ
            "DATALEN",
            // ここからデータなんだけどバイナリ形式だから特別扱いする。
        };
        //
        std::map<std::string, std::string> kv;
        for (const auto &key : keys) {
          auto opt_token = get_token<512>();
          if (opt_token.has_value()) {
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
        std::size_t datalen =
            std::strtol(kv.at("DATALEN").c_str(), nullptr, 16);
        // メモリーを確保して
        std::vector<uint8_t> vect{};
        vect.resize(datalen);
        // データの長さ分読み込む
        commport.readBytes(vect.data(), vect.size());
        // バイナリからテキスト形式に変換する
        std::string textformat = binary_to_text(vect);
        // key-valueストアに入れる
        kv.insert(std::make_pair("DATA", textformat));
        return Response(std::time(nullptr), Response::Tag::ERXUDP,
                        std::move(kv));
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
  template <std::size_t N> std::optional<std::string> get_token() {
    std::array<char, N> buf{};

    for (auto itr = buf.begin(); itr != buf.end(); ++itr) {
      // 1文字読み込んで
      int ch = commport.read();
      if (ch < 0) {
        // ストリームの最後まで読み込んだので脱出する
        break;
      } else if (isspace(ch)) {
        // 空白を見つけたので脱出する
        // 読み込んだ文字は捨てる
        break;
      } else {
        // 読み込んだ文字はバッファへ追加する
        *itr = ch;
      }
    }
    //
    if (std::strlen(buf.data()) == 0) {
      // 空白またはCRまたはLFで始まっていたので,トークンがなかった
      return std::nullopt;
    }
    return std::string{buf.data()};
  }
  // ストリームから読み込んでCRLFを捨てた行を得る関数
  template <std::size_t N> std::string read_line_without_crln() {
    std::array<char, N> buffer{'\0'};
    std::size_t len =
        commport.readBytesUntil('\n', buffer.data(), buffer.size());
    if (buffer[len - 1] == '\r') {
      len--;
    }
    buffer[len] = '\0';
    if (len > 0) {
      ESP_LOGD(RECEIVE, "%s", buffer.data());
    }
    return std::string{buffer.data()};
  }
  // CRLFを付けてストリームに1行書き込む関数
  void write_with_crln(const std::string &line) {
    const char *cstr = line.c_str();
    ESP_LOGD(SEND, "%s", cstr);
    commport.write(cstr, std::strlen(cstr));
    commport.write("\r\n");
    // メッセージ送信完了待ち
    commport.flush();
  }
};
