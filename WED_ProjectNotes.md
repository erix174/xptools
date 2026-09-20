# Ramp Livery Picker — project notes

Working notes for the `feature/ramp-livery-picker` branch. **Read this first** when
picking the work back up; it records the things that are not recoverable from the code
or the git history.

Last updated: 2026-09-16.

---

## What the feature is

A property-panel tab in WED ("Static Liveries") that lets a scenery author say which
airlines — and eventually which specific aircraft and liveries — should park at a given
ramp start, with a visual preview of each one.

The preview is the part that forced everything else. To show "United's 737-800 in the
retro livery," something has to be able to *name* that object. Nothing in X-Plane's
data could, which is where most of this project's work has gone.

## The three-way dependency (why this feels messy)

| | owner | status as of 2026-09-16 |
|---|---|---|
| X-Plane reads the new apt.dat format | Jim K. (ATC engineer) | blocked — needs a reference apt.dat from us |
| apt.dat generation spec | us | not written |
| WED index + filtering + UX | us | index done, loader/wiring in progress |

Missing either of the first two means nothing is visible in the simulator, so the whole
flow can only be exercised inside WED. That is expected and fine — but it means "it
works" currently means "it works in the editor."

Release timing: WED is waiting on the 2.7.2/2.7.3 cargo. This feature is heavy enough
to be **2.8**, so it waits for that merge before an official PR. **Do not push to
GitHub in the meantime** — commit locally and mirror to the bare repo (below).

## Repos

- Working tree: `C:\Users\Eric\Desktop\Laminar Misc Project\WED\xptools-livery`
  (a git worktree; main checkout is `...\WED\xptools` on `wed_270_release`).
- Local backup mirror: `...\WED\wed-local.git`, registered as remote `local`.
  `git push local <branch>`. The clone is shallow, so the bare repo has
  `receive.shallowUpdate=true` set — without it, pushes are rejected.
- `origin` = X-Plane/xptools (read-only for us), `myfork` = erix174/xptools.

---

## Data files and who owns them

### WED side — `src/WEDLivery/*.txt`

Loose files copied next to `WED.exe` at build time, each opened with a plain
`ifstream` at `FILE_get_dir_name(GetApplicationPath()) + "<name>.txt"`. Every one starts
with the same two-line stamp (`I` / `1 WED Aviation Database`) checked by
`WED_MandatoryHeader` — a **hard** failure, while individual malformed rows are skipped
so one bad row can't take out the file.

| file | loader | notes |
|---|---|---|
| `WED_AirportDatabase.txt` | yes | ICAO → country + airlines |
| `livery_index.txt` | yes | liveries AND operator records - code → name, IOC country, operation class, fleet size, hubs. Lives in the X-Plane install, not beside WED.exe, because the sim reads the same file. |
| `WED_AircraftSizeReference.txt` | **no** | type → ICAO wingspan class. Data-only; the generator bakes the class into the index, so WED never loads this at runtime. 2619 rows but only ~468 carry a class — the blank ones are a deliberate TODO convention, not corruption. |

**Known bug:** the `.txt` POST_BUILD copies in `cmake/WED.cmake:810-828` are
Windows-only. Mac (`:835`) and Linux (`:881`) deploy `WED_RESOURCE_FILES`, which does
not contain them — so on those platforms the loaders silently `LoadFailed()`.
Must be fixed; Windows-only is not acceptable to the release manager.

### X-Plane side — `livery_index.txt`

```
<X-Plane root>/Resources/default scenery/sim objects/apt_aircraft/livery_index.txt
```

**Manually maintained.** Bootstrapped by `tools/scripts/airline_research/gen_livery_index.py`,
but edited by hand from here on — re-running the generator is a diff review, never a
blind overwrite.

```
<TYPE> *** <AIRLINE> *** <REG> *** <REG COUNTRY> *** <NOTE> *** <path under apt_aircraft/>

B738 *** UAL *** N78540 *** USA *** Retro   *** jet/B738_UAL_Legacy/738_United_Legacy_N78540.obj
B738 *** AAL ***        ***     *** Default *** jet/B738_AAL/B738_AAL_static.obj
```

It lives on the X-Plane side because the inventory is a property of the X-Plane
installation, not of WED, and because any PR adding a livery is already touching that
directory. WED reaches it through `gPackageMgr->GetXPlaneFolder()`; `SetXPlaneFolder()`
refuses a root without `Resources/default scenery`, so the path always resolves when
`HasSystemFolder()` is true. Selecting an X-Plane root is already a hard prerequisite
for loading any livery, so this adds no new dependency.

Country codes are **IOC, not ISO** — Hong Kong `HKG`, Taiwan `TPE`, Macau `MAC`
(see `WED_IocCountryCodes.h`). Empty registration is legal; older assets have none.
`????` in any column is a TODO marker, not a value.

---

## What we learned about X-Plane's static aircraft

This is the part that is expensive to rediscover.

**Where they are:** `Resources/default scenery/sim objects/apt_aircraft/<category>/<folder>/`,
298 `.obj` files in 12.4.3-r2, 376 in the 12.4.4-pnl5 beta — the count is
install-specific, which is why the index records the build it was read from.
Each livery is its **own complete .obj** (A320_BAW is 9.4 MB, 135,972
vertices) — there is no shared-mesh/texture-swap scheme. A future move to glTF may
change that; the index is designed to survive it.

