# apt.dat row 1313: per-stand fleet weighting

**Specification and implementation manual.** Draft 7, 2026-09-17. Targets WED 2.8.0.
For the X-Plane side of WED's ramp livery picker.

---

## How to use this document

This is the long form, written to be complete rather than readable. It is meant
to be handed to an assistant alongside the X-Plane apt.dat reader, and it is
structured so that can be done mechanically:

| § | contains | use it for |
|---|---|---|
| 1 | Normative rules, numbered `R1`… | the checklist an implementation is graded against |
| 2 | ABNF grammar | writing the parser |
| 3 | Reader algorithm | what to do with each row |
| 4 | Selection algorithm | the runtime behaviour, which is the real change |
| 5 | Conformance vectors | tests, including every malformed case we could construct |
| 6 | The livery index | the companion data file, and what it does and does not guarantee |
| 7 | Evidence | measurements and the compatibility test, with method |
| 8 | Rationale | why this shape, and not the three candidates or the two rows we dropped |
| 9 | Open questions | what is not decided |

**The human-sized version is `WED_LiveryFormat_Brief.md`.** It has the three
decisions and nothing else. If you are Jim, read that one.

### Conventions

MUST / MUST NOT / SHOULD / MAY are used in the RFC 2119 sense. Every normative
statement carries an `R`-number so an implementation can be checked against a
list rather than against prose. Line references of the form `AptIO.cpp:1208` are
into the WED/xptools tree at branch `feature/ramp-livery-picker`.

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
  and aborts the whole load (`AptIO.cpp:1208`, `:1217`). These rows are
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
  does. `1301` already had this constraint: `AptIO.cpp:740-743` rejects it
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
  (`AptIO.cpp:743`). R4 forbids that here — a duplicate must not fail the file —
  so first-wins is the only rule that satisfies both. See §9 for what is given up
  by not reporting it.

### Writer-side

Constraints on whoever produces the file, not defences the reader must implement.
A reader that has to defend against all of these is a reader nobody implements
correctly.

- **R9** — No whitespace other than a single U+0020 SPACE inside any field.
- **R10** — Airline codes match `[A-Z0-9]{3,5}`. Anything else is dropped by the
  writer, not emitted for the reader to police.
- **R11** — Weights are non-negative integers in `0..1000`.
- **R14** — A writer MUST NOT emit weights pointing exclusively at classes that no
  listed airline can fill. WED treats this as a hard export error (§4.5).

  **Two situations look identical here and must not be treated alike**, which the
  “Emirates A380 gate” case makes concrete: the library ships **no class-F livery
  at all** today, so an author building that gate writes a weight nothing can
  currently satisfy.

  | the class is unfillable because… | verdict |
  |---|---|
  | the listed airlines have liveries at other classes but none here | **error.** The weights are wrong and one click fixes them |
  | **no asset exists at that class anywhere in the library** | **warning.** The author is ahead of the art, and late binding is the whole design |

  Blocking the second would make it impossible to author for an aircraft that is
  coming, which is exactly the capability §8.3 argues the format exists to
  preserve. The validator therefore asks "can these airlines fill it?" only after
  establishing that **something** could.
- **R19** - **apt.dat has no comment syntax. A writer MUST NOT emit comment lines.**
  There is no `#` form, no `//` form, and nothing else. Confirmed against the
  shipped data: **zero** lines begin with `#` in the 12,351,496-line Global
  Airports apt.dat.

  The mechanism is worth knowing, because it is not "the reader rejects `#`" - it
  is quieter than that. `TextScanner_FormatScan(s, "i", &rec_code)` tokenizes the
  line and converts the first token with `atoi()` (`MemFileUtils.cpp:769-800`).
  `atoi("#")` returns **0**, and the function still returns 1 because it counted a
  token. So the caller's `if (FormatScan(...) != 1) { skip }` guard
  (`AptIO.cpp:346`) never fires, the line is processed as **record code 0**, and 0
  falls through to `default:` - `ok = "Illegal unknown record"`, which fails the
  entire file (`AptIO.cpp:1209`).

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
  ignored rather than rejected.
