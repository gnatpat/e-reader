#include "web/apps_upload.h"

#include "config.h"                       // MAX_APPS, MAX_APP_BINARY
#include "pala_app.h"                     // PalaAppHeader, PALA_APP_MAGIC
#include "pure/app_header.h"              // validateAppHeader / status enum
#include "pure/paths.h"                   // sanitizeUploadedAppFilename
#include "state.h"                        // FS, server
#include "storage/app_catalog.h"          // g_apps, loadApps
#include "storage/fs_util.h"              // fsFreeBytesSafe
#include "ui/screens/upload_screen.h"     // UploadScreen::state()
#include "web/chrome.h"                   // successPage, htmlEscape, humanBytes

// ============================================================================
//  App .bin upload — single-pass streaming receiver.
//
//  Files land in /apps/<sanitized>.bin.tmp during the transfer; on
//  UPLOAD_FILE_END we read the first sizeof(PalaAppHeader) bytes back
//  off the temp file and run them through `validateAppHeader`. If that
//  passes, we rename the temp into place and refresh `g_apps` so the
//  on-device AppsScreen sees the new entry without a reboot.
//
//  Header validation here mirrors what the on-device loader does
//  (Stage 6). The intent is "fail at upload time, not at launch time" —
//  if the magic or api_version is wrong, we'd rather the browser show
//  the error now than have a useless entry sit in the launcher.
// ============================================================================

static void handleUploadAppDone() {
  UploadState& s = UploadScreen::state();
  if (!s.appOk) {
    server.send(400, "text/plain; charset=utf-8",
                s.appError.length() ? s.appError : String("App upload failed"));
    return;
  }

  loadApps();  // refresh so AppsScreen sees the new entry on next entry

  String finalPath = String("/apps/") + s.appFinalName;
  size_t storedSize = 0;
  File stored = FS.open(finalPath, "r");
  if (stored) { storedSize = stored.size(); stored.close(); }

  String inner;
  inner.reserve(900);
  inner += "<div class='card'><h2>App installed</h2>";
  inner += "<p class='muted'>Open the device, scroll the library to <b>Apps</b>, and double-click to launch.</p>";
  inner += "<div class='stats'>";
  inner += "<div class='stat'><span class='muted'>App</span><b>"        + htmlEscape(s.appFinalName)   + "</b></div>";
  inner += "<div class='stat'><span class='muted'>Stored size</span><b>"+ humanBytes(storedSize)        + "</b></div>";
  inner += "<div class='stat'><span class='muted'>Apps now</span><b>"   + String(g_apps.count)          + "</b></div>";
  inner += "<div class='stat'><span class='muted'>Free space</span><b>" + humanBytes(fsFreeBytesSafe()) + "</b></div>";
  inner += "</div><div class='actions'><a class='btn' href='/'>Upload another</a></div></div>";
  inner += storageCardHtml();

  String page = successPage(
    "App installed",
    "App saved to /apps/.",
    "&#10003; App ready to run.",
    inner
  );
  server.send(200, "text/html; charset=utf-8", page);
}

// Map an AppHeaderStatus into a user-facing message. Mirrors the
// runApp switch in ui/pala_api_impl.cpp; lives here so the upload route
// can present the same diagnostics at install time.
static const char* validationErrorMessage(AppHeaderStatus s, uint32_t fileApiVer) {
  static char buf[48];
  switch (s) {
    case AppHeaderStatus::Ok:             return "OK";
    case AppHeaderStatus::TooSmall:       return "Invalid app (file too small)";
    case AppHeaderStatus::BadMagic:       return "Invalid app (bad magic)";
    case AppHeaderStatus::BadEntryOffset: return "Invalid app (bad entry offset)";
    case AppHeaderStatus::BadRelocTable:  return "Invalid app (bad reloc table)";
    case AppHeaderStatus::ApiMismatch:
      snprintf(buf, sizeof(buf), "Invalid app (API v%u, need v%u)",
               (unsigned)fileApiVer, (unsigned)PALA_API_VERSION);
      return buf;
  }
  return "Invalid app";
}

