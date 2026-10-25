#pragma once

#include <Arduino.h>

#include "services/label_printer.h"

// The model and label size lists, as <option> rows for a <select>: the
// printer page and the label page offer the same two, and both post to the
// printer page's routes. The current choice is selected.
void webPrinterModelOptions(String& h, const LabelPrinterConfig& c);
// A size set elsewhere and not in the table is listed too, selected and
// inert, so it is shown rather than silently replaced.
void webPrinterMediaOptions(String& h, const LabelPrinterConfig& c);
