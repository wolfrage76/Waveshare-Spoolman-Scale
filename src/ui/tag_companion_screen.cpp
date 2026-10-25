#include "tag_companion_screen.h"
#include "navigation.h"
#include "app/app_state.h"
#include "app/deferred_actions.h"

#include <Arduino.h>
#include <lvgl.h>

#include "hardware/sd_logger.h"
#include "lang.h"
#include "services/settings_registry.h"
#include "services/user_options.h"
#include "ui_common.h"
#include "theme.h"

// ============================================================
//  COMPANION FIELD
//
//  A three way choice, so the rows carry no icon of their own -
//  a tick in front of every entry reads as "all three are on".
//  The active one is marked on the right and by its border, the
//  way the other pickers do it. The values and their names come
//  from the descriptor; what stays here is the line under each
//  one saying what goes into the field.
// ============================================================

// Indexed by TagCompanion, alongside OPT_TAG_COMP in the registry.
static const StringID COMP_SUB[TAG_COMP_COUNT] = {
  STR_TAG_COMP_OFF_SUB, STR_TAG_COMP_TAG_SUB, STR_TAG_COMP_NFCID_SUB
};

static void addCompanionRow(lv_obj_t *list, const SettingDesc &s, uint8_t value) {
  const bool active = settingGet(s) == value;

  char buf_t[40];
  copyT(buf_t, sizeof(buf_t), (StringID)s.opt_str[value]);
  char buf_s[64];
  copyT(buf_s, sizeof(buf_s), COMP_SUB[value]);

  lv_obj_t *btn = makeListBtn(list, "", buf_t, buf_s, active);
  markListBtnChosen(btn, active);

  lv_obj_set_user_data(btn, (void *)(intptr_t)value);
  lv_obj_add_event_cb(btn, [](lv_event_t *e) {
    const uint8_t v = (uint8_t)(intptr_t)lv_obj_get_user_data(lv_event_get_target(e));
    const SettingDesc *d = settingById(TAG_COMP_KEY);
    if (!d || v == settingGet(*d)) return;
    settingSet(*d, v);
    logSDf("BTN: Companion field -> %u", (unsigned)v);
    // Through the flag rather than deleting from inside this button's own
    // callback: that path goes through releaseScreen() and lv_obj_del_async().
    show_tag_comp_pending = true;
  }, LV_EVENT_CLICKED, NULL);
}

void buildTagCompanionScreen() {
  logSD("BUILD: TagCompanionScreen");
  releaseScreen(&scr_tag_comp);
  scr_tag_comp = buildOverlayScreen();
  buildSubHeader(scr_tag_comp, T(STR_TAG_COMP),
    [](lv_event_t *e){
      logSD("BTN: Back -> Spoolman options");
      show_spoolman_options_pending = true;
    });

  lv_obj_t *list = buildOptionList(scr_tag_comp);

  const SettingDesc *s = settingById(TAG_COMP_KEY);
  if (!s || !s->opt_str) return;
  for (uint8_t v = 0; v < s->opt_count; v++) addCompanionRow(list, *s, v);
}
