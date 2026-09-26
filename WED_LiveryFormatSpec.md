# apt.dat row 1313: per-stand fleet weighting

**Specification and implementation manual.** Draft 8, 2026-09-25. WED 2.8.0,
for X-Plane 12.5. For the X-Plane side of WED's ramp livery picker.

---

## How to use this document

This is the long form, written to be complete rather than readable. It is meant
to be handed to an assistant alongside the X-Plane apt.dat reader, and it is
structured so that can be done mechanically:

| § | contains | use it for |
|---|---|---|
| 1 | Normative rules, numbered `R1`… | the checklist an implementation is graded against |
| 2 | ABNF grammar, and a worked example | writing the parser |
| 3 | Reader algorithm | what to do with each row |
| 4 | Selection algorithm | the runtime behaviour, which is the real change |
| 5 | Conformance vectors | tests, including every malformed case we could construct |
| 6 | The livery index | the companion data file (schema 4), the range, home and GA rules, and WED-only authoring features |
| 7 | Evidence | measurements and the compatibility test, with method |
| 8 | Rationale | why this shape, and not the three candidates or the rows we dropped |
| 9 | Open questions | what is not decided |

**The short version is `WED_LiveryFormat_Brief.md`**: what the feature is, what
we are asking of the sim, and where the WED side stands. If you are Jim or a
reviewer, read that one first.

### Conventions

MUST / MUST NOT / SHOULD / MAY are used in the RFC 2119 sense. Every normative
statement carries an `R`-number so an implementation can be checked against a
list rather than against prose. Line references of the form `AptIO.cpp:359` are
into the WED/xptools tree at branch `feature/ramp-livery-picker` as of this
draft; earlier drafts' line numbers were not carried forward.

### What changed since draft 7

Draft 8 records the decisions of 2026-09-25 and reorganises the document. The
format itself is still one row.

- **`1313` is live.** WED 2.8 writes it on every export aimed at X-Plane 12,
  Gateway included (`AptIO.cpp:1428`, gated on version 1200). The number is
  still provisional pending Jim K.'s confirmation, and changing it is one
  constant (`apt_startup_loc_weights`, `AptDefs.h:104`). The feature targets
  X-Plane 12.5. No apt.dat version bump; the recommendation of §9 stands.
- **`1312` stays deleted.** It survives only as history (§8.6).
- **The airline code grammar is exact and checked (R10).**
  `[a-z0-9]{3,4}(_[a-z0-9]{1,6})?`, lower case in `1301`; anything else is a
  validator error. Gateway caps the
  `1301` airline string below 100 characters, about 24 codes - recorded as a
  known constraint.
- **Operation type `none` means no static aircraft (R29).** The sim reads it
  exactly as six zero weights. `earth.wed.xml` keeps operation types under their
  original names (General Aviation, Airline, Cargo, Military); the Liveries tab
  labels them Private/BizJet, Passenger, Cargo, Military/Gov, and nothing else
  changes.
- **The index is schema 4 (§6.2).** Mandatory two-line header, OPERATOR records
  carrying each operator's country, operation class, fleet and hub ICAOs; livery
  rows of ten cells ending in `path`. Hubs are placed by each reader from Global
  Airports, not written as coordinates.
- **R26, R27, R28 and R29 join §1**, with conformance vectors V33-V44 in §5. R26
  (range), R27 (`HOME`) and R28 (general aviation) were specified in §6.7-6.7e
  in draft 7 but were missing from the rule list and the tests.
- **R18 now carries the range and operation-class filters.** Draft 7 said an
  operator whose liveries were all out of range was drawn and then parked
  nothing "for that draw", which contradicted R18. It is now one `eligible()`
  that includes every filter, so an operator with nothing in range is never
  drawn (§4.1).
- **R14 is a WED validation warning** (`warn_ramp_livery_parks_nothing`), for
  Airline and Cargo stands only, with all-zero weights exempt. Draft 7's
  "hard export error" wording in §4.5 is gone.
- **WED's reader no longer fails a file on an unknown row code.** It skips the
  row, records it on its airport (`AptInfo_t::discarded_rows`), lists what it
  skipped after File > Import and raises `warn_apt_dat_rows_not_imported` in
  Validate. R19's "a `#` line fails the file" and §7.3's "every WED refuses the
  file" are now statements about WED 2.7.x and earlier.
- **WED-only features are labelled as such.** Auto-fill, moderation (stepping
  through stands, with a web search for doubtful operators) and the
  `auto_filled` watermark in `earth.wed.xml` are described in §6.7f; none of
  them adds anything to apt.dat or the index.
- **Structure.** §6.7b-6.8 moved from after §9 into §6; §8's subsections are in
  order (8.6 no longer sits between 8.3 and 8.4); §2 has 2.1-2.4; the appendix
  is a status table. §7's evidence is kept as recorded, with dated draft-8
  notes; §8's rationale is unchanged.
- **44 `Obsolete` marks, not 12** (§6.6). The twelve superseded-asset marks are
  joined by twenty retired airframes, `UAL:B744` among them, and by the twelve
  liveries of eleven operators that no longer fly at all. The superseded marks
  alone still empty nothing; together the 44 empty 25 (airline, class) pairs,
  each a deliberate withdrawal.
- **The worked example is recomputed** against the schema 4 index
  (`20260925-r1`) with R25 and R26 applied. Range and the new marks change seven
  of its nine stands, which is why §2.3's table differs from draft 7's.

### What changed since draft 6

**The format is now one row, not two.** `1312` — per-stand `+`/`-` refinements
naming an airline and an aircraft type — is deleted, with R21, R22, its grammar
productions and eight conformance vectors. §8.6 records the measurement: only
**8.9%** of real (operator, class) pairs in the shipped index carry more than one
aircraft type, and **90.5%** of operators have assets at exactly one class, so
the size control every stand already has was pinning the aircraft anyway. What
refinement was really being used for was working around retired aircraft, one
stand at a time, worldwide.

- **"Only this operator" needs no format support.** A one-element airline list on
  `1301` has always meant it. It becomes a UI assertion in WED — a lock on the
  card, greying out the rest — and costs a reader nothing.
- **`Obsolete` is new, and is where refinement's real work moves to** (R25,
  §6.6). A marked index row never enters the spawn pool while its `.obj` stays on
  disk for hard-path scenery. This is also what §4.4 needed and could not do: the
  standing recommendation there was to delete superseded exports from
  `library.txt`, which would have broken the legacy airports it was trying to
  leave alone.
- **R24 is new**: a duplicate `1313` is resolved by taking the first in file
  order, never by adjudicating which was meant. §9 records what that costs.
- **§4.5 gains the statistic-field sentences**, one per scenario, with the
  percentage defined as occupancy rather than as "the chance of this operator" —
  which under a single-operator stand is always 100% and says nothing.
- **§8.3 gains the symmetric property**: the design can now withdraw a livery
  globally, not only add one.

### What changed since draft 5

- **§4.5 gains a `P(empty)` readout**, which is the substantive addition. The
  17.2% empty-stand figure previously had one answer — a hard export error — and
  that answer arrives only at the end, cannot distinguish an intentional empty
  stand from a mistaken one, and has no eyes on it during the bulk fill that is
  the only thing which would ever produce those stands. A continuous readout, a
  delta shown at the moment of the edit, and an airport-level rollup after a fill
  address all three. It also made R21/R22's open-versus-closed sets visible
  rather than merely specified — moot now that draft 7 has deleted both, but the
  readout outlived them.
- **§9 gains a fourth open question**: duplicate `1312`/`1313` on one stand is
  currently a silent overwrite, where `1301` rejects a repeat outright.
- **Conformance vectors corrected.** Eight vectors in §5 — V1, V2, V8–V12, V15 —
  still carried the placeholder field of the grouped design dropped in draft 5,
  which made them malformed under draft 5's own grammar. The §4.2 example and the
  §2 weight production had the same residue. §5 is the section handed to an
  implementer as tests; it is now consistent with §2.
- **The appendix no longer overstates the WED side.** It claimed WED reads the
  livery index; the class has no consumer. Corrected, and split into what runs
  and what has only been read through.

### What changed since draft 4

- **Index count corrected: 298, not 376.** 376 is the 12.4.4-pnl5 *beta* asset
  count. The shipped index describes **12.4.3-r2**, which has 298. Draft 4 cited
  both and was internally inconsistent. See §6.4 — this mismatch fails silently
  and is the most likely way to waste a day on this feature.
- **§6.3 is new, and it constrains stage 3**: `(type, airline, note)` is **not** a
  unique key in the index and cannot be made into one. Adding registration
  narrows it but does not close it. The only unique key is the object path. A
  reader that de-duplicates on anything else silently drops liveries; a reader
  that does not de-duplicate at all gives duplicated assets double spawn
  probability. Both are wrong, in opposite directions; §4.4 says which to prefer
  and why.
- Normative rules, grammar, reader pseudocode and conformance vectors (§1–§5,
  §6.5) are new. Draft 4 described the format in prose only, which is not enough
  to implement against.

---

## 1. Normative rules

### Format-level

- **R1** — `1301` MUST NOT be modified, reinterpreted or deprecated. Every row
  defined here is an additive refinement of the stand `1301` already describes.
- **R2** — A reader that discards every `1313` row MUST produce exactly
  today's behaviour. This is the floor, and it MUST be unreachable from any input.
- **R3** — **The airline list on `1301` is the only one.** It is not repeated
  anywhere, so it cannot disagree with anything. Selection reads it directly.
- **R23** — **`1301`'s size letter is derived from `1313`, not the other way round.**
  A writer MUST set it to the **highest class carrying a non-zero weight**. WED
  treats a mismatch as auto-fixable rather than as an author decision: `1301 A`
  next to `1313 0 0 0 0 9 1` is corrected to `F`.

  The letter is a **slave field, and carries no intent**. When `1313` is present,
  nothing consults the letter to choose a class — the weights are the whole truth
  (R5). It survives for two consumers that cannot read `1313`: an old sim, which
  step-downs from it and so should start from the largest class the author allows,
  and WED's map view, which sizes the stand icon from it.

  This is the one place where a value in `1301` follows the new rows instead of
  leading them, and it is safe precisely because no reader uses it for selection
  once `1313` exists.
- **R4** — A malformed or unparseable `1313` row MUST be
  discarded, and MUST NOT fail the file, the airport, or the stand. **This is the
  opposite of the rest of `AptIO.cpp`**, where a bad row sets `ok = "Illegal …"`
  and aborts the whole load (`AptIO.cpp:471`, `:727`). These rows are
  hand-editable, and one bad weight vector must not make an apt.dat
  unopenable.
- **R5** — A `1313` row that fails to parse MUST be discarded **whole**, falling
  back to "no `1313` row present". It MUST NOT be partially applied. All-zero is
  a legal and meaningful value (§4.2), so a truncated row parsed as zeros would
  silently empty the stand — the one place where soft-fail must be explicit about
  what it falls back *to*.
- **R20** — **`1313` attaches to the most recent `1300`, and MUST follow it.**
  It binds by position and nothing else — there is no name to bind to, which is
  the whole point of the inline-only shape. A `1313` appearing before any `1300`
  in the block has no stand to attach to: discard it per R4.

  Writers emit `1300`, then `1301`, then `1313`, which is what every example here
  does. `1301` already had this constraint: `AptIO.cpp:754-756` rejects it
  outright if there is no gate yet, or if the gate already has airlines.

- **R24** — **Ambiguous input is never adjudicated.** Where a stand carries more
  than one `1313`, the **first in file order** wins; the rest are discarded
  whole, not merged, not overwritten, and not compared for plausibility.

  This is not a tolerance policy, it is a division of labour. Two conflicting
  answers in one file means only the person who wrote it knows which was meant,
  and a reader that quietly picks the "better" one has converted an author's
  mistake into nobody's mistake. Taking the first is arbitrary on purpose:
  arbitrary and stated beats clever and silent.

  Note this is *more* lenient than `1301`, which rejects a repeat outright
  (`AptIO.cpp:756`). R4 forbids that here — a duplicate must not fail the file —
  so first-wins is the only rule that satisfies both. WED records the discarded
  row and shows it to the author (§9).

### Writer-side

Constraints on whoever produces the file, not defences the reader must implement.
A reader that has to defend against all of these is a reader nobody implements
correctly.

- **R9** — No whitespace other than a single U+0020 SPACE inside any field.
- **R10** — **An airline code matches `[a-z0-9]{3,4}(_[a-z0-9]{1,6})?`, and is
  written lower case in `1301`.** Three or four letters or digits, optionally
  followed by `_` and one to six more. The base is an ICAO designator or an index
  code (`dal`, `xpa0`); the suffix names a division flying on that designator
  (`afr_f`, `ryr_1`) or, for the generic airliners, the type (`xpzz_b752`).
  WED folds the string to lower case (`CorrectAirlinesString`), then checks each
  code against this shape (`WED_RampPosition::IsValidAirlineCode`); a code
  outside it is a validation error (`err_ramp_airlines_malformed_code`), not
  silently dropped. The index writes codes upper case; a reader compares them
  without regard to case.

  **Known constraint: Gateway caps the `1301` airline string below 100
  characters** (`err_ramp_airlines_too_long`, Gateway target only) — about 24
  three-letter codes. WED's auto-fill stops short of it, so a busy hub cannot list
  every airline that serves it. Whether the cap can be raised is a question for
  the Gateway team, not for this format.
