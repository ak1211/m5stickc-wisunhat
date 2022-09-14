// Copyright (c) 2022 Akihiro Yamamoto.
// Licensed under the MIT License <https://spdx.org/licenses/MIT.html>
// See LICENSE file in the project root for full license information.
//
#include "Application.hpp"
#include "credentials.h"
#include <M5StickCPlus.h>
#undef min
#include "Bp35a1.hpp"
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

//
// 測定値表示
//
template <class T> class MeasurementDisplay {
  // 表示用の変換関数
  typedef std::string (*ConvertFn)(std::optional<T>);
  //
  int size;
  int font;
  int color;
  int cursor_x;
  int cursor_y;
  ConvertFn to_string;
  std::optional<T> current_value; // 現在表示中の値
  std::optional<T> next_value;    // 次回に表示する値

public:
  MeasurementDisplay(int text_size, int font, int text_color,
                     std::pair<int, int> cursor_xy, ConvertFn to_string_fn)
      : size{text_size},
        font{font},
        color{text_color},
        cursor_x{cursor_xy.first},
        cursor_y{cursor_xy.second},
        to_string{to_string_fn},
        current_value{std::nullopt},
        next_value{std::nullopt} {}
  //
  MeasurementDisplay &set(std::optional<T> next) {
    current_value = next_value;
    next_value = next;
    return *this;
  }
  //
  MeasurementDisplay &update(bool forced_repaint = false) {
    // 値に変化がある場合のみ更新する
    bool work_on = true;
    //
    auto a = current_value.has_value();
    auto b = next_value.has_value();
    //
    if (a == true && b == true) { // 双方の値がある
      auto cur = current_value.value();
      auto next = next_value.value();
      if (cur.equal_value(next)) {
        // 値に変化がないので何もしない
        work_on = false;
      } else {
        // 値に変化があるので更新する
        work_on = true;
      }
    } else if (a == false && b == false) { // 双方の値がない
      // 値に変化がないので何もしない
      work_on = false;
    } else { // 片方のみ値がある
      // 値に変化があるので更新する
      work_on = true;
    }
    //
    if (forced_repaint || work_on) {
      //
      std::string current = to_string(current_value);
      std::string next = to_string(next_value);
      //
      M5.Lcd.setTextSize(size);
      // 黒色で現在表示中の文字を上書きする
      M5.Lcd.setTextColor(BLACK);
      M5.Lcd.setCursor(cursor_x, cursor_y, font);
      M5.Lcd.print(current.c_str());
      //
      // 現在値を表示する
      M5.Lcd.setTextColor(color);
      M5.Lcd.setCursor(cursor_x, cursor_y, font);
      M5.Lcd.print(next.c_str());
      // 更新
      current_value = next_value;
    }
    return *this;
  }
};

// 前方参照
static std::string to_str_watt(std::optional<SmartWhm::InstantWatt>);
static std::string to_str_ampere(std::optional<SmartWhm::InstantAmpere>);
static std::string
    to_str_cumlative_wh(std::optional<SmartWhm::CumulativeWattHour>);

// vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv
// グローバル変数
//
// BP35A1 初期化が完了するまでnull
Bp35a1 *bp35a1(nullptr);
// 測定量表示用
static MeasurementDisplay<SmartWhm::InstantWatt> measurement_watt{
    2, 4, YELLOW, std::make_pair(10, 10), to_str_watt};
static MeasurementDisplay<SmartWhm::InstantAmpere> measurement_ampere{
    1, 4, WHITE, std::make_pair(10, 10 + 48), to_str_ampere};
static MeasurementDisplay<SmartWhm::CumulativeWattHour>
    measurement_cumlative_wh{1, 4, WHITE, std::make_pair(10, 10 + 48 + 24),
                             to_str_cumlative_wh};
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
    delay(500);
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
  ESP_LOGI(MAIN, "Setting time using SNTP");

  configTzTime(TZ_TIME_ZONE, "ntp.nict.jp", "time.google.com",
               "ntp.jst.mfeed.ad.jp");
  //
  for (std::size_t retry = 0; retry < retry_count; ++retry) {
    delay(500);
    if (sntp_get_sync_status() == SNTP_SYNC_STATUS_COMPLETED) {
      char buf[50];
      time_t now = time(nullptr);
      ESP_LOGI(MAIN, "local time: \"%s\"", asctime_r(localtime(&now), buf));
      ESP_LOGI(MAIN, "Time initialized!");
      return true;
    }
  }
  //
  ESP_LOGE(MAIN, "SNTP sync failed");
  return false;
}

//
static bool establishConnection() {
  connectToWiFi();
  bool ok = true;
  ok = ok ? initializeTime() : ok;
  ok = ok ? connectToAwsIot() : ok;
  return ok;
}

