# Refactor opportunities

Captured from a code-review pass. Roughly ordered by payoff-per-effort.

## 1. Invert the storage → ui dependency — DONE

Started as "stop storage from `#include`ing `ui/...`," ended as a broader
file-by-file reshape of `src/storage/`. Two principles drove the work:

1. **Vertical**: storage is below ui — a file in `storage/` may include
   `pure/` (lower) but never `ui/` or `hal/` (higher).
2. **Horizontal**: each storage file owns one nameable concept. If you
   can't describe it in a single line, it's the wrong grouping.

### Verification

- `grep '^#include\s+"(ui|hal)/' src/storage/` → empty
- Every file under `src/storage/` has a one-line description (see table
  at the bottom of this section)

### Functions that moved up to ui/

Storage had quietly been doing reader-state work. These were lifted out:

| Function | New home |
|---|---|
| `safeCloseCurrentBook`, `clearCurrentBookState`, `reopenCurrentBookIfNeeded` | `ui/reader.cpp` |
| `saveProgressThrottled`, `resetSaveThrottle`, `addBookmarkForCurrentBook` | `ui/reader.cpp` |
| `resetUiEphemeralState` (clears `g_toast`) | `hal/display.cpp` |
| `resetNavigationState` (clears the library cursor) | `ui/screens/library_screen.cpp` |
| `applyFontSize` orchestration (was called from inside `loadSettings`) | caller (`main.cpp::setup`) |

### New helpers introduced

| Helper | Where | Why |
|---|---|---|
| `setCurrentBook(path)` | `ui/reader.cpp` | Single source of truth for the `(currentBookPath, currentBookKey)` invariant |
| `renameBook(oldPath, newPath)` | `ui/reader.cpp` | Wraps `migrateBookMetadata`; updates `g_reader` and wake target if the open book is being renamed |
| `resetAllPagination()` | `ui/reader.cpp` | Composes the three "layout changed" storage primitives + resets open reader's in-memory state |
| `PageOffsetTable` struct | `pure/page_offset_table.h` | Replaces `g_reader.pageOffsets[] + knownPages`; lets storage take the buffer as a typed parameter |
| `bookLeafLabel(path)` | `pure/paths.cpp` | Sibling of `folderLeafLabel`; was previously a firmware-only `g_library`-indexed helper |

### Signature changes

- `loadPageOffsetCacheForBook` / `savePageOffsetCacheForBook` now take a
  `PageOffsetTable&` / `const PageOffsetTable&` instead of writing into
  `g_reader` directly.
- `syncWakeState` now takes an explicit `path` arg instead of reading
  `g_reader.currentBookPath`.

### File reorganization (second-round cleanup)

After moving functions to the right *layer*, several `storage/` files were
in the wrong *neighborhood*:

- `storage/bookmarks.{h,cpp}` was renamed to `storage/book_metadata.{h,cpp}`.
  The name was misleading — the file owned bookmarks AND saved-page progress
  AND their joint lifecycle (clear, rename, bulk-invalidate). All three are
  "per-book NVS state" and follow the same lifecycle, so they share a file;
  the new name says so.
- `storage/book_state.{h,cpp}` was deleted entirely. Its contents were
  misc, not coherent:
  - `deleteBookMetadata` / `migrateBookMetadata` → `book_metadata.cpp`
    (they're per-book NVS lifecycle composers)
  - `isFolderExpanded` / `setFolderExpanded` → `library.cpp` (touches
    `g_library`, which lives there)
  - `bookLeafLabel(int idx)` → became pure `bookLeafLabel(path)` in
    `pure/paths.cpp` (it was just path formatting)
  - `syncWakeState` → new `storage/wake_state.{h,cpp}` (device-level wake
    intention, not per-book state)
- `page_cache.cpp` lost two cross-cutting concerns it had been carrying:
  the per-book bookmark/progress invalidation in `invalidateAllPageCaches`
  moved to `book_metadata.cpp` (as `resetAllSavedProgress` +
  `invalidateAllBookmarkOffsets`), and the `pc_*.bin` rename/delete logic
  inside `migrateBookMetadata` / `deleteBookMetadata` became
  `renamePageCacheForBook` / `deletePageCacheForBook` primitives that
  `book_metadata.cpp` calls.

### Dead-code dropped along the way

- The `wake_path` branch inside `deleteBookMetadata` — always a no-op given
  `handleDelete` clears wake state before calling it.
- The duplicate `kv.putInt(..._p, 0)` at the end of `invalidateAllPageCaches`
  — redundant with the per-book loop earlier in the same function.
- The unused `static PreferencesStore makeKv()` helper in `bookmarks.cpp`.
- Stale `pageOffsets[]` / `knownPages` direct accesses (all converted to
  `pages.offsets[]` / `pages.count` after the struct extraction).

### Final shape of src/storage/

| File | Owns |
|---|---|
| `book_metadata.{h,cpp}` | Per-book NVS state — bookmarks, saved progress, joint lifecycle (delete/rename/bulk invalidate) |
| `wake_state.{h,cpp}` | Device wake intention (`wake_mode`, `wake_path`) |
| `library.{h,cpp}` | Library catalog + folder UI state |
| `page_cache.{h,cpp}` | Page-offset RAM LRU + `pc_*.bin` files |
| `list_items.{h,cpp}` | Todo list |
| `settings_store.{h,cpp}` | Runtime settings load |
| `fs_util.{h,cpp}` | Filesystem setup |
| `preferences_store.h`, `kv_store.h` | KV interface + Preferences adapter |

### Follow-ups this unlocked

- The `#ifdef ARDUINO` walls in `storage/book_metadata.cpp` and
  `storage/list_items.cpp` can be revisited — some of what they guard is
  now straightforwardly host-testable.
- Refactor #2 (split `g_reader`) is partly done: `PageOffsetTable` is one
  of the four pieces that refactor was going to extract.

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
