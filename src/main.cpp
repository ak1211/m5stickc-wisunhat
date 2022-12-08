// Copyright (c) 2022 Akihiro Yamamoto.
// Licensed under the MIT License <https://spdx.org/licenses/MIT.html>
// See LICENSE file in the project root for full license information.
//
#include "Application.hpp"
#include "credentials.h"
#include <M5StickCPlus.h>
#undef min
#include "Bp35a1.hpp"
#include "Gauge.hpp"
#include "SmartWhm.hpp"
#include "Telemetry.hpp"
#include <ArduinoJson.h>
#include <WiFi.h>
#include <chrono>
#include <cstring>
#include <ctime>
#include <esp_sntp.h>
#include <map>
#include <optional>
#include <queue>
#include <string>
#include <vector>

// time zone = Asia_Tokyo(UTC+9)
static constexpr char TZ_TIME_ZONE[] = "JST-9";

// BP35A1と会話できるポート番号
constexpr int CommPortRx{26};
constexpr int CommPortTx{0};

// vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv
// グローバル変数

// BP35A1 初期化が完了するまでnull
Bp35a1 *bp35a1(nullptr);

// 瞬時電力量
static Gauge<SmartElectricEnergyMeter::InstantWatt> instant_watt_gauge{
    2, 4, YELLOW, std::make_pair(10, 10),
    [](std::optional<SmartElectricEnergyMeter::InstantWatt> iw) -> std::string {
      auto result = std::string{"---- W"};
      if (iw.has_value()) {
        char buff[100]{'\0'};
        std::sprintf(buff, "%4d W", iw.value().watt);
        result = std::string(buff);
      }
      return result;
    }};

// 瞬時電流
static Gauge<SmartElectricEnergyMeter::InstantAmpere> instant_ampere_gauge{
    1, 4, WHITE, std::make_pair(10, 10 + 48),
    [](std::optional<SmartElectricEnergyMeter::InstantAmpere> ia)
        -> std::string {
      //
      auto to_string = [](int32_t deci_ampere) -> std::string {
        auto i = deci_ampere / 10; // 整数部
        auto f = deci_ampere % 10; // 小数部
        char buff[50]{'\0'};
        std::sprintf(buff, "%2d.%01d", i, f);
        return std::string(buff);
      };
      //
      auto result = std::string{"R:--.- A, T:--.- A"};
      if (ia.has_value()) {
        auto r = to_string(ia.value().r_deciA);
        auto t = to_string(ia.value().t_deciA);
        result = std::string{};
        result += "R:" + r + " A, ";
        result += "T:" + t + " A";
      }
      return result;
    }};

// 積算電力量
static Gauge<SmartElectricEnergyMeter::CumulativeWattHour>
    cumulative_watt_hour_gauge{
        1, 4, WHITE, std::make_pair(10, 10 + 48 + 24),
        [](std::optional<SmartElectricEnergyMeter::CumulativeWattHour>
               watt_hour) -> std::string {
          std::string result = std::string{"--:-- ----------    "};
          if (watt_hour) {
            auto wh = watt_hour.value();
            //
            result = std::string{};
            // 時間
            {
              char buff[100]{'\0'};
              std::sprintf(buff, "%02d:%02d", wh.hour(), wh.minutes());
              result += std::string(buff);
            }
            // 電力量
            std::optional<std::string> opt = wh.to_string_kwh();
            if (opt.has_value()) {
              result += " " + opt.value() + " kwh";
            } else {
              // 単位がないならそのまま出す
              result += " " + std::to_string(wh.raw_cumlative_watt_hour());
            }
          }
          return result;
        }};

// グローバル変数
// ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

