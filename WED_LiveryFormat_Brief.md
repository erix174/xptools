# Static aircraft at ramp stands — the short version

For Jim K. and reviewers. 2026-09-26. WED 2.8.0, for X-Plane 12.5.

The full specification is `WED_LiveryFormatSpec.md` (draft 9). It is long on
purpose, written to be handed to an assistant with the apt.dat reader; section
and rule numbers below point into it. You should not need it to answer the asks.

---

## What the feature is

WED 2.8 gains a Liveries tab where a scenery author says, per stand, **which
airlines park there** (the existing `1301` list) and **which ICAO size classes
may spawn, in what proportion**. The second half needs somewhere to live, so
apt.dat gains **one row**, written right after the stand's `1301`:

```
1300 40.07800000 116.57223200 000.0 gate heavy|jets 03-MIX-CDE
1301 E airline dal ual
1313 0 0 6 3 1 0            <- relative weights for classes A-F: C 60%, D 30%, E 10%
```

`1301` is not modified and not deprecated. A reader that ignores `1313` behaves
exactly as today. A bad `1313` is dropped whole and never fails the file (R4, R5).

What parks is resolved at load time against a **livery index** shipped next to
the assets, so new models appear at every airport already authored, and retired
ones disappear, with no apt.dat edit. That index is the other half of the ask.

The sim change is three stages, class first (spec §4.1):

```
1. CLASS    weighted by the six integers in 1313
2. AIRLINE  uniform among those in 1301 with a USABLE livery at that class
3. LIVERY   uniform (or EXPORT_RATIO) among that airline's usable liveries there
```

"Usable" is one `eligible()` used by stages 2 and 3 alike (R18): right class,
right operation class for the stand, not `Obsolete`, within range, `HOME` rules
met. If stage 2 asks only "has this airline any livery?", an airline with
nothing usable at the drawn class gets picked and the stand parks nothing. On
the spec's example stand that happens **half the time**. Gate all of it on `1313`
being present (R17); without the row, today's behaviour is untouched.

---

## What we are asking of the sim

| # | ask | notes |
|---|---|---|
| 1 | **Confirm row code `1313`**, or give the one to use | It is **live**: WED 2.8 writes it on every X-Plane 12 export, Gateway included. Changing it is one constant (`AptDefs.h`). Best settled before 2.8 is released |
| 1b | **Confirm row code `1315`** (`1315 A` / `1315 M`: auto-filled or set by hand, R30), or give the one to use | New 2026-09-27. It carries WED's auto-fill mark through apt.dat so Gateway moderators see it. The sim only has to skip it - it never changes what parks. One constant in `AptDefs.h` |
| 2 | **Version policy** | Our recommendation: **no bump.** Tested on 12.4.4 and 12.4.3-r2: the sim ignores unknown rows *and* unknown version numbers (spec §7.2), so the row rides in a `1200` file |
| 3 | **Implement the reader rules** | Three-stage selection (R17, R18) plus: **R25** NOTE `Obsolete` never spawns; **R26** skip a row when the stand is farther than `RANGE_KM` from the operator's nearest hub (Military/Gov exempt; unknown range or no placed hub is never filtered); **R27** `HOME` rows only in the operator's own country, fail open if unknown; **R28** GA stands draw a home-registered GA livery 70% of the time when one exists, no range, no airline list; **R29** operation type `none` = no static aircraft, read exactly as six zero weights |
| 4 | **Ship `livery_index.txt` (schema 4) with X-Plane 12.5** | Under `Resources/default scenery/sim objects/apt_aircraft/`, from the same build as the assets (a mismatch fails silently, spec §6.4). WED 2.8 turns its livery features on when it finds it |
| 5 | **Please don't add a fallback** | An empty stand is the correct reading of what the author wrote. WED makes empties visible instead (below) |

**The index, schema 4** (spec §6.2). Mandatory header `I` / `1 WED Aviation
Database`, a `# schema 4` stamp, then one record per operator and one row per
`.obj`, cells split on `***` with spaces and tabs stripped:

```
OPERATOR *** CODE *** NAME *** IOC CTY *** Pax|Cargo|GA|Military|Gov *** FLEET *** HUB ICAOs
TYPE *** CLASS *** AIRLINE *** REG *** REG CTY *** NOTE *** RANGE_KM *** SCOPE *** OP *** path
```

