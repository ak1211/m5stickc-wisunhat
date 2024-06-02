// Copyright (c) 2022 Akihiro Yamamoto.
// Licensed under the MIT License <https://spdx.org/licenses/MIT.html>
// See LICENSE file in the project root for full license information.
//
#pragma once
#include "Bp35a1_TypeDefine.hpp"
#include "EchonetLite.hpp"
#include <chrono>
#include <optional>
#include <ostream>
#include <string>
#include <variant>

//
//
//
class Bp35a1Class {
public:
  constexpr static auto RETRY_TIMEOUT = std::chrono::seconds{10};
  // BP35A1から受け取ったイベント
  using Response =
      std::variant<Bp35a1::ResEvent, Bp35a1::ResEpandesc, Bp35a1::ResErxudp>;

public:
  Bp35a1Class(Stream &comm_port) : _comm_port{comm_port} {}
  // BP35A1を起動してアクティブスキャンを開始する
  std::optional<Bp35a1::SmartMeterIdentifier>
  startup_and_find_meter(std::ostream &os, const std::string &route_b_id,
                         const std::string &route_b_password,
                         std::chrono::seconds timeout);
  // 接続(PANA認証)要求を送る
  bool connect(std::ostream &os, Bp35a1::SmartMeterIdentifier smart_meter_ident,
               std::chrono::seconds timeout);
  // 要求を送る
  bool send_request(
      const Bp35a1::SmartMeterIdentifier &smart_meter_ident,
      EchonetLiteTransactionId tid,
      const std::vector<SmartElectricEnergyMeter::EchonetLiteEPC> epcs);
  // 受信
  std::optional<Response> receive_response();

private:
  // BP35A1と会話できるポート
  Stream &_comm_port;
  // 受信メッセージを破棄する
  void clear_read_buffer();
  // ストリームからsepで区切られたトークンを得る
  std::pair<std::string, std::string> get_token(int sep);
  // CRLFを付けてストリームに1行書き込む関数
  void write_with_crln(const std::string &line);
  // 成功ならtrue, それ以外ならfalse
  bool has_ok(std::chrono::seconds timeout);
  // ipv6 アドレスを受け取る関数
  std::optional<Bp35a1::IPv6Addr>
  get_ipv6_address(const std::string &addr, std::chrono::seconds timeout);
  // アクティブスキャンを実行する
  std::optional<Bp35a1::ResEpandesc>
  do_active_scan(std::ostream &os, std::chrono::seconds timeout);
};