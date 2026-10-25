"""Compile the production accent folder and check what the large label fonts get to spell."""
import subprocess
import tempfile
from pathlib import Path

root = Path(__file__).parents[1]
source = r'''
#include "services/accent_fold.h"
#include <cassert>
#include <cstring>

static void same(const char* in, const char* want, bool changed) {
  char out[64];
  assert(accentFold(in, out, sizeof(out)) == changed);
  assert(strcmp(out, want) == 0);
}

int main() {
  // French as a maker, a material or a filament name may carry it.
  same("Société", "Societe", true);
  same("Œuvre à côté", "OEuvre a cote", true);
  same("L’été…", "L'ete...", true);
  same("naïve ñ ø å Ÿ", "naive n o a Y", true);
  same("Æsir Þór þ", "AEsir Thor th", true);
  same("« PLA »", "\" PLA \"", true);
  same("Bleu\xC2\xA0Océan", "Bleu Ocean", true);
  // What the large fonts spell themselves stays, ASCII included.
  same("Grün Ärger ÜBER ß 230 °C", "Grün Ärger ÜBER ß 230 °C", false);
  same("PLA Tough+", "PLA Tough+", false);
  same("", "", false);
  // Unknown letters are copied as they are, so the caller still sees them.
  same("Привет", "Привет", false);
  // A broken lead byte is copied byte by byte, never read past the NUL.
  same("a\xC3", "a\xC3", false);
  // No room: the copy ends on a character boundary, the NUL always fits.
  char tiny[3];
  assert(accentFold("éab", tiny, sizeof(tiny)) && strcmp(tiny, "ea") == 0);
  assert(!accentFold("Œ", tiny, 2) && tiny[0] == 0);
  assert(!accentFold("ab", tiny, 1) && tiny[0] == 0);
  assert(!accentFold(nullptr, tiny, sizeof(tiny)) && tiny[0] == 0);
  assert(!accentFold("a", nullptr, 0));
  return 0;
}
'''

with tempfile.TemporaryDirectory() as tmp:
    cpp = Path(tmp) / "t.cpp"
    binary = Path(tmp) / "t"
    cpp.write_text(source)
    subprocess.run(["c++", "-std=c++17", "-Wall", "-Wextra", "-Werror", "-I", str(root / "src"), str(cpp), "-o", str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
print("accent fold: ok")
