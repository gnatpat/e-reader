#pragma once

#include "state.h"   // File, FS
#include "ui/screen.h"

// ============================================================================
//  Per-session state for a book / sleep-image upload. The struct is public
//  so the upload-stream handlers in `web/web.cpp` can read/write it through
//  `UploadScreen::state()`; only one upload session exists at a time, so
//  storing it as a static member of UploadScreen is appropriate.
// ============================================================================
struct UploadState {
  File bookTmpFile;
  File sleepTmpFile;

  String bookTmpPath;
  String bookPendingUtf8Tail;
  String bookFinalName;
  bool bookOk = false;
  String bookError;

  String sleepTmpPath;
  bool sleepOk = false;
  String sleepError;

  uint32_t startedMs = 0;
};

class UploadScreen : public Screen {
public:
  void onEnter() override;
  void onButton(const ButtonEvent& e) override;
  void draw() override;
  void onIdleTick() override;

  // The SoftAP can't keep running while the device deep-sleeps.
  bool allowSleep() const override { return false; }

  // The upload session is a singleton (only one transfer in flight). The web
  // upload-stream handlers in `web/web.cpp` reach the session state through
  // this accessor, avoiding a global `g_upload`.
  static UploadState& state();

private:
  static UploadState s_state;

  void startSession();
  void stopSessionToLibrary();
};

extern UploadScreen g_uploadScreen;