- **R11** — Weights are non-negative integers in `0..1000`.
- **R14** — A writer SHOULD NOT emit weights (or a size range) pointing
  exclusively at classes that no listed airline can fill. WED reports it as a
  validation **warning** (`warn_ramp_livery_parks_nothing`), visible from
  Validate without exporting and never blocking an export: the airline list
  still drives ATC and AI parking, so a stand with no static livery is not
  wrong, only empty of static aircraft (§4.5).

  The check applies only where the airline list chooses the static aircraft:
  **Airline and Cargo stands**. None parks nothing by definition (R29); GA and
  military are drawn from the library by size. **All-zero weights are exempt** —
  they are the author saying nothing parks here (V2). "Can fill" is the same
  `eligible()` the sim uses (§4.1), range (R26) included, and WED adds the stand's
  equipment type, so the warning and the Liveries tab never disagree. Without an
  index (an X-Plane before 12.5) there is nothing to check against and the
  warning stays quiet.

  **Two situations look identical here and must not be treated alike**, which the
  "Emirates A380 gate" case makes concrete: the library ships **no class-F livery
  at all** today, so an author building that gate writes a weight nothing can
  currently satisfy.

  | the class is unfillable because… | what the author is told |
  |---|---|
  | the listed airlines have liveries at other classes, or out of range, but none usable here | **the weights are wrong** for these operators, and one click fixes them |
  | **no asset exists at that class anywhere in the library** | **the author is ahead of the art**, and late binding is the whole design |

  Both are warnings; the wording differs. Treating the second as a mistake would
  make it impossible to author for an aircraft that is coming, which is exactly
  the capability §8.3 argues the format exists to preserve.
- **R19** - **apt.dat has no comment syntax. A writer MUST NOT emit comment lines.**
  There is no `#` form, no `//` form, and nothing else. Confirmed against the
  shipped data: **zero** lines begin with `#` in the 12,351,496-line Global
  Airports apt.dat.

  The mechanism is worth knowing, because it is not "the reader rejects `#`" - it
  is quieter than that. `TextScanner_FormatScan(s, "i", &rec_code)` tokenizes the
  line and converts the first token with `atoi()` (`MemFileUtils.cpp:769-800`).
  `atoi("#")` returns **0**, and the function still returns 1 because it counted a
  token. So the caller's `if (FormatScan(...) != 1) { skip }` guard
  (`AptIO.cpp:359`) never fires, and the line is processed as **record code 0**.

  What happens next depends on the WED. **WED 2.7.x and earlier** fall through
  to `ok = "Illegal unknown record"`, which fails the entire file. **WED 2.8**
  treats record code 0 like any unknown row (R15): the line is skipped, recorded
  on its airport and shown to the author (`AptIO.cpp:1322`). Either way the
  comment is gone, so the rule stands.

  **Blank lines are safe**, by the same code read the other way: a blank line
  yields zero tokens, `FormatScan` returns 0, and the guard does skip it. A writer
  may use blank lines freely to separate sections, and every example here does.

  Stated as a rule because we got it wrong ourselves. The first sample package was
  commented for readability; WED refused it at the first comment line, four lines
  before it reached a row this proposal defines. X-Plane loaded the same file
  without complaint, which is consistent with section 7.2 and is exactly why the
  mistake survived a successful sim test. Anything explanatory belongs in a
  companion file, which is where the sample's now lives.

### Reader-side

- **R15** — Unknown row codes, and unknown extra fields within a known row, MUST be
  ignored rather than rejected. X-Plane has always done this (§7.2). WED does it
  from 2.8: the row is skipped, recorded per airport, listed after File > Import
  and raised as the validation warning `warn_apt_dat_rows_not_imported`.
- **R17** — Three-stage selection (§4) applies **only** to stands carrying a `1313`
  row. A stand without one MUST keep today's behaviour, unchanged.
- **R18** — **Stage 2 and stage 3 MUST ask the index the same question.** An
  airline with no usable livery in the class just drawn is not a stage-2
  candidate at all. "Usable" means every filter at once: the class, the
  operation class of the stand, `Obsolete` (R25), range (R26) and `HOME` (R27).

  Filtering stage 2 on "has any livery at all" while filtering stage 3 on "has
  one usable in this class" makes an airline a candidate with zero options, so
  every time it is drawn the stand parks nothing. Concretely: Emirates' only
  E-class aircraft is the 777, so at a stand weighted for class F they must be
  absent from the pool, not drawn and then found empty. The same holds when the
  filter that empties the set is range: United at Beijing with only 737s in the
  drawn class is not drawn at all. Write it as one `eligible()` used by both
  stages and the two cannot drift apart — see §4.1.
- **R25** — **An index row whose NOTE is `Obsolete` MUST NOT enter the spawn
  pool.** Not at stage 2, not at stage 3, not in any count of what an airline can
  fill. It is as if the row were absent.

  This is where type-level exclusion went. The apt.dat format no longer has a way
  to say "this airline, but not that aircraft" (§8.6), because the thing authors
  actually needed it for was a retired or superseded asset — a fact about the
  library, true at every airport at once, not a per-stand decision. §6.6 defines
  the mark and §4.4 explains what it fixes.

  **This rule is normative for the sim, not a WED convention.** If only WED
  honours it, an author sees no preview for an obsolete livery while X-Plane goes
  on spawning it, which is worse than either behaviour alone. It is one string
  comparison at index-load time.
- **R26** — **Range.** A livery row is not a candidate at a stand when the
  great-circle distance from the stand (its `1300` position) to the operator's
  nearest placed hub exceeds the row's `RANGE_KM`. Military and Gov operators
  are exempt, and so is GA (R28). An empty or zero `RANGE_KM`, or an operator
  with no hub that could be placed, is **never filtered**. Full text §6.7; hubs
  §6.7b.
- **R27** — **Home only.** A row whose `SCOPE` is `HOME` is a candidate only at
  an airport in the operator's own country: the country on its OPERATOR record,
  else the row's `REG CTY`. If either country is unknown the row stays a
  candidate. Full text §6.7d.
- **R28** — **General aviation.** At a GA stand the sim draws a GA livery of the
  drawn class registered in the airport's country with probability 0.7 when one
  exists, otherwise any GA livery of that class. No range rule and no airline
  list. Full text §6.7e.
- **R29** — **Operation type `none` means no static aircraft.** A stand whose
  `1301` operation type is `none` MUST be read exactly as six zero weights:
  nothing parks, whatever the `1301` letter, airline list or `1313` row say. The
  airline list, if any, still drives ATC and AI parking.

  In WED this is the Liveries tab's "None" operation type. The tab relabels the
  other types — Private/BizJet for General Aviation, Passenger for Airline,
  Military/Gov for Military — but `earth.wed.xml` stores them under their
  original names, so a 2.7 WED still reads the document and nothing in apt.dat
  changes.

---

## 2. Grammar

### 2.1 The row

```abnf
; ---- the new row -------------------------------------------------------
weights-row    = %s"1313" 6(1*SP weight) *SP

; ---- the airline code on 1301 (R10) - existing row, grammar now exact ----
airline-code   = 3*4(lc-alnum) [ "_" 1*6(lc-alnum) ]
lc-alnum       = %x61-7A / DIGIT     ; a-z, 0-9: lower case only

; ---- lexical -----------------------------------------------------------
weight         = 1*4DIGIT            ; value MUST be 0..1000 (R11); a
                                     ;   syntactically valid 1001..9999 is
                                     ;   out of range - drop the row (R5, V11)
SP             = %x20
```

That is the entire addition to apt.dat: **one row code, six integers.** There is
no second row, no sigil, no aircraft-type field and no name. Everything else this
document describes is either an existing row (`1300`, `1301`), a rule about how to
read these six numbers, or a companion data file that is not apt.dat at all.

Parser notes:

- `1313` attaches to the most recent `1300` and carries no name, no reference and
  no shared state. **Everything about a stand is written at the stand** (see
  §8.2 for the measurement that settled this).
- It takes **exactly six** weights. Five or seven is malformed; discard whole
  per R5. Do not pad and do not truncate.
- A second `1313` on the same stand is not a merge and not an override: the first
  wins and the rest are discarded (R24).
- The code number is provisional (§9). In WED it is one constant,
  `apt_startup_loc_weights` in `AptDefs.h`.

### 2.2 Rows in context

```
1300 51.157304 -0.171800 347.5 gate heavy|jets Gate 42
1301 E airline ual dal baw aal afr klm dlh
1313 0 0 3 0 1 0

1300 51.147042 -0.174540 -128.8 gate heavy Cargo 1
1301 E cargo fdx ups
1313 0 0 0 0 1 0
```

Two stands, nothing shared, nothing to resolve. A stand that wants none of this
writes neither row and keeps today's behaviour.

### 2.3 A complete worked example - one airport, every feature

Everything above in one airport block. **This is the reference scenario**: if an
implementation reproduces the resolution table below, it has all of §1-§4 right.

`KXYZ` is fictional and placed at Seattle, the airlines are real, and every
outcome in the table was computed against the shipped index (`20260925-r1`,
schema 4, X-Plane 12.4.3-r2 assets) with R25 and R26 applied and hubs placed from
that install's Global Airports - not asserted. Draft 7 computed it without the
range rule and before the retired-airframe marks of §6.6; seven of its nine
stands changed.

```
1    433 0 0 KXYZ Example Intl
1302 icao_code KXYZ
1302 country USA
100 45.00 26 0 0.25 0 2 1 16L 47.46300000 -122.30800000 0 0 2 0 0 0 34R 47.43100000 -122.30800000 0 0 2 0 0 0

1300 47.44310000 -122.30120000 090.0 gate heavy|jets A1
1301 E airline dal ual aal baw uae
1313 0 0 0 3 7 0

1300 47.44240000 -122.30120000 090.0 gate heavy|jets A2
1301 E airline dal ual aal baw uae
1313 0 0 0 3 7 0

1300 47.44170000 -122.30120000 090.0 gate jets B1
1301 C airline dal ual aal afr dlh klm swa aca
1313 0 0 10 0 0 0

1300 47.44100000 -122.30120000 090.0 gate jets B2
1301 C airline dal ual aal afr dlh klm swa aca
1313 0 2 8 0 0 0

1300 47.44030000 -122.30120000 090.0 gate turboprops|jets R1
1301 B airline dal afr
1313 0 10 0 0 0 0

1300 47.43960000 -122.30120000 090.0 gate heavy|jets CGO1
1301 D cargo fdx ups
1313 0 0 0 10 0 0

1300 47.43890000 -122.30120000 090.0 tie_down props GA1
1301 A general_aviation

1300 47.43820000 -122.30120000 090.0 gate jets F1
1301 F airline uae
1313 0 0 0 0 0 10

1300 47.43750000 -122.30120000 090.0 gate jets T1
1301 C airline cca ana jal sia qfa
1313 0 0 9 1 0 0
99
```

#### What each stand demonstrates

| stand | feature |
|---|---|
| `A1`, `A2` | two stands with **identical** weights, written out at each. No name, no reference, no resolution step |
| `A1`, `A2` | operators on `1301` with nothing at one of the weighted classes — the R18 case, three of them made so by `Obsolete` marks |
| `B1` | a single-class weight: everything here is class C. Three European operators drop out on range (R26) |
| `B2` | same operators as `B1`, **different weights** — the edit that used to mean "leave the group" is now just a different number |
| `R1` | weights concentrated on one class, a different one — and **nothing can fill it**: Delta's `CRJ2` is retired, Air France's `CRJ1` is retired *and* out of range. The R14 "the weights are wrong" case |
| `CGO1` | weights plus a cargo op type |
| `GA1` | **no new row at all** — today's behaviour, untouched (R17) |
| `F1` | a class the shipped library cannot fill — the R14 "ahead of the art" case |
| `T1` | weights spanning two classes, and **range emptying a whole class**: every operator is listed, none of their class-C aircraft can reach |

#### What actually spawns

Computed, not asserted:

