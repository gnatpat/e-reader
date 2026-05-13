#include "web/web.h"

#include <WiFi.h>
#include <esp_wifi.h>
#include <esp_bt.h>

#include "hal/display.h"
#include "pure/hashing.h"
#include "pure/paths.h"
#include "storage/book_state.h"
#include "storage/bookmarks.h"
#include "storage/fs_util.h"
#include "storage/library.h"
#include "storage/list_items.h"
#include "storage/page_cache.h"
#include "storage/settings_store.h"
#include "ui/reader.h"   // renderCurrentPage for font-change live-reflow
#include "ui/screens/bookmarks/preview_screen.h"
#include "ui/screens/library_screen.h"
#include "ui/screens/list_screen.h"
#include "ui/screens/reader_screen.h"
#include "ui/screens/upload_screen.h"  // owns the singleton upload session state
#include "ui/text.h"

// ============================================================================
//  Web UI helpers
// ============================================================================
static String webUiStyle() {
  return String(
    "<style>"
    ":root{--bg:#f3efe7;--card:#fff;--line:#ddd4c7;--line-soft:#ece5d9;--text:#1f2328;--muted:#667085;--link:#3c5a7a;--ok:#216e39;--okbg:#e7f6ec;--warn:#8a5a00;--warnbg:#fff4d6;--danger:#6e2a2a}"
    "*{box-sizing:border-box}"
    "body{margin:0;background:var(--bg);color:var(--text);font:15px/1.45 system-ui,sans-serif}"
    ".wrap{max-width:820px;margin:0 auto;padding:18px}"
    ".wide{max-width:1020px}"
    ".top{display:flex;justify-content:space-between;align-items:flex-start;gap:12px;margin-bottom:14px}"
    ".top a,.link{color:var(--link);text-decoration:none}"
    ".top a:hover,.link:hover{text-decoration:underline}"
    ".muted{color:var(--muted);font-size:13px}"
    ".card{background:var(--card);border:1px solid var(--line);border-radius:14px;padding:14px 15px;margin:0 0 14px;box-shadow:0 1px 0 rgba(0,0,0,.03)}"
    ".grid{display:grid;gap:12px}"
    ".actions{display:flex;flex-wrap:wrap;gap:10px;align-items:center;margin-top:14px}"
    ".nav{display:flex;flex-wrap:wrap;gap:10px 14px;font-size:14px}"
    ".nav a{color:var(--link);text-decoration:none}"
    ".list{list-style:none;padding:0;margin:0}"
    ".list li{padding:11px 0;border-top:1px solid var(--line-soft)}"
    ".list li:first-child{border-top:0;padding-top:0}"
    ".row{display:flex;justify-content:space-between;gap:12px;align-items:flex-start}"
    ".meta{color:var(--muted);font-size:13px}"
    ".pill{display:inline-block;background:#f6f2ea;color:#6b6358;border-radius:999px;padding:3px 8px;font-size:12px}"
    ".pre{white-space:pre-wrap;line-height:1.45;padding:12px;border:1px solid var(--line);border-radius:10px;background:#fcfaf7}"
    ".danger{background:var(--danger)}"
    ".banner-ok{background:var(--okbg);color:var(--ok);border:1px solid #cfe9d7;border-radius:12px;padding:12px 13px;margin-bottom:14px}"
    ".banner-warn{background:var(--warnbg);color:var(--warn);border:1px solid #ecd9a3;border-radius:12px;padding:12px 13px;margin-bottom:14px}"
    ".stats{display:grid;gap:10px;grid-template-columns:repeat(2,minmax(0,1fr));margin-top:12px}"
    ".stat{padding:11px 12px;border:1px solid var(--line-soft);border-radius:12px;background:#fcfaf7}"
    ".stat b{display:block;font-size:17px;line-height:1.2;margin-top:2px}"
    ".bar{height:10px;border-radius:999px;background:#ece5d9;overflow:hidden;border:1px solid #e0d7ca;margin-top:12px}"
    ".bar > span{display:block;height:100%;background:#3c5a7a}"
    ".stack{display:grid;gap:8px}"
    ".small{font-size:13px}"
    "button,.btn{display:inline-flex;align-items:center;justify-content:center;border:0;border-radius:10px;background:#1f2328;color:#fff;padding:10px 14px;font:600 14px system-ui,sans-serif;text-decoration:none;cursor:pointer}"
    ".btn.secondary{background:#eef2f6;color:#334e68;border:1px solid #d8e0e8}"
    "input[type=text],input[type=file],select{width:100%;box-sizing:border-box;border:1px solid #c9c2b8;border-radius:10px;background:#fff;padding:10px;font:inherit}"
    "h1,h2,h3,p{margin:0}"
    "h1,h2,h3{margin-bottom:6px}"
    "p + p{margin-top:10px}"
    "@media(min-width:760px){.stats{grid-template-columns:repeat(4,minmax(0,1fr))}}"
    "@media(max-width:640px){.row,.top{flex-direction:column}.wrap{padding:14px}}"
    "</style>"
  );
}

static String webPageStart(const String& title, const String& subtitle, const String& navHtml, bool wide = false) {
  String out;
  out.reserve(1100);
  out = "<!doctype html><html><head><meta charset='utf-8'><meta name='viewport' content='width=device-width,initial-scale=1'><title>";
  out += title;
  out += "</title>";
  out += webUiStyle();
  out += "</head><body><div class='wrap";
  if (wide) out += " wide";
  out += "'><div class='top'><div><h1>";
  out += title;
  out += "</h1><div class='muted'>";
  out += subtitle;
  out += "</div></div>";
  if (navHtml.length() > 0) {
    out += "<div class='nav'>";
    out += navHtml;
    out += "</div>";
  }
  out += "</div>";
  return out;
}

static String webPageEnd() {
  return String("</div></body></html>");
}

static String htmlEscape(const String& in) {
  String out;
  out.reserve(in.length() + 16);
  for (size_t i = 0; i < in.length(); i++) {
    char c = in[i];
    if (c == '&') out += "&amp;";
    else if (c == '<') out += "&lt;";
    else if (c == '>') out += "&gt;";
    else if (c == '"') out += "&quot;";
    else if (c == '\'') out += "&#39;";
    else out += c;
  }
  return out;
}

static String humanBytes(size_t bytes) {
  if (bytes < 1024) return String(bytes) + " B";
  if (bytes < (1024UL * 1024UL)) return String(bytes / 1024.0f, 1) + " KB";
  return String(bytes / 1024.0f / 1024.0f, 2) + " MB";
}

static int storageUsedPct() {
  size_t totalBytes = fsTotalBytesSafe();
  size_t usedBytes = fsUsedBytesSafe();
  if (totalBytes == 0) return 0;
  int pct = (int)((usedBytes * 100UL) / totalBytes);
  if (pct < 0) pct = 0;
  if (pct > 100) pct = 100;
  return pct;
}

static String storageCardHtml(const char* title = "Storage") {
  size_t totalBytes = fsTotalBytesSafe();
  size_t usedBytes = fsUsedBytesSafe();
  size_t freeBytes = fsFreeBytesSafe();
  int pct = storageUsedPct();

  String out;
  out.reserve(900);
  out += "<div class='card'><h2>";
  out += title;
  out += "</h2><div class='stats'>";
  out += "<div class='stat'><span class='muted'>Books</span><b>" + String(g_library.bookCount) + "</b></div>";
  out += "<div class='stat'><span class='muted'>Used</span><b>" + humanBytes(usedBytes) + "</b></div>";
  out += "<div class='stat'><span class='muted'>Free</span><b>" + humanBytes(freeBytes) + "</b></div>";
  out += "<div class='stat'><span class='muted'>Total</span><b>" + humanBytes(totalBytes) + "</b></div>";
  out += "</div><div class='bar'><span style='width:" + String(pct) + "%'></span></div>";
  out += "<div class='muted' style='margin-top:8px'>" + String(pct) + "% of internal storage currently used.</div></div>";
  return out;
}

static String successPage(const String& title, const String& subtitle, const String& banner, const String& innerHtml) {
  String out = webPageStart(title, subtitle, "<a href='/'>Home</a><a href='/files'>Files</a><a href='/settings'>Settings</a>");
  out += "<div class='banner-ok'>" + banner + "</div>";
  out += innerHtml;
  out += webPageEnd();
  return out;
}

// ============================================================================
//  Web handlers
// ============================================================================
static void handleRoot() {
  loadBooks();

  size_t totalBytes = FS.totalBytes();
  size_t usedBytes = FS.usedBytes();
  size_t freeBytes = (totalBytes >= usedBytes) ? (totalBytes - usedBytes) : 0;

  String subtitle = "Firmware ";
  subtitle += FW_VERSION;
  subtitle += " &middot; ";
  subtitle += String(g_library.bookCount);
  subtitle += " books &middot; Free: ";
  subtitle += humanBytes(freeBytes);
  subtitle += " / ";
  subtitle += humanBytes(totalBytes);

  String out = webPageStart(
    "Pala One",
    subtitle,
    "<a href='/files'>Files</a><a href='/bookmarks'>Bookmarks</a><a href='/list'>List</a><a href='/settings'>Settings</a><a href='/reset'>Factory reset</a>"
  );

  out += storageCardHtml();

  if (fsTotalBytesSafe() == 0 || fsFreeBytesSafe() < 8192) {
    out += "<div class='banner-warn'>&#9888; Storage is not available or almost full. If uploads fail, delete books or use Factory reset from this web UI.</div>";
  }

  out +=
    "<div class='card'><h2>Upload book</h2>"
    "<p class='muted'>Send UTF-8 plain text files to <b>/books</b> on the device, then sort them into folders from the Files page.</p>"
    "<form method='POST' action='/upload' enctype='multipart/form-data' accept-charset='UTF-8' style='margin-top:14px'>"
    "<input type='file' name='file' accept='.txt,text/plain' required>"
    "<div class='actions'><button type='submit'>Upload</button><a class='btn secondary' href='/files'>Manage files</a></div>"
    "</form></div>";

  out += "<div class='card'><h2>Notes</h2><p class='muted'>Uploaded books are normalized and compacted before saving, so a source TXT can be larger than the final stored file. The reader is optimized for UTF-8 plain text and Latin-based languages.</p></div>";

  out += webPageEnd();
  server.send(200, "text/html; charset=utf-8", out);
}

static void handleFiles() {
  loadBooks();
  String out = webPageStart(
    "Files",
    "Manage books, folders and library structure for Pala One.",
    "<a href='/'>Home</a><a href='/bookmarks'>Bookmarks</a><a href='/settings'>Settings</a>",
    true
  );

  out +=
    "<div class='card'><h2>Create folder</h2>"
    "<form method='POST' action='/mkdir' class='stack' accept-charset='UTF-8' style='margin-top:12px'>"
    "<input type='text' name='folder' placeholder='books or classics/english' maxlength='64'>"
    "<div class='actions'><button type='submit'>Create folder</button><span class='muted'>Folders live inside /books.</span></div>"
    "</form></div>";

  out += "<div class='card'><h2>Folders</h2>";
  if (g_library.folderCount == 0) {
    out += "<p class='muted'>No folders yet. Books currently live in the root of /books.</p>";
  } else {
    out += "<ul class='list'>";
    for (int i = 0; i < g_library.folderCount; i++) {
      out += "<li><div class='row'><div><span class='pill'>";
      out += htmlEscape(prettyRelativeLabel(String(g_library.folders[i])));
      out += "<span></span></div><div><form method='POST' action='/rmdir' style='display:inline'>";
      out += "<input type='hidden' name='folder' value='";
      out += htmlEscape(g_library.folders[i]);
      out += "'><button type='submit' class='btn secondary' onclick=\"return confirm('Delete folder? Only empty folders can be deleted.')\">Delete</button></form></div></div></li>";
    }
    out += "</ul>";
  }
  out += "</div>";

  out += "<div class='card'><h2>Library files</h2>";
  if (g_library.bookCount >= MAX_BOOKS) out += "<p style='color:#b91c1c;font-weight:600'>&#9888; Library full (80 books max). Delete books to make room.</p>";
  if (g_library.folderCount >= MAX_FOLDERS) out += "<p style='color:#b91c1c;font-weight:600'>&#9888; Folder limit reached (32 max).</p>";

  if (g_library.bookCount == 0) {
    out += "<p class='muted'>No books uploaded yet.</p>";
  } else {
    out += "<ul class='list'>";
    for (int i = 0; i < g_library.bookCount; i++) {
      String bookPath = String(g_library.books[i].path);
      String folderLabel = g_library.books[i].folder[0] ? prettyRelativeLabel(g_library.books[i].folder) : String("Root");
      int savedPage = savedPageForBookPath(bookPath) + 1;
      if (savedPage < 1) savedPage = 1;

      out += "<li><div class='row'><div><h3>";
      out += htmlEscape(String(g_library.books[i].name));
      out += "</h3><div class='meta'>";
      out += String((int)g_library.books[i].size);
      out += " bytes &middot; folder: ";
      out += htmlEscape(folderLabel);
      out += " &middot; current page: ";
      out += String(savedPage);
      out += "</div>";

      out += "<form method='POST' action='/jumppage' class='stack small' accept-charset='UTF-8' style='margin-top:10px'>";
      out += "<input type='hidden' name='id' value='" + String(i) + "'>";
      out += "<div class='row' style='align-items:end;gap:10px'><div style='flex:1'><input type='text' name='page' value='" + String(savedPage) + "' inputmode='numeric' placeholder='Page'></div><div><button type='submit'>Jump</button></div></div>";
      out += "<div class='muted'>Set the page that should open next on the device.<br><span class='muted'>The first open may take a moment.</span></div></form>";

      out += "<form method='POST' action='/move' class='stack small' accept-charset='UTF-8' style='margin-top:10px'>";
      out += "<input type='hidden' name='id' value='" + String(i) + "'>";
      out += "<input type='text' name='folder' value='" + htmlEscape(String(g_library.books[i].folder)) + "' placeholder='leave blank for root' maxlength='64'>";
      out += "<div class='actions'><button type='submit'>Move</button><span class='muted'>Use the exact folder path.</span></div></form></div>";
      out += "<div><form method='POST' action='/del' style='display:inline'><input type='hidden' name='id' value='" + String(i) + "'>";
      out += "<button type='submit' class='btn secondary' onclick=\"return confirm('Delete file?')\">Delete</button></form></div></div></li>";
    }
    out += "</ul>";
  }

  out += "</div>";
  out += webPageEnd();
  server.send(200, "text/html; charset=utf-8", out);
}

