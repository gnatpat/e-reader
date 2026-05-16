#include "web/reset.h"

#include "state.h"
#include "storage/fs_util.h"
#include "storage/library.h"
#include "ui/screens/library_screen.h"   // resetLibraryNav
#include "ui/toast.h"
#include "web/chrome.h"

static void doFactoryReset() {
  // g_bookview was cleared on library entry (which we passed through to get
  // to upload mode, where this handler is served). The remaining helpers
  // wipe ambient ui/library state — wake state and the rest of NVS are
  // nuked by prefs.clear() below.
  Toast::reset();
  resetLibraryNav();

  prefs.clear();
  FS.end();
  delay(100);
  FS.format();
  delay(200);
  if (!FS.begin(true)) return;
  ensureBooksDir();
  loadBooks();
}

static void handleResetConfirm() {
  String out = webPageStart(
    "Factory Reset",
    "Erase all books, bookmarks, progress, and custom assets.",
    "<a href='/'>Back</a>"
  );
  out +=
    "<div class='card'><h2>Confirm reset</h2>"
    "<p><strong>This will delete ALL books, bookmarks and reading progress.</strong></p>"
    "<p class='muted'>The device filesystem will be formatted and settings will return to defaults.</p>"
    "<form method='POST' action='/reset' style='margin-top:14px'><button class='danger' type='submit'>Yes, reset</button></form>"
    "</div>";
  out += webPageEnd();
  server.send(200, "text/html; charset=utf-8", out);
}

static void handleResetDo() {
  doFactoryReset();

  String inner;
  inner.reserve(600);
  inner += "<div class='card'><h2>Factory reset complete</h2><p class='muted'>All books, bookmarks, progress and custom assets were removed. The device is now back to a clean state.</p><div class='actions'><a class='btn' href='/'>Go to home</a><a class='btn secondary' href='/files'>Open files</a></div></div>";
  inner += storageCardHtml();

  String page = successPage(
    "Reset complete",
    "Pala One was reset successfully.",
    "&#10003; Factory reset complete.",
    inner
  );
  server.send(200, "text/html; charset=utf-8", page);
}

void registerResetRoutes() {
  server.on("/reset", HTTP_GET,  handleResetConfirm);
  server.on("/reset", HTTP_POST, handleResetDo);
}
