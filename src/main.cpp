// Copyright (c) 2022 Akihiro Yamamoto.
// Licensed under the MIT License <https://spdx.org/licenses/MIT.html>
// See LICENSE file in the project root for full license information.
//
#include "Application.hpp"
#include <chrono>
#include <future>

#include <M5Unified.h>

using namespace std::chrono_literals;

// Arduinoのsetup()関数
void setup() {
  if (new Application(M5.Display)) {
    Application::getInstance()->startup();
  } else {
    M5_LOGE("out of memory");
    M5.Display.print("out of memory.");
    std::this_thread::sleep_for(1min);
    esp_system_abort("out of memory.");
  }
}

// Arduinoのloop()関数
void loop() { Application::getInstance()->task_handler(); }
