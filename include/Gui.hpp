// Copyright (c) 2022 Akihiro Yamamoto.
// Licensed under the MIT License <https://spdx.org/licenses/MIT.html>
// See LICENSE file in the project root for full license information.
//
#pragma once
#include "Repository.hpp"
#include <algorithm>
#include <chrono>
#include <lvgl.h>
#include <memory>
#include <optional>
#include <string>

#include <M5Unified.h>

namespace Widget {
using namespace std::chrono;

//
//
//
class Dialogue {
public:
  Dialogue(const std::string &title_text);
  void setMessage(const std::string &text);
  void info(const std::string &text);
  void error(const std::string &text);

private:
  lv_style_t _dialogue_style{};
  std::shared_ptr<lv_obj_t> _dialogue_obj;
  //
  lv_style_t _title_label_style{};
  std::shared_ptr<lv_obj_t> _title_label_obj;
  //
  lv_style_t _message_label_style{};
  std::shared_ptr<lv_obj_t> _message_label_obj;
};

// init argument for "lv_tileview_add_tile()"
using InitArg =
    std::tuple<std::shared_ptr<lv_obj_t>, uint8_t, uint8_t, lv_dir_t>;

//
//
//
class TileBase {
public:
  TileBase(InitArg init);
  //
  bool isActiveTile() const;
  //
  void setActiveTile();
  //
  virtual void update() = 0;

protected:
  std::shared_ptr<lv_obj_t> _tileview_obj;
  //
  std::shared_ptr<lv_obj_t> _tile_obj;
  //
  const lv_font_t &_title_label_font{lv_font_montserrat_20};
  lv_style_t _title_label_style{};
  std::shared_ptr<lv_obj_t> _title_label_obj;
  //
  const lv_font_t &_value_label_font{lv_font_montserrat_46};
  lv_style_t _value_label_style{};
  std::shared_ptr<lv_obj_t> _value_label_obj;
  //
  const lv_font_t &_time_label_font{lv_font_montserrat_20};
  lv_style_t _time_label_style{};
  std::shared_ptr<lv_obj_t> _time_label_obj;
};

//
// 測定値表示
//
class InstantWatt : public TileBase {
public:
  InstantWatt(InitArg init);
  //
  void showValue(const std::optional<Repository::InstantWatt> iw);
  //
  virtual void update() override;
};

//
// 測定値表示
//
class InstantAmpere : public TileBase {
public:
  InstantAmpere(InitArg init);
  //
  void showValue(const std::optional<Repository::InstantAmpere> ia);
  //
  virtual void update() override;
};

//
// 測定値表示
//
class CumlativeWattHour : public TileBase {
public:
  CumlativeWattHour(InitArg init);
  //
  void showValue(const std::optional<Repository::CumlativeWattHour> cwh);
  //
  virtual void update() override;
};
} // namespace Widget

//
//
//
class Gui {
  inline static Gui *_instance{nullptr};
  constexpr static uint16_t MILLISECONDS_OF_PERIODIC_TIMER = 125;

public:
  //
  Gui(M5GFX &gfx) : _gfx{gfx}, _active_tile_itr{_tiles.begin()} {
    if (_instance) {
      delete _instance;
    }
    _instance = this;
  }
  //
  static Gui *getInstance() { return _instance; }
  //
  bool begin();
  //
  void startUi();
  //
  void home() {
    _active_tile_itr = _tiles.begin();
    if (auto ptr = _active_tile_itr->get(); ptr) {
      ptr->setActiveTile();
    }
  }
  //
  void moveNext() {
    if (_active_tile_itr != _tiles.end()) {
      ++_active_tile_itr;
    }
    if (_active_tile_itr == _tiles.end()) {
      _active_tile_itr = _tiles.begin();
    }
    if (auto ptr = _active_tile_itr->get(); ptr) {
      ptr->setActiveTile();
    }
  }

private:
  M5GFX &_gfx;
  // LVGL timer
  lv_timer_t *periodic_timer{nullptr};
  // LVGL tileview object
  lv_style_t _tileview_style{};
  std::shared_ptr<lv_obj_t> _tileview;
  // tile widget
  using TileVector = std::vector<std::unique_ptr<Widget::TileBase>>;
  TileVector _tiles{};
  TileVector::iterator _active_tile_itr{};

private:
  constexpr static auto LVGL_BUFFER_ONE_SIZE_OF_BYTES = size_t{2048};
  // LVGL use area
  struct LvglUseArea {
    // LVGL draw buffer
    static lv_color_t
        draw_buf_1[LVGL_BUFFER_ONE_SIZE_OF_BYTES / sizeof(lv_color_t)];
    static lv_color_t
        draw_buf_2[LVGL_BUFFER_ONE_SIZE_OF_BYTES / sizeof(lv_color_t)];
    lv_disp_draw_buf_t draw_buf_dsc;
    lv_disp_drv_t disp_drv;
  } _lvgl_use;
  //
  static void lvgl_use_display_flush_callback(lv_disp_drv_t *disp_drv,
                                              const lv_area_t *area,
                                              lv_color_t *color_p);
};
