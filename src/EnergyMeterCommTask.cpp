// Copyright (c) 2022 Akihiro Yamamoto.
// Licensed under the MIT License <https://spdx.org/licenses/MIT.html>
// See LICENSE file in the project root for full license information.
//
#include "EnergyMeterCommTask.hpp"
#include "Application.hpp"
#include "Bp35a1.hpp"
#include "EchonetLite.hpp"
#include <chrono>
#include <future>
#include <string>

using namespace std::chrono;
using namespace std::chrono_literals;

//
bool EnergyMeterCommTask::begin(std::chrono::system_clock::time_point nowtp) {
  auto extra_sec =
      std::chrono::duration_cast<seconds>(nowtp.time_since_epoch()) % 60s;
  //
  _next_send_request_in_tp = nowtp + 1min - extra_sec;
  return true;
}

// 測定関数
void EnergyMeterCommTask::task_handler(
    std::chrono::system_clock::time_point nowtp) {
  if (nowtp >= _next_send_request_in_tp) {
    auto extra_sec =
        std::chrono::duration_cast<seconds>(nowtp.time_since_epoch()) % 60s;
    _next_send_request_in_tp = nowtp + 1min - extra_sec;
    //
    send_request_to_port(nowtp); // 送信
  } else {
    receive_from_port(nowtp); // 受信
  }
}

// 受信
void EnergyMeterCommTask::receive_from_port(system_clock::time_point nowtp) {
  // (あれば)連続でスマートメーターからのメッセージを受信する
  for (auto count = 0; count < 25; ++count) {
    if (auto resp = Bp35a1::receive_response(_comm_port)) {
      _received_message_fifo.push({nowtp, resp.value()});
    }
    std::this_thread::yield();
  }

  // スマートメーターからのメッセージ受信処理
  if (_received_message_fifo.empty()) {
    /* nothing to do */
  } else {
    auto [time_at, resp] = _received_message_fifo.front();
    std::visit([](const auto &x) { M5_LOGD("%s", to_string(x).c_str()); },
               resp);
    if (auto *pevent = std::get_if<Bp35a1::ResEvent>(&resp)) {
      // イベント受信処理
      process_event(*pevent);
    } else if (auto *perxudp = std::get_if<Bp35a1::ResErxudp>(&resp)) {
      // ERXUDPを処理する
      process_erxudp(time_at, *perxudp);
    }
    // 処理したメッセージをFIFOから消す
    _received_message_fifo.pop();
  }
}

// スマートメーターに要求を送る
void EnergyMeterCommTask::send_request_to_port(system_clock::time_point nowtp) {
  if (Application::getElectricPowerData().whm_unit.has_value()) {
    send_periodical_request();
  } else {
    // 積算電力量単位が初期値の場合にスマートメーターに最初の要求を出す
    send_first_request();
  }
}

// BP35A1から受信したイベントを処理する
void EnergyMeterCommTask::process_event(const Bp35a1::ResEvent &ev) {
  switch (ev.num.u8) {
  case 0x01: // EVENT 1 :
             // NSを受信した
    M5_LOGI("Received NS");
    break;
  case 0x02: // EVENT 2 :
             // NAを受信した
    M5_LOGI("Received NA");
    break;
  case 0x05: // EVENT 5 :
             // Echo Requestを受信した
    M5_LOGI("Received Echo Request");
    break;
  case 0x1F: // EVENT 1F :
             // EDスキャンが完了した
    M5_LOGI("Complete ED Scan.");
    break;
  case 0x20: // EVENT 20 :
             // BeaconRequestを受信した
    M5_LOGI("Received BeaconRequest");
    break;
  case 0x21: // EVENT 21 :
             // UDP送信処理が完了した
    M5_LOGD("UDP transmission successful.");
    break;
  case 0x24: // EVENT 24 :
             // PANAによる接続過程でエラーが発生した(接続が完了しなかった)
    M5_LOGD("PANA reconnect");
    _pana_session_established = false;
    break;
  case 0x25: // EVENT 25 :
             // PANAによる接続が完了した
    M5_LOGD("PANA session connected");
    _pana_session_established = true;
    break;
  case 0x26: // EVENT 26 :
             // 接続相手からセッション終了要求を受信した
    M5_LOGD("session terminate request");
    _pana_session_established = false;
    break;
  case 0x27: // EVENT 27 :
             // PANAセッションの終了に成功した
    M5_LOGD("PANA session terminate");
    _pana_session_established = false;
    break;
  case 0x28: // EVENT 28 :
             // PANAセッションの終了要求に対する応答がなくタイムアウトした(セッションは終了)
    M5_LOGD("PANA session terminate. reason: timeout");
    _pana_session_established = false;
    break;
  case 0x29: // PANAセッションのライフタイムが経過して期限切れになった
    M5_LOGI("PANA session expired");
    _pana_session_established = false;
    break;
  case 0x32: // ARIB108の送信緩和時間の制限が発動した
    M5_LOGI("");
    break;
  case 0x33: // ARIB108の送信緩和時間の制限が解除された
    M5_LOGI("");
    break;
  default:
    break;
  }
}