- **R17** — Three-stage selection (§4) applies **only** to stands carrying a `1313`
  row. A stand without one MUST keep today's behaviour, unchanged.
- **R18** — **Stage 2 and stage 3 MUST ask the index the same question.** An
  airline with no usable livery in the class just drawn is not a stage-2
  candidate at all.

  This has nothing to do with refinements and survives their removal. Filtering
  stage 2 on "has any livery at all" while filtering stage 3 on "has one in this
  class" makes an airline a candidate with zero options, so every time it is
  drawn the stand parks nothing. Concretely: Emirates' only E-class aircraft is
  the 777, so at a stand weighted for class F they must be absent from the pool,
  not drawn and then found empty. Write it as one `eligible()` used by both
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

---

## 2. Grammar

```abnf
; ---- the new row -------------------------------------------------------
weights-row    = %s"1313" 6(1*SP weight) *SP

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

### Rows in context

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

### 2.4 A complete worked example - one airport, every feature

Everything above in one airport block. **This is the reference scenario**: if an
implementation reproduces the resolution table below, it has all of §1-§4 right.

`KXYZ` is fictional, the airlines are real, and every outcome in the table was
computed against the shipped index (`20260916-r1`, X-Plane 12.4.3-r2) rather than
asserted.

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
| `A1`, `A2` | an operator on `1301` with nothing at one of the weighted classes — the R18 case |
| `B1` | a single-class weight: everything here is class C |
| `B2` | same operators as `B1`, **different weights** — the edit that used to mean "leave the group" is now just a different number |
| `R1` | weights concentrated on one class, a different one |
| `CGO1` | weights plus a cargo op type |
| `GA1` | **no new row at all** — today's behaviour, untouched (R17) |
| `F1` | a class the shipped library cannot fill — the R14 warning case, not an error |
| `T1` | weights spanning two classes |

#### What actually spawns

Computed, not asserted:

| stand | class draw | resolves to |
|---|---|---|
| `A1`, `A2` | D 30% | 2 airlines — `B752`, `B763` |
| | E 70% | **4** airlines — `B772` |
| `B1` | C 100% | 8 airlines — `A320`, `AT72`, `B738` |
| `B2` | B 20% | 2 airlines — `CRJ1`, `CRJ2` |
| | C 80% | 8 airlines — `A320`, `AT72`, `B738` |
| `R1` | B 100% | 2 airlines — `CRJ1`, `CRJ2` |
| `CGO1` | D 100% | 2 airlines — `B752`, `B763` |
| `GA1` | — | today's step-down from `A` |
| `F1` | F 100% | nothing **today** — the library ships no F-class livery yet. The day an Emirates A380 ships, this stand starts working with no apt.dat edit |
| `T1` | C 90% | 5 airlines — `A320`, `B738` |
| | D 10% | 2 airlines — `B763` |

`F1` is the one intentional empty, and it is the difference between the two kinds
of emptiness §4.5 cares about: the author asked for something real that does not
exist yet, and late binding will fill it in. A weight pointing at a class **none
of the listed airlines can ever fill** is the error case, and R14 forbids it.

#### The number that shows why R18 exists

`A1` lists five operators and is weighted 30/70 across D and E. United's only
class-E livery in the shipped library is the `B744` — an aircraft they retired in
2017, and one the library should therefore be marking `Obsolete` (§6.6). Once it
is, United has **nothing at E**: `B752` and `B763` at D, and that is all.

| | class E candidates | stand parks nothing |
|---|---|---|
| **With R18** — stage 2 asks "has one at *this* class" | **4** (UAL not a candidate) | **0%** |
| Without — stage 2 asks "has one at all" | 5 (UAL picked, then empty) | **14%** |

Fourteen percent of the time, on a stand whose author did nothing wrong, from a
single missing asset. Note United still parks at D — withdrawing one aircraft
does not withdraw the operator, which is exactly the surgical effect earlier
drafts were trying to buy with a per-stand exclusion row.

#### Things this example is careful about

- **Every stand stands alone.** `A1` and `A2` repeat two identical rows and that is
  fine: it costs about 40 bytes, and §8.2 measures the alternative as both larger
  and more complex.
- **A withdrawn aircraft leaves something behind — usually.** Marking Delta's
  `MD82` obsolete is safe because they also have `A320` and `B738` at C. United's
  `B744` empties their class E outright, and only R18 keeps that from turning into
  an intermittently empty stand. §6.6's blast-radius check exists to tell those
  two cases apart before the mark is made, not after.
- **`GA1` carries no new row.** An airport does not have to adopt this format
  stand-by-stand, and a mixed file is normal.
- **Class A is GA and military only** — no real airline has an A-class livery,
  which is why `GA1` is `general_aviation` rather than an airline stand.
- **No comments.** apt.dat has no comment syntax (R19); the annotation lives here.

### 2.5 One stand, line by line

Stand `A1` from §2.4, pulled apart. Three lines, and two of them already exist.

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
 |    |  |        airline ICAO codes, space separated, case-insensitive
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
1. class   := weighted draw over 0 0 0 3 7 0        -> D 30%, E 70%
2. airline := uniform over those in 1301 that have a livery in the
              class drawn (R18)
                 at E: dal, aal, baw, uae            <- ual has nothing at E
                 at D: dal, ual                      <- ual IS here
3. livery  := uniform over that airline's liveries in that class, skipping
              any marked Obsolete (R25), honouring EXPORT_RATIO if
              library.txt sets one
```

