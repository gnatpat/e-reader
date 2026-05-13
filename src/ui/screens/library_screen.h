#pragma once

#include "ui/screen.h"

class LibraryScreen : public Screen {
public:
  void onEnter() override;
  void onButton(const ButtonEvent& e) override;
  void draw() override;
};

extern LibraryScreen g_libraryScreen;

// Request a deferred transition to the library root from any context where
// `g_currentScreen` is set (i.e. mid-iteration of `loop()`). The actual
// state cleanup runs in `LibraryScreen::onEnter()` on the next dispatch.
void navigateToLibraryRoot();
