# Livery picker — open items

The one live list for `feature/ramp-livery-picker`. The Roadmap, both audits and
ProjectNotes it replaced were archived on 2026-09-25 to
`WED/livery_history/2026-09-25_superseded_docs/`, outside the tree. Local
planning only: this file does not go upstream either.

Last updated 2026-09-25.

## To tell colleagues — once everything is done, in one go

Nothing here is sent before the feature is complete (Eric's call, 2026-09-25).

| For | Item |
|---|---|
| Jim K. (sim) | **Row code 1313** is live in WED and exported everywhere, Gateway included, as of 2026-09-25. It does not crash X-Plane (unknown rows are skipped - measured, spec §7.2). Ask him to confirm the code, or give the one to use; changing it is one constant in `AptDefs.h`. |
| Jim K. (sim) | Whether the apt.dat **version** is bumped for this. Our recommendation: no (spec §9.2). |
| Jim K. (sim) | Reader rules the sim must implement: R25 (NOTE `Obsolete` never spawns), R26 (range from the operator's hub ICAOs, placed from Global Airports), R27 (`HOME` = only in the operator's own country), R28 (GA: 70% home-registered, no range), None = no static aircraft. All in `WED_LiveryFormatSpec.md` §6.6–6.7e. |
| Laminar (release) | `livery_index.txt` (schema 4) must **ship with X-Plane 12.5** under `apt_aircraft/`; WED 2.8 turns livery features on when it finds it. |
| Gateway team | **The 100-character limit on a ramp start's airline string** (`WED_Validate.cpp`, Gateway target). About 24 three-letter codes. Auto-fill stops short of it, so a busy airport cannot list all the airlines that serve it; Eric would like 40–50. Ask whether it can be raised, and to what. |
| Gateway team | Airline codes now take the form `[a-z0-9]{3,4}(_[a-z0-9]{1,6})?` (`ryr_1`, `afr_f`, `xpzz_b752`) - check the server's own validation accepts them. |
| Release manager | 2.8 targets X-Plane 12.5 only. `earth.wed.xml` stays readable by 2.7 (operation types are stored under their original names). |

## Still to build or decide

- Moderation mode: further details from Eric. Notes for the auto-fill watermark
  and op type None are detected but not shown anywhere yet.
- Military country line (point 5 of Eric's nine-point list); the other eight
  points are not recorded anywhere - ask Eric.
- Mac / Linux build and run - never done.
- Upstream PRs: five local branches off `origin/wed_270_release`, all built. Worktree `WED/xptools-upstream`, backed up to `local`.
  - `upstream/save-and-doubleclick`
  - `upstream/text-field-word-select`: stacked on the one above.
  - `upstream/ctype-and-validate-country`
  - `upstream/exit-logging`
  - `upstream/apt-unknown-rows`: the 1313-free version, tested end to end on 2.7 with the ZBAA apt.dat.
  - Not split yet: the parking PNGs.
  - When the feature itself goes up, rebase it onto these.
- Docs: spec draft 8 and the Brief rewrite are DONE (2026-09-25). Left over:
  - §4.1 says a Military stand with an empty airline list draws any Military/Gov livery at its class. That was inferred from the auto-fill code; Eric to confirm.
  - `docs/livery_sample/README.md` still describes the old ZZLI package.
  - §4.5's 17.2% and §8.6's 168/148 figures predate range and the 32 marks, and need recomputing.
- `WED_LiveryPane.cpp` decomposition: DONE 2026-09-25. Five files (core, Layout,
  Input, Draw, Rows) plus `WED_LiveryPaneInternal.h`. The code was moved, not
  changed. Release and Debug build, smoke-tested.
- Data, 2026-09-25 pass: DONE for the 7 suspicious operator records.
  - AEH, DSB, EPT, LHA, MXM and NSE were corrected. DTH was already right (Tassili was renamed Domestic Airlines).
  - LHA and MXM are now Cargo.
  - 20 retired airframes are marked `Obsolete` (spec §6.6 case 2). That makes 32 marks, which empty 14 pairs: AFR/B AUA/B CJT/C CPA/E DAL/B DAL/E DLA/C EXS/D HMF/A SAS/C SIA/C UAE/E UAL/E VIR/E.
  - Left unmarked: AHY B752 (stored, not confirmed retired) and CNV BE9L (mark it once the T-44 sundown completes in 2026).
  - **Eric to decide:** should liveries of operators that no longer exist be marked Obsolete too? These are AZA MD82, EGF AT72, AWE A320, BER A320, LPV D328, RUS J328, WLC J328, RVF B752, SWG B738, SWQ B738, and CYH B738 (2 rows). Each is that operator's only pair.
  - Still open: 419 country-level hubs ("approx"), which barely matter for range.
- Upstream WED bugs fixed on this branch: now on `upstream/save-and-doubleclick`.
  - A failed save left the document reading as saved, so closing it afterwards threw the edits away.
  - The package list needed a double-click inside 0.1 s of CPU time.
- Sync the schema-4 index to the installs: DONE 2026-09-25 (both installs).