Step 2 is where R18 earns its keep. United's only class-E aircraft in the shipped
library is the 747-400, an aircraft they retired in 2017 — so the library, not
this file, is what should stop it spawning (§6.6). Once it is marked, United
simply is not a class-E candidate anywhere, while remaining one at D where they
have the 757 and 767.

Had stage 2 skipped that check and asked only "does this airline have *any*
livery", United would still be drawn at E, find nothing, and the stand would park
**nothing 14% of the time** (§2.4).

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

## 4. Selection algorithm

**This is the behavioural change.** Everything above is transport.

### 4.1 The three stages

```
if stand has no 1313 row:
    use today's behaviour, unchanged                        # R17

class := weighted_choice(A..F, weights = stand.weights)
         if every weight is zero -> nothing parks here, stop        # §4.2

airlines_in_class := [ a for a in stand.airlines            # from 1301
                         if eligible(a, class) is non-empty ]
if airlines_in_class is empty -> nothing parks here, stop
airline := uniform_choice(airlines_in_class)

liveries := eligible(airline, class)
livery   := weighted_choice(liveries, weights = EXPORT_RATIO or uniform)

where
  eligible(a, class):
      return [ L for L in index.liveries(a, class) if not L.obsolete ]   # R25
```

That is the whole of it. There is no per-stand filter to apply, because the
format no longer carries one: a stand says which airlines and which classes, and
the index says what exists. The only subtraction left is `Obsolete`, and it lives
in the index because it is a fact about the library rather than about this stand
(§8.6).

If a stage has no candidates, nothing parks at that stand this time. That is a
correct outcome, not an error — see §4.5.

**Both stages MUST call the same `eligible()` (R18).** Filtering stage 2 on "has
any livery at all" while filtering stage 3 on "has one in this class" makes an
airline a candidate with zero options, so every time it is drawn the stand parks
nothing. Concretely, in the sample package: BAW's only C-class livery is the
A320, so at a stand weighted for class D they must be absent from the stage-2
pool, not drawn there and found empty.

Writing it once and calling it twice makes the two impossible to drift apart.
Drafts 1–4 had the flattened form and the bug; it was found by building the
sample in `docs/livery_sample/` and tracing one stand by hand. The rule outlived
the refinements it was originally written for, because it was never really about
them — it is about the index not having what the author assumed.

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

Writing all six rather than only the non-zero ones removes the "is an absent
class zero, or unspecified?" ambiguity entirely, gives the row a fixed shape that
is cheap to validate and hard to tamper with, and costs 12 characters — about
175 KB across every weighted stand in the global apt.dat.

