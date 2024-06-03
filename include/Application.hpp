// Copyright (c) 2022 Akihiro Yamamoto.
// Licensed under the MIT License <https://spdx.org/licenses/MIT.html>
// See LICENSE file in the project root for full license information.
//
#pragma once
#include "EchonetLite.hpp"
#include "ElectricityMeterCommTask.hpp"
#include "Gui.hpp"
#include "Repository.hpp"
#include "Telemetry.hpp"
#include <ArduinoJson.h>
#include <chrono>
#include <lvgl.h>
#include <memory>
#include <optional>
#include <tuple>

//
//
//
class Application final {
public:
  // BP35A1と会話できるポート番号
  constexpr static auto CommPortRx{26};
  constexpr static auto CommPortTx{0};
  //
  constexpr static auto LVGL_TASK_STACK_SIZE = size_t{8192};
  //
  constexpr static auto APPLICATION_TASK_STACK_SIZE = size_t{8192};
  //
  constexpr static auto TIMEOUT = std::chrono::seconds{60};
  // time zone = Asia_Tokyo(UTC+9)
  constexpr static auto TZ_TIME_ZONE = std::string_view{"JST-9"};
  //
  constexpr static std::string_view SETTINGS_FILE_PATH{"/settings.json"};

public:
  Application(const Application &) = delete;
  Application &operator=(const Application &) = delete;
  Application(Application &&) = delete;
  Application &operator=(Application &&) = delete;
  Application(M5GFX &gfx) : _gui{gfx} {
    if (_instance) {
      esp_system_abort("multiple Application started.");
    }
    _instance = this;
  }
  //
  void task_handler();
  // 起動
  bool startup();
  //
  static Repository::ElectricPowerData &getElectricPowerData() {
    return getInstance()->_electric_power_data;
  }
  //
  static std::shared_ptr<Telemetry> getTelemetry() {
    return getInstance()->_telemetry;
  }
  //
  static std::shared_ptr<ElectricityMeterCommTask> getEnergyMeterCommTask() {
    return getInstance()->_electricity_meter_comm_task;
  }
  //
  static Gui &getGui() { return getInstance()->_gui; }
  //
  static Application *getInstance() {
    if (_instance == nullptr) {
      esp_system_abort("Application is not started.");
    }
    return _instance;
  }

private:
  static Application *_instance;
  // インターネット時間サーバーに同期しているか
  bool _time_is_synced{false};
  //
  Repository::ElectricPowerData _electric_power_data;
  // JSON形式設定ファイル
  JsonDocument _settings_json;
  // AWS設定
  std::optional<Telemetry::AwsIotRootCa> _aws_iot_root_ca;
  std::optional<Telemetry::AwsIotCertificate> _aws_iot_certificate;
  std::optional<Telemetry::AwsIotPrivateKey> _aws_iot_private_key;
  //
  std::optional<std::string> getSettings_wifi_SSID();
  std::optional<std::string> getSettings_wifi_password();
  std::optional<std::string> getSettings_RouteB_id();
  std::optional<std::string> getSettings_RouteB_password();
  std::optional<Telemetry::SensorId> getSettings_SensorId();
  std::optional<Telemetry::DeviceId> getSettings_DeviceId();
  std::optional<Telemetry::AwsIotEndpoint> getSettings_AwsIoT_Endpoint();
  std::optional<std::string> getSettings_AwsIoT_root_ca_file();
  std::optional<std::string> getSettings_AwsIoT_certificate_file();
  std::optional<std::string> getSettings_AwsIoT_private_key_file();
  //
  Gui _gui;
  //
  std::shared_ptr<Telemetry> _telemetry;
  //
  std::shared_ptr<ElectricityMeterCommTask> _electricity_meter_comm_task;
  //
  TaskHandle_t _rtos_lvgl_task_handle{};
  //
  TaskHandle_t _rtos_application_task_handle{};
  //
  bool read_settings_json(std::ostream &os);
  //
  bool start_wifi(std::ostream &os);
  // インターネット時間サーバと同期する
  bool synchronize_ntp(std::ostream &os);
  //
  bool start_telemetry(std::ostream &os);
  //
  bool start_electricity_meter_communication(std::ostream &os);
  //
  static void time_sync_notification_callback(struct timeval *time_val);
  //
  static void wifi_event_callback(WiFiEvent_t event);
};
