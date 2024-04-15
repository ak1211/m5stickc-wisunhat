// Copyright (c) 2022 Akihiro Yamamoto.
// Licensed under the MIT License <https://spdx.org/licenses/MIT.html>
// See LICENSE file in the project root for full license information.
//
#include "Gui.hpp"
#include "Repository.hpp"
#include <algorithm>
#include <chrono>
#include <lvgl.h>
#include <memory>
#include <optional>
#include <string>

#include <M5Unified.h>

//
//
//
Widget::Dialogue::TitlePart::TitlePart(lv_obj_t *parent) {
  if (label = lv_label_create(parent); label) {
    lv_style_init(&style);
    lv_style_set_text_font(&style, &lv_font_montserrat_14);
    lv_style_set_text_align(&style, LV_TEXT_ALIGN_LEFT);
    lv_obj_add_style(label, &style, 0);
    //
    lv_label_set_recolor(label, true);
    lv_label_set_long_mode(label, LV_LABEL_LONG_WRAP);
    lv_obj_align(label, LV_ALIGN_TOP_LEFT, 0, 0);
  }
}
//
Widget::Dialogue::MessagePart::MessagePart(lv_obj_t *parent,
                                           lv_obj_t *above_obj) {
  if (label = lv_label_create(parent); label) {
    lv_style_init(&style);
    lv_style_set_text_font(&style, &lv_font_montserrat_14);
    lv_style_set_text_align(&style, LV_TEXT_ALIGN_LEFT);
    lv_obj_add_style(label, &style, 0);
    //
    lv_label_set_recolor(label, true);
    lv_label_set_long_mode(label, LV_LABEL_LONG_SCROLL_CIRCULAR);
    lv_obj_align_to(label, above_obj, LV_ALIGN_OUT_BOTTOM_LEFT, 0, 0);
  }
}
//
Widget::Dialogue::Dialogue(const std::string &title_text, lv_obj_t *parent) {
  lv_style_init(&dialogue_style);
  // Set a background color and a radius
  lv_style_set_radius(&dialogue_style, 5);
  lv_style_set_bg_opa(&dialogue_style, LV_OPA_COVER);
  lv_style_set_bg_color(&dialogue_style,
                        lv_palette_lighten(LV_PALETTE_GREY, 3));
  // Add a shadow
  lv_style_set_shadow_width(&dialogue_style, 55);
  lv_style_set_shadow_color(&dialogue_style, lv_palette_main(LV_PALETTE_BLUE));
  do {
    if (dialogue = lv_obj_create(parent); !dialogue) {
      break;
    }
    lv_obj_set_width(dialogue, lv_obj_get_width(parent) - 50);
    lv_obj_set_height(dialogue, lv_obj_get_height(parent) - 50);
    lv_obj_add_style(dialogue, &dialogue_style, 0);
    lv_obj_align(dialogue, LV_ALIGN_CENTER, 0, 0);
    if (title = std::make_unique<TitlePart>(dialogue); !title) {
      break;
    }
    lv_label_set_text(title->label, title_text.c_str());
    if (message = std::make_unique<MessagePart>(dialogue, title->label);
        !message) {
      break;
    }
  } while (false /* No looping */);
}

