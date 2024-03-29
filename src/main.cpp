// Copyright (c) 2022 Akihiro Yamamoto.
// Licensed under the MIT License <https://spdx.org/licenses/MIT.html>
// See LICENSE file in the project root for full license information.
//
#include <M5StickCPlus.h>
#undef min
#include <ArduinoJson.h>
#include <WiFi.h>
#include <chrono>
#include <cstring>
#include <ctime>
#include <esp_sntp.h>
#include <iomanip>
#include <map>
#include <memory>
#include <optional>
#include <queue>
#include <sstream>
#include <string>
#include <variant>
#include <vector>

#include "Application.hpp"
#include "Bp35a1.hpp"
#include "EchonetLite.hpp"
#include "Gauge.hpp"
#include "Telemetry.hpp"
#include "credentials.h"

using namespace std::literals::string_view_literals;

struct SmartWhm {
  // BP35A1と会話できるポート
  Stream &commport;
  // スマート電力量計のＢルート識別子
  Bp35a1::SmartMeterIdentifier identifier;
  // 乗数(無い場合の乗数は1)
  std::optional<SmartElectricEnergyMeter::Coefficient> whm_coefficient;
  // 単位
  std::optional<SmartElectricEnergyMeter::Unit> whm_unit;
  // 積算履歴収集日
  std::optional<uint8_t> day_for_which_the_historcal;
  // 瞬時電力
  std::optional<Telemetry::PayloadInstantWatt> instant_watt;
  // 瞬時電流
  std::optional<Telemetry::PayloadInstantAmpere> instant_ampere;
  // 定時積算電力量
  std::optional<Telemetry::PayloadCumlativeWattHour> cumlative_watt_hour;
  //
  SmartWhm(Stream &comm, Bp35a1::SmartMeterIdentifier ident)
      : commport{comm}, identifier{ident} {}
};

// time zone = Asia_Tokyo(UTC+9)
constexpr char TZ_TIME_ZONE[] = "JST-9";

// BP35A1と会話できるポート番号
constexpr int CommPortRx{26};
constexpr int CommPortTx{0};

// vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv
// グローバル変数はじまり
//
// 接続相手のスマートメーター
static std::unique_ptr<SmartWhm> smart_watt_hour_meter;
// MQTT
static Telemetry::Mqtt telemetry;
// 瞬時電力量
static Gauge<Telemetry::PayloadInstantWatt> instant_watt_gauge{
    2,
    4,
    YELLOW,
    {10, 10},
    [](std::optional<Telemetry::PayloadInstantWatt> pIW) -> std::string {
      std::ostringstream oss;
      if (pIW.has_value()) {
        auto [unused, iw] = pIW.value();
        oss << std::setfill(' ') << std::setw(5) << iw.watt.count();
      } else {
        oss << std::setfill('-') << std::setw(5) << "";
      }
      oss << " W"sv;
      return oss.str();
    }};
// 瞬時電流
static Gauge<Telemetry::PayloadInstantAmpere> instant_ampere_gauge{
    1,
    4,
    WHITE,
    {10, 10 + 48},
    [](std::optional<Telemetry::PayloadInstantAmpere> pIA) -> std::string {
      using namespace SmartElectricEnergyMeter;
      auto map_deciA = [](std::optional<DeciAmpere> dA) -> std::string {
        std::ostringstream os;
        if (dA.has_value()) {
          int32_t i = dA->count() / 10; // 整数部
          int32_t f = dA->count() % 10; // 小数部
          os << std::setfill(' ')       //
             << std::setw(3) << i << "." << std::setw(1) << f;
        } else {
          char c[] = "";
          os << std::setfill('-') //
             << std::setw(3) << c << "." << std::setw(1) << c;
        }
        return os.str();
      };
      std::ostringstream oss;
      std::string r{};
      std::string t{};
      if (pIA.has_value()) {
        auto [unused, ia] = pIA.value();
        r = map_deciA(ia.ampereR);
        t = map_deciA(ia.ampereT);
      } else {
        r = t = map_deciA(std::nullopt);
      }
      oss << "R:" << r << " A, T:" << t << " A";
      return oss.str();
    }};
