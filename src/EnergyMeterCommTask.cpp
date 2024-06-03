// Copyright (c) 2022 Akihiro Yamamoto.
// Licensed under the MIT License <https://spdx.org/licenses/MIT.html>
// See LICENSE file in the project root for full license information.
//
#include "EnergyMeterCommTask.hpp"
#include "Application.hpp"
#include "Bp35a1.hpp"
#include "EchonetLite.hpp"
#include "Gui.hpp"
#include "StringBufWithDialogue.hpp"
#include <chrono>
#include <future>
#include <sstream>
#include <string>

using namespace std::chrono;
using namespace std::chrono_literals;

// スマートメーターとのセッションを開始する。
bool EnergyMeterCommTask::begin(std::ostream &os,
                                std::chrono::seconds timeout) {
  bool ok{true};
  ok = ok ? find_energy_meter(os, timeout) : false;
  ok = ok ? connect(os, timeout) : false;
  //
  send_first_request();
  //
  adjust_timing(system_clock::now());
  //
  return ok;
}

//
void EnergyMeterCommTask::adjust_timing(
    std::chrono::system_clock::time_point now_tp) {
  auto extra_sec =
      std::chrono::duration_cast<seconds>(now_tp.time_since_epoch()) % 60s;
  //
  _next_send_request_in_tp = now_tp + 1min - extra_sec;
}

// 測定関数
void EnergyMeterCommTask::task_handler() {
  if (!_pana_session_established) {
    // 再接続
    StringBufWithDialogue buf{"Reconnect meter"};
    std::ostream ostream(&buf);
    connect(ostream, RECONNECT_TIMEOUT);
  } else {
    auto now_tp = system_clock::now();
    if (now_tp >= _next_send_request_in_tp) {
      adjust_timing(now_tp);
      send_periodical_request(); // 送信
    } else {
      receive_from_port(now_tp); // 受信
    }
  }
}

// 接続対象のスマートメーターを探す
bool EnergyMeterCommTask::find_energy_meter(std::ostream &os,
                                            std::chrono::seconds timeout) {
  {
    std::ostringstream ss;
    ss << "Find a smart energy meter";
    os << ss.str() << std::endl;
    M5_LOGD("%s", ss.str().c_str());
  }
  _pana_session_established = false; // この後接続が切れるので
  auto identifier = _bp35a1.startup_and_find_meter(os, _route_b_id,
                                                   _route_b_password, timeout);
  if (identifier) {
    _smart_meter_identifier = identifier;
    return true;
  } else {
    _smart_meter_identifier = std::nullopt;
    // スマートメーターが見つからなかった
    std::ostringstream ss;
    ss << "ERROR: meter not found.";
    os << ss.str() << std::endl;
    M5_LOGE("%s", ss.str().c_str());
    return false;
  }
}

// スマートメーターとのセッションを開始する。
bool EnergyMeterCommTask::connect(std::ostream &os,
                                  std::chrono::seconds timeout) {
  if (!_smart_meter_identifier) {
    // 接続対象のスマートメーターを探す
    find_energy_meter(os, timeout);
  }
  if (_smart_meter_identifier) {
    // スマートメーターに接続要求を送る
    auto ok = _bp35a1.connect(os, *_smart_meter_identifier, timeout);
    if (ok) {
      // 接続成功
      _pana_session_established = true;
    } else {
      // 接続失敗
      _pana_session_established = false;
      std::ostringstream ss;
      ss << "smart meter connection error.";
      os << ss.str() << std::endl;
      M5_LOGE("%s", ss.str().c_str());
    }
  }
  return _pana_session_established;
}

