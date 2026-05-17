#include "ui/screens/apps_screen.h"

#include "hal/display.h"
#include "storage/app_catalog.h"
#include "ui/font.h"
#include "ui/pala_api_impl.h"          // runApp
#include "ui/screens/library_screen.h" // g_libraryScreen
#include "ui/widgets.h"

// Cursor persists across visits — entering Apps doesn't reset to 0,
// matching how LibraryScreen / ListScreen behave.
static int s_cursor = 0;

void AppsScreen::onEnter() {
  // Re-scan the catalog on entry so newly uploaded apps appear without
  // a reboot. Cheap (one /apps/ directory walk).
  loadApps();
  if (s_cursor < 0) s_cursor = 0;
  if (s_cursor >= g_apps.count) s_cursor = max(0, g_apps.count - 1);
  draw();
}

void AppsScreen::draw() {
  prepareMenuFrame();
  Font::useBody();
  int y = drawSectionHeader("Apps");

  if (g_apps.count == 0) {
    drawMenuRow(y, "No apps installed", false);
    display.update();
    return;
  }

  drawScrollableList(y, g_apps.count, s_cursor,
    [&](int idx, int rowY, bool selected, int /*budget*/) {
      drawMenuRow(rowY, String(g_apps.entries[idx].name), selected);
      return 1;
    });

  display.update();
}

void AppsScreen::onButton(const ButtonEvent& e) {
  if (!e.any()) return;

  if (e.kind == ButtonEvent::Triple) {
    nextScreen = &g_libraryScreen;
    return;
  }

  if (e.kind == ButtonEvent::Short) {
    if (g_apps.count > 0) {
      s_cursor = (s_cursor + 1) % g_apps.count;
    }
    draw();
    return;
  }

  if (e.kind == ButtonEvent::Double) {
    if (g_apps.count == 0) return;
    if (s_cursor < 0 || s_cursor >= g_apps.count) return;

    // runApp blocks for the entire app lifetime; when it returns the
    // app's framebuffer is whatever the app last drew. Repaint our menu
    // so the screen reflects "back in Apps" again.
    const char* path = g_apps.entries[s_cursor].path;
    runApp(path);
    draw();
    return;
  }

  // Long-press: reserved (future per-app uninstall, perhaps). No-op for now.
}
