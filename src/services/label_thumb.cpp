#include "services/label_thumb.h"

#include <stddef.h>

#define LT_FULL 255u   // a pixel all of whose dots are black
#define LT_MAX(a, b) ((a) > (b) ? (a) : (b))

// The label as it reads: flat, the raster's columns from x0; upright, turned
// back, so its width is the raster's height.
static uint16_t sourceW(const LabelRaster& r, const LabelThumbSpec& s) {
  return s.upright ? r.height : s.content_w;
}
static uint16_t sourceH(const LabelRaster& r, const LabelThumbSpec& s) {
  return s.upright ? s.content_w : r.height;
}

// Dot (ux, uy) of the label as it reads. Upright, it is raster dot
// (x0 + uy, height - 1 - ux), as the browser's preview turns it.
static bool labelDot(const LabelRaster& r, const LabelThumbSpec& s, uint32_t ux, uint32_t uy) {
  const uint32_t x = s.upright ? s.x0 + uy : s.x0 + ux;
  const uint32_t y = s.upright ? r.height - 1 - ux : uy;
  if (x >= r.width || y >= r.height || !r.pixels) return false;
  return (r.pixels[size_t(y) * r.row_bytes + x / 8] >> (7 - x % 8)) & 1;
}

void labelThumbSize(const LabelRaster& image, const LabelThumbSpec& spec,
                    uint16_t* w, uint16_t* h) {
  const uint32_t sw = sourceW(image, spec), sh = sourceH(image, spec);
  *w = *h = 0;
  if (!sw || !sh || !spec.max_w || !spec.max_h) return;
  // Never larger than it prints; smaller by the one factor that fits both ways.
  uint32_t tw = sw, th = sh;
  if (tw > spec.max_w) { th = th * spec.max_w / tw; tw = spec.max_w; }
  if (th > spec.max_h) { tw = tw * spec.max_h / th; th = spec.max_h; }
  *w = (uint16_t)(tw ? tw : 1);
  *h = (uint16_t)(th ? th : 1);
}

// The dots one screen pixel covers: [x0, x1) x [y0, y1) of the label.
struct DotBox { uint32_t x0, x1, y0, y1; };

// The share of black in the box, 0 to LT_FULL.
static uint8_t coverage(const LabelRaster& r, const LabelThumbSpec& s, const DotBox& b) {
  uint32_t black = 0;
  for (uint32_t y = b.y0; y < b.y1; y++)
    for (uint32_t x = b.x0; x < b.x1; x++)
      if (labelDot(r, s, x, y)) black++;
  return (uint8_t)(black * LT_FULL / ((b.x1 - b.x0) * (b.y1 - b.y0)));
}

void labelThumbFill(const LabelRaster& image, const LabelThumbSpec& spec,
                    uint16_t w, uint16_t h, uint8_t* out) {
  const uint32_t sw = sourceW(image, spec), sh = sourceH(image, spec);
  if (!w || !h || !out) return;
  for (uint32_t oy = 0; oy < h; oy++) {
    // The source dots this pixel covers, at least one each way.
    const uint32_t y0 = oy * sh / h;
    const uint32_t y1 = LT_MAX((oy + 1) * sh / h, y0 + 1);
    for (uint32_t ox = 0; ox < w; ox++) {
      const uint32_t x0 = ox * sw / w;
      const uint32_t x1 = LT_MAX((ox + 1) * sw / w, x0 + 1);
      out[size_t(oy) * w + ox] = coverage(image, spec, DotBox{ x0, x1, y0, y1 });
    }
  }
}
