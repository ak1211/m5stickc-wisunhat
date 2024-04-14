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
  lv_obj_t *dialogue{nullptr};
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
  virtual ~Dialogue() { lv_obj_del(dialogue); }
  void setMessage(const std::string &text) {
    if (message) {
      lv_label_set_text(message->label, text.c_str());
    }
  }
  void info(const std::string &text) { setMessage(text); }
  void error(const std::string &text) { setMessage("#ff0000 " + text + "#"); }
};

//
//
//
struct TileBase {
  virtual void setActiveTile(lv_obj_t *tileview) noexcept = 0;
  virtual void timerHook() noexcept = 0;
};

// init argument for "lv_tileview_add_tile()"
using InitArg = std::tuple<lv_obj_t *, uint8_t, uint8_t, lv_dir_t>;

//
// 測定値表示
//
class InstantWatt : public TileBase {
  lv_obj_t *tile{nullptr};
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
    void setValue(const std::optional<Repository::InstantWatt> &);
  };
  //
  struct TimePart {
    lv_style_t style{};
    lv_obj_t *label{nullptr};
    TimePart(lv_obj_t *parent, lv_obj_t *above_obj);
    void setValue(const std::optional<Repository::InstantWatt> &);
  };
  //
  std::unique_ptr<TitlePart> title{};
  std::unique_ptr<ValuePart> value{};
  std::unique_ptr<TimePart> time{};

public:
  InstantWatt(InitArg init) noexcept;
  InstantWatt(InstantWatt &&) = delete;
  InstantWatt &operator=(const InstantWatt &) = delete;
  virtual ~InstantWatt() noexcept { lv_obj_del(tile); }
  virtual void setActiveTile(lv_obj_t *tileview) noexcept override {
    lv_obj_set_tile(tileview, tile, LV_ANIM_OFF);
  }
  virtual void timerHook() noexcept override;
};

//
// 測定値表示
//
class InstantAmpere : public TileBase {
  lv_obj_t *tile{nullptr};
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
    void setValue(const std::optional<Repository::InstantAmpere> &);
  };
  //
  struct TimePart {
    lv_style_t style{};
    lv_obj_t *label{nullptr};
    TimePart(lv_obj_t *parent, lv_obj_t *above_obj);
    void setValue(const std::optional<Repository::InstantAmpere> &);
  };
  //
  std::unique_ptr<TitlePart> title{};
  std::unique_ptr<ValuePart> value{};
  std::unique_ptr<TimePart> time{};

public:
  InstantAmpere(InitArg init) noexcept;
  InstantAmpere(InstantAmpere &&) = delete;
  InstantAmpere &operator=(const InstantAmpere &) = delete;
  virtual ~InstantAmpere() noexcept { lv_obj_del(tile); }
  virtual void setActiveTile(lv_obj_t *tileview) noexcept override {
    lv_obj_set_tile(tileview, tile, LV_ANIM_OFF);
  }
  virtual void timerHook() noexcept override;
};

//
// 測定値表示
//
class CumlativeWattHour : public TileBase {
  lv_obj_t *tile{nullptr};
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
    void setValue(const std::optional<Repository::CumlativeWattHour> &);
  };
  //
  struct TimePart {
    lv_style_t style{};
    lv_obj_t *label{nullptr};
    TimePart(lv_obj_t *parent, lv_obj_t *above_obj);
    void setValue(const std::optional<Repository::CumlativeWattHour> &);
  };
  //
  std::unique_ptr<TitlePart> title{};
  std::unique_ptr<ValuePart> value{};
  std::unique_ptr<TimePart> time{};

public:
  CumlativeWattHour(InitArg init) noexcept;
  CumlativeWattHour(CumlativeWattHour &&) = delete;
  CumlativeWattHour &operator=(const CumlativeWattHour &) = delete;
  virtual ~CumlativeWattHour() noexcept { lv_obj_del(tile); }
  virtual void setActiveTile(lv_obj_t *tileview) noexcept override {
    lv_obj_set_tile(tileview, tile, LV_ANIM_OFF);
  }
  virtual void timerHook() noexcept override;
};
} // namespace Widget

//
//
//
class Gui {
  inline static Gui *_instance{nullptr};
  constexpr static uint16_t MILLISECONDS_OF_PERIODIC_TIMER = 300;

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
  using TileVector = std::vector<std::unique_ptr<Widget::TileBase>>;
  TileVector tiles{};
  TileVector::iterator active_tile_itr{};
};
