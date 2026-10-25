#pragma once

#include <lvgl.h>

// ============================================================
//  THE CARD FOR A SPOOL THE INVENTORY DOES NOT KNOW
//
//  Put down a spool that is in no inventory, and the main screen used to
//  answer only by turning two of its buttons into "Link" and "New / Copy".
//  This card says it in words and offers the step that most likely comes
//  next: for a Bambu tag the spool without a tag that matches it, or a new
//  one from the tag when there is none; for any other tag the ways in, as
//  tiles. Every way ends in a flow that exists anyway - the card only
//  chooses the door.
//
//  It opens once per placement, only after a verdict beyond doubt (see
//  lookupUnknownIsCertain()), only on a main screen nothing covers, and not
//  at all with g_unk_ask off. The buttons stay as they are underneath. It
//  closes when the spool is found or another tag replaces it, and when the
//  loop says the spool left the pad - not on a tag that merely dropped out.
// ============================================================

// The lookup's verdict was "not in the inventory". `key` is what the lookup
// asked for: the tray uuid of a Bambu tag, the uid of any other.
void unknownSpoolOffer(bool is_bambu, const char* key);

// The loop decided the spool left the pad (not a tag that merely dropped out
// over a spool still lying there): the card closes, an offer not yet opened is
// dropped, and the next placement may be asked about again, the same spool
// included.
void unknownSpoolForget();

// From the loop: opens the card it was offered, carries out what was tapped
// on it, and closes it once the spool it was about is found or replaced.
void unknownSpoolTick();

lv_obj_t* unknownSpoolPopupScreen();
void closeUnknownSpoolPopup();