//
// ノードプロファイルクラスのEchonetLiteフレームを処理する
//
void EnergyMeterCommTask::process_node_profile_class_frame(
    const EchonetLiteFrame &frame) {
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
        M5_LOGD("list of instances (EOJ): %s", oss.str().c_str());
      }
      //
      // 通知されているのは自分自身だろうから
      // なにもしませんよ
      //
      break;
    default:
      M5_LOGD("unknown EPC: %02X", prop.epc);
      break;
    }
  }
}

//
// BP35A1から受信したERXUDPイベントを処理する
//
void EnergyMeterCommTask::process_erxudp(
    std::chrono::system_clock::time_point at, const Bp35a1::ResErxudp &ev) {
  // EchonetLiteFrameに変換
  if (auto opt = deserializeToEchonetLiteFrame(ev.data)) {
    const EchonetLiteFrame &frame = opt.value();
    //  EchonetLiteフレームだった
    M5_LOGD("%s", to_string(frame).c_str());
    //
    if (frame.edata.seoj.s == NodeProfileClass::EchonetLiteEOJ) {
      // ノードプロファイルクラス
      process_node_profile_class_frame(frame);
    } else if (frame.edata.seoj.s == SmartElectricEnergyMeter::EchonetLiteEOJ) {
      // 低圧スマート電力量計クラス
      namespace M = SmartElectricEnergyMeter;
      for (auto rx : M::process_echonet_lite_frame(frame)) {
        if (auto *p = std::get_if<M::Coefficient>(&rx)) {
          Application::getElectricPowerData().whm_coefficient = *p;
        } else if (std::get_if<M::EffectiveDigits>(&rx)) {
          // no operation
        } else if (auto *p = std::get_if<M::Unit>(&rx)) {
          Application::getElectricPowerData().whm_unit = *p;
        } else if (auto *p = std::get_if<M::InstantAmpere>(&rx)) {
          Application::getElectricPowerData().instant_ampere =
              std::make_pair(at, *p);
          // 送信バッファへ追加する
          Application::getTelemetry().enqueue(std::make_pair(at, *p));
        } else if (auto *p = std::get_if<M::InstantWatt>(&rx)) {
          Application::getElectricPowerData().instant_watt =
              std::make_pair(at, *p);
          // 送信バッファへ追加する
          Application::getTelemetry().enqueue(std::make_pair(at, *p));
        } else if (auto *p = std::get_if<M::CumulativeWattHour>(&rx)) {
          if (auto unit = Application::getElectricPowerData().whm_unit) {
            auto coeff =
                Application::getElectricPowerData().whm_coefficient.value_or(
                    M::Coefficient{});
            Application::getElectricPowerData().cumlative_watt_hour =
                std::make_tuple(*p, coeff, *unit);
            // 送信バッファへ追加する
            Application::getTelemetry().enqueue(
                std::make_tuple(*p, coeff, *unit));
          }
        }
      }
    }
  }
}

//
// スマートメーターに最初の要求を出す
//
void EnergyMeterCommTask::send_first_request() {
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
  M5_LOGD("request status / location / fault / manufacturer / coefficient / "
          "unit for whm / request number of "
          "effective digits");
  // スマートメーターに要求を出す
  const auto tid = EchonetLiteTransactionId({12, 34});

  if (_pana_session_established) {
    Bp35a1::send_request(_comm_port, _identifier, tid, epcs);
  } else {
    M5_LOGD("No connection to smart meter.");
  }
}

