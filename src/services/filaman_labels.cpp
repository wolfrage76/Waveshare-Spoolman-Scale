// ArduinoJson ahead of anything that could bring lang.h's T() macro along.
#include <ArduinoJson.h>
#include <esp_heap_caps.h>
#include <math.h>

#include "services/filaman_labels.h"

#include <Arduino.h>
#include <HTTPClient.h>

#include "hardware/sd_logger.h"
#include "services/backend_http.h"
#include "services/filaman_api.h"
#include "services/text_util.h"

namespace {
struct SpiRamAllocator : ArduinoJson::Allocator {
  void* allocate(size_t size) override {
    void* ptr = heap_caps_malloc(size, MALLOC_CAP_SPIRAM);
    if (!ptr) ptr = malloc(size);
    return ptr;
  }
  void deallocate(void* pointer) override { heap_caps_free(pointer); }
  void* reallocate(void* ptr, size_t new_size) override {
    void* p = heap_caps_realloc(ptr, new_size, MALLOC_CAP_SPIRAM);
    if (!p) p = realloc(ptr, new_size);
    return p;
  }
};
}  // namespace

#define FM_LABEL_PATH_LEN 192
// A label is far smaller than any real one in millimetres; what lies outside
// is not a size the server meant.
#define FM_LABEL_MM_MAX   1000

// The raster's shape travels in headers, the body is the bare pixels.
static const char* RASTER_HEADERS[] = {
  "X-Image-Width", "X-Image-Height", "X-Row-Bytes", "X-Content-Width",
  "X-Rotated", "X-Preset-Id",
};
static const size_t RASTER_HEADER_COUNT = sizeof(RASTER_HEADERS) / sizeof(RASTER_HEADERS[0]);

static bool hasBase(const char* base_url) { return base_url && base_url[0]; }

// The list sends millimetres with a fraction, or null for a broken preset.
static uint16_t wholeMm(JsonVariantConst v) {
  if (!v.is<float>()) return 0;
  const float mm = v.as<float>();
  return (mm > 0 && mm < FM_LABEL_MM_MAX) ? (uint16_t)lroundf(mm) : 0;
}

int filamanLabelPresets(const char* base_url, const char* api_key,
                        FilamanLabelPresetList* list, uint32_t timeout_ms) {
  if (!list) return -1;
  list->count = 0;
  if (!hasBase(base_url)) return -1;

  BackendHttp http;
  if (!http.begin(String(base_url) + "/api/v1/labels/presets")) return -1;
  http.setTimeout(timeout_ms);
  filamanAddApiKey(http, api_key);
  const int code = http.GET();
  if (code != 200) {
    http.end();
    logSDf("FilaMan: label presets HTTP %d", code);
    return code;
  }

  JsonDocument filter;
  JsonObject keep = filter.to<JsonArray>().add<JsonObject>();
  keep["id"] = true;
  keep["name"] = true;
  keep["width_mm"] = true;
  keep["height_mm"] = true;
  SpiRamAllocator alloc;
  JsonDocument doc(&alloc);
  const DeserializationError err =
    deserializeJson(doc, http.getStream(), DeserializationOption::Filter(filter));
  http.end();
  if (err) {
    logSDf("FilaMan: label presets parse error: %s", err.c_str());
    return -3;
  }
  for (JsonObjectConst p : doc.as<JsonArrayConst>()) {
    if (list->count >= FM_LABEL_PRESETS_MAX) break;
    FilamanLabelPreset& out = list->items[list->count];
    out.id = p["id"] | 0;
    if (out.id <= 0) continue;
    utf8Cut(p["name"] | "", sizeof(out.name) - 1, out.name, sizeof(out.name));
    out.width_mm = wholeMm(p["width_mm"]);
    out.height_mm = wholeMm(p["height_mm"]);
    list->count++;
  }
  logSDf("FilaMan: %d label presets", list->count);
  return 200;
}

