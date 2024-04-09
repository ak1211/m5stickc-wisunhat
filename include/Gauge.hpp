// Copyright (c) 2022 Akihiro Yamamoto.
// Licensed under the MIT License <https://spdx.org/licenses/MIT.html>
// See LICENSE file in the project root for full license information.
//
#pragma once
#include "Application.hpp"
#include <cstdint>
#include <functional>
#include <string>

#include <M5Unified.h>

//
// 測定値表示
//
template <typename T> class Gauge {
  using ConvertFn = std::function<std::string(std::optional<T>)>;
  uint8_t size;
  uint8_t font;
  uint16_t color;
  int16_t cursor_x;
  int16_t cursor_y;
  ConvertFn value_to_string;      // 表示用の変換関数
  std::optional<T> current_value; // 現在表示中の値
  std::optional<T> next_value;    // 次回に表示する値

public:
  Gauge(uint8_t text_size, uint8_t font, uint16_t text_color,
        std::pair<int16_t, int16_t> cursor_xy, ConvertFn converter)
      : size{text_size},
        font{font},
        color{text_color},
        cursor_x{cursor_xy.first},
        cursor_y{cursor_xy.second},
        current_value{std::nullopt},
        next_value{std::nullopt},
        value_to_string{converter} {}
  //
  Gauge &set(std::optional<T> next) {
    current_value = next_value;
    next_value = next;
    return *this;
  }
  //
  Gauge &update(bool forced_repaint = false) {
    // 値に変化がある場合のみ更新する
    if (forced_repaint || current_value != next_value) {
      //
      std::string current = value_to_string(current_value);
      std::string next = value_to_string(next_value);
      //
      M5.Display.setTextSize(size);
      // 黒色で現在表示中の文字を上書きする
      M5.Display.setTextColor(BLACK);
      M5.Display.setCursor(cursor_x, cursor_y, font);
      M5.Display.print(current.c_str());
      //
      // 現在値を表示する
      M5.Display.setTextColor(color);
      M5.Display.setCursor(cursor_x, cursor_y, font);
      M5.Display.print(next.c_str());
      // 更新
      current_value = next_value;
    }
    return *this;
  }
};
