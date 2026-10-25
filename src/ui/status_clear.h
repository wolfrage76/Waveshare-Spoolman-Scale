#pragma once

// ============================================================
//  THE CLEAR BUTTON IN THE STATUS LINE
//
//  A small x right behind the status text, for a spool that stays on screen
//  although nothing holds it there any more: one shown by its id, and one
//  whose tag was lifted while the display waits out NO_TAG_CLEAR_MS. It sits
//  behind the text rather than at the end of the bar, so the address and the
//  scan counter stay where they are.
//
//  Built on demand into the status bar and deleted again from the loop. Its
//  tap is only parked; the owner of the moment collects it.
// ============================================================

// From the loop, every pass: shows or hides the button, and keeps it behind
// the text when the text changes.
void statusClearSync(bool want);

// True once after the button was tapped.
bool statusClearTaken();
