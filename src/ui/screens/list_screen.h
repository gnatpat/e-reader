#pragma once

#include "ui/screen.h"

class ListScreen : public Screen {
public:
  void onEnter() override;
  void onButton(const ButtonEvent& e) override;
  void draw() override;
};

extern ListScreen g_listScreen;
