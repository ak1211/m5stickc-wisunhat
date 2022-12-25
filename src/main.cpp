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
#include "Gauge.hpp"
#include "SmartWhm.hpp"
#include "Telemetry.hpp"
#include "credentials.h"

using namespace std::literals::string_view_literals;

// time zone = Asia_Tokyo(UTC+9)
constexpr char TZ_TIME_ZONE[] = "JST-9";

// BP35A1と会話できるポート番号
constexpr int CommPortRx{26};
constexpr int CommPortTx{0};

// vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv
// グローバル変数はじまり

// スマート電力量計のＢルート識別子
struct SmartWhmBRoute {
  Stream &commport;
  Bp35a1::SmartMeterIdentifier identifier;
};
static std::unique_ptr<SmartWhmBRoute> smart_whm_b_route;
//  スマートメーター
static SmartWhm smart_watt_hour_meter{};

// MQTT
static Telemetry::Mqtt telemetry;

// 瞬時電力量
static Gauge<SmartElectricEnergyMeter::InstantWatt> instant_watt_gauge{
    2,
    4,
    YELLOW,
    {10, 10},
    [](std::optional<SmartElectricEnergyMeter::InstantWatt> iw) -> std::string {
      std::ostringstream oss;
      if (iw.has_value()) {
        oss << std::setfill(' ') << std::setw(5) << iw->watt.count();
      } else {
        oss << std::setfill('-') << std::setw(5) << "";
      }
      oss << " W"sv;
      return oss.str();
    }};

// 瞬時電流
static Gauge<SmartElectricEnergyMeter::InstantAmpere> instant_ampere_gauge{
    1,
    4,
    WHITE,
    {10, 10 + 48},
    [](std::optional<SmartElectricEnergyMeter::InstantAmpere> ia)
        -> std::string {
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
      if (ia.has_value()) {
        r = map_deciA(ia->ampereR);
        t = map_deciA(ia->ampereT);
      } else {
        r = t = map_deciA(std::nullopt);
      }
      oss << "R:" << r << " A, T:" << t << " A";
      return oss.str();
    }};

