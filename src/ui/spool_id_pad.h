#pragma once

#include <lvgl.h>

// ============================================================
//  SPOOL BY ID
//
//  For a spool without a tag: its id typed in, the spool shown beside the
//  keys for a look, and only a confirmed one handed on. The pad is the price
//  pad's family (keys left, answers right) and is built on demand and freed
//  again. Its buttons only park what was tapped; spoolIdPadTick() asks the
//  server and closes the pad from the loop.
//
//  The button that opens it stands in the left slot of the main screen's bar
//  while nothing is on screen, see updateLinkButton().
// ============================================================

// Builds the main screen's "Spool by ID" button into the bar, hidden.
lv_obj_t* buildSpoolByIdButton(lv_obj_t* bar);

// Opens the pad. `done` gets the confirmed spool id; on Cancel it is not called.
void showSpoolIdPad(void (*done)(int spool_id));

bool spoolIdPadOpen();
void closeSpoolIdPad();

// From the loop: asks the server for a typed id, repaints the answer column,
// carries out Cancel and the confirmation.
void spoolIdPadTick();
