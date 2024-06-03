// Copyright (c) 2022 Akihiro Yamamoto.
// Licensed under the MIT License <https://spdx.org/licenses/MIT.html>
// See LICENSE file in the project root for full license information.
//
#pragma once
#include "Bp35a1.hpp"
#include <chrono>
#include <queue>
#include <string>
#include <tuple>

//
// スマートメーターとの通信
//
class ElectricityMeterCommTask final {
public:
  constexpr static auto RECONNECT_TIMEOUT = std::chrono::seconds{30};
  ElectricityMeterCommTask(Stream &comm_port, std::string route_b_id,
                           std::string route_b_password)
      : _bp35a1{comm_port},
        _route_b_id{route_b_id},
        _route_b_password{route_b_password},
        _pana_session_established{false} {}
  //
  bool begin(std::ostream &os, std::chrono::seconds timeout);
  //
  void adjust_timing(std::chrono::system_clock::time_point now_tp);
  //
  void task_handler();

private:
  //
  std::chrono::system_clock::time_point _next_send_request_in_tp{};
  //
  Bp35a1Class _bp35a1;
  //
  const std::string _route_b_id;
  //
  const std::string _route_b_password;
  // スマート電力量計のＢルート識別子
  std::optional<Bp35a1::SmartMeterIdentifier> _smart_meter_identifier;
  // メッセージ受信バッファ
  std::queue<
      std::pair<std::chrono::system_clock::time_point, Bp35a1Class::Response>>
      _received_message_fifo{};
  // Echonet Lite PANA session
  bool _pana_session_established{false};

private:
  //
  bool find_energy_meter(std::ostream &os, std::chrono::seconds timeout);
  //
  bool connect(std::ostream &os, std::chrono::seconds timeout);
  //
  void receive_from_port(std::chrono::system_clock::time_point nowtp);
  // スマートメーターに最初の要求を出す
  void send_first_request();
  // スマートメーターに定期的な要求を出す
  void send_periodical_request();
  // BP35A1から受信したイベントを処理する
  void process_event(const Bp35a1::ResEvent &ev);
  // ノードプロファイルクラスのEchonetLiteフレームを処理する
  void process_node_profile_class_frame(const EchonetLiteFrame &frame);
  // 低圧スマート電力量計クラスのEchonetLiteフレームを処理する
  void process_electricity_meter_class_frame(
      std::chrono::system_clock::time_point at, const EchonetLiteFrame &frame);
  // 低圧スマート電力量計のデータを処理する
  void process_electricity_meter_data(
      std::chrono::system_clock::time_point at,
      EchonetLite::ElectricityMeterData electricity_data);
  // BP35A1から受信したERXUDPイベントを処理する
  void process_erxudp(std::chrono::system_clock::time_point at,
                      const Bp35a1::ResErxudp &ev);
};
