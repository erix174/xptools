# Static aircraft liveries - hand-off to the X-Plane side (detailed)

**Audience:** whoever implements the X-Plane (sim) side of WED 2.8's static-aircraft
liveries, and the AI agents they use. This is the long, reasoning-included version.
The short version for people is `WED_LiveryFormat_Brief.md`. The normative text is
`WED_LiveryFormatSpec.md` (draft 10); where this document and the spec disagree,
**the spec wins**. 2026-09-28, WED 2.8.0-a1, target X-Plane 12.5.

Branch: `feature/ramp-livery-picker` (fork `erix174/xptools`). All paths below are
relative to the repository root.

---

## 0. How to use this document (for an agent)

Read in this order, and do not skip step 2:

1. §1 below - what the feature is and why, in one page.
2. **Spec §0** - the frozen contract and the open questions P0-P6. Anything marked
   PENDING must be answered by a person before you build on it.
3. Spec §1 (rules R1-R33) - the checklist an implementation is graded against.
4. Spec §4.1 - the selection algorithm, as pseudocode. §2 of this file restates it.
5. Spec §6.2 - the livery index file format.
6. §5 below - how to prove your implementation matches WED (the conformance kit).

Rules are numbered and stable (a number is never reused). Cite them in code
comments (`// R18`) so a later reader can check you against the list.

---

## 1. What the feature is

Today X-Plane parks static aircraft at ramp starts by picking from `library.txt`
buckets keyed by operation type, size and airline (`lib/airport/aircraft/...`).
Authors can list airlines on a stand (`1301`), but cannot say which **sizes** may
park there, and the library has no notion of an aircraft being retired, of an
operator's reach, or of home-only liveries.

WED 2.8 adds, per stand:

- **Which size classes, in what proportion** - a new apt.dat row `1313` with six
  integer weights for ICAO wingspan classes A-F, written right after the stand's
  `1301`.
- **Who set it** - a new row `1315 A|M` (auto-filled / set by hand). Editor-only; the
  sim must skip it (R30).

And X-Plane 12.5 ships a **livery index**, `livery_index.txt`, next to the static
aircraft assets. It lists every livery `.obj` with its type, class, operator,
registration, notes (including `Obsolete`), typical range and scope, plus one
OPERATOR record per operator with country, operation class and hub airports. The
sim resolves what parks **at load time** against the index, so a new model appears
at every airport already authored and a retired one disappears, with no apt.dat edit.

Guiding principles the whole design follows (keep them when you have to decide
something the spec does not cover):

- **Additive only.** `1301` is untouched (R1). A reader that ignores the new rows
  behaves exactly as today (R2). Old scenery keeps working.
- **Soft-fail.** A malformed new row is dropped whole and never fails the file,
  airport or stand (R4, R5). apt.dat has always failed hard on bad rows; these must not.
- **One eligibility function.** Every stage of selection asks the index the same
  question (R18), and WED's editor, validator and moderation tools use the same rule,
  so what the author is shown is what the sim does.
- **An empty stand is a correct outcome.** If nothing fits, nothing parks. No
  fallback (§4 below, spec §4.5). WED warns the author instead.
- **Facts about the library live in the index, not in apt.dat.** Retirement, range,
  home-only - each is a fact about an aircraft or operator, so it is in the index and
  changes without anyone re-exporting scenery.

---

## 2. What the sim implements

### 2.1 apt.dat reading (spec §1, §2, §3)

| row | content | rules |
|---|---|---|
| `1313 wA wB wC wD wE wF` | six non-negative integers 0..1000, relative weights | exactly six, integers, in range, else the row is discarded whole (R4, R5, R11); binds to the most recent `1300` (R20); a second `1313` for the same stand: the first **valid** one wins (R24, V45) |
| `1315 A` / `1315 M` | who set the stand | ignored for selection (R30); anything else discarded |
| `1301 ... none ...` | operation type `none` | nothing parks, whatever the letter, list or `1313` say (R29) - see P5 |
| unknown rows / extra fields | | ignored (R15). X-Plane 12.4 already does this (measured, spec §7.2) |

With `1313` present, `1301`'s size letter is not used for selection (R23, V30) - WED
sets it to the highest non-zero class only for old readers and ATC/AI sizing.

### 2.2 Selection (spec §4.1 - restated)

