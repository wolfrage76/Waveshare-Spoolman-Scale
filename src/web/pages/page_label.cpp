// Labels: the template the scale prints a spool's label with. An
// arrangement, the fields to print and a few options on the left, the label
// as the printer would get it on the right, drawn by the scale itself from
// the last spool it found.
//
// The preview is the print raster, cut to the label and sent as a 1-bit BMP:
// the renderer runs on the loop task, which is where this handler runs too,
// and it makes no request. The dates a label carries are the one thing the
// scan does not hold; the state route parks a request for them and the page
// asks again until they are there.
//
// One template for every label size for now. The layout's bits are appended
// to, never renumbered, so the page can grow without losing what was saved.
//
// On a FilaMan with the label API the label can come from a preset laid out
// in FilaMan's designer instead (app/label_server.h). That one needs HTTP for
// the list and the preview, which these handlers must not make: the state
// route asks for them, a task fetches, and the page polls until they are in.
#include "web/web_pages.h"

#include <Arduino.h>
#include <WebServer.h>
#include <esp_heap_caps.h>

#include "app/app_state.h"
#include "app/deferred_actions.h"
#include "app/label_server.h"
#include "app/label_spool.h"
#include "hardware/sd_logger.h"
#include "services/ble_service.h"
#include "services/label_bmp.h"
#include "services/label_fit.h"
#include "services/label_layout.h"
#include "services/label_printer.h"
#include "services/label_render.h"
#include "ui/printer_screen.h"
#include "web/web_access.h"
#include "web/web_shell.h"
// Last on purpose: T() is a macro and ArduinoJson uses T as a template
// parameter, so lang.h has to come after anything that pulls it in.
#include "lang.h"

static const char* label() { return T(STR_W_NAV_LABEL); }

// The fields in the order the page lists them, with their captions.
struct FieldRow { LabelField field; int caption; };
static const FieldRow FIELD_ROWS[] = {
  { LF_VENDOR,   STR_LBL_VENDOR },
  { LF_MATERIAL, STR_LBL_MATERIAL },
  { LF_NAME,     STR_W_L_F_NAME },
  { LF_SPOOL_ID, STR_W_TAG_SPOOLID },
  { LF_COLOR,    STR_LBL_L_COLOR },
  { LF_ARTICLE,  STR_LBL_ARTICLE_NO_SHORT },
  { LF_DATE,     STR_W_L_F_DATE },
  { LF_QR,       STR_W_L_F_QR },
  { LF_BRAND,    STR_W_L_F_BRAND },
};
static const int FIELD_ROW_COUNT = sizeof(FIELD_ROWS) / sizeof(FIELD_ROWS[0]);

struct OptionRow { LabelOption option; int caption; };
static const OptionRow OPTION_ROWS[] = {
  { LO_MATERIAL_PLAIN, STR_W_L_O_PLAIN },
  { LO_DATE_ADDED,     STR_W_L_O_ADDED },
};
static const int OPTION_ROW_COUNT = sizeof(OPTION_ROWS) / sizeof(OPTION_ROWS[0]);

static const int PRESET_CAPTIONS[LABEL_PRESET_COUNT] = {
  STR_W_L_P_STANDARD, STR_W_L_P_COMPACT, STR_W_L_P_BIG_QR,
};

// What the preview shows until a spool has been found since boot: a spool
// as a Bambu tag would bring it, so every field has something to show.
static void sampleSpool(SpoolLabelData* d) {
  *d = SpoolLabelData{};
  d->id = 42;
  snprintf(d->vendor, sizeof(d->vendor), "Bambu Lab");
  snprintf(d->material, sizeof(d->material), "PLA");
  snprintf(d->name, sizeof(d->name), "PLA Basic Jade White");
  snprintf(d->color, sizeof(d->color), "F2F2E8");
  snprintf(d->article, sizeof(d->article), "10100");
  snprintf(d->first_used, sizeof(d->first_used), "03.03.2026");
  snprintf(d->added, sizeof(d->added), "01.03.2026");
}

