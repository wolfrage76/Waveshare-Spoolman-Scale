#pragma once

// ============================================================
//  PRINTING ON THE PC THROUGH FILAMAN
//
//  For a scale without a Bluetooth printer: the More info header offers
//  "Print label" anyway when the backend is FilaMan, and this card asks
//  which label - the spool's or its filament's. The scale only files a
//  request (FilaMan 1.4.3); a FilaMan tab open in the user's browser offers
//  to print it, and the label is printed from FilaMan's designer with the
//  PC's own print dialog. So a USB printer works too.
//
//  The card has three stages in one place: the question, a short "sending"
//  while the request runs, and the answer - what to do in the browser, or
//  why FilaMan refused. The request blocks the loop for up to a few seconds
//  and runs from pcPrintTick(), never from a button.
// ============================================================

// From the loop: opens the card show_pc_print_pending asked for, for the
// spool in sm_id, and carries out what was tapped on it.
void pcPrintTick();

// For the navigation: every way off the screen takes the card along.
void closePcPrintPopup();
