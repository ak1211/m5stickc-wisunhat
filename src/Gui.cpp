// Copyright (c) 2022 Akihiro Yamamoto.
// Licensed under the MIT License <https://spdx.org/licenses/MIT.html>
// See LICENSE file in the project root for full license information.
//
#include "Gui.hpp"
#include "Application.hpp"
#include <algorithm>
#include <chrono>
#include <lvgl.h>
#include <memory>
#include <optional>
#include <string>

#include <M5Unified.h>

//
lv_color_t Gui::LvglUseArea::draw_buf_1[];
lv_color_t Gui::LvglUseArea::draw_buf_2[];

//
void Gui::lvgl_use_display_flush_callback(lv_disp_drv_t *disp_drv,
                                          const lv_area_t *area,
                                          lv_color_t *color_p) {
  M5GFX &gfx = *static_cast<M5GFX *>(disp_drv->user_data);

  int32_t width = area->x2 - area->x1 + 1;
  int32_t height = area->y2 - area->y1 + 1;
  gfx.startWrite();
  gfx.setAddrWindow(area->x1, area->y1, width, height);
  gfx.writePixels(reinterpret_cast<uint16_t *>(color_p), width * height, true);
  gfx.endWrite();

  lv_disp_flush_ready(disp_drv);
}

//
void Gui::periodic_timer_callback(lv_timer_t *arg) {
  auto gui = static_cast<Gui *>(arg->user_data);
  assert(gui);
  //
  static int8_t gravity_dir_counter = 0;
  // Display rotation
  if (float ax, ay, az; M5.Imu.getAccelData(&ax, &ay, &az)) {
    if (ax <= 0.0) {
      gravity_dir_counter--;
    } else if (ax >= 0.0) {
      gravity_dir_counter++;
    }
  }
  if (std::abs(gravity_dir_counter) >= 10) {
    auto current = gui->_gfx.getRotation();
    auto next = gravity_dir_counter < 0 ? 3 : 1;
    if (current != next) {
      gui->_gfx.setRotation(next);
      // force redraw
      lv_obj_invalidate(lv_scr_act());
    }
    gravity_dir_counter = 0;
  }
}

//
void Gui::update_timer_callback(lv_timer_t *arg) {
  assert(arg);
  auto gui = static_cast<Gui *>(arg->user_data);
  if (gui && gui->_active_tile_itr->get()) {
    gui->_active_tile_itr->get()->update();
  }
}

//
bool Gui::begin() {
  // Display init
  _gfx.setColorDepth(LV_COLOR_DEPTH);
  _gfx.setRotation(3);
  // LVGL init
  M5_LOGD("initializing LVGL");
  lv_init();
  lv_disp_draw_buf_init(&_lvgl_use.draw_buf_dsc, _lvgl_use.draw_buf_1,
                        _lvgl_use.draw_buf_2, std::size(_lvgl_use.draw_buf_1));

  // LVGL display driver
  lv_disp_drv_init(&_lvgl_use.disp_drv);
  _lvgl_use.disp_drv.user_data = &_gfx;
  _lvgl_use.disp_drv.hor_res = _gfx.width();
  _lvgl_use.disp_drv.ver_res = _gfx.height();
  _lvgl_use.disp_drv.flush_cb = lvgl_use_display_flush_callback;
  _lvgl_use.disp_drv.draw_buf = &_lvgl_use.draw_buf_dsc;

  // register the display driver
  lv_disp_drv_register(&_lvgl_use.disp_drv);

  // set timer callback
  _periodic_timer.reset(
      lv_timer_create(periodic_timer_callback,
                      std::chrono::duration_cast<std::chrono::milliseconds>(
                          PERIODIC_TIMER_INTERVAL)
                          .count(),
                      this),
      lv_timer_del);

  return true;
}

