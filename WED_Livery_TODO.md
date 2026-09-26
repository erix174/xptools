# Livery picker — open items

The one live list for `feature/ramp-livery-picker`. Supersedes the open-item
sections of `WED_LiveryRoadmap.md` and `WED_ProjectNotes.md`, which are stale and
are to be archived out of the tree before the upstream PR. Local planning only:
this file does not go upstream either.

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
- Upstream PR: split out the changes that are not the feature (ctype casts,
  validator country fix, AptIO unknown-row policy + discarded-row reporting,
  DEV logging hooks, parking PNGs, GUI text field word select / wider edit).
- Docs: spec draft 8 (move §6.7–6.8 before §7, R26–R28 into §1 with vectors,
  schema 4 grammar); rewrite the Brief as the one overview; archive Roadmap,
  both audits, ProjectNotes.
- `WED_LiveryPane.cpp` (~5,900 lines) - decomposition plan in the 09-25 audit.
- Data: 7 suspicious operator records and 419 country-level hubs from the
  09-19 enrichment report; `Obsolete` marks for retired airframes (e.g.
  `UAL:B744` empties UAL/E - `livery_obsolete_radius.py` first).
- Upstream WED bugs fixed on this branch - send as their own small PR, not with
  the feature: a failed save left the document reading as saved, so a close after
  it threw the edits away (now stays dirty); the package list needed a
  double-click inside 0.1 s of CPU time (now 0.4 s wall clock).
- Sync the schema-4 index to the installs: DONE 2026-09-25 (both installs).