**Relative integers, never decimals.** `0 1 1 8 0 0` is the same distribution as
`0 0.1 0.1 0.8 0 0`, and the editor shows the author percentages either way — but
the file MUST NOT contain a decimal point. WED calls `setlocale(LC_ALL, "C")`
only inside `#if LIN` (`WED_AppMain.cpp:231`), and that one call is all that
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
replacement sitting beside it. **They empty zero (airline, class) pairs**, so the
skew disappears and nothing becomes unfillable.

The thirteenth pair is not one: `F15EX_cft` / `F15EX` are genuinely different
airframes (conformal tanks) that the index cannot currently tell apart. That is
an index fix by hand on the NOTE column, not an obsolescence mark — see §6.3.

**The danger of this mechanism is its reach.** A mark is global and has no
per-stand undo: the livery stops existing at every airport at once, and an
(airline, class) pair left with no asset is a stand that silently parks nothing —
the §4.5 defect, created by a data edit. Marking `B752` would empty **twenty**
pairs, and the 757 is in daily service. The generator therefore prints the blast
radius of every mark on every run, and answers `--what-if <TYPE>` without
requiring the mark to be made first. Read that number before committing a row.

### 4.5 What class-first costs, and who absorbs it

Class-first is strictly more expressive, but it gives up the one virtue the
step-down had: it always found *something*. Four consequences.

**1. A stand can now be silently empty, and this is common.** Measured against the
real global apt.dat and the real shipped livery set: if every stand's weights were
set to the class it already declares, **7,604 of 44,242 stands (17.2%) would spawn
nothing at all**, touching **1,675 of 3,958 airports (42%)**. The cause is almost
never "this airline has no models" — only 1.8% of stands are that. It is "this
airline has no model **in this stand's class**". A gate marked E whose airlines fly
C-class aircraft is the common case, and today the step-down hides it by quietly
substituting something smaller.

> **WED absorbs this.** Weights pointing at a class none of the listed airlines
> can fill is a hard export error — not a dismissible warning — with one-click
> repair offered at the point of failure, **and, before that, a continuous
> readout of P(empty) while the author edits.** **The sim SHOULD NOT add a
> fallback.** An empty stand is the correct reading of what the author wrote, and
> re-introducing a step-down takes back the expressiveness this change exists to
> provide.

#### Where the 17.2% actually comes from, and what answers it

The figure is **not** a property of the format, and it is not a hazard of hand
authoring. R17 makes that precise: a stand with no `1313` keeps today's
behaviour, so **importing an existing apt.dat creates no weights and therefore no
empty stands.** The 17.2% is conditional — "*if* every stand's weights were set to
the class it already declares" — and the only thing that would do that at scale is
**WED's own one-click fill**, which is specified as a bulk operation the author
accepts wholesale, across up to 326 stands at a single airport.

So the exposure is concentrated exactly where no human is inspecting individual
stands. That splits the answer in two, and both halves are required:

| path | answer |
|---|---|
| an author editing one stand | **live per-stand readout**, updated on every change |
| bulk fill across an airport | **airport-level rollup**, presented at the moment the fill completes |

A rollup reads: *"47 stands filled. 12 will be empty more than half the time, 8
always."* Without it, 42% of commercially served airports acquire the defect
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
   wording must survive that.** `XPMI` carries 11 rows of which 6 have no country
   at all; `XPGA` carries 51 across 11 countries, 28 of them USA. Naming three
   countries is the most these sentences can honestly do today.

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
(a mistake), everything excluded (a mistake). Only the first should ever reach a
released file — that is what the validator is for.

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
whatever the current step-down does with it.

### 5.1 Well-formed

| # | input | expected |
|---|---|---|
| V1 | `1313 0 0 10 0 0 0` on a stand | class C always; stages 2–3 then run |
| V2 | `1313 0 0 0 0 0 0` | nothing parks. **Legal**, not an error (R5) |
| V6 | `1313 1000 0 0 0 0 0` | legal; 1000 is the ceiling (R11) |
| V26 | `1313 0 0 0 0 0 10` where the only listed operator has nothing at F | operator is **not** a stage-2 candidate; the stand parks nothing. Correct behaviour, and the R14 warning case for a writer |
| V30 | `1301 A airline dal` with `1313 0 0 0 0 9 1` | reader: selection uses the weights, the letter is not consulted. Writer/WED: correct the letter to `F` (R23) |
| V31 | an index row for the drawn (airline, class) whose NOTE is `Obsolete` | that row is not a stage-3 candidate, and does not make its operator a stage-2 candidate either (R25) |