// 積算電力量
static Gauge<Telemetry::PayloadCumlativeWattHour> cumulative_watt_hour_gauge{
    1,
    4,
    WHITE,
    {10, 10 + 48 + 24},
    [](std::optional<Telemetry::PayloadCumlativeWattHour> pCWH) -> std::string {
      auto map_hour_min =
          [](std::optional<std::pair<int, int>> hour_min) -> std::string {
        std::ostringstream os;
        if (hour_min.has_value()) {
          auto [h, m] = hour_min.value();
          os << std::setfill(' ') //
             << std::setw(2) << h //
             << ":";
          os << std::setfill('0') //
             << std::setw(2) << m;
        } else {
          char c[] = "";
          os << std::setfill('-') //
             << std::setw(2) << c //
             << ":";
          os << std::setfill('-') //
             << std::setw(2) << c;
        }
        return os.str();
      };
      auto map_cwh =
          [](auto digit_width, auto unit_width,
             std::optional<SmartElectricEnergyMeter::CumulativeWattHour>
                 opt_cwh) -> std::string {
        std::ostringstream os;
        std::string str_kwh{};
        std::string str_unit{};
        if (opt_cwh.has_value()) {
          auto cwh = opt_cwh.value();
          if (smart_watt_hour_meter->whm_unit.has_value()) {
            auto unit = smart_watt_hour_meter->whm_unit.value();
            str_kwh = to_string_cumlative_kilo_watt_hour(
                cwh, smart_watt_hour_meter->whm_coefficient, unit);
            str_unit = "kwh";
          } else {
            // kwh単位にできなかったら,受け取ったそのままの値を出す
            str_kwh = std::to_string(cwh.raw_cumlative_watt_hour());
            str_unit = "";
          }
          os << std::setfill(' ')                 //
             << std::setw(digit_width) << str_kwh //
             << " "                               //
             << std::setw(unit_width) << str_unit;
        } else {
          char c[] = "";
          os << std::setfill('-')           //
             << std::setw(digit_width) << c //
             << " "                         //
             << std::setw(unit_width) << c;
        }
        return os.str();
      };
      std::string str_hm{};
      std::string str_cwh{};
      if (pCWH.has_value()) {
        auto [cwh, coeff, unit] = pCWH.value();
        str_hm = map_hour_min(std::make_pair(cwh.hour(), cwh.minutes()));
        str_cwh = map_cwh(10, 3, cwh);
      } else {
        str_hm = map_hour_min(std::nullopt);
        str_cwh = map_cwh(10, 3, std::nullopt);
      }
      std::ostringstream oss;
      oss << str_hm << " " << str_cwh;
      return oss.str();
    }};
//
// グローバル変数おわり
// ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

//
// WiFi APへ接続確立を試みる
//
static bool connectToWiFi(std::size_t retry_count = 100) {
  if (WiFi.status() == WL_CONNECTED) {
    ESP_LOGD(MAIN, "WIFI connected, pass");
    return true;
  }
  ESP_LOGI(MAIN, "Connecting to WIFI SSID %s", WIFI_SSID.data());

  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID.data(), WIFI_PASSWORD.data());
  for (auto retry = 0; retry < retry_count; ++retry) {
    if (WiFi.status() == WL_CONNECTED) {
      break;
    }
    delay(1000);
    Serial.print(".");
  }

  Serial.println("");

  if (WiFi.status() == WL_CONNECTED) {
    ESP_LOGI(MAIN, "WiFi connected, IP address: %s",
             WiFi.localIP().toString().c_str());
    return true;
  } else {
    return false;
  }
}

