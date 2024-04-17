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
  lv_style_t dialogue_style{};
  lv_obj_t *dialogue_obj{nullptr};
  //
  struct TitlePart {
    lv_style_t style{};
    lv_obj_t *label{nullptr};
    TitlePart(lv_obj_t *parent);
  };
  //
  struct MessagePart {
    lv_style_t style{};
    lv_obj_t *label{nullptr};
    MessagePart(lv_obj_t *parent, lv_obj_t *above_obj);
  };
  //
  std::unique_ptr<TitlePart> title{};
  std::unique_ptr<MessagePart> message{};

public:
  Dialogue(const std::string &title_text, lv_obj_t *parent = lv_scr_act());
  virtual ~Dialogue() { lv_obj_del(dialogue_obj); }
  void setMessage(const std::string &text) {
    if (message) {
      lv_label_set_text(message->label, text.c_str());
    }
  }
  void info(const std::string &text) { setMessage(text); }
  void error(const std::string &text) { setMessage("#ff0000 " + text + "#"); }
};

// init argument for "lv_tileview_add_tile()"
using InitArg = std::tuple<lv_obj_t *, uint8_t, uint8_t, lv_dir_t>;

//
//
//
class BasicTile {
protected:
  //
  struct TitlePart {
    lv_style_t style{};
    lv_obj_t *label{nullptr};
    TitlePart(lv_obj_t *parent);
  };
  //
  struct ValuePart {
    lv_style_t style{};
    lv_obj_t *label{nullptr};
    ValuePart(lv_obj_t *parent, lv_obj_t *above_obj);
  };
  //
  struct TimePart {
    lv_style_t style{};
    lv_obj_t *label{nullptr};
    TimePart(lv_obj_t *parent, lv_obj_t *above_obj);
  };
  //
  std::unique_ptr<TitlePart> title_part{};
  std::unique_ptr<ValuePart> value_part{};
  std::unique_ptr<TimePart> time_part{};
  lv_obj_t *tile_obj{nullptr};

public:
  BasicTile(BasicTile &&) = delete;
  BasicTile &operator=(const BasicTile &) = delete;
  //
  BasicTile(InitArg init) noexcept;
  virtual ~BasicTile() noexcept;
  void setActiveTile(lv_obj_t *tileview) noexcept;
  virtual void timerHook() noexcept = 0;
};

//
// 測定値表示
//
class InstantWatt : public BasicTile {
public:
  InstantWatt(InitArg init) noexcept;
  virtual void timerHook() noexcept override;
  void setValue(const std::optional<Repository::InstantWatt> iw);
};

//
// 測定値表示
//
class InstantAmpere : public BasicTile {
public:
  InstantAmpere(InitArg init) noexcept;
  virtual void timerHook() noexcept override;
  void setValue(const std::optional<Repository::InstantAmpere> ia);
};

//
// 測定値表示
//
class CumlativeWattHour : public BasicTile {
public:
  CumlativeWattHour(InitArg init) noexcept;
  virtual void timerHook() noexcept override;
  void setValue(const std::optional<Repository::CumlativeWattHour> iw);
};
} // namespace Widget

//
//
//
class Gui {
  inline static Gui *_instance{nullptr};
  constexpr static uint16_t MILLISECONDS_OF_PERIODIC_TIMER = 100;

public:
  //
  Gui(M5GFX &gfx) : gfx{gfx}, active_tile_itr{tiles.begin()} {
    if (_instance) {
      delete _instance;
    }
    _instance = this;
  }
  //
  virtual ~Gui() { lv_obj_del(tileview); }
  //
  static Gui *getInstance() noexcept { return _instance; }
  //
  bool begin() noexcept;
  //
  void startUi() noexcept;
  //
  void moveNext() noexcept {
    if (active_tile_itr != tiles.end()) {
      ++active_tile_itr;
    }
    if (active_tile_itr == tiles.end()) {
      active_tile_itr = tiles.begin();
    }
    if (auto ptr = active_tile_itr->get(); ptr) {
      ptr->setActiveTile(tileview);
    }
  }

private:
  M5GFX &gfx;
  // LVGL use area
  struct {
    // LVGL draw buffer
    std::unique_ptr<lv_color_t[]> draw_buf_1;
    std::unique_ptr<lv_color_t[]> draw_buf_2;
    lv_disp_draw_buf_t draw_buf_dsc;
    lv_disp_drv_t disp_drv;
  } lvgl_use;
  // LVGL timer
  lv_timer_t *periodic_timer{nullptr};
  // LVGL tileview object
  lv_style_t tileview_style{};
  lv_obj_t *tileview{nullptr};
  // tile widget
  using TileVector = std::vector<std::unique_ptr<Widget::BasicTile>>;
  TileVector tiles{};
  TileVector::iterator active_tile_itr{};
};
