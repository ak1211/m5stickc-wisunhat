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
void send_measurement_request(Bp35a1 *bp35a1) {
  //
  struct DispatchList {
    using UpdateFn = std::function<void(DispatchList *)>;
    //
    UpdateFn update;     // 更新
    bool run;            // これがtrueなら実行する
    const char *message; // ログに送るメッセージ
    std::vector<SmartWhm::EchonetLiteEPC> epcs; // 要求
  };
  // 1回のみ実行する場合
  DispatchList::UpdateFn one_shot = [](DispatchList *d) { d->run = false; };
  // 繰り返し実行する場合
  DispatchList::UpdateFn continueous = [](DispatchList *d) { d->run = true; };
  //
  static std::array<DispatchList, 3> dispach_list = {
      //
      // 起動時に1回のみ送る要求
      //
      // 係数
      // 積算電力量単位
      // 積算電力量有効桁数
      DispatchList{
          one_shot,
          true,
          "request coefficient / unit for whm / request number of effective "
          "digits",
          {
              SmartWhm::EchonetLiteEPC::Coefficient,
              SmartWhm::EchonetLiteEPC::Unit_for_cumulative_amounts,
              SmartWhm::EchonetLiteEPC::Number_of_effective_digits,
          }},
      // 定時積算電力量計測値(正方向計測値)
      DispatchList{
          one_shot,
          true,
          "request amounts of electric power",
          {SmartWhm::EchonetLiteEPC::
               Cumulative_amounts_of_electric_energy_measured_at_fixed_time}},
      //
      // 定期的に繰り返して送る要求
      //
      // 瞬時電力要求
      // 瞬時電流要求
      DispatchList{continueous,
                   true,
                   "request inst-epower and inst-current",
                   {SmartWhm::EchonetLiteEPC::Measured_instantaneous_power,
                    SmartWhm::EchonetLiteEPC::Measured_instantaneous_currents}},
  };

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
  if (bp35a1->send_request(itr->epcs)) {
  } else {
    ESP_LOGD(MAIN, "request NG");
  }
  itr->update(itr); // 結果がどうあれ更新する

  // 次
  itr = next_itr(itr);
}

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
  constexpr std::clock_t INTERVAL = 30 * CLOCKS_PER_SEC;
  static std::clock_t previous = 0L;
  std::clock_t current = std::clock();

  if (current - previous < INTERVAL) {
    return true;
  }

  for (std::size_t retry = 0; retry < retry_count; ++retry) {
    if (WiFi.status() != WL_CONNECTED) {
      ESP_LOGI(MAIN, "WiFi reconnect");
      WiFi.disconnect(true);
      WiFi.reconnect();
      establishConnection();
    }
  }

  if (WiFi.status() != WL_CONNECTED) {
    return false;
  } else {
    previous = current;
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
  measurement_watt.update(std::nullopt);
  measurement_ampere.update(std::nullopt);
  measurement_cumlative_wh.update(std::nullopt);

  //
  // FreeRTOSタスク起動
  //
  xTaskCreatePinnedToCore(check_MQTT_connection_task, "checkMQTTtask", 8192,
                          nullptr, 10, nullptr, ARDUINO_RUNNING_CORE);
}

