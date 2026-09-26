# Livery picker — open items

The one live list for `feature/ramp-livery-picker`. The Roadmap, both audits and
ProjectNotes it replaced were archived on 2026-09-25 to
`WED/livery_history/2026-09-25_superseded_docs/`, outside the tree. Local
planning only: this file does not go upstream either.

**Checkpoint 2026-09-25**, taken before Moderation Mode starts. The tree is clean,
Release and Debug build, and everything is pushed to `local`.

## To tell colleagues — once everything is done, in one go

Nothing here is sent before the feature is complete (Eric's call, 2026-09-25).

| For | Item |
|---|---|
| Jim K. (sim) | **Row code 1313** is live in WED and exported everywhere, Gateway included, as of 2026-09-25. It does not crash X-Plane (unknown rows are skipped - measured, spec §7.2). Ask him to confirm the code, or give the one to use; changing it is one constant in `AptDefs.h`. |
| Jim K. (sim) | Whether the apt.dat **version** is bumped for this. Our recommendation: no (spec §9.2). |
| Jim K. (sim) | Reader rules the sim must implement: R25 (NOTE `Obsolete` never spawns), R26 (range from the operator's hub ICAOs, placed from Global Airports), R27 (`HOME` = only in the operator's own country), R28 (GA: 70% home-registered, no range), R29 (None = no static aircraft). All in `WED_LiveryFormatSpec.md` §1 and §6.6–6.7e. |
| Jim K. (sim) | Spec §4.5: 29.8% of airline/cargo stands would park nothing if weighted to their declared class, mostly because of two `Obsolete` marks (Delta CRJ-200, Air France CRJ-100). The sim must not add a fallback. |
| Laminar (release) | `livery_index.txt` (schema 4) must **ship with X-Plane 12.5** under `apt_aircraft/`; WED 2.8 turns livery features on when it finds it. |
| Gateway team | **The 100-character limit on a ramp start's airline string** (`WED_Validate.cpp`, Gateway target). About 24 three-letter codes. Auto-fill stops short of it, so a busy airport cannot list all the airlines that serve it; Eric would like 40–50. Ask whether it can be raised, and to what. |
| Gateway team | Airline codes now take the form `[a-z0-9]{3,4}(_[a-z0-9]{1,6})?` (`ryr_1`, `afr_f`, `xpzz_b752`) - check the server's own validation accepts them. |
| Release manager | 2.8 targets X-Plane 12.5 only. `earth.wed.xml` stays readable by 2.7 (operation types are stored under their original names). |
| WED maintainers | Six small upstream PRs, independent of the feature - see below. |

## Decided 2026-09-25 (evening)

- **The CRJ `Obsolete` marks stay**, Delta's CRJ-200 included. A batch of new CRJs is coming soon to fill those stands.
- **Military and GA stands that park nothing now warn**, with the same waivable `warn_ramp_livery_parks_nothing`. The message names the size as the cause. The check also gained the operation-class filter the tab always had: a cargo operator no longer satisfies a passenger stand.
- **The installed sample packages were replaced** by `docs/livery_sample/` in both installs. The old ones were kept in the session scratchpad only.

## Next: Moderation Mode

- **Map callouts: framework built 2026-09-25** (`WEDMap/WED_ModerationLayer`, model `WED_ModerationDescribe`).
  - Every selected ramp start gets a card: flag / ICAO / name; airlines; Legacy or Updated (A/M) with size or weights; op type, equipment and ramp type.
  - The hover tray lists the operators three to a row, with a verdict each. "?" opens a web search.
  - Similarity colour; pin-and-compare (+ / - / ~).
  - It shows in normal mode for now. Before release, gate it on Moderation Mode through `WED_ModerationEnabled()`.
- **Callouts, open:**
  - Card placement on a dense row of stands: cards cover their neighbours.
  - Whether the whole airport, or only the selection, gets cards.
  - Clicking "?" was not exercised by automation, since it opens a browser.
- What already exists:
  - Ramp stepping with Ctrl+Shift+. and Ctrl+Shift+,.
  - The "operators to check" prompt with a web search.
  - `WED_ModerationEnabled()`, which is always true: one version for everyone.
- Detected but not shown anywhere yet: the auto-fill watermark (`auto_filled`), and op type None.
- `WED_LiveryModeration.h` still speaks of a preference checkbox. Settle that in the design.

## Open, no decision needed

- **Mac / Linux build and run** - never done. Eric tests on his Intel Mac before hand-over. The new code has nothing platform-specific (checked 2026-09-25).
- **UI check after `2600006`** (the placeholder airline list removed): the airline list's names and search. The build is fine; the screen check was stopped because Eric was typing.
- **Data:**
  - 419 country-level hubs ("approx") - barely matter for range.
  - AHY B752 is stored, not confirmed retired.
  - The CYH Peacock row lost its caption to its `Obsolete` mark.
- **Sample:** a stand carrying two `1313` rows (R24).

## Upstream PRs (ready, not sent)

- Local branches off `origin/wed_270_release`, each one built on its own, in worktree `WED/xptools-upstream`, backed up to `local`.
- When the feature itself goes up, rebase it onto these.

| Branch | What |
|---|---|
| `upstream/save-and-doubleclick` | A failed save no longer reads as saved; a package-list double-click works (0.4 s wall clock) |
| `upstream/text-field-word-select` | Double-click selects a word; the table edit field grows to fit. Stacked on the branch above |
| `upstream/ctype-and-validate-country` | Signed-char ctype calls; a bare "USA" country aborted validation |
| `upstream/exit-logging` | DEV log flush and exit-path logging |
| `upstream/apt-unknown-rows` | Unknown apt.dat rows are skipped and reported instead of failing the file. The 1313-free version, tested end to end on 2.7 |
| `upstream/parking-icons-d-e` | ClassD and ClassE icons fill their frame like the other four |

`archive/stash-2026-09-13-wip` preserves the pre-commit WIP stash of 2026-09-13. Everything in it was superseded by the branch; it was removed from the stash list.

## Done, 2026-09-25 (details in git log)

- **Pane:**
  - `WED_LiveryPane.cpp` split into five files.
  - The readout names the military countries a stand draws from, and says GA comes "from all over the world".
  - UAL's 767 card got its flag.
  - The placeholder airline list was removed.
- **Data:**
  - 7 suspicious operator records checked; 6 corrected.
  - 44 `Obsolete` marks: 12 superseded assets, 20 retired airframes, 12 rows of 11 defunct operators.
  - US military transports and trainers marked HOME.
  - Everything synced to both installs.
- **Docs:**
  - Spec draft 8, with §4.5 and §8.6 re-measured.
  - The Brief.
  - The sample README, regenerated by `livery_sample_expect.py`.
  - The WEDLivery section of `WED_Architecture.md`.
- **Eric's nine-point list (2026-09-17)** is closed.
  - Points 3 and 4 are waived in favour of the occupancy readout.
  - Points 5 and 6 are now built.