V26 is the case most likely to be got wrong, and it is the one R18 exists for: an
operator with no asset at the class drawn must be absent from the pool, not drawn
and then found empty.

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
| V18 | a `1314` row | ignored (R15) |
| V19 | an embedded tab inside an airline list | writer defect (R9); reader treats it as a field separator, which may yield an unparseable token — drop that token, keep the row |
| V20 | file at version `1200` containing a `1313` row | loads; the row applies (§7.2 — no version gate) |
| V32 | `1313` appearing before any `1300` in the airport block | drop the row — there is no stand to attach it to (R20) |

V17 is new in draft 7 and is the one with a decision behind it rather than a
mechanism: see §9 for what taking the first costs.

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

### 6.2 Format and location

Lives next to the assets it describes:
`Resources/default scenery/sim objects/apt_aircraft/livery_index.txt`.

```
<TYPE> *** <CLASS> *** <AIRLINE> *** <REG> *** <REG COUNTRY> *** <NOTE> *** <path>
```

```
B738 *** C *** UAL  *** N78540  *** USA *** Retro   *** jet/B738_UAL/B738_UAL_Legacy.obj
B752 *** D *** UAL  *** N48127  *** USA *** Default *** jet/B752/757_United_Modern_N48127.obj
```

- `CLASS` is the ICAO wingspan class `A`–`F`, the same vocabulary as `1301`'s
  letter and `1313`'s six slots.
- `REG COUNTRY` is an **IOC** code, not ISO — so `HKG`, `TPE`, `MAC`. `???`
  means unresolved.
- `NOTE` is **free text and an open vocabulary**, not an enum. WED renders
  anything other than `Default` in parentheses after the airline name — "United
  (Retro)", "Air China (Peony)". A closed vocabulary here would throw away
  exactly the variants nobody thought to enumerate; we shipped that bug once and
  will not again.

Header carries three versions, deliberately separate:

```
# schema 1                          how many columns and what they mean
# data 20260916-r1                  which day's content this is
# source X-Plane 12.4.3-r2-15ff1e4d WHICH INSTALL this describes
# assets 298 liveries under apt_aircraft/
```

Folding schema and data into one would force a reader to write
`if (date >= 20260916)`, which is wrong both ways: the layout can change twice in
a day, or not change for a year.

### 6.3 What the index does NOT guarantee — read before implementing stage 3

**`(type, airline, note)` is not a unique key, and cannot be made into one.**
Verified against all 298 rows: **26 groups covering 83 rows (28%) share that
tuple.** Adding registration narrows it to 14 groups / 30 rows. It does not close
it. **The only unique key is the object path.**

The 26 groups have three distinct causes, and they need different responses:

| cause | groups | rows | what to do |
|---|---|---|---|
| **Old and new asset both exported** for one real aircraft — `AT45_FDX_static.obj` *and* `ATR42-500_FedEx.obj` | 13 | 26 | **Mark the old one `Obsolete`** (§6.6). Twelve are marked; the thirteenth is the `F15EX` pair below, which is not a duplicate at all. Each unmarked pair doubles that aircraft's spawn probability (§4.4) |
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

Verified mechanically against the shipped file; a generator change that breaks one
of these is a regression.

- **I1** — Every row has exactly seven `***`-separated fields.
- **I2** — `path` is unique across the file, and is the only unique key (§6.3).
- **I3** — Every `path` resolves to a real `.obj` under `apt_aircraft/`. All 298
  verified.
- **I4** — Every `.obj` has a resolvable texture, via the `TEXTURE` directive
  inside the object, with `.dds`/`.png` substitution. All 298 verified.
- **I5** — `CLASS` is always one of `A`–`F`, never blank. `AIRLINE` is never
  blank.