// FilaMan's presets for the source row, and how the server's preview stands.
// "fm" is the list's state; the preview is asked for only while a preset is
// the source.
static void serverJson(String& j, const LabelPrinterConfig& c, int spool_id) {
  static const char* const LIST_STATES[] = { "unknown", "loading", "ready", "absent", "failed" };
  static const char* const PREVIEW_STATES[] = { "none", "loading", "ready", "failed" };
  const FilamanLabelPresetList* list = nullptr;
  const LabelServerListState st = labelServerList(&list);
  j += F(",\"source\":");
  j += String(labelServerPresetId());
  j += F(",\"fm\":\"");
  j += LIST_STATES[st];
  j += F("\",\"presets\":[");
  for (int i = 0; st == LS_READY && i < list->count; i++) {
    const FilamanLabelPreset& p = list->items[i];
    const LabelFit fit = labelPresetFit(p.width_mm, p.height_mm, c.media_width_mm, c.media_length_mm);
    if (i) j += ',';
    j += F("{\"id\":");      j += String(p.id);
    j += F(",\"name\":\"");  j += jsonEsc(p.name);
    j += F("\",\"size\":\""); j += String(p.width_mm); j += F(" x ");
    j += String(p.height_mm);
    j += F(" mm\",\"fit\":");
    j += fit != LABEL_FIT_NONE ? F("true") : F("false");
    j += F(",\"rotated\":");
    j += fit == LABEL_FIT_ROTATED ? F("true") : F("false");
    j += '}';
  }
  j += ']';
  if (!labelServerInUse()) return;
  const LabelRaster* r = nullptr;
  const char* reason = "";
  const LabelServerPreviewState ps = labelServerPreview(c, spool_id, &r, &reason);
  j += F(",\"prev\":\"");
  j += PREVIEW_STATES[ps];
  j += F("\",\"prevRev\":");
  j += String(labelServerPreviewRev());
  j += F(",\"prevReason\":\"");
  j += jsonEsc(reason);
  j += '"';
}

static String stateJson() {
  const LabelLayout l = labelLayoutLoad();
  const LabelPrinterConfig c = labelPrinterLoadConfig();
  SpoolLabelData s{};
  const bool have = labelSpoolLast(&s);
  // The preview waits for these; the loop asks the backend, once per spool.
  if (have && !labelSpoolDatesKnown()) label_dates_pending = true;
  String j;
  j.reserve(1400);
  j += F("{\"preset\":");   j += String((unsigned)l.preset);
  j += F(",\"fields\":");   j += String((unsigned long)l.fields);
  j += F(",\"options\":");  j += String((unsigned long)l.options);
  j += F(",\"spool\":");
  if (have) {
    j += F("{\"id\":");      j += String(s.id);
    j += F(",\"text\":\"");
    j += jsonEsc(s.vendor); j += ' '; j += jsonEsc(s.name);
    j += F("\",\"on\":");
    // The display keeps a spool after it left; the tag says whether it is there.
    j += (tag_present && sm_found && sm_id == s.id) ? F("true") : F("false");
    j += '}';
  } else {
    j += F("null");
  }
  j += F(",\"dates\":");
  j += (!have || labelSpoolDatesKnown()) ? F("true") : F("false");
  j += F(",\"media\":\"");
  j += String((unsigned)c.media_width_mm); j += F(" x ");
  j += String((unsigned)c.media_length_mm); j += F(" mm\",\"model\":\"");
  j += jsonEsc(labelPrinterProfile(c.model).brand); j += ' ';
  j += jsonEsc(labelPrinterProfile(c.model).name);
  j += F("\",\"configured\":");
  j += labelPrinterConfigured(c) ? F("true") : F("false");
  j += F(",\"canPrint\":");
  const bool can = labelPrinterConfigured(c) && bleEnabled() && bleStackAvailable() &&
                   !bleStackStuck();
  j += can ? F("true") : F("false");
  j += F(",\"lastPrint\":");
  const int last = printerLastLabelResult();
  if (last < 0) j += F("null");
  else { j += '"'; j += jsonEsc(T(last)); j += '"'; }
  j += F(",\"lastReason\":\"");
  if (last == STR_PRN_ERR_SERVER_LABEL) j += jsonEsc(labelServerLastReason());
  j += '"';
  serverJson(j, c, have ? s.id : 0);
  j += '}';
  return j;
}

