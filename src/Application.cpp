// Copyright (c) 2022 Akihiro Yamamoto.
// Licensed under the MIT License <https://spdx.org/licenses/MIT.html>
// See LICENSE file in the project root for full license information.
//
#include "Application.hpp"
#include "StringBufWithDialogue.hpp"
#include <ArduinoJson.h>
#include <LittleFS.h>
#include <WiFi.h>
#include <ctime>
#include <esp_sntp.h>
#include <functional>
#include <future>

#include <M5Unified.h>

using namespace std::chrono;
using namespace std::chrono_literals;

Application *Application::_instance{nullptr};

//
void Application::task_handler() {
  M5.update();
  if (M5.BtnA.wasPressed()) {
    _gui.moveNext();
  }
  //
  if (WiFi.status() != WL_CONNECTED) {
    // WiFiが接続されていない場合は接続する。
    StringBufWithDialogue buf{"Connect to WiFi."};
    std::ostream ostream{&buf};
    start_wifi(ostream);
  } else {
    //
    if (_energy_meter_comm_task) {
      _energy_meter_comm_task->task_handler();
    }
    //
    if (_telemetry) {
      _telemetry->task_handler();
    }
    //
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
  }
}

//
std::optional<std::string> Application::getSettings_wifi_SSID() {
  if (_settings_json.containsKey("wifi") &&
      _settings_json["wifi"].containsKey("SSID")) {
    return _settings_json["wifi"]["SSID"];
  }
  return std::nullopt;
}

//
std::optional<std::string> Application::getSettings_wifi_password() {
  if (_settings_json.containsKey("wifi") &&
      _settings_json["wifi"].containsKey("password")) {
    return _settings_json["wifi"]["password"];
  }
  return std::nullopt;
}

//
std::optional<std::string> Application::getSettings_RouteB_id() {
  if (_settings_json.containsKey("RouteB") &&
      _settings_json["RouteB"].containsKey("id")) {
    return _settings_json["RouteB"]["id"];
  }
  return std::nullopt;
}

//
std::optional<std::string> Application::getSettings_RouteB_password() {
  if (_settings_json.containsKey("RouteB") &&
      _settings_json["RouteB"].containsKey("password")) {
    return _settings_json["RouteB"]["password"];
  }
  return std::nullopt;
}

//
std::optional<Telemetry::SensorId> Application::getSettings_SensorId() {
  if (_settings_json.containsKey("SensorId")) {
    std::string str = _settings_json["SensorId"];
    return Telemetry::SensorId{std::move(str)};
  }
  return std::nullopt;
}

//
std::optional<Telemetry::DeviceId> Application::getSettings_DeviceId() {
  if (_settings_json.containsKey("DeviceId")) {
    std::string str = _settings_json["DeviceId"];
    return Telemetry::DeviceId{std::move(str)};
  }
  return std::nullopt;
}

//
std::optional<Telemetry::AwsIotEndpoint>
Application::getSettings_AwsIoT_Endpoint() {
  if (_settings_json.containsKey("AwsIoT") &&
      _settings_json["AwsIoT"].containsKey("Endpoint")) {
    std::string str = _settings_json["AwsIoT"]["Endpoint"];
    return Telemetry::AwsIotEndpoint{std::move(str)};
  }
  return std::nullopt;
}

//
std::optional<std::string> Application::getSettings_AwsIoT_root_ca_file() {
  if (_settings_json.containsKey("AwsIoT") &&
      _settings_json["AwsIoT"].containsKey("root_ca_file")) {
    return _settings_json["AwsIoT"]["root_ca_file"];
  }
  return std::nullopt;
}

//
std::optional<std::string> Application::getSettings_AwsIoT_certificate_file() {
  if (_settings_json.containsKey("AwsIoT") &&
      _settings_json["AwsIoT"].containsKey("certificate_file")) {
    return _settings_json["AwsIoT"]["certificate_file"];
  }
  return std::nullopt;
}

//
std::optional<std::string> Application::getSettings_AwsIoT_private_key_file() {
  if (_settings_json.containsKey("AwsIoT") &&
      _settings_json["AwsIoT"].containsKey("private_key_file")) {
    return _settings_json["AwsIoT"]["private_key_file"];
  }
  return std::nullopt;
}

