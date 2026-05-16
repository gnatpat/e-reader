#include "web/settings.h"

#include "config.h"
#include "state.h"
#include "ui/font.h"
#include "ui/sleep.h"
#include "web/chrome.h"

static void handleSettings() {
  int curFont = Font::currentBodySize();
  String sel8  = (curFont == 8)  ? " selected" : "";
  String sel10 = (curFont == 10) ? " selected" : "";
  String sel12 = (curFont == 12) ? " selected" : "";
  String sel14 = (curFont == 14) ? " selected" : "";

  int curSleep = Sleep::idleTimeoutSecs();
  String ss30   = (curSleep == 30)   ? " selected" : "";
  String ss60   = (curSleep == 60)   ? " selected" : "";
  String ss120  = (curSleep == 120)  ? " selected" : "";
  String ss300  = (curSleep == 300)  ? " selected" : "";
  String ss600  = (curSleep == 600)  ? " selected" : "";
  String ss1800 = (curSleep == 1800) ? " selected" : "";

  int curGap = Font::currentLineGap();
  String lg0 = (curGap == 0) ? " selected" : "";
  String lg1 = (curGap == 1) ? " selected" : "";
  String lg2 = (curGap == 2) ? " selected" : "";
  String lg3 = (curGap == 3) ? " selected" : "";

  bool hasSleepImg = FS.exists("/sleep.bin");

  String out = webPageStart(
    "Pala One Settings",
    "Firmware " FW_VERSION " configuration page stored directly on the device.",
    "<a href='/'>&#8592; Home</a>"
  );
  out.reserve(out.length() + 4000);

  out +=
    "<div class='card'><h2>Reading</h2>"
    "<form method='POST' action='/settings' accept-charset='UTF-8'>"
    "<div class='grid cols-2'>"
    "<div><label for='font'>Font size</label><select id='font' name='font'>"
    "<option value='8'";  out += sel8;  out += ">8px &mdash; tiny</option>"
    "<option value='10'"; out += sel10; out += ">10px &mdash; small</option>"
    "<option value='12'"; out += sel12; out += ">12px &mdash; medium</option>"
    "<option value='14'"; out += sel14; out += ">14px &mdash; large</option>"
    "</select><div class='hint'>Controls how many lines fit on each page.</div></div>"
    "<div><label for='sleep'>Sleep after</label><select id='sleep' name='sleep'>"
    "<option value='30'";   out += ss30;   out += ">30 seconds</option>"
    "<option value='60'";   out += ss60;   out += ">1 minute</option>"
    "<option value='120'";  out += ss120;  out += ">2 minutes</option>"
    "<option value='300'";  out += ss300;  out += ">5 minutes</option>"
    "<option value='600'";  out += ss600;  out += ">10 minutes</option>"
    "<option value='1800'"; out += ss1800; out += ">30 minutes</option>"
    "</select><div class='hint'>Auto-sleep keeps battery draw low while idle.</div></div>"
    "<div><label for='lgap'>Line spacing</label><select id='lgap' name='lgap'>"
    "<option value='0'"; out += lg0; out += ">0 px &mdash; compact</option>"
    "<option value='1'"; out += lg1; out += ">1 px &mdash; normal</option>"
    "<option value='2'"; out += lg2; out += ">2 px &mdash; relaxed</option>"
    "<option value='3'"; out += lg3; out += ">3 px &mdash; loose</option>"
    "</select><div class='hint'>A small change here can make text much easier to scan.</div></div>"
    "</div>"
    "<div class='actions' style='margin-top:24px'><button type='submit'>Save settings</button><span class='muted'>No extra files, scripts, or fonts.</span></div>"
    "</form></div>"
    "<div class='card'><h2>Screensaver</h2>"
    "<p>Upload raw XBM bytes: <b>3904 bytes</b>, 250&times;122 px, 1-bit, LSB-first, 32 bytes per row.</p>"
    "<p class='muted'>Tip: use <a class='link' href='https://javl.github.io/image2cpp/' target='_blank'>image2cpp</a> with <b>Plain bytes</b>. Invert colors if needed.</p>";

  if (hasSleepImg) {
    out += "<div class='status ok'>&#10003; Custom screensaver active. <a class='link' href='/del-sleep' onclick=\"return confirm('Delete custom screensaver?')\">Delete</a></div>";
  } else {
    out += "<div class='status idle'>Using built-in screensaver.</div>";
  }

  out +=
    "<form method='POST' action='/upload-sleep' enctype='multipart/form-data' style='margin-top:14px'>"
    "<div class='grid'><div><label for='file'>Sleep image file</label><input id='file' type='file' name='file' accept='.bin'></div></div>"
    "<div class='actions'><button type='submit'>Upload image</button></div>"
    "</form></div>";

  out += webPageEnd();
  server.send(200, "text/html; charset=utf-8", out);
}

static void handleSettingsPost() {
  // Font::setBodySize, Font::setLineGap, Sleep::setIdleTimeout all clamp /
  // validate internally — no inline guards needed here.
  if (server.hasArg("font")) {
    int fs = server.arg("font").toInt();
    if (fs != Font::currentBodySize()) Font::setBodySize(fs);
  }
  if (server.hasArg("sleep")) {
    int ss = server.arg("sleep").toInt();
    if (ss != Sleep::idleTimeoutSecs()) Sleep::setIdleTimeout(ss);
  }
  if (server.hasArg("lgap")) {
    int lg = server.arg("lgap").toInt();
    if (lg != Font::currentLineGap()) Font::setLineGap(lg);
  }
  // On-disk page caches are layout-stamped and self-invalidate on load, so
  // a font/lineGap change needs no cross-cutting cleanup here.
  server.sendHeader("Location", "/settings");
  server.send(302, "text/plain", "");
}

static void handleDeleteSleepImg() {
  if (FS.exists("/sleep.bin")) FS.remove("/sleep.bin");
  server.sendHeader("Location", "/settings");
  server.send(302, "text/plain", "");
}

void registerSettingsRoutes() {
  server.on("/settings",  HTTP_GET,  handleSettings);
  server.on("/settings",  HTTP_POST, handleSettingsPost);
  server.on("/del-sleep", HTTP_GET,  handleDeleteSleepImg);
}