//
// スマートメーターに定期的な要求を出す
//
void EnergyMeterCommTask::send_periodical_request() {
  using E = SmartElectricEnergyMeter::EchonetLiteEPC;
  std::vector<E> epcs = {
      E::Measured_instantaneous_power,    // 瞬時電力要求
      E::Measured_instantaneous_currents, // 瞬時電流要求
  };
  M5_LOGD("request inst-epower and inst-current");
  //
  std::time_t displayed_jst = []() -> std::time_t {
    if (Application::getElectricPowerData().cumlative_watt_hour.has_value()) {
      auto [cwh, unuse, unused] =
          Application::getElectricPowerData().cumlative_watt_hour.value();
      return cwh.get_time_t().value_or(0);
    } else {
      return 0;
    }
  }();
#if 0
  auto measured_at = std::chrono::system_clock::from_time_t(displayed_jst);
  if (auto elapsed = current_time - measured_at;
      elapsed >= std::chrono::minutes{36}) {
    // 表示中の定時積算電力量計測値が36分より古い場合は
    // 定時積算電力量計測値(30分値)をつかみそこねたと判断して
    // 定時積算電力量要求を出す
    epcs.push_back(
        E::Cumulative_amounts_of_electric_energy_measured_at_fixed_time);
    // 定時積算電力量計測値(正方向計測値)
    M5_LOGD("request amounts of electric power");
  }
// 積算履歴収集日
  if (!whm.day_for_which_the_historcal.has_value()) {
    epcs.push_back(E::Day_for_which_the_historcal_data_1);
    ESP_LOGD(MAIN, "request day for historical data 1");
  }
#endif
  // スマートメーターに要求を出す
  if (_pana_session_established) {
    const auto tid = EchonetLiteTransactionId({12, 34});
    Bp35a1::send_request(_comm_port, _identifier, tid, epcs);
  } else {
    M5_LOGD("No connection to smart meter.");
  }
}

#if 0

//
// 低速度loop()関数
//
inline void low_speed_loop(std::chrono::system_clock::time_point nowtp) {
  if (M5.Power.getBatteryLevel() < 100 &&
      M5.Power.isCharging() == m5::Power_Class::is_discharging) {
    // バッテリー駆動時は明るさを下げる
    if (M5.Display.getBrightness() != 75) {
      M5.Display.setBrightness(75);
    }
  } else {
    // 通常の明るさ
    if (M5.Display.getBrightness() != 150) {
      M5.Display.setBrightness(150);
    }
  }
  //
  using namespace std::chrono;
  //
  auto display_message = [](const std::string &str, void *user_data) -> void {
    M5_LOGD("%s", str.c_str());
    if (user_data) {
      static_cast<Widget::Dialogue *>(user_data)->setMessage(str);
    }
  };
  //
  if (!smart_watt_hour_meter) {
    // 接続対象のスマートメーターの情報が無い場合は探す。
    M5_LOGD("Find a smart energy meter");
    Widget::Dialogue dialogue{"Find a meter."};
    display_message("seeking...", &dialogue);
    auto identifier = Bp35a1::startup_and_find_meter(
        Serial2, {BID, BPASSWORD}, display_message, &dialogue);
    if (identifier) {
      // 見つかったスマートメーターをグローバル変数に設定する
      smart_watt_hour_meter =
          std::make_unique<SmartWhm>(SmartWhm(Serial2, identifier.value()));
    } else {
      // スマートメーターが見つからなかった
      M5_LOGE("ERROR: meter not found.");
      dialogue.error("ERROR: meter not found.");
      delay(1000);
    }
  } else if (!smart_watt_hour_meter->isPanaSessionEstablished) {
    // スマートメーターとのセッションを開始する。
    M5_LOGD("Connect to a meter.");
    Widget::Dialogue dialogue{"Connect to a meter."};
    display_message("Send request to a meter.", &dialogue);
    // スマートメーターに接続要求を送る
    if (auto ok = connect(smart_watt_hour_meter->commport,
                          smart_watt_hour_meter->identifier, display_message,
                          &dialogue);
        ok) {
      // 接続成功
      smart_watt_hour_meter->isPanaSessionEstablished = true;
    } else {
      // 接続失敗
      smart_watt_hour_meter->isPanaSessionEstablished = false;
      M5_LOGE("smart meter connection error.");
      dialogue.error("smart meter connection error.");
      delay(1000);
    }
  } else if (WiFi.status() != WL_CONNECTED) {
    // WiFiが接続されていない場合は接続する。
    Widget::Dialogue dialogue{"Connect to WiFi."};
    if (auto ok = waitingForWiFiConnection(dialogue); !ok) {
      dialogue.error("ERROR: WiFi");
      delay(1000);
    }
  } else if (bool connected = telemetry.connected(); !connected) {
    // AWS IoTと接続されていない場合は接続する。
    Widget::Dialogue dialogue{"Connect to AWS IoT."};
    if (auto ok = telemetry.connectToAwsIot(std::chrono::seconds{60},
                                            display_message, &dialogue);
        !ok) {
      dialogue.error("ERROR");
      delay(1000);
    }
  }

  // MQTT送受信
  telemetry.loop_mqtt();

  // スマートメーターに要求を送る
  send_request_to_smart_meter();
}
#endif