#include "web/web.h"

#include "web/bookmarks.h"
#include "web/chrome.h"
#include "web/files.h"
#include "web/list.h"
#include "web/reset.h"
#include "web/settings.h"
#include "web/upload.h"

// ============================================================================
//  Web routes — split per topic across the files in this directory. This
//  function is the single entry point called from setup() to mount them all.
//  Each `register*Routes` is an `HTTP_GET`/`HTTP_POST` registration on the
//  shared global `server`.
// ============================================================================
void registerWebRoutes() {
  registerChromeRoutes();      // /style.css
  registerFilesRoutes();       // /, /files, /del, /mkdir, /rmdir, /move, /jumppage
  registerBookmarksRoutes();   // /bookmarks, /viewbm, /delbm, /exportbm
  registerListRoutes();        // /list, /list-clear-done
  registerSettingsRoutes();    // /settings, /del-sleep
  registerUploadRoutes();      // /upload, /upload-sleep
  registerResetRoutes();       // /reset
}