//
// NTPと同期する
//
static bool initializeTime(std::size_t retry_count = 300) {
  sntp_sync_status_t status = sntp_get_sync_status();
  //
  if (status == SNTP_SYNC_STATUS_COMPLETED) {
    ESP_LOGI(MAIN, "SNTP synced, pass");
    return true;
  }
  ESP_LOGI(MAIN, "Setting time using SNTP");

  configTzTime(TZ_TIME_ZONE, "ntp.jst.mfeed.ad.jp", "time.cloudflare.com",
               "ntp.nict.jp");
  //
  for (auto retry = 0; retry < retry_count; ++retry) {
    status = sntp_get_sync_status();
    if (status == SNTP_SYNC_STATUS_COMPLETED) {
      break;
    } else {
      delay(1000);
      Serial.print(".");
    }
  }

  Serial.println("");

  if (status == SNTP_SYNC_STATUS_COMPLETED) {
    char buf[50];
    std::time_t now = std::time(nullptr);
    ESP_LOGI(MAIN, "local time: \"%s\"", asctime_r(std::localtime(&now), buf));
    ESP_LOGI(MAIN, "Time initialized!");
    return true;
  } else {
    ESP_LOGE(MAIN, "SNTP sync failed");
    return false;
  }
}

//
// AWS IoTへ接続確立を試みる
//
static bool establishConnection() {
  bool ok = connectToWiFi();
  ok = ok ? initializeTime() : ok;
  ok = ok ? telemetry.connectToAwsIot(std::chrono::seconds{60}) : ok;
  return ok;
}

//
// WiFi接続検査
//
static bool checkWiFi(std::chrono::seconds timeout) {
  using namespace std::chrono;
  time_point tp = system_clock::now() + timeout;
  do {
    if (WiFi.isConnected()) {
      return true;
    }
    // WiFi再接続シーケンス
    ESP_LOGI(MAIN, "WiFi reconnect");
    WiFi.reconnect();
    delay(10);
  } while (system_clock::now() < tp);
  return WiFi.isConnected();
}

//
// bootメッセージ表示用
//
static void display_boot_message(const char *s) { M5.Lcd.print(s); }

//
// Arduinoのsetup()関数
//
void setup() {
  M5.begin(true, true, true);
  M5.Lcd.setRotation(3);
  M5.Lcd.setTextSize(2);
  delay(2000);
  //
  Serial2.begin(115200, SERIAL_8N1, CommPortRx, CommPortTx);
  //
  display_boot_message("connect to IoT Hub\n");
  if (!establishConnection()) {
    display_boot_message("can't connect to IotHub, bye\n");
    ESP_LOGD(MAIN, "can't connect to IotHub");
    delay(60e3);
    esp_restart();
  }
  M5.Lcd.fillScreen(BLACK);
  M5.Lcd.setCursor(0, 0);
  //

  if (auto ident = Bp35a1::startup_and_find_meter(Serial2, {BID, BPASSWORD},
                                                  display_boot_message)) {
    //
    smart_watt_hour_meter =
        std::make_unique<SmartWhm>(SmartWhm(Serial2, ident.value()));
    // 見つかったスマートメーターに接続要求を送る
    if (!connect(smart_watt_hour_meter->commport,
                 smart_watt_hour_meter->identifier, display_boot_message)) {
      // 接続失敗
      display_boot_message("smart meter connecion failed, bye");
      ESP_LOGD(MAIN, "smart meter connecion failed");
      delay(60e3);
      esp_restart();
    }
    // 接続成功
    ESP_LOGD(MAIN, "connection successful");
  } else {
    // スマートメーターが見つからなかった
    display_boot_message("smart meter connecion failed, bye");
    ESP_LOGD(MAIN, "smart meter connecion failed");
    delay(60e3);
    esp_restart();
  }
  //
  ESP_LOGD(MAIN, "setup success");

  //
  // ディスプレイ表示
  //
  M5.Lcd.fillScreen(BLACK);
  instant_watt_gauge.update(true);
  instant_ampere_gauge.update(true);
  cumulative_watt_hour_gauge.update(true);
}

