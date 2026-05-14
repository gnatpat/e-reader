#pragma once

#include "ui/screen.h"

class BookmarkPreviewScreen : public Screen {
public:
  void onEnter() override;
  void onButton(const ButtonEvent& e) override;
  void draw() override;
  void onSleep() override;
};

extern BookmarkPreviewScreen g_bmPreviewScreen;