**How they are published:** `library.txt` `EXPORT_EXTEND` stacks several real objects
onto one vpath and the sim picks randomly:

```
lib/airport/aircraft/<op type>/<class>_<size>[_<airline icao>].obj
                      airliners    jet     a-f       baw
                      cargo        heavy
                      general_aviation  prop
                      military     turboprop
```

`EXPORT_RATIO` also exists and already gives weighted random selection (Norwegian 8 :
fictional 1) — a syntax precedent for the weighted apt.dat rows we want.

**What the vpath does NOT encode:** aircraft type (`heavy_e` mixes A359 ×39, A35K ×11,
B772 ×5) and livery variant. Those two axes are exactly what our index adds.

**Variants already exist in the shipped data**, just unaddressable:
`B738_UAL_Legacy`/`_Modern`, `A359_Korean`/`_Korean_Modern`, `B738_JYH_*` (11 colours),
`B738_CCA_Peony_*` (5 tail numbers), `B738_RYR_9H/EI/G/SP`.

**Ryanair's four folders are four companies, not four liveries** — each has its own ICAO
code and its own OPERATOR record in the index: `RYR` Ryanair, `RUK` Ryanair UK, `RYS` Buzz,
`MAY` Malta Air. Both generators share one `AIRLINE_REMAP` table so they can't drift.

**Resource-library defects the index surfaced** (Laminar's to fix, not ours):
38 `_DUP` collisions where old and new assets coexist and both get exported
(`AT45_FDX_static.obj` and `ATR42-500_FedEx.obj` are the same aircraft);
6 orphan objects on disk that `library.txt` never exports;
`heavy/B772_AAL/` and `heavy/B772_AAl/` are byte-identical duplicate folders (~13 MB
wasted) that the generic and airline buckets point at *separately* — fine on
case-insensitive filesystems, broken on Linux.

## apt.dat facts (verified, needed for the spec)

- Ramp start is two rows: `1300 <lat> <lon> <heading> <type> <equipment> <name>` then
  `1301 <size letter> <ramp op type> <airlines...>`. Writer `AptIO.cpp:1441-1483`,
  reader `AptIO.cpp:737-765`, struct `AptGate_t` at `AptDefs.h:477`.
- `1301`'s airline list is **variable-length and last**, so nothing can be appended to
  that row. New data needs a new row code. `1300`/`1301`/`1302` are taken; `1303` is free.
- **WED does not skip unknown numeric row codes** — `AptIO.cpp:1208` falls through to
  `"Illegal unknown record"` and fails the whole load. Jim must confirm whether the sim
  does the same; if so, new rows cannot ship before the readers do.
- WED re-emits from typed structs, so any row it does not model is silently dropped on
  the next export.
- `#` comment lines are safe: `AptIO.cpp:346` skips any line not starting with an int.
- **`width_min` already dies at the export boundary.** WED stores a size *range* per ramp
  (`ramp_start/width_min` in its own XML) but `WED_RampPosition::Export()` drops it
  because apt.dat has nowhere to put it. Independent evidence that the format is already
  losing authored data.
- Scale: 200,102 ramp starts globally, but only **2,821 distinct** `1301` payloads —
  a 71× duplication factor. That, not file size, is the argument for
  definition/reference separation.

---

## Standing design rules

1. **Preview cards are static images, never live 3D.** `WED_LiveryThumbnailCache` renders
   off-screen into a texture via a throwaway FBO. Do not "simplify" this into drawing
   models on the cards. The long comment in `WED_LiveryThumbnailCache.h` is the reason
   this survived several rounds of iteration.
   - The bug that cost the most time: `glClear(GL_DEPTH_BUFFER_BIT)` is a **silent no-op**
     while `glDepthMask(GL_FALSE)`, which 2D GUI drawing leaves off. `SetState(...)` must
     come *before* the clear. Symptom was black thumbnails with only depth-test-disabled
     debug shapes visible.
2. **Nothing outside the resolver may include the index header.** The index is a
   placeholder that X-Plane's real mechanism replaces in weeks; the isolation is what
   makes that deletion a small diff.
3. **Don't touch `src/XESCore/AptIO.cpp`** until the format is agreed with Jim. It is
   shared with the wider scenery toolchain and a new row code is a permanent commitment.
4. **All three platforms, always.** The release manager will not accept Windows-only.
5. Data files never duplicate a fact another data file owns, *unless* the file is
   generated — a generator cannot disagree with itself. That is why the index carries the
   size class but `WED_AircraftSizeReference.txt` stays the source.

## Open items

- `kFlagAssetRoot` in `WED_FlagAssets.cpp` / `WED_FlagIndex.cpp` is a hardcoded absolute
  path under one developer's home directory. **Must be fixed before this ships.**
- Mac/Linux `.txt` deployment (above).
- 21 rows in `livery_index.txt` still need a human (military, business jets, and two
  Laminar fictional liveries `LR-AUS` / `LR-XUT`).
- The weighted-size UI (bar chart per class, relative integer weights, non-contiguous
  sets so a class can be banned outright) is designed but not built.
- apt.dat spec document for Jim K. is not written.
