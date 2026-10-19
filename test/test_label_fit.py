"""Compile the production preset-fit helper and check both roll orientations."""
import subprocess
import tempfile
from pathlib import Path

root = Path(__file__).parents[1]
source = r'''
#include "services/label_fit.h"
#include <cassert>

int main() {
  struct Case { unsigned width, height, roll_width, roll_height; LabelFit want; };
  const Case cases[] = {
    {40, 30, 40, 30, LABEL_FIT_DIRECT},
    {30, 40, 40, 30, LABEL_FIT_ROTATED},
    {30, 40, 30, 40, LABEL_FIT_DIRECT},
    {40, 30, 30, 40, LABEL_FIT_ROTATED},
    {40, 40, 40, 40, LABEL_FIT_DIRECT},
    {40, 40, 40, 30, LABEL_FIT_NONE},
    {50, 30, 40, 30, LABEL_FIT_NONE},
    {20, 30, 40, 30, LABEL_FIT_NONE},
    {0, 0, 0, 0, LABEL_FIT_NONE},
    {0, 30, 0, 30, LABEL_FIT_NONE},
    {40, 0, 0, 40, LABEL_FIT_NONE},
    {40, 30, 0, 0, LABEL_FIT_NONE},
  };
  for (const auto& c : cases)
    assert(labelPresetFit(c.width, c.height, c.roll_width, c.roll_height) == c.want);
}
'''
with tempfile.TemporaryDirectory() as tmp:
    cpp = Path(tmp) / "test.cpp"
    cpp.write_text(source)
    binary = Path(tmp) / "test"
    subprocess.run(["c++", "-std=c++17", "-Wall", "-Wextra", "-Werror", "-I", str(root / "src"), str(cpp), "-o", str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
print("Label preset fit tests passed")