// 起動
bool Application::startup() {
  // 起動シーケンス
  std::vector<std::function<bool(std::ostream &)>> startup_sequence{
      std::bind(&Application::read_settings_json, this, std::placeholders::_1),
      std::bind(&Application::start_wifi, this, std::placeholders::_1),
      std::bind(&Application::synchronize_ntp, this, std::placeholders::_1),
      std::bind(&Application::start_telemetry, this, std::placeholders::_1),
      std::bind(&Application::start_energy_meter_communication, this,
                std::placeholders::_1),
  };

  // initializing M5Stick-C with M5Unified
  auto m5_config = M5.config();
  M5.begin(m5_config);

  // ログの設定
  M5.Log.setLogLevel(m5::log_target_serial, ESP_LOG_DEBUG);
  M5.Log.setEnableColor(m5::log_target_serial, true);

  // BP35A1用シリアルポート
  Serial2.begin(115200, SERIAL_8N1, CommPortRx, CommPortTx);

  // file system init
  if (LittleFS.begin(true)) {
    M5_LOGI("filesystem status : %lu / %lu.", LittleFS.usedBytes(),
            LittleFS.totalBytes());
  } else {
    M5_LOGE("filesystem inititalize failed.");
    M5.Display.print("filesystem inititalize failed.");
    std::this_thread::sleep_for(1min);
    esp_system_abort("filesystem inititalize failed.");
  }

  // Display
  M5.Display.setColorDepth(LV_COLOR_DEPTH);
  M5.Display.setBrightness(160);

  // init GUI
  if (!_gui.begin()) {
    M5.Display.print("GUI inititalize failed.");
    return false;
  }

  // create RTOS task for LVGL
  xTaskCreatePinnedToCore(
      [](void *arg) -> void {
        while (true) {
          lv_task_handler();
          std::this_thread::sleep_for(10ms);
        }
      },
      "Task:LVGL", LVGL_TASK_STACK_SIZE, nullptr, 5, &_rtos_lvgl_task_handle,
      ARDUINO_RUNNING_CORE);

  //
  StringBufWithDialogue buf{"HELLO"};
  std::ostream ostream(&buf);

  // 起動
  for (auto it = startup_sequence.begin(); it != startup_sequence.end(); ++it) {
    std::this_thread::sleep_for(100ms);
    auto func = *it;
    //
    if (func(ostream)) {
      /* nothing to do */
    } else {
      std::this_thread::sleep_for(1min);
      esp_system_abort("startup failure");
    }
  }
  //
  _gui.startUi();

  // create RTOS task for this Application
  xTaskCreatePinnedToCore(
      [](void *user_context) -> void {
        assert(user_context);
        while (true) {
          Application *app = static_cast<Application *>(user_context);
          assert(app);
          app->task_handler();
        }
      },
      "Task:Application", APPLICATION_TASK_STACK_SIZE, this, 1,
      &_rtos_application_task_handle, ARDUINO_RUNNING_CORE);

  return true;
}

//
bool Application::read_settings_json(std::ostream &os) {
  {
    std::ostringstream ss;
    ss << "Read settings json file is \"" << SETTINGS_FILE_PATH << "\"";
    os << ss.str() << std::endl;
    M5_LOGI("%s", ss.str().c_str());
  }
  //
  if (auto file = LittleFS.open(SETTINGS_FILE_PATH.data()); file) {
    DeserializationError error = deserializeJson(_settings_json, file);
    file.close();
    if (error == DeserializationError::Ok) {
      M5_LOGI("read settings file: ok");
    } else {
      std::ostringstream ss;
      ss << "Error; Read \"" << SETTINGS_FILE_PATH << "\" file.";
      os << ss.str() << std::endl;
      M5_LOGE("%s", ss.str().c_str());
      goto error_abort;
    }
  } else {
    std::ostringstream ss;
    ss << "Error; Open \"" << SETTINGS_FILE_PATH << "\" file.";
    os << ss.str() << std::endl;
    M5_LOGE("%s", ss.str().c_str());
    goto error_abort;
  }

  // 設定ファイルの確認
  {
    auto check = [&os](std::string name, bool condition_under_test) -> bool {
      std::ostringstream ss;
      if (condition_under_test) {
        ss << "check \"" << name << "\" is good";
        os << ss.str() << std::endl;
        M5_LOGE("%s", ss.str().c_str());
        return true;
      } else {
        ss << "Error; \"" << name << "\" is undefined";
        os << ss.str() << std::endl;
        M5_LOGE("%s", ss.str().c_str());
        return false;
      }
    };
    //
#define CHECK(_x, _y)                                                          \
  do {                                                                         \
    if (!check(_x, static_cast<bool>(_y)))                                     \
      goto error_abort;                                                        \
  } while (0)
    //
    CHECK("wifi SSID", getSettings_wifi_SSID());
    CHECK("wifi password", getSettings_wifi_password());
    CHECK("RouteB id", getSettings_RouteB_id());
    CHECK("RouteB password", getSettings_RouteB_password());
    CHECK("DeviceId", getSettings_DeviceId());
    CHECK("AwsIoT Endpoint", getSettings_AwsIoT_Endpoint());
    CHECK("AwsIoT root_ca_file", getSettings_AwsIoT_root_ca_file());
    CHECK("AwsIoT certificate_file", getSettings_AwsIoT_certificate_file());
    CHECK("AwsIoT private_key_file", getSettings_AwsIoT_private_key_file());
  }
#undef CHECK
  // 設定ファイルに書いてあるファイルを読む
  {
    auto read_contents = [&os](std::string path) -> std::optional<std::string> {
      std::ostringstream ss;
      if (auto file = LittleFS.open(path.c_str()); file) {
        auto bytes = file.available();
        std::string buff(bytes, '\0');
        file.readBytes(buff.data(), bytes);
        buff.shrink_to_fit();
        file.close();
        //
        ss << "read \"" << path << "\" file seccess";
        os << ss.str() << std::endl;
        M5_LOGI("%s", ss.str().c_str());
        return buff;
      } else {
        ss << "Error; \"" << path << "\" file read error";
        os << ss.str() << std::endl;
        M5_LOGE("%s", ss.str().c_str());
        return std::nullopt;
      }
    };
    if (auto path = getSettings_AwsIoT_root_ca_file(); path) {
      if (auto opt_contents = read_contents(*path); opt_contents) {
        _aws_iot_root_ca = Telemetry::AwsIotRootCa{*opt_contents};
      } else {
        _aws_iot_root_ca = std::nullopt;
      }
    }
    if (auto path = getSettings_AwsIoT_certificate_file(); path) {
      if (auto opt_contents = read_contents(*path); opt_contents) {
        _aws_iot_certificate = Telemetry::AwsIotCertificate{*opt_contents};
      } else {
        _aws_iot_certificate = std::nullopt;
      }
    }
    if (auto path = getSettings_AwsIoT_private_key_file(); path) {
      if (auto opt_contents = read_contents(*path); opt_contents) {
        _aws_iot_private_key = Telemetry::AwsIotPrivateKey{*opt_contents};
      } else {
        _aws_iot_private_key = std::nullopt;
      }
    }
  }

  return true;
error_abort:
  std::this_thread::sleep_for(1min);
  esp_system_abort("Setting file read error.");
  return false;
}

