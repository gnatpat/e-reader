#pragma once

#include "hal/input.h"           // ButtonState + ButtonEvent
#include "state.h"
#include "storage/book_state.h"  // syncWakeState — default Screen::onSleep

class Screen {
public:
  virtual ~Screen() = default;
  virtual void onEnter() {}
  virtual void onButton(const ButtonEvent& e) = 0;
  virtual void draw() = 0;
  virtual void onIdleTick() {}

  // Called once just before the device deep-sleeps. Default: clear wake state
  // so the next boot lands on the library. Screens that want to resume their
  // state (reader, bookmark preview) override to save progress + set wake.
  virtual void onSleep() { syncWakeState(false); }

  // May the device deep-sleep while this screen is active? Default yes;
  // UploadScreen overrides to false because a sleeping device can't keep the
  // SoftAP up.
  virtual bool allowSleep() const { return true; }

  Screen* nextScreen = nullptr;
};

extern Screen* g_currentScreen;