static void handleDelete() {
  if (!server.hasArg("id")) {
    server.send(400, "text/plain; charset=utf-8", "missing id");
    return;
  }

  loadBooks();
  int id = server.arg("id").toInt();
  if (id < 0 || id >= g_library.bookCount) {
    server.send(400, "text/plain; charset=utf-8", "bad id");
    return;
  }

  String path = String(g_library.books[id].path);
  if (g_reader.currentBookPath == path) {
    clearCurrentBookState();
    syncWakeState(false);
  }

  if (FS.exists(path)) FS.remove(path);
  deleteBookMetadata(path);
  resetOffsetCache();

  server.sendHeader("Location", "/files");
  server.send(302, "text/plain", "");
}

static void handleCreateFolder() {
  ensureBooksDir();
  if (!server.hasArg("folder")) {
    server.send(400, "text/plain; charset=utf-8", "missing folder");
    return;
  }

  loadBooks();
  String folder = sanitizeFolderInput(server.arg("folder"));
  if (folder.length() == 0) {
    server.send(400, "text/plain; charset=utf-8", "bad folder");
    return;
  }
  if (g_library.folderCount >= MAX_FOLDERS) {
    server.send(409, "text/plain; charset=utf-8", "folder limit reached");
    return;
  }

  String fullPath = "/books/" + folder;
  if (!ensureDirRecursive(fullPath)) {
    server.send(500, "text/plain; charset=utf-8", "mkdir failed");
    return;
  }

  server.sendHeader("Location", "/files");
  server.send(302, "text/plain", "");
}

static void handleDeleteFolder() {
  if (!server.hasArg("folder")) {
    server.send(400, "text/plain; charset=utf-8", "missing folder");
    return;
  }

  String folder = sanitizeFolderInput(server.arg("folder"));
  if (folder.length() == 0) {
    server.send(400, "text/plain; charset=utf-8", "bad folder");
    return;
  }

  String fullPath = "/books/" + folder;
  if (!FS.exists(fullPath)) {
    server.send(404, "text/plain; charset=utf-8", "folder not found");
    return;
  }
  if (!isDirEmpty(fullPath)) {
    server.send(409, "text/plain; charset=utf-8", "folder not empty");
    return;
  }
  if (!FS.rmdir(fullPath)) {
    server.send(500, "text/plain; charset=utf-8", "delete failed");
    return;
  }

  server.sendHeader("Location", "/files");
  server.send(302, "text/plain", "");
}

static void handleMoveBook() {
  loadBooks();
  if (!server.hasArg("id")) {
    server.send(400, "text/plain; charset=utf-8", "missing id");
    return;
  }

  int id = server.arg("id").toInt();
  if (id < 0 || id >= g_library.bookCount) {
    server.send(400, "text/plain; charset=utf-8", "bad id");
    return;
  }

  String oldPath = String(g_library.books[id].path);
  String folder = sanitizeFolderInput(server.arg("folder"));
  String destDir = (folder.length() == 0) ? String("/books") : String("/books/") + folder;

  if (!ensureDirRecursive(destDir)) {
    server.send(500, "text/plain; charset=utf-8", "folder create failed");
    return;
  }

  String newPath = destDir + "/" + lastPathComponent(oldPath);
  if (newPath == oldPath) {
    server.sendHeader("Location", "/files");
    server.send(302, "text/plain", "");
    return;
  }
  if (FS.exists(newPath)) {
    server.send(409, "text/plain; charset=utf-8", "destination exists");
    return;
  }

  bool wasCurrent = (g_reader.currentBookPath == oldPath);
  if (wasCurrent && g_reader.file) g_reader.file.close();

  if (!FS.rename(oldPath, newPath)) {
    server.send(500, "text/plain; charset=utf-8", "move failed");
    return;
  }

  migrateBookMetadata(oldPath, newPath);
  resetOffsetCache();
  if (wasCurrent) g_reader.file = FS.open(newPath, "r");

  server.sendHeader("Location", "/files");
  server.send(302, "text/plain", "");
}