```
if op_type == none:                 nothing                      # R29
if no valid 1313:                   today's behaviour, unchanged # R2, R17
class := weighted draw over A..F by the 1313 weights; all zero -> nothing
if op_type == general_aviation:     ga_choice(class, airport)    # R28
elif op_type == military and 1301 lists no airline:
                                    uniform over eligible military/gov liveries of class
else:
    airlines := [a in 1301 if eligible(a, class) non-empty]      # R18: same function
    if empty: nothing                                            # no fallback
    airline  := uniform(airlines)
    livery   := uniform (or EXPORT_RATIO) over eligible(airline, class)

eligible(a, class) = liveries of operator a (matched by the index AIRLINE cell, R31)
   at class, whose operator class matches the stand's operation type,
   not Obsolete (R25), in range (R26), home_ok (R27)
   [+ equipment test if P3 is accepted]
```

`op_class_matches`: airline stands take `Pax` (and `XPZZ_<type>`), cargo stands
`Cargo` (and `XPZZ_<type>`), military stands `Military` or `Gov`, GA stands `GA`.

### 2.3 The livery index (spec §6)

File: `Resources/default scenery/sim objects/apt_aircraft/livery_index.txt`. Header
lines `I` and `1 WED Aviation Database` are mandatory; `#` lines are comments; cells
are separated by `***`, and each cell is trimmed of spaces and tabs.

```
OPERATOR *** CODE *** NAME *** IOC CTY *** Pax|Cargo|GA|Military|Gov *** FLEET *** HUB ICAOs
TYPE *** CLASS *** AIRLINE *** REG *** REG CTY *** NOTE *** RANGE_KM *** SCOPE *** OP *** path
```

What the sim reads and how (R32, R33):

- **By position.** `path` is always the last cell of a livery row; everything else by
  position from the start. OPERATOR: first seven cells by position, ignore any after.
  Never reject a file for its `# schema` number.
- `CLASS` A-F. `AIRLINE` compared without regard to case against `1301` codes (R10, R31).
- `NOTE` equal to exactly `Obsolete` (whole trimmed cell, case-sensitive) = never
  spawns (R25). Anything else is free text.
- `RANGE_KM` whole km; empty / zero / negative / non-numeric = no limit. Distance:
  great-circle on a 6371 km sphere from the stand's `1300` position to the operator's
  nearest *placed* hub. Military and Gov are never range-checked; GA neither (R28).
  An operator with no placed hub has no limit (R26).
- `SCOPE` exactly `HOME` = only at an airport in the operator's country (OPERATOR
  country, else the row's `REG CTY`); anything else = no restriction. Unknown
  country on either side: allowed (R27).
- Hubs: space-separated ICAOs on the OPERATOR record, placed from the install's own
  Global Airports - `1302 icao_code` wins over the header ident; position is the
  datum, else the first runway's midpoint (spec §6.7b). A hub that cannot be placed
  is simply absent.
- Duplicate OPERATOR code or duplicate `path`: first wins (R33). `????` in a
  sim-read cell: row ineligible. Unknown operation class: PENDING (P4).
- Display-only cells the sim need not read: `NAME`, `FLEET`, `REG`, `TYPE`.
- Pseudo-operators: `XPGA` (generic GA), `XPMI` (generic military), `XPZZ_<type>`
  (unpainted airliner of that type - a passenger operator to the sim; that WED never
  places it automatically is WED-only).

### 2.4 Things WED will never change under you (spec §0)

Frozen: row codes, R1-R33, index cells in order and meaning, reserved words.
Free, never needing the sim: every data value (operators, hubs, ranges, notes,
Obsolete marks, new liveries), new index cells only where R32 puts them, the
WED-only airport database, and all WED tooling. A change outside that list means a
new spec draft agreed with you first.

---

## 3. Open questions for the sim side (P0-P6)

Each with our recommendation and what depends on the answer.