// Why the server refused: its "detail" where that is a sentence (a QR too
// small for the printer, a preset it cannot read), else the code.
static void readReason(BackendHttp& http, int code, char* reason, size_t n) {
  if (!reason || !n) return;
  if (code < 0) {
    // No answer at all: HTTPClient's words ("connection refused") say more.
    snprintf(reason, n, "%s", HTTPClient::errorToString(code).c_str());
    return;
  }
  snprintf(reason, n, "HTTP %d", code);
  JsonDocument filter;
  filter["detail"] = true;
  JsonDocument doc;
  if (deserializeJson(doc, http.getStream(), DeserializationOption::Filter(filter))) return;
  const char* detail = doc["detail"] | (const char*)nullptr;
  if (detail && detail[0]) utf8Cut(detail, n - 1, reason, n);
}

static uint16_t headerU16(BackendHttp& http, const char* name) {
  const long v = http.header(name).toInt();
  return (v > 0 && v <= UINT16_MAX) ? (uint16_t)v : 0;
}

// The shape from the headers, checked before any memory is taken for it.
static bool rasterShape(BackendHttp& http, LabelRaster* out) {
  out->width = headerU16(http, "X-Image-Width");
  out->height = headerU16(http, "X-Image-Height");
  out->row_bytes = headerU16(http, "X-Row-Bytes");
  out->content_width = headerU16(http, "X-Content-Width");
  out->length = size_t(out->row_bytes) * out->height;
  if (!labelRasterShapeValid(out->width, out->height, out->row_bytes, out->length)) return false;
  const int size = http.getSize();
  return size < 0 || size_t(size) == out->length;
}

static bool readPixels(BackendHttp& http, LabelRaster* out) {
  out->pixels = (uint8_t*)heap_caps_malloc(out->length, MALLOC_CAP_SPIRAM);
  if (!out->pixels) return false;
  Stream& in = http.getStream();
  size_t got = 0;
  while (got < out->length) {
    const size_t n = in.readBytes((char*)out->pixels + got, out->length - got);
    if (n == 0) break;
    got += n;
  }
  return got == out->length && labelRasterValid(*out);
}

int filamanLabelRender(const FilamanLabelRequest& req, LabelRaster* out,
                       char* reason, size_t reason_size) {
  if (reason && reason_size) reason[0] = '\0';
  if (!out) return -1;
  *out = LabelRaster{};
  if (!hasBase(req.base_url) || req.spool_id <= 0 || req.preset_id <= 0) return -1;

  char path[FM_LABEL_PATH_LEN];
  snprintf(path, sizeof(path),
           "/api/v1/labels/spool/%d/render?format=mono1&dpi=%u&width=%u&align=left"
           "&preset_id=%d&orientation=%s",
           req.spool_id, (unsigned)req.dpi, (unsigned)req.row_width, req.preset_id,
           req.landscape ? "landscape" : "portrait");
  BackendHttp http;
  if (!http.begin(String(req.base_url) + path)) return -1;
  http.setTimeout(req.timeout_ms);
  filamanAddApiKey(http, req.api_key);
  http.collectHeaders(RASTER_HEADERS, RASTER_HEADER_COUNT);
  const unsigned long started = millis();
  const int code = http.GET();
  if (code != 200) {
    readReason(http, code, reason, reason_size);
    http.end();
    logSDf("FilaMan: label of spool #%d, preset %d refused, HTTP %d: %s",
           req.spool_id, req.preset_id, code, reason ? reason : "");
    return code;
  }
  const bool ok = rasterShape(http, out) && readPixels(http, out);
  // "1" when FilaMan turned a portrait preset for the roll (orientation=).
  const String rotated = http.header("X-Rotated"), preset = http.header("X-Preset-Id");
  http.end();
  if (!ok) {
    logSDf("FilaMan: label of spool #%d, preset %d not a raster for this printer (%ux%u)",
           req.spool_id, req.preset_id, (unsigned)out->width, (unsigned)out->height);
    labelRasterFree(out);
    *out = LabelRaster{};
    return FM_LABEL_BAD_RASTER;
  }
  logSDf("FilaMan: label of spool #%d, preset %s, rotated %s, %ux%u in %lu ms",
         req.spool_id, preset.c_str(), rotated == "1" ? "yes" : "no",
         (unsigned)out->width, (unsigned)out->height, millis() - started);
  return 200;
}
