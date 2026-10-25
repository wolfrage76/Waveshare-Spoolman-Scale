#include "ui/status_clear.h"

#include <lvgl.h>
#include <stdio.h>
#include <string.h>

#include "app/app_state.h"
#include "ui/theme.h"

// The bar is 22 px high and catches nothing outside itself, so the button
// takes the bar's height and gets its width for the finger.
#define SC_CHIP_W     40
#define SC_CHIP_H     20
#define SC_CHIP_GAP    8
#define SC_CHIP_EXT_X 12
#define SC_TEXT_MAX   64

static lv_obj_t* s_chip  = nullptr;
static bool      s_taken = false;
static char      s_placed_for[SC_TEXT_MAX] = "";   // the text the button sits behind

static void placeChip() {
  const char* text = lv_label_get_text(lbl_status);
  lv_point_t size;
  lv_txt_get_size(&size, text, lv_obj_get_style_text_font(lbl_status, 0),
                  0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
  const lv_coord_t text_w = LV_MIN(size.x, lv_obj_get_width(lbl_status));
  lv_obj_t* bar = lv_obj_get_parent(lbl_status);
  lv_obj_set_pos(s_chip, lv_obj_get_x(lbl_status) + text_w + SC_CHIP_GAP,
                 (lv_obj_get_height(bar) - SC_CHIP_H) / 2);
  snprintf(s_placed_for, sizeof(s_placed_for), "%s", text);
}

static void buildChip() {
  s_chip = lv_btn_create(lv_obj_get_parent(lbl_status));
  lv_obj_set_size(s_chip, SC_CHIP_W, SC_CHIP_H);
  lv_obj_set_style_bg_color(s_chip, lv_color_hex(UI_COL_CHIP), 0);
  lv_obj_set_style_bg_color(s_chip, lv_color_hex(UI_COL_PRESS_FILL), LV_STATE_PRESSED);
  lv_obj_set_style_border_width(s_chip, 1, 0);
  lv_obj_set_style_border_color(s_chip, lv_color_hex(UI_COL_LINE), 0);
  lv_obj_set_style_radius(s_chip, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_shadow_width(s_chip, 0, 0);
  lv_obj_set_style_pad_all(s_chip, 0, 0);
  lv_obj_set_ext_click_area(s_chip, SC_CHIP_EXT_X);
  lv_obj_add_event_cb(s_chip, [](lv_event_t*) { s_taken = true; }, LV_EVENT_CLICKED, NULL);
  lv_obj_t* l = lv_label_create(s_chip);
  lv_label_set_text(l, LV_SYMBOL_CLOSE);
  lv_obj_set_style_text_color(l, lv_color_hex(UI_COL_INK_2), 0);
  lv_obj_set_style_text_font(l, UI_FONT_CAPTION, 0);
  lv_obj_center(l);
}

void statusClearSync(bool want) {
  if (!lbl_status) return;
  if (!want) {
    if (s_chip) lv_obj_del(s_chip);
    s_chip = nullptr;
    s_taken = false;
    s_placed_for[0] = '\0';
    return;
  }
  if (!s_chip) buildChip();
  // Compared, not measured, on every pass: the text changes rarely.
  if (strncmp(s_placed_for, lv_label_get_text(lbl_status), sizeof(s_placed_for) - 1) != 0) placeChip();
}

bool statusClearTaken() {
  const bool taken = s_taken;
  s_taken = false;
  return taken;
}