//
// BP35A1から受信したイベントを処理する
//
static void process_event(const Bp35a1::ResEvent &ev) {
  switch (ev.num.u8) {
  case 0x01: // EVENT 1 :
             // NSを受信した
    ESP_LOGI(MAIN, "Received NS");
    break;
  case 0x02: // EVENT 2 :
             // NAを受信した
    ESP_LOGI(MAIN, "Received NA");
    break;
  case 0x05: // EVENT 5 :
             // Echo Requestを受信した
    ESP_LOGI(MAIN, "Received Echo Request");
    break;
  case 0x1F: // EVENT 1F :
             // EDスキャンが完了した
    ESP_LOGI(MAIN, "Complete ED Scan.");
    break;
  case 0x20: // EVENT 20 :
             // BeaconRequestを受信した
    ESP_LOGI(MAIN, "Received BeaconRequest");
    break;
  case 0x21: // EVENT 21 :
             // UDP送信処理が完了した
    ESP_LOGD(MAIN, "UDP transmission successful.");
    break;
  case 0x24: // EVENT 24 :
             // PANAによる接続過程でエラーが発生した(接続が完了しなかった)
    ESP_LOGD(MAIN, "PANA reconnect");
    // 再接続を試みる
    M5.Lcd.fillScreen(BLACK);
    M5.Lcd.setCursor(0, 0);
    display_boot_message("reconnect");
    if (!connect(smart_watt_hour_meter->commport,
                 smart_watt_hour_meter->identifier, display_boot_message)) {
      display_boot_message("reconnect error, try to reboot");
      ESP_LOGD(MAIN, "reconnect error, try to reboot");
      delay(5000);
      esp_restart();
    }
    M5.Lcd.fillScreen(BLACK);
    instant_watt_gauge.set(std::nullopt).update();
    instant_ampere_gauge.set(std::nullopt).update();
    cumulative_watt_hour_gauge.set(std::nullopt).update();
    break;
  case 0x25: // EVENT 25 :
             // PANAによる接続が完了した
    ESP_LOGD(MAIN, "PANA session connected");
    break;
  case 0x26: // EVENT 26 :
             // 接続相手からセッション終了要求を受信した
    ESP_LOGD(MAIN, "session terminate request");
    break;
  case 0x27: // EVENT 27 :
             // PANAセッションの終了に成功した
    ESP_LOGD(MAIN, "PANA session terminate");
    break;
  case 0x28: // EVENT 28 :
             // PANAセッションの終了要求に対する応答がなくタイムアウトした(セッションは終了)
    ESP_LOGD(MAIN, "PANA session terminate. reason: timeout");
    break;
  case 0x29: // PANAセッションのライフタイムが経過して期限切れになった
    ESP_LOGI(MAIN, "PANA session expired");
    break;
  case 0x32: // ARIB108の送信緩和時間の制限が発動した
    ESP_LOGI(MAIN, "");
    break;
  case 0x33: // ARIB108の送信緩和時間の制限が解除された
    ESP_LOGI(MAIN, "");
    break;
  default:
    break;
  }
}

//
// ノードプロファイルクラスのEchonetLiteフレームを処理する
//
static void process_node_profile_class_frame(const EchonetLiteFrame &frame) {
  for (const EchonetLiteProp &prop : frame.edata.props) {
    switch (prop.epc) {
    case 0xD5:                    // インスタンスリスト通知
      if (prop.edt.size() >= 4) { // 4バイト以上
        std::ostringstream oss;
        auto it = prop.edt.cbegin();
        uint8_t total_number_of_instances = *it++;
        while (std::distance(it, prop.edt.cend()) >= 3) {
          auto obj = EchonetLiteObjectCode({*it++, *it++, *it++});
          oss << obj << ",";
        }
        ESP_LOGD(MAIN, "list of instances (EOJ): %s", oss.str().c_str());
      }
      //
      // 通知されているのは自分自身だろうから
      // なにもしませんよ
      //
      break;
    default:
      ESP_LOGD(MAIN, "unknown EPC: %02X", prop.epc);
      break;
    }
  }
}

