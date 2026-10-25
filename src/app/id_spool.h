#pragma once

// ============================================================
//  A SPOOL ON SCREEN WITHOUT A TAG
//
//  Typed in by its id (ui/spool_id_pad.h) and shown like a scanned one, so
//  everything that works on the spool on screen - weight, location, AMS,
//  drying, labels - works on it too. Two things differ from a tag:
//
//  - Nothing is saved on its own. Automatic weighing keys on a tag; an id
//    typed by hand is one digit away from another spool, so the weight goes
//    out only when "Update weight" is tapped.
//  - It needs its own end, since no tag can leave. It ends ID_SPOOL_LIFTED_MS
//    after the spool that was put on the pad is taken off again (the grace a
//    lifted tag gets), when nothing is put on within ID_SPOOL_IDLE_MS, when
//    the clear button in the status line is tapped, and the moment a tag
//    turns up and takes over. Without a load cell only the last two apply.
// ============================================================

bool idSpoolActive();

// Loop task only: fetches the spool and shows it, see the header comment.
void idSpoolAdopt(int spool_id);

// From the loop: ends the state on the events above.
void idSpoolTick();