// The source on top of the template: the scale's own or a FilaMan preset.
// Hidden until the server has a list; the options are built by the script,
// a list rather than tabs because the user decides how many presets there are.
static void sourceRow(String& h) {
  h += F("<div class='field' id='lsrcf' style='display:none'><label>");
  h += T(STR_W_L_SOURCE);
  h += F("</label><select id='lsrc'></select><span class='hint'>");
  h += T(STR_W_L_SRC_HINT);
  h += F("</span><span class='hint' id='lsrv' style='display:none'>");
  h += T(STR_W_L_SRC_SERVER);
  h += F("</span><span class='msg' id='lsrc-s'></span></div>");
}

// The left card: the arrangement as tabs, the fields and options as switches.
// The values are set by the script from the state, so the markup is static.
static void templateCard(String& h) {
  h += F("<div class='card'><h2>");
  h += T(STR_W_L_TEMPLATE);
  h += F("</h2>");
  sourceRow(h);
  h += F("<div id='ltpl'><div class='field'><label>");
  h += T(STR_W_L_ARRANGE);
  h += F("</label><div class='btabs' id='lpre'>");
  for (int i = 0; i < LABEL_PRESET_COUNT; i++) {
    h += F("<button class='btab' data-p='");
    h += String(i);
    h += F("'>");
    h += T(PRESET_CAPTIONS[i]);
    h += F("</button>");
  }
  h += F("</div><span class='hint'>");
  h += T(STR_W_L_ARRANGE_HINT);
  h += F("</span></div><div class='field'><label>");
  h += T(STR_W_L_FIELDS);
  h += F("</label><div id='lf' class='lb-sw'>");
  for (int i = 0; i < FIELD_ROW_COUNT; i++) {
    h += F("<label class='check'><span class='switch'><input type='checkbox' data-b='");
    h += String((int)FIELD_ROWS[i].field);
    h += F("'><i></i></span>");
    h += T(FIELD_ROWS[i].caption);
    h += F("</label>");
  }
  h += F("</div></div><div class='field'><label>");
  h += T(STR_W_L_OPTIONS);
  h += F("</label><div id='lo'>");
  for (int i = 0; i < OPTION_ROW_COUNT; i++) {
    h += F("<label class='check'><span class='switch'><input type='checkbox' data-b='");
    h += String((int)OPTION_ROWS[i].option);
    h += F("'><i></i></span>");
    h += T(OPTION_ROWS[i].caption);
    h += F("</label>");
  }
  h += F("</div><span class='hint'>");
  h += T(STR_W_L_DATE_HINT);
  h += F("</span></div><div class='field'><div class='inrow'><button id='lr' class='quiet'>");
  h += T(STR_W_DEFAULTS);
  h += F("</button><span class='msg' id='ls-s'></span></div></div></div></div>");
}

// The right card: the label as the scale draws it, what it was drawn for,
// and a print of it.
static void previewCard(String& h) {
  h += F("<div class='card lb-prev'><h2>");
  h += T(STR_W_TAG_PREVIEW);
  h += F("</h2><div class='lb-paper'><img id='lp' alt=''></div>"
         "<span class='msg bad' id='lp-s'></span>"
         "<div class='row'><span class='k'>");
  h += T(STR_W_L_SPOOL);
  h += F("</span><span class='v' id='lsp'></span></div><div class='row'><span class='k'>");
  h += T(STR_PRN_MEDIA);
  h += F("</span><span class='v' id='lme'></span></div>"
         "<div class='hint lb-note' id='lsa' style='display:none'>");
  h += T(STR_W_L_SAMPLE);
  h += F("</div><div class='hint lb-note' id='lnp' style='display:none'>");
  h += T(STR_W_L_NO_PRINTER);
  h += F(" <a href='/printer'>");
  h += T(STR_W_NAV_PRINTER);
  h += F("</a></div><div class='field lb-print'><div class='inrow'><button id='lpb'>");
  h += T(STR_W_L_PRINT);
  h += F("</button><span class='hint' style='flex:1'>");
  h += T(STR_W_L_PRINT_HINT);
  h += F("</span></div><span class='msg' id='lpb-s'></span>"
         "<div class='row' id='llr' style='display:none'><span class='k'>");
  h += T(STR_W_L_LAST_PRINT);
  h += F("</span><span class='v' id='ll'></span></div></div></div>");
}