//
// 要求時間からトランザクションIDへ変換する
//
static EchonetLiteTransactionId
time_to_transaction_id(std::chrono::system_clock::time_point tp) {
  using namespace std::chrono;
  auto epoch = tp.time_since_epoch();
  int32_t sec = duration_cast<seconds>(epoch).count();
  EchonetLiteTransactionId tid{};
  tid.u8[0] = static_cast<uint8_t>(sec / 3600 % 24);
  tid.u8[1] = static_cast<uint8_t>(sec / 60 % 60);
  return tid;
}

//
// トランザクションIDから要求時間へ変換する
//
static int32_t transaction_id_to_seconds(EchonetLiteTransactionId tid) {
  int32_t h = tid.u8[0];
  int32_t m = tid.u8[1];
  int32_t sec = h * 3600 + m * 60;
  return sec;
}

//
// BP35A1から受信したERXUDPイベントを処理する
//
static void process_erxudp(std::chrono::system_clock::time_point at,
                           const Bp35a1::ResErxudp &ev) {
  // EchonetLiteFrameに変換
  if (auto opt = deserializeToEchonetLiteFrame(ev.data)) {
    const EchonetLiteFrame &frame = opt.value();
    //  EchonetLiteフレームだった
    ESP_LOGD(MAIN, "%s", to_string(frame).c_str());
    //
    if (frame.edata.seoj.s == NodeProfileClass::EchonetLiteEOJ) {
      // ノードプロファイルクラス
      process_node_profile_class_frame(frame);
    } else if (frame.edata.seoj.s == SmartElectricEnergyMeter::EchonetLiteEOJ) {
      // 低圧スマート電力量計クラス
      if constexpr (false) {
        auto tsec = transaction_id_to_seconds(frame.tid);
        int32_t h = tsec / 3600 % 24;
        int32_t m = tsec / 60 % 60;
        ESP_LOGD(MAIN, "tid(H:M) is %02d:%02d", h, m);
        auto epoch = std::chrono::system_clock::now().time_since_epoch();
        auto seconds =
            std::chrono::duration_cast<std::chrono::seconds>(epoch).count();
        int32_t elapsed = seconds % (24 * 60 * 60) - tsec;
        ESP_LOGI(MAIN, "Response elasped time of seconds:%4d", elapsed);
      }
      namespace M = SmartElectricEnergyMeter;
      for (auto rx : M::process_echonet_lite_frame(frame)) {
        if (auto *p = std::get_if<M::Coefficient>(&rx)) {
          smart_watt_hour_meter->whm_coefficient = *p;
        } else if (std::get_if<M::EffectiveDigits>(&rx)) {
          // no operation
        } else if (auto *p = std::get_if<M::Unit>(&rx)) {
          smart_watt_hour_meter->whm_unit = *p;
        } else if (auto *p = std::get_if<M::InstantAmpere>(&rx)) {
          smart_watt_hour_meter->instant_ampere = std::make_pair(at, *p);
          // 送信バッファへ追加する
          telemetry.push_queue(std::make_pair(at, *p));
        } else if (auto *p = std::get_if<M::InstantWatt>(&rx)) {
          smart_watt_hour_meter->instant_watt = std::make_pair(at, *p);
          // 送信バッファへ追加する
          telemetry.push_queue(std::make_pair(at, *p));
        } else if (auto *p = std::get_if<M::CumulativeWattHour>(&rx)) {
          if (auto unit = smart_watt_hour_meter->whm_unit) {
            auto coeff = smart_watt_hour_meter->whm_coefficient.value_or(
                M::Coefficient{});
            smart_watt_hour_meter->cumlative_watt_hour =
                std::make_tuple(*p, coeff, *unit);
            // 送信バッファへ追加する
            telemetry.push_queue(std::make_tuple(*p, coeff, *unit));
          }
        }
      }
    }
  }
}

