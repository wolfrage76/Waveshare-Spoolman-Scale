#pragma once

#include <stdint.h>

// ============================================================
//  WHERE A LABEL'S QR CODE LINKS TO
//
//  The server's own setting for the address its labels carry -
//  Spoolman's "Base URL", BamBuddy's "External URL" - so a code
//  printed here opens what one printed by the server opens, also
//  behind a reverse proxy or from outside the house. Asked only
//  when a label needs it, kept per server and asked again after
//  LABEL_LINK_TTL_MS. Without one the code keeps the address the
//  scale talks to.
// ============================================================

// An 80-character address, the budget LABEL_QR_LEN leaves for it.
#define LABEL_LINK_LEN      81
// Room for what the server sends, so a longer value is seen and refused
// rather than cut into a wrong address.
#define LABEL_LINK_RAW_LEN  160

// One answer of the server, filled on any task, taken in by the loop.
struct LabelLinkAnswer {
  int code;                       // HTTP code, BACKEND_NOT_SUPPORTED on FilaMan
  uint32_t gen;                   // backendGeneration() when asked
  char base[LABEL_LINK_RAW_LEN];  // as the server holds it
};

// The base address for the code: the server's setting where it has one,
// else backendBaseUrl(). No request. Loop task.
const char* labelLinkBase();

// Whether the setting is to be asked for: never asked of this server, or
// the answer is older than LABEL_LINK_TTL_MS. False on FilaMan and without
// an address. Loop task.
bool labelLinkDue();

// Asks the server. Makes an HTTP request. Loop task: it reads where the
// backend is as it goes.
void labelLinkFetch(LabelLinkAnswer* out);

// The same against a snapshot the loop took (backendTargetSnapshot()) and the
// generation it took it under, so a backend switched in the meantime neither
// shapes the request nor owns the answer. From any task.
struct BackendTarget;
void labelLinkFetchAt(const BackendTarget& at, uint32_t gen, LabelLinkAnswer* out);

// Keeps an answer, unless it came from a server the scale has since left.
// Loop task.
void labelLinkTake(const LabelLinkAnswer& a);

// labelLinkDue(), then fetch and take. Makes an HTTP request: from the
// loop only, never from a handler.
void labelLinkRefresh();