//
// 電力値表示
//
Widget::InstantWatt::TitlePart::TitlePart(lv_obj_t *parent) {
  if (label = lv_label_create(parent); label) {
    lv_style_init(&style);
    lv_style_set_border_color(&style, lv_palette_darken(LV_PALETTE_BROWN, 3));
    lv_style_set_border_side(&style, LV_BORDER_SIDE_BOTTOM);
    lv_style_set_border_width(&style, 3);
    lv_style_set_text_color(&style, lv_palette_darken(LV_PALETTE_BROWN, 3));
    lv_style_set_text_font(&style, &lv_font_montserrat_20);
    lv_style_set_text_align(&style, LV_TEXT_ALIGN_LEFT);
    lv_obj_add_style(label, &style, LV_PART_MAIN);
    //
    lv_obj_set_size(label, lv_obj_get_content_width(parent), 30);
    lv_obj_align(label, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_label_set_recolor(label, true);
    lv_label_set_text(label, "instant watt");
  }
}
//
Widget::InstantWatt::ValuePart::ValuePart(lv_obj_t *parent,
                                          lv_obj_t *above_obj) {
  if (label = lv_label_create(parent); label) {
    lv_style_init(&style);
    lv_style_set_radius(&style, 5);
    lv_style_set_bg_opa(&style, LV_OPA_COVER);
    lv_style_set_bg_color(&style, lv_palette_darken(LV_PALETTE_BROWN, 4));
    lv_style_set_text_font(&style, &lv_font_montserrat_46);
    lv_style_set_text_color(&style, lv_palette_main(LV_PALETTE_ORANGE));
    lv_style_set_text_letter_space(&style, 2);
    lv_obj_add_style(label, &style, LV_PART_MAIN);
    //
    lv_obj_set_size(label, lv_obj_get_content_width(parent),
                    lv_font_montserrat_46.line_height);
    lv_obj_align_to(label, above_obj, LV_ALIGN_OUT_BOTTOM_LEFT, 0, 8);
    //
    lv_label_set_long_mode(label, LV_LABEL_LONG_SCROLL_CIRCULAR);
    lv_label_set_recolor(label, true);
    //
    setValue(Repository::electric_power_data.instant_watt);
  }
}
//
void Widget::InstantWatt::ValuePart::setValue(
    const std::optional<Repository::InstantWatt> &iw) {
  if (iw.has_value()) {
    auto [time, value] = *iw;
    lv_style_set_text_align(&style, LV_TEXT_ALIGN_RIGHT);
    lv_label_set_text_fmt(label, "%d", value.watt.count());
  } else {
    lv_style_set_text_align(&style, LV_TEXT_ALIGN_CENTER);
    lv_label_set_text(label, "Now loading");
  }
}
//
Widget::InstantWatt::TimePart::TimePart(lv_obj_t *parent, lv_obj_t *above_obj) {
  if (label = lv_label_create(parent); label) {
    lv_style_init(&style);
    lv_style_set_text_color(&style, lv_palette_darken(LV_PALETTE_BROWN, 3));
    lv_style_set_text_font(&style, &lv_font_montserrat_20);
    lv_style_set_text_align(&style, LV_TEXT_ALIGN_LEFT);
    lv_obj_add_style(label, &style, 0);
    lv_obj_set_size(label, lv_obj_get_content_width(parent),
                    lv_font_montserrat_20.line_height);
    lv_obj_align_to(label, above_obj, LV_ALIGN_OUT_BOTTOM_LEFT, 0, 8);
    lv_label_set_recolor(label, true);
    setValue(Repository::electric_power_data.instant_watt);
  }
}
//
void Widget::InstantWatt::TimePart::setValue(
    const std::optional<Repository::InstantWatt> &iw) {
  using namespace std::chrono;
  if (iw.has_value()) {
    auto [tp, value] = *iw;
    auto nowtp = system_clock::now();

    auto duration = nowtp - tp;
    if (duration <= 1s) {
      lv_label_set_text(label, "W (just now)");
    } else if (duration < 2s) {
      lv_label_set_text(label, "W (1 second ago)");
    } else {
      int32_t sec = duration_cast<seconds>(duration).count();
      lv_label_set_text_fmt(label, "W (%d seconds ago)", sec);
    }
  } else {
    lv_label_set_text(label, "W");
  }
}
//
Widget::InstantWatt::InstantWatt(Widget::InitArg init) noexcept {
  tile = std::apply(lv_tileview_add_tile, init);
  lv_obj_set_style_pad_all(tile, 8, LV_PART_MAIN);
  lv_obj_clear_flag(tile, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_update_layout(tile);
  do {
    if (title = std::make_unique<TitlePart>(tile); !title) {
      break;
    }
    if (value = std::make_unique<ValuePart>(tile, title->label); !value) {
      break;
    }
    if (time = std::make_unique<TimePart>(tile, value->label); !time) {
      break;
    }
  } while (false /* No looping */);
}
//
void Widget::InstantWatt::timerHook() noexcept {
  if (value) {
    value->setValue(Repository::electric_power_data.instant_watt);
  }
  if (time) {
    time->setValue(Repository::electric_power_data.instant_watt);
  }
}

//
// 電流値表示
//
Widget::InstantAmpere::TitlePart::TitlePart(lv_obj_t *parent) {
  if (label = lv_label_create(parent); label) {
    lv_style_init(&style);
    lv_style_set_border_color(&style, lv_palette_darken(LV_PALETTE_BROWN, 3));
    lv_style_set_border_side(&style, LV_BORDER_SIDE_BOTTOM);
    lv_style_set_border_width(&style, 3);
    lv_style_set_text_color(&style, lv_palette_darken(LV_PALETTE_BROWN, 3));
    lv_style_set_text_font(&style, &lv_font_montserrat_20);
    lv_style_set_text_align(&style, LV_TEXT_ALIGN_LEFT);
    lv_obj_add_style(label, &style, LV_PART_MAIN);
    //
    lv_obj_set_size(label, lv_obj_get_content_width(parent), 30);
    lv_obj_align(label, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_label_set_recolor(label, true);
    lv_label_set_text(label, "instant ampere");
  }
}
//
Widget::InstantAmpere::ValuePart::ValuePart(lv_obj_t *parent,
                                            lv_obj_t *above_obj) {
  if (label = lv_label_create(parent); label) {
    lv_style_init(&style);
    lv_style_set_radius(&style, 5);
    lv_style_set_bg_opa(&style, LV_OPA_COVER);
    lv_style_set_bg_color(&style, lv_palette_darken(LV_PALETTE_BROWN, 4));
    lv_style_set_text_font(&style, &lv_font_montserrat_46);
    lv_style_set_text_color(&style, lv_palette_main(LV_PALETTE_ORANGE));
    lv_style_set_text_letter_space(&style, 2);
    lv_obj_add_style(label, &style, LV_PART_MAIN);
    //
    lv_obj_set_size(label, lv_obj_get_content_width(parent),
                    lv_font_montserrat_46.line_height);
    lv_obj_align_to(label, above_obj, LV_ALIGN_OUT_BOTTOM_LEFT, 0, 8);
    //
    lv_label_set_long_mode(label, LV_LABEL_LONG_SCROLL_CIRCULAR);
    lv_label_set_recolor(label, true);
    //
    setValue(Repository::electric_power_data.instant_ampere);
  }
}
//
void Widget::InstantAmpere::ValuePart::setValue(
    const std::optional<Repository::InstantAmpere> &ia) {
  if (ia.has_value()) {
    auto [time, value] = *ia;
    uint16_t r_A = value.ampereR.count() / 10;
    uint16_t r_dA = value.ampereR.count() % 10;
    //
    uint16_t t_A = value.ampereT.count() / 10;
    uint16_t t_dA = value.ampereT.count() % 10;
    //
    lv_style_set_text_align(&style, LV_TEXT_ALIGN_RIGHT);
    lv_color32_t c32{
        .full = lv_color_to32(lv_palette_lighten(LV_PALETTE_ORANGE, 4))};
    lv_label_set_text_fmt(label, "R%d.%d#%02x%02x%02x /#T%d.%d", r_A, r_dA,
                          c32.ch.red, c32.ch.green, c32.ch.blue, t_A, t_dA);
  } else {
    lv_style_set_text_align(&style, LV_TEXT_ALIGN_CENTER);
    lv_label_set_text(label, "Now loading");
  }
}
//
Widget::InstantAmpere::TimePart::TimePart(lv_obj_t *parent,
                                          lv_obj_t *above_obj) {
  if (label = lv_label_create(parent); label) {
    lv_style_init(&style);
    lv_style_set_text_color(&style, lv_palette_darken(LV_PALETTE_BROWN, 3));
    lv_style_set_text_font(&style, &lv_font_montserrat_20);
    lv_style_set_text_align(&style, LV_TEXT_ALIGN_LEFT);
    lv_obj_add_style(label, &style, 0);
    lv_obj_set_size(label, lv_obj_get_content_width(parent),
                    lv_font_montserrat_20.line_height);
    lv_obj_align_to(label, above_obj, LV_ALIGN_OUT_BOTTOM_LEFT, 0, 8);
    lv_label_set_recolor(label, true);
    setValue(Repository::electric_power_data.instant_ampere);
  }
}
//
void Widget::InstantAmpere::TimePart::setValue(
    const std::optional<Repository::InstantAmpere> &ia) {
  using namespace std::chrono;
  //
  if (ia.has_value()) {
    auto [tp, value] = *ia;
    auto nowtp = system_clock::now();

    auto duration = nowtp - tp;
    if (duration <= 1s) {
      lv_label_set_text(label, "A (just now)");
    } else if (duration < 2s) {
      lv_label_set_text(label, "A (1 second ago)");
    } else {
      int32_t sec = duration_cast<seconds>(duration).count();
      lv_label_set_text_fmt(label, "A (%d seconds ago)", sec);
    }
  } else {
    lv_label_set_text(label, "A");
  }
}
//
Widget::InstantAmpere::InstantAmpere(Widget::InitArg init) noexcept {
  tile = std::apply(lv_tileview_add_tile, init);
  lv_obj_set_style_pad_all(tile, 8, LV_PART_MAIN);
  lv_obj_clear_flag(tile, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_update_layout(tile);
  do {
    if (title = std::make_unique<TitlePart>(tile); !title) {
      break;
    }
    if (value = std::make_unique<ValuePart>(tile, title->label); !value) {
      break;
    }
    if (time = std::make_unique<TimePart>(tile, value->label); !time) {
      break;
    }
  } while (false /* No looping */);
}
//
void Widget::InstantAmpere::timerHook() noexcept {
  if (auto p = value.get(); p) {
    p->setValue(Repository::electric_power_data.instant_ampere);
  }
  if (auto p = time.get(); p) {
    p->setValue(Repository::electric_power_data.instant_ampere);
  }
}

//
// 積算電力量表示
//
Widget::CumlativeWattHour::TitlePart::TitlePart(lv_obj_t *parent) {
  if (label = lv_label_create(parent); label) {
    lv_style_init(&style);
    lv_style_set_border_color(&style, lv_palette_darken(LV_PALETTE_BROWN, 3));
    lv_style_set_border_side(&style, LV_BORDER_SIDE_BOTTOM);
    lv_style_set_border_width(&style, 3);
    lv_style_set_text_color(&style, lv_palette_darken(LV_PALETTE_BROWN, 3));
    lv_style_set_text_font(&style, &lv_font_montserrat_20);
    lv_style_set_text_align(&style, LV_TEXT_ALIGN_LEFT);
    lv_obj_add_style(label, &style, LV_PART_MAIN);
    //
    lv_obj_set_size(label, lv_obj_get_content_width(parent), 30);
    lv_obj_align(label, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_label_set_recolor(label, true);
    lv_label_set_text(label, "cumlative watt hour");
  }
}
//
Widget::CumlativeWattHour::ValuePart::ValuePart(lv_obj_t *parent,
                                                lv_obj_t *above_obj) {
  if (label = lv_label_create(parent); label) {
    lv_style_init(&style);
    lv_style_set_radius(&style, 5);
    lv_style_set_bg_opa(&style, LV_OPA_COVER);
    lv_style_set_bg_color(&style, lv_palette_darken(LV_PALETTE_BROWN, 4));
    lv_style_set_text_font(&style, &lv_font_montserrat_46);
    lv_style_set_text_color(&style, lv_palette_main(LV_PALETTE_ORANGE));
    lv_style_set_text_letter_space(&style, 2);
    lv_obj_add_style(label, &style, LV_PART_MAIN);
    //
    lv_obj_set_size(label, lv_obj_get_content_width(parent),
                    lv_font_montserrat_46.line_height);
    lv_obj_align_to(label, above_obj, LV_ALIGN_OUT_BOTTOM_LEFT, 0, 8);
    //
    lv_label_set_long_mode(label, LV_LABEL_LONG_SCROLL_CIRCULAR);
    lv_label_set_recolor(label, true);
    //
    setValue(Repository::electric_power_data.cumlative_watt_hour);
  }
}
//
void Widget::CumlativeWattHour::ValuePart::setValue(
    const std::optional<Repository::CumlativeWattHour> &cwh) {
  if (cwh.has_value()) {
    auto cumlative_kilo_watt_hour =
        SmartElectricEnergyMeter::cumlative_kilo_watt_hour(*cwh).count();
    lv_style_set_text_align(&style, LV_TEXT_ALIGN_RIGHT);
    lv_label_set_text_fmt(label, "%.2f", cumlative_kilo_watt_hour);
  } else {
    lv_style_set_text_align(&style, LV_TEXT_ALIGN_CENTER);
    lv_label_set_text(label, "Now loading");
  }
}
//
Widget::CumlativeWattHour::TimePart::TimePart(lv_obj_t *parent,
                                              lv_obj_t *above_obj) {
  if (label = lv_label_create(parent); label) {
    lv_style_init(&style);
    lv_style_set_text_color(&style, lv_palette_darken(LV_PALETTE_BROWN, 3));
    lv_style_set_text_font(&style, &lv_font_montserrat_20);
    lv_style_set_text_align(&style, LV_TEXT_ALIGN_LEFT);
    lv_obj_add_style(label, &style, 0);
    lv_obj_set_size(label, lv_obj_get_content_width(parent),
                    lv_font_montserrat_20.line_height);
    lv_obj_align_to(label, above_obj, LV_ALIGN_OUT_BOTTOM_LEFT, 0, 8);
    lv_label_set_recolor(label, true);
    setValue(Repository::electric_power_data.cumlative_watt_hour);
  }
}
//
void Widget::CumlativeWattHour::TimePart::setValue(
    const std::optional<Repository::CumlativeWattHour> &cwh) {
  using namespace std::chrono;
  //
  std::optional<std::time_t> asia_tokyo_time{std::nullopt};
  if (cwh.has_value()) {
    auto [cumlative_kilo_watt_hour, b, c] = *cwh;
    asia_tokyo_time = cumlative_kilo_watt_hour.get_time_t();
  }

  if (asia_tokyo_time.has_value()) {
    auto nowtp = system_clock::now();
    auto duration = nowtp - system_clock::from_time_t(*asia_tokyo_time);
    if (duration <= 1min) {
      lv_label_set_text(label, "kWh (just now)");
    } else if (duration < 2min) {
      lv_label_set_text(label, "kWh (1 min ago)");
    } else {
      int32_t min = duration_cast<minutes>(duration).count();
      lv_label_set_text_fmt(label, "kWh (%d mins ago)", min);
    }
  } else {
    lv_label_set_text(label, "kWh");
  }
}
//
Widget::CumlativeWattHour::CumlativeWattHour(Widget::InitArg init) noexcept {
  tile = std::apply(lv_tileview_add_tile, init);
  lv_obj_set_style_pad_all(tile, 8, LV_PART_MAIN);
  lv_obj_clear_flag(tile, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_update_layout(tile);
  do {
    if (title = std::make_unique<TitlePart>(tile); !title) {
      break;
    }
    if (value = std::make_unique<ValuePart>(tile, title->label); !value) {
      break;
    }
    if (time = std::make_unique<TimePart>(tile, value->label); !time) {
      break;
    }
  } while (false /* No looping */);
}
//
void Widget::CumlativeWattHour::timerHook() noexcept {
  if (auto p = value.get(); p) {
    p->setValue(Repository::electric_power_data.cumlative_watt_hour);
  }
  if (auto p = time.get(); p) {
    p->setValue(Repository::electric_power_data.cumlative_watt_hour);
  }
}

//
//
//
bool Gui::begin() noexcept {
  // Display init
  gfx.setColorDepth(LV_COLOR_DEPTH);
  gfx.setRotation(3);
  // LVGL init
  M5_LOGD("initializing LVGL");
  lv_init();

  // allocate LVGL draw buffer
  const int32_t DRAW_BUFFER_SIZE = gfx.width() * (gfx.height() / 10);
  lvgl_use.draw_buf_1 = std::make_unique<lv_color_t[]>(DRAW_BUFFER_SIZE);
  lvgl_use.draw_buf_2 = std::make_unique<lv_color_t[]>(DRAW_BUFFER_SIZE);
  if (lvgl_use.draw_buf_1 == nullptr || lvgl_use.draw_buf_2 == nullptr) {
    M5_LOGE("memory allocation error");
    return false;
  }
  lv_disp_draw_buf_init(&lvgl_use.draw_buf_dsc, lvgl_use.draw_buf_1.get(),
                        lvgl_use.draw_buf_2.get(), DRAW_BUFFER_SIZE);

  // LVGL display driver
  lv_disp_drv_init(&lvgl_use.disp_drv);
  lvgl_use.disp_drv.user_data = &gfx;
  lvgl_use.disp_drv.hor_res = gfx.width();
  lvgl_use.disp_drv.ver_res = gfx.height();
  // vvvvvvvvvv DISPLAY FLUSH CALLBACK FUNCTION vvvvvvvvvv
  lvgl_use.disp_drv.flush_cb = [](lv_disp_drv_t *disp_drv,
                                  const lv_area_t *area,
                                  lv_color_t *color_p) -> void {
    M5GFX &gfx = *static_cast<M5GFX *>(disp_drv->user_data);

    int32_t width = area->x2 - area->x1 + 1;
    int32_t height = area->y2 - area->y1 + 1;

    gfx.startWrite();
    gfx.setAddrWindow(area->x1, area->y1, width, height);
    gfx.writePixels(reinterpret_cast<uint16_t *>(color_p), width * height,
                    true);
    gfx.endWrite();

    lv_disp_flush_ready(disp_drv);
  };
  // ^^^^^^^^^^ DISPLAY FLUSH CALLBACK FUNCTION ^^^^^^^^^^
  lvgl_use.disp_drv.draw_buf = &lvgl_use.draw_buf_dsc;

  // register the display driver
  lv_disp_drv_register(&lvgl_use.disp_drv);

  // set timer callback
  periodic_timer = lv_timer_create(
      [](lv_timer_t *arg) noexcept -> void {
        static int8_t countY = 0;
        // Display rotation
        if (float ax, ay, az; M5.Imu.getAccelData(&ax, &ay, &az)) {
          if (ax <= 0.0) {
            countY--;
          } else if (ax >= 0.0) {
            countY++;
          }
        }
        if (std::abs(countY) >= 10) {
          auto current = static_cast<M5GFX *>(arg->user_data)->getRotation();
          auto next = countY < 0 ? 3 : 1;
          if (current != next) {
            static_cast<M5GFX *>(arg->user_data)->setRotation(next);
            // force redraw
            lv_obj_invalidate(lv_scr_act());
          }
          countY = 0;
        }
        // timer
        auto itr = Gui::getInstance()->active_tile_itr;
        if (auto p = itr->get(); p) {
          p->timerHook();
        }
      },
      MILLISECONDS_OF_PERIODIC_TIMER, &gfx);

  return true;
}

//
void Gui::startUi() noexcept {
  // tileview style init
  lv_style_init(&tileview_style);
  lv_style_set_bg_opa(&tileview_style, LV_OPA_COVER);
  lv_style_set_bg_color(&tileview_style,
                        lv_palette_lighten(LV_PALETTE_LIGHT_GREEN, 2));
  // tileview init
  tileview = lv_tileview_create(lv_scr_act());
  lv_obj_add_style(tileview, &tileview_style, LV_PART_MAIN);
  lv_obj_update_layout(tileview);

  Widget::InitArg iwatt{tileview, 0, 0, LV_DIR_NONE};
  Widget::InitArg iampere{tileview, 0, 1, LV_DIR_NONE};
  Widget::InitArg cwatthour{tileview, 0, 2, LV_DIR_NONE};
  tiles.emplace_back(std::make_unique<Widget::InstantWatt>(iwatt));
  tiles.emplace_back(std::make_unique<Widget::InstantAmpere>(iampere));
  tiles.emplace_back(std::make_unique<Widget::CumlativeWattHour>(cwatthour));

  active_tile_itr = tiles.begin();
  active_tile_itr->get()->setActiveTile(tileview);
}