- **I6** — `REG`, `REG COUNTRY` and `NOTE` MAY be blank or `???`. A consumer must
  tolerate all three.
- **I7** — The file is manually maintained. Re-running the generator is a
  diff-review, never a blind overwrite. 114 registrations came from an OCR pass
  and are not yet human-verified; they are marked in the source tree.

I3 and I4 are the ones worth re-running in CI. They are the only way the
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

**Two things it is for.**

1. **Superseded assets.** An old and a new object for the same real aircraft are
   both exported, so that aircraft draws double probability (§4.4). Twelve such
   pairs are marked today and they empty no (airline, class) pair.
2. **Retired airframes.** The operator no longer flies the type, so a stand
   listing them should not park one. United's 747-400 is the clean case: retired
   in 2017, and it is their only class-E asset in the library — which is exactly
   why earlier drafts needed a per-stand `-ual:B744` at every 747-era gate. The
   mark states the fact once, globally, where it is true.

**Marks live in `livery_obsolete.txt`, never in the index directly.** The index is
generated; a hand edit to it is wiped on the next regeneration. The sidecar is
keyed by object path and merged by the generator, exactly as
`livery_reg_overrides.txt` already does for hand-read registrations.

**A mark's blast radius must be read before it is made.** Withdrawing a livery is
global and has no per-stand undo, and an (airline, class) pair left with no asset
is a stand that silently parks nothing — the §4.5 defect, manufactured by a data
edit. The generator prints, for every mark, how many pairs it would empty, and
answers the question hypothetically without requiring the mark:

```
obsolete marks        : 12 rows
  would empty         : 0 (airline,class) pair(s)
what-if B752          : 36 rows, would empty 20 pair(s)  AHY/D ATN/D AZV/D ...
what-if MD82          :  6 rows, would empty  2 pair(s)  AZA/C SAS/C
```

The 757 is in daily service; twenty emptied pairs is what one careless line
costs. Emptying pairs is not always wrong — a genuinely retired type *should*
empty them — but it must be a decision someone made with the number in front of
them.

---

### 6.7 Schema 2 — the range rule

**The problem it answers.** Stage 3 chooses among an operator's liveries in the
drawn class with nothing but `EXPORT_RATIO` to go on, so a stand at Beijing that
lists United draws United's 737 as readily as its 777. The 737 cannot reach
Beijing from anywhere United flies it. This is the one systematic absurdity the
three-stage design still produced, and it is a property of the *aircraft and the
operator*, not of the stand — so it does not belong in apt.dat, and R23/R25's
argument applies again: the row that describes the livery is where the fact goes.

**Two columns, inserted before `path`:**

```
<TYPE> *** <CLASS> *** <AIRLINE> *** <REG> *** <REG CTY> *** <NOTE> *** <RANGE_KM> *** <HUBS> *** <path>

B738 *** C *** UAL *** N79521 *** USA *** Default ***  5700 *** 41.98,-87.91 29.98,-95.34 37.62,-122.38 *** jet/B738_UAL_Modern/...
B744 *** E *** UAL ***        ***     *** Default *** 13450 *** 41.98,-87.91 29.98,-95.34 37.62,-122.38 *** heavy/B744_UAL/...
```

- `RANGE_KM` — typical operating range of `TYPE` at a realistic payload, in km.
  Not the ferry figure. A physical constant; it is authored once in
  `WED_AircraftSizeReference.txt` and never revisited.
- `HUBS` — the operator's hub positions as `lat,lon` pairs, space separated,
  two decimals. Resolved by the generator from hub ICAOs kept in
  `WED_AirlineDirectory.txt` against Global Airports, so **the sim receives
  numbers and needs no airport lookup at spawn time**, while the hand-edited file
  keeps codes a human can check at a glance.
- Either column empty means *unknown*, and **unknown is never filtered**.
  Military and generic pseudo-codes have no hubs; a type without a researched
  range has no figure. Both fall through the rule untouched. A missing fact must
  never hide a livery (same posture as R4/R5: fail open).

**The rule — R26.** At a stand, for each candidate row the drawn class and
operator admit:

```
if RANGE_KM is empty or HUBS is empty        -> eligible
d = min over HUBS of greatcircle(hub, stand)   -- stand from the 1300 row
if d > RANGE_KM                              -> not a candidate
else                                         -> eligible
```

If the filter empties an operator's set in the drawn class, stage 3 has nothing
to choose and the stand stays empty for that draw — exactly the outcome R18
already defines for an operator with no livery at that class. No new failure
mode is introduced.

**What is deliberately not in apt.dat.** Nothing. The inputs the rule needs are
the stand's own position, which every `1300` row has always carried, and two
facts about the livery, which live in the index. Every apt.dat in existence
therefore gains the behaviour the day the index does, with no re-export — and
there is no derived copy of the answer anywhere to go stale. (A per-airport
"deny list" row was considered and rejected on exactly that ground: it is a
cache of this computation written into tens of thousands of files that cannot
be refreshed together. See 8.7.)

**WED evaluates the same predicate.** `WED_LiveryInRange()` reads the same two
columns and the same stand position (`GetLocation`, i.e. the number the `1300`
row is written from) and removes the same rows from the preview cards and the
coverage count. The two sides cannot disagree because there is no second copy
of anything. The readout names what was removed — "Beyond range from their hubs,
so not offered here: UAL B738/A320" — so the author is not left inferring why a
card shrank.

**Schema.** The header stamp is `# schema 2`. `path` is guaranteed to be the
**last** column in every schema, which is what lets a schema 1 reader that takes
the path from the end keep working, and lets a schema 2 reader accept a schema 1
file (seven cells: both optional columns absent, nothing filtered).

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
other; the same holds for FedEx (757/DC-10 vs 767) and UPS. The test bench in
`docs/livery_sample/` stands 24 and 26 exist to make this failure visible.

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

That is the opposite of WED, which whitelists versions (`AptIO.cpp:313-320`),
rejects unknown row codes (`:1208`), and aborts the entire load on either. Row
`1301` shipped gated at version 1050 (`:738`), which is what led us to assume the
sim gated too.

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

### 8.6 Why per-stand type refinement was dropped

Drafts 1–6 carried a second row, `1312`, letting an author write `-ual:B744` or
`+uae:A388`: this airline, but only (or never) that aircraft. Draft 7 deletes it
outright, along with R21, R22 and eight conformance vectors.

**It was not paying for itself.** Measured against the shipped 298-row index,
counting real operators and ignoring the `XP*` pseudo-codes:

| | |
|---|---|
| (operator, class) pairs in the index | 168 |
| …carrying **more than one** aircraft type | **15 — 8.9%** |
| real operators in the index | 148 |
| …whose assets sit at **exactly one** class | **134 — 90.5%** |

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
is 15 pairs across the entire library, and in every one of them the operator
really does fly both types. Relative frequency between two current aircraft is
what `EXPORT_RATIO` is for (§4.3), and it belongs to the library, which sees all
airports, rather than to one stand.

This is the same lesson §8.2 records, applied to expressiveness instead of size:
**measure a mechanism against the payload it will actually operate on**, not the
payload that motivated it. Grouping died because the thing it shared turned out to
be six integers. Refinement died because the thing it discriminated turned out to
be, nine times in ten, a single aircraft.

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
the import — or becomes a typed value that WED re-serialises in its own format.

Combined with §7.2, where even a genuinely corrupt row leaves X-Plane saying "the
scenery may not look correct" rather than failing, we could not find a path from a
hand-edited local file to a crash on anyone else's machine.

One way through existed and is now closed: `CorrectAirlinesString` collapsed only
the literal space character, so a newline — reachable from a hand-authored
document, where the value is an XML attribute — travelled through `fprintf("%s")`
(`AptIO.cpp:1475`) and appeared in the exported file as **a row of its own**.
Fixed on our side; R9 exists so it is not reintroduced in another field.

---

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

## 9. Open questions