//
// スマートメーターに最初の要求を出す
//
static void
send_first_request(std::chrono::system_clock::time_point current_time) {
  using E = SmartElectricEnergyMeter::EchonetLiteEPC;
  std::vector<E> epcs = {
      E::Operation_status,            // 動作状態
      E::Installation_location,       // 設置場所
      E::Fault_status,                // 異常発生状態
      E::Manufacturer_code,           // メーカーコード
      E::Coefficient,                 // 係数
      E::Unit_for_cumulative_amounts, // 積算電力量単位
      E::Number_of_effective_digits,  // 積算電力量有効桁数
  };
  ESP_LOGD(MAIN,
           "request status / location / fault / manufacturer / coefficient / "
           "unit for whm / request number of "
           "effective digits");
  // スマートメーターに要求を出す
  const auto tid = time_to_transaction_id(current_time);

  Bp35a1::send_request(smart_watt_hour_meter->commport,
                       smart_watt_hour_meter->identifier, tid, epcs);
}

//
// スマートメーターに定期的な要求を出す
//
static void
send_periodical_request(std::chrono::system_clock::time_point current_time) {
  using E = SmartElectricEnergyMeter::EchonetLiteEPC;
  std::vector<E> epcs = {
      E::Measured_instantaneous_power,    // 瞬時電力要求
      E::Measured_instantaneous_currents, // 瞬時電流要求
  };
  ESP_LOGD(MAIN, "request inst-epower and inst-current");
  //
  std::time_t displayed_jst = []() -> std::time_t {
    if (smart_watt_hour_meter->cumlative_watt_hour.has_value()) {
      auto [cwh, unuse, unused] =
          smart_watt_hour_meter->cumlative_watt_hour.value();
      return cwh.get_time_t().value_or(0);
    } else {
      return 0;
    }
  }();
  if constexpr (false) {
    char buf[50]{'\0'};
    auto tm = std::chrono::system_clock::to_time_t(current_time);
    ESP_LOGI(MAIN, "current time:%s", ctime_r(&tm, buf));
    ESP_LOGI(MAIN, "displayed time:%s", ctime_r(&displayed_jst, buf));
  }
  auto measured_at = std::chrono::system_clock::from_time_t(displayed_jst);
  if (auto elapsed = current_time - measured_at;
      elapsed >= std::chrono::minutes{36}) {
    // 表示中の定時積算電力量計測値が36分より古い場合は
    // 定時積算電力量計測値(30分値)をつかみそこねたと判断して
    // 定時積算電力量要求を出す
    epcs.push_back(
        E::Cumulative_amounts_of_electric_energy_measured_at_fixed_time);
    // 定時積算電力量計測値(正方向計測値)
    ESP_LOGD(MAIN, "request amounts of electric power");
  }
#if 0
// 積算履歴収集日
  if (!whm.day_for_which_the_historcal.has_value()) {
    epcs.push_back(E::Day_for_which_the_historcal_data_1);
    ESP_LOGD(MAIN, "request day for historical data 1");
  }
#endif
  // スマートメーターに要求を出す
  const auto tid = time_to_transaction_id(current_time);
  Bp35a1::send_request(smart_watt_hour_meter->commport,
                       smart_watt_hour_meter->identifier, tid, epcs);
}