//
static bool checkWiFi(std::size_t retry_count = 100) {
  constexpr std::clock_t INTERVAL = 10 * CLOCKS_PER_SEC;
  static std::clock_t previous = 0L;
  std::clock_t current = std::clock();

  if (current - previous < INTERVAL) {
    return true;
  }

  for (std::size_t retry = 0; retry < retry_count; ++retry) {
    if (WiFi.status() == WL_CONNECTED) {
      break;
    } else {
      ESP_LOGI(MAIN, "WiFi reconnect");
      WiFi.disconnect(true);
      WiFi.reconnect();
      establishConnection();
    }
  }
  previous = current;

  if (WiFi.status() != WL_CONNECTED) {
    return false;
  } else {
    return true;
  }
}

//
//
//
static void check_MQTT_connection_task(void *) {
  while (1) {
    delay(1000);
    // WiFi接続検査
    if (checkWiFi() == false) {
      // WiFiの接続に失敗しているのでシステムリセットして復帰する
      esp_restart();
    }
    // MQTT接続検査
    checkTelemetry();
  }
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
    delay(10000);
    esp_restart();
  }
  M5.Lcd.fillScreen(BLACK);
  M5.Lcd.setCursor(0, 0);
  //
  static Bp35a1 bp35a1_instance(Serial2, std::make_pair(BID, BPASSWORD));
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
  measurement_watt.update(true);
  measurement_ampere.update(true);
  measurement_cumlative_wh.update(true);

  //
  // FreeRTOSタスク起動
  //
  xTaskCreatePinnedToCore(check_MQTT_connection_task, "checkMQTTtask", 8192,
                          nullptr, 10, nullptr, ARDUINO_RUNNING_CORE);
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
    measurement_watt.set(std::nullopt).update();
    measurement_ampere.set(std::nullopt).update();
    measurement_cumlative_wh.set(std::nullopt).update();
    break;
  case 0x29: // ライフタイムが経過して期限切れになった
    ESP_LOGD(MAIN, "session timeout occurred");
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
        const EchonetLiteObjectCode *p =
            reinterpret_cast<const EchonetLiteObjectCode *>(&prop->edt[1]);
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
      break;
    default:
      break;
    }
  }
}

