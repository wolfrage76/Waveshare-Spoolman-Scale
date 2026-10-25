#include "app/id_spool.h"

#include <Arduino.h>
#include <lvgl.h>

#include "app/app_state.h"
#include "app_config.h"
#include "hardware/sd_logger.h"
#include "lang.h"
#include "services/auto_weight_state.h"
#include "services/user_options.h"
#include "ui/main_screen_helpers.h"
#include "ui/spoolman_lookup.h"
#include "ui/status_clear.h"
#include "ui/tag_display.h"
#include "ui/theme.h"

static bool          s_active     = false;
static int           s_id         = 0;
static bool          s_seen_load  = false;   // the spool has been on the pad since
static unsigned long s_since_ms   = 0;
static unsigned long s_lifted_ms  = 0;       // when it left the pad, 0 while it is on

bool idSpoolActive() { return s_active; }

static void paintStatus() {
  lv_label_set_text(lbl_nfc_dot, LV_SYMBOL_BULLET);
  lv_obj_set_style_text_color(lbl_nfc_dot, lv_color_hex(UI_COL_STATUS_BLUE), 0);
  lv_label_set_text(lbl_status, T(STR_ID_SPOOL_STATUS));
  lv_obj_set_style_text_color(lbl_status, lv_color_hex(UI_COL_STATUS_BLUE), 0);
}

// The weight button reads plain while the state holds: "(A)" would promise a
// save that does not happen. Back to the idle look afterwards, the same two
// the main screen starts with.
static void paintWeightButton(bool by_id) {
  if (!lbl_weight_main_lbl) return;
  char buf[48];
  const bool auto_look = g_auto_weight && !by_id;
  if (auto_look) snprintf(buf, sizeof(buf), "%s (A)", T(STR_BTN_WEIGHT));
  else           copyT(buf, sizeof(buf), STR_BTN_WEIGHT);
  lv_label_set_text(lbl_weight_main_lbl, buf);
  lv_obj_set_style_text_color(lbl_weight_main_lbl,
                              lv_color_hex(auto_look ? UI_COL_WEIGHT_AUTO : UI_COL_WEIGHT_TEXT), 0);
}

static void endIdSpool(const char* why, bool clear_display) {
  logSDf("IdSpool: spool %d no longer shown (%s)", s_id, why);
  s_active = false;
  paintWeightButton(false);
  if (clear_display) clearTagDisplay();
}

void idSpoolAdopt(int spool_id) {
  // A tag put down while the id was being typed is the better answer.
  if (tag_present) {
    logSDf("IdSpool: spool %d not adopted, a tag is on the reader", spool_id);
    return;
  }
  if (s_active) endIdSpool("replaced", false);
  clearTagDisplay();
  querySpoolmanById(spool_id);
  // Failed reads leave sm_found false; querySpoolmanById() already said why.
  if (!sm_found || sm_id != spool_id) {
    logSDf("IdSpool: spool %d could not be loaded", spool_id);
    return;
  }
  s_active    = true;
  s_id        = spool_id;
  s_seen_load = scale_weight_g >= ID_SPOOL_LOAD_MIN_G;
  s_since_ms  = millis();
  s_lifted_ms = 0;
  paintStatus();
  paintWeightButton(true);
  logSDf("IdSpool: spool %d shown without a tag (load %.0fg)", spool_id, scale_weight_g);
}

void idSpoolTick() {
  if (!s_active) return;
  if (tag_present) { endIdSpool("tag took over", false); return; }
  // Cleared or replaced by something else: a backend switch, an archive.
  if (!sm_found || sm_id != s_id) { endIdSpool("display changed", false); return; }
  // The clear button behind the status text, see statusClearSync() in the loop.
  if (statusClearTaken()) { endIdSpool("cleared by hand", true); return; }
  // Without a load cell nothing below can tell: the button and a tag end it.
  if (!g_scale_fitted) return;
  if (scale_weight_g >= ID_SPOOL_LOAD_MIN_G) {
    s_seen_load = true;
    s_lifted_ms = 0;
    return;
  }
  if (!s_seen_load) {
    if (millis() - s_since_ms > ID_SPOOL_IDLE_MS) endIdSpool("never put on the pad", true);
    return;
  }
  // Lifted: shown for the grace a lifted tag gets, so a location or weight
  // the spool is carried away during still finds it. Put back in time, it
  // goes on as before.
  if (!s_lifted_ms) {
    s_lifted_ms = millis();
    logSDf("IdSpool: spool %d lifted, shown for %lu s more", s_id, ID_SPOOL_LIFTED_MS / 1000UL);
  } else if (millis() - s_lifted_ms > ID_SPOOL_LIFTED_MS) {
    endIdSpool("taken off the pad", true);
  }
}