//
void Gui::startUi() {
  // tileview style init
  lv_style_init(&_tileview_style);
  lv_style_set_bg_opa(&_tileview_style, LV_OPA_COVER);
  lv_style_set_bg_color(&_tileview_style,
                        lv_palette_lighten(LV_PALETTE_GREEN, 2));
  // tileview init
  _tileview_obj.reset(lv_tileview_create(nullptr), lv_obj_del);
  if (_tileview_obj) {
    lv_obj_add_style(_tileview_obj.get(), &_tileview_style, LV_PART_MAIN);

    Widget::InitArg iwatt{_tileview_obj, 0, 0, LV_DIR_NONE};
    Widget::InitArg iampere{_tileview_obj, 0, 1, LV_DIR_NONE};
    Widget::InitArg cwatthour{_tileview_obj, 0, 2, LV_DIR_NONE};
    _tiles.emplace_back(std::make_unique<Widget::InstantWatt>(iwatt));
    _tiles.emplace_back(std::make_unique<Widget::InstantAmpere>(iampere));
    _tiles.emplace_back(std::make_unique<Widget::CumlativeWattHour>(cwatthour));
    _active_tile_itr = _tiles.begin();
    lv_scr_load(_tileview_obj.get());
    // set update timer callback
    _update_timer.reset(
        lv_timer_create(update_timer_callback,
                        std::chrono::duration_cast<std::chrono::milliseconds>(
                            UPDATE_TIMER_INTERVAL)
                            .count(),
                        this),
        lv_timer_del);
  }

  home();
}

//
//
//
Widget::Dialogue::Dialogue(const std::string &title_text)
    : _title_label_font{lv_font_montserrat_20},
      _message_label_font{lv_font_montserrat_14} {
  //
  lv_style_init(&_dialogue_style);
  // Set a background color and a radius
  lv_style_set_radius(&_dialogue_style, 5);
  lv_style_set_bg_opa(&_dialogue_style, LV_OPA_COVER);
  lv_style_set_bg_color(&_dialogue_style,
                        lv_palette_darken(LV_PALETTE_AMBER, 2));
  // Add a shadow
  lv_style_set_shadow_width(&_dialogue_style, 20);
  lv_style_set_shadow_color(&_dialogue_style,
                            lv_palette_darken(LV_PALETTE_AMBER, 2));
  //
  lv_style_init(&_title_label_style);
  lv_style_set_text_font(&_title_label_style, &_title_label_font);
  lv_style_set_text_align(&_title_label_style, LV_TEXT_ALIGN_CENTER);
  //
  lv_style_init(&_message_label_style);
  lv_style_set_text_font(&_message_label_style, &_message_label_font);
  lv_style_set_text_align(&_message_label_style, LV_TEXT_ALIGN_LEFT);

  // create
  auto width = lv_disp_get_physical_hor_res(lv_obj_get_disp(lv_scr_act()));
  auto height = lv_disp_get_physical_ver_res(lv_obj_get_disp(lv_scr_act()));
  _dialogue_obj.reset(lv_obj_create(lv_scr_act()), lv_obj_del);
  if (_dialogue_obj) {
    _title_label_obj.reset(lv_label_create(_dialogue_obj.get()), lv_obj_del);
    _message_label_obj.reset(lv_label_create(_dialogue_obj.get()), lv_obj_del);
  }

  // set style
  if (_dialogue_obj) {
    lv_obj_add_style(_dialogue_obj.get(), &_dialogue_style, LV_PART_MAIN);
    lv_obj_set_width(_dialogue_obj.get(), LV_PCT(80));
    lv_obj_set_height(_dialogue_obj.get(), LV_PCT(80));
    lv_obj_align(_dialogue_obj.get(), LV_ALIGN_CENTER, 0, 0);
    lv_obj_update_layout(_dialogue_obj.get());
  }
  //
  if (_title_label_obj && _dialogue_obj) {
    lv_obj_add_style(_title_label_obj.get(), &_title_label_style, LV_PART_MAIN);
    lv_obj_set_width(_title_label_obj.get(),
                     lv_obj_get_content_width(_dialogue_obj.get()));
    lv_obj_set_height(_title_label_obj.get(), _title_label_font.line_height);
    lv_label_set_recolor(_title_label_obj.get(), true);
    lv_label_set_long_mode(_title_label_obj.get(), LV_LABEL_LONG_WRAP);
    lv_obj_align(_title_label_obj.get(), LV_ALIGN_TOP_LEFT, 0, 0);
    lv_label_set_text(_title_label_obj.get(), title_text.c_str());
  }
  //
  if (_message_label_obj && _title_label_obj && _dialogue_obj) {
    lv_obj_add_style(_message_label_obj.get(), &_message_label_style,
                     LV_PART_MAIN);
    lv_obj_set_width(_message_label_obj.get(),
                     lv_obj_get_content_width(_dialogue_obj.get()));
    lv_obj_set_height(_message_label_obj.get(), LV_SIZE_CONTENT);
    lv_label_set_recolor(_message_label_obj.get(), true);
    lv_obj_align_to(_message_label_obj.get(), _title_label_obj.get(),
                    LV_ALIGN_OUT_BOTTOM_LEFT, 0, 8);
    lv_label_set_text(_message_label_obj.get(), "");
  }
}

