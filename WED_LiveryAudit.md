# Ramp Livery Picker — end-to-end audit

Traced at commit `90d46af`, 2026-09-16. Covers the whole chain: selection → ask →
search → load → feedback → fallback → resource library → apt.dat / X-Plane.

Every line reference below was read, not inferred.

---

## Blockers

### B1. Flag assets are hardcoded to one developer's machine

```cpp
// WED_FlagAssets.cpp:44  AND  WED_FlagIndex.cpp:34 — the same literal, twice
static const char * kFlagAssetRoot =
    "C:\\Users\\Eric\\Desktop\\Laminar Misc Project\\WED\\xptools-livery\\src\\WEDLivery\\flags\\";
```

Three separate problems in one: duplicated definition, Windows-only separators, and
the 207 flag PNGs are **not deployed by any build system** — `cmake/WED.cmake` and
`WED.rc` contain no reference to `flags/`. Use sites append `\\`-separated
sub-paths (`WED_FlagAssets.cpp:161-181`, `WED_FlagIndex.cpp:45-61`).

Failure is silent: `EnsureFlagTexture` logs and returns, `mFlagTexId` stays 0, and
`Draw()` just skips the banner. `mLoadAttempted` makes it permanent for the session.

So the flag feature works on exactly one machine, in a source tree, on Windows.

### B2. `livery_index.txt` paths cannot be resolved — the preview chain is broken by design

The index stores real relative paths (`jet/B738_UAL_Legacy/738_United_Legacy_N78540.obj`).
The only resolution path in WED is `WED_LibraryMgr::GetResourcePath()`
(`WED_LibraryMgr.cpp:212-219`), an **exact-match lookup in a vpath table** built solely
from `library.txt` `EXPORT*` directives. A real path is not in it, so it returns `""`.

`WED_ResourceMgr::GetObj("")` then fails silently — and `DebugAssert(variant < GetNumVariants(vpath))`
does not fire, because `GetNumVariants()` returns 1 on a miss (`WED_LibraryMgr.cpp:297-302`).

There is **no public API to load an .obj by real path**: `WED_ResourceMgr::LoadObj(const string& abspath)`
is private (`WED_ResourceMgr.h:297`), and `GetObjRelative()` still requires a known
vpath as its anchor.

Two ways out, to decide before any more wiring:

- **(a)** add a public `GetObjAbsolute(abspath)` / make `LoadObj` public. Smallest change,
  but widens `WED_ResourceMgr`'s contract.
- **(b)** have the index store **vpaths** instead of real paths. The assets *are*
  already published as vpaths (`lib/airport/aircraft/airliners/jet_c_ual.obj`) — but
  those buckets are keyed only by (op type, class, airline), which is exactly the
  addressing gap the index exists to close. Requires X-Plane to publish per-livery
  vpaths, i.e. Jim K.'s side.

(a) unblocks us now; (b) is the right long-term shape. They are not exclusive —
do (a), and treat it as part of the placeholder that (b) retires.

### B3. No FBO capability check — crash, not degrade

`WED_LiveryThumbnailCache.cpp:139` calls `glGenFramebuffers` with no
`GLEW_ARB_framebuffer_object` test and no null check. On Windows and Linux these are
GLEW function pointers; on a driver without the extension the pointer is **NULL and
calling it crashes**, never reaching the `glCheckFramebufferStatus` guard at `:144`.

`WED_LibraryPreviewPane.cpp` has the same exposure but at least has a disable path
(`:462`, `:498-505`). This file has none. Latch a `mFBOUnavailable` flag on first use.

### B4. Multi-select toggle deletes airlines the user never set

`WED_LiveryPane.cpp:1400-1409`:

```cpp
set<string> first_codes = ParseCodes(mSelectedRamps[0]->GetAirlines());
bool was_set = first_codes.count(icao) != 0;     // decided by ramp 0 alone
```

then applied to every selected ramp. Ramp A lacks `aal`, ramp B has it: first click
inserts on both, second click **erases from both** — B loses a code the user never
touched and which was drawn unchecked the whole time. There is no mixed-state
checkbox. Data loss with no warning.

`SetRampOpFilter:1273` and `ApplyDragRange:1336` share the "read ramp 0, write all"
shape, but those are unambiguous "set all to X" gestures. The checkbox is not.

---

## High

### H1. Airline lists are not normalized on the way in

`WED_RampPosition::SetAirlines()` (`WED_RampPosition.cpp:180-183`) stores raw.
`CorrectAirlinesString()` runs only on `Export()` and inside the pane — **not on
import** (`:61`). `ParseCodes` (`WED_LiveryPane.cpp:675-682`) does not case-fold, and
row codes are always lowercase.

