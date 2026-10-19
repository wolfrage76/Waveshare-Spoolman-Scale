#pragma once

#include <stdint.h>

enum LabelFit { LABEL_FIT_NONE, LABEL_FIT_DIRECT, LABEL_FIT_ROTATED };

inline LabelFit labelPresetFit(uint16_t width, uint16_t height,
                               uint16_t roll_width, uint16_t roll_height) {
  if (!width || !height) return LABEL_FIT_NONE;
  if (width == roll_width && height == roll_height) return LABEL_FIT_DIRECT;
  if (width == roll_height && height == roll_width) return LABEL_FIT_ROTATED;
  return LABEL_FIT_NONE;
}