// The page's own look: the label as paper, the switches in two columns.
static void styles(String& h) {
  h += F("<style>"
         // One print dot per CSS pixel, never stretched: scaled by an
         // uneven factor the dots came out blurred and blocky. A label wider
         // than the card shrinks to it.
         ".lb-paper{background:#fff;border:1px solid var(--line);border-radius:8px;"
         "padding:6px;margin:0 auto 12px;line-height:0;width:fit-content;max-width:100%;"
         "box-sizing:border-box}"
         ".lb-paper img{max-width:100%;height:auto;display:block;image-rendering:pixelated}"
         ".lb-paper img:not([src]){visibility:hidden}"
         ".lb-sw{display:grid;grid-template-columns:1fr 1fr;gap:10px 16px}"
         ".lb-sw .check+.check{margin-top:0}"
         ".lb-note{margin-top:12px}"
         ".lb-print{margin-top:16px}"
         "#lpre{flex-wrap:wrap}"
         // One column on a phone: the preview first, so a switch shows its
         // effect without scrolling.
         "@media(max-width:700px){.lb-prev{order:-1}}"
         "@media(max-width:420px){.lb-sw{grid-template-columns:1fr}}"
         "</style>");
}

// The state, the preview and the saving; the handlers are bound below.
// $, flash, post, postFlash and getJson come from /app.js.
static void scriptFunctions(String& h) {
  h += F("<script>");
  h += webShellJsStrings();
  h += F("const L={fail:");
  h += jsStr(T(STR_W_L_PREVIEW_FAIL));
  h += F(",offPad:");
  h += jsStr(T(STR_W_L_OFF_PAD));
  h += F(",scale:");
  h += jsStr(T(STR_W_L_SRC_SCALE));
  h += F(",srcLoading:");
  h += jsStr(T(STR_W_L_SRC_LOADING));
  h += F(",srcFailed:");
  h += jsStr(T(STR_W_L_SRC_FAILED));
  h += F(",loading:");
  h += jsStr(T(STR_W_L_PREVIEW_LOADING));
  h += F(",noSpool:");
  h += jsStr(T(STR_W_L_SRC_NO_SPOOL));
  h += F(",rotated:");
  h += jsStr(T(STR_W_L_SRC_ROTATED));
  h += F("};"
         "let S=null,shown='',timer=0,srcKey='';"
         "function mask(box){let m=0;"
         "document.querySelectorAll('#'+box+' input').forEach(function(i){"
         "if(i.checked)m|=1<<(+i.dataset.b);});return m;}"
         "function setMask(box,m){document.querySelectorAll('#'+box+' input').forEach(function(i){"
         "i.checked=!!(m&(1<<(+i.dataset.b)));});}"
         // A FilaMan preset: the server's picture once it is in, else why not.
         "function srv(d){return d.source>0&&d.fm!=='absent';}"
         "function picServer(d){const m=$('lp-s');"
         "$('lp').parentNode.style.display=d.prev==='ready'?'':'none';"
         "if(d.prev==='ready'){const k='s|'+d.prevRev;if(k===shown)return;shown=k;"
         "m.textContent='';$('lp').src='/api/label/preview.bmp?t='+Date.now();return;}"
         "shown='';$('lp').removeAttribute('src');m.classList.toggle('bad',d.prev!=='loading');"
         "m.textContent=d.prev==='loading'?L.loading:(d.prevReason||(d.spool?L.fail:L.noSpool));}"
         // The picture is drawn anew only when something it shows changed.
         "function pic(d){if(srv(d)){picServer(d);return;}"
         "$('lp').parentNode.style.display='';$('lp-s').classList.add('bad');"
         "const k=[d.preset,d.fields,d.options,d.spool?d.spool.id:0,"
         "d.dates,d.media,d.model].join('|');if(k===shown)return;shown=k;"
         "$('lp-s').textContent='';$('lp').src='/api/label/preview.bmp?t='+Date.now();}"
         // The source list, built anew only when the presets or the choice
         // changed; a preset the loaded roll cannot take is listed, not offered.
         "function srcRow(d){const on=d.fm!=='absent'&&d.fm!=='unknown';"
         "$('lsrcf').style.display=on?'':'none';"
         // Our own status line only: a "saved" flashed there stays until it fades.
         "const m=$('lsrc-s'),t=d.fm==='loading'?L.srcLoading:(d.fm==='failed'?L.srcFailed:'');"
         "if(t||m.dataset.st)m.textContent=t;m.dataset.st=t;"
         "$('ltpl').style.display=srv(d)?'none':'';$('lsrv').style.display=srv(d)?'':'none';"
         "const k=d.source+'|'+JSON.stringify(d.presets);if(k===srcKey)return;srcKey=k;"
         "const b=$('lsrc');b.textContent='';"
         "[{id:0,name:L.scale,fit:true}].concat(d.presets).forEach(function(p){"
         "const o=document.createElement('option');o.value=String(p.id);"
         "o.textContent=p.name+(p.rotated?' \u00b7 '+L.rotated:(p.fit?'':' \u00b7 '+p.size));"
         "o.disabled=!p.fit&&p.id!==d.source;"
         "b.appendChild(o);});b.value=String(d.source);}"
         "function paint(d){S=d;srcRow(d);"
         "document.querySelectorAll('#lpre .btab').forEach(function(b){"
         "b.classList.toggle('on',+b.dataset.p===d.preset);});"
         "setMask('lf',d.fields);setMask('lo',d.options);"
         "$('lsp').textContent=d.spool?(d.spool.text+' \u00b7 #'+d.spool.id+(d.spool.on?'':' '+L.offPad)):'-';"
         "$('lme').textContent=d.media+' \u00b7 '+d.model;"
         "$('lsa').style.display=d.spool||srv(d)?'none':'';"
         "$('lnp').style.display=d.configured?'none':'';"
         "$('lpb').disabled=!d.spool||!d.canPrint;"
         "$('ll').textContent=(d.lastPrint||'')+(d.lastReason?' ('+d.lastReason+')':'');"
         "$('llr').style.display=d.lastPrint?'':'none';"
         "pic(d);"
         // The dates come from the server a moment later; a new spool on the
         // pad shows up on the next look.
         "const busy=!d.dates||d.fm==='loading'||d.prev==='loading';"
         "clearTimeout(timer);timer=setTimeout(load,busy?1500:5000);}"
         "function load(){if(document.hidden)return;"
         "getJson('/api/label').then(function(d){if(d)paint(d);});}"
         "function save(p,f,o){"
         "postFlash('/api/label',p+','+f+','+o,'ls-s',2500).then(load);}");
}

