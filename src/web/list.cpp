#include "web/list.h"

#include "config.h"
#include "state.h"
#include "storage/list_items.h"
#include "ui/screen.h"
#include "ui/screens/library_screen.h"
#include "ui/screens/list_screen.h"
#include "web/chrome.h"

static void handleListWeb() {
  String out = webPageStart(
    "List",
    "Create a simple shopping or to-do list for Pala One.",
    "<a href='/'>Home</a><a href='/files'>Files</a><a href='/bookmarks'>Bookmarks</a><a href='/settings'>Settings</a>",
    true
  );

  out += "<div class='card'><h2>Edit list</h2><p class='muted'>Items appear on the device only when at least one line contains text. Hold the button on the device to mark an item as done.</p>";
  out += "<form method='POST' action='/list' class='stack' accept-charset='UTF-8' style='margin-top:12px'>";
  for (int i = 0; i < MAX_LIST_ITEMS; i++) {
    String value   = (i < g_list.count) ? htmlEscape(String(g_list.items[i].text)) : String("");
    String checked = (i < g_list.count && g_list.items[i].done) ? " checked" : "";
    out += "<div class='row' style='align-items:center;gap:10px'><div style='width:26px;text-align:center'><input type='checkbox' name='done" + String(i) + "' value='1'" + checked + "></div><div style='flex:1'><input type='text' name='item" + String(i) + "' value='" + value + "' maxlength='64' placeholder='List item'></div></div>";
  }
  out += "<div class='actions'><button type='submit'>Save list</button><button type='submit' formaction='/list-clear-done'>Delete checked items</button><span class='muted'>Blank rows are ignored. Checked rows can be removed directly.</span></div></form></div>";
  out += webPageEnd();
  server.send(200, "text/html; charset=utf-8", out);
}

// Build `g_list` from the form fields and persist. If `dropChecked`, items
// whose checkbox is set are discarded entirely; otherwise their done state
// is recorded. Either way, blank rows are skipped.
static void rebuildListFromForm(bool dropChecked) {
  ListState newList;
  newList.count = 0;
  newList.selectedIndex = 0;

  for (int i = 0; i < MAX_LIST_ITEMS; i++) {
    String text = server.arg(String("item") + String(i));
    sanitizeListText(text);
    if (text.length() == 0) continue;

    bool checked = server.hasArg(String("done") + String(i));
    if (dropChecked && checked) continue;

    strncpy(newList.items[newList.count].text, text.c_str(), MAX_LIST_TEXT);
    newList.items[newList.count].text[MAX_LIST_TEXT] = '\0';
    newList.items[newList.count].done = (checked && !dropChecked) ? 1 : 0;
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

static void handleListSaveWeb()      { rebuildListFromForm(false); }
static void handleListClearDoneWeb() { rebuildListFromForm(true);  }

void registerListRoutes() {
  server.on("/list",            HTTP_GET,  handleListWeb);
  server.on("/list",            HTTP_POST, handleListSaveWeb);
  server.on("/list-clear-done", HTTP_POST, handleListClearDoneWeb);
}