//
bool Application::start_wifi(std::ostream &os) {
  {
    std::ostringstream ss;
    ss << "connect to WiFi";
    os << ss.str() << std::endl;
    M5_LOGI("%s", ss.str().c_str());
  }
  //
  std::string ssid;
  std::string password;
  // guard
  if (getSettings_wifi_SSID()) {
    ssid = getSettings_wifi_SSID().value();
  } else {
    std::ostringstream ss;
    ss << "wifi SSID not set";
    os << ss.str() << std::endl;
    M5_LOGE("%s", ss.str().c_str());
    return false;
  }
  if (getSettings_wifi_password()) {
    password = getSettings_wifi_password().value();
  } else {
    std::ostringstream ss;
    ss << "wifi password not set";
    os << ss.str() << std::endl;
    M5_LOGE("%s", ss.str().c_str());
    return false;
  }
  //
  {
    std::ostringstream ss;
    ss << "connect to WiFi AP SSID: \""s << ssid << "\""s;
    os << ss.str() << std::endl;
    M5_LOGI("%s", ss.str().c_str());
  }
  //  WiFi with Station mode
  WiFi.onEvent(wifi_event_callback);
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid.c_str(), password.c_str());
  // WiFi APとの接続待ち
  auto timeover{steady_clock::now() + TIMEOUT};
  while (WiFi.status() != WL_CONNECTED && steady_clock::now() < timeover) {
    std::this_thread::sleep_for(100ms);
  }

  return WiFi.status() == WL_CONNECTED;
}

// インターネット時間サーバと同期する
bool Application::synchronize_ntp(std::ostream &os) {
  //
  if (_time_is_synced) {
    return true;
  }
  //
  os << "synchronize time server." << std::endl;
  M5_LOGI("synchronize time server.");
  // インターネット時間に同期する
  sntp_set_time_sync_notification_cb(time_sync_notification_callback);
  configTzTime(TZ_TIME_ZONE.data(), "time.cloudflare.com",
               "ntp.jst.mfeed.ad.jp", "ntp.nict.jp");
  //
  os << "waiting for time sync" << std::endl;
  M5_LOGI("waiting for time sync");
  //
  while (!_time_is_synced) {
    std::this_thread::sleep_for(100ms);
  }
  //
  return true;
}

