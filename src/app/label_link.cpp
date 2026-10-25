#include "app/label_link.h"

#include <Arduino.h>
#include <string.h>
#include <strings.h>

#include "app/backend_switch.h"
#include "hardware/sd_logger.h"
#include "services/backend.h"
#include "services/backend_api.h"
#include "services/text_util.h"

// A setting changed on the server reaches the labels within this, without a
// request for every label printed.
#define LABEL_LINK_TTL_MS      600000UL
#define LABEL_LINK_TIMEOUT_MS  3000
// "http://" alone is no address: the scale's own base reads like that unset.
#define LABEL_LINK_MIN_BASE    8
// How much of a refused value the log quotes.
#define LABEL_LINK_LOG_BYTES   60

static char     s_base[LABEL_LINK_LEN] = "";   // "" = the server has none
static uint32_t s_gen      = 0;
static bool     s_known    = false;
static uint32_t s_asked_ms = 0;

static bool startsWithScheme(const char* s) {
  return strncasecmp(s, "http://", 7) == 0 || strncasecmp(s, "https://", 8) == 0;
}

// The server's value as a base for the code: an http(s) address of printable
// ASCII that fits, without the slash at its end. "" stays "" (unset). False
// for anything else; the code then keeps the scale's own address.
static bool acceptBase(const char* raw, char* out, size_t n) {
  out[0] = '\0';
  size_t len = strlen(raw);
  while (len > 0 && raw[len - 1] == '/') len--;
  if (len == 0) return true;
  if (len >= n || len < LABEL_LINK_MIN_BASE || !startsWithScheme(raw)) return false;
  for (size_t i = 0; i < len; i++) {
    if (raw[i] <= ' ' || raw[i] > '~') return false;
  }
  memcpy(out, raw, len);
  out[len] = '\0';
  return true;
}

const char* labelLinkBase() {
  if (s_known && s_gen == backendGeneration() && s_base[0]) return s_base;
  return backendBaseUrl();
}

bool labelLinkDue() {
  if (backendIsFilaMan() || strlen(backendBaseUrl()) < LABEL_LINK_MIN_BASE) return false;
  if (!s_known || s_gen != backendGeneration()) return true;
  return millis() - s_asked_ms >= LABEL_LINK_TTL_MS;
}

void labelLinkFetchAt(const BackendTarget& at, uint32_t gen, LabelLinkAnswer* out) {
  if (!out) return;
  *out = LabelLinkAnswer{};
  out->gen  = gen;
  out->code = backendGetLinkBaseAt(at, out->base, sizeof(out->base), LABEL_LINK_TIMEOUT_MS);
}

void labelLinkFetch(LabelLinkAnswer* out) {
  BackendTarget at;
  backendTargetSnapshot(&at);
  labelLinkFetchAt(at, backendGeneration(), out);
}

void labelLinkTake(const LabelLinkAnswer& a) {
  if (a.gen != backendGeneration()) return;
  // A new server starts without the old one's address.
  const bool fresh = !s_known || s_gen != a.gen;
  if (fresh) s_base[0] = '\0';
  s_known    = true;
  s_gen      = a.gen;
  s_asked_ms = millis();
  // No answer keeps what this server said before, until the next ask.
  if (a.code != 200) {
    if (a.code != BACKEND_NOT_SUPPORTED)
      logSDf("Label: link address not read (HTTP %d), QR codes use %s", a.code, labelLinkBase());
    return;
  }
  char clean[LABEL_LINK_LEN];
  if (!acceptBase(a.base, clean, sizeof(clean))) {
    char shown[LABEL_LINK_LOG_BYTES + 1];
    utf8Cut(a.base, LABEL_LINK_LOG_BYTES, shown, sizeof(shown));
    logSDf("Label: server link address \"%s\" refused (http/https, up to %d characters), "
           "QR codes use %s", shown, LABEL_LINK_LEN - 1, backendBaseUrl());
    s_base[0] = '\0';
    return;
  }
  if (fresh || strcmp(clean, s_base) != 0)
    logSDf("Label: QR codes link to %s", clean[0] ? clean : backendBaseUrl());
  memcpy(s_base, clean, sizeof(s_base));
}

void labelLinkRefresh() {
  if (!labelLinkDue()) return;
  LabelLinkAnswer a;
  labelLinkFetch(&a);
  labelLinkTake(a);
}