// Every handler is bound here rather than written into an onclick
// attribute: a page body is JavaScript inside a C++ string literal, and an
// attribute is the one place where the two levels of quoting collide.
static void scriptBindings(String& h) {
  h += F("document.querySelectorAll('#lpre .btab').forEach(function(b){"
         "b.addEventListener('click',function(){if(S)save(+b.dataset.p,S.fields,S.options);});});"
         "document.querySelectorAll('#lf input,#lo input').forEach(function(i){"
         "i.addEventListener('change',function(){if(S)save(S.preset,mask('lf'),mask('lo'));});});"
         "$('lr').addEventListener('click',function(){postFlash('/api/label/reset','','ls-s',2500).then(load);});"
         "$('lsrc').addEventListener('change',function(){"
         "postFlash('/api/label/source',$('lsrc').value,'lsrc-s',2500).then(load);});"
         "$('lp').addEventListener('error',function(){$('lp-s').textContent=L.fail;shown='';});"
         // Hard dot edges only where the dots are enlarged; shrunk, the
         // nearest dot would drop whole rows of them.
         "$('lp').addEventListener('load',function(){var i=$('lp');"
         "i.style.imageRendering=i.clientWidth*(window.devicePixelRatio||1)>=i.naturalWidth?'pixelated':'auto';});"
         // The print takes about ten seconds on the scale; the verdict is
         // fetched after that and shown on the last line.
         "$('lpb').addEventListener('click',function(){"
         "postFlash('/api/label/print','','lpb-s',4000).then(function(){setTimeout(load,12000);});});"
         "document.addEventListener('visibilitychange',load);"
         "load();"
         "</script>");
}

