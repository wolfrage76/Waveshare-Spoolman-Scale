"""Compile the production raster crop and check rows, padding and refusals."""
import subprocess
import tempfile
from pathlib import Path

root = Path(__file__).parents[1]
source = r'''
#include "services/label_raster.h"
#include <cassert>
#include <vector>

// A raster `width` dots wide whose every byte is 0xFF, the label `content` dots.
static LabelRaster filled(std::vector<uint8_t>& px, uint16_t width, uint16_t height,
                          uint16_t content) {
  const uint16_t row_bytes = (width + 7) / 8;
  px.assign(size_t(row_bytes) * height, 0xFF);
  LabelRaster r{};
  r.width = width;
  r.height = height;
  r.row_bytes = row_bytes;
  r.pixels = px.data();
  r.length = px.size();
  r.content_width = content;
  return r;
}

int main() {
  std::vector<uint8_t> px;

  // The M110S at 40 mm: FilaMan draws 384, the printer takes 320.
  LabelRaster r = filled(px, 384, 3, 320);
  for (uint16_t y = 0; y < 3; ++y)
    for (uint16_t x = 0; x < 48; ++x) px[size_t(y) * 48 + x] = uint8_t(y * 48 + x);
  assert(labelRasterCropWidth(&r, 320));
  assert(r.width == 320 && r.row_bytes == 40 && r.length == 120 && r.height == 3);
  for (uint16_t y = 0; y < 3; ++y)
    for (uint16_t x = 0; x < 40; ++x) assert(px[size_t(y) * 40 + x] == uint8_t(y * 48 + x));
  assert(labelRasterValid(r));

  // A width that ends inside a byte: the bits past it come back white.
  r = filled(px, 384, 2, 300);
  assert(labelRasterCropWidth(&r, 300));
  assert(r.row_bytes == 38 && px[37] == 0xF0 && px[38 + 37] == 0xF0 && px[38] == 0xFF);
  assert(labelRasterValid(r));

  // Already the right width: nothing moves.
  r = filled(px, 320, 2, 320);
  assert(labelRasterCropWidth(&r, 320) && r.width == 320 && r.length == 80);

  // Refused, and left as it was: a label wider than the row, a wider target,
  // a target below the narrowest raster, no raster at all.
  r = filled(px, 384, 2, 360);
  assert(!labelRasterCropWidth(&r, 320) && r.width == 384 && r.row_bytes == 48);
  r = filled(px, 384, 2, 320);
  assert(!labelRasterCropWidth(&r, 400) && r.width == 384);
  assert(!labelRasterCropWidth(&r, LABEL_RASTER_MIN_W - 8) && r.width == 384);
  assert(!labelRasterCropWidth(nullptr, 320));
}
'''
with tempfile.TemporaryDirectory() as tmp:
    cpp = Path(tmp) / "test.cpp"
    cpp.write_text(source)
    binary = Path(tmp) / "test"
    subprocess.run(["c++", "-std=c++17", "-Wall", "-Wextra", "-Werror", "-I", str(root / "src"), str(cpp), "-o", str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
print("Label raster crop tests passed")