So an apt.dat carrying `1301 C airline AAL DAL` imports as uppercase, every checkbox
renders **unchecked**, and the first click writes `"aal dal aal"` — a duplicated code
that then round-trips back out. One-line fix: normalize in `SetAirlines`, or case-fold
in `ParseCodes`.

### H2. `WED_LiveryIndex` is dead code, and its one-shot load can't recover

Nothing calls it. Separately, `EnsureLoaded()` latches `mLoadAttempted` permanently,
but the X-Plane root can be changed at any time (`WED_StartWindow.cpp:334`,
`wed_ChangeSystem`). Selecting or changing the root after a failed attempt leaves the
index dead until WED restarts. Key the load on the resolved path and reload when it
differs — self-healing, no listener needed.

The other two loaders are unaffected: their files ship with WED and always exist.

### H3. No negative caching on a failed object load

`WED_ResourceMgr::GetObj` stores nothing on failure (`:288-292`), so a missing livery
re-runs `GetResourcePath` + `MemFile_Open` every frame. `kMaxRendersPerFrame = 2`
caps it at two attempts per frame — each with a `LOG_FLUSH()`
(`WED_LiveryThumbnailCache.cpp:91`), which is an fflush per frame, forever.

On Linux it is worse: `FILE_case_correct` does an `opendir` + linear `readdir` **per
path component** on every miss (`FileUtils.cpp:102-185`; it is a no-op on Windows).

### H4. `width_min` dies at the export boundary

WED stores a size *range* per ramp (`WED_RampPosition.h:73`, XML attr
`ramp_start/width_min`, with legacy backfill at `.cpp:118-134`), but `Export()`
(`:78-88`) cannot write it because `AptGate_t` has no field for it
(`AptDefs.h:477-486`). The range survives only in `earth.wed.xml`.

This is independent evidence for the weighted-size row proposal: the format is
already losing authored data today.

### H5. `WED_AirlineDirectory` load failure is completely silent

`WED_LiveryPane.cpp:552-553` gates on `IsLoaded()/LoadFailed()` and says nothing.
With it unloaded, names fall back to the 26-entry placeholder table, and the "Popular
Airlines" and "Same Country" tiers vanish entirely — `AppendAirlineSection:239`
returns false on an empty section and emits no header. The user sees a short,
name-less list and is told nothing. Contrast `WED_AirportDatabase`, which does report
(`:1927-1931`).

---

## Medium

- **"not found" is shown for corrupt files too.** `EnsureLoaded` returns false
  identically for missing, unreadable, and failed-mandatory-header
  (`WED_AirportDatabase.cpp:38-47`). A tampered file reports "not found", which will
  burn support time. Distinguish the header failure.
- **Unguarded index** `vpath[vpath.size()-3]` in `GetObj` (`WED_ResourceMgr.cpp:258`) —
  out of bounds for any path under 3 chars. Index-derived strings are now in play.
- **GL state leaks out of the thumbnail render**: `GL_LIGHT0` and
  `GL_LIGHT_MODEL_AMBIENT = {2,2,2,2}` are set and never restored
  (`WED_LiveryThumbnailCache.cpp:230-233`); also `glClearColor`/`glClearDepth`
  (`:164-165`), the renderbuffer binding (`:135`), and the texture binding (`:126`).
  Anything later in the frame that enables lighting inherits an ambient of 2.0.
- **Slider command spans two event handlers.** `StartCommand` at
  `WED_LiveryPane.cpp:1525` (MouseDown), `CommitCommand` at `:1664` (MouseUp).
  `Hide()` is reachable in between via the tab-switch path and does not close the
  command; the next `StartCommand` then trips the undo manager's assert.
- **Empty-list feedback is wrong and misplaced.** `:2963-2966` always says "…tagged
  for this operation type yet" even when the list is empty because a search matched
  nothing, and it draws at `ContentTop`, which is where the card strip lives — so it
  overlaps card 1.
- **Nothing explains *why* a tier is empty.** "Not researched", "filtered by search"
  and "directory failed to load" are indistinguishable from each other.
- **Auto-switch locks the map.** With `gPromptLiveriesOnRampSelect` on, clicking a
  ramp start reaches `WED_MapPane::SetTabFilterMode(tab_Liveries)`
  (`WED_MapPane.cpp:905-926`), locking orthos, facades, objects, polys, runways,
  taxiways and lines. Intended or not, it needs to be in the release notes.
  Related: `GUI_Control::SetValue` broadcasts even when unchanged, so a multi-select
  slider drag rebuilds the map filter once per ramp, per ephemeral broadcast.
