"""Compile the production UID list helpers and check that merging counts each tag once."""
import subprocess
import tempfile
from pathlib import Path

root = Path(__file__).parents[1]
source = r'''
#include "services/tag_uid.h"
#include <cassert>
#include <cstring>

int main() {
  char seen[CARD_UIDS_MAX] = "";
  // One NTAG in three places and three notations: the relation keeps colons,
  // extra.tag and the companion nfc_id keep plain hex. That is one tag.
  assert(cardUidsMerge(seen, sizeof(seen), "04:94:41:BB:D1:2A:81"));
  assert(cardUidsMerge(seen, sizeof(seen), "049441BBD12A81"));
  assert(cardUidsMerge(seen, sizeof(seen), "\"049441bbd12a81\""));
  assert(cardUidsCount(seen) == 1 && strcmp(seen, "049441BBD12A81") == 0);

  // A Bambu spool: two chips and the tray uuid in the relation, the tray uuid
  // again in the companion field. Three tags.
  seen[0] = '\0';
  assert(cardUidsMerge(seen, sizeof(seen),
                       "6B:EF:96:FF,AB:86:95:FF,F6FF8C41070B44AD9AEACBD729C14D00"));
  assert(cardUidsMerge(seen, sizeof(seen), "F6FF8C41070B44AD9AEACBD729C14D00"));
  assert(cardUidsCount(seen) == 3);

  // Empty entries and an empty value change nothing.
  assert(cardUidsMerge(seen, sizeof(seen), ""));
  assert(cardUidsMerge(seen, sizeof(seen), " , ,"));
  assert(cardUidsCount(seen) == 3);

  // A list that does not fit says so and stays well formed.
  char tiny[20] = "";
  assert(cardUidsMerge(tiny, sizeof(tiny), "049441BBD12A81"));
  assert(!cardUidsMerge(tiny, sizeof(tiny), "6BEF96FF"));
  assert(strcmp(tiny, "049441BBD12A81") == 0);

  // The unlink compares a single value with the same rule.
  assert(cardUidsContain("\"049441BBD12A81\"", "04:94:41:BB:D1:2A:81"));
  assert(!cardUidsContain("F6FF8C41070B44AD9AEACBD729C14D00", "6B:EF:96:FF"));
  return 0;
}
'''

with tempfile.TemporaryDirectory() as tmp:
    cpp = Path(tmp) / "t.cpp"
    binary = Path(tmp) / "t"
    cpp.write_text(source)
    subprocess.run(["c++", "-std=c++17", "-Wall", "-Wextra", "-Werror", "-I", str(root / "src"),
                    str(cpp), str(root / "src" / "services" / "tag_uid.cpp"), "-o", str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
print("card uids merge: ok")