//
bool Application::start_telemetry(std::ostream &os) {
  {
    std::ostringstream ss;
    ss << "start telemetry";
    os << ss.str() << std::endl;
    M5_LOGI("%s", ss.str().c_str());
  }
  //
  std::optional<Telemetry::SensorId> opt_sensorId;
  std::optional<Telemetry::DeviceId> opt_deviceId;
  std::optional<Telemetry::AwsIotEndpoint> opt_awsiot_endpoint;
  // guard
  if (opt_sensorId = getSettings_SensorId(); opt_sensorId) {
    /* nothing to do */
  } else {
    std::ostringstream ss;
    ss << "SensorId not set";
    os << ss.str() << std::endl;
    M5_LOGE("%s", ss.str().c_str());
    return false;
  }
  if (opt_deviceId = getSettings_DeviceId(); opt_deviceId) {
    /* nothing to do */
  } else {
    std::ostringstream ss;
    ss << "DeviceId not set";
    os << ss.str() << std::endl;
    M5_LOGE("%s", ss.str().c_str());
    return false;
  }
  if (opt_awsiot_endpoint = getSettings_AwsIoT_Endpoint();
      opt_awsiot_endpoint) {
    /* nothing to do */
  } else {
    std::ostringstream ss;
    ss << "AWS IoT Endpoint not set";
    os << ss.str() << std::endl;
    M5_LOGE("%s", ss.str().c_str());
    return false;
  }
  if (!_aws_iot_certificate) {
    std::ostringstream ss;
    ss << "AWS IoT Certificate not set";
    os << ss.str() << std::endl;
    M5_LOGE("%s", ss.str().c_str());
    return false;
  }
  if (!_aws_iot_private_key) {
    std::ostringstream ss;
    ss << "AWS IoT PrivateKey not set";
    os << ss.str() << std::endl;
    M5_LOGE("%s", ss.str().c_str());
    return false;
  }
  if (!_aws_iot_root_ca) {
    std::ostringstream ss;
    ss << "AWS IoT RootCA not set";
    os << ss.str() << std::endl;
    M5_LOGE("%s", ss.str().c_str());
    return false;
  }

  //
  _telemetry.reset(new Telemetry{*opt_deviceId, *opt_sensorId,
                                 *opt_awsiot_endpoint, *_aws_iot_root_ca,
                                 *_aws_iot_certificate, *_aws_iot_private_key});
  //
  if (_telemetry) {
    // AWS IoTへ接続確立を試みる
    return _telemetry->begin(os, TIMEOUT);
  }

  return false;
}

//
bool Application::start_energy_meter_communication(std::ostream &os) {
  {
    std::ostringstream ss;
    ss << "start meter communication";
    os << ss.str() << std::endl;
    M5_LOGI("%s", ss.str().c_str());
  }
  //
  std::string rb_id;
  std::string rb_password;
  // guard
  if (getSettings_RouteB_id()) {
    rb_id = getSettings_RouteB_id().value();
  } else {
    std::ostringstream ss;
    ss << "Route B ID not set";
    os << ss.str() << std::endl;
    M5_LOGE("%s", ss.str().c_str());
    return false;
  }
  if (getSettings_RouteB_password()) {
    rb_password = getSettings_RouteB_password().value();
  } else {
    std::ostringstream ss;
    ss << "Route B Password not set";
    os << ss.str() << std::endl;
    M5_LOGE("%s", ss.str().c_str());
    return false;
  }

  //
  _energy_meter_comm_task.reset(
      new EnergyMeterCommTask{Serial2, rb_id, rb_password});
  //
  if (_energy_meter_comm_task) {
    // スマートメーターへ接続確立を試みる
    return _energy_meter_comm_task->begin(os, TIMEOUT);
  }

  return true;
}

//
void Application::time_sync_notification_callback(struct timeval *time_val) {
  Application::getInstance()->_time_is_synced = true;
}

//
void Application::wifi_event_callback(WiFiEvent_t event) {
  switch (event) {
  case SYSTEM_EVENT_AP_START:
    M5_LOGI("AP Started");
    break;
  case SYSTEM_EVENT_AP_STOP:
    M5_LOGI("AP Stopped");
    break;
  case SYSTEM_EVENT_STA_START:
    M5_LOGI("STA Started");
    break;
  case SYSTEM_EVENT_STA_CONNECTED:
    M5_LOGI("STA Connected");
    break;
  case SYSTEM_EVENT_AP_STA_GOT_IP6: {
    auto localipv6 = WiFi.localIPv6();
    M5_LOGI("STA IPv6: %s", localipv6.toString());
  } break;
  case SYSTEM_EVENT_STA_GOT_IP: {
    auto localip = WiFi.localIP();
    M5_LOGI("STA IPv4: %s", localip.toString());
  } break;
  case SYSTEM_EVENT_STA_DISCONNECTED:
    M5_LOGI("STA Disconnected");
    break;
  case SYSTEM_EVENT_STA_STOP:
    M5_LOGI("STA Stopped");
    break;
  default:
    break;
  }
}