- **Hardcoded tab index in two files** (`WED_LiveryPane.cpp:92-93`,
  `WED_MapPane.cpp:89-99`), correct only because of `AddPane` ordering in
  `WED_DocumentWindow.cpp:212-241`. Inserting a tab before index 5 silently
  mis-targets both.
- **Dirty flag false positive**: a click with no drag still does
  Start/CommitCommand. The undo manager drops the empty command, but
  `WED_Archive::CommitCommand:156` increments `mOpCount` first, so the document is
  marked dirty and prompts to save with nothing changed.
- **Thumbnail cache has no invalidation on X-Plane folder change**, and
  `mPreviewObjVpaths` is built once in the constructor and never rebuilt on
  `msg_LibraryChanged`.
- **Cache cap is a silent dead end**: at `kMaxCachedThumbnails = 32` `GetThumbnail`
  returns `nullptr` without evicting and without logging
  (`WED_LiveryThumbnailCache.cpp:84-85`). Unreachable at 4 cards; a permanent
  blank-card mode once cards become one-per-livery.

---

## Cross-platform

**Correct already:**

- `.txt` data-file placement, via `WedDataFileDir()` (`WED_MandatoryHeader.cpp:31-43`)
  with a proper `#if APL` bundle-vs-executable split, deployed from one
  `WED_DATA_FILES` list for all three platforms.
- The `.png`-named-but-`.dds`-on-disk texture substitution — **I expected this to be
  broken and it is not.** `process_texture_path()` (`WED_ResourceMgr.cpp:79-97`)
  discards the declared extension and probes `.dds` → `.png` → `.bmp`, and
  `FILE_exists` is case-desensing on Linux/macOS and case-insensitive on Windows.
- FBO entry points *are* GLEW-loaded on Linux (`glew.c` compiled in the LINUX branch,
  `glewInit()` in `XWinGL.lin.cpp:64-73`). The gap is the missing extension check
  (B3), not the loading.
- `(std::min)`/`(std::max)` parenthesisation against the `<windows.h>` macros.
- Layout is derived from `GUI_GetLineHeight` / `GUI_MeasureRange` throughout — no
  hardcoded font pixel sizes.

**Correction to something I said earlier in this session:** I claimed WED pins the C
locale globally. It does **not** — `setlocale(LC_ALL, "C")` at `WED_AppMain.cpp:231`
is inside `#if LIN`. The conclusion still holds, but for a different reason: on
Windows and macOS the startup locale is `"C"` by definition and nothing in WED changes
it, so `%lf` cannot emit a comma separator there. The residual risk is **Linux only**,
where that single one-shot call after the first window creation is the only thing
standing between FLTK and `48,123456` in every exported apt.dat. There is no
defensive per-write guard in the export path.

**Remaining platform risks:**

- macOS is the least-proven build for `WED_LiveryThumbnailCache.cpp`: it relies on
  `<OpenGL/gl.h>` transitively declaring the unsuffixed ARB entry points rather than
  the `…EXT` spellings. It matches `WED_LibraryPreviewPane.cpp`'s established
  pattern, but wants an actual Mac compile before shipping.
- `glDeleteTextures` is called from `Hide()` (`WED_LiveryPane.cpp:847`) and from the
  destructor with no guarantee a GL context is current. Benign on Windows/Linux with
  one shared context; the classic undefined case on macOS.
- Line endings differ by build platform (`CRLF` is `"\r\n"` on Windows, `"\n"`
  elsewhere — `XDefs.h:30-34`), so the same scenery exported from WED on Windows and
  on Linux is byte-different. X-Plane tolerates it; diffs and checksums do not.

---

## What is missing rather than broken

- Nothing wires `WED_LiveryIndex` into the pane yet — the tri-state
  (`livery_Hit` / `livery_Ignore` / `livery_Faulty`) exists and compiles but is never
  called.
- No UI for `livery_Ignore` / `livery_Faulty`. Until there is, "no model yet" and "bad
  code" both render as nothing.
- No "index not loaded" banner, distinct from "this airline has no model". Without it
  a user with no X-Plane root selected sees an empty tray with no explanation — the
  exact failure this feature was supposed to avoid.
- The apt.dat spec document for Jim K. is still unwritten, and no new row code is
  emitted, so nothing of this reaches the simulator yet.

---

## Suggested order

1. B4 and H1 first — they corrupt user data, and both are small.
2. B2 next: it decides whether the preview chain can work at all, and every later
   wiring decision depends on the answer.
3. B1 before any release build.
4. B3 before anyone runs WED on remote desktop, a VM, or software GL.
5. H2, H3, H5 — cheap, and each removes a silent failure.
6. Medium items as they are touched.
