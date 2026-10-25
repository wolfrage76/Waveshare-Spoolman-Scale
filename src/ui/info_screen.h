#pragma once

void showInfoScreen();
// The full-screen QR code of a target; the info screen's tiles open 0 to 4.
#define QR_PRINTERS 5   // the manual's recommended printers, opened from Bluetooth
void showQRPopup(int idx);