1. **Row code allocation.** `1313` is a proposal. **One code, not two** —
   dropping per-stand refinement (§8.6) gave back the one that carried them, as
   dropping the grouping machinery before it (§8.2) gave back two more. Four, to
   two, to one. **Any single unused code works.**

   Earlier drafts called this "the only item blocking WED-side implementation".
   That was wrong, and the WED side proved it by shipping: the reader, the
   writer, the entity property and the editor were all built with the code as a
   single named constant in `AptDefs.h`, so an answer changes one line. What is
   actually blocked is **distributing a file anyone else will read** - a Gateway
   submission, a public build - because a provisional code written into scenery
   that other people open is the one mistake §8.1 calls globally fatal.
2. **Version policy.** §7.2 removes the compatibility argument for a bump, so this
   is now a question about signalling intent. Our recommendation: no bump.
3. **A process decision that is not one person's**: §7.3. If a bump happens, new
   WED writes a version old X-Plane refuses, while Gateway serves both. Who
   generates which version for whom? This needs Jim and the release manager. It
   has no answer today and it is the only part of this with no technical
   solution.

**Closed since draft 6 — a duplicate `1313` on one stand.** The behaviour is R24:
the first in file order wins and the rest are discarded whole. Recorded here
rather than silently, because it comes with a cost that is worth being explicit
about.

A reader that repairs ambiguous input should say it did. This one cannot, and the
reason is structural rather than an oversight: WED's parser (`AptIO.cpp`) has no
concept of a non-fatal diagnostic — a problem sets `ok = "Illegal …"` and aborts
the whole file, which R4 forbids for this row. Adding a warning channel means
changing a parser shared with MeshTool, DSF2Text and RenderFarm, for a defect
that no shipped file has ever contained, since `1313` does not exist in the wild
yet. Every check that *can* run against WED's object model goes into the existing
validator instead — which has the better reach anyway, because Gateway already
runs it on submission (`WED_GatewayExport.cpp:498`). But a duplicate row is gone
by the time that validator sees anything: the parser took the first and the
second never became a value.

So the honest statement is: hand-write two `1313` rows and one of them vanishes
without a word. That is a deliberate trade, not a gap. The person who wrote them
went around the editor and knows it; the next author will never see that anything
was dropped; and nothing propagates, because a Gateway submission is re-emitted
from WED's object model rather than forwarded as the author's bytes (§8.5).

If it ever needs recovering, the cheap shape is a count on `AptGate_t` — "N rows
discarded while parsing" — which reaches the validator without touching the
parser's signature or any other tool.

---

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

## Appendix — what is already built on the WED side

So the shape above is not speculative. **Stated at the level it has actually been
verified**, because the distinction matters to anyone estimating the remaining
work:

**Shipped and running:**

- **`livery_index.txt`** — 298 shipped static-aircraft liveries, each with ICAO
  type, wingspan class, operator, registration, country of registration and a
  free-text livery note. Generated from `library.txt` plus the asset tree, then
  maintained by hand.
- **The Liveries tab** — airline selection with tri-state multi-select, the size
  range control, the flag pipeline, and the airport recommendation list. These
  read `WED_AirlineDirectory.txt` and `WED_AirportDatabase.txt`, both of which are
  loaded and queried on every selection change.
- **Round-tripping of `1301`** — size letter, operation type and airline list are
  edited on the tab and survive import and export through the existing path.

**Written, audited, but never executed:**

- **`WED_LiveryIndex`** — the loader, the (airline, class) query used by stage 2,
  the availability tri-state and real-path object loading are all implemented and
  read through, but the class **has no consumer**. Nothing in WED instantiates it;
  the only call into its header is `WED_LiveryIndexDefaultPath()`, used as a cache
  key for discarding thumbnails when the X-Plane root changes
  (`WED_LiveryPane.cpp:2728`). The preview strip still renders placeholder objects
  picked from the library with literal captions (`PickPlaceholderObjectVpaths()`,
  `:835`).

Everything the index is claimed to do above is therefore verified **by reading,
not by running.** The first consumer will be the probability readout of §4.5,
which is what turns "this file parses" into "these numbers are right".

The design principle the index exists to serve is unaffected, and is the same one
as D in §8.3: availability is **recomputed on every load and never persisted**, so
a model shipped later simply starts appearing.
