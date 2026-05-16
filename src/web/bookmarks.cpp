#include "web/bookmarks.h"

#include "config.h"
#include "state.h"
#include "pure/hashing.h"
#include "pure/paths.h"         // stripTxtExt, lastPathComponent
#include "storage/book_metadata.h"
#include "storage/library.h"
#include "ui/text.h"            // resolveBookmarkOffset, extractPageText, pageOffsetForPage
#include "web/chrome.h"

// One book page rendered as plain text — used by the bookmark export
// download. Opens the file, locates page N's offset, captures one page's
// worth of lines into a String. Single-caller helper; lives here rather than
// in text.cpp because it's pure web concern (the "Open failed" / "(empty)"
// strings are user-facing for this download flow).
static String readPageTextForWeb(const String& path, int page) {
  File f = FS.open(path, "r");
  if (!f) return String("Open failed.");
  uint32_t off = pageOffsetForPage(f, path, page);
  String out;
  out.reserve(900);
  (void)extractPageText(f, off, out);
  f.close();
  out.trim();
  if (out.length() == 0) out = "(empty)";
  return out;
}

static void handleBookmarksWeb() {
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

  int b   = server.arg("book").toInt();
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
    pages[i - 1]   = pages[i];
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

  int b   = server.arg("book").toInt();
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
    (void)extractPageText(vf, off, txt);
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

void registerBookmarksRoutes() {
  server.on("/bookmarks", HTTP_GET,  handleBookmarksWeb);
  server.on("/viewbm",    HTTP_GET,  handleViewBookmarkWeb);
  server.on("/delbm",     HTTP_POST, handleDeleteBookmarkWeb);   // POST: destructive
  server.on("/exportbm",  HTTP_GET,  handleExportBookmarksWeb);
}