//
static bool connectToWiFi(std::size_t retry_count = 100) {
  if (WiFi.status() == WL_CONNECTED) {
    ESP_LOGD(MAIN, "WIFI connected, pass");
    return true;
  }
  ESP_LOGI(MAIN, "Connecting to WIFI SSID %s", WIFI_SSID.data());

  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID.data(), WIFI_PASSWORD.data());
  for (std::size_t retry = 0; retry < retry_count; ++retry) {
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
static bool initializeTime(std::size_t retry_count = 100) {
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
static bool establishConnection() {
  bool ok = connectToWiFi();
  ok = ok ? initializeTime() : ok;
  ok = ok ? connectToAwsIot() : ok;
  return ok;
}

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
  static Bp35a1 bp35a1_instance(Serial2, std::make_pair(BID, BPASSWORD));
  if (!bp35a1_instance.boot(display_boot_message)) {
    display_boot_message("smart meter connecion failed, bye");
    ESP_LOGD(MAIN, "smart meter connecion failed");
    delay(60e3);
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
  instant_watt_gauge.update(true);
  instant_ampere_gauge.update(true);
  cumulative_watt_hour_gauge.update(true);
}

//
// BP35A1から受信したイベントを処理する
//
static void process_event(const Bp35a1::Response &r) {
  switch (std::strtol(r.keyval.at("NUM").c_str(), nullptr, 16)) {
  case 0x21: // EVENT 21 :
             // UDP送信処理が完了した
    ESP_LOGD(MAIN, "UDP transmission successful.");
    break;
  case 0x24: // EVENT 24 :
             // PANAによる接続過程でエラーが発生した(接続が完了しなかった)
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
    instant_watt_gauge.set(std::nullopt).update();
    instant_ampere_gauge.set(std::nullopt).update();
    cumulative_watt_hour_gauge.set(std::nullopt).update();
    break;
  case 0x29: // ライフタイムが経過して期限切れになった
    ESP_LOGI(MAIN, "session timeout occurred");
    break;
  default:
    break;
  }
}

//
// ノードプロファイルクラスのEchonetLiteフレームを処理する
//
static void process_node_profile_class_frame(const EchonetLiteFrame &frame) {
  for (const auto &v : splitToEchonetLiteData(frame.edata)) {
    auto prop = reinterpret_cast<const EchonetLiteProp *>(v.data());
    switch (prop->epc) {
    case 0xD5:              // インスタンスリスト通知
      if (prop->pdc >= 4) { // 4バイト以上
        ESP_LOGD(MAIN, "instances list");
        uint8_t total_number_of_instances = prop->edt[0];
        const EchonetLiteObjectCode *begin =
            reinterpret_cast<const EchonetLiteObjectCode *>(&prop->edt[1]);
        const EchonetLiteObjectCode *end = begin + total_number_of_instances;
        std::string str;
        std::for_each(begin, end, [&str](const EchonetLiteObjectCode &eoj) {
          str += std::to_string(eoj) + ",";
        });
        str.pop_back(); // 最後の,を削る
        ESP_LOGD(MAIN, "list of object code(EOJ): %s", str.c_str());
      }
      //
      // 通知されているのは自分自身だろうから
      // なにもしませんよ
      //
      break;
    default:
      break;
    }
  }
}

// 要求時間からトランザクションIDへ変換する
template <class Rep, class Period>
static EchonetLiteTransactionId
time_to_transaction_id(std::chrono::duration<Rep, Period> epoch) {
  int32_t sec = std::chrono::duration_cast<std::chrono::seconds>(epoch).count();
  EchonetLiteTransactionId tid{};
  tid.u8[0] = static_cast<uint8_t>(sec / 3600 % 24);
  tid.u8[1] = static_cast<uint8_t>(sec / 60 % 60);
  return tid;
}

// トランザクションIDから要求時間へ変換する
static int32_t transaction_id_to_seconds(EchonetLiteTransactionId tid) {
  int32_t h = tid.u8[0];
  int32_t m = tid.u8[1];
  int32_t sec = h * 3600 + m * 60;
  return sec;
}

//
// BP35A1から受信したERXUDPイベントを処理する
//
static bool process_erxudp(const Bp35a1::Response &r,
                           SmartWhm &smart_watt_hour_meter,
                           std::queue<std::string> &to_sending_message_fifo) {
  //
  // key-valueストアに入れるときにテキスト形式に変換してあるので元のバイナリに戻す
  //
  // ペイロード(テキスト形式)
  std::string_view textformat = r.keyval.at("DATA");
  // ペイロード(バイナリ形式)
  std::vector<uint8_t> binaryformat = text_to_binary(textformat);
  // EchonetLiteFrameに変換
  EchonetLiteFrame *frame =
      reinterpret_cast<EchonetLiteFrame *>(binaryformat.data());
  // フレームヘッダの確認
  if (frame->ehd == EchonetLiteEHD) {
    //  EchonetLiteフレームだった
    ESP_LOGD(MAIN, "%s", std::to_string(*frame).c_str());
    //
    if (frame->edata.seoj == NodeProfileClass::EchonetLiteEOJ) {
      // ノードプロファイルクラス
      process_node_profile_class_frame(*frame);
    } else if (frame->edata.seoj == SmartElectricEnergyMeter::EchonetLiteEOJ) {
      // 低圧スマート電力量計クラス
      //
      if constexpr (false) {
        auto tsec = transaction_id_to_seconds(frame->tid);
        int32_t h = tsec / 3600 % 24;
        int32_t m = tsec / 60 % 60;
        ESP_LOGD(MAIN, "tid(H:M) is %02d:%02d", h, m);
        auto epoch = std::chrono::system_clock::now().time_since_epoch();
        auto seconds =
            std::chrono::duration_cast<std::chrono::seconds>(epoch).count();
        int32_t elapsed = seconds % (24 * 60 * 60) - tsec;
        ESP_LOGI(MAIN, "Response elasped time of seconds:%4d", elapsed);
      }
      //
      smart_watt_hour_meter.process_echonet_lite_frame(r.created_at, *frame,
                                                       to_sending_message_fifo);
    } else {
      ESP_LOGD(MAIN, "Unknown SEOJ: %s",
               std::to_string(frame->edata.seoj).c_str());
      return false;
    }
  } else {
    ESP_LOGD(MAIN, "unknown frame header. EHD: %s",
             std::to_string(frame->ehd).c_str());
    return false;
  }
  return true;
}

//
// プログレスバーを表示する
//
static void render_progress_bar(uint32_t permille) {
  int32_t bar_width = M5.Lcd.width() * permille / 1000;
  int32_t y = M5.Lcd.height() - 2;
  M5.Lcd.fillRect(bar_width, y, M5.Lcd.width(), M5.Lcd.height(), BLACK);
  M5.Lcd.fillRect(0, y, bar_width, M5.Lcd.height(), YELLOW);
}

// スマートメーターに最初の要求を出す
template <class Clock, class Duration>
static void
send_first_request(std::chrono::time_point<Clock, Duration> current_time) {
  std::vector<SmartElectricEnergyMeter::EchonetLiteEPC> epcs{};
  // 動作状態
  // 設置場所
  // 異常発生状態
  // メーカーコード
  // 係数
  // 積算電力量単位
  // 積算電力量有効桁数
  epcs = {
      SmartElectricEnergyMeter::EchonetLiteEPC::Operation_status,
      SmartElectricEnergyMeter::EchonetLiteEPC::Installation_location,
      SmartElectricEnergyMeter::EchonetLiteEPC::Fault_status,
      SmartElectricEnergyMeter::EchonetLiteEPC::Manufacturer_code,
      SmartElectricEnergyMeter::EchonetLiteEPC::Coefficient,
      SmartElectricEnergyMeter::EchonetLiteEPC::Unit_for_cumulative_amounts,
      SmartElectricEnergyMeter::EchonetLiteEPC::Number_of_effective_digits,
  };
  ESP_LOGD(MAIN, "%s",
           "request status / location / fault / manufacturer / coefficient / "
           "unit for whm / request number of "
           "effective digits");
  // スマートメーターに要求を出す
  const auto tid = time_to_transaction_id(current_time.time_since_epoch());

  if (bp35a1->send_request(tid, epcs)) {
    ESP_LOGV(MAIN, "request OK");
  } else {
    ESP_LOGD(MAIN, "request NG");
  }
}

// スマートメーターに定期的な要求を出す
template <class Clock, class Duration>
static void
send_periodical_request(std::chrono::time_point<Clock, Duration> current_time,
                        const SmartWhm &whm) {
  std::vector<SmartElectricEnergyMeter::EchonetLiteEPC> epcs{};
  // 瞬時電力要求
  // 瞬時電流要求
  epcs = {
      SmartElectricEnergyMeter::EchonetLiteEPC::Measured_instantaneous_power,
      SmartElectricEnergyMeter::EchonetLiteEPC::
          Measured_instantaneous_currents};
  ESP_LOGD(MAIN, "%s", "request inst-epower and inst-current");
  // 定時積算電力量計測値(正方向計測値)
  //
  std::time_t displayed_jst = [&whm]() -> std::time_t {
    auto &opt = whm.cumlative_watt_hour;
    return opt.has_value() ? opt.value().get_time_t().value_or(0) : 0;
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
        SmartElectricEnergyMeter::EchonetLiteEPC::
            Cumulative_amounts_of_electric_energy_measured_at_fixed_time);
    ESP_LOGD(MAIN, "request amounts of electric power");
  }
  // 積算履歴収集日
  if (!whm.day_for_which_the_historcal.has_value()) {
    epcs.push_back(SmartElectricEnergyMeter::EchonetLiteEPC::
                       Day_for_which_the_historcal_data_1);
    ESP_LOGD(MAIN, "day for historical data 1");
  }
  // スマートメーターに要求を出す
  const auto tid = time_to_transaction_id(current_time.time_since_epoch());
  if (bp35a1->send_request(tid, epcs)) {
    ESP_LOGV(MAIN, "request OK");
  } else {
    ESP_LOGD(MAIN, "request NG");
  }
}

//
// Arduinoのloop()関数
//
void loop() {
  // スマートメーター
  static SmartWhm smart_watt_hour_meter{};
  // メッセージ受信バッファ
  static std::queue<Bp35a1::Response> received_message_fifo{};
  // IoT Hub送信用バッファ
  static std::queue<std::string> to_sending_message_fifo{};
  using namespace std::chrono;
  // IoT Coreにメッセージを送信した時間
  static time_point time_of_sent_message_to_iot_core{system_clock::now()};
  // スマートメーターにメッセージを送信した時間
  static time_point time_of_sent_message_to_smart_whm{system_clock::now()};
  // スマートメーターからの応答
  static bool had_good_response_of_smart_whm{true};

  // 現在時刻
  time_point current_time_point = system_clock::now();

  //
  // (あれば)２５個連続でスマートメーターからのメッセージを受信する
  //
  for (std::size_t count = 0; count < 25; ++count) {
    std::optional<Bp35a1::Response> r = bp35a1->watch_response();
    if (r.has_value()) {
      received_message_fifo.push(r.value());
    }
    delay(100);
  }

  //
  // 送信するべき測定値があればIoT Coreへ送信する(2秒以上の間隔をあけて)
  //
  if (auto elapsed = current_time_point - time_of_sent_message_to_iot_core;
      !to_sending_message_fifo.empty() && elapsed >= seconds{2}) {
    if (sendTelemetry(to_sending_message_fifo.front())) {
      // IoT Coreへ送信したメッセージをFIFOから消す
      to_sending_message_fifo.pop();
    }
    // IoT Coreへの送信時間を記録する
    time_of_sent_message_to_iot_core = current_time_point;
  }

  //
  // スマートメーターからのメッセージ受信処理
  //
  if (!received_message_fifo.empty()) {
    Bp35a1::Response r = received_message_fifo.front();
    ESP_LOGD(MAIN, "%s", std::to_string(r).c_str());
    switch (r.tag) {
    case Bp35a1::Response::Tag::EVENT:
      // イベント受信処理
      process_event(r);
      break;
    case Bp35a1::Response::Tag::ERXUDP:
      // ERXUDPを処理する
      had_good_response_of_smart_whm =
          process_erxudp(r, smart_watt_hour_meter, to_sending_message_fifo);
      // 測定値をセットする
      instant_watt_gauge.set(smart_watt_hour_meter.instant_watt);
      instant_ampere_gauge.set(smart_watt_hour_meter.instant_ampere);
      cumulative_watt_hour_gauge.set(smart_watt_hour_meter.cumlative_watt_hour);
      break;
    default:
      break;
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
  loopTelemetry();

  //
  // プログレスバーを表示する
  //
  const milliseconds one_min_in_ms = milliseconds{60000};
  const milliseconds seconds_in_ms = duration_cast<milliseconds>(
      current_time_point.time_since_epoch() % one_min_in_ms);
  // 毎分0秒までの残り時間(1000分率)
  const uint32_t remains_in_permille =
      1000 * (one_min_in_ms - seconds_in_ms) / one_min_in_ms;
  render_progress_bar(remains_in_permille);

  //
  // スマートメーターに要求を出す(1秒以上の間隔をあけて)
  //
  if (auto elapsed = current_time_point - time_of_sent_message_to_smart_whm;
      elapsed >= seconds{1}) {
    // 積算電力量単位が初期値の場合にスマートメーターに最初の要求を出す
    if (!smart_watt_hour_meter.whm_unit.has_value()) {
      send_first_request(current_time_point);
      // 送信時間を記録する
      time_of_sent_message_to_smart_whm = current_time_point;
    } else if (!had_good_response_of_smart_whm || elapsed > seconds{60} ||
               duration_cast<seconds>(seconds_in_ms) == seconds{0}) {
      // 前回要求が失敗した。
      // または毎分０秒
      // または前回要求より６０秒超過したらスマートメーターに定期要求を出す
      send_periodical_request(current_time_point, smart_watt_hour_meter);
      // 送信時間を記録する
      time_of_sent_message_to_smart_whm = current_time_point;
    }
  }

  //
  // 45秒以上の待ち時間があるうちに接続状態の検査をする:
  //
  if (duration_cast<seconds>(seconds_in_ms) >= seconds{45}) {
    if (WiFi.isConnected()) {
      // MQTT接続検査
      checkTelemetry(seconds{10});
    } else {
      // WiFi接続検査
      checkWiFi(seconds{10});
    }
  }

  //
  // IoT Coreへの送信が20分以上されていない場合
  //
  if (auto elapsed = current_time_point - time_of_sent_message_to_iot_core;
      elapsed >= minutes{20}) {
    //
    if (WiFi.isConnected()) {
      // WiFiの接続に失敗しているので,システムリセットして復帰する
      ESP_LOGI(MAIN, "System reboot, Bye!");
      esp_restart();
    }
  }
}
