#pragma once

#include <stdint.h>

// ============================================================
//  WHAT A SPOOL LABEL SHOWS
//
//  The one template the label editor in the browser sets: an
//  arrangement, which fields go on the label and a few options.
//  The renderer reads it, nothing else decides. The default is
//  the label as it printed before the editor existed, bit for
//  bit, so a scale that never opens the editor prints as before.
//
//  Built to grow: a field or an option is a bit, a new one goes
//  at the end, none is ever renumbered or reused (the bits are
//  NVS content). LABEL_LAYOUT_VERSION is stored alongside, so a
//  later version can tell a mask saved before a field existed
//  from one that switched it off.
// ============================================================

#define LABEL_LAYOUT_VERSION 1

// Where the blocks go. NVS content like the bits below.
enum LabelPreset : uint8_t {
  LABEL_PRESET_STANDARD = 0,   // maker large on top, band, title, facts beside the code
  LABEL_PRESET_COMPACT  = 1,   // no large maker: it joins the facts, the code grows
  LABEL_PRESET_BIG_QR   = 2,   // the code takes the full height, the text a column
  LABEL_PRESET_MATERIAL_BIG = 3, // the material across a third of the label, a small code
  LABEL_PRESET_TEXT_ONLY = 4,  // no code, the facts large, in two columns where they fit
  LABEL_PRESET_COUNT
};

enum LabelField : uint8_t {
  LF_VENDOR = 0, LF_MATERIAL = 1, LF_NAME = 2, LF_SPOOL_ID = 3,
  LF_COLOR = 4, LF_DATE = 5, LF_QR = 6, LF_BRAND = 7,
  // Off by default: the default stays the label from before, and a mask
  // saved before the field existed has the bit clear already.
  LF_ARTICLE = 8,
  // Off by default as well. The location and the drying change while the
  // label stays; the page says so next to them.
  LF_SPOOL_WEIGHT = 9, LF_NET_WEIGHT = 10, LF_TEMPS = 11, LF_LOCATION = 12,
  LF_DRIED = 13, LF_FREE_TEXT = 14,
  // The drying the filament wants, temperature and hours: the backend's
  // value, else the Bambu tag's. Off by default.
  LF_DRYING = 15,
  LABEL_FIELD_COUNT
};

enum LabelOption : uint8_t {
  LO_MATERIAL_PLAIN = 0,   // the material as a line, not white on a black band
  LO_DATE_ADDED     = 1,   // the day the spool was added, never the first use
  // Upright: the label drawn 30 wide and 40 or 50 tall, turned onto the roll.
  // The head starts on the left, as on FilaMan's portrait presets.
  LO_PORTRAIT       = 2,
  LABEL_OPTION_COUNT
};

// What a label is for: a spool, or the filament it is made of (for a dryer
// box, a sample card, a shelf). Each has a template of its own.
enum LabelKind : uint8_t {
  LABEL_KIND_SPOOL = 0,
  LABEL_KIND_FILAMENT = 1,
  LABEL_KIND_COUNT
};

struct LabelLayout {
  uint8_t  preset;
  uint32_t fields;    // bit n set: LabelField n is on the label
  uint32_t options;   // bit n set: LabelOption n applies
};

// The spool's: the fields of the label from before on, no option, the
// standard arrangement. The filament's: maker, material, name, colour,
// temperatures and the code to the filament's page.
LabelLayout labelLayoutDefault(LabelKind kind = LABEL_KIND_SPOOL);
// The fields a kind of label has at all: a filament has no spool id, no
// dates, no tare, no place.
uint32_t labelKindFields(LabelKind kind);
// Known bits only, a known arrangement: what the browser sent may be anything.
LabelLayout labelLayoutSanitized(const LabelLayout& in);
bool labelLayoutHas(const LabelLayout& layout, LabelField field);
bool labelLayoutOption(const LabelLayout& layout, LabelOption option);

// Whether an arrangement draws a code at all; the QR field means nothing
// where it does not.
bool labelPresetHasQr(uint8_t preset);
// Whether this layout's label carries a code: the arrangement has one and
// the field is on.
bool labelLayoutHasCode(const LabelLayout& layout);
// How many fact lines this layout keeps whole on a roll of this size, upright
// or not, with its code or without. In millimetres, so it holds at 203 and
// 300 dpi. The renderer stops there, and the browser lets no more be on.
int labelFactCapacity(const LabelLayout& layout, uint16_t width_mm, uint16_t length_mm);
// The fields that become a fact line in this arrangement, as a mask: what
// the capacity counts. The header blocks and the code are not among them.
uint32_t labelFactFields(uint8_t preset);

// Cached after the first read; every write goes through the save.
LabelLayout labelLayoutLoad(LabelKind kind = LABEL_KIND_SPOOL);
bool labelLayoutSave(const LabelLayout& layout, LabelKind kind = LABEL_KIND_SPOOL);

// The free line LF_FREE_TEXT prints, empty when none was set. The buffer
// holds 40 characters of up to 4 bytes each, so a line of umlauts or a euro
// sign keeps all 40 (64 bytes cut a line of umlauts after 31).
#define LABEL_FREE_TEXT_CHARS 40
#define LABEL_FREE_TEXT_BYTES (LABEL_FREE_TEXT_CHARS * 4 + 1)
const char* labelLayoutFreeText();
// Cut to the buffer on a character boundary; false when NVS refused it.
bool labelLayoutSaveFreeText(const char* text);
