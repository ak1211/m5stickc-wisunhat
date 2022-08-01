// Copyright (c) 2022 Akihiro Yamamoto.
// Licensed under the MIT License <https://spdx.org/licenses/MIT.html>
// See LICENSE file in the project root for full license information.
//
#include <M5StickCPlus.h>
#include <cstring>
#include <map>
#include <optional>
#include <string>
#include <vector>

//
// Bルート接続文字列
//
static constexpr std::string_view BPASSWORD{"000000000000"};
static constexpr std::string_view BID{"00000000000000000000000000000000"};

// BP35A1と会話できるポート番号
constexpr int CommPortRx{26};
constexpr int CommPortTx{0};

// ログ出し用
static constexpr char MAIN[] = "MAIN";
static constexpr char SEND[] = "SEND";
static constexpr char RECEIVE[] = "RECEIVE";

// ECHONET Lite の UDP ポート番号
constexpr std::string_view EchonetLiteUdpPort = "0E1A";

// ECHONET Lite 電文ヘッダー 1,2
// 0x1081で固定
// EHD1: 0x10 (ECHONET Lite規格)
// EHD2: 0x81 (規定電文形式)
constexpr uint8_t EchonetLiteEHD1{0x10};
constexpr uint8_t EchonetLiteEHD2{0x81};

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
namespace HomeController {
// グループコード 0x05 (管理・操作関連機器クラス)
// クラスコード 0xFF (コントローラ)
// インスタンスコード 0x01 (インスタンスコード1)
constexpr std::array<uint8_t, 3> EchonetLiteEOJ{0x05, 0xFF, 0x01};
}; // namespace HomeController