// 受信
void EnergyMeterCommTask::receive_from_port(system_clock::time_point nowtp) {
  // (あれば)連続でスマートメーターからのメッセージを受信する
  for (auto count = 0; count < 25; ++count) {
    if (auto resp = _bp35a1.receive_response()) {
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

// ノードプロファイルクラスのEchonetLiteフレームを処理する
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

// BP35A1から受信したERXUDPイベントを処理する
void EnergyMeterCommTask::process_erxudp(
    std::chrono::system_clock::time_point at, const Bp35a1::ResErxudp &ev) {
  // EchonetLiteFrameに変換
  EchonetLiteFrame frame;
  auto result = EchonetLite::deserializeToEchonetLiteFrame(frame, ev.data);
  if (auto *perror = std::get_if<EchonetLite::DeserializeError>(&result)) {
    // エラー
    M5_LOGE("%s", perror->reason.c_str());
    return;
  } else {
    //  EchonetLiteフレームだった
    M5_LOGD("%s", to_string(frame).c_str());
    //
    if (frame.edata.seoj.s == NodeProfileClass::EchonetLiteEOJ) {
      // ノードプロファイルクラス
      process_node_profile_class_frame(frame);
    } else if (frame.edata.seoj.s == SmartElectricEnergyMeter::EchonetLiteEOJ) {
      // 低圧スマート電力量計クラス
      namespace M = SmartElectricEnergyMeter;
      for (auto rx : EchonetLite::process_echonet_lite_frame(frame)) {
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
          if (auto tele = Application::getTelemetry(); tele) {
            tele->enqueue(std::make_pair(at, *p));
          }
        } else if (auto *p = std::get_if<M::InstantWatt>(&rx)) {
          Application::getElectricPowerData().instant_watt =
              std::make_pair(at, *p);
          // 送信バッファへ追加する
          if (auto tele = Application::getTelemetry(); tele) {
            tele->enqueue(std::make_pair(at, *p));
          }
        } else if (auto *p = std::get_if<M::CumulativeWattHour>(&rx)) {
          if (auto unit = Application::getElectricPowerData().whm_unit) {
            auto coeff =
                Application::getElectricPowerData().whm_coefficient.value_or(
                    M::Coefficient{});
            Application::getElectricPowerData().cumlative_watt_hour =
                std::make_tuple(*p, coeff, *unit);
            // 送信バッファへ追加する
            if (auto tele = Application::getTelemetry(); tele) {
              tele->enqueue(std::make_tuple(*p, coeff, *unit));
            }
          }
        }
      }
    }
  }
}

// スマートメーターに最初の要求を出す
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
      E::Cumulative_amounts_of_electric_energy_measured_at_fixed_time, // 定時積算電力量計測値(正方向計測値)

  };
  M5_LOGD("request status / location / fault / manufacturer / coefficient / "
          "unit for whm / request number of "
          "effective digits / amounts of electric power");
  // スマートメーターに要求を出す
  const auto tid = EchonetLiteTransactionId({12, 34});

  if (_pana_session_established && _smart_meter_identifier) {
    _bp35a1.send_request(*_smart_meter_identifier, tid, epcs);
  } else {
    M5_LOGD("No connection to smart meter.");
  }
}

// スマートメーターに定期的な要求を出す
void EnergyMeterCommTask::send_periodical_request() {
  using E = SmartElectricEnergyMeter::EchonetLiteEPC;
  std::vector<E> epcs = {
      E::Measured_instantaneous_power,    // 瞬時電力要求
      E::Measured_instantaneous_currents, // 瞬時電流要求
  };
  M5_LOGD("request inst-epower and inst-current");
  //
  std::time_t displayed_jst = []() -> std::time_t {
    if (Application::getElectricPowerData().cumlative_watt_hour) {
      auto [cwh, unuse, unused] =
          Application::getElectricPowerData().cumlative_watt_hour.value();
      return cwh.get_time_t().value_or(0);
    } else {
      return 0;
    }
  }();
  auto measured_at = system_clock::from_time_t(displayed_jst);
  if (auto elapsed = system_clock::now() - measured_at;
      elapsed >= std::chrono::minutes{36}) {
    // 表示中の定時積算電力量計測値が36分より古い場合は定時積算電力量要求を出す
    epcs.push_back(
        E::Cumulative_amounts_of_electric_energy_measured_at_fixed_time);
    // 定時積算電力量計測値(正方向計測値)
    M5_LOGD("request amounts of electric power");
  }
  if constexpr (false) {
    // 積算履歴収集日
    if (!Application::getElectricPowerData().day_for_which_the_historcal) {
      epcs.push_back(E::Day_for_which_the_historcal_data_1);
      M5_LOGD("request day for historical data 1");
    }
  }
  // スマートメーターに要求を出す
  if (_pana_session_established && _smart_meter_identifier) {
    const auto tid = EchonetLiteTransactionId({12, 34});
    _bp35a1.send_request(*_smart_meter_identifier, tid, epcs);
  } else {
    M5_LOGD("No connection to smart meter.");
  }
}