static String body() {
  String h;
  h.reserve(7000);
  styles(h);
  h += F("<div class='grid'>");
  templateCard(h);
  previewCard(h);
  h += F("</div>");
  scriptFunctions(h);
  scriptBindings(h);
  return h;
}

// A raster's label part, from dot x0 on, as a 1-bit BMP.
static void sendRasterBmp(WebServer& srv, const LabelRaster& r, uint16_t x0) {
  const uint16_t w = r.content_width;
  const uint32_t row = labelBmpRowBytes(w);
  const uint32_t bytes = labelBmpFileBytes(w, r.height);
  uint8_t* bmp = (uint8_t*)heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM);
  if (!bmp) {
    logSD("Web: no PSRAM for the label preview");
    srv.send(503, "text/plain", T(STR_W_L_PREVIEW_FAIL));
    return;
  }
  labelBmpHeader(w, r.height, bmp);
  for (uint16_t y = 0; y < r.height; y++)
    labelBmpRow(r, x0, w, y, bmp + LABEL_BMP_HEADER_BYTES + size_t(y) * row);
  srv.sendHeader("Cache-Control", "no-store");
  srv.setContentLength(bytes);
  srv.send(200, "image/bmp", "");
  srv.sendContent((const char*)bmp, bytes);
  heap_caps_free(bmp);
}

// The server's label for the last spool: in hand, on its way, or refused.
// The server draws it at the row's left edge.
static void sendServerPreview(WebServer& srv, const LabelPrinterConfig& c) {
  SpoolLabelData spool{};
  const int id = labelSpoolLast(&spool) ? spool.id : 0;
  // The picture the state route announced, even when it has just grown old
  // enough to be fetched again: the page asks for it right after.
  const LabelRaster* in_hand = labelServerPreviewInHand(c, id);
  if (in_hand) {
    sendRasterBmp(srv, *in_hand, 0);
    return;
  }
  const LabelRaster* r = nullptr;
  const char* reason = "";
  switch (labelServerPreview(c, id, &r, &reason)) {
    case LSP_READY:
      sendRasterBmp(srv, *r, 0);
      return;
    case LSP_LOADING:
      srv.send(202, "text/plain", T(STR_W_L_PREVIEW_LOADING));
      return;
    default:
      srv.send(409, "text/plain",
               reason[0] ? reason : T(id ? STR_W_L_PREVIEW_FAIL : STR_W_L_SRC_NO_SPOOL));
  }
}

// The label as the printer would get it, cut to the label, as a 1-bit BMP.
static void sendPreview(WebServer& srv) {
  const LabelPrinterConfig c = labelPrinterLoadConfig();
  if (labelServerInUse()) {
    sendServerPreview(srv, c);
    return;
  }
  SpoolLabelData spool{};
  if (!labelSpoolLast(&spool)) sampleSpool(&spool);
  LabelRaster r{};
  if (!labelRenderSpool(c, labelLayoutLoad(), spool, &r)) {
    labelRasterFree(&r);
    srv.send(409, "text/plain", T(STR_W_L_PREVIEW_FAIL));
    return;
  }
  sendRasterBmp(srv, r, labelPrinterContentX(c));
  labelRasterFree(&r);
}