//
void Widget::Dialogue::setMessage(const std::string &text) {
  if (_message_label_obj) {
    lv_label_set_text(_message_label_obj.get(), text.c_str());
  }
}

//
void Widget::Dialogue::info(const std::string &text) { setMessage(text); }

//
void Widget::Dialogue::error(const std::string &text) {
  setMessage("#ff0000 " + text + "#");
}

//
//
//
Widget::TileBase::TileBase(Widget::InitArg init)
    : _tileview_obj{std::get<0>(init)} {
  //
  lv_style_init(&_title_label_style);
  lv_style_set_border_color(&_title_label_style,
                            lv_palette_darken(LV_PALETTE_BROWN, 4));
  lv_style_set_border_side(&_title_label_style, LV_BORDER_SIDE_BOTTOM);
  lv_style_set_border_width(&_title_label_style, 3);
  lv_style_set_text_color(&_title_label_style,
                          lv_palette_darken(LV_PALETTE_BROWN, 4));
  lv_style_set_text_font(&_title_label_style, &_title_label_font);
  lv_style_set_text_align(&_title_label_style, LV_TEXT_ALIGN_LEFT);
  //
  lv_style_init(&_value_label_style);
  lv_style_set_radius(&_value_label_style, 5);
  lv_style_set_bg_opa(&_value_label_style, LV_OPA_COVER);
  lv_style_set_bg_color(&_value_label_style,
                        lv_palette_darken(LV_PALETTE_BROWN, 4));
  lv_style_set_text_font(&_value_label_style, &_value_label_font);
  lv_style_set_text_color(&_value_label_style,
                          lv_palette_main(LV_PALETTE_ORANGE));
  lv_style_set_text_letter_space(&_value_label_style, 2);
  //
  lv_style_init(&_time_label_style);
  lv_style_set_text_color(&_time_label_style,
                          lv_palette_darken(LV_PALETTE_BROWN, 4));
  lv_style_set_text_font(&_time_label_style, &_time_label_font);
  lv_style_set_text_align(&_time_label_style, LV_TEXT_ALIGN_LEFT);

  // create
  if (_tileview_obj) {
    auto &[_, col_id, row_id, dir] = init;
    _tile_obj.reset(
        lv_tileview_add_tile(_tileview_obj.get(), col_id, row_id, dir),
        lv_obj_del);
  }
  if (_tile_obj) {
    _title_label_obj.reset(lv_label_create(_tile_obj.get()), lv_obj_del);
    _value_label_obj.reset(lv_label_create(_tile_obj.get()), lv_obj_del);
    _time_label_obj.reset(lv_label_create(_tile_obj.get()), lv_obj_del);
  }

  // set style
  if (_tile_obj) {
    lv_obj_set_style_pad_all(_tile_obj.get(), 8, LV_PART_MAIN);
    lv_obj_clear_flag(_tile_obj.get(), LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_update_layout(_tile_obj.get());
  }
  //
  if (_title_label_obj && _tile_obj) {
    lv_obj_add_style(_title_label_obj.get(), &_title_label_style, LV_PART_MAIN);
    //
    lv_obj_set_size(_title_label_obj.get(),
                    lv_obj_get_content_width(_tile_obj.get()), 30);
    lv_obj_align(_title_label_obj.get(), LV_ALIGN_TOP_LEFT, 0, 0);
    lv_label_set_recolor(_title_label_obj.get(), true);
  }
  //
  if (_value_label_obj && _title_label_obj && _tile_obj) {
    lv_obj_add_style(_value_label_obj.get(), &_value_label_style, LV_PART_MAIN);
    //
    lv_obj_set_size(_value_label_obj.get(),
                    lv_obj_get_content_width(_tile_obj.get()),
                    _value_label_font.line_height);
    lv_obj_align_to(_value_label_obj.get(), _title_label_obj.get(),
                    LV_ALIGN_OUT_BOTTOM_LEFT, 0, 8);
    //
    lv_label_set_long_mode(_value_label_obj.get(),
                           LV_LABEL_LONG_SCROLL_CIRCULAR);
    lv_label_set_recolor(_value_label_obj.get(), true);
  }
  //
  if (_time_label_obj && _value_label_obj && _tile_obj) {
    lv_obj_add_style(_time_label_obj.get(), &_time_label_style, LV_PART_MAIN);
    lv_obj_set_size(_time_label_obj.get(),
                    lv_obj_get_content_width(_tile_obj.get()),
                    _time_label_font.line_height);
    lv_obj_align_to(_time_label_obj.get(), _value_label_obj.get(),
                    LV_ALIGN_OUT_BOTTOM_LEFT, 0, 8);
    lv_label_set_recolor(_time_label_obj.get(), true);
  }
}

//
bool Widget::TileBase::isActiveTile() const {
  if (_tileview_obj && _tile_obj) {
    return _tile_obj.get() == lv_tileview_get_tile_act(_tileview_obj.get());
  } else {
    M5_LOGE("null");
    return false;
  }
}

//
void Widget::TileBase::setActiveTile() {
  if (_tileview_obj && _tile_obj) {
    lv_obj_set_tile(_tileview_obj.get(), _tile_obj.get(), LV_ANIM_OFF);
  } else {
    M5_LOGE("null");
  }
}

//
// 電力値表示
//
Widget::InstantWatt::InstantWatt(Widget::InitArg init) : TileBase(init) {
  if (_title_label_obj) {
    lv_label_set_text(_title_label_obj.get(), "instant watt");
  }
  showValue(std::nullopt);
}

//
void Widget::InstantWatt::showValue(
    const std::optional<Repository::InstantWatt> iw) {
  if (_value_label_obj) {
    if (iw.has_value()) {
      auto [_, value] = *iw;
      int32_t instant_watt = value.watt.count();
      lv_style_set_text_align(&_value_label_style, LV_TEXT_ALIGN_RIGHT);
      lv_label_set_text_fmt(_value_label_obj.get(), "%ld", instant_watt);
    } else {
      lv_style_set_text_align(&_value_label_style, LV_TEXT_ALIGN_CENTER);
      lv_label_set_text(_value_label_obj.get(), "Now loading");
    }
  }
  if (_time_label_obj) {
    if (iw.has_value()) {
      auto [tp, _] = *iw;
      auto duration = system_clock::now() - tp;
      if (duration <= 1s) {
        lv_label_set_text(_time_label_obj.get(), "W (just now)");
      } else if (duration < 2s) {
        lv_label_set_text(_time_label_obj.get(), "W (1 second ago)");
      } else {
        int32_t sec = duration_cast<seconds>(duration).count();
        lv_label_set_text_fmt(_time_label_obj.get(), "W (%ld seconds ago)",
                              sec);
      }
    } else {
      lv_label_set_text(_time_label_obj.get(), "W");
    }
  }
}

//
void Widget::InstantWatt::update() {
  showValue(Application::getElectricPowerData().instant_watt);
}

//
// 電流値表示
//
Widget::InstantAmpere::InstantAmpere(Widget::InitArg init) : TileBase(init) {
  if (_title_label_obj) {
    lv_label_set_text(_title_label_obj.get(), "instant ampere");
  }
  showValue(std::nullopt);
}

//
void Widget::InstantAmpere::showValue(
    const std::optional<Repository::InstantAmpere> ia) {
  if (_value_label_obj) {
    if (ia.has_value()) {
      auto [_, value] = *ia;
      int32_t r_A = value.ampereR.count() / 10;
      int32_t r_dA = value.ampereR.count() % 10;
      //
      int32_t t_A = value.ampereT.count() / 10;
      int32_t t_dA = value.ampereT.count() % 10;
      //
      lv_style_set_text_align(&_value_label_style, LV_TEXT_ALIGN_RIGHT);
      lv_color32_t c32{
          .full = lv_color_to32(lv_palette_lighten(LV_PALETTE_ORANGE, 4))};
      lv_label_set_text_fmt(_value_label_obj.get(),
                            "R%ld.%ld#%02x%02x%02x /#T%ld.%ld", r_A, r_dA,
                            c32.ch.red, c32.ch.green, c32.ch.blue, t_A, t_dA);
    } else {
      lv_style_set_text_align(&_value_label_style, LV_TEXT_ALIGN_CENTER);
      lv_label_set_text(_value_label_obj.get(), "Now loading");
    }
  }
  if (_time_label_obj) {
    if (ia.has_value()) {
      auto [tp, _] = *ia;
      auto duration = system_clock::now() - tp;
      if (duration <= 1s) {
        lv_label_set_text(_time_label_obj.get(), "A (just now)");
      } else if (duration < 2s) {
        lv_label_set_text(_time_label_obj.get(), "A (1 second ago)");
      } else {
        int32_t sec = duration_cast<seconds>(duration).count();
        lv_label_set_text_fmt(_time_label_obj.get(), "A (%ld seconds ago)",
                              sec);
      }
    } else {
      lv_label_set_text(_time_label_obj.get(), "A");
    }
  }
}

//
void Widget::InstantAmpere::update() {
  showValue(Application::getElectricPowerData().instant_ampere);
}

//
// 積算電力量表示
//
Widget::CumlativeWattHour::CumlativeWattHour(Widget::InitArg init)
    : TileBase(init) {
  if (_title_label_obj) {
    lv_label_set_text(_title_label_obj.get(), "cumlative watt hour");
  }
  showValue(std::nullopt);
}

//
void Widget::CumlativeWattHour::showValue(
    const std::optional<Repository::CumlativeWattHour> cwh) {
  if (_value_label_obj) {
    if (cwh.has_value()) {
      auto cumlative_kilo_watt_hour =
          SmartElectricEnergyMeter::cumlative_kilo_watt_hour(*cwh).count();
      lv_style_set_text_align(&_value_label_style, LV_TEXT_ALIGN_RIGHT);
      lv_label_set_text_fmt(_value_label_obj.get(), "%.2f",
                            cumlative_kilo_watt_hour);
    } else {
      lv_style_set_text_align(&_value_label_style, LV_TEXT_ALIGN_CENTER);
      lv_label_set_text(_value_label_obj.get(), "Now loading");
    }
  }
  if (_time_label_obj) {
    std::optional<std::time_t> asia_tokyo_time =
        cwh.has_value() ? std::get<0>(*cwh).get_time_t() : std::nullopt;
    //
    if (asia_tokyo_time.has_value()) {
      auto nowtp = system_clock::now();
      auto duration = nowtp - system_clock::from_time_t(*asia_tokyo_time);
      if (duration <= 1min) {
        lv_label_set_text(_time_label_obj.get(), "kWh (just now)");
      } else if (duration < 2min) {
        lv_label_set_text(_time_label_obj.get(), "kWh (1 min ago)");
      } else {
        int32_t min = duration_cast<minutes>(duration).count();
        lv_label_set_text_fmt(_time_label_obj.get(), "kWh (%ld mins ago)", min);
      }
    } else {
      lv_label_set_text(_time_label_obj.get(), "kWh");
    }
  }
}

//
void Widget::CumlativeWattHour::update() {
  showValue(Application::getElectricPowerData().cumlative_watt_hour);
}