| stand | class draw | resolves to |
|---|---|---|
| `A1`, `A2` | D 30% | 2 airlines (DAL, UAL) — `B752`, `B763` |
| | E 70% | 2 airlines (AAL, BAW) — `B772`. DAL's `B772`, UAL's `B744` and UAE's `B772` are marked `Obsolete` (R25) |
| `B1` | C 100% | 5 airlines (DAL, UAL, AAL, SWA, ACA) — `A320`, `B738`. AFR, DLH, KLM are 7,800-8,200 km from their nearest hub with 5,700-6,100 km aircraft (R26); Delta's `MD82`s are marked |
| `B2` | B 20% | **nothing** — no listed operator has a current class-B livery that reaches |
| | C 80% | as `B1` |
| `R1` | B 100% | **nothing, always.** Both operators' only class-B aircraft are marked `Obsolete`; AFR's would also be 8,050 km from Paris with 3,000 km of range |
| `CGO1` | D 100% | 2 airlines — `B752`, `B763` (FedEx's `DC10` is marked) |
| `GA1` | — | today's step-down from `A` |
| `F1` | F 100% | nothing **today** — the library ships no F-class livery yet. The day an Emirates A380 ships, this stand starts working with no apt.dat edit |
| `T1` | C 90% | **nothing.** All five operators have only `A320`/`B738` at C, and the nearest hub is 7,657 km away (Tokyo) |
| | D 10% | 2 airlines (ANA, JAL) — `B763` |

`F1` is the one intentional empty, and it is the difference between the two kinds
of emptiness §4.5 cares about: the author asked for something real that does not
exist yet, and late binding will fill it in. `R1` is the other kind — a weight
pointing at a class **none of the listed airlines can fill** — and R14 warns
about it, with "the weights are wrong" rather than "ahead of the art". `B2` is
the same defect diluted: 20% of the time.

`T1` is the range rule at its bluntest: a stand that parks nothing 90% of the
time, on a list that looks entirely reasonable. It is correct - no 737 in the
library can reach Seattle from Asia - and it is exactly what the P(empty)
readout of §4.5 exists to put in front of the author. R14 does not fire here,
because class D can still be filled.

#### The number that shows why R18 exists

`A1` lists five operators and is weighted 30/70 across D and E. United's only
class-E livery in the shipped library is the `B744` — an aircraft they retired in
2017, and marked `Obsolete` in the index (§6.6). Delta's `B772` is marked too,
and so is Emirates' only livery of any kind. So United and Delta have **nothing
at E**, American and BA **nothing at D**, and Emirates nothing anywhere.

| | class D candidates | class E candidates | stand parks nothing |
|---|---|---|---|
| **With R18** — stage 2 asks "has one usable at *this* class" | **2** (DAL, UAL) | **2** (AAL, BAW) | **0%** |
| Without — stage 2 asks "has a usable livery at all" | 4, two of them empty here | 4, two of them empty here | **50%** |

Half the time, on a stand whose author did nothing wrong, from three withdrawn
assets. Note United and Delta still park at D — withdrawing one aircraft does
not withdraw the operator, which is exactly the surgical effect earlier drafts
were trying to buy with a per-stand exclusion row.

The same arithmetic applies to range. At `B1`, a stage 2 that ignored R26 would
draw AFR, DLH or KLM three times in eight and park nothing each time: 37.5%
empty, instead of 0%.

#### Things this example is careful about

- **Every stand stands alone.** `A1` and `A2` repeat two identical rows and that is
  fine: it costs about 40 bytes, and §8.2 measures the alternative as both larger
  and more complex.
- **A withdrawn aircraft leaves something behind — usually.** Delta's `MD82`
  marks are harmless because they also have `A320` and `B738` at C. United's
  `B744` empties their class E outright, and only R18 keeps that from turning into
  an intermittently empty stand. Delta's `CRJ2` empties their class B, which is
  what makes `R1` unfillable. §6.6's blast-radius check exists to tell those
  cases apart before a mark is made, not after.
- **`GA1` carries no new row.** An airport does not have to adopt this format
  stand-by-stand, and a mixed file is normal.
- **Class A is GA and military only** — no real airline has an A-class livery,
  which is why `GA1` is `general_aviation` rather than an airline stand.
- **No comments.** apt.dat has no comment syntax (R19); the annotation lives here.
- **The range outcomes depend on the install.** Hubs are placed from the reader's
  own Global Airports (§6.7b), so a different install could move a hub by a few
  hundred metres. Nothing here is within a few hundred kilometres of a range
  limit.

### 2.4 One stand, line by line

Stand `A1` from §2.3, pulled apart. Three lines, and two of them already exist.

```
1300 47.44310000 -122.30120000 090.0 gate heavy|jets A1
1301 E airline dal ual aal baw uae
1313 0 0 0 3 7 0
```

That is the whole stand. There is nothing elsewhere in the file to look up.

#### The two rows that already exist, and are not touched

```
1300  47.44310000  -122.30120000  090.0  gate  heavy|jets  A1
 |    |            |              |      |     |           |
 |    latitude     longitude      |      |     |           name (rest of line)
 |                                |      |     equipment bitfield
 row code                         |      ramp type: gate / hangar / misc / tie_down
                                  heading, degrees true
```

```
1301  E  airline  dal ual aal baw uae
 |    |  |        |
 |    |  |        airline codes, lower case, space separated (R10)
 |    |  ramp operation type: none / airline / cargo / general_aviation / military
 |    ICAO wingspan class A-F. Today this is the ONLY size control, and the sim
 |    step-downs from it: ~75% E, then 75% of the rest D, and so on.
 row code
```

**Everything below is additive. Delete the one new row and this stand behaves
exactly as it does today** (R2). That is the property the whole design is built on.

#### The one row added

```
1313  0  0  0  3  7  0
      A  B  C  D  E  F
               |  |
               |  70% of arrivals are E-class
               30% are D-class

      Always six integers, in class order, no exceptions (R5).
      Relative, not percentages - "3 and 7" is the same as "30 and 70".
      Integers only: a decimal point would be at the mercy of the user's
      locale (see 4.2).
```

#### What the sim does with it, in order

```
0. op type 'airline' is not none (R29), and the stand has a 1313 (R17)
1. class   := weighted draw over 0 0 0 3 7 0        -> D 30%, E 70%
2. airline := uniform over those in 1301 that have a USABLE livery in the
              class drawn: a Pax operator, not Obsolete, within range of
              one of its hubs (R18, R25, R26)
                 at E: aal, baw                      <- dal, ual, uae: marked
                 at D: dal, ual                      <- dal and ual ARE here
3. livery  := uniform over that airline's usable liveries in that class,
              honouring EXPORT_RATIO if library.txt sets one
```

Step 2 is where R18 earns its keep. United's only class-E aircraft in the shipped
library is the 747-400, an aircraft they retired in 2017 — so the library, not
this file, is what stops it spawning: it is marked `Obsolete` (§6.6). United
simply is not a class-E candidate anywhere, while remaining one at D where they
have the 757 and 767. Delta's 777 is marked for the same reason.

Had stage 2 skipped that check and asked only "does this airline have *any*
usable livery", United and Delta would still be drawn at E, and American and BA
at D, and the stand would park **nothing half the time** (§2.3).

#### What this looks like in a Gateway diff

1. **A stand is a contiguous run of lines.** Reviewing what parks somewhere means
   reading two or three consecutive rows, never resolving a name against a
   definition elsewhere in the block.
2. **An edit to one stand touches only that stand.** There is no shared object to
   change underneath other stands, and no name that changes when content changes.
3. **`1301` still reads correctly on its own**, so a reviewer who ignores the new
   row entirely still sees which airlines an author assigned.

## 3. Reader algorithm

One pass. There is nothing to resolve, because nothing refers to anything.

```
for each row in the airport block:
    1300 …          -> begin a new stand; it becomes "current"
    1301 …          -> size letter, op type, airline list  (existing behaviour)
    1313 w×6        -> if current stand already has weights: DISCARD this row (R24)
                       else if no current stand:             DISCARD this row (R20)
                       else current_stand.weights = [w×6]

then, per stand:
    if weights present and length != 6:      drop weights entirely   (R5)
    if any weight outside 0..1000:           drop weights entirely   (R5)
```

**A bad `1313` is dropped whole, never partially** (R5). The row means "here is
the whole distribution", so discarding one element silently changes what the
other five mean — and because all-zero is legal and means "nothing parks here"
(§4.2), a truncated row read as zeros would empty the stand without saying so.
There is no partial-acceptance path to get wrong, because there is nothing left
in this format that could be partially accepted.

A stand with no `1313` is not a special case to detect. It simply has no weights,
and R17 keeps it on today's behaviour.

Every DISCARD above, and every row with a code the reader does not know (R15),
is dropped without failing anything. WED 2.8 additionally records each one on
its airport (`AptInfo_t::discarded_rows`, as `line N: <row> (why)`) so it can
tell the author; a sim reader has no need to.

## 4. Selection algorithm

**This is the behavioural change.** Everything above is transport.

### 4.1 The three stages

```
if stand.op_type is none:
    nothing parks here, stop                                # R29

if stand has no 1313 row:
    use today's behaviour, unchanged                        # R17

class := weighted_choice(A..F, weights = stand.weights)
         if every weight is zero -> nothing parks here, stop        # §4.2

if stand.op_type is general_aviation:
    livery := ga_choice(class, airport)                     # R28, §6.7e
    stop

if stand.op_type is military and stand.airlines is empty:
    livery := uniform_choice([ L at class, not obsolete, operator Military
                               or Gov, home_ok(L, op, airport.country) ])
    stop                                                    # by size, as today

airlines_in_class := [ a for a in stand.airlines            # from 1301
                         if eligible(a, class) is non-empty ]
if airlines_in_class is empty -> nothing parks here, stop
airline := uniform_choice(airlines_in_class)

liveries := eligible(airline, class)
livery   := weighted_choice(liveries, weights = EXPORT_RATIO or uniform)

where
  eligible(a, class):                  # ONE function, called by both stages (R18)
      op := index.operator(a)          # the OPERATOR record (§6.7b)
      if not op_class_matches(op, stand.op_type): return []
      return [ L for L in index.liveries(a, class)
                 if not L.obsolete                           # R25
                 and in_range(L, op, stand.position)         # R26
                 and home_ok(L, op, airport.country) ]       # R27

  op_class_matches(op, op_type):
      airline  -> op.class is Pax            (and XPZZ_<TYPE>)
      cargo    -> op.class is Cargo          (and XPZZ_<TYPE>)
      military -> op.class is Military or Gov

  in_range(L, op, p):                  # §6.7
      if op.class is Military or Gov:                  return true
      if L.range_km is empty or op has no placed hub:  return true
      return min over hubs h of greatcircle(h, p) <= L.range_km

  home_ok(L, op, country):             # §6.7d
      if L.scope is not HOME:                          return true
      home := op.country, else L.reg_country
      if home or country is unknown:                   return true
      return home == country

  ga_choice(class, airport):           # §6.7e
      pool := GA liveries at class, not obsolete
      home := those in pool whose REG CTY is airport.country
      if home is non-empty and random() < 0.7: return uniform_choice(home)
      return uniform_choice(pool)      # empty pool -> nothing parks
```

There is no per-stand filter to apply, because the format no longer carries
one: a stand says which airlines and which classes, and the index says what
exists and where it can go. Every subtraction — `Obsolete`, range, home — lives
in the index because each is a fact about the library or the operator rather
than about this stand (§8.6, §8.7).

If a stage has no candidates, nothing parks at that stand this time. That is a
correct outcome, not an error — see §4.5.

**Both stages MUST call the same `eligible()` (R18), and it MUST carry every
filter.** Filtering stage 2 on "has any livery at all" while filtering stage 3
on "has one usable in this class" makes an airline a candidate with zero
options, so every time it is drawn the stand parks nothing. Concretely, in the
sample package: BAW's only C-class livery is the A320, so at a stand weighted
for class D they must be absent from the stage-2 pool, not drawn there and found
empty. Range is the same case in another guise: United at a class-C stand in
Beijing has two 737s and an A320, none of which reach, so United is not a
candidate there at all — not drawn and then found empty.

Draft 7 got this wrong for range. It put the range filter in stage 3 and said
an emptied operator left "the stand empty for that draw", which is exactly the
failure R18 forbids. Writing it once and calling it twice makes the two
impossible to drift apart. Drafts 1–4 had the flattened form and the same bug;
it was found by building the sample in `docs/livery_sample/` and tracing one
stand by hand. The rule outlived the refinements it was originally written for,
because it was never really about them — it is about the index not having what
the author assumed.

### 4.2 Weights

Always six values, A through F, in that order — not a list of the classes the
author happened to mention.

```
1313 0 1 1 8 0 0
     A B C D E F     ->  D 80%, B and C 10% each, nothing else
```

| state | meaning |
|---|---|
| no `1313` row | today's behaviour (R17) |
| a `1313` row | **the whole truth** — six numbers, nothing implied |
| all six zero | legal; nothing spawns here — the same thing `ramp_operation_none` says today |
| operation type `none` | nothing spawns, whatever the row says — read exactly as six zeros (R29) |

Writing all six rather than only the non-zero ones removes the "is an absent
class zero, or unspecified?" ambiguity entirely, gives the row a fixed shape that
is cheap to validate and hard to tamper with, and costs 12 characters — about
175 KB across every weighted stand in the global apt.dat.

**Relative integers, never decimals.** `0 1 1 8 0 0` is the same distribution as
`0 0.1 0.1 0.8 0 0`, and the editor shows the author percentages either way — but
the file MUST NOT contain a decimal point. WED calls `setlocale(LC_ALL, "C")`
only inside `#if LIN` (`WED_AppMain.cpp:264`), and that one call is all that
stands between FLTK and a comma decimal separator in everything `sscanf` reads
and every `%f` written. Integers remove the hazard rather than depending on that
call. They also need no sum-to-one validation, and adding or removing a class
does not force the other five to be recomputed.

### 4.3 Why the order is the whole point

Flattening this into one weighted draw over every eligible livery lets the **size
of the asset library decide the outcome**. B738 ships 120 liveries and A359 ships
42; an author who sets two classes to equal probability still gets overwhelmingly
737s. Intent is silently overridden by how much art happens to exist.

The same bias repeats one level down, which is why airline is its own stage: in
class C with `ual` (3 liveries) and `baw` (1), drawing uniformly over liveries
gives United 75%. Listing two airlines means "both park here", not "United three
times as often".

**Each stage is decided by whoever should decide it.** Class is the author's, via
`1313`. Airline is the author's, via `1301`. Only the last stage — which of
United's 737s — belongs to the library, and that is exactly where `EXPORT_RATIO`
already expresses Laminar's own "this fictional livery is rare".

It also leaves room to weight airlines later - `1301` already carries the list,
so a future row could carry per-airline weights alongside it - without
restructuring anything here.

### 4.4 Stage 3 and duplicate assets

Stage 3 draws uniformly (or by `EXPORT_RATIO`) over index rows matching
(airline, class). **The index contains duplicate assets** — 38 cases where an old
and a new object for the same real aircraft are both exported (§6.3, §8.4). Under
a uniform draw, each of those aircraft gets **double the probability** of its
neighbours.

Two wrong fixes, stated so they are not tried:

- De-duplicating on `(type, airline, note)` **drops real liveries**. That tuple is
  not unique for legitimate reasons — Air China ships five Peony tail numbers
  differing only by registration (§6.3).
- De-duplicating on the object path changes nothing. Paths are unique by
  construction, including for the duplicate pairs.

**Recommendation: do not de-duplicate in the sim. Mark the superseded asset
`Obsolete` in the index (§6.6) and let R25 drop it.**

This replaces the recommendation earlier drafts carried, which was to delete the
superseded `EXPORT_EXTEND` lines from `library.txt`. That fix was never performed
and could not have been: an asset removed from the library stops resolving, and
scenery that references it by a hard path stops loading. The whole problem is
that these objects must remain reachable while ceasing to be *chosen*, and
nothing in `library.txt` can express that distinction. A NOTE value can.

The twelve pairs are identified and marked — `AT45_FDX_static.obj` superseded by
`ATR42-500_FedEx.obj`, and eleven more of the same shape, every one with its
replacement sitting beside it. **These twelve marks alone empty zero (airline,
class) pairs**, so the skew disappears and nothing becomes unfillable. (The
index now carries 44 marks in all; the other thirty-two are retired airframes and
defunct operators, which empty pairs on purpose — §6.6.)

The thirteenth pair is not one: `F15EX_cft` / `F15EX` are genuinely different
airframes (conformal tanks) that the index cannot currently tell apart. That is
an index fix by hand on the NOTE column, not an obsolescence mark — see §6.3.

**The danger of this mechanism is its reach.** A mark is global and has no
per-stand undo: the livery stops existing at every airport at once, and an
(airline, class) pair left with no asset is a stand that silently parks nothing —
the §4.5 defect, created by a data edit. Marking `B752` would empty
**twenty-two** pairs, and the 757 is in daily service. The blast radius of every mark is
therefore printed on every generator run, and `--what-if` answers it without the
mark being made first (§6.6). Read that number before committing a row.

### 4.5 What class-first costs, and who absorbs it

Class-first is strictly more expressive, but it gives up the one virtue the
step-down had: it always found *something*. Four consequences.

**1. A stand can now be silently empty, and this is common.** Measured against the
real global apt.dat and the real shipped livery set: if every airline and cargo
stand's weights were set to the class it already declares, **12,946 of 43,412
stands (29.8%) would spawn nothing at all**, touching **2,613 of 3,924 airports
(67%)**. The cause is almost never "this airline has no models" — 1.9% of stands
are that — nor range (0.8%). It is "this airline has no model **in this stand's
class**" (27.1%). A gate marked E whose airlines fly C-class aircraft is the common
case, and today the step-down hides it by quietly substituting something smaller.

| rules applied | empty stands | airports |
|---|---|---|
| class and op class only | 7,138 (16.4%) | 1,643 (42%) |
| + range (R26) | 8,742 (20.1%) | 1,980 (50%) |
| + range and `Obsolete` (R25) — **today** | **12,946 (29.8%)** | **2,613 (67%)** |

Draft 7 measured 7,604 of 44,242 (17.2%) over every stand carrying airlines, GA
and military included; the same method on today's data gives 7,688 of 44,242.
**The rise is the `Obsolete` marks, and two of them carry most of it**: Delta's
CRJ-200 (`DAL` class B) is the only aircraft that can fill 1,915 stands at 487
airports, and Air France's CRJ-100 (`AFR` B) 1,300 stands at 360 - together 3,215
of the 4,204 stands the marks empty. Those stands list an operator that really
did fly a class-B jet there, and no longer does; the library has nothing current
to put in its place. Retired airframes account for 95% of the marks' effect,
defunct operators for 1%. Reproduce with
`tools/scripts/airline_research/measure_empty_stands.py` (`--by-mark` for the
per-pair split); it reads Global Airports in about four seconds.

> **WED absorbs this.** Weights pointing at a class none of the listed airlines
> can fill raise a validation warning (R14), visible in Validate at any time and
> never blocking an export, **and, before that, a continuous readout of P(empty)
> while the author edits.** The airline list still drives ATC and AI parking,
> which is why it is not an error.
> **The sim SHOULD NOT add a
> fallback.** An empty stand is the correct reading of what the author wrote, and
> re-introducing a step-down takes back the expressiveness this change exists to
> provide.

#### Where the 29.8% actually comes from, and what answers it

The figure is **not** a property of the format, and it is not a hazard of hand
authoring. R17 makes that precise: a stand with no `1313` keeps today's
behaviour, so **importing an existing apt.dat creates no weights and therefore no
empty stands.** The 29.8% is conditional — "*if* every stand's weights were set to
the class it already declares" — and the only thing that would do that at scale is
**WED's own auto-fill** (§6.7f), a bulk operation the author accepts wholesale,
across up to 326 stands at a single airport.

So the exposure is concentrated exactly where no human is inspecting individual
stands. That splits the answer in two, and both halves are required:

| path | answer |
|---|---|
| an author editing one stand | **live per-stand readout**, updated on every change |
| bulk fill across an airport | **airport-level rollup**, presented at the moment the fill completes |

A rollup reads: *"47 stands filled. 12 will be empty more than half the time, 8
always."* Without it, two in three commercially served airports acquire the defect
silently and in one click.

#### What to display, and what not to

All four levels are computable from `1301`, `1313` and the index. They are not
equally worth showing:

| value | how | show it? |
|---|---|---|
| P(class) | `1313` normalised | low value — the author just wrote it |
| **P(empty)** | `Σ_c P(c) · [no airline eligible at c]` | **first-class. Not buried in a detail view** |
| P(airline) | `Σ_c P(c) · [eligible] / n_eligible(c)` | yes — this is where deselecting an operator shows its cost |
| P(one livery) | `P(airline, c) · 1/\|eligible\|` or `EXPORT_RATIO` | **de-emphasise** |

`P(empty)` earns the top slot because it is **the only quantity in this design
that is invisible in the sim.** A gate with no aircraft looks exactly like a gate
that did not happen to get one this time; it produces no log line, no error, and
no bug report — only a vague sense that airports got emptier. Turning that into a
number is the whole reason the readout exists.

`P(one livery)` is de-emphasised deliberately. §4.3 assigns stage 3 to the
**library**, not the author — `EXPORT_RATIO` is where Laminar expresses "this
fictional livery is rare". Surfacing those fractions prominently invites authors
to tune the one stage that is not theirs.

**Show the delta, not just the level.** The author's question is never "what is
the number" but "what did I just do". At the moment of the click:

```
Deselect United:     empty  3% → 31%
```

This is what makes an over-aggressive cut self-evident, and it needs no memory of
the previous value.

#### The sentences

One per scenario, because the scenarios differ in kind rather than in degree. The
percentage is always **occupancy** — `1 − P(empty)` — never "the chance of this
operator", which under a single-operator stand is 100% and says nothing:

```
one operator   This ramp will spawn Emirates aircraft, size D-E, 70% of the time.
               (no Emirates aircraft exists at size D)

several        This ramp has an even chance between whichever of Delta, United,
               American has an aircraft at the size drawn - size C-E, 95% of
               the time.

military       This ramp will spawn military aircraft from: Italy, Sweden, USA.
general av.    This ramp will spawn GA aircraft from: USA, Germany, Canada, France.

nothing        This ramp will not spawn any static aircraft.
```

Three constraints these sentences are written to satisfy:

1. **"Even chance" is true per class, not overall.** R18 removes an operator that
   has nothing at the class drawn, so listing three operators does not give each
   33%. The wording says "whichever of … has an aircraft at the size drawn"
   rather than implying an even split over all three, because an author who reads
   the simpler sentence will plan around a number that is not true.
2. **"Nothing parks here" has three causes that look identical from outside**
   (§4.5 point 2 above): all-zero weights, no operator with an asset at a weighted
   class, and an operation type of `none`. The sentence must say which — the
   readout is the only place in the whole design where they can be told apart.
3. **The country data behind the military and GA sentences is thin, and the
   wording must survive that.** Of the 22 Military and Gov rows, 10 have no
   registration country at all; `XPGA` carries 58 rows across 11 countries, 32
   of them USA. Naming three countries is the most these sentences can honestly
   do today.

**A heterogeneous multi-selection cannot be given any of these sentences.** Ramps
in one selection may differ in operator list and in size range — which is why the
operator checkbox is tri-state to begin with. A uniform selection takes the same
sentence with a count; a mixed one gets the aggregate only, *"12 of 47 stands park
nothing"*, and no sentence pretending the selection is one stand.

#### The other empty: a stand that parks the same thing forever

`P(empty)` is not the only invisible failure, and it is not the only common one.
Measured against the real global apt.dat: of the **39,247** stands listing two or
more operators, authors list a mean of **7.8** and only **4.0** can appear at the
declared class — and **6,879 of them (17.5%)** collapse to **exactly one**.

That stand parks the same airline every single time. It is not empty, so nothing
above sees it: aircraft spawn, occupancy reads 100%, and the only symptom is that
a whole pier turns out to be Air France. It will never be reported as a bug,
because nothing about it looks broken.

R18 is not at fault — an operator with no asset at the class drawn *must* be
absent from the pool, or the stand would park nothing part of the time instead.
The side effect is that the author's list is silently truncated, and the truncation
is the thing worth showing:

```
Only DELTA will ever park here
The other 7 listed operators have no aircraft at size C-E, so every
aircraft on this stand is the same airline.
```

**This ranks below an empty stand and above a healthy one.** Parking nothing is
worse news than parking one thing, so it does not displace the empty warning; but
it must displace the reassuring sentence, which is otherwise the only thing an
author would ever see.

#### Two properties of the readout worth stating

**It makes late binding visible, which is otherwise the design's most abstract
property.** Nothing in the file says what will actually park anywhere — that is
resolved against whatever is installed, which is the entire argument of §8.3 and
also the thing an author cannot see. The readout is where it becomes concrete:

```
United      30%   3 aircraft at C-E
Emirates    10%   1 aircraft at E — nothing at D or F yet
```

"nothing at D or F **yet**" is the format working as intended, not a defect, and
an author who can see the word stops reading an empty class as a mistake.

**It is computed against the author's install, and must say so.** Per §6.4 the
index is install-specific and a mismatch fails silently. A probability readout
turns that silent failure into *confident wrong numbers*, which is worse. The
readout MUST name the index version it resolved against, and **MUST distinguish
"0%" from "index not loaded"** rather than rendering both as a blank or a zero.

Note the direction of drift, which is favourable and is the late-binding argument
of §8.3 made visible: as the library grows, **P(empty) falls monotonically** — a
class nothing could fill becomes fillable. P(airline) and P(one livery) are *not*
stable, and are not meant to be: shipping an airline's first model at some class
adds them to the stage-2 pool there and reduces every other airline's share from
`1/n` to `1/(n+1)`. That is the format working, not drifting.

#### What the readout does not fix

It narrows the exposure; it does not remove it. Hand-edited apt.dat and
third-party writers bypass WED entirely, and nothing in the file distinguishes an
intentional empty stand from a mistaken one (see point 2 below). What it buys is
that **the WED path is legible throughout rather than only adjudicated at export**
— which is what makes the "no fallback in the sim" position defensible rather than
merely asserted.

**2. Three stages means three ways to find no candidate**, and from outside they
look identical: all-zero weights (intentional), no listed airline has that class
(a mistake), everything excluded by `Obsolete` or range (a mistake, or the
library moving on). Only the first should ever reach a released file
deliberately — that is what the R14 warning is for.

**3. The pipeline must be gated on `1313` (R17).** Applying three-stage selection
everywhere would change the appearance of all 20,108 airports that have ramp
starts, none of whose authors asked for it. Whether the legacy path's own library
bias is worth fixing separately is a sim-side call, but it should not ride in on
this.

**4. A malformed `1313` must be discarded whole (R5).** See the rule for why this
single case needs stating explicitly.

### 4.6 What this replaces

Today a stand set to class F gets a cascading step-down — roughly 75% F, and of
the remainder 75% E, and so on. It has no floor, cannot be pinned to a single
class, and offers no control over proportions. An author who wants "this apron is
Bs" has no way to say it. `1313` says it in six numbers.

---

## 5. Conformance vectors

Each case is one airport block. "today's behaviour" means the `1301` letter and
whatever the current step-down does with it. Vectors that name ZBAA are stands in
the sample package (`docs/livery_sample/`, stand number in brackets), computed
against the `20260925-r1` index with hubs placed from X-Plane 12.4.3-r2's Global
Airports.

### 5.1 Well-formed

| # | input | expected |
|---|---|---|
| V1 | `1313 0 0 10 0 0 0` on a stand | class C always; stages 2–3 then run |
| V2 | `1313 0 0 0 0 0 0` | nothing parks. **Legal**, not an error (R5), and exempt from R14 |
| V6 | `1313 1000 0 0 0 0 0` | legal; 1000 is the ceiling (R11) |
| V26 | `1313 0 0 0 0 0 10` where the only listed operator has nothing at F | operator is **not** a stage-2 candidate; the stand parks nothing. Correct behaviour, and the R14 warning case for a writer |
| V30 | `1301 A airline dal` with `1313 0 0 0 0 9 1` | reader: selection uses the weights, the letter is not consulted. Writer/WED: correct the letter to `F` (R23) |
| V31 | an index row for the drawn (airline, class) whose NOTE is `Obsolete` | that row is not a stage-3 candidate, and does not make its operator a stage-2 candidate either (R25) |
| V33 | ZBAA, `1301 C airline ual dal aal baw`, `1313 0 0 10 0 0 0` [22] | **nothing parks**: every listed operator's class-C aircraft (`A320` 6,100 km, `B738` 5,700 km) is beyond range of its nearest hub — UAL's is 9,495 km away. No operator is a stage-2 candidate (R26, R18) |
| V34 | as V33 with `1301 E`, `1313 0 0 0 0 10 0` [23] | AAL and BAW park: their `B772` (14,000 km) reaches (R26). UAL and DAL are not candidates — their only class-E liveries are marked `Obsolete` (R25) |
| V35 | ZBAA, `1301 D airline ual ups`, `1313 0 0 0 10 0 0` [24] | UAL is a candidate through `B763` only (its `B752`, 7,200 km, is filtered row by row); UPS is not a candidate. Range is per (operator, type), not per operator |
| V36 | a candidate row with `RANGE_KM` empty or `0`, far from every hub | **eligible.** Unknown range is never filtered (R26) |
| V37 | an operator none of whose hub ICAOs is in the reader's Global Airports | **eligible**, all rows. No placed hub is never filtered (R26) |
| V38 | a Military or Gov row at a stand 15,000 km from anything | eligible. Military and Gov are exempt from range (R26); only `HOME` restricts them |
| V39 | a `HOME` row (`B752 *** DOJ *** N119NA *** USA *** … *** HOME *** Gov`) at a military stand in the USA, and at one in China | candidate in the USA; **not** a candidate in China (R27) |
| V40 | a `HOME` row whose OPERATOR record has no country and whose `REG CTY` is empty, or at an airport whose country is unknown | candidate (R27 fails open) |
| V41 | a GA stand in the USA with `1313 10 0 0 0 0 0` and `1301 A general_aviation xyz` | class A. A USA-registered GA livery at A with probability 0.7, else any GA livery at A. `xyz` plays no part; no range rule (R28) |
| V42 | as V41 at an airport whose country has no registered GA livery at the drawn class | any GA livery at that class, always (R28) |
| V43 | `1301 C none` with `1313 0 0 10 0 0 0` | nothing parks, as if the weights were six zeros (R29) |

V26 is the case most likely to be got wrong, and it is the one R18 exists for: an
operator with no asset at the class drawn must be absent from the pool, not drawn
and then found empty. V33 and V35 are the same case where the filter that empties
the set is range rather than class.

### 5.2 Malformed — all of these MUST load the file successfully

| # | input | expected |
|---|---|---|
| V8 | `1313 0 0 10 0 0` (five weights) | drop weights **whole**; stand falls back to today's behaviour (R5). MUST NOT be read as `0 0 10 0 0 0` |
| V9 | `1313 0 0 10 0 0 0 0` (seven) | drop weights whole (R5) |
| V10 | `1313 0 0 -5 0 0 0` | drop weights whole (R11, R5) |
| V11 | `1313 0 0 9999 0 0 0` | drop weights whole (R11, R5) |
| V12 | `1313 0 0 1.5 0 0 0` | drop weights whole — no decimal point is legal (§4.2) |
| V16 | `1313` with nothing after it | drop the row (R4) |
| V17 | **two** `1313` rows on one stand | the **first** applies; the second is discarded whole. Not merged, not overwritten, not compared (R24) |
| V18 | a `1314` row | ignored (R15). WED 2.8 lists it after import and in Validate |
| V19 | an embedded tab inside an airline list | writer defect (R9); reader treats it as a field separator, which may yield an unparseable token — drop that token, keep the row |
| V20 | file at version `1200` containing a `1313` row | loads; the row applies (§7.2 — no version gate) |
| V32 | `1313` appearing before any `1300` in the airport block | drop the row — there is no stand to attach it to (R20) |
| V44 | a line beginning `#` | writer defect (R19). X-Plane ignores it; WED 2.8 skips it as record code 0 and lists it; WED 2.7.x fails the file |

V17 is the one with a decision behind it rather than a mechanism: see §9 for
what taking the first costs, and what WED now does to make it visible.

### 5.3 The two that must never happen

| # | input | expected |
|---|---|---|
| V21 | any row above | **the file still loads.** No input in §5.2 may produce a load failure (R4) |
| V22 | every `1313` row stripped | byte-identical aircraft placement to today (R2) |

V22 is the regression test worth automating. It is the one guarantee everything
else rests on.

---

## 6. The livery index

### 6.1 What it is and why it exists

X-Plane publishes static aircraft through `library.txt` `EXPORT_EXTEND` buckets
keyed by (operation type, size class, airline):

```
EXPORT_EXTEND lib/airport/aircraft/airliners/heavy_e.obj      apt_aircraft/heavy/A35K/A35K_BritishAirways.obj
EXPORT_EXTEND lib/airport/aircraft/airliners/heavy_e_baw.obj  apt_aircraft/heavy/A35K/A35K_BritishAirways.obj
```

That name carries **no aircraft type** — one `heavy_e` bucket mixes A359 (×39),
A35K (×11) and B772 (×5) — and **no livery**. So "United's 737-800 in the retro
livery" cannot be named, even though `B738_UAL_Legacy` and `B738_UAL_Modern` have
both shipped for years. Nine Air ships eleven colour variants of one 737; Air
China ships five Peony tail numbers. None of it is addressable.

`library.txt` describes how the sim **picks** an object. The index describes
**what an object is**. Those change for different reasons and on different
schedules, which is why they are separate files.

### 6.2 Format and location (schema 4)

Lives next to the assets it describes, and ships with X-Plane 12.5:
`Resources/default scenery/sim objects/apt_aircraft/livery_index.txt`. WED 2.8
turns its livery features on when it finds the file, and stays quiet without it.

A file is, in order: a two-line header, the version stamps, the OPERATOR
records, then the livery rows. Lines starting `#` are comments; blank lines are
ignored. Cells are separated by `***`, and **every reader strips spaces *and*
tabs from each cell**, so the whitespace between cells is layout, never data.

**Header — mandatory.** The first two lines are exactly:

```
I
1 WED Aviation Database
```

WED refuses a file without them (`CheckWedMandatoryHeader`) and treats it as
absent, which is also how a truncated or foreign file is kept out. Then the
stamps, deliberately separate:

```
# schema 4                          how many cells and what they mean
# data 20260925-r1                  which day's content this is
# source X-Plane 12.4.3-r2-15ff1e4d WHICH INSTALL this describes
# assets 298 liveries under apt_aircraft/
```

Folding schema and data into one would force a reader to write
`if (date >= 20260916)`, which is wrong both ways: the layout can change twice in
a day, or not change for a year.

**OPERATOR records** — one per operator, seven cells:

```
OPERATOR *** <CODE> *** <NAME> *** <IOC CTY> *** <Pax|Cargo|GA|Military|Gov> *** <FLEET> *** <HUB ICAOs>

OPERATOR *** UAL  *** United Airlines         *** USA *** Pax      *** 1138 *** KORD KIAH KDEN KSFO KEWR KIAD KLAX
OPERATOR *** DOJ  *** US Department of Justice *** USA *** Gov      *** 0    ***
OPERATOR *** XPMI *** Generic - military      ***     *** Military *** 0    ***
```

- `CODE` is the airline code in upper case (R10's shape).
- `IOC CTY` is the operator's country, an **IOC** code (`USA`, `GER`, `HKG`).
  Empty only for the three pseudo-operator kinds (§6.7c).
- The fifth cell is the **operation class**, one of five words, and is what the
  stand's operation type is matched against (§4.1).
- `FLEET` is an approximate fleet size, used by WED to rank operators; `0`
  means not researched (and is always `0` on military, government and generic
  records). A reader may ignore it.
- `HUB ICAOs` are space-separated airport ICAO codes, the operator's hubs,
  in no particular order. **Military and Gov records carry none.** Each reader
  places them itself (§6.7b).

**Livery rows** — one per `.obj`, ten cells, `path` always last:

```
<TYPE> *** <CLASS> *** <AIRLINE> *** <REG> *** <REG CTY> *** <NOTE> *** <RANGE_KM> *** <SCOPE> *** <OP> *** <path>

B738 *** C *** UAL *** N78540 *** USA *** Retro   *** 5700 ***      *** Pax *** jet/B738_UAL_Legacy/738_United_Legacy_N78540.obj
B752 *** D *** DOJ *** N119NA *** USA *** Default *** 7200 *** HOME *** Gov *** jet/B752/757_USDOJ_N119NA.obj
```

- `TYPE` is the ICAO aircraft type designator.
- `CLASS` is the ICAO wingspan class `A`–`F`, the same vocabulary as `1301`'s
  letter and `1313`'s six slots.
- `AIRLINE` is the operator code, and has an OPERATOR record.
- `REG CTY` is an **IOC** code, not ISO — so `HKG`, `TPE`, `MAC`. `???`
  means unresolved.
- `NOTE` is **free text and an open vocabulary**, not an enum. WED renders
  anything other than `Default` in parentheses after the airline name — "United
  (Retro)", "Air China (Peony)". A closed vocabulary here would throw away
  exactly the variants nobody thought to enumerate; we shipped that bug once and
  will not again. The one reserved value is `Obsolete` (§6.6).
- `RANGE_KM` is the type's typical operating range in km (R26, §6.7).
- `SCOPE` is `HOME` or empty (R27, §6.7d).
- `OP` repeats the operator's operation class, so a row is self-contained.
- `path` is relative to `apt_aircraft/`, and is the only unique key (§6.3).

**Reading older schemas.** `path` has been the last cell in every schema, so a
reader takes it from the end. A schema 1 row has seven cells (no range, no
scope, no `OP` — nothing filtered); schema 2 and 3 rows carried hub
coordinates in the eighth cell, which a schema 4 reader ignores, because the
same file's OPERATOR records carry the ICAOs they were computed from.

### 6.3 What the index does NOT guarantee — read before implementing stage 3

**`(type, airline, note)` is not a unique key, and cannot be made into one.**
Verified against all 298 rows: **26 groups covering 83 rows (28%) share that
tuple.** Adding registration narrows it to 14 groups / 30 rows. It does not close
it. **The only unique key is the object path.**

The 26 groups have three distinct causes, and they need different responses:

| cause | groups | rows | what to do |
|---|---|---|---|
| **Old and new asset both exported** for one real aircraft — `AT45_FDX_static.obj` *and* `ATR42-500_FedEx.obj` | 13 | 26 | **Mark the old one `Obsolete`** (§6.6). Twelve are marked as superseded (the index's other twenty marks are retired airframes); the thirteenth is the `F15EX` pair below, which is not a duplicate at all. Each unmarked pair doubles that aircraft's spawn probability (§4.4) |
| **Genuinely different airframes, distinguished only by registration** — Air China's five Peony tails, eight PC-12s, Delta's two 757s | 11 | 50 | **Nothing. Correct as-is.** The index carries the distinction in `REG`; the note legitimately repeats |
| **Genuinely different airframes the index cannot currently name** — `757PW` / `757PW_winglet` / `757RR` / `757RR_winglet` (engine and winglet variants), `F15EX` / `F15EX_cft` (conformal tanks) | 2 | 7 | **Index fix, by hand** — 6 of those 7 rows. The variant is in the *filename* but the folder is a bare `<TYPE>/` with no airline token, so the folder-driven variant rule finds nothing. Deriving it from the filename would have to tell a variant from an airline name and a registration (`757_Delta_N654DL.obj`), a heuristic that would misfire across the other 292 rows to fix 6 |

The consequence for a reader: **do not de-duplicate**, and do not assume a lookup
by `(type, airline, note)` returns one row. It returns a list. See §4.4.

### 6.4 The index is install-specific, and a mismatch fails silently

This bit us while writing draft 4, which is why it has its own section.

The machine this was built on carries two installs: **12.4.4-pnl5 with 376 static
aircraft** and **12.4.3-r2 with 298**. The 78 extra are beta-lane assets that
released users do not have. An index built from the beta and read against the
release resolves 78 entries to files that are not there — and **the only symptom
is previews that quietly never appear.** No error, no log line, nothing to search
for.

Hence the `source` header field, and hence the rule: **whatever ships this index
must ship it from the same build as the assets it describes.**

### 6.5 Index invariants a consumer may rely on

Verified mechanically against the shipped file (`20260925-r1`: 298 livery rows,
1,499 OPERATOR records); a generator change that breaks one of these is a
regression.

- **I1** — The file begins with the two header lines `I` and
  `1 WED Aviation Database`, and carries a `# schema 4` stamp.
- **I2** — Every livery row has exactly ten `***`-separated cells, and every
  OPERATOR record exactly seven.
- **I3** — `path` is unique across the file, and is the only unique key (§6.3).
- **I4** — Every `path` resolves to a real `.obj` under `apt_aircraft/`. All 298
  verified.
- **I5** — Every `.obj` has a resolvable texture, via the `TEXTURE` directive
  inside the object, with `.dds`/`.png` substitution. All 298 verified.
- **I6** — `CLASS` is always one of `A`–`F`, never blank. `AIRLINE` is never
  blank, and every `AIRLINE` has an OPERATOR record.
- **I7** — A row's `OP` equals its operator record's operation class.
- **I8** — `SCOPE` is `HOME` or empty. In the shipped file `HOME` appears only on
  Military and Gov rows (11 of them).
- **I9** — Military and Gov OPERATOR records carry no hubs. The only records
  with no country are the pseudo-operators `XPGA`, `XPMI` and `XPZZ_<TYPE>`.
- **I10** — `REG`, `REG COUNTRY` and `NOTE` MAY be blank or `???`. A consumer must
  tolerate all three. `RANGE_KM` MAY be blank; it is filled on every shipped row.
- **I11** — The file is manually maintained. Re-running the generator is a
  merge and a diff-review, never a blind overwrite. Registrations that came from
  an OCR pass are marked in the source tree until a human has checked them.

I4 and I5 are the ones worth re-running in CI. They are the only way the
"data says yes, disk says no" failure of §6.4 gets caught before a user sees it.

### 6.6 `Obsolete` — the one reserved NOTE value

**`Obsolete` in the NOTE column means the livery must never be chosen** (R25). It
is not a caption, and it is the only value in that column a reader interprets.

Everything else about NOTE is unchanged: it stays an **open vocabulary**, free
text, rendered in parentheses after the operator's name. `Obsolete` is a reserved
sentinel inside an open field, not the first entry of an enum, and a reader that
starts validating this column against a list will silently drop the Peony, Mixue
and Peacock liveries that the column exists to carry (§6.2).

The marked row keeps its `.obj` on disk and its `EXPORT_EXTEND` line in
`library.txt`. That is the entire point: scenery referencing the object by a hard
path still loads, while nothing *chooses* it any more. No other mechanism
available to us separates "reachable" from "selectable" — deleting the export
does both at once.

**Three things it is for.**

1. **Superseded assets.** An old and a new object for the same real aircraft are
   both exported, so that aircraft draws double probability (§4.4). Twelve such
   old objects are marked today, and **those twelve alone empty no (airline,
   class) pair**.
2. **Retired airframes.** The operator no longer flies the type, so a stand
   listing them should not park one. United's 747-400 is the clean case: retired
   in 2017, and it is their only class-E asset in the library — which is exactly
   why earlier drafts needed a per-stand `-ual:B744` at every 747-era gate. It is
   marked; the mark states the fact once, globally, where it is true. Twenty
   retired airframes are marked today, among them Delta's 777 and CRJ-200,
   Emirates' 777, Cathay's and Virgin's 747s and FedEx's DC-10.
3. **Defunct operators** — the same fact one level up: the airline flies nothing
   at all. Alitalia, Air Berlin, US Airways, Sunwing and seven more, twelve rows.
   A mark here costs one thing the other two do not: NOTE holds one value, so a
   caption on the row (China Eastern Yunnan's `Peacock`) gives way to the mark.

Together the **44 marks empty 25 (airline, class) pairs**: AFR/B, AUA/B, AWE/C, AZA/C, BER/C, CJT/C, CPA/E, CYH/C, DAL/B, DAL/E, DLA/C, EGF/C, EXS/D, HMF/A, LPV/B, RUS/B, RVF/D, SAS/C, SIA/C, SWG/C, SWQ/C, UAE/E, UAL/E, VIR/E, WLC/B.
Every one is a retirement or a closure, and so a deliberate withdrawal: a stand weighted
only for one of those classes, listing only that operator, now parks nothing,
and R14 says so.

**A mark is the word `Obsolete` in the row's own NOTE column.** There is no
sidecar and no second file: the index is hand-maintained and the generator
merges into it rather than overwriting it, so the mark survives a regeneration
because it is simply still there.

**A mark's blast radius must be read before it is made.** Withdrawing a livery is
global and has no per-stand undo, and an (airline, class) pair left with no asset
is a stand that silently parks nothing — the §4.5 defect, manufactured by a data
edit. `tools/scripts/airline_research/livery_obsolete_radius.py` prints how many
pairs the marks already made have emptied, and answers the question
hypothetically without requiring the mark. It is read-only and needs no X-Plane
install, so it runs against whichever copy of the index is being edited; the
generator prints the same report after its merge, on the rows it is about to
write.

```
livery_obsolete_radius.py [index] --what-if B752 MD82 UAL:B744

obsolete marks        : 44 rows
  would empty         : 25 (airline,class) pair(s)  <-- each one is a stand that parks nothing
      AFR class B
      ...                                           (25 lines)
      WLC class B
what-if B752          :  36 rows (2 already marked), would empty  21 pair(s)  AHY/D AIO/D ATN/D AZV/D ... +17
what-if MD82          :   6 rows (6 already marked), would empty   0 pair(s)
what-if UAL:B744      :   1 rows (1 already marked), would empty   0 pair(s)
```

A selector is a type, `AIRLINE:TYPE` (the retired-airframe case: one operator's
rows, not the type everywhere) or a path. Each is answered on top of the marks
already in the file. `--rank` lists every type by the pairs it would empty.

The 757 is in daily service; twenty-two emptied pairs is what one careless line
costs. Emptying pairs is not always wrong — a genuinely retired type *should*
empty them — but it must be a decision someone made with the number in front of
them.

### 6.7 The range rule — R26

**The problem it answers.** Stage 3 chose among an operator's liveries in the
drawn class with nothing but `EXPORT_RATIO` to go on, so a stand at Beijing that
lists United drew United's 737 as readily as its 777. The 737 cannot reach
Beijing from anywhere United flies it. This is the one systematic absurdity the
three-stage design still produced, and it is a property of the *aircraft and the
operator*, not of the stand — so it does not belong in apt.dat, and R23/R25's
argument applies again: the index, which describes the livery and the operator,
is where the fact goes.

**Two inputs, both in the index.** Schema 2 introduced them as two cells before
`path`; schema 4 keeps one on the row and moves the other to the operator:

- `RANGE_KM` — on the livery row. Typical operating range of `TYPE` at a
  realistic payload, in km. Not the ferry figure. A physical constant; it is
  authored once in `WED_AircraftSizeReference.txt` and never revisited.
- **Hubs** — the ICAO codes on the operator's OPERATOR record (§6.7b), placed by
  each reader from its own Global Airports. Schema 2 and 3 wrote resolved
  `lat,lon` pairs into the row instead; nobody could check them by eye, so
  nobody could maintain them. The row's eighth cell is now `SCOPE` (§6.7d).
- Either missing means *unknown*, and **unknown is never filtered**. Military
  and Gov records have no hubs, pseudo-operators have none, and a type without a
  researched range has no figure. All fall through the rule untouched. A missing
  fact must never hide a livery (same posture as R4/R5: fail open).

**The rule — R26.** For each livery row that the drawn class and the stand's
operator admit, inside `eligible()` (§4.1):

```
if OP is Military or Gov                     -> eligible (R27 decides where)
if OP is GA                                  -> eligible (R28 has no range)
if RANGE_KM is empty or no hub was placed    -> eligible
d = min over hubs of greatcircle(hub, stand)   -- stand from the 1300 row
if d > RANGE_KM                              -> not a candidate
else                                         -> eligible
```

Military and government rows are exempt: they park at home (R27) or anywhere,
and no country is wide enough for range to matter at home.

**Range is part of eligibility, not a stage-3 afterthought.** If the rule
removes every row an operator has in the drawn class, that operator is not a
stage-2 candidate for that class (R18): it is never drawn, so it can never
leave the stand empty. The stand parks nothing only when *no* listed operator
has a usable row at the class drawn — the same outcome, and the same readout,
as any other class nobody can fill. Draft 7 had this wrong (§4.1).

**What is deliberately not in apt.dat.** Nothing. The inputs the rule needs are
the stand's own position, which every `1300` row has always carried, and facts
about the livery and operator, which live in the index. Every apt.dat in
existence therefore gains the behaviour the day the index does, with no
re-export — and there is no derived copy of the answer anywhere to go stale. (A
per-airport "deny list" row was considered and rejected on exactly that ground:
it is a cache of this computation written into tens of thousands of files that
cannot be refreshed together. See 8.7.)

**WED evaluates the same predicate.** `WED_LiveryInRange()` reads the same
range, the same hubs placed from the same Global Airports, and the same stand
position (`GetLocation`, i.e. the number the `1300` row is written from), and
removes the same rows from the preview cards, the coverage count and the R14
warning. The two sides cannot disagree because there is no second copy of
anything. The readout names what was removed — "Beyond range from their hubs,
so not offered here: UAL B738/A320" — so the author is not left inferring why a
card shrank.

**What the rule is not.** It is a floor. It removes what cannot physically reach
the stand and says nothing about what an operator *chooses* to fly there: a
United A321neo (7,400 km) at London (7,155 km from Newark) passes. Encoding
commercial choice needs a route network at (airport × operator × type)
granularity, which changes every season and is a maintenance endpoint of a
different order. This design does not attempt it, and should not be read as a
partial attempt at it. A domestic operator needs no exemption: its nearest hub is
close by definition.

**Why not class as a proxy for range.** Because wingspan and fuel fraction are
unrelated: in the shipped size reference class C spans the ATR-42 at 1,500 km
and the Global 7500 at 14,260 km, and a class-B Challenger out-ranges most
class-C airliners. Any class-level rule either strands domestic narrowbodies or
admits everything. Measured, not assumed — see 8.7.

**Why not the operator list on the stand.** Because the constraint is per
(operator, type) and the `1301` list is per operator. United at a class-D stand
in Beijing has a 757 that cannot reach (7,200 km) and a 767 that can (11,000 km)
— both class D. No action on the operator as a whole keeps one and drops the
other. Stand 24 of the ZBAA test bench in `docs/livery_sample/` exists to make
this visible (V35). Stand 26 is its cargo counterpart: with Hong Kong and Tokyo
on FedEx's hub list, both of FedEx's current class-D types (757, 767) reach
Beijing - the rule working from better hub data, not a failure of the test.

### 6.7b Operator records: one file for both readers

Every fact about an operator - name, country, operation class, fleet size, hub
ICAOs - is written into the index once per operator, ahead of the livery rows
(format in §6.2):

```
OPERATOR	***	UAL		***	United Airlines				***	USA	***	Pax			***	1138	***	KORD KIAH KDEN KSFO KEWR KIAD KLAX
OPERATOR	***	FDX		***	FedEx Express				***	USA	***	Cargo		***	478		***	KMEM KIND KOAK ... VHHH RJAA
OPERATOR	***	AIO		***	United States Air Force		***	USA	***	Military	***	0		***
OPERATOR	***	XPMI	***	Generic - military			***		***	Military	***	0		***
```

The operator section is **tab-aligned** (laid out by the generator for a tab
width of 4), unlike the space-padded livery rows: it has a wide free-text NAME
cell and is the part of the file people actually read. Every reader splits on
`***` and strips spaces *and* tabs, so the whitespace between cells is layout,
never data - a reader that matches a literal `" *** "` drops every record.

One record per operator rather than the facts repeated on every livery row.
The three kinds of pseudo-operator (§6.7c) get records too, so the ramp's
operation-type filter has one answer for everything in the file. A reader that
does not know the record kind skips it: a livery reader skips `OPERATOR`, and
the operator reader skips every line that does not start with it. A record with
no livery row is normal — the operator directory is also WED's recommendation
source — and is kept across regeneration.

**Placing hubs.** Each reader places the ICAOs itself when it loads the index,
from the install's Global Airports: the airport whose `1302 icao_code` is the
hub, else the one whose header ident is; its `1302 datum_lat`/`datum_lon`, else
the midpoint of its first runway. The metadata wins because the ident is not
always the ICAO code - Ezhou (ZHEC) and Chengdu Tianfu (ZUTF) sit under
placeholder idents, and the ident ZSQD is the closed Liuting while `icao_code
ZSQD` is the new Jiaodong. A hub that cannot be placed is simply absent; an
operator with none placed is never range-filtered (R26). WED places them on a
worker thread after load, and until that lands every operator reads as having
no hubs, which is the fail-open state.

**The index is the only file either program reads, and it is the file that is
maintained.** There is no WED-side directory and no fallback: a second source
that only kicks in when the first is missing rots between releases unnoticed.
The generator (`tools/scripts/airline_research/gen_livery_index.py`) is a
*merge* tool: it keeps every row and operator record already in the file
exactly as written, drops rows whose asset is gone, and adds a guessed row only
for a genuinely new asset, using two bootstrap tables under
`tools/scripts/airline_research/bootstrap/` that nothing at run time reads.

### 6.7c Reserved pseudo-operators, and the one that is never placed for you

Three kinds of code stand in for an operator that does not exist, so that every
asset in the file still has an operator record and therefore an answer for the
ramp's operation-type filter:

| code | stands for | op class |
|------|------------|----------|
| `XPGA` | general aviation - light aircraft and business jets alike | GA |
| `XPMI` | military | Military |
| `XPZZ_<TYPE>` | unpainted / house-colours airliner of that type (`XPZZ_B752`, `XPZZ_DC10`), one code per type | Pax |

They are the only records with an empty country, because a generic has no
nationality; the reader exempts exactly these from the country requirement, and
nothing else. The generic airliner is one code per type (schema 4) so a stand can
list the one white airframe it means, and `1301` carries it like any other code
(R10). A bare `XPZZ` from an older index is still recognised. At an Airline or a
Cargo stand, `XPZZ_<TYPE>` passes the operation-class filter either way.

An **unpainted** airframe takes the code for what it *is*, not a single "white"
bucket: a white light aircraft is `XPGA`, a white airliner `XPZZ_<TYPE>`, a bare
military airframe `XPMI`. Otherwise a white Cessna would answer the Airline
filter and a white 757 the GA one.

**`XPZZ_*` is never placed automatically.** It sorts below every other operator
in the picker whichever way the sort arrow points, and any auto-fill pass that
populates stands must skip it. It exists so a person can deliberately park an
unpainted airframe. If a machine could pick it, every airport in the world
would sprout white 757s - the fallback would become the most common aircraft
in the sim. The Z's are the mnemonic: the code sorts last on purpose.

`XPBZ`, a fourth code for business jets, was retired 2026-09-19. The ramp
offers one General Aviation operation type, so a light aircraft and a business
jet were never once treated differently - the split cost a code to remember and
bought nothing.

Codes of the shape `XPnn` (`XPA0`, `XPB7`) are not pseudo-operators: they are
real operators for which no ICAO designator could be confirmed, allocated in
order, and replaced the moment a real code is.

### 6.7d `SCOPE` and home-only equipment — R27

The eighth cell of a livery row is `SCOPE`: `HOME` or empty.

- **R27** — A row whose `SCOPE` is `HOME` is a candidate only at an airport in
  the operator's own country: the country on its OPERATOR record, else the
  row's `REG CTY`. The airport's country comes from the airport, not from a
  distance. If either country is unknown the row stays a candidate (fail open,
  as R26). A military or government row without `HOME` parks anywhere.

`HOME` is for equipment that names one operator so specifically it has no
business abroad: a head-of-state 757, an air force's own-marked airliner, a
justice-department transport - and, by the same test, an air force's own
transports and trainers (the USAF's Challenger, C-146 and PC-12, the Navy's
T-44). Combat types are flown by many countries, and an F-15, an F/A-18 or a
Seahawk at a foreign base is unremarkable, so the default is anywhere. In the
shipped index `HOME` appears on 15 rows, all of them Military or Gov.

Military and government OPERATOR records carry no hubs: R26 never reads them.

A schema 3 file still reads: its eighth cell is `HOME` or coordinates, and
coordinates are ignored, because the same file's OPERATOR records carry the
ICAOs they were computed from.

### 6.7e General aviation stands — R28

- **R28** — At a GA stand the sim draws a GA livery of the drawn class
  registered in the airport's country (the row's `REG CTY`) with probability
  0.7 when one exists, otherwise any GA livery of that class. There is no range
  rule for GA (R26 exempts it) and no airline list: GA is drawn from the
  library by size, so `1301`'s airlines play no part.

A GA livery is a row whose `OP` is `GA`, `XPGA` included. The 70% keeps a
field mostly home-registered without making a foreign visitor impossible. The
class draw still comes from `1313`, so the author controls the size of what
parks and the index controls whose it is.

### 6.7f Auto-fill (WED only - nothing new in the file)

WED can fill an airport's stands in one step, the way it updates metadata,
from `WED_AirportDatabase` (which operators serve the airport) and this index.
It **extends and never overwrites**: an author's airlines, weights and
operation types stay as they are. Everything it writes is ordinary `1301` and
`1313` content; a reader cannot tell a filled stand from a hand-authored one.
Per gate or tie-down, by operation type (Liveries tab label, then the stored
name):

| operation type | what auto-fill does |
|---|---|
| None | nothing - None means no static aircraft (R29) |
| any, no weights | converts the one size letter to weights: A: A100; B: A30 B70; C: B30 C70; D: B10 C40 D50; E: C10 D30 E60; F: D10 E50 F40. A draw that lands on a class where no listed operator is eligible parks nothing for that load (§4.1); weights are never renormalised |
| Passenger (Airline) / Cargo | adds the airport's recommended operators of that operation class that have an eligible livery at a weighted class (range R26 included) and fit the stand's equipment type; never `XPZZ_*`; stops short of Gateway's 100-character airline string (R10) |
| Military/Gov (Military) | adds the airport country's own military and government operators with such a livery; if there are none, nothing is added and the sim draws military by size |
| Private/BizJet (General Aviation) | weights only (R28) |

A stand it changes carries a watermark in `earth.wed.xml` (`auto_filled="1"`
on `<ramp_start>`), never in apt.dat. Any human edit of the stand other than
its weights clears it: someone has looked. WED's moderation tooling reads it:
a moderator steps through an airport's stands one at a time, and a stand is
flagged if it is still auto-filled, has operation type None, or lists an
operator that is unknown to the index or not listed for this airport - the last
two with a web search offered to settle them. None of it puts anything in the
file.

### 6.8 Authoring note — the weights mode is a property of the stand

WED keeps two things per stand in its own document (`earth.wed.xml`), not one:
the six weights, and whether they are **in use**. "Simple Mode" parks the
weights rather than deleting them; "Set Spawn Weights" brings them back exactly
as left. Both survive save and reload.

Export follows the mode: a stand in simple mode writes no `1313` row whatever
it holds, and a stand in weights mode writes one (with R23's derived size
letter). So the apt.dat says what the author last *chose*, and the author can
change their mind without retyping a distribution. Nothing about this reaches
the format - `weights_mode` is a WED-side XML attribute - and an imported
`1313` row puts the stand in weights mode, since data present is data in use.

---

## 7. Evidence

### 7.1 Measurements

Four candidate formats were measured against the real global apt.dat — 380 MB,
38,888 airports, 200,102 ramp starts, 44,242 of which carry airlines — crossed
with the real shipped livery set. We did not pick a shape and rationalise it.

| | what it stores | size today | after the library fills out |
|---|---|---|---|
| **A** explicit pairs `A320_UAL A359_DAL …` | resolved result | 2.21 MB | ~4.4 MB and climbing |
| **B** grouped by type `B738=UAL,DAL` | resolved result | 1.34 MB | ~2.7 MB and climbing |
| **C** named fleet sets, referenced per stand | resolved result | 0.88 MB | ~1.8 MB and climbing |
| **D** subtractive — airlines, minus exceptions | **intent** | 0.03 MB | **unchanged** |

Supporting numbers:

- **220,610** (type, airline) pairs would exist if expanded explicitly — about 5
  per stand.
- **53.9%** of the airline codes authors already write resolve to a model today.
  The other 46% expand to nothing, so an explicit list silently records a gap as
  if it were a complete answer.
- **83%** of stands share their airline list with four or more stands at the same
  airport; **53%** share with sixteen or more.
- Only **84 airports** have 100 or more airline-carrying stands — but they are CDG
  (326), ICN (285), CKG (277), ORD (273), IST (244), PEK (231). The pathological
  case is rare and lands exactly where it hurts.

### 7.2 Compatibility, tested rather than assumed

We said we would answer this rather than ask, so we did. The format was written
into real scenery packages and loaded by a real X-Plane. **The sim ignores both
the new rows and an unrecognised version number.** Full log:
`docs/livery_evidence/XPlane12.4.4_apt_dat_tolerance_Log.txt`.

Four single-airport packages in `Custom Scenery/`, loaded by **X-Plane 12.4.4
(build 124406)**:

| pack | apt.dat | result |
|---|---|---|
| `ZZ01` | version 1200, valid | silent |
| `ZZ02` | version 1200, plus unknown row code `1310` | **silent** |
| `ZZ03` | **version 1300** — a version that does not exist | **silent** |
| `ZZ04` | version 1200, deliberately corrupt runway row | error, named, with a line number |

`ZZ04` is the control proving the harness works:

```
E/SCN: An apt.dat enumeration is out of range: Invalid surface code. Expected a code
       less than 58 but got 60615503. Airport is ZZ04.
       File is Custom Scenery/ZZZ_aptdat_test_ZZ04/Earth nav data/apt.dat.
E/SYS: MACIBM_alert: There was a problem loading the scenery package: ...
       The scenery may not look correct.
```

So the sim does parse these files at startup, does report errors with file and
line, and would have told us about `1310` or version `1300` if it objected.

Conclusions:

1. X-Plane 12 **ignores unknown row codes**.
2. X-Plane 12 **accepts an unrecognised version number**.
3. Even a genuine error is **non-fatal** — "the scenery may not look correct",
   not "this file is rejected".

That was the opposite of WED up to 2.7.x, which whitelists versions
(`AptIO.cpp:327-334` today), rejected unknown row codes, and aborted the entire
load on either. Row `1301` shipped gated at version 1050 (`:751`), which is what
led us to assume the sim gated too. WED 2.8 now skips unknown row codes the way
the sim does (R15); the version whitelist is unchanged.

**Consequence: these rows can go into a 1200 file and old X-Plane keeps working**,
reading `1301` and ignoring the rest — exactly the degradation the format is
built around. A version bump becomes a choice about signalling intent rather than
a compatibility requirement.

### 7.2b Repeated on a second build, with the real sample file

The test above used four minimal synthetic packages. The sample package in
`docs/livery_sample/` — seventeen stands, all four row codes the format then
defined, a deliberate `1314`, and every malformed vector from §5.2 — was then
loaded by a **different build in the release lane: X-Plane 12.4.3-r2 (build
124311)**. Log:
`docs/livery_evidence/XPlane12.4.3-r2_livery_sample_load_Log.txt`.

> **The sample in the repo is no longer the one described here.** It was
> regenerated for the inline-only shape of §8.2 and now carries sixteen stands
> and two new row codes, plus the same deliberate `1314`. The evidence is
> unaffected: the run below proved X-Plane ignores unrecognised row codes, and
> the regenerated sample exercises a strict subset of the codes that run
> contained. Re-running it would weaken the test, not strengthen it.
>
> *Draft 8:* it has since been regenerated again. The package in the repo is now
> airport **ZBAA** (placed over Beijing Capital), 24 stands: the control and
> malformed stands of before, plus the range test bench (stands 20-27, V33-V35).
> It carries `1313` only. The ZZLI run below remains the evidence for unknown
> row codes.

```
I/FLT: Init dat_p0 type:'runway_start' apt:ZZLI rwy:09 ...
I/SCN: Loading sim objects for airport ZZLI
```

A flight started at the airport. Across the entire run there were 28 `APT`
diagnostics and **not one names ZZLI, our file, or any row we added.** All 28 are
pre-existing Global Airports issues — bad ATC frequencies and duplicate airport
codes.

The control is stronger here than in 7.2, because it is in the same run rather
than a separate package: X-Plane reported

```
W/APT: Unknown key 'altimeter_setting' for 'Washington Dulles Intl' in
       Global Scenery/Global Airports/Earth nav data/apt.dat on line 6737874.
```

So in **this very run** the parser found an unrecognised *metadata key*
objectionable enough to name, with file and line — and said nothing about four
unrecognised *row codes* plus a `1314` that does not exist. The silence is
demonstrably the parser's judgement, not the parser being asleep.

**What this does NOT establish.** Two builds, both 12.4.x
(124406 in the beta lane, 124311 in the release lane). Whether X-Plane 11, or an
early 12, is equally tolerant is still unknown, and that tolerance may have been
added at some point. If Gateway has to serve builds older than 12.4, that needs
checking against whichever is the real floor — a question about release support
policy more than about the format.

Also not established: that the seventeen ramp starts appear correctly in the gate
picker. The flight above was started from the runway, so rows `1300`/`1301` are
confirmed only as far as "they did not prevent the airport loading". The
`Loading sim objects for airport ZZLI` line is followed by 97 ms of preloading in
a package that contains no DSF and no objects of its own, which is consistent
with static aircraft spawning at those stands — but that is circumstantial and is
not claimed as proof.

### 7.3 The half that is not solved: an old WED

The same test applied to WED's own reader gives the opposite answer, and this is
the part still needing a decision. **Measured, not inferred** — we opened the
sample package in a WED built from this very branch:

```
Unable to read apt.dat file
'D:\...\Custom Scenery\ZZZ_livery_format_sample\Earth nav data\apt.dat':
Illegal unknown record (Line 13)
```

Line 13 was that file's first new row. (The sample has since been regenerated
for the inline-only shape of §8.2, so its line numbers have moved; the observation
stands as recorded.) WED imports **nothing** — not a degraded
airport, no airport.

The path is `AptIO.cpp:1188-1209` falling through to
`ok = "Illegal unknown record"`, which `WED_AptIE.cpp:1162-1167` turns into the
message above.

Note *which* WED that was: the one carrying all of this feature's work. Refusing
the file is not a property of old builds — it is every WED that has not yet
implemented these row codes, which today is all of them. That is why the decision
below is about distribution rather than about waiting for a release.

| | opening 2.8-era scenery |
|---|---|
| **X-Plane 12.2.x** | fine. Ignores the new rows, parks aircraft from `1301` exactly as today |
| **WED 2.7.x** | refuses the file, with a message naming the line |
| **WED 2.8** *(draft 8)* | opens it. Reads `1313`; skips any other unknown row and lists it (R15) |

> **Draft 8 — resolved for WED 2.8, not for 2.7.x.** The observation above is
> kept as recorded, and it remains true of every WED up to 2.7.x. WED 2.8's
> reader no longer fails on an unknown row code: it skips the row, records it on
> its airport (`AptInfo_t::discarded_rows`, `AptIO.cpp:1322`), lists what it
> skipped after File > Import, and raises `warn_apt_dat_rows_not_imported` in
> Validate. So from 2.8 on, no row code added later can make WED refuse a file
> again. What follows is still the question for 2.7.x users.

The sim side is genuinely solved. The editor side is not. Three ways out, none of
them ours to choose alone:

1. Gateway serves an old client an apt.dat with the new rows stripped — it can,
   since `1301` alone is complete and authoritative by design (R1, R2).
2. A 2.7.x patch that skips unknown row codes instead of failing. Small change,
   but it needs a release, and it only helps people who take it.
3. Accept it: editing 2.8-era scenery requires 2.8-era WED.

The populations differ in size and updatability. Sim users are many and update on
their own schedule; WED authors are far fewer and already track releases closely.
That argues for (3) with (1) as a safety net — but it is a Gateway policy call.

---

## 8. Rationale

### 8.1 Why A and B were rejected

Not size. **2 MB on a 380 MB file is not a reason to do anything**, and we were
initially wrong to assume it would decide this.

They were rejected because they **freeze a snapshot of the asset library into
every scenery file**. A file written today says "United has an A320 and a 737
here". Ship a United A350 next year and no existing airport will ever use it —
not until every one of them is re-opened and re-saved. At Gateway's scale that is
not a migration, it is a permanent loss.

The same argument kills them for third-party livery packs: a pack would be
invisible to every airport already authored, which removes any reason to make one.

### 8.2 Why C was rejected outright, sharing and all

C solves a real problem on paper. **83%** of stands share their airline list with
four or more stands at the same airport, **53%** with sixteen or more; a hub author
editing one terminal should not touch eighty stands. C also still stores the
resolved result, so it inherits the freezing problem in full — that alone rules it
out.

Drafts 1–5 of this document kept C's *sharing mechanism* and put D's content
inside it: named policies, defined once and referenced per stand. **That was
wrong, and it was wrong because we carried the 83% figure across without
re-measuring it against D's much smaller payload.**

Sharing pays in proportion to the size of the thing shared. Under C a stand's
payload was a resolved cross-product — large, and worth naming once. Under D a
stand's payload is **six integers**. Measured against the real global apt.dat
(380 MB, 42,275 stands carrying airlines, 3,910 airports):

| | what it costs |
|---|---|
| **inline only** — `1312`/`1313` written at each stand | **0.85 MB** |
| grouped, definition carries the airline list | 1.31 MB |
| grouped, definition carries only weights and refinements | 0.92 MB |

(Those figures were measured while the format still had two rows. Draft 7 dropped
`1312` entirely, so the real inline cost is now lower than 0.85 MB and the case
against grouping only widens. The numbers are left as measured rather than
rescaled, because the conclusion never depended on their size.)

**Grouping is larger, not smaller** — every variant of it. A reference row costs
roughly what the six integers cost, so you pay the per-stand price anyway and add
the definitions on top. The break-even is a group of **more than 6.8 stands**, and
the mean group is 4.7.

Size was never the deciding factor and is not now — a megabyte on a 380 MB file
decides nothing. What matters is that grouping bought **no measurable benefit** for
its real cost in reader complexity: a resolve pass, block scoping, dangling
references, an inline-vs-referenced special case, and a redundant airline list kept
in sync by a writer-enforced invariant. Six normative rules existed only to hold
that machinery up. They are gone.

The diff argument fell too. Grouping was supposed to make a shared edit one line
instead of eighty, but names were content-derived precisely so that unrelated
insertions would not renumber everything. Those two properties are incompatible:
change a policy's content and its hash changes, so the definition **and every
reference to it** change — eighty-one lines instead of eighty. Content-addressed
names make edits more churn, not less.

What survives from C is nothing structural. The lesson it leaves is the one above:
a compression scheme has to be measured against the payload it will actually
compress, not the payload that motivated it.

### 8.3 Why D won

It stores what the author actually decided — airlines, class weights, and the rare
exception — and lets the sim resolve that against whatever is installed at the
time. New models and third-party packs appear at every relevant airport with
**zero apt.dat edits**. Its size is bounded by author behaviour rather than by
library growth.

It also matches the UI: the author's default is "any aircraft this airline
operates that fits", and the only available action is to remove one. Storing an
expanded list would mean WED writing out a computed answer the author never gave.

One honest cost: **it is not self-explanatory.** Reading apt.dat alone will not
tell you what parks at a stand; you need the index too. Acceptable, because
tooling can show it — WED does, and an export report could — while freezing
cannot be undone by tooling.

**Draft 7 adds the symmetric half: the design can now remove, not only add.** A
row marked `Obsolete` (§6.6) stops being chosen at every airport in the world,
with zero apt.dat edits — the same late-binding mechanism as "a model ships and
it just appears", run backwards. This is the first time anything in the design
withdraws rather than supplies, and it is worth stating plainly because it
changes the behaviour of existing scenery without changing any file. That is
acceptable for the same reason the forward direction is: the author wrote intent,
not a resolved answer, and an aircraft its operator retired was never part of
what they meant.

### 8.4 Defects found in the shipped library

By-product of building the index, offered as data rather than complaint:

- **38** cases where an old and a new asset for the same aircraft are both
  `EXPORT_EXTEND`ed (`AT45_FDX_static.obj` and `ATR42-500_FedEx.obj`). These are
  the §6.3 duplicate-key groups and they double those aircraft's spawn
  probability under §4.1 stage 3.
- **6** objects on disk that `library.txt` never exports.
- `heavy/B772_AAL/` and `heavy/B772_AAl/` — byte-identical folders, ~13 MB wasted,
  whose two `library.txt` lines point at **different** paths. Fine on Windows and
  macOS, broken on a case-sensitive filesystem.

### 8.5 Why we believe the abuse question is closed

A Gateway submission is validated and then **re-emitted from WED's object model**
(`WED_GatewayExport.cpp:498`, then `:543`), not forwarded as the author's bytes.
Whatever someone puts in a file either fails to parse — in which case WED refuses
the import, or from 2.8 skips an unknown row and says so — or becomes a typed
value that WED re-serialises in its own format.

Combined with §7.2, where even a genuinely corrupt row leaves X-Plane saying "the
scenery may not look correct" rather than failing, we could not find a path from a
hand-edited local file to a crash on anyone else's machine.

One way through existed and is now closed: `CorrectAirlinesString` collapsed only
the literal space character, so a newline — reachable from a hand-authored
document, where the value is an XML attribute — travelled through `fprintf("%s")`
(`AptIO.cpp:1595` today) and appeared in the exported file as **a row of its own**.
Fixed on our side; R9 exists so it is not reintroduced in another field.

### 8.6 Why per-stand type refinement was dropped

Drafts 1–6 carried a second row, `1312`, letting an author write `-ual:B744` or
`+uae:A388`: this airline, but only (or never) that aircraft. Draft 7 deletes it
outright, along with R21, R22 and eight conformance vectors.

**It was not paying for itself.** Measured against the shipped index - the 254
rows of 298 that are not `Obsolete`, schema 4, data `20260925` - counting real
operators and ignoring the `XP*` pseudo-codes (draft 7's count, over all 298 rows
before the marks, was 168 / 15 / 148 / 134):

| | |
|---|---|
| (operator, class) pairs in the index | 144 |
| …carrying **more than one** aircraft type | **12 — 8.3%** |
| real operators in the index | 132 |
| …whose assets sit at **exactly one** class | **122 — 92.4%** |

An aircraft type only ever appears in its own wingspan class, so naming a type
already names a class — which means the size slider that every stand has anyway
already pins the aircraft for nine operators in ten. `1312` bought discrimination
*within* a class, and within-class is where the library almost never offers a
choice.

**What it was actually being used for was a library defect.** Every worked
example in drafts 1–6 tells the same story on inspection: `-ual:B744` at a 747-era
gate, `-dal:MD82` at a Delta stand, `-fdx:DC10` at a cargo stand. None of those is
an author expressing a preference — they are all authors working around an
aircraft the operator no longer flies, one stand at a time, at every airport in
the world. Stating that fact once in the index (§6.6) is the same answer in the
right place, and it cannot go stale the way a per-stand list does.

**What is genuinely lost**, and it is not nothing: an author cannot say "Delta,
but the A320 rather than the 737" — both are class C and both are current. That
is 12 pairs across the entire library, and in every one of them the operator
really does fly both types. Relative frequency between two current aircraft is
what `EXPORT_RATIO` is for (§4.3), and it belongs to the library, which sees all
airports, rather than to one stand.

This is the same lesson §8.2 records, applied to expressiveness instead of size:
**measure a mechanism against the payload it will actually operate on**, not the
payload that motivated it. Grouping died because the thing it shared turned out to
be six integers. Refinement died because the thing it discriminated turned out to
be, nine times in ten, a single aircraft.

### 8.7 Why the range rule lives in the index and nowhere else

Six ways to keep United's 737 out of Beijing were tried against the shipped data
before 6.7 was written. In the order they died:

1. **Zero the stand's class-C weight.** Removes every class-C aircraft at the
   stand, including the Chinese 737s that belong there. Cure worse than disease.
2. **Class as a proxy for range.** Class C runs from 1,500 km (AT45) to
   14,260 km (GL7T); a class-B Challenger out-ranges most class-C airliners.
   Wingspan and fuel fraction are unrelated. Measured against
   `WED_AircraftSizeReference.txt`, not assumed.
3. **Remove the operator from the stand's `1301` list.** The constraint is per
   (operator, type); the list is per operator. At a class-D stand United's 757
   fails and its 767 passes — same class, same operator. Seven of the operators
   actually listed at Beijing are mixed this way; on a single-class D stand,
   five. No per-operator action is correct for any of them.
4. **library.txt `REGION_RECT`.** Existing sim feature, no apt.dat change — but
   not honoured for `apt_aircraft` today, and a rectangle is a geometric
   approximation of a commercial fact that can never be made exact. Also
   authored in a file the generator regenerates.
5. **A per-stand `1312` deny row.** Draft 6's row, deleted in draft 7 (8.6) as
   micromanagement; re-proposed as machine-owned and hidden. Still a copy of a
   computed answer written into every stand.
6. **A per-airport deny row.** Same, collapsed to one line per airport. Still a
   cache of the rule's result in tens of thousands of files that cannot be
   refreshed together; stale the day the index changes.

What survives is the observation that every input the rule needs already exists
on both sides: the stand position has always been in the `1300` row, and the
index already carries the operator per livery. The two facts that were missing —
range of the type, hubs of the operator — are properties of the livery row, so
that is where they went. Nothing is derived, nothing is stored twice, and every
existing apt.dat is covered the day the index is.

---

## 9. Open questions

1. **Row code confirmation.** `1313` is provisional, pending Jim K. **One code,
   not two** — dropping per-stand refinement (§8.6) gave back the one that
   carried them, as dropping the grouping machinery before it (§8.2) gave back
   two more. Four, to two, to one. **Any single unused code works.**

   The code is **live**: WED 2.8 writes it on every export aimed at X-Plane 12,
   Gateway included. Earlier drafts held that nothing should leave this machine
   carrying an unconfirmed number; that was superseded on 2026-09-25. X-Plane
   ignores unknown rows (§7.2) and WED 2.8 now does too (R15), so a file
   carrying a code that later changes still loads everywhere. The
   reader, the writer, the entity property and the editor all use the one named
   constant in `AptDefs.h`, so an answer changes one line. What a different
   answer would still cost is every file already written with `1313` before the
   change, whose weights would then be ignored until re-exported — which is why
   the confirmation matters before 2.8 is released.
2. **Version policy.** §7.2 removes the compatibility argument for a bump, so this
   is now a question about signalling intent. Our recommendation: **no bump.**
   The rows ride in a `1200` file today (`AptIO.cpp:1424`).
3. **A process decision that is not one person's**: §7.3. WED 2.8 opens any
   file that carries rows it does not know, but WED 2.7.x and earlier still
   refuse one. If a bump happens as well, new WED writes a version old X-Plane
   refuses, while Gateway serves both. Who generates which version for whom?
   This needs Jim and the release manager.
4. **Gateway's airline-string cap.** The `1301` airline string must be under 100
   characters for a Gateway submission — about 24 three-letter codes (R10). A
   busy hub is served by more airlines than that, and auto-fill stops short. The
   Gateway team should say whether it can be raised, and to what, and confirm the
   server's own validation accepts R10's suffixed codes (`ryr_1`, `afr_f`,
   `xpzz_b752`).

**Closed since draft 6 — a duplicate `1313` on one stand.** The behaviour is R24:
the first in file order wins and the rest are discarded whole.

Draft 7 recorded a cost here: WED's parser (`AptIO.cpp`) had no concept of a
non-fatal diagnostic, so a hand-written second `1313` vanished without a word,
and adding a warning channel seemed to mean changing a parser shared with
MeshTool, DSF2Text and RenderFarm. **That cost is gone.** The reader now records
every row it skips - an unknown row code, a duplicate or malformed `1313` - on
the airport it belongs to (`AptInfo_t::discarded_rows`), without changing any
function signature, so the other tools are untouched. WED lists them after the
import, again before the first save (which is what makes the loss permanent),
and as the validation warning `warn_apt_dat_rows_not_imported`. The file still
loads, as it must.

Nothing propagates in any case: a Gateway submission is re-emitted from WED's
object model rather than forwarded as the author's bytes (§8.5), so the second
row never reaches anyone else.

---

## Appendix — current status

As of 2026-09-25, on branch `feature/ramp-livery-picker`, Windows build. Draft
7's appendix said `WED_LiveryIndex` had no consumer and the preview strip showed
placeholder objects; both are long out of date and the section is replaced by
this table.

| piece | state |
|---|---|
| Read and write `1313` (`AptIO.cpp`), entity property, weights mode (§6.8) | **done** — written on every X-Plane 12 export, Gateway included |
| Reader robustness: unknown rows skipped and reported (R15, §9) | **done** — listed after import; `warn_apt_dat_rows_not_imported` |
| `livery_index.txt` schema 4 — 298 liveries, 1,499 operator records, header, hubs | **done**; generator merges, never overwrites (§6.7b) |
| Index reader (`WED_LiveryIndex`), hub placement from Global Airports | **done** — drives every livery feature below |
| Liveries tab: operator cards with real aircraft previews, size range / weights, flags, recommendations | **done** |
| Coverage readout: P(empty), per-operator share, "only X will ever park here" (§4.5) | **done** |
| R25 `Obsolete`, R26 range, R27 `HOME` in WED's `eligible()` | **done** — same predicate for cards, coverage and validator |
| Auto-fill (§6.7f) | **done** |
| Moderation: stepping through stands, notes, web search (§6.7f) | **done**; the moderator preference switch is not yet wired |
| Validator: R10 code shape, Gateway 100-character cap, R14 `warn_ramp_livery_parks_nothing` | **done** |
| Sample package `docs/livery_sample/` — ZBAA, 24 stands incl. the range cases | **done**; README regenerated from `livery_sample_expect.py` (2026-09-25) |
| **X-Plane: R17/R18 three-stage selection, R25–R29, reading the index** | **open** — the sim side of this spec |
| **Index shipped with X-Plane 12.5 under `apt_aircraft/`** | **open** — Laminar release |
| **Mac and Linux build and run** | **open** — never done |

The design principle the index exists to serve is unaffected, and is the same one
as D in §8.3: availability is **recomputed on every load and never persisted**, so
a model shipped later simply starts appearing.
