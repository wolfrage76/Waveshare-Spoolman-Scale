#pragma once

#include <stddef.h>
#include <stdint.h>

#include "services/label_raster.h"

// ============================================================
//  FILAMAN LABEL PRESETS
//
//  FilaMan can render a spool's label from a preset the user laid
//  out in its label designer, as the 1-bit raster the printer takes
//  (FilaMan PR #181, "mono1"). The scale asks for its print row and
//  resolution and gets the label drawn at the row's left edge; the
//  caller moves it to where the stock runs.
//
//  A server without that API answers the preset list with 404, so
//  the list doubles as the probe: no list, no choice offered.
//  FilaMan chooses its configured renderer; the scale only requires
//  the mono1 raster contract, not a particular rendering engine.
// ============================================================

#define FM_LABEL_NAME_LEN    40
#define FM_LABEL_REASON_LEN  120
// More than a person lays out by hand; the rest of a longer list is left off.
#define FM_LABEL_PRESETS_MAX 16

// One preset as the list names it. Sizes in whole millimetres; 0 when the
// server could not tell (a preset with broken dimensions).
struct FilamanLabelPreset {
  int      id;
  char     name[FM_LABEL_NAME_LEN];
  uint16_t width_mm, height_mm;
};

struct FilamanLabelPresetList {
  FilamanLabelPreset items[FM_LABEL_PRESETS_MAX];
  int count;
};

// The user's spool presets. Returns the HTTP code; 200 fills `list`. Any task.
int filamanLabelPresets(const char* base_url, const char* api_key,
                        FilamanLabelPresetList* list, uint32_t timeout_ms);

// One label to fetch: from where, which spool and preset, and the print row
// it is drawn for.
struct FilamanLabelRequest {
  const char* base_url;
  const char* api_key;
  int         spool_id, preset_id;
  uint16_t    dpi, row_width;
  bool        landscape;
  uint32_t    timeout_ms;
};

// Returned when the server answered 200 but the body was not a raster the
// printer can take: wrong shape, cut short, no memory for it.
#define FM_LABEL_BAD_RASTER (-20)

// Fetches the label. On 200 `out` owns PSRAM pixels (labelRasterFree());
// on anything else `out` is empty and `reason` holds the server's own words
// where it gave some (FilaMan's "detail"), else the HTTP code. Any task.
int filamanLabelRender(const FilamanLabelRequest& req, LabelRaster* out,
                       char* reason, size_t reason_size);