static void handleJumpPageWeb() {
  loadBooks();
  if (!server.hasArg("id") || !server.hasArg("page")) {
    server.send(400, "text/plain; charset=utf-8", "missing id/page");
    return;
  }

  int id = server.arg("id").toInt();
  if (id < 0 || id >= g_library.bookCount) {
    server.send(400, "text/plain; charset=utf-8", "bad id");
    return;
  }

  int page = server.arg("page").toInt();
  if (page < 1) page = 1;
  int zeroBasedPage = page - 1;

  String path = String(g_library.books[id].path);
  String key = prefKeyForBook(path);
  prefs.putInt((key + "_p").c_str(), zeroBasedPage);

  if (g_reader.currentBookPath == path) {
    g_reader.pageIndex = zeroBasedPage;
    if (g_reader.pageIndex < 0) g_reader.pageIndex = 0;
    resetSaveThrottle();
    saveProgressThrottled(true);
    if (g_reader.file) {
      savePageOffsetCacheForBook(g_reader.currentBookPath, g_reader.file.size());
    }
  }

  server.sendHeader("Location", "/files");
  server.send(302, "text/plain", "");
}

static void handleUploadDone() {
  if (!UploadScreen::state().bookOk) {
    server.send(400, "text/plain; charset=utf-8", UploadScreen::state().bookError.length() ? UploadScreen::state().bookError : "Upload failed");
    return;
  }

  loadBooks();
  size_t totalBytes = FS.totalBytes();
  size_t usedBytes = FS.usedBytes();
  size_t freeBytes = (totalBytes >= usedBytes) ? (totalBytes - usedBytes) : 0;

  String finalPath = "/books/" + UploadScreen::state().bookFinalName;
  size_t storedSize = 0;
  File stored = FS.open(finalPath, "r");
  if (stored) {
    storedSize = stored.size();
    stored.close();
  }

  String inner;
  inner.reserve(1200);
  inner += "<div class='card'><h2>Upload complete</h2><p class='muted'>Your book is now stored on the device and available in the library.</p>";
  inner += "<div class='stats'>";
  inner += "<div class='stat'><span class='muted'>Book</span><b>" + htmlEscape(UploadScreen::state().bookFinalName) + "</b></div>";
  inner += "<div class='stat'><span class='muted'>Stored size</span><b>" + humanBytes(storedSize) + "</b></div>";
  inner += "<div class='stat'><span class='muted'>Books now</span><b>" + String(g_library.bookCount) + "</b></div>";
  inner += "<div class='stat'><span class='muted'>Free space</span><b>" + humanBytes(freeBytes) + "</b></div>";
  inner += "</div><div class='actions'><a class='btn' href='/'>Upload another</a><a class='btn secondary' href='/files'>Open files</a></div></div>";
  inner += storageCardHtml();

  String page = successPage(
    "Upload complete",
    "Book saved successfully.",
    "&#10003; Upload finished. No more blank status page.",
    inner
  );
  server.send(200, "text/html; charset=utf-8", page);
}

static void handleBookmarksWeb() {
  loadBooks();
  String out = webPageStart(
    "Bookmarks",
    "Saved reading positions for Pala One, grouped by book.",
    "<a href='/'>Home</a><a href='/files'>Files</a><a href='/settings'>Settings</a>",
    true
  );

  if (g_library.bookCount == 0) out += "<div class='card'><p class='muted'>No books available yet.</p></div>";

  for (int i = 0; i < g_library.bookCount; i++) {
    String bookPath = String(g_library.books[i].path);
    String key = prefKeyForBook(bookPath);
    uint16_t pages[MAX_BOOKMARKS];
    uint32_t offsets[MAX_BOOKMARKS];
    uint8_t count = loadBookmarksForKey(key, pages, offsets);

    out += "<div class='card'><h2>";
    out += htmlEscape(String(g_library.books[i].name));
    out += "</h2>";

    if (count == 0) {
      out += "<p class='muted'>No bookmarks</p></div>";
      continue;
    }

    File f = FS.open(bookPath, "r");
    if (!f) {
      out += "<p class='muted'>Open failed</p></div>";
      continue;
    }

    out += "<ul class='list'>";

    for (int j = 0; j < count; j++) {
      int targetPage = (int)pages[j];
      if (targetPage < 0) targetPage = 0;

      uint32_t pageOff = resolveBookmarkOffset(bookPath, (uint16_t)targetPage, offsets[j]);
      FileReadStream fs(f);
      String sn = readBookmarkLabelAtOffset(fs, pageOff, targetPage);
      out += "<li><div class='row'><div><div class='pill'>Bookmark ";
      out += String(j + 1);
      out += "</div><p class='meta' style='margin-top:8px'>";
      out += htmlEscape(sn);
      out += "</p></div><div><a class='link' href='/viewbm?book=" + String(i) + "&idx=" + String(j) + "'>View</a> | ";
      out += "<form method='POST' action='/delbm' style='display:inline'>";
      out += "<input type='hidden' name='book' value='" + String(i) + "'>";
      out += "<input type='hidden' name='idx' value='" + String(j) + "'>";
      out += "<button type='submit' class='btn secondary' style='padding:4px 8px;font-size:13px' onclick=\"return confirm('Delete bookmark?')\">Delete</button>";
      out += "</form></div></div></li>";
    }

    out += "</ul><div class='actions'><a class='btn secondary' href='/exportbm?book=" + String(i) + "'>Download all bookmarks</a></div></div>";
    f.close();
  }

  out += webPageEnd();
  server.send(200, "text/html; charset=utf-8", out);
}

static void handleDeleteBookmarkWeb() {
  if (!server.hasArg("book") || !server.hasArg("idx")) {
    server.send(400, "text/plain; charset=utf-8", "missing book/idx");
    return;
  }

  loadBooks();
  int b = server.arg("book").toInt();
  int idx = server.arg("idx").toInt();
  if (b < 0 || b >= g_library.bookCount) {
    server.send(400, "text/plain; charset=utf-8", "bad book");
    return;
  }

  String key = prefKeyForBook(String(g_library.books[b].path));
  uint16_t pages[MAX_BOOKMARKS];
  uint32_t offsets[MAX_BOOKMARKS];
  uint8_t count = loadBookmarksForKey(key, pages, offsets);
  if (idx < 0 || idx >= count) {
    server.send(400, "text/plain; charset=utf-8", "bad idx");
    return;
  }

  for (int i = idx + 1; i < count; i++) {
    pages[i - 1] = pages[i];
    offsets[i - 1] = offsets[i];
  }
  count--;
  saveBookmarksForKey(key, pages, offsets, count);

  server.sendHeader("Location", "/bookmarks");
  server.send(302, "text/plain", "");
}