`path` is always last. `SCOPE` is `HOME` or empty. Hubs are ICAO codes on the
OPERATOR record, placed by each reader from its own Global Airports (`1302
icao_code` wins over the header ident; datum, else first-runway midpoint), so WED
and the sim measure from the same points. Military/Gov records carry no hubs.
Pseudo-operators: `XPGA` general aviation, `XPMI` military, `XPZZ_<TYPE>` a
generic airliner of that type — never placed automatically. Today: 298 liveries,
1,548 operator records, 42 `Obsolete` marks (12 superseded assets, which alone
empty nothing; 20 retired airframes and 10 liveries of defunct operators, which
withdraw 24 airline/class pairs on purpose).

Match `1301` codes against the index's `AIRLINE` column, not the `library.txt`
bucket suffix: some differ on purpose (`EJU` is filed under `_ezy`, `CES_1`
under `_cyh`, `CHH` under `_hna`, `MAY`/`RUK`/`RYS` under `_ryr`; the full list
is in the spec's appendix).

**One Gateway constraint to know about.** WED caps the `1301` airline string for
Gateway submissions: 99 characters in 2.7, raised to 299 (75 three-letter codes)
in 2.8 - please confirm the sim's reader has no shorter limit. Airline codes are now
`[a-z0-9]{3,4}(_[a-z0-9]{1,6})?`, lower case (`dal`, `afr_f`, `xpzz_b752`), and
WED's validator rejects anything else (R10). The Gateway team should say
whether the cap can be raised, and confirm the server accepts suffixed codes.

---

## Try it

`docs/livery_sample/ZZZ_livery_format_sample/` — copy into `Custom Scenery/`.
Airport data only. It sits over **ZBAA** (Beijing Capital), 25 stands in a row,
named so you can read them off the ground: controls and malformed rows
(`01-CONTROL`, `04-ALL-ZERO`, `11-BAD-5-WEIGHTS` …), then the range bench
(`20-CN-CONTROL` parks Chinese narrowbodies; `22-FOREIGN-NARROW` lists
United, Delta, American and BA at class C and parks **nothing** — none of their
737s or A320s reaches Beijing; `24-SPLIT-IN-CLASS` keeps United's 767 and drops
its 757). A `1200` file carrying `1313`; the sim loads packages like it silently
(evidence: `docs/livery_evidence/`). Its README lists what every stand should
spawn, computed by `tools/scripts/airline_research/livery_sample_expect.py`.

---

## Where things stand

| | state |
|---|---|
| WED reads and writes `1313` | **done** — every X-Plane 12 export, Gateway included |
| WED reader skips unknown rows instead of failing the file, and lists them after import and in Validate (`warn_apt_dat_rows_not_imported`) | **done** — WED 2.7.x still refuses such files (spec §7.3) |
| Index reader, hub placement, schema 4 index + merge-only generator | **done** |
| Liveries tab: operator cards with real aircraft previews, weights, flags, recommendations | **done** |
| Coverage readout — "This ramp will spawn … 70% of the time", empty and single-operator warnings (spec §4.5) | **done** |
| Auto-fill of an airport's stands, never overwriting the author (spec §6.7f) | **done** |
| Moderation (Moderator Mode only): stepping through stands, flags for operators to check (not listed at the airport, or foreign military), stands with no airport data and stands that park nothing, web search, clipboard report | **done** |
| Validator: code shape and the Gateway's 299-character cap (errors; repeated codes are dropped silently on entry), R14 "this stand parks nothing" (`warn_ramp_livery_parks_nothing`, a warning; Airline/Cargo stands against their list, GA and unlisted military stands against the whole library; all-zero weights exempt) | **done** — with R14's three wordings and a one-click Fix in the Validation list |
| **Sim implementation** of R17, R18, R25–R30 and reading the index | **open** — asks 3 and 4 |
| **Row code and version policy** | **open** — asks 1 and 2 |
| **Mac / Linux build and run of WED** | **open** — never done |

---

## If you only remember five things

1. `1301` is untouched. `1313` is additive, six integers, soft-fail, and live.
2. Selection is **three stages, class first**, gated on `1313` — and stage 2
   uses the same `eligible()` as stage 3, range and `Obsolete` included.
3. The index ships with 12.5 and carries everything that changes: `Obsolete`,
   range, hubs, `HOME`. No apt.dat is ever re-exported for it.
4. `none` parks nothing; GA is by size and 70% home-registered.
5. We need a **row code** confirmation and a **version** decision from you.
   Everything on the WED side is done except Mac/Linux.