static void handleUploadAppStream() {
  UploadState& s = UploadScreen::state();
  HTTPUpload& up = server.upload();

  if (up.status == UPLOAD_FILE_START) {
    s.appOk = false;
    s.appError = "";
    s.appFinalName = "";
    s.appTmpPath = "";

    // Defensive — protects MAX_APPS check from a stale catalog.
    loadApps();
    if (g_apps.count >= MAX_APPS) {
      s.appError = "Apps directory full";
      return;
    }

    // Pre-check free space against the maximum app size — we don't know
    // the upload's final size yet, but we know it can't exceed
    // MAX_APP_BINARY. Lets us fail fast if storage is nearly full
    // rather than burning the whole transfer.
    if (fsFreeBytesSafe() < MAX_APP_BINARY) {
      s.appError = "Not enough free space";
      return;
    }

    // Make sure /apps/ exists; loadApps creates it but we may have
    // bailed out above.
    if (!FS.exists("/apps")) FS.mkdir("/apps");

    s.appFinalName = sanitizeUploadedAppFilename(up.filename);
    s.appTmpPath   = String("/apps/") + s.appFinalName + ".tmp";

    if (FS.exists(s.appTmpPath)) FS.remove(s.appTmpPath);
    s.appTmpFile = FS.open(s.appTmpPath, "w");
    if (!s.appTmpFile) {
      s.appError = "Cannot create temp app file";
      s.appTmpPath = "";
    }
  }
  else if (up.status == UPLOAD_FILE_WRITE) {
    if (s.appError.length() > 0) return;
    if (!s.appTmpFile) return;

    // Hard cap on cumulative size during streaming so a runaway upload
    // can't fill the partition before END fires.
    if (s.appTmpFile.size() + up.currentSize > MAX_APP_BINARY) {
      s.appTmpFile.close();
      if (FS.exists(s.appTmpPath)) FS.remove(s.appTmpPath);
      s.appTmpPath = "";
      s.appError = "App too large (> 48 KB)";
      return;
    }
    s.appTmpFile.write(up.buf, up.currentSize);
  }
  else if (up.status == UPLOAD_FILE_END) {
    if (s.appTmpFile) s.appTmpFile.close();
    if (s.appError.length() > 0 || s.appTmpPath.length() == 0) return;

    if (up.totalSize < sizeof(PalaAppHeader) + 4) {
      if (FS.exists(s.appTmpPath)) FS.remove(s.appTmpPath);
      s.appError = "App binary too small";
      s.appTmpPath = "";
      return;
    }

    // Read the header back and validate it with the same pure routine
    // the on-device loader uses, so install-time and load-time agree on
    // what counts as a valid app.
    uint8_t hdrBuf[sizeof(PalaAppHeader)] = {0};
    File vf = FS.open(s.appTmpPath, "r");
    bool readOk = false;
    if (vf) {
      readOk = (vf.read(hdrBuf, sizeof(hdrBuf)) == (int)sizeof(hdrBuf));
      vf.close();
    }
    if (!readOk) {
      if (FS.exists(s.appTmpPath)) FS.remove(s.appTmpPath);
      s.appError = "Could not read app header";
      s.appTmpPath = "";
      return;
    }

    uint32_t fileApiVer = 0;
    // We only have the header bytes here, but `validateAppHeader` only
    // touches header fields. Pass the *real* file size so the
    // entry/reloc range checks operate against the true bounds.
    AppHeaderStatus st = validateAppHeader(hdrBuf, (size_t)up.totalSize, &fileApiVer);
    if (st != AppHeaderStatus::Ok) {
      if (FS.exists(s.appTmpPath)) FS.remove(s.appTmpPath);
      s.appError = String(validationErrorMessage(st, fileApiVer));
      s.appTmpPath = "";
      return;
    }

    // Commit: atomic rename into the canonical /apps/<name>.bin path.
    String finalPath = String("/apps/") + s.appFinalName;
    if (FS.exists(finalPath)) FS.remove(finalPath);
    if (FS.rename(s.appTmpPath, finalPath)) {
      s.appOk = true;
    } else {
      if (FS.exists(s.appTmpPath)) FS.remove(s.appTmpPath);
      s.appError = "Failed to finalize app upload";
    }
    s.appTmpPath = "";
  }
  else if (up.status == UPLOAD_FILE_ABORTED) {
    if (s.appTmpFile) s.appTmpFile.close();
    if (s.appTmpPath.length() > 0 && FS.exists(s.appTmpPath)) FS.remove(s.appTmpPath);
    s.appTmpPath = "";
    s.appOk = false;
    s.appError = "App upload aborted";
  }
}

static void handleDeleteApp() {
  if (!server.hasArg("name")) {
    server.send(400, "text/plain; charset=utf-8", "missing name");
    return;
  }
  String name = server.arg("name");
  if (name.indexOf('/') >= 0 || name.indexOf('\\') >= 0 || !name.endsWith(".bin")) {
    server.send(400, "text/plain; charset=utf-8", "invalid name");
    return;
  }
  String path = String("/apps/") + name;
  if (FS.exists(path)) FS.remove(path);
  loadApps();   // refresh catalog so the next read sees the deletion

  server.sendHeader("Location", "/files");
  server.send(303);
}

void registerAppUploadRoutes() {
  server.on("/upload-app", HTTP_POST, handleUploadAppDone, handleUploadAppStream);
  server.on("/del-app",    HTTP_POST, handleDeleteApp);   // POST: destructive
}