static void handleViewBookmarkWeb() {
  if (!server.hasArg("book") || !server.hasArg("idx")) {
    server.send(400, "text/plain; charset=utf-8", "missing book/idx");
    return;
  }

  loadBooks();
  int b = server.arg("book").toInt();
  int idx = server.arg("idx").toInt();
  if (b < 0 || b >= g_library.bookCount) {
    server.send(400, "text/plain; charset=utf-8", "bad book");
    return;
  }

  String key = prefKeyForBook(String(g_library.books[b].path));
  uint16_t pages[MAX_BOOKMARKS];
  uint32_t offsets[MAX_BOOKMARKS];
  uint8_t count = loadBookmarksForKey(key, pages, offsets);
  if (idx < 0 || idx >= count) {
    server.send(400, "text/plain; charset=utf-8", "bad idx");
    return;
  }

  int page = (int)pages[idx];
  String bookPath = String(g_library.books[b].path);
  File vf = FS.open(bookPath, "r");
  String txt;
  if (!vf) {
    txt = "Open failed.";
  } else {
    uint32_t off = resolveBookmarkOffset(bookPath, (uint16_t)page, offsets[idx]);
    txt.reserve(900);
    (void)readPageFromFile(vf, off, false, &txt);
    vf.close();
    txt.trim();
    if (txt.length() == 0) txt = "(empty)";
  }
  String out = webPageStart(
    "Bookmark View",
    "Preview the saved page text for this bookmark.",
    "<a href='/bookmarks'>&#8592; Back</a><a href='/files'>Files</a><a href='/'>Home</a>",
    true
  );

  out += "<div class='card'><h2>";
  out += htmlEscape(String(g_library.books[b].name));
  out += "</h2><p class='muted'>Bookmark ";
  out += String(idx + 1);
  out += "</p><pre class='pre'>";
  out += htmlEscape(txt);
  out += "</pre><div class='actions'><a class='btn secondary' href='/exportbm?book=" + String(b) + "'>Download all bookmarks</a></div></div>";
  out += webPageEnd();
  server.send(200, "text/html; charset=utf-8", out);
}

static void handleExportBookmarksWeb() {
  if (!server.hasArg("book")) {
    server.send(400, "text/plain; charset=utf-8", "missing book");
    return;
  }

  loadBooks();
  int b = server.arg("book").toInt();
  if (b < 0 || b >= g_library.bookCount) {
    server.send(400, "text/plain; charset=utf-8", "bad book");
    return;
  }

  String bookPath = String(g_library.books[b].path);
  String key = prefKeyForBook(bookPath);
  uint16_t pages[MAX_BOOKMARKS];
  uint32_t offsets[MAX_BOOKMARKS];
  uint8_t count = loadBookmarksForKey(key, pages, offsets);

  if (count == 0) {
    server.send(404, "text/plain; charset=utf-8", "No bookmarks for this book");
    return;
  }

  File f = FS.open(bookPath, "r");
  if (!f) {
    server.send(500, "text/plain; charset=utf-8", "Open failed");
    return;
  }

  String exportName = stripTxtExt(lastPathComponent(bookPath));
  exportName.replace(' ', '_');
  exportName += "_bookmarks.txt";

  String out;
  out.reserve(8192);

  out += "Book: ";
  out += stripTxtExt(lastPathComponent(bookPath));
  out += "\n";

  out += "Bookmarks: ";
  out += String(count);
  out += "\n\n";

  for (int i = 0; i < count; i++) {
    int targetPage = (int)pages[i];
    if (targetPage < 0) targetPage = 0;

    uint32_t pageOff = resolveBookmarkOffset(bookPath, (uint16_t)targetPage, offsets[i]);
    FileReadStream fs(f);
    String label = readBookmarkLabelAtOffset(fs, pageOff, targetPage);
    String txt = readPageTextForWeb(bookPath, targetPage);

    out += "==================================================\n";
    out += "Bookmark ";
    out += String(i + 1);
    out += "\n";
    out += label;
    out += "\n";
    out += "--------------------------------------------------\n";
    out += txt;
    out += "\n\n";
  }

  f.close();

  server.sendHeader(
    "Content-Disposition",
    String("attachment; filename=\"") + exportName + "\""
  );
  server.send(200, "text/plain; charset=utf-8", out);
}

