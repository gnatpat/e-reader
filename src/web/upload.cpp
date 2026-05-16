#include "web/upload.h"

#include "config.h"
#include "state.h"
#include "pure/paths.h"
#include "pure/text_util.h"
#include "storage/fs_util.h"
#include "storage/library.h"
#include "ui/screens/upload_screen.h"   // singleton UploadState lives here
#include "web/chrome.h"

// ============================================================================
//  Book upload
//
//  Two handlers per route: a streaming chunk receiver (`*Stream`) and a final
//  response handler (`*Done`). The stream handler maintains a 4-byte UTF-8
//  tail across chunks so a multibyte codepoint isn't split mid-character;
//  each chunk is normalized + compacted before being written to the temp
//  file. On END, atomic rename into place.
// ============================================================================

static void handleUploadDone() {
  if (!UploadScreen::state().bookOk) {
    server.send(400, "text/plain; charset=utf-8",
                UploadScreen::state().bookError.length()
                  ? UploadScreen::state().bookError
                  : "Upload failed");
    return;
  }

  loadBooks();   // refresh after the stream handler appended the new book

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
  inner += "<div class='stat'><span class='muted'>Book</span><b>"        + htmlEscape(UploadScreen::state().bookFinalName) + "</b></div>";
  inner += "<div class='stat'><span class='muted'>Stored size</span><b>" + humanBytes(storedSize)                          + "</b></div>";
  inner += "<div class='stat'><span class='muted'>Books now</span><b>"   + String(g_library.bookCount)                     + "</b></div>";
  inner += "<div class='stat'><span class='muted'>Free space</span><b>"  + humanBytes(fsFreeBytesSafe())                   + "</b></div>";
  inner += "</div><div class='actions'><a class='btn' href='/'>Upload another</a><a class='btn secondary' href='/files'>Open files</a></div></div>";
  inner += storageCardHtml();

  String page = successPage(
    "Upload complete",
    "Book saved successfully.",
    "&#10003; Upload finished.",
    inner
  );
  server.send(200, "text/html; charset=utf-8", page);
}

static void handleUploadBookStream() {
  HTTPUpload& up = server.upload();

  if (up.status == UPLOAD_FILE_START) {
    UploadScreen::state().bookOk = false;
    UploadScreen::state().bookError = "";
    UploadScreen::state().bookFinalName = "";
    UploadScreen::state().bookPendingUtf8Tail = "";
    UploadScreen::state().bookTmpPath = "";

    loadBooks();   // defensive — protects MAX_BOOKS check from a stale catalog
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
    UploadScreen::state().bookTmpPath   = "/books/" + clean + ".tmp";

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

// ============================================================================
//  Sleep image upload — straight binary, must be exactly 3904 bytes.
// ============================================================================

static void handleUploadSleepDone() {
  if (!UploadScreen::state().sleepOk) {
    server.send(400, "text/plain; charset=utf-8",
                UploadScreen::state().sleepError.length()
                  ? UploadScreen::state().sleepError
                  : "Sleep image upload failed");
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

void registerUploadRoutes() {
  server.on("/upload",       HTTP_POST, handleUploadDone,      handleUploadBookStream);
  server.on("/upload-sleep", HTTP_POST, handleUploadSleepDone, handleUploadSleepStream);
}