//
// スマートメータに接続時に送られてくるECHONET Liteオブジェクト
//
namespace NodeProfileClass {
// グループコード 0x0E (ノードプロファイルクラス)
// クラスコード 0xF0
// インスタンスコード 0x01 (一般ノード)
constexpr std::array<uint8_t, 3> EchonetLiteEOJ{0x0E, 0xF0, 0x01};
// ノードプロファイルクラスからの応答フレームならtrue
bool isNodeProfileClass(const std::array<uint8_t, 3> eoj) {
  return eoj == EchonetLiteEOJ;
}
}; // namespace NodeProfileClass

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
namespace SmartWhm {
// 低圧スマート電力量メータクラス規定より
// スマートメーターのECHONET Liteオブジェクト
// クラスグループコード 0x02
// クラスコード 0x88
// インスタンスコード 0x01
constexpr std::array<uint8_t, 3> EchonetLiteEOJ{0x02, 0x88, 0x01};

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

// スマートメータからの応答フレームならtrue
bool isSmartWhm(const std::array<uint8_t, 3> eoj) {
  return eoj == EchonetLiteEOJ;
}

// 通信用のフレームを作る
std::vector<uint8_t> make_get_frame(uint8_t tid1, uint8_t tid2,
                                    EchonetLiteESV esv, EchonetLiteEPC epc) {
  EchonetLiteFrame frame;
  frame.ehd1 = EchonetLiteEHD1; // ECHONET Lite 電文ヘッダー 1
  frame.ehd2 = EchonetLiteEHD2; // ECHONET Lite 電文ヘッダー 2
  frame.tid1 = tid1;
  frame.tid2 = tid2;
  // メッセージの送り元(sender : 自分自身)
  std::copy(HomeController::EchonetLiteEOJ.begin(),
            HomeController::EchonetLiteEOJ.end(), frame.edata.seoj);
  // メッセージの行き先(destination : スマートメーター)
  std::copy(SmartWhm::EchonetLiteEOJ.begin(), SmartWhm::EchonetLiteEOJ.end(),
            frame.edata.deoj);
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
std::string show(const EchonetLiteFrame &frame) {
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
// 測定値
//

// 瞬時電力
struct Watt {
  int32_t watt; // 瞬時電力(単位 W)
  //
  Watt(std::array<uint8_t, 4> array) {
    watt = (array[0] << 24) | (array[1] << 16) | (array[2] << 8) | array[3];
  }
  //
  std::string show() const { return std::to_string(watt) + " W"; }
  //
  bool operator==(const Watt &other) const { return (watt == other.watt); }
  bool operator!=(const Watt &other) const { return !(*this == other); }
};

// 瞬時電流
struct Ampere {
  int16_t r_deciA; // R相電流(単位 1/10A == 1 deci A)
  int16_t t_deciA; // T相電流(単位 1/10A == 1 deci A)
  //
  Ampere(std::array<uint8_t, 4> array) {
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
    auto r = std::make_pair(r_deciA / 10, r_deciA * 10 % 10);
    auto t = std::make_pair(t_deciA / 10, t_deciA * 10 % 10);
    std::string s;
    s += "R: " + std::to_string(r.first) + "." + std::to_string(r.second) +
         " A, T: " + std::to_string(t.first) + "." + std::to_string(t.second) +
         " A";
    return s;
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
  //
  bool operator==(const Ampere &other) const {
    return (r_deciA == other.r_deciA) && (t_deciA == other.t_deciA);
  }
  bool operator!=(const Ampere &other) const { return !(*this == other); }
};

// 定時積算電力量
struct CumulativeWattHour {
  uint16_t year;                // 年
  uint8_t month;                // 月
  uint8_t day;                  // 日
  uint8_t hour;                 // 時
  uint8_t minutes;              // 分
  uint8_t seconds;              // 秒
  uint32_t cumlative_watt_hour; // 積算電力量
  //
  CumulativeWattHour(std::array<uint8_t, 11> array) {
    year = (array[0] << 8) | array[1];
    month = array[2];
    day = array[3];
    hour = array[4];
    minutes = array[5];
    seconds = array[6];
    cumlative_watt_hour =
        (array[7] << 24) | (array[8] << 16) | (array[9] << 8) | array[10];
  }
  //
  std::string show() const {
    char buff[100]{'\0'};
    std::sprintf(buff, "%4d/%2d/%2d %02d:%02d:%02d %d", year, month, day, hour,
                 minutes, seconds, cumlative_watt_hour);
    return std::string(buff);
  }
  //
  bool operator==(const CumulativeWattHour &other) const {
    return (year == other.year) && (month == other.month) &&
           (day == other.day) && (hour == other.hour) &&
           (minutes == other.minutes) && (seconds == other.seconds) &&
           (cumlative_watt_hour == other.cumlative_watt_hour);
  }
  bool operator!=(const CumulativeWattHour &other) const {
    return !(*this == other);
  }
};
} // namespace SmartWhm

//
//
//
class Response {
public:
  // 受信したイベントの種類
  enum class Tag { EVENT, EPANDESC, ERXUDP };
  Tag tag;
  // key-valueストア
  std::map<std::string, std::string> keyval;
  //
  std::string show() const {
    std::string s;
    switch (tag) {
    case Tag::EVENT:
      s += "EVENT ";
      break;
    case Tag::EPANDESC:
      s += "EPANDESC ";
      break;
    case Tag::ERXUDP:
      s += "ERXUDP ";
      break;
    default:
      s += "??? ";
      break;
    }
    for (const auto &item : keyval) {
      s += "\"" + item.first + "\":\"" + item.second + "\"" + ",";
    }
    s.pop_back(); // 最後の,を削る
    return s;
  }
  // バイナリからテキスト形式に変換する
  static std::string binary_to_text(const std::vector<uint8_t> &vect) {
    std::string text;
    for (auto itr = vect.begin(); itr != vect.end(); ++itr) {
      char work[100]{'\0'};
      int32_t datum = *itr;
      std::sprintf(work, "%02X", datum);
      text += std::string(work);
    }
    return text;
  }
  // テキスト形式からバイナリに戻す
  static std::vector<uint8_t> text_to_binary(std::string_view text) {
    std::vector<uint8_t> binary;
    char work[10]{'\0'};

    for (auto itr = text.begin(); itr != text.end();) {
      // 8ビットは16進数2文字なので2文字毎に変換する
      work[0] = *itr++;
      work[1] = (itr == text.end()) ? '\0' : *itr++;
      work[2] = '\0';
      binary.push_back(std::strtol(work, nullptr, 16));
    }
    return binary;
  }
};

//
//
//
class BP35A1 {
public:
  struct BRouteConnectionString {
    std::string_view b_id;
    std::string_view b_password;
  };

private:
  const size_t retry_limits;
  Stream &commport;
  BRouteConnectionString b_route_connection_string;
  struct SmartMeterIdentifier {
    std::string ipv6_address;
    std::string channel;
    std::string pan_id;
    //
    std::string show() const {
      std::string s;
      s += "ipv6_address: \"" + ipv6_address + "\",";
      s += "channel: \"" + channel + "\",";
      s += "pan_id: \"" + pan_id + "\"";
      return s;
    }
  } smart_meter_ident;

public:
  //
  BP35A1(Stream &stream, BRouteConnectionString br, size_t limits = 100)
      : retry_limits(limits),
        commport{stream},
        b_route_connection_string{br},
        smart_meter_ident{} {}
  // メッセージを表示する関数
  typedef void (*DisplayMessageT)(const char *);
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
    write_with_crln("SKSETPWD C " +
                    std::string{b_route_connection_string.b_password});
    if (!has_ok()) {
      return false;
    }

    // ID設定
    display_message("Set ID\n");
    write_with_crln("SKSETRBID " + std::string{b_route_connection_string.b_id});
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
                                              void (*message)(const char *)) {
    // 返答を受け取る前にクリアしておく
    clear_read_buffer();
    // ipv6 アドレス要求を送る
    message("Fetch ipv6 address\n");
    write_with_crln("SKLL64 " + addr);
    // ipv6 アドレスを受け取る
    for (uint32_t retry = 1; retry <= retry_limits; ++retry) {
      // いったん止める
      delay(100);
      //
      std::string str = read_line_without_crln<256>();
      if (str.length() > 0) {
        return str;
      }
    }
    return std::nullopt;
  }
  // アクティブスキャンを実行する
  std::optional<Response> do_active_scan(void (*message)(const char *)) {
    // スマートメーターからの返答を待ち受ける関数
    auto got_respond = [this](int8_t duration) -> std::optional<Response> {
      // スキャン対象のチャンネル番号
      // CHANNEL_MASKがFFFFFFFF つまり 11111111 11111111 11111111
      // 11111111なので 最下位ビットがチャンネル33なので
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
      //
      for (uint32_t u = 0; u <= all_scan_millis; u += single_ch_scan_millis) {
        // いったん止める
        delay(single_ch_scan_millis);
        // 結果を受け取る
        std::optional<Response> opt_res = watch_response();
        if (!opt_res.has_value()) {
          continue;
        }
        // 何か受け取ったみたい
        Response r = opt_res.value();
        ESP_LOGD(MAIN, "%s", r.show().c_str());
        if (r.tag == Response::Tag::EVENT) {
          // イベント番号
          int num = std::strtol(r.keyval["NUM"].c_str(), nullptr, 16);
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
    for (auto duration : std::array<uint8_t, 5>{4, 5, 6, 7, 8}) {
      message(".");
      // スキャン要求を出す
      write_with_crln("SKSCAN 2 FFFFFFFF " + std::to_string(duration));
      if (!has_ok()) {
        break;
      }
      found = got_respond(duration);
      if (found) {
        // 接続対象のスマートメータを発見したら脱出する
        break;
      }
    }
    message("\n");
    return found;
  }
  // 接続(PANA認証)要求を送る
  bool connect(void (*message)(const char *)) {
    //
    ESP_LOGD(MAIN, "%s", smart_meter_ident.show().c_str());

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
    for (std::size_t retry = 0; retry <= retry_limits; ++retry) {
      // いったん止める
      delay(100);
      //
      std::optional<Response> opt_res = watch_response();
      if (opt_res.has_value()) {
        // 何か受け取った
        Response r = opt_res.value();
        if (r.tag == Response::Tag::EVENT) {
          int num = std::strtol(r.keyval["NUM"].c_str(), nullptr, 16);
          switch (num) {
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
    for (std::size_t retry = 0; retry < retry_limits; ++retry) {
      std::string str = read_line_without_crln<256>();
      if (str.length() == 0) {
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
  bool send_request(SmartWhm::EchonetLiteEPC epc) {
    std::vector<uint8_t> frame =
        SmartWhm::make_get_frame(0x00, 0x01, EchonetLiteESV::Get, epc);
    //
    auto to_string_hexd_u16 = [](uint16_t word) -> std::string {
      char buff[10]{'\0'};
      std::sprintf(buff, "%04X", word);
      return std::string(buff);
    };
    std::string line;
    line += {"SKSENDTO "};
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
    std::optional<std::string> opt_tag = get_token<256>();
    if (!opt_tag.has_value()) {
      return std::nullopt;
    }

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
        const std::array<std::string_view, 3> keys = {
            "NUM",    // イベント番号
            "SENDER", // イベントのトリガーとなったメッセージの発信元アドレス
            "PARAM",  // イベント固有の引数
        };
        //
        Response response;
        response.tag = Response::Tag::EVENT; // "EVENT"
        for (const auto &key : keys) {
          auto opt_token = get_token<256>();
          if (opt_token.has_value()) {
            // 値を得る
            response.keyval.insert(std::make_pair(key, opt_token.value()));
          } else {
            // 値が不足している
            // 3番目のパラメータがないことがあるので
            // とくに何もしません
          }
        }
        return response;
      }();
    } else if (opt_tag.value() == std::string_view("EPANDESC")) {
      //
      // EPANDESCを受け取った
      //
      return [this]() -> std::optional<Response> {
        // EPANDESCメッセージの各行ごとの値
        // この順番どおりに読み込む
        const std::array<std::string_view, 6> items = {
            "Channel", // 発見したPANの周波数(論理チャンネル番号)
            "Channel Page", // 発見したPANのチャンネルページ
            "Pan ID",       //  発見したPANのPAN ID
            "Addr", // アクティブスキャン応答元のアドレス
            "LQI",  // 受信したビーコンの受信ED値(RSSI)
            "PairID" // (IEが含まれる場合)相手から受信したPairing ID
        };
        //
        Response r;
        r.tag = Response::Tag::EPANDESC; // "EPANDESC"
        for (const auto &it : items) {
          std::string line;
          for (int retry = 1; retry <= retry_limits; ++retry) {
            // 次の行を得る
            line = read_line_without_crln<256>();
            if (line.length() > 0) {
              break;
            }
            delay(10);
          }
          // ':'の位置でleftとrightに分ける
          std::string_view sv{line};
          size_t pos = sv.find(":");
          if (pos == std::string::npos) {
            // 違う行が入ってきたのでバッファをそのままにして脱出する
            return r;
          }
          std::string left{sv, 0, pos};
          std::string right{sv, pos + 1, sv.length()};
          // 先頭に空白があるからここではfindで確認する
          // キーが一致したらmapに入れる
          if (left.find(it) != std::string::npos) {
            r.keyval.insert(std::make_pair(it, right));
          }
        }
        // keyvalue数の一致確認
        if (r.keyval.size() != items.size()) {
          ESP_LOGE(MAIN, "Mismatched size : %d, %d", r.keyval.size(),
                   items.size());
        }
        return r;
      }();
    } else if (opt_tag.value() == std::string_view("ERXUDP")) {
      //
      // ERXUDPを受け取った
      //
      return [this]() -> std::optional<Response> {
        // ERXUDPメッセージの値
        const std::array<std::string_view, 7> keys = {
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
        Response response;
        response.tag = Response::Tag::ERXUDP; // "ERXUDP"
        for (const auto &key : keys) {
          auto opt_token = get_token<256>();
          if (opt_token.has_value()) {
            // 値を得る
            response.keyval.insert(std::make_pair(key, opt_token.value()));
          } else {
            // 値が不足している
            return std::nullopt;
          }
        }
        //
        // データはバイナリで送られてくるので, テキスト形式に変換する。
        //
        std::size_t datalen =
            std::strtol(response.keyval["DATALEN"].c_str(), nullptr, 16);
        // メモリーを確保して
        std::vector<uint8_t> vect{};
        vect.resize(datalen);
        // データの長さ分読み込む
        commport.readBytes(vect.data(), vect.size());
        // バイナリからテキスト形式に変換する
        std::string textformat = Response::binary_to_text(vect);
        // key-valueストアに入れる
        response.keyval.insert(std::make_pair("DATA", textformat));
        return response;
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

//
// 測定値表示
//
template <class T> class MeasurementDisplay {
  typedef std::string (*ConvertFn)(std::optional<T>);
  //
  int size;
  int font;
  int color;
  int cursor_x;
  int cursor_y;
  ConvertFn to_string;
  std::string nowstr; // 現在表示中の文字

public:
  MeasurementDisplay(int text_size, int font, int text_color,
                     std::pair<int, int> cursor_xy, ConvertFn to_string_fn)
      : size{text_size},
        font{font},
        color{text_color},
        cursor_x{cursor_xy.first},
        cursor_y{cursor_xy.second},
        to_string{to_string_fn},
        nowstr{} {}
  //
  void update(std::optional<T> next) {
    std::string nextstr = to_string(next);
    // 値に変化がない
    if (nowstr == nextstr) {
      return;
    }
    M5.Lcd.setTextSize(size);
    // 黒色で現在表示中の文字を上書きする
    M5.Lcd.setTextColor(BLACK);
    M5.Lcd.setCursor(cursor_x, cursor_y, font);
    M5.Lcd.print(nowstr.c_str());
    //
    // 現在値を表示する
    M5.Lcd.setTextColor(color);
    M5.Lcd.setCursor(cursor_x, cursor_y, font);
    M5.Lcd.print(nextstr.c_str());
    // 更新
    nowstr = nextstr;
  }
};

// スマートメーターに定期的に要求を送る
void send_measurement_request(BP35A1 *bp35a1) {
  //
  struct DispatchList {
    using UpdateFn = std::function<void(DispatchList *)>;
    //
    UpdateFn update;              // 更新
    bool run;                     // これがtrueなら実行する
    const char *message;          // ログに送るメッセージ
    SmartWhm::EchonetLiteEPC epc; // 要求
  };
  // 1回のみ実行する場合
  DispatchList::UpdateFn one_shot = [](DispatchList *d) { d->run = false; };
  // 繰り返し実行する場合
  DispatchList::UpdateFn continueous = [](DispatchList *d) { d->run = true; };
  //
  static std::array<DispatchList, 6> dispach_list = {
      //
      // 起動時にそれぞれ1回のみ送る
      //
      // 係数
      DispatchList{one_shot, true, "request coefficient",
                   SmartWhm::EchonetLiteEPC::Coefficient},
      // 積算電力量単位
      DispatchList{one_shot, true, "request unit for whm",
                   SmartWhm::EchonetLiteEPC::Unit_for_cumulative_amounts},
      // 積算電力量有効桁数
      DispatchList{one_shot, true, "request number of effective digits",
                   SmartWhm::EchonetLiteEPC::Number_of_effective_digits},
      // 定時積算電力量計測値(正方向計測値)
      DispatchList{
          one_shot, true, "request amounts of electric power",
          SmartWhm::EchonetLiteEPC::
              Cumulative_amounts_of_electric_energy_measured_at_fixed_time},
      //
      // ここから定期的に繰り返して送る要求
      //
      // 瞬時電力要求
      DispatchList{continueous, true, "request inst-epower",
                   SmartWhm::EchonetLiteEPC::Measured_instantaneous_power},
      // 瞬時電流要求
      DispatchList{continueous, true, "request inst-current",
                   SmartWhm::EchonetLiteEPC::Measured_instantaneous_currents}};

  // 関数から抜けた後も保存しておくイテレータ
  static decltype(dispach_list)::iterator itr{dispach_list.begin()};

  // 次
  auto next_itr = [](decltype(dispach_list)::iterator itr)
      -> decltype(dispach_list)::iterator {
    if (itr == dispach_list.end()) {
      return dispach_list.begin();
    }
    return itr + 1;
  };

  // 実行フラグが立ってないなら, 次に送る
  for (; !itr->run; itr = next_itr(itr)) {
  }

  // 実行
  ESP_LOGD(MAIN, "%s", itr->message);
  if (bp35a1->send_request(itr->epc)) {
  } else {
    ESP_LOGD(MAIN, "request NG");
  }
  itr->update(itr); // 結果がどうあれ更新する

  // 次
  itr = next_itr(itr);
}

// 前方参照
static std::string to_str_watt(std::optional<SmartWhm::Watt>);
static std::string to_str_ampere(std::optional<SmartWhm::Ampere>);
static std::string
    to_str_cumlative_wh(std::optional<SmartWhm::CumulativeWattHour>);

// vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv
// グローバル変数
//
// BP35A1 初期化が完了するまでnull
BP35A1 *bp35a1(nullptr);
// 乗数(無い場合の乗数は1)
static std::optional<SmartWhm::Coefficient> whm_coefficient{std::nullopt};
// 単位
static std::optional<SmartWhm::Unit> whm_unit{std::nullopt};
// 測定量表示用
static MeasurementDisplay<SmartWhm::Watt> measurement_watt{
    2, 4, YELLOW, std::make_pair(10, 10), to_str_watt};
static MeasurementDisplay<SmartWhm::Ampere> measurement_ampere{
    1, 4, WHITE, std::make_pair(10, 10 + 48), to_str_ampere};
static MeasurementDisplay<SmartWhm::CumulativeWattHour>
    measurement_cumlative_wh{1, 4, WHITE, std::make_pair(10, 10 + 48 + 24),
                             to_str_cumlative_wh};
// ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

//
// bootメッセージ表示用
//
void display_boot_message(const char *s) { M5.Lcd.print(s); }

//
// Arduinoのsetup()関数
//
void setup() {
  M5.begin(true, true, true);
  M5.Lcd.setRotation(3);
  M5.Lcd.setTextSize(2);
  //
  Serial2.begin(115200, SERIAL_8N1, CommPortRx, CommPortTx);
  delay(1000);
  static BP35A1 bp35a1_instance(Serial2,
                                BP35A1::BRouteConnectionString{BID, BPASSWORD});
  if (!bp35a1_instance.boot(display_boot_message)) {
    display_boot_message("boot error, bye");
    ESP_LOGD(MAIN, "boot error");
    delay(10000);
    esp_restart();
  }
  // 初期化が完了したのでグローバルにセットする
  bp35a1 = &bp35a1_instance;
  //
  ESP_LOGD(MAIN, "setup success");

  //
  // ディスプレイ表示
  //
  M5.Lcd.fillScreen(BLACK);
  measurement_watt.update(std::nullopt);
  measurement_ampere.update(std::nullopt);
  measurement_cumlative_wh.update(std::nullopt);
}

//
// Arduinoのloop()関数
//
void loop() {
  //
  static std::array<char, 256> buffer{};
  static int remains = 0;
  // この関数の実行400回に1回メッセージを送るという意味
  constexpr int CYCLE{400};

  // プログレスバーを表示する
  {
    int bar_width = M5.Lcd.width() * (CYCLE - remains) / CYCLE;
    int y = M5.Lcd.height() - 2;
    M5.Lcd.fillRect(bar_width, y, M5.Lcd.width(), M5.Lcd.height(), BLACK);
    M5.Lcd.fillRect(0, y, bar_width, M5.Lcd.height(), YELLOW);
  }

  //
  // 定期メッセージ送信処理
  //
  if (remains == 0) {
    send_measurement_request(bp35a1);
  }
  remains = (remains + 1) % CYCLE;

  //
  // メッセージ受信処理
  //
  std::optional<Response> opt = bp35a1->watch_response();
  if (opt) {
    Response r = opt.value();
    ESP_LOGD(MAIN, "%s", r.show().c_str());
    if (r.tag == Response::Tag::EVENT) {
      int num = std::strtol(r.keyval["NUM"].c_str(), nullptr, 16);
      switch (num) {
      case 0x24: // EVENT 24 :
                 // PANAによる接続過程でエラーが発生した(接続が完了しなかった)
      {
        ESP_LOGD(MAIN, "reconnect");
        // 再接続を試みる
        M5.Lcd.fillScreen(BLACK);
        M5.Lcd.setCursor(0, 0);
        display_boot_message("reconnect");
        if (!bp35a1->connect(display_boot_message)) {
          display_boot_message("reconnect error, try to reboot");
          ESP_LOGD(MAIN, "reconnect error, try to reboot");
          delay(5000);
          esp_restart();
        }
        M5.Lcd.fillScreen(BLACK);
        measurement_watt.update(std::nullopt);
        measurement_ampere.update(std::nullopt);
        measurement_cumlative_wh.update(std::nullopt);
      } break;
      case 0x29: // ライフタイムが経過して期限切れになった
      {
        ESP_LOGD(MAIN, "session timeout occurred");
      } break;
      default:
        break;
      }
    } else if (r.tag == Response::Tag::ERXUDP) {
      // key-valueストアに入れるときにテキスト形式に変換してあるので元のバイナリに戻す
      std::size_t datalen =
          std::strtol(r.keyval["DATALEN"].data(), nullptr, 16);
      // テキスト形式
      std::string_view textformat = r.keyval["DATA"];
      // 変換後のバイナリ
      std::vector<uint8_t> binaryformat = Response::text_to_binary(textformat);
      // EchonetLiteFrameに変換
      EchonetLiteFrame *frame =
          reinterpret_cast<EchonetLiteFrame *>(binaryformat.data());
      ESP_LOGD(MAIN, "%s", SmartWhm::show(*frame).c_str());
      //
      if (NodeProfileClass::isNodeProfileClass({frame->edata.seoj[0],
                                                frame->edata.seoj[1],
                                                frame->edata.seoj[2]})) {
        switch (frame->edata.epc) {
        case 0xD5: // インスタンスリスト通知
        {
          if (frame->edata.pdc >= 4) { // 4バイト以上
            ESP_LOGD(MAIN, "instances list");
            uint8_t total_number_of_instances = frame->edata.edt[0];
            EchonetLiteObjectCode *p =
                reinterpret_cast<EchonetLiteObjectCode *>(&frame->edata.edt[1]);
            //
            ESP_LOGD(MAIN, "total number of instances: %d",
                     total_number_of_instances);
            std::string str;
            for (uint8_t i = 0; i < total_number_of_instances; ++i) {
              char buffer[10]{'\0'};
              std::sprintf(buffer, "%02X%02X%02X", p[i].class_group,
                           p[i].class_code, p[i].instance_code);
              str += std::string(buffer) + ",";
            }
            str.pop_back(); // 最後の,を削る
            ESP_LOGD(MAIN, "list of object code(EOJ): %s", str.c_str());
          }
          //
          // 通知されているのは自分自身だろうから
          // なにもしませんよ
          //
        } break;
        default:
          break;
        }
      } else if (SmartWhm::isSmartWhm({frame->edata.seoj[0],
                                       frame->edata.seoj[1],
                                       frame->edata.seoj[2]})) {
        // 低圧スマートメーターからやってきたメッセージだった
        switch (frame->edata.epc) {
        case 0xD3: // 係数
        {
          if (frame->edata.pdc == 0x04) { // 4バイト
            auto c = SmartWhm::Coefficient(
                {frame->edata.edt[0], frame->edata.edt[1], frame->edata.edt[2],
                 frame->edata.edt[3]});
            ESP_LOGD(MAIN, "%s", c.show().c_str());
            whm_coefficient = c;
          } else {
            // 係数が無い場合は１倍となる
            whm_coefficient = std::nullopt;
            ESP_LOGD(MAIN, "no coefficient");
          }
        } break;
        case 0xD7: // 積算電力量有効桁数
        {
          if (frame->edata.pdc == 0x01) { // 1バイト
            auto digits = SmartWhm::EffectiveDigits(frame->edata.edt[0]);
            ESP_LOGD(MAIN, "%s", digits.show().c_str());
          } else {
            ESP_LOGD(MAIN, "pdc is should be 1 bytes, this is %d bytes.",
                     frame->edata.pdc);
          }
        } break;
        case 0xE1: // 積算電力量単位 (正方向、逆方向計測値)
        {
          if (frame->edata.pdc == 0x01) { // 1バイト
            auto unit = SmartWhm::Unit(frame->edata.edt[0]);
            ESP_LOGD(MAIN, "%s", unit.show().c_str());
            whm_unit = unit;
          } else {
            ESP_LOGD(MAIN, "pdc is should be 1 bytes, this is %d bytes.",
                     frame->edata.pdc);
          }
        } break;
        case 0xE7: // 瞬時電力値 W
        {
          if (frame->edata.pdc == 0x04) { // 4バイト
            auto w = SmartWhm::Watt({frame->edata.edt[0], frame->edata.edt[1],
                                     frame->edata.edt[2], frame->edata.edt[3]});
            ESP_LOGD(MAIN, "%s", w.show().c_str());
            // 測定値を表示する
            measurement_watt.update(w);
          } else {
            ESP_LOGD(MAIN, "pdc is should be 4 bytes, this is %d bytes.",
                     frame->edata.pdc);
          }
        } break;
        case 0xE8: // 瞬時電流値
        {
          if (frame->edata.pdc == 0x04) { // 4バイト
            auto a =
                SmartWhm::Ampere({frame->edata.edt[0], frame->edata.edt[1],
                                  frame->edata.edt[2], frame->edata.edt[3]});
            ESP_LOGD(MAIN, "%s", a.show().c_str());
            // 測定値を表示する
            measurement_ampere.update(a);
          } else {
            ESP_LOGD(MAIN, "pdc is should be 4 bytes, this is %d bytes.",
                     frame->edata.pdc);
          }
        } break;
        case 0xEA: // 定時積算電力量
        {
          if (frame->edata.pdc == 0x0B) { // 11バイト
            // std::to_arrayの登場はC++20からなのでこんなことになった
            std::array<uint8_t, 11> memory;
            std::copy_n(frame->edata.edt, 11, memory.begin());
            //
            auto cwh = SmartWhm::CumulativeWattHour(memory);
            ESP_LOGD(MAIN, "%s", cwh.show().c_str());
            if (whm_coefficient.has_value()) {
              ESP_LOGD(MAIN, "coeff: %d", whm_coefficient.value().coefficient);
            } else {
              ESP_LOGD(MAIN, "coeff: %d", 1);
            }
            if (whm_unit.has_value()) {
              ESP_LOGD(MAIN, "unit: %s", whm_unit.value().show().c_str());
            }
            measurement_cumlative_wh.update(cwh);
          } else {
            ESP_LOGD(MAIN, "pdc is should be 11 bytes, this is %d bytes.",
                     frame->edata.pdc);
          }
        } break;
        default:
          break;
        }
      }
    }
  }
  //
  M5.update();
  delay(50);
}

//
// ディスプレイ表示用
//

// 瞬時電力量
static std::string to_str_watt(std::optional<SmartWhm::Watt> watt) {
  if (!watt.has_value()) {
    return std::string("---- W");
  }
  char buff[100]{'\0'};
  std::sprintf(buff, "%4d W", watt.value().watt);
  return std::string(buff);
};

// 瞬時電流
static std::string to_str_ampere(std::optional<SmartWhm::Ampere> ampere) {
  if (!ampere.has_value()) {
    return std::string("R:--.- A, T:--.- A");
  }
  uint16_t r_deciA = ampere.value().get_deciampere_R();
  uint16_t t_deciA = ampere.value().get_deciampere_T();
  // 整数部と小数部
  auto r = std::make_pair(r_deciA / 10, r_deciA * 10 % 10);
  auto t = std::make_pair(t_deciA / 10, t_deciA * 10 % 10);
  char buff[100]{'\0'};
  std::sprintf(buff, "R:%2d.%01d A, T:%2d.%01d A", r.first, r.second, t.first,
               t.second);
  return std::string(buff);
};

// 積算電力
static std::string
to_str_cumlative_wh(std::optional<SmartWhm::CumulativeWattHour> watt_hour) {
  if (!watt_hour) {
    return std::string("--:-- ----------    ");
  }
  auto wh = watt_hour.value();
  //
  std::string output = std::string{};
  // 時間
  {
    char buff[100]{'\0'};
    std::sprintf(buff, "%02d:%02d", wh.hour, wh.minutes);
    output += std::string(buff);
  }
  // 電力量
  uint32_t cwh = wh.cumlative_watt_hour;
  // 係数(無い場合の係数は1)
  if (whm_coefficient.has_value()) {
    cwh = cwh * whm_coefficient.value().coefficient;
  }
  // 単位
  if (whm_unit.has_value()) {
    // 文字にする
    std::string str_kwh = std::to_string(cwh);
    // 1kwhの位置に小数点を移動する
    int powers_of_10 = whm_unit.value().get_powers_of_10();
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
    // 小数点を入れたら出力バッファに追加する
    output += " " + str_kwh + " kwh";
  } else {
    // 単位がないならそのまま出す
    output += " " + std::to_string(cwh);
  }
  return output;
}