//
// スマートメーターに要求を送る
//
static void send_request_to_smart_meter() {
  using namespace std::chrono;
  static system_clock::time_point send_time_at;
  auto nowtp = system_clock::now();
  //
  constexpr auto INTERVAL = 15;
  if (auto elapsed = nowtp - send_time_at; elapsed < seconds{INTERVAL}) {
    return;
  }
  auto sec = duration_cast<seconds>(nowtp.time_since_epoch()).count() % 60;
  //
  // 積算電力量単位が初期値の場合にスマートメーターに最初の要求を出す
  //
  if (!smart_watt_hour_meter->whm_unit.has_value()) {
    send_first_request(nowtp);
    // 送信時間を記録する
    send_time_at = nowtp;
  }
  //
  // １分毎にスマートメーターに定期要求を出す
  //
  else if (sec < INTERVAL) {
    send_periodical_request(nowtp);
    // 送信時間を記録する
    send_time_at = nowtp;
  }
}

//
// 高速度loop()関数
//
inline void high_speed_loop(std::chrono::system_clock::time_point nowtp) {
  // メッセージ受信バッファ
  static std::queue<
      std::pair<std::chrono::system_clock::time_point, Bp35a1::Response>>
      received_message_fifo{};

  //
  // (あれば)連続でスマートメーターからのメッセージを受信する
  //
  for (auto count = 0; count < 25; ++count) {
    if (auto resp = Bp35a1::receive_response(smart_watt_hour_meter->commport)) {
      received_message_fifo.push({nowtp, resp.value()});
    }
    delay(10);
  }

  //
  // スマートメーターからのメッセージ受信処理
  //
  if (!received_message_fifo.empty()) {
    auto [time_at, resp] = received_message_fifo.front();
    std::visit(
        [](const auto &x) { ESP_LOGD(MAIN, "%s", to_string(x).c_str()); },
        resp);
    if (auto *pevent = std::get_if<Bp35a1::ResEvent>(&resp)) {
      // イベント受信処理
      process_event(*pevent);
    } else if (auto *perxudp = std::get_if<Bp35a1::ResErxudp>(&resp)) {
      // ERXUDPを処理する
      process_erxudp(time_at, *perxudp);
      // 測定値をセットする
      instant_watt_gauge.set(smart_watt_hour_meter->instant_watt);
      instant_ampere_gauge.set(smart_watt_hour_meter->instant_ampere);
      cumulative_watt_hour_gauge.set(
          smart_watt_hour_meter->cumlative_watt_hour);
    }
    // 処理したメッセージをFIFOから消す
    received_message_fifo.pop();
  }

  //
  // 測定値を更新する
  //
  instant_watt_gauge.update();
  instant_ampere_gauge.update();
  cumulative_watt_hour_gauge.update();
  //
  M5.update();
}

//
// 低速度loop()関数
//
inline void low_speed_loop(std::chrono::system_clock::time_point nowtp) {
  using namespace std::chrono;

  // MQTT送受信
  telemetry.loop_mqtt();

  // スマートメーターに要求を送る
  send_request_to_smart_meter();

  // プログレスバーを表示する
  uint16_t remain_sec =
      60 - duration_cast<seconds>(nowtp.time_since_epoch()).count() % 60;
  int32_t bar_width = M5.Lcd.width() * remain_sec / 60;
  int32_t y = M5.Lcd.height() - 2;
  M5.Lcd.fillRect(bar_width, y, M5.Lcd.width(), M5.Lcd.height(), BLACK);
  M5.Lcd.fillRect(0, y, bar_width, M5.Lcd.height(), YELLOW);

  // 30秒以上の待ち時間があるうちに接続状態の検査をする:
  if (remain_sec >= 30) {
    if (WiFi.isConnected()) {
      // MQTT接続検査
      telemetry.check_mqtt(seconds{10});
    } else {
      // WiFi接続検査
      checkWiFi(seconds{10});
    }
  }
}

//
// Arduinoのloop()関数
//
void loop() {
  using namespace std::chrono;
  static system_clock::time_point before;
  auto nowtp = system_clock::now();
  //
  high_speed_loop(nowtp);
  //
  if (nowtp - before >= 1s) {
    low_speed_loop(nowtp);
    before = nowtp;
  }
}
