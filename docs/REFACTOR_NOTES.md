# Refactor opportunities

Captured from a code-review pass. Roughly ordered by payoff-per-effort.

## 1. Invert the storage → ui dependency — DONE

`storage/page_cache.cpp`, `storage/book_state.cpp`, `storage/bookmarks.cpp`,
and `storage/settings_store.cpp` all used to include `ui/reader.h` or
`hal/display.h` and reach into `g_reader` / `g_toast` directly. Now they
don't — `grep '^#include\s+"(ui|hal)/' src/storage/` returns empty.

What moved:
- Reader-lifecycle helpers (`safeCloseCurrentBook`, `clearCurrentBookState`,
  `reopenCurrentBookIfNeeded`, `saveProgressThrottled`, `resetSaveThrottle`,
  `addBookmarkForCurrentBook`) → `ui/reader.cpp`.
- `resetUiEphemeralState` (clears the toast) → `hal/display.cpp`.
- `resetNavigationState` (clears the library cursor) → `ui/screens/library_screen.cpp`.
- `applyFontSize` orchestration moved out of `storage/settings_store.cpp`;
  the caller (`main.cpp::setup`) now drives it.

New helpers introduced:
- `setCurrentBook(path)` in `ui/reader.cpp` — single source of truth for
  the `(currentBookPath, currentBookKey)` invariant.
- `renameBook(oldPath, newPath)` in `ui/reader.cpp` — wraps
  `migrateBookMetadata` (storage primitive) and keeps `g_reader` in sync.
- `resetAllPagination()` in `ui/reader.cpp` — wraps
  `invalidateAllPageCaches` (storage primitive) and resets the open
  reader's in-memory state.
- `PageOffsetTable` struct in `pure/page_offset_table.h` — replaces
  `g_reader.pageOffsets[] + knownPages`, lets storage take the buffer as
  a typed parameter instead of writing into a global.

Signature changes:
- `loadPageOffsetCacheForBook` and `savePageOffsetCacheForBook` now take
  a `PageOffsetTable&` / `const PageOffsetTable&` instead of writing into
  `g_reader` directly.
- `syncWakeState` now takes an explicit `path` argument instead of
  reading `g_reader.currentBookPath`.

Follow-ups this unlocked:
- The `#ifdef ARDUINO` walls in `storage/bookmarks.cpp` and
  `storage/list_items.cpp` can be revisited — some of what they guard
  is now straightforwardly host-testable.
- Refactor #2 (split `g_reader`) is partly done: `PageOffsetTable` is
  one of the four pieces that refactor was going to extract.

## 2. `g_reader` is a god-struct — partly done

`ReaderState` mixes four concerns: open-book lifecycle (file, path, key), the
pagination index (now `PageOffsetTable pages` + `eofReached`), UI cursor
state (`pageIndex`, `pageTurnsSinceFull`, `lastPageStartOffset`), and
save-throttle bookkeeping (`lastSaveMs`, `lastSavedPage`).

The `PageOffsetTable` piece was extracted as part of refactor #1. Remaining
work: split off `OpenBook` (file+path+key), `ReaderCursor` (pageIndex +
pageTurns + lastPageStart), and `SaveThrottle` (lastSaveMs + lastSavedPage).
Each piece becomes individually testable; the "where does this state live"
search space shrinks dramatically. Also makes `BookmarkPreviewScreen`, which
currently borrows `g_reader` as a scratchpad, much less spooky.

## 3. Extract a `ScrollableList` widget

`LibraryScreen`, `ListScreen`, `BookmarkBookSelectScreen`, and
`BookmarkListScreen` all reimplement the same scrolling-list math:
`visible = (SCREEN_H - y - BOT_PAD) / lineH`, top-clamping, a draw loop calling
`drawMenuBulletRow`. ~150 lines of duplication.

A `ScrollableList(itemCount, selected, drawRowFn, contentTopY)` widget would
collapse all four, fix the inconsistent `+1` in the per-screen `lineH`
formulas, and make features like a scrollbar a one-place change.

## 4. `web/web.cpp` is 1180 lines of hand-rolled HTML

Two paths, increasing in scope:

- **Cheap**: extract CSS to a `/style.css` route with a cache header; lift more
  template helpers out of `webPageStart` / `webPageEnd` (`card(title, body)`,
  `formRow(...)`, `statsGrid(...)`); collapse the 30+ `UploadScreen::state().X`
  repetitions with local references in each handler.
- **Bigger**: turn the server into `/api/...` JSON endpoints + ship a small
  static SPA (one HTML + one JS file in LittleFS at `/web/`). Iterate on UI
  without re-flashing.

The SPA version is the right end state if web is going to keep growing; the
template extraction is enough if web is "done".

## 5. Move more logic into `pure/`

Menu navigation (cursor up/down, wrap, expand folder, build entry list) is
pure logic currently entangled with `g_library` globals. Same for the
bookmark-flow state transitions across the three screens. Extract into pure
types with operations like `Library::cursorDown()`,
`Library::toggleExpand(folderIdx)` — testable in the host CMake build,
mostly mechanical to extract.

## 6. `state.h` as a universal include is hurting build times

Every firmware file `#include`s it, and it pulls in `Arduino.h`,
`heltec-eink-modules.h`, `WebServer.h`, `Preferences.h`, `LittleFS.h`. Touching
`state.h` rebuilds the world.

Split into `state_fwd.h` (just `extern` declarations + forward declarations
where possible) and let TUs pull in the implementation headers themselves.
Incremental rebuilds get noticeably faster and the dependency graph becomes
legible.

## 7. Heap-allocated `String` on hot paths

`paginator.cpp` allocates `String chunk`, `String candidate`, `String ch` per
character in some branches. For a long reading session this means continuous
heap churn and potential fragmentation. The pure module is the right shape —
it's the `String` type that's the hazard.

A fixed-buffer alternative (a small `StringView` + scratch buffer) on the
hot paths would matter more for long-term reliability than any of the items
above. Lower structural priority, higher real-world-impact priority — this
is the one that could surface as "the device gets weird after eight hours of
reading."

---

If picking only two: ~~**#1** (cheapest, unlocks the others) and~~ **#3** (pure
win, no risk). #4 changes the most lines but is the most contained — safe to
defer. With #1 done, the next-biggest payoff is finishing #2 (~half remaining,
the `PageOffsetTable` piece is already split out).
