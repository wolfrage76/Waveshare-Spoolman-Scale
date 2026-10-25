#pragma once

// ============================================================
//  THE PRINT SHEET
//
//  What "Print label" on the More info card opens while the
//  printer screen's "Show before printing" is on: the print
//  button and the spool's or its filament's label on top, as the
//  printer will get it, and under it, a scroll away, the few
//  settings worth changing at the scale - arrangement, upright or
//  flat, label stock - and the way to the printer screen. The
//  fields, the own line and FilaMan's presets stay in the browser.
//
//  Every tap parks a flag in app/deferred_actions.h; the preview
//  renders, NVS is written and the print starts from the loop.
// ============================================================

// From the loop, every pass: carries out what the sheet's taps parked.
void printSheetTick();
// Deletes the sheet and its picture. Safe when it is not open.
void closePrintSheet();