//
// Arduinoのloop()関数
//
void loop() {
  // 乗数(無い場合の乗数は1)
  static std::optional<SmartWhm::Coefficient> whm_coefficient{std::nullopt};
  // 単位
  static std::optional<SmartWhm::Unit> whm_unit{std::nullopt};
  // スマートメーターからのメッセージ受信バッファ
  static std::queue<std::string_view> smart_whm_received_message_fifo{};
  // IoT Hub送信用バッファ
  static std::queue<std::string> to_sending_message_fifo{};
  //
  static int remains = 0;
  // この関数の実行4000回に1回メッセージを送るという意味
  constexpr int CYCLE{4000};

  loopTelemetry();
  //
  std::optional<Bp35a1::Response> opt = bp35a1->watch_response();
  if (opt.has_value()) {
    // メッセージ受信処理
    Bp35a1::Response r = opt.value();
    ESP_LOGD(MAIN, "%s", r.show().c_str());
    if (r.tag == Bp35a1::Response::Tag::EVENT) {
      int num = std::strtol(r.keyval["NUM"].c_str(), nullptr, 16);
      switch (num) {
      case 0x21: // EVENT 21 :
                 // UDP送信処理が完了した
      {
        ESP_LOGD(MAIN, "UDP transmission successful.");
      } break;
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
    } else if (r.tag == Bp35a1::Response::Tag::ERXUDP) {
      // key-valueストアに入れるときにテキスト形式に変換してあるので元のバイナリに戻す
      std::size_t datalen =
          std::strtol(r.keyval["DATALEN"].data(), nullptr, 16);
      // テキスト形式
      std::string_view textformat = r.keyval["DATA"];
      // 変換後のバイナリ
      std::vector<uint8_t> binaryformat =
          Bp35a1::Response::text_to_binary(textformat);
      // EchonetLiteFrameに変換
      EchonetLiteFrame *frame =
          reinterpret_cast<EchonetLiteFrame *>(binaryformat.data());
      ESP_LOGD(MAIN, "%s", SmartWhm::show(*frame).c_str());
      //
      const std::array<uint8_t, 3> seoj{
          frame->edata.seoj[0], frame->edata.seoj[1], frame->edata.seoj[2]};
      if (seoj == NodeProfileClass::EchonetLiteEOJ()) {
        std::vector<std::vector<uint8_t>> vv =
            splitToEchonetLiteData(frame->edata);
        for (const auto &v : vv) {
          auto prop = reinterpret_cast<const EchonetLiteProp *>(v.data());
          switch (prop->epc) {
          case 0xD5: // インスタンスリスト通知
          {
            if (prop->pdc >= 4) { // 4バイト以上
              ESP_LOGD(MAIN, "instances list");
              uint8_t total_number_of_instances = prop->edt[0];
              const EchonetLiteObjectCode *p =
                  reinterpret_cast<const EchonetLiteObjectCode *>(
                      &prop->edt[1]);
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
        }
      } else if (seoj == SmartWhm::EchonetLiteEOJ()) {
        //
        // 低圧スマートメーターからやってきたメッセージだった
        //
        // FIFOに積んでおいて後で処理する
        smart_whm_received_message_fifo.emplace(std::move(textformat));
      }
    }
  } else if (!smart_whm_received_message_fifo.empty()) {
    //
    // スマートメーターからのメッセージがない間にFIFOに積んでおいた処理をする
    //
    // 変換後のバイナリ
    std::vector<uint8_t> binaryformat = Bp35a1::Response::text_to_binary(
        smart_whm_received_message_fifo.front());
    // EchonetLiteFrameに変換
    EchonetLiteFrame *frame =
        reinterpret_cast<EchonetLiteFrame *>(binaryformat.data());
    std::vector<std::vector<uint8_t>> vv = splitToEchonetLiteData(frame->edata);
    for (const auto &v : vv) {
      const EchonetLiteProp *prop =
          reinterpret_cast<const EchonetLiteProp *>(v.data());
      switch (prop->epc) {
      case 0xD3: // 係数
      {
        if (prop->pdc == 0x04) { // 4バイト
          auto c = SmartWhm::Coefficient(
              {prop->edt[0], prop->edt[1], prop->edt[2], prop->edt[3]});
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
        if (prop->pdc == 0x01) { // 1バイト
          auto digits = SmartWhm::EffectiveDigits(prop->edt[0]);
          ESP_LOGD(MAIN, "%s", digits.show().c_str());
        } else {
          ESP_LOGD(MAIN, "pdc is should be 1 bytes, this is %d bytes.",
                   prop->pdc);
        }
      } break;
      case 0xE1: // 積算電力量単位 (正方向、逆方向計測値)
      {
        if (prop->pdc == 0x01) { // 1バイト
          auto unit = SmartWhm::Unit(prop->edt[0]);
          ESP_LOGD(MAIN, "%s", unit.show().c_str());
          whm_unit = unit;
        } else {
          ESP_LOGD(MAIN, "pdc is should be 1 bytes, this is %d bytes.",
                   prop->pdc);
        }
      } break;
      case 0xE7: // 瞬時電力値 W
      {
        if (prop->pdc == 0x04) { // 4バイト
          auto w = SmartWhm::InstantWatt(
              {prop->edt[0], prop->edt[1], prop->edt[2], prop->edt[3]},
              std::time(nullptr));
          ESP_LOGD(MAIN, "%s", w.show().c_str());
          // 測定値を表示する
          measurement_watt.update(w);
          // 送信バッファへ追加する
          to_sending_message_fifo.emplace(w.watt_to_telemetry_message());
        } else {
          ESP_LOGD(MAIN, "pdc is should be 4 bytes, this is %d bytes.",
                   prop->pdc);
        }
      } break;
      case 0xE8: // 瞬時電流値
      {
        if (prop->pdc == 0x04) { // 4バイト
          auto a = SmartWhm::InstantAmpere(
              {prop->edt[0], prop->edt[1], prop->edt[2], prop->edt[3]},
              std::time(nullptr));
          ESP_LOGD(MAIN, "%s", a.show().c_str());
          // 測定値を表示する
          measurement_ampere.update(a);
          // 送信バッファへ追加する
          to_sending_message_fifo.emplace(a.ampere_to_telemetry_message());
        } else {
          ESP_LOGD(MAIN, "pdc is should be 4 bytes, this is %d bytes.",
                   prop->pdc);
        }
      } break;
      case 0xEA: // 定時積算電力量
      {
        if (prop->pdc == 0x0B) { // 11バイト
          // std::to_arrayの登場はC++20からなのでこんなことになった
          std::array<uint8_t, 11> memory;
          std::copy_n(prop->edt, 11, memory.begin());
          //
          auto cwh =
              SmartWhm::CumulativeWattHour(memory, whm_coefficient, whm_unit);
          ESP_LOGD(MAIN, "%s", cwh.show().c_str());
          // 測定値を表示する
          measurement_cumlative_wh.update(cwh);
          // 送信バッファへ追加する
          to_sending_message_fifo.emplace(cwh.cwh_to_telemetry_message());
        } else {
          ESP_LOGD(MAIN, "pdc is should be 11 bytes, this is %d bytes.",
                   prop->pdc);
        }
      } break;
      default:
        ESP_LOGD(MAIN, "unknown epc: 0x%x", prop->epc);
        break;
      }
    }
    //
    // 処理したメッセージをFIFOから消す
    //
    smart_whm_received_message_fifo.pop();
  } else {
    //
    // スマートメーターからのメッセージ受信処理が無い場合にそれ以外の処理をする
    //
    // 送信するべき測定値があればIoTHubへ送信する
    if (!to_sending_message_fifo.empty()) {
      if (sendTelemetry(to_sending_message_fifo.front())) {
        to_sending_message_fifo.pop();
      }
    }
    //
    // 定期メッセージ送信処理
    //
    if (remains == 0) {
      send_measurement_request(bp35a1);
    }
  }

  //
  M5.update();

  // プログレスバーを表示する
  {
    int bar_width = M5.Lcd.width() * (CYCLE - remains) / CYCLE;
    int y = M5.Lcd.height() - 2;
    M5.Lcd.fillRect(bar_width, y, M5.Lcd.width(), M5.Lcd.height(), BLACK);
    M5.Lcd.fillRect(0, y, bar_width, M5.Lcd.height(), YELLOW);
  }

  //
  remains = (remains + 1) % CYCLE;

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
