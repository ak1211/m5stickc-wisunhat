// Copyright (c) 2022 Akihiro Yamamoto.
// Licensed under the MIT License <https://spdx.org/licenses/MIT.html>
// See LICENSE file in the project root for full license information.
//
#pragma once
#include "Bp35a1.hpp"
#include <chrono>
#include <queue>
#include <tuple>

//
// スマートメーターとの通信
//
class EnergyMeterCommTask final {
public:
  EnergyMeterCommTask(Stream &port, Bp35a1::SmartMeterIdentifier identifier)
      : _comm_port{port},
        _identifier{identifier},
        _pana_session_established{false} {}
  //
  bool begin(std::chrono::system_clock::time_point nowtp);
  //
  void task_handler(std::chrono::system_clock::time_point nowtp);

private:
  //
  std::chrono::system_clock::time_point _next_send_request_in_tp{};
  // メッセージ受信バッファ
  std::queue<std::pair<std::chrono::system_clock::time_point, Bp35a1::Response>>
      _received_message_fifo{};
  // BP35A1と会話できるポート
  Stream &_comm_port;
  // スマート電力量計のＢルート識別子
  Bp35a1::SmartMeterIdentifier _identifier;
  // Echonet Lite PANA session
  bool _pana_session_established{false};
  //
  void receive_from_port(std::chrono::system_clock::time_point nowtp);
  // スマートメーターに要求を送る
  void send_request_to_port(std::chrono::system_clock::time_point nowtp);
  // スマートメーターに最初の要求を出す
  void send_first_request();
  // スマートメーターに定期的な要求を出す
  void send_periodical_request();
  // BP35A1から受信したイベントを処理する
  void process_event(const Bp35a1::ResEvent &ev);
  // ノードプロファイルクラスのEchonetLiteフレームを処理する
  void process_node_profile_class_frame(const EchonetLiteFrame &frame);
  // BP35A1から受信したERXUDPイベントを処理する
  void process_erxudp(std::chrono::system_clock::time_point at,
                      const Bp35a1::ResErxudp &ev);
};
