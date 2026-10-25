#pragma once

#include <lvgl.h>
#include <stdint.h>

// ============================================================
//  CHOICE GRID: ONE PICK FROM A HANDFUL OF CARDS
//
//  A popup that lays a list out as a grid of cards, so a choice
//  is one tap instead of tapping a row round to it. Each
//  card has a caption, a value, an optional shape and a tag
//  below. Built for the printer screen's model and label stock;
//  the closing works like the status picker's.
// ============================================================

// More cards than the screen holds scroll; the cap only bounds the arrays.
#define CHOICE_GRID_MAX 24

enum ChoiceTag : uint8_t { CHOICE_TAG_NONE = 0, CHOICE_TAG_GOOD, CHOICE_TAG_WARN };

struct ChoiceCard {
  const char* caption;      // small line above; null for none, "" keeps its room
  char value[28];           // the card's name
  int tag_text;             // a StringID: the line below, when tag is not NONE
  ChoiceTag tag;
  // A label outline in proportion, in mm; 0 draws none.
  uint8_t shape_w_mm, shape_h_mm;
  bool enabled;             // a disabled card is shown but cannot be picked
};

// The answer, delivered from the loop one pass after the grid is gone.
// -1 means dismissed without a choice.
typedef void (*ChoicePickCb)(int index);

// Safe to call from an event callback: it only creates. cols is 1 to 3.
// title is a StringID, kept an int so this header does not pull in lang.h.
void showChoiceGrid(int title, const ChoiceCard* cards, int count,
                    int current, int cols, ChoicePickCb cb);

// Takes the grid down without an answer.
void closeChoiceGrid();

// Releases the overlay and, one pass later, delivers the answer.
void handleChoiceGridDeferredActions();