| # | question | recommendation | if the answer differs |
|---|---|---|---|
| **P1** | Does the sim read `livery_index.txt` as specified, and who regenerates it for each X-Plane release? | Yes, read it; regenerate with `tools/scripts/airline_research/gen_livery_index.py <X-Plane root>` (merge-only: every hand correction in the file is kept) as part of each release that touches `apt_aircraft/` | **The only answer that could change the file format.** Tell us before implementing; WED adapts. The index is install-specific - a stale index against new assets fails silently (spec §6.4) |
| P0 | Row codes `1313`, `1315` | Keep them | One constant each in WED (`src/XESCore/AptDefs.h`). Must be settled before WED 2.8 is released to authors - after that, data with the codes exists on the Gateway |
| P2 | Airport country for R27 / R28 | Use `1302 country` (WED's Gateway export adds an ISO 3166 code to every airport) with an ISO-to-IOC table shipped beside the index | If you need another source, it is additive (a new file or column), not a change to what you already parse |
| P3 | Equipment filter in `eligible()` | Apply it: a livery whose `path` starts with `heavy`, `jet`, `turboprop`, `prop`, `helo` or `fighter` is refused at a stand whose equipment types do not include that kind; other folders, or a stand with no equipment set, pass | If the sim does not filter, WED must drop the test, or its previews, coverage readout and R14 warnings disagree with what parks (R18). WED-side change only |
| P4 | Unknown operation class in the index | Fail closed: row never eligible | WED-side follows your choice |
| P5 | What does today's sim do with operation type `none`? | R29: nothing parks | If today's sim parks aircraft at `none`, R29 changes legacy stands. Note WED 2.7.2 (xptools#61) stopped turning `none` into Airline/GA on Gateway export, so `none` now reaches the Gateway as authored |
| P6 | Today's step-down for stands without `1313` | WED models: 75% to the letter's class, 75% of the rest to each smaller class, class A the remainder, falling through a class with nothing to park | WED's legacy preview and "Update Legacy Stands" use this model; if the numbers differ, WED changes a constant (`WED_LegacyStepDownWeights`) |

Also please confirm: a 299-character `1301` airline string fits the reader (WED 2.7
capped it at 99; 2.8 allows 299 = 75 three-letter codes).

---

## 4. Why it is shaped this way (the thought process)

These are the decisions that were argued out; each lists what was rejected so the
argument need not be repeated. Spec §8 has the long versions.

- **Weights, not a size range.** A range ("C to E") cannot say "mostly C, some D";
  six weights can, and "all zero" cleanly means "nothing parks here, on purpose".
  Weights are relative, not percentages, so they never need to sum to anything.
- **One row, not two.** Drafts 1-6 had a second row, `1312`, with per-stand `+`/`-`
  airline/type refinements. Dropped in draft 7: it duplicated what the index can
  say better (retirement, reach), it let the same fact be written in two places that
  could disagree, and a stand's airline list plus the class weights already pin the
  aircraft for nine operators in ten. A per-stand "lock to one operator" survives
  only as a UI view in WED, never in apt.dat.
- **Class first, then airline, then livery (§4.3).** Drawing the airline first lets
  an airline with no livery at the drawn class empty the stand. Class first, with
  stage 2 filtered by the same `eligible()` as stage 3, removes that failure (R18).
  The spec's example stand parked nothing half the time under the other order.
- **Obsolete in the index NOTE, not a deletion.** Retiring a livery must work for
  airports already authored, without a re-export. A reserved NOTE value does that;
  deleting the row would also delete the history of what was there.
- **Range from hubs, per livery row (R26).** Six alternatives were rejected (spec
  §8.7): per-stand deny lists, per-airport lists, country lists, region tags,
  computed networks, and none at all. Hub distance with a per-type range is small,
  data-only, and wrong only at the margins. Military/Gov and GA are exempt on purpose.
- **HOME scope (R27)** for a handful of liveries that must never appear abroad
  (a head-of-state 757, an air force's own airliner). Fails open when a country is
  unknown, because failing closed would empty stands for missing data.
- **GA 70% home-registered (R28).** GA traffic is overwhelmingly local; 70% keeps
  variety. No range, no airline list - a GA stand never names operators.
- **No fallback (§4.5).** About 30% of airline/cargo stands would park nothing if
  weighted strictly to their declared class, mostly because two retired CRJ liveries
  leave some operators without a class-C/D aircraft until new models land. A
  fallback would hide authoring errors and make WED's predictions wrong; WED instead
  warns the author (validator R14, with a one-click fix where the weights are the
  cause).
- **Legacy stands keep today's behaviour (R2, R17).** 30,000+ Gateway airports are
  authored without `1313`. They must park exactly as today until an author updates
  them; WED's "Update Legacy Stands to Spawn Weights" writes today's step-down as
  weights so an updated stand parks what it parked.
- **`1315` as its own row (R30).** The Gateway keeps only apt.dat, so the auto-fill
  mark has to live there for moderators. A seventh value on `1313` would break R5
  for readers that already implement six; `1312` is retired. A separate row the sim
  simply skips was the least intrusive. It also serves as WED's **2.8 fingerprint**:
  a stand carrying `1313` or `1315` was set in 2.8, which lets WED's legacy Gateway
  upgrade code leave it alone (the Gateway's bulk export must run WED 2.8+ for this).
- **Airline codes loosened (R10).** 3-4 letters/digits plus an optional `_suffix`
  (`afr_f`, `ryr_1`, `xpzz_b752`), because the index needs codes for divisions and
  generic types. Lower case in apt.dat, compared without case.
- **The index is read by position and tolerates new cells (R32).** So that data work
  (the bulk of all future maintenance) never needs a sim change.

---

## 5. Proving conformance

`docs/livery_conformance/` contains WED's own per-stand answer, produced by the same
C++ function WED's editor and validator use:

- `ZBAA_sample.tsv` - the sample package `docs/livery_sample/` (25 stands, one per
  rule, including malformed rows; sits over Beijing so the range rule shows).
- `LFPG.tsv` - Paris CDG from Global Airports, 513 stands.

Columns: `stand lat lon op equipment weights class airline type path verdict`. One
line per livery row at a class the stand opens with the verdict of `eligible()`
(`yes`, `equipment`, `out_of_range`, `home_only`), code-level lines
(`wrong_operation_class`, `no_livery`), and a final `STAND` line per stand
(`parks at C D`, `PARKS NOTHING`, `none: ...`, `not judged`). Sorted by stand,
class, airline, path. The header states the WED build and index version; compare
only against the same index.

Suggested procedure: add a debug switch that prints, per stand of a loaded airport,
the rows `eligible()` accepts per open class, in these columns; sort; diff against
the TSV. Every difference is a bug on one side or one of P2-P6. The README in that
folder explains regeneration (WED, Moderator Mode, Shift+click "Copy Summary to
Clipboard").

Do **not** use `tools/scripts/airline_research/livery_sample_expect.py` as the
reference - it predates several rules.

---

## 6. WED reference implementation (read, don't copy)

| what | where |
|---|---|
| Row codes, apt.dat structs | `src/XESCore/AptDefs.h`, reader/writer `src/XESCore/AptIO.cpp` |
| Per-stand import/export of 1313/1315 | `src/WEDEntities/WED_RampPosition.cpp` (`Import`, `Export`) |
| `eligible()` equivalent | `WED_LiveryFitsStand`, `WED_LiveryAllowedAt`, `WED_LiveryEquipment`, `WED_LiveryOperatorFitsRampOp` in `src/WEDLivery/WED_LiveryRules.cpp` |
| Range, hubs, index parsing | `src/WEDLivery/WED_LiveryIndex.cpp` |
| Operator records | `src/WEDLivery/WED_AirlineDirectory.cpp` |
| Legacy step-down model | `WED_LegacyStepDownWeights` in `WED_LiveryRules.cpp` |
| Per-stand analysis and the conformance report | `AnalyseStand`, `WED_LiveryConformanceReport` in `src/WEDLivery/WED_LiveryModeration.cpp` |
| Index generator | `tools/scripts/airline_research/gen_livery_index.py` |
| Code architecture | `WED_Architecture.md` §12c-§12d |

---

## 7. Release coordination the sim side should know about

- WED 2.7 refuses an apt.dat with any unknown row. Before WED 2.8 airports reach the
  Gateway, the 2.7 line must ship unknown-row tolerance, and the Gateway's bulk
  export must run WED 2.8. X-Plane itself already skips unknown rows.
- `livery_index.txt` must ship with 12.5, generated from the same build as the
  assets; WED 2.8 turns its livery features on when it finds the file.
- Until the sim implements 1313, authors' weights are stored and ignored (old
  behaviour): harmless, and they take effect the day the sim ships.

## 8. Glossary

- **Stand / ramp start:** apt.dat `1300` + `1301` (+ `1313`, `1315`).
- **Class:** ICAO wingspan class A-F.
- **Operation type (stand):** `none`, `general_aviation`, `airline`, `cargo`, `military`.
- **Operation class (operator):** `Pax`, `Cargo`, `GA`, `Military`, `Gov`.
- **Legacy stand:** one without a valid `1313`; parks by today's behaviour.
- **Pool stand:** GA, or military with no airline list; draws from the whole library by class.
- **Fingerprint:** a stand carrying `1313` or `1315`, i.e. set in WED 2.8+.
