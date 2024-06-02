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

namespace Bp35a1 {
constexpr static auto RETRY_TIMEOUT = std::chrono::seconds{10};

// 受信メッセージを破棄する
extern void clear_read_buffer(Stream &commport);

// ストリームからsepで区切られたトークンを得る
extern std::pair<std::string, std::string> get_token(Stream &commport, int sep);

// CRLFを付けてストリームに1行書き込む関数
extern void write_with_crln(Stream &commport, const std::string &line);

// 成功ならtrue, それ以外ならfalse
extern bool has_ok(Stream &commport, std::chrono::seconds timeout);

// BP35A1から受け取ったイベント
using Response = std::variant<ResEvent, ResEpandesc, ResErxudp>;

// ipv6 アドレスを受け取る関数
extern std::optional<IPv6Addr> get_ipv6_address(Stream &commport,
                                                std::chrono::seconds timeout,
                                                const std::string &addr);

// 受信
extern std::optional<Response> receive_response(Stream &commport);

// 要求を送る
extern bool
send_request(Stream &commport, const SmartMeterIdentifier &smart_meter_ident,
             EchonetLiteTransactionId tid,
             const std::vector<SmartElectricEnergyMeter::EchonetLiteEPC> epcs);

// 接続(PANA認証)要求を送る
extern bool connect(std::ostream &os, std::chrono::seconds timeout,
                    Stream &commport, SmartMeterIdentifier smart_meter_ident);

// アクティブスキャンを実行する
extern std::optional<ResEpandesc> do_active_scan(std::ostream &os,
                                                 Stream &commport,
                                                 std::chrono::seconds timeout);

// BP35A1を起動してアクティブスキャンを開始する
extern std::optional<SmartMeterIdentifier> startup_and_find_meter(
    std::ostream &os, Stream &commport, const std::string &route_b_id,
    const std::string &route_b_password, std::chrono::seconds timeout);
} // namespace Bp35a1