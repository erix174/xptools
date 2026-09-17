# apt.dat rows 1310–1313: per-stand fleet and livery data

**Specification and implementation manual.** Draft 5, 2026-09-16. Targets WED 2.8.0.
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
| 8 | Rationale | why this shape and not the three we rejected |
| 9 | Open questions | what is not decided |

**The human-sized version is `WED_LiveryFormat_Brief.md`.** It has the three
decisions and nothing else. If you are Jim, read that one.

### Conventions

MUST / MUST NOT / SHOULD / MAY are used in the RFC 2119 sense. Every normative
statement carries an `R`-number so an implementation can be checked against a
list rather than against prose. Line references of the form `AptIO.cpp:1208` are
into the WED/xptools tree at branch `feature/ramp-livery-picker`.

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
- **R2** — A reader that discards every `1310`–`1313` row MUST produce exactly
  today's behaviour. This is the floor, and it MUST be unreachable from any input.
- **R3** — On any conflict between `1301` and a refinement row, `1301` wins.
- **R4** — A malformed, dangling or unparseable `1310`–`1313` row MUST be
  discarded, and MUST NOT fail the file, the airport, or the stand. **This is the
  opposite of the rest of `AptIO.cpp`**, where a bad row sets `ok = "Illegal …"`
  and aborts the whole load (`AptIO.cpp:1208`, `:1217`). These rows are
  hand-editable refinements; one bad exclusion must not make an apt.dat
  unopenable.
- **R5** — A `1313` row that fails to parse MUST be discarded **whole**, falling
  back to "no `1313` row present". It MUST NOT be partially applied. All-zero is
  a legal and meaningful value (§4.2), so a truncated row parsed as zeros would
  silently empty the stand — the one place where soft-fail must be explicit about
  what it falls back *to*.
- **R6** — Policy definitions are scoped to their airport block and are valid
  **anywhere within it**, before or after use. They MUST NOT be required to
  precede their reference. A precede-rule is broken by moving two lines, and the
  reader already buffers one airport at a time (`AptIO.cpp:354`), so it buys
  nothing. This also pins the blast radius of any corruption at one airport.
- **R7** — A policy name MUST NOT resolve across airport-block boundaries. A
  reference to a name not defined in the same block is dangling: discard per R4.
- **R8** — The policy name `-` means "this stand only". It MUST NOT be treated as
  a definable or referenceable name.

### Writer-side

Constraints on whoever produces the file, not defences the reader must implement.
A reader that has to defend against all of these is a reader nobody implements
correctly.

- **R9** — No whitespace other than a single U+0020 SPACE inside any field.
- **R10** — Type designators match `[A-Z0-9]{2,5}`; airline codes match
  `[A-Z0-9]{3,5}`. Anything else is dropped by the writer, not emitted for the
  reader to police.
- **R11** — Weights are non-negative integers in `0..1000`.
- **R12** — Policy names are generated from a content hash, never author-supplied.
  There is no free-text field in any of these rows.
- **R13** — `1301`'s airline list MUST equal the union of the referenced policy's
  airlines. Writer-enforced; the reader trusts `1301` (R3). The redundancy is
  deliberate — it is the only cross-check that catches a file edited in one place
  and not the other.
- **R14** — A writer MUST NOT emit weights pointing exclusively at classes that no
  listed airline can fill. WED treats this as a hard export error (§4.5).

### Reader-side

- **R15** — Unknown row codes, and unknown extra fields within a known row, MUST be
  ignored rather than rejected.
- **R16** — Grouping is a storage detail, never semantics. A reader copies a
  referenced policy into the stand and MUST NOT expose policy identity to
  anything downstream. WED re-derives grouping by content hash on export, so a
  hand-forged grouping is silently corrected by one round-trip.
- **R17** — Three-stage selection (§4) applies **only** to stands carrying a `1313`
  row. A stand without one MUST keep today's behaviour, unchanged.

---

## 2. Grammar

```abnf
; ---- the four new rows -------------------------------------------------
airlines-row   = %s"1310" 1*SP policy 1*(1*SP airline) *SP
reference-row  = %s"1311" 1*SP policy-ref *SP
exclusion-row  = %s"1312" 1*SP policy 1*(1*SP exclusion) *SP
weights-row    = %s"1313" 1*SP policy 6(1*SP weight) *SP

exclusion      = "-" airline ":" actype

; ---- lexical -----------------------------------------------------------
policy         = generated-name / inline-marker
policy-ref     = generated-name
generated-name = %s"g_" 6HEXDIG      ; content hash, writer-generated (R12)
inline-marker  = "-"                 ; "this stand only" (R8)

airline        = 3*5(ALPHA / DIGIT)  ; R10
actype         = 2*5(ALPHA / DIGIT)  ; R10
weight         = 1*4DIGIT            ; 0..1000, R11
SP             = %x20
```

Parser notes:

- `1310` with a policy and **zero** airlines is malformed; discard per R4. It is
  not "a policy with no airlines".
- `1313` takes **exactly six** weights. Five or seven is malformed; discard whole
  per R5. Do not pad and do not truncate.
- The policy field is always the first token after the row code; everything after
  it is payload. That resolves the only apparent ambiguity — a leading `-` as the
  inline marker versus a leading `-` on an exclusion — without lookahead.
- `-fdx:B763` is one token. Whitespace anywhere inside it is malformed.

### Rows in context

```
# airport block; definitions valid anywhere within it (R6)
1310 g_a3f91c  ual dal baw aal afr klm dlh
1312 g_a3f91c  -dal:B772 -baw:A35K
1313 g_a3f91c  0 0 3 0 1 0

1300 51.157304 -0.171800 347.5 gate heavy|jets Gate 42
1301 E airline ual dal baw aal afr klm dlh
1311 g_a3f91c

1300 51.157035 -0.172179 347.3 gate heavy|jets Gate 43
1301 E airline ual dal baw aal afr klm dlh
1311 g_a3f91c
```

A stand that shares nothing writes the same rows inline under the name `-`:

```
1300 51.147042 -0.174540 -128.8 gate heavy Cargo 1
1301 E cargo fdx ups
1312 - -fdx:B763
1313 -  0 0 0 0 1 0
```

The inline form is a strict subset of the referenced form, so a writer can emit
only inline rows and add grouping later with **no reader change**. WED will do
exactly that.

---

## 3. Reader algorithm

Per airport block. The reader already buffers one airport at a time
(`AptIO.cpp:354`), which is what makes R6 implementable as one pass plus a
resolve step.

```
PASS 1 — accumulate, do not resolve
  for each row in the airport block:
    1310 name a…   -> policy[name].airlines   = [a…]       (malformed: skip row)
    1312 name e…   -> policy[name].exclusions = [e…]       (malformed: skip row)
    1313 name w×6  -> policy[name].weights    = [w×6]      (malformed: skip WHOLE, R5)
    1311 name      -> stand.policy_ref        = name
    1312 "-"  e…   -> stand.exclusions        = [e…]       (inline form)
    1313 "-"  w×6  -> stand.weights           = [w×6]      (inline form)

PASS 2 — resolve, after the block is complete (R6)
  for each stand:
    if stand.policy_ref is set:
      if policy[stand.policy_ref] does not exist:
        discard the reference                  # dangling, R4 + R7
      else:
        COPY policy fields into the stand for each field not already set inline
        # copy, never reference (R16). Policy identity ends here.
    # the stand now carries:
    #   airlines   — from 1301, authoritative (R3)
    #   exclusions — possibly empty
    #   weights    — possibly absent

PASS 3 — validate, discarding only what is bad
  if stand.weights present and length != 6:      drop weights entirely   (R5)
  if any weight outside 0..1000:                 drop weights entirely   (R5)
  if an exclusion does not match the grammar:    drop THAT exclusion only
```

The asymmetry in pass 3 is intentional, and it is the one place the two row types
are treated differently. An exclusion means "also remove this" — dropping one bad
exclusion leaves the rest meaningful and independent. A weight vector means "here
is the whole distribution" — dropping one element changes what the other five
mean.

Inline rows win over copied ones (pass 2) so that a stand can refine a shared
policy without leaving the group. WED does not currently emit that combination,
but the reader should not forbid it.

---

## 4. Selection algorithm

**This is the behavioural change.** Everything above is transport.

### 4.1 The three stages

```
if stand has no 1313 row:
    use today's behaviour, unchanged                        # R17

class := weighted_choice(A..F, weights = stand.weights)
         if every weight is zero -> nothing parks here, stop        # §4.2

airlines_in_class := [ a for a in stand.airlines            # from 1301
                         if index has any livery for (a, class) ]
if airlines_in_class is empty -> nothing parks here, stop
airline := uniform_choice(airlines_in_class)

liveries := [ L for L in index.liveries(airline, class)
                if (airline, L.type) not in stand.exclusions ]
if liveries is empty -> nothing parks here, stop
livery := weighted_choice(liveries, weights = EXPORT_RATIO or uniform)
```

If a stage has no candidates, nothing parks at that stand this time. That is a
correct outcome, not an error — see §4.5.

### 4.2 Weights

Always six values, A through F, in that order — not a list of the classes the
author happened to mention.

```
1313 g_a3f91c  0 1 1 8 0 0
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
175 KB across every policy in the global apt.dat.

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

It also leaves room to weight airlines later (`1310` could carry `ual=3 baw=1`)
without restructuring anything.

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

**Recommendation: do not de-duplicate in the sim.** Fix it in the library by
removing the superseded exports — a data problem with a data fix, and the list is
in §6.3. A 2× skew on 38 aircraft is a much smaller defect than a de-duplication
rule that silently deletes liveries, and unlike that rule it disappears
permanently once the assets are cleaned.

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
> repair offered at the point of failure. **The sim SHOULD NOT add a fallback.**
> An empty stand is the correct reading of what the author wrote, and
> re-introducing a step-down takes back the expressiveness this change exists to
> provide.

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
| V1 | `1313 - 0 0 10 0 0 0` on a stand | class C always; stages 2–3 then run |
| V2 | `1313 - 0 0 0 0 0 0` | nothing parks. **Legal**, not an error (R5) |
| V3 | `1310`/`1312`/`1313` defined **after** the `1311` that uses them | resolves normally (R6) |
| V4 | two stands referencing one policy | both resolve; policy identity not observable downstream (R16) |
| V5 | `1312 - -fdx:B763` with no `1313` on the stand | exclusion recorded, but **today's behaviour** still applies (R17) — exclusions alone do not activate three-stage selection |
| V6 | `1313 g_x 1000 0 0 0 0 0` | legal; 1000 is the ceiling (R11) |
| V7 | stand with `1311` and its own inline `1313` | inline wins for weights, policy supplies the rest (§3 pass 2) |

V5 is the case most likely to be got wrong. Exclusions are subtractive
refinements of a selection that only exists once `1313` is present.

### 5.2 Malformed — all of these MUST load the file successfully

| # | input | expected |
|---|---|---|
| V8 | `1313 - 0 0 10 0 0` (five weights) | drop weights **whole**; stand falls back to today's behaviour (R5). MUST NOT be read as `0 0 10 0 0 0` |
| V9 | `1313 - 0 0 10 0 0 0 0` (seven) | drop weights whole (R5) |
| V10 | `1313 - 0 0 -5 0 0 0` | drop weights whole (R11, R5) |
| V11 | `1313 - 0 0 9999 0 0 0` | drop weights whole (R11, R5) |
| V12 | `1313 - 0 0 1.5 0 0 0` | drop weights whole — no decimal point is legal (§4.2) |
| V13 | `1311 g_nosuch` | drop the reference; stand keeps `1301` (R4, R7) |
| V14 | `1311 g_x` where `g_x` is defined in a **different** airport block | dangling; drop (R7) |
| V15 | `1312 - -fdx:B763 garbage -ups:B752` | drop `garbage` only; both valid exclusions survive (§3 pass 3) |
| V16 | `1310 g_x` with no airlines | drop the row (R4) |
| V17 | `1301` lists `ual dal`, policy lists `ual dal baw` | `1301` wins; `baw` never spawns (R3, R13) |
| V18 | a `1314` row | ignored (R15) |
| V19 | an embedded tab inside an airline list | writer defect (R9); reader treats it as a field separator, which may yield an unparseable token — drop that token, keep the row |
| V20 | file at version `1200` containing all four rows | loads; new rows apply (§7.2 — no version gate) |

### 5.3 The two that must never happen

| # | input | expected |
|---|---|---|
| V21 | any row above | **the file still loads.** No input in §5.2 may produce a load failure (R4) |
| V22 | every new row stripped | byte-identical aircraft placement to today (R2) |

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
| **Old and new asset both exported** for one real aircraft — `AT45_FDX_static.obj` *and* `ATR42-500_FedEx.obj` | 13 | 26 | **Library fix.** The visible tip of the 38 `_DUP` collisions in §8.4. Each doubles that aircraft's spawn probability (§4.4) |
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

That is the opposite of WED, which whitelists versions (`AptIO.cpp:313-320`),
rejects unknown row codes (`:1208`), and aborts the entire load on either. Row
`1301` shipped gated at version 1050 (`:738`), which is what led us to assume the
sim gated too.

**Consequence: these rows can go into a 1200 file and old X-Plane keeps working**,
reading `1301` and ignoring the rest — exactly the degradation the format is
built around. A version bump becomes a choice about signalling intent rather than
a compatibility requirement.

**What this does NOT establish.** We tested **one current build**. Whether X-Plane
11, or an early 12, is equally tolerant is unknown, and that tolerance may have
been added at some point. If Gateway has to serve builds older than 12.4, that
needs checking against whichever is the real floor — a question about release
support policy more than about the format.

### 7.3 The half that is not solved: an old WED

The same test applied to WED's own reader gives the opposite answer, and this is
the part still needing a decision.

A 2.7.x WED opening an apt.dat containing `1310`–`1313` falls through
`AptIO.cpp:1188-1209` to `ok = "Illegal unknown record"`, and
`WED_AptIE.cpp:1162-1167` turns that into

```
Unable to read apt.dat file '<path>': Illegal unknown record (Line N)
```

and imports **nothing**. Not a degraded airport — no airport.

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

### 8.2 Why C alone was rejected, and what survives of it

C solves a real problem — the 83% sharing figure is not theoretical, and a hub
author editing one terminal should not touch eighty stands. But C still stores the
resolved result, so it inherits the freezing problem in full.

What survives is its *sharing* mechanism, which is orthogonal to what is stored.
The proposal here is **C's structure carrying D's content**.

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

## 9. Open questions

1. **Row code allocation.** `1310`–`1313` are proposals. Any four work. This is
   the only item blocking WED-side implementation.
2. **Version policy.** §7.2 removes the compatibility argument for a bump, so this
   is now a question about signalling intent. Our recommendation: no bump.
3. **A process decision that is not one person's**: §7.3. If a bump happens, new
   WED writes a version old X-Plane refuses, while Gateway serves both. Who
   generates which version for whom? This needs Jim and the release manager. It
   has no answer today and it is the only part of this with no technical
   solution.

---

## Appendix — what is already built on the WED side

So the shape above is not speculative:

- **`livery_index.txt`** — 298 shipped static-aircraft liveries, each with ICAO
  type, wingspan class, operator, registration, country of registration and a
  free-text livery note. Generated from `library.txt` plus the asset tree, then
  maintained by hand.
- **WED reads it**, resolves an author's airline selection against it, and renders
  off-screen previews of the actual aircraft.
- **WED distinguishes** "this airline has a model", "real airline, nothing
  modelled yet" and "unknown code" — and never persists which. It is recomputed on
  every load, so a model shipped later simply starts appearing. Same principle as
  D in §8.3: never persist what can be recomputed.