// "preset,fields,options" as numbers; anything unknown in them is dropped.
static bool parseLayout(const String& v, LabelLayout* out) {
  const int a = v.indexOf(',');
  const int b = a < 0 ? -1 : v.indexOf(',', a + 1);
  if (a <= 0 || b <= a) return false;
  LabelLayout l{};
  l.preset = (uint8_t)strtoul(v.c_str(), nullptr, 10);
  l.fields = strtoul(v.c_str() + a + 1, nullptr, 10);
  l.options = strtoul(v.c_str() + b + 1, nullptr, 10);
  *out = labelLayoutSanitized(l);
  return true;
}

static void routes(WebServer &srv) {
  srv.on("/api/label", HTTP_GET, [&srv]() {
    if (!webRequire(srv, GATE_CONFIG, T(STR_W_NAV_LABEL))) return;
    srv.send(200, "application/json", stateJson());
  });

  srv.on("/api/label", HTTP_POST, [&srv]() {
    if (!webRequire(srv, GATE_CONFIG, T(STR_W_NAV_LABEL))) return;
    LabelLayout l{};
    if (!parseLayout(srv.arg("plain"), &l)) { srv.send(400, "text/plain", "bad layout"); return; }
    const bool ok = labelLayoutSave(l);
    srv.send(ok ? 200 : 500, "application/json", ok ? "{\"ok\":true}" : "{\"ok\":false}");
  });

  srv.on("/api/label/reset", HTTP_POST, [&srv]() {
    if (!webRequire(srv, GATE_CONFIG, T(STR_W_NAV_LABEL))) return;
    const bool ok = labelLayoutSave(labelLayoutDefault());
    srv.send(ok ? 200 : 500, "application/json", ok ? "{\"ok\":true}" : "{\"ok\":false}");
  });

  // The source: 0 for the scale's own layout, else a FilaMan preset's id.
  srv.on("/api/label/source", HTTP_POST, [&srv]() {
    if (!webRequire(srv, GATE_CONFIG, T(STR_W_NAV_LABEL))) return;
    const String v = srv.arg("plain");
    char* end = nullptr;
    const long id = strtol(v.c_str(), &end, 10);
    if (v.isEmpty() || *end || id < 0 || id > INT32_MAX) {
      srv.send(400, "text/plain", "bad source");
      return;
    }
    const bool ok = labelServerSetPresetId((int)id);
    srv.send(ok ? 200 : 500, "application/json", ok ? "{\"ok\":true}" : "{\"ok\":false}");
  });

  srv.on("/api/label/preview.bmp", HTTP_GET, [&srv]() {
    if (!webRequire(srv, GATE_CONFIG, T(STR_W_NAV_LABEL))) return;
    sendPreview(srv);
  });

  // Parks the print like the test print: the loop renders and prints the
  // last spool under the overlay, the page fetches the verdict afterwards.
  srv.on("/api/label/print", HTTP_POST, [&srv]() {
    if (!webRequire(srv, GATE_CONFIG, T(STR_W_NAV_LABEL))) return;
    SpoolLabelData s{};
    if (!labelSpoolLast(&s)) { srv.send(409, "text/plain", T(STR_PRN_ERR_NO_SPOOL)); return; }
    if (!labelPrinterConfigured(labelPrinterLoadConfig())) { srv.send(409, "text/plain", T(STR_PRN_ERR_NO_PRINTER)); return; }
    if (!bleEnabled()) { srv.send(409, "text/plain", T(STR_PRN_ERR_BLE_OFF)); return; }
    if (bleStackStuck()) { srv.send(409, "text/plain", T(STR_PRN_ERR_STUCK)); return; }
    print_last_label_pending = true;
    logSDf("Web: label print for spool #%d requested", s.id);
    srv.send(200, "text/plain", T(STR_W_P_QUEUED));
  });
}

extern const WebPage PAGE_LABEL;
const WebPage PAGE_LABEL = {
  "/label", label, GATE_CONFIG, nullptr,
  body, routes
};