// 積算電力量
static Gauge<SmartElectricEnergyMeter::CumulativeWattHour>
    cumulative_watt_hour_gauge{
        1,
        4,
        WHITE,
        {10, 10 + 48 + 24},
        [](std::optional<SmartElectricEnergyMeter::CumulativeWattHour>
               watt_hour) -> std::string {
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
              if (smart_watt_hour_meter.whm_unit.has_value()) {
                auto unit = smart_watt_hour_meter.whm_unit.value();
                str_kwh = to_string_cumlative_kilo_watt_hour(
                    cwh, smart_watt_hour_meter.whm_coefficient, unit);
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
          std::string hm{};
          std::string cwh{};
          if (watt_hour.has_value()) {
            hm = map_hour_min(
                std::make_pair(watt_hour->hour(), watt_hour->minutes()));
            cwh = map_cwh(10, 3, *watt_hour);
          } else {
            hm = map_hour_min(std::nullopt);
            cwh = map_cwh(10, 3, std::nullopt);
          }
          std::ostringstream oss;
          oss << hm << " " << cwh;
          return oss.str();
        }};

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
  ok = ok ? telemetry.connectToAwsIot() : ok;
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
  smart_whm_b_route = std::make_unique<SmartWhmBRoute>(
      SmartWhmBRoute{Serial2, Bp35a1::SmartMeterIdentifier{}});

  if (auto ident = Bp35a1::startup_and_find_meter(
          smart_whm_b_route->commport, {BID, BPASSWORD}, display_boot_message);
      ident.has_value()) {
    smart_whm_b_route->identifier = ident.value();
    // 見つかったスマートメーターに接続要求を送る
    if (!connect(smart_whm_b_route->commport, smart_whm_b_route->identifier,
                 display_boot_message)) {
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
    if (!connect(smart_whm_b_route->commport, smart_whm_b_route->identifier,
                 display_boot_message)) {
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
        for (;;) {
          if (std::distance(it, prop.edt.cend()) < 3) {
            break;
          }
          auto o = EchonetLiteObjectCode({*it++, *it++, *it++});
          oss << to_string(o) << ",";
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
template <class Clock, class Duration>
static EchonetLiteTransactionId
time_to_transaction_id(std::chrono::time_point<Clock, Duration> tp) {
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
static Telemetry::MessageId
process_erxudp(std::chrono::system_clock::time_point at,
               Telemetry::MessageId messageId, const Bp35a1::ResErxudp &ev) {
  // EchonetLiteFrameに変換
  if (auto opt = deserializeToEchonetLiteFrame(ev.data); opt.has_value()) {
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
      namespace Meter = SmartElectricEnergyMeter;
      for (auto rx : Meter::process_echonet_lite_frame(frame)) {
        if (std::holds_alternative<Meter::Coefficient>(rx)) {
          auto coeff = std::get<Meter::Coefficient>(rx);
          smart_watt_hour_meter.whm_coefficient = coeff;
        } else if (std::holds_alternative<Meter::EffectiveDigits>(rx)) {
          // no operation
        } else if (std::holds_alternative<Meter::Unit>(rx)) {
          auto unit = std::get<Meter::Unit>(rx);
          smart_watt_hour_meter.whm_unit = unit;
        } else if (std::holds_alternative<Meter::InstantAmpere>(rx)) {
          auto ampere = std::get<Meter::InstantAmpere>(rx);
          smart_watt_hour_meter.instant_ampere = ampere;
          // 送信バッファへ追加する
          telemetry.push_queue(std::make_tuple(messageId++, at, ampere));
        } else if (std::holds_alternative<Meter::InstantWatt>(rx)) {
          auto watt = std::get<Meter::InstantWatt>(rx);
          smart_watt_hour_meter.instant_watt = watt;
          // 送信バッファへ追加する
          telemetry.push_queue(std::make_tuple(messageId++, at, watt));
        } else if (std::holds_alternative<Meter::CumulativeWattHour>(rx)) {
          auto cwh = std::get<Meter::CumulativeWattHour>(rx);
          smart_watt_hour_meter.cumlative_watt_hour = cwh;
          ESP_LOGD(MAIN, "%s", to_string(cwh).c_str());
          if (smart_watt_hour_meter.whm_unit.has_value()) {
            // 送信バッファへ追加する
            telemetry.push_queue(
                std::make_tuple(messageId++, cwh,
                                smart_watt_hour_meter.whm_coefficient.value_or(
                                    Meter::Coefficient{}),
                                smart_watt_hour_meter.whm_unit.value()));
          }
        }
      }
    }
  }
  return messageId;
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

  Bp35a1::send_request(smart_whm_b_route->commport,
                       smart_whm_b_route->identifier, tid, epcs);
}

//
// スマートメーターに定期的な要求を出す
//
static void
send_periodical_request(std::chrono::system_clock::time_point current_time,
                        const SmartWhm &whm) {
  using E = SmartElectricEnergyMeter::EchonetLiteEPC;
  std::vector<E> epcs = {
      E::Measured_instantaneous_power,    // 瞬時電力要求
      E::Measured_instantaneous_currents, // 瞬時電流要求
  };
  ESP_LOGD(MAIN, "request inst-epower and inst-current");
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
        E::Cumulative_amounts_of_electric_energy_measured_at_fixed_time);
    // 定時積算電力量計測値(正方向計測値)
    ESP_LOGD(MAIN, "request amounts of electric power");
  }
  // 積算履歴収集日
  if (!whm.day_for_which_the_historcal.has_value()) {
    epcs.push_back(E::Day_for_which_the_historcal_data_1);
    ESP_LOGD(MAIN, "request day for historical data 1");
  }
  // スマートメーターに要求を出す
  const auto tid = time_to_transaction_id(current_time);
  Bp35a1::send_request(smart_whm_b_route->commport,
                       smart_whm_b_route->identifier, tid, epcs);
}

//
// スマートメーターに要求を送る
//
static void send_request_to_smart_meter() {
  using namespace std::chrono;
  static time_point send_time_at{system_clock::now()};
  auto tp = system_clock::now();
  //
  // スマートメーターに要求を出す(1秒以上の間隔をあけて)
  //
  if (auto elapsed = tp - send_time_at; elapsed >= seconds{1}) {
    // 積算電力量単位が初期値の場合にスマートメーターに最初の要求を出す
    if (!smart_watt_hour_meter.whm_unit.has_value()) {
      send_first_request(tp);
    } else if (duration_cast<seconds>(tp.time_since_epoch()).count() % 60 ==
               0) {
      // 毎分0秒にスマートメーターに定期要求を出す
      send_periodical_request(tp, smart_watt_hour_meter);
    }
    // 送信時間を記録する
    send_time_at = tp;
  }
}

//
// Arduinoのloop()関数
//
void loop() {
  // メッセージ受信バッファ
  static std::queue<
      std::pair<std::chrono::system_clock::time_point, Bp35a1::Response>>
      received_message_fifo{};
  using namespace std::chrono;
  // メッセージIDカウンタ(IoT Core用)
  static Telemetry::MessageId messageId{};

  //
  // (あれば)２５個連続でスマートメーターからのメッセージを受信する
  //
  for (auto count = 0; count < 25; ++count) {
    if (auto resp = Bp35a1::receive_response(smart_whm_b_route->commport);
        resp.has_value()) {
      received_message_fifo.push({system_clock::now(), resp.value()});
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
    if (std::holds_alternative<Bp35a1::ResEvent>(resp)) {
      Bp35a1::ResEvent &event = std::get<Bp35a1::ResEvent>(resp);
      // イベント受信処理
      process_event(event);
    } else if (std::holds_alternative<Bp35a1::ResErxudp>(resp)) {
      Bp35a1::ResErxudp &event = std::get<Bp35a1::ResErxudp>(resp);
      // ERXUDPを処理する
      messageId = process_erxudp(time_at, messageId, event);
      // 測定値をセットする
      instant_watt_gauge.set(smart_watt_hour_meter.instant_watt);
      instant_ampere_gauge.set(smart_watt_hour_meter.instant_ampere);
      cumulative_watt_hour_gauge.set(smart_watt_hour_meter.cumlative_watt_hour);
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

  //
  // MQTT送受信
  //
  telemetry.loop_mqtt();

  //
  // プログレスバーを表示する
  //
  constexpr milliseconds one_min_in_ms = milliseconds{60000};
  const milliseconds seconds_in_ms = duration_cast<milliseconds>(
      system_clock::now().time_since_epoch() % one_min_in_ms);
  // 毎分0秒までの残り時間(1000分率)
  const uint32_t remains_in_permille =
      1000 * (one_min_in_ms - seconds_in_ms) / one_min_in_ms;
  render_progress_bar(remains_in_permille);

  // スマートメーターに要求を送る
  send_request_to_smart_meter();

  //
  // 55秒以上の待ち時間があるうちに接続状態の検査をする:
  //
  if (auto s = duration_cast<seconds>(system_clock::now().time_since_epoch());
      s.count() % 60 >= 55) {
    if (WiFi.isConnected()) {
      // MQTT接続検査
      telemetry.check_mqtt(seconds{10});
    } else {
      // WiFi接続検査
      checkWiFi(seconds{10});
    }
  }
}