static void doFactoryReset() {
  safeCloseCurrentBook();
  clearCurrentBookState();
  resetUiEphemeralState();
  resetNavigationState();
  syncWakeState(false);

  prefs.clear();
  FS.end();
  delay(100);
  FS.format();
  delay(200);
  if (!FS.begin(true)) return;
  ensureBooksDir();
  resetOffsetCache();
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

static void handleListWeb() {
  loadListItems();
  String out = webPageStart(
    "List",
    "Create a simple shopping or to-do list for Pala One.",
    "<a href='/'>Home</a><a href='/files'>Files</a><a href='/bookmarks'>Bookmarks</a><a href='/settings'>Settings</a>",
    true
  );

  out += "<div class='card'><h2>Edit list</h2><p class='muted'>Items appear on the device only when at least one line contains text. Hold the button on the device to mark an item as done.</p>";
  out += "<form method='POST' action='/list' class='stack' accept-charset='UTF-8' style='margin-top:12px'>";
  for (int i = 0; i < MAX_LIST_ITEMS; i++) {
    String value = (i < g_list.count) ? htmlEscape(String(g_list.items[i].text)) : String("");
    String checked = (i < g_list.count && g_list.items[i].done) ? " checked" : "";
    out += "<div class='row' style='align-items:center;gap:10px'><div style='width:26px;text-align:center'><input type='checkbox' name='done" + String(i) + "' value='1'" + checked + "></div><div style='flex:1'><input type='text' name='item" + String(i) + "' value='" + value + "' maxlength='64' placeholder='List item'></div></div>";
  }
  out += "<div class='actions'><button type='submit'>Save list</button><button type='submit' formaction='/list-clear-done'>Delete checked items</button><span class='muted'>Blank rows are ignored. Checked rows can be removed directly.</span></div></form></div>";
  out += webPageEnd();
  server.send(200, "text/html; charset=utf-8", out);
}

static void handleListSaveWeb() {
  ListState newList;
  newList.count = 0;
  newList.selectedIndex = 0;

  for (int i = 0; i < MAX_LIST_ITEMS; i++) {
    String name = String("item") + String(i);
    String doneName = String("done") + String(i);
    String text = server.arg(name);
    sanitizeListText(text);
    if (text.length() == 0) continue;
    strncpy(newList.items[newList.count].text, text.c_str(), MAX_LIST_TEXT);
    newList.items[newList.count].text[MAX_LIST_TEXT] = '\0';
    newList.items[newList.count].done = server.hasArg(doneName) ? 1 : 0;
    newList.count++;
    if (newList.count >= MAX_LIST_ITEMS) break;
  }

  g_list = newList;
  saveListItems();
  if (!listHasVisibleItems() && g_currentScreen == &g_listScreen) {
    g_currentScreen->nextScreen = &g_libraryScreen;
  }
  server.sendHeader("Location", "/list");
  server.send(302, "text/plain", "");
}

static void handleListClearDoneWeb() {
  ListState newList;
  newList.count = 0;
  newList.selectedIndex = 0;

  for (int i = 0; i < MAX_LIST_ITEMS; i++) {
    String name = String("item") + String(i);
    String doneName = String("done") + String(i);
    String text = server.arg(name);
    sanitizeListText(text);

    if (text.length() == 0) continue;
    if (server.hasArg(doneName)) continue;  // checked in web UI => delete it

    strncpy(newList.items[newList.count].text, text.c_str(), MAX_LIST_TEXT);
    newList.items[newList.count].text[MAX_LIST_TEXT] = '\0';
    newList.items[newList.count].done = 0;
    newList.count++;
    if (newList.count >= MAX_LIST_ITEMS) break;
  }

  g_list = newList;
  saveListItems();
  if (!listHasVisibleItems() && g_currentScreen == &g_listScreen) {
    g_currentScreen->nextScreen = &g_libraryScreen;
  }

  server.sendHeader("Location", "/list");
  server.send(302, "text/plain", "");
}

static void handleSettings() {
  String sel8 = (g_settings.fontSize == 8) ? " selected" : "";
  String sel10 = (g_settings.fontSize == 10) ? " selected" : "";
  String sel12 = (g_settings.fontSize == 12) ? " selected" : "";
  String sel14 = (g_settings.fontSize == 14) ? " selected" : "";

  String ss30 = (g_settings.sleepSecs == 30) ? " selected" : "";
  String ss60 = (g_settings.sleepSecs == 60) ? " selected" : "";
  String ss120 = (g_settings.sleepSecs == 120) ? " selected" : "";
  String ss300 = (g_settings.sleepSecs == 300) ? " selected" : "";
  String ss600 = (g_settings.sleepSecs == 600) ? " selected" : "";
  String ss1800 = (g_settings.sleepSecs == 1800) ? " selected" : "";

  String lg0 = (g_settings.lineGap == 0) ? " selected" : "";
  String lg1 = (g_settings.lineGap == 1) ? " selected" : "";
  String lg2 = (g_settings.lineGap == 2) ? " selected" : "";
  String lg3 = (g_settings.lineGap == 3) ? " selected" : "";

  bool hasSleepImg = FS.exists("/sleep.bin");

  String out;
  out.reserve(4800);
  out =
    "<!doctype html><html><head><meta charset='utf-8'>"
    "<meta name='viewport' content='width=device-width,initial-scale=1'>"
    "<title>Settings</title>"
    "<style>"
    "body{margin:0;background:#f3efe7;color:#1f2328;font:15px/1.45 system-ui,sans-serif}"
    ".wrap{max-width:760px;margin:0 auto;padding:18px}"
    ".top{display:flex;justify-content:space-between;align-items:center;gap:12px;margin-bottom:14px}"
    ".top a,.link{color:#3c5a7a;text-decoration:none}"
    ".muted{color:#667085;font-size:13px}"
    ".card{background:#fff;border:1px solid #ddd4c7;border-radius:14px;padding:14px 15px;margin:0 0 14px;box-shadow:0 1px 0 rgba(0,0,0,.03)}"
    ".grid{display:grid;gap:12px}"
    "label{display:block;font-weight:600;margin:0 0 6px}"
    "select,input[type=file]{width:100%;box-sizing:border-box;border:1px solid #c9c2b8;border-radius:10px;background:#fff;padding:10px;font:inherit}"
    ".hint{margin:6px 0 0;color:#667085;font-size:12px}"
    ".actions{display:flex;flex-wrap:wrap;gap:10px;align-items:center;margin-top:14px}"
    "button{border:0;border-radius:10px;background:#1f2328;color:#fff;padding:10px 14px;font:600 14px system-ui,sans-serif}"
    ".status{padding:10px 12px;border-radius:10px;font-size:14px;margin:10px 0 0}"
    ".ok{background:#e7f6ec;color:#216e39}"
    ".idle{background:#f6f2ea;color:#6b6358}"
    "h1,h2{margin:0 0 6px}"
    "p{margin:0 0 10px}"
    "@media(min-width:620px){.grid.cols-2{grid-template-columns:1fr 1fr}}"
    "</style></head><body><div class='wrap'>"
    "<div class='top'><div><h1>Pala One Settings</h1><div class='muted'>Firmware " FW_VERSION " configuration page stored directly on the device.</div></div><a href='/'>&#8592; Home</a></div>"
    "<div class='card'><h2>Reading</h2><form method='POST' action='/settings' accept-charset='UTF-8'><div class='grid cols-2'><div><label for='font'>Font size</label><select id='font' name='font'>"
    "<option value='8'"; out += sel8; out += ">8px &mdash; tiny</option>";
  out += "<option value='10'"; out += sel10; out += ">10px &mdash; small</option>";
  out += "<option value='12'"; out += sel12; out += ">12px &mdash; medium</option>";
  out += "<option value='14'"; out += sel14; out += ">14px &mdash; large</option>";
  out +=
    "</select><div class='hint'>Controls how many lines fit on each page.</div></div>"
    "<div><label for='sleep'>Sleep after</label><select id='sleep' name='sleep'>"
    "<option value='30'"; out += ss30; out += ">30 seconds</option>";
  out += "<option value='60'"; out += ss60; out += ">1 minute</option>";
  out += "<option value='120'"; out += ss120; out += ">2 minutes</option>";
  out += "<option value='300'"; out += ss300; out += ">5 minutes</option>";
  out += "<option value='600'"; out += ss600; out += ">10 minutes</option>";
  out += "<option value='1800'"; out += ss1800; out += ">30 minutes</option>";
  out += "</select><div class='hint'>Auto-sleep keeps battery draw low while idle.</div></div>";
  out += "<div><label for='lgap'>Line spacing</label><select id='lgap' name='lgap'>";
  out += "<option value='0'"; out += lg0; out += ">0 px &mdash; compact</option>";
  out += "<option value='1'"; out += lg1; out += ">1 px &mdash; normal</option>";
  out += "<option value='2'"; out += lg2; out += ">2 px &mdash; relaxed</option>";
  out += "<option value='3'"; out += lg3; out += ">3 px &mdash; loose</option>";
  out +=
    "</select><div class='hint'>A small change here can make text much easier to scan.</div></div>"
    "</div>"
    "<div class='actions' style='margin-top:24px;'><button type='submit'>Save settings</button><span class='muted'>No extra files, scripts, or fonts.</span></div></form></div>"
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
    "</form></div></div></body></html>";

  server.send(200, "text/html; charset=utf-8", out);
}

static void handleSettingsPost() {
  bool layoutChanged = false;

  if (server.hasArg("font")) {
    int fs = server.arg("font").toInt();
    if (fs != 8 && fs != 10 && fs != 12 && fs != 14) fs = 10;
    if (fs != g_settings.fontSize) {
      applyFontSize(fs);
      prefs.putInt("cfg_font", fs);
      layoutChanged = true;
    }
  }

  if (server.hasArg("sleep")) {
    int ss = server.arg("sleep").toInt();
    if (ss < 10) ss = 10;
    if (ss > 3600) ss = 3600;
    if ((uint32_t)ss != g_settings.sleepSecs) {
      g_settings.sleepSecs = (uint32_t)ss;
      prefs.putInt("cfg_sleep", ss);
    }
  }

  if (server.hasArg("lgap")) {
    int lg = server.arg("lgap").toInt();
    if (lg < 0) lg = 0;
    if (lg > 4) lg = 4;
    if (lg != g_settings.lineGap) {
      g_settings.lineGap = lg;
      prefs.putInt("cfg_lgap", lg);
      invalidateMetrics();
      layoutChanged = true;
    }
  }

  if (layoutChanged) {
    // invalidateAllPageCaches() already resets pageIndex to 0 for the open book.
    // Call it BEFORE renderCurrentPage() so the page is redrawn from byte 0
    // with the new font metrics -- not from the now-invalid old page number.
    invalidateAllPageCaches();
    if (g_currentScreen == &g_readerScreen || g_currentScreen == &g_bmPreviewScreen) {
      if (g_currentScreen != &g_readerScreen) g_currentScreen->nextScreen = &g_readerScreen;
      else renderCurrentPage();
    }
  }

  server.sendHeader("Location", "/settings");
  server.send(302, "text/plain", "");
}

static void handleDeleteSleepImg() {
  if (FS.exists("/sleep.bin")) FS.remove("/sleep.bin");
  server.sendHeader("Location", "/settings");
  server.send(302, "text/plain", "");
}

static void handleUploadSleepDone() {
  if (!UploadScreen::state().sleepOk) {
    server.send(400, "text/plain; charset=utf-8", UploadScreen::state().sleepError.length() ? UploadScreen::state().sleepError : "Sleep image upload failed");
    return;
  }

  String inner;
  inner.reserve(500);
  inner += "<div class='card'><h2>Screensaver updated</h2><p class='muted'>Your custom sleep image was saved successfully and will be shown the next time the device goes to sleep.</p><div class='actions'><a class='btn' href='/settings'>Back to settings</a><a class='btn secondary' href='/'>Home</a></div></div>";

  String page = successPage(
    "Upload complete",
    "Screensaver saved successfully.",
    "&#10003; Custom sleep image uploaded.",
    inner
  );
  server.send(200, "text/html; charset=utf-8", page);
}

// ============================================================================
//  Upload stream handlers
// ============================================================================
static void handleUploadBookStream() {
  HTTPUpload& up = server.upload();

  if (up.status == UPLOAD_FILE_START) {
    UploadScreen::state().bookOk = false;
    UploadScreen::state().bookError = "";
    UploadScreen::state().bookFinalName = "";
    UploadScreen::state().bookPendingUtf8Tail = "";
    UploadScreen::state().bookTmpPath = "";

    loadBooks();
    if (g_library.bookCount >= MAX_BOOKS) {
      UploadScreen::state().bookError = "Library full";
      return;
    }

    size_t freeBytes = fsFreeBytesSafe();
    if (freeBytes < 8192) {
      UploadScreen::state().bookError = "Not enough free space";
      return;
    }

    String clean = sanitizeUploadedFilename(up.filename);
    UploadScreen::state().bookFinalName = clean;
    UploadScreen::state().bookTmpPath = "/books/" + clean + ".tmp";

    if (FS.exists(UploadScreen::state().bookTmpPath)) FS.remove(UploadScreen::state().bookTmpPath);
    UploadScreen::state().bookTmpFile = FS.open(UploadScreen::state().bookTmpPath, "w");
    if (!UploadScreen::state().bookTmpFile) {
      UploadScreen::state().bookError = "Cannot create temp upload file";
      UploadScreen::state().bookTmpPath = "";
    }
  }
  else if (up.status == UPLOAD_FILE_WRITE) {
    if (UploadScreen::state().bookError.length() > 0) return;
    if (UploadScreen::state().bookTmpFile && up.currentSize > 0) {
      String chunk = UploadScreen::state().bookPendingUtf8Tail + String((const char*)up.buf, up.currentSize);
      int len = (int)chunk.length();
      if (len > 4) {
        UploadScreen::state().bookPendingUtf8Tail = chunk.substring(len - 4);
        chunk = chunk.substring(0, len - 4);
      } else {
        UploadScreen::state().bookPendingUtf8Tail = chunk;
        chunk = "";
      }
      if (chunk.length() > 0) {
        String cleaned = normalizeTypography(chunk);
        cleaned = compactText(cleaned);
        UploadScreen::state().bookTmpFile.print(cleaned);
      }
    }
  }
  else if (up.status == UPLOAD_FILE_END) {
    if (UploadScreen::state().bookError.length() > 0 && !UploadScreen::state().bookTmpFile) return;
    if (UploadScreen::state().bookTmpFile) {
      if (UploadScreen::state().bookPendingUtf8Tail.length() > 0) {
        String cleaned = normalizeTypography(UploadScreen::state().bookPendingUtf8Tail);
        cleaned = compactText(cleaned);
        UploadScreen::state().bookTmpFile.print(cleaned);
        UploadScreen::state().bookPendingUtf8Tail = "";
      }
      UploadScreen::state().bookTmpFile.close();

      if (UploadScreen::state().bookTmpPath.length() > 0 && up.totalSize > 0) {
        String finalPath = UploadScreen::state().bookTmpPath.substring(0, UploadScreen::state().bookTmpPath.length() - 4);
        if (FS.exists(finalPath)) FS.remove(finalPath);
        if (FS.rename(UploadScreen::state().bookTmpPath, finalPath)) {
          UploadScreen::state().bookOk = true;
        } else {
          if (FS.exists(UploadScreen::state().bookTmpPath)) FS.remove(UploadScreen::state().bookTmpPath);
          UploadScreen::state().bookError = "Failed to finalize upload";
        }
      } else {
        if (UploadScreen::state().bookTmpPath.length() > 0 && FS.exists(UploadScreen::state().bookTmpPath)) FS.remove(UploadScreen::state().bookTmpPath);
        UploadScreen::state().bookError = "Empty upload";
      }
      UploadScreen::state().bookTmpPath = "";
    } else {
      if (UploadScreen::state().bookTmpPath.length() > 0 && FS.exists(UploadScreen::state().bookTmpPath)) FS.remove(UploadScreen::state().bookTmpPath);
      if (UploadScreen::state().bookError.length() == 0) UploadScreen::state().bookError = "Upload failed";
      UploadScreen::state().bookTmpPath = "";
    }
  }
  else if (up.status == UPLOAD_FILE_ABORTED) {
    if (UploadScreen::state().bookTmpFile) UploadScreen::state().bookTmpFile.close();
    if (UploadScreen::state().bookTmpPath.length() > 0 && FS.exists(UploadScreen::state().bookTmpPath)) FS.remove(UploadScreen::state().bookTmpPath);
    UploadScreen::state().bookPendingUtf8Tail = "";
    UploadScreen::state().bookTmpPath = "";
    UploadScreen::state().bookOk = false;
    UploadScreen::state().bookError = "Upload aborted";
  }
}

static void handleUploadSleepStream() {
  HTTPUpload& upS = server.upload();

  if (upS.status == UPLOAD_FILE_START) {
    UploadScreen::state().sleepOk = false;
    UploadScreen::state().sleepError = "";
    UploadScreen::state().sleepTmpPath = "/sleep.bin.tmp";
    if (FS.exists(UploadScreen::state().sleepTmpPath)) FS.remove(UploadScreen::state().sleepTmpPath);
    UploadScreen::state().sleepTmpFile = FS.open(UploadScreen::state().sleepTmpPath, "w");
    if (!UploadScreen::state().sleepTmpFile) UploadScreen::state().sleepError = "Cannot create temp sleep file";
  }
  else if (upS.status == UPLOAD_FILE_WRITE) {
    if (UploadScreen::state().sleepTmpFile) UploadScreen::state().sleepTmpFile.write(upS.buf, upS.currentSize);
  }
  else if (upS.status == UPLOAD_FILE_END) {
    if (UploadScreen::state().sleepTmpFile) UploadScreen::state().sleepTmpFile.close();
    File f = FS.open(UploadScreen::state().sleepTmpPath, "r");
    size_t sz = f ? f.size() : 0;
    if (f) f.close();

    if (sz != 3904) {
      if (FS.exists(UploadScreen::state().sleepTmpPath)) FS.remove(UploadScreen::state().sleepTmpPath);
      UploadScreen::state().sleepError = "Sleep image must be exactly 3904 bytes";
      UploadScreen::state().sleepOk = false;
    } else {
      if (FS.exists("/sleep.bin")) FS.remove("/sleep.bin");
      if (FS.rename(UploadScreen::state().sleepTmpPath, "/sleep.bin")) UploadScreen::state().sleepOk = true;
      else {
        if (FS.exists(UploadScreen::state().sleepTmpPath)) FS.remove(UploadScreen::state().sleepTmpPath);
        UploadScreen::state().sleepError = "Failed to save sleep image";
      }
    }
    UploadScreen::state().sleepTmpPath = "";
  }
  else if (upS.status == UPLOAD_FILE_ABORTED) {
    if (UploadScreen::state().sleepTmpFile) UploadScreen::state().sleepTmpFile.close();
    if (UploadScreen::state().sleepTmpPath.length() > 0 && FS.exists(UploadScreen::state().sleepTmpPath)) FS.remove(UploadScreen::state().sleepTmpPath);
    UploadScreen::state().sleepError = "Sleep image upload aborted";
    UploadScreen::state().sleepOk = false;
    UploadScreen::state().sleepTmpPath = "";
  }
}

// ============================================================================
//  Server lifecycle
// ============================================================================
void registerWebRoutes() {
  server.on("/", HTTP_GET, handleRoot);
  server.on("/files", HTTP_GET, handleFiles);
  server.on("/del",   HTTP_POST, handleDelete);   // POST: prevents accidental deletion via browser prefetch
  server.on("/mkdir", HTTP_POST, handleCreateFolder);
  server.on("/move", HTTP_POST, handleMoveBook);
  server.on("/jumppage", HTTP_POST, handleJumpPageWeb);
  server.on("/list", HTTP_GET, handleListWeb);
  server.on("/list", HTTP_POST, handleListSaveWeb);
  server.on("/list-clear-done", HTTP_POST, handleListClearDoneWeb);
  server.on("/rmdir", HTTP_POST, handleDeleteFolder); // POST: destructive

  server.on("/reset", HTTP_GET, handleResetConfirm);
  server.on("/reset", HTTP_POST, handleResetDo);

  server.on("/bookmarks", HTTP_GET, handleBookmarksWeb);
  server.on("/delbm",  HTTP_POST, handleDeleteBookmarkWeb); // POST: destructive
  server.on("/viewbm", HTTP_GET, handleViewBookmarkWeb);
  server.on("/exportbm", HTTP_GET, handleExportBookmarksWeb);

  server.on("/settings", HTTP_GET, handleSettings);
  server.on("/settings", HTTP_POST, handleSettingsPost);
  server.on("/del-sleep", HTTP_GET, handleDeleteSleepImg);

  server.on("/upload-sleep", HTTP_POST, handleUploadSleepDone, handleUploadSleepStream);
  server.on("/upload", HTTP_POST, handleUploadDone, handleUploadBookStream);
}