//
// BP35A1から受信したERXUDPイベントを処理する
//
static void process_erxudp(const Bp35a1::Response &r,
                           SmartWhm &smart_watt_hour_meter,
                           std::queue<std::string> &to_sending_message_fifo) {
  //
  // key-valueストアに入れるときにテキスト形式に変換してあるので元のバイナリに戻す
  //
  // ペイロード(テキスト形式)
  std::string_view textformat = r.keyval.at("DATA");
  // ペイロード(バイナリ形式)
  std::vector<uint8_t> binaryformat =
      Bp35a1::Response::text_to_binary(textformat);
  // EchonetLiteFrameに変換
  EchonetLiteFrame *frame =
      reinterpret_cast<EchonetLiteFrame *>(binaryformat.data());
  // フレームヘッダの確認
  if (frame->ehd1 == EchonetLiteEHD1 && frame->ehd2 == EchonetLiteEHD2) {
    // EchonetLiteフレームだった
    ESP_LOGD(MAIN, "%s", SmartWhm::show(*frame).c_str());
    //
    auto const seoj = std::array<uint8_t, 3>{
        frame->edata.seoj[0], frame->edata.seoj[1], frame->edata.seoj[2]};
    if (seoj == NodeProfileClass::EchonetLiteEOJ()) {
      // ノードプロファイルクラス
      process_node_profile_class_frame(*frame);
    } else if (seoj == SmartWhm::EchonetLiteEOJ()) {
      // 低圧スマート電力量計クラス
      smart_watt_hour_meter.process_echonet_lite_frame(r.created_at, *frame,
                                                       to_sending_message_fifo);
    } else {
      ESP_LOGD(MAIN, "Unknown SEOJ: [0x%x, 0x%x, 0x%x]", seoj[0], seoj[1],
               seoj[2]);
    }
  } else {
    ESP_LOGD(MAIN, "unknown frame header: [0x%x, 0x%x]", frame->ehd1,
             frame->ehd2);
    return;
  }
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
  //
  using namespace std::chrono;
  // スマートメーターにメッセージを送信した時間
  static time_point time_of_sent_message_to_smart_whm{system_clock::now()};
  // 現在時刻
  time_point current_time = system_clock::now();
  auto current_epoch = current_time.time_since_epoch();
  auto current_millis = duration_cast<milliseconds>(current_epoch);
  auto current_seconds = current_millis.count() / 1000 % 60;

  // (あれば)２５個連続でメッセージを受信する
  for (std::size_t count = 0; count < 25; ++count) {
    std::optional<Bp35a1::Response> r = bp35a1->watch_response();
    if (r.has_value()) {
      auto val = r.value();
      received_message_fifo.push(std::move(val));
    } else {
      break;
    }
  }

  //
  // 送信処理と受信処理は1秒ごとに交互に行う
  //
  if (current_seconds % 2 == 0) {
    if (!to_sending_message_fifo.empty()) {
      // 送信するべき測定値があればIoTHubへ送信する
      if (sendTelemetry(to_sending_message_fifo.front())) {
        // 処理したメッセージをFIFOから消す
        to_sending_message_fifo.pop();
      }
    }
  } else {
    if (!received_message_fifo.empty()) {
      // メッセージ受信処理
      Bp35a1::Response r = received_message_fifo.front();
      ESP_LOGD(MAIN, "%s", r.show().c_str());
      switch (r.tag) {
      case Bp35a1::Response::Tag::EVENT:
        // イベント受信処理
        process_event(r);
        break;
      case Bp35a1::Response::Tag::ERXUDP:
        // ERXUDPを処理する
        process_erxudp(r, smart_watt_hour_meter, to_sending_message_fifo);
        // 測定値をセットする
        measurement_watt.set(smart_watt_hour_meter.instant_watt);
        measurement_ampere.set(smart_watt_hour_meter.instant_ampere);
        measurement_cumlative_wh.set(smart_watt_hour_meter.cumlative_watt_hour);
        break;
      default:
        break;
      }
      // 処理したメッセージをFIFOから消す
      received_message_fifo.pop();
    }
  }

  // 測定値を更新する
  measurement_watt.update();
  measurement_ampere.update();
  measurement_cumlative_wh.update();
  //
  M5.update();
  loopTelemetry();

  // プログレスバーを表示する
  render_progress_bar(1000 * (60000 - (current_millis.count() % 60000)) /
                      60000);

  // 毎分0秒にスマートメーターに要求を出す
  if (auto dt = duration_cast<seconds>(current_time -
                                       time_of_sent_message_to_smart_whm);
      (current_seconds == 0) && (dt >= seconds{1})) {
    std::vector<SmartWhm::EchonetLiteEPC> epcs{};
    //
    if (!smart_watt_hour_meter.whm_unit.has_value()) {
      // 係数
      // 積算電力量単位
      // 積算電力量有効桁数
      epcs = {
          SmartWhm::EchonetLiteEPC::Coefficient,
          SmartWhm::EchonetLiteEPC::Unit_for_cumulative_amounts,
          SmartWhm::EchonetLiteEPC::Number_of_effective_digits,
      };
      ESP_LOGD(MAIN, "%s",
               "request coefficient / unit for whm / request number of "
               "effective digits");
    } else if (!smart_watt_hour_meter.cumlative_watt_hour.has_value() ||
               !smart_watt_hour_meter.day_for_which_the_historcal.has_value()) {
      // 定時積算電力量計測値(正方向計測値)
      // 積算履歴収集日
      epcs = {
          SmartWhm::EchonetLiteEPC::
              Cumulative_amounts_of_electric_energy_measured_at_fixed_time,
          SmartWhm::EchonetLiteEPC::Day_for_which_the_historcal_data_1,
      };
      ESP_LOGD(MAIN, "%s",
               "request amounts of electric power / day for historical data 1");
    } else {
      // 瞬時電力要求
      // 瞬時電流要求
      epcs = {SmartWhm::EchonetLiteEPC::Measured_instantaneous_power,
              SmartWhm::EchonetLiteEPC::Measured_instantaneous_currents};
      ESP_LOGD(MAIN, "%s", "request inst-epower and inst-current");
    }
    // スマートメーターに要求を出す
    if (!bp35a1->send_request(epcs)) {
      ESP_LOGD(MAIN, "request NG");
    }
    // 送信時間を記録する
    time_of_sent_message_to_smart_whm = current_time;
  }

  //
  delay(10);
}

//
// ディスプレイ表示用
//

// 瞬時電力量
static std::string to_str_watt(std::optional<SmartWhm::InstantWatt> watt) {
  if (!watt.has_value()) {
    return std::string("---- W");
  }
  char buff[100]{'\0'};
  std::sprintf(buff, "%4d W", watt.value().watt);
  return std::string(buff);
};

// 瞬時電流
static std::string
to_str_ampere(std::optional<SmartWhm::InstantAmpere> ampere) {
  if (!ampere.has_value()) {
    return std::string("R:--.- A, T:--.- A");
  }
  uint16_t r_deciA = ampere.value().get_deciampere_R();
  uint16_t t_deciA = ampere.value().get_deciampere_T();
  // 整数部と小数部
  auto r = std::make_pair(r_deciA / 10, r_deciA % 10);
  auto t = std::make_pair(t_deciA / 10, t_deciA % 10);
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
    std::sprintf(buff, "%02d:%02d", wh.hour(), wh.minutes());
    output += std::string(buff);
  }
  // 電力量
  std::optional<std::string> opt = wh.to_string_kwh();
  if (opt.has_value()) {
    output += " " + opt.value() + " kwh";
  } else {
    // 単位がないならそのまま出す
    output += " " + std::to_string(wh.cumlative_watt_hour());
  }
  return output;
}
