#pragma once

#include <stdint.h>

#include "services/label_raster.h"

// ============================================================
//  A LABEL AS A PICTURE FOR THE DISPLAY
//
//  The label part of a print raster as an 8-bit coverage map the
//  touchscreen draws: 0 white, 255 black. At most the size it
//  prints, smaller only where the screen has less room: then each
//  pixel is the share of black dots it covers, so text shrinks to
//  grey edges instead of losing strokes. An upright label comes
//  back upright. Pure: no LVGL, no allocation, no network.
// ============================================================

// Which part of the raster, and how much room there is.
struct LabelThumbSpec {
  uint16_t x0, content_w;    // the label's columns in the print row
  bool     upright;          // drawn turned onto the roll (LO_PORTRAIT)
  uint16_t max_w, max_h;     // the room on screen, in pixels
};

// The picture's size for this raster and room; 0 x 0 for an empty raster.
void labelThumbSize(const LabelRaster& image, const LabelThumbSpec& spec,
                    uint16_t* w, uint16_t* h);

// Fills w * h bytes, row by row, for a size labelThumbSize() gave.
void labelThumbFill(const LabelRaster& image, const LabelThumbSpec& spec,
                    uint16_t w, uint16_t h, uint8_t* out);
