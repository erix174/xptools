# apt.dat: per-stand fleet and livery data — proposed format

For Jim K. — the X-Plane side of WED's ramp livery picker.
Draft 1, 2026-09-16. Targets WED 2.8.0.

---

## What this is, in one paragraph

WED is gaining a UI where a scenery author says which airlines park at a stand, which
ICAO size classes may spawn there and in what proportion, and — rarely — which specific
aircraft types to exclude. This document proposes how that reaches apt.dat, and asks
for three decisions only you can make. **`1301` is not modified.** Everything proposed
here is additive, and a reader that discards all of it is left with exactly today's
behaviour.

---

## Why anything is needed

X-Plane publishes static aircraft through `library.txt` `EXPORT_EXTEND` buckets keyed
by (operation type, size class, airline):

```
EXPORT_EXTEND lib/airport/aircraft/airliners/heavy_e.obj      apt_aircraft/heavy/A35K/A35K_BritishAirways.obj
EXPORT_EXTEND lib/airport/aircraft/airliners/heavy_e_baw.obj  apt_aircraft/heavy/A35K/A35K_BritishAirways.obj
```

That name carries no **aircraft type** — one `heavy_e` bucket mixes A359 (×39), A35K
(×11) and B772 (×5) — and no **livery**. So "United's 737-800 in the retro livery"
cannot be named, even though `B738_UAL_Legacy` and `B738_UAL_Modern` have both shipped
for years. Nine Air ships eleven colour variants of one 737; Air China ships five
Peony tail numbers. None of it is addressable, and an author cannot ask for any of it.

apt.dat has the same gap one level up: `1301` says *which airlines*, and nothing about
size distribution or type.

---

## What we evaluated, and the numbers

We did not pick a shape and rationalise it. Four candidates were measured against the
real global apt.dat — 380 MB, 38,888 airports, 200,102 ramp starts, 44,242 of which
carry airlines — crossed with the real shipped livery set.

| | What it stores | Size today | After the asset library fills out |
|---|---|---|---|
| **A** explicit pairs `A320_UAL A359_DAL …` | resolved result | 2.21 MB | ~4.4 MB and climbing |
| **B** grouped by type `B738=UAL,DAL` | resolved result | 1.34 MB | ~2.7 MB and climbing |
| **C** named fleet sets, referenced per stand | resolved result | 0.88 MB | ~1.8 MB and climbing |
| **D** subtractive — airlines, minus exceptions | **intent** | 0.03 MB | **unchanged** |

Supporting measurements:

- **220,610** (type, airline) pairs would exist if expanded explicitly — an average of
  5 per stand.
- **53.9%** of the airline codes authors already write resolve to a model today. The
  other 46% expand to nothing, so an explicit list silently records a gap as if it were
  a complete answer.
- **83%** of stands share their airline list with four or more stands at the same
  airport; **53%** share with sixteen or more. Per-stand storage makes every bulk edit
  a repeated one.
- Only **84 airports** have 100 or more airline-carrying stands — but they are CDG
  (326), ICN (285), CKG (277), ORD (273), IST (244), PEK (231). The pathological case
  is rare and lands exactly where it hurts.

### Why A and B were rejected

Not size — **2 MB on a 380 MB file is not a reason to do anything**, and we were
initially wrong to assume it would be the deciding factor.

They were rejected because they **freeze a snapshot of the asset library into every
scenery file**. A file written today says "United has an A320 and a 737 here". Ship a
United A350 next year and no existing airport will ever use it — not until every one of
them is re-opened and re-saved. At Gateway's scale that is not a migration, it is a
permanent loss.

The same argument kills them for third-party livery packs: a pack would be invisible to
every airport already authored, which removes any reason to make one.

### Why C alone was rejected, and why part of it survives

C solves a real problem — the 83% sharing figure is not theoretical, and a hub author
editing one terminal should not touch eighty stands. But C still stores the resolved
result, so it inherits the freezing problem in full.

What survives is its *sharing* mechanism, which is orthogonal to what is being stored.
The proposal below is **C's structure carrying D's content**.

### Why D won

It stores what the author actually decided — airlines, size weights, and the rare
exception — and lets the sim resolve that against whatever is installed at the time.
New models and third-party packs appear at every relevant airport with **zero apt.dat
edits**. Its size is bounded by author behaviour rather than by library growth.

It also matches the UI: the author's default is "any aircraft this airline operates
that fits", and the only action available is to remove one. Storing an expanded list
would mean WED writing out a computed answer the author never gave.

One honest cost: **it is not self-explanatory.** Reading apt.dat alone will not tell
you what parks at a stand; you need the livery index too. We judged that acceptable
because tooling can show it (WED does, and an export report could) while freezing
cannot be undone by tooling.

---

## Proposed format

### Rows

```
1310  <policy>  <airline> [<airline> …]        define a policy's airline set
1311  <policy>                                 a stand uses this policy
1312  <policy>  -<airline>:<type> […]          exclusions
1313  <policy>  <class>=<weight> […]           ICAO class weights, relative integers
```

`1302` is taken (metadata), so `1310`–`1313` are the first free block. Final numbers
are yours to assign.

### Example — a shared policy

```
# airport block; definitions are valid anywhere within it
1310 g_a3f91c  ual dal baw aal afr klm dlh
1312 g_a3f91c  -dal:B772 -baw:A35K
1313 g_a3f91c  C=3 E=1

1300 51.157304 -0.171800 347.5 gate heavy|jets Gate 42
1301 E airline ual dal baw aal afr klm dlh
1311 g_a3f91c

1300 51.157035 -0.172179 347.3 gate heavy|jets Gate 43
1301 E airline ual dal baw aal afr klm dlh
1311 g_a3f91c
```

### Example — a stand that shares nothing

The policy name `-` means "this stand only". Same syntax, no definition, no reference.

```
1300 51.147042 -0.174540 -128.8 gate heavy Cargo 1
1301 E cargo fdx ups
1312 - -fdx:B763
1313 - E=1
```

The inline form is a strict subset of the referenced form, so a writer can emit only
inline rows and add grouping later with no reader change.

### Weights

Relative integers, not percentages: `C=3 E=1` means C three times as often as C+E's
total of four. No sum-to-100 rule, no float rounding, no validation pass. A class
absent from the list is **not** weight zero — it falls back to `1301`'s single letter,
so a model in a class the author never mentioned still spawns. `C=0` is an explicit
ban and is how an author says "never D here", which is a case they have asked for.

---

## Invariants

1. **`1301` stands alone and is authoritative.** Every row here is an additive
   refinement. A reader that discards all of them gets today's behaviour, correctly. On
   any conflict, `1301` wins.
2. **`1301`'s airline list equals the union of the referenced policy's airlines.**
   Writer-enforced. The redundancy is deliberate: it is the only cross-check that
   catches a file edited in one place and not the other.
3. **Policies are scoped to their airport block and valid anywhere within it.** Not
   "must precede use" — that is broken by moving two lines, and the reader already
   buffers one airport at a time. This pins the blast radius of any corruption at one
   airport.
4. **Soft-fail, always.** A malformed or dangling row is discarded and the stand falls
   back to `1301`. It must never fail the file. Note this differs from the rest of
   `AptIO.cpp`, where a bad row sets `ok = "Illegal …"` and aborts the whole load
   (`AptIO.cpp:1208`, `:1217`) — for these rows that behaviour would let one
   hand-edited exclusion make an apt.dat unopenable.
5. **Grouping is re-derived, never trusted.** WED copies a referenced policy into each
   stand on import and re-groups by content hash on export, so a hand-forged grouping is
   silently corrected by one round-trip. Policy names are content-derived
   (`g_a3f91c`), which also keeps definitions stable in diffs when their content has
   not changed — Julian reviews those diffs.

---

## Version and unknown rows — tested on a real build, not assumed

We said we would answer this rather than ask you to, so we did: the format below was
written into real scenery packages and loaded by a real X-Plane. **The sim ignores
both the new rows and an unrecognised version number.** Evidence is the full log,
kept at `docs/livery_evidence/XPlane12.4.4_apt_dat_tolerance_Log.txt`.

The reasoning matters because **WED and the sim behave differently**, and we had
assumed they matched. They do not, and the difference cuts both ways.

### The test

Four single-airport scenery packages in `Custom Scenery/`, loaded by
**X-Plane 12.4.4 (build 124406)**:

| pack | apt.dat | result |
|---|---|---|
| `ZZ01` | version 1200, valid | silent |
| `ZZ02` | version 1200, plus an unknown row code `1310` | **silent** |
| `ZZ03` | **version 1300** — not a version that exists | **silent** |
| `ZZ04` | version 1200, deliberately corrupt runway row | error, named, with a line number |

`ZZ04` is the control that proves the harness works. It produced:

```
E/SCN: An apt.dat enumeration is out of range: Invalid surface code. Expected a code
       less than 58 but got 60615503. Airport is ZZ04.
       File is Custom Scenery/ZZZ_aptdat_test_ZZ04/Earth nav data/apt.dat.
E/SYS: MACIBM_alert: There was a problem loading the scenery package: ...
       The scenery may not look correct.
```

So the sim does parse these files at startup, does report errors with file and line, and
would have told us about `1310` or version `1300` if it objected.

### What this means

1. **X-Plane 12 ignores unknown row codes.** `1310` passed without comment.
2. **X-Plane 12 accepts an unrecognised version number.** `1300` passed without comment.
3. **Even a genuine error is non-fatal.** `ZZ04` still loaded — "the scenery may not look
   correct", not "this file is rejected".

That is the opposite of WED, which whitelists versions (`AptIO.cpp:313-320`), rejects
unknown row codes (`:1208`), and aborts the entire load on either. Row `1301` shipped
gated at version 1050 (`:738`), which is what led us to assume the sim gated too.

**Consequence: these rows can go into a 1200 file and old X-Plane keeps working**,
reading `1301` and ignoring the rest — exactly the degradation the format is built
around. A version bump becomes a choice about signalling intent, not a compatibility
requirement, and the Gateway two-version problem largely disappears.

### The other half: an old WED cannot open these files

The same test, applied to WED's own reader by inspection, gives the opposite answer —
and this is the part that still needs a decision.

A 2.7.x WED opening an apt.dat containing `1310`-`1313` falls through
`AptIO.cpp:1188-1209` to `ok = "Illegal unknown record"`, and
`WED_AptIE.cpp:1162-1167` turns that into

```
Unable to read apt.dat file '<path>': Illegal unknown record (Line N)
```

and imports **nothing**. Not a degraded airport — no airport.

So for a user still on X-Plane 12.2.x who downloads Gateway scenery written in the new
format:

| | behaviour |
|---|---|
| **X-Plane 12.2.x** | fine. Ignores the new rows, parks aircraft from `1301` exactly as today. |
| **WED 2.7.x** | refuses to open the file, with a message naming the line. |

The sim side is genuinely solved. The editor side is not: an author on an older WED
cannot open scenery that a newer WED submitted. Three ways out, none of them ours to
choose alone:

1. Gateway serves an old client an apt.dat with the new rows stripped — it can, since
   `1301` alone is complete and authoritative by design.
2. A 2.7.x patch that skips unknown row codes instead of failing. Small change, but it
   needs a release, and it only helps people who take it.
3. Accept it: editing 2.8-era scenery requires 2.8-era WED.

Worth noting the populations differ in size and in updatability. Sim users are many and
update on their own schedule; WED authors are far fewer and already track releases
closely. That argues for (3) with (1) as a safety net, but it is a Gateway policy call.

### What this does NOT establish

We tested **one current build**. Whether X-Plane 11, or an early 12, is equally tolerant
is unknown, and that tolerance may have been added at some point. If Gateway has to
serve builds older than 12.4, that needs checking against whichever is the real floor —
a question about your release support policy more than about the format.

The full log is kept alongside this document.

## What we need from you

1. **Row code allocation.** `1310`–`1313` are proposals.
2. **Version policy**, once the test above answers whether a bump is forced.
3. **A process decision that is not really yours alone**: if a bump is forced, new WED
   writes a version old X-Plane refuses, while Gateway serves both. Who generates which
   version for whom? This needs you and the release manager. It has no answer today and
   it is the only part of this with no technical solution.

---

## What is already built on our side

So the shape above is not speculative:

- `livery_index.txt` — a catalogue of all 376 shipped static-aircraft liveries, each
  with ICAO type, wingspan class, operator, registration, country of registration and a
  free-text livery note. Lives next to the assets it describes, at
  `Resources/default scenery/sim objects/apt_aircraft/`. Generated from `library.txt`
  plus the asset tree, then maintained by hand.
- WED reads it, resolves an author's airline selection against it, and renders
  off-screen previews of the actual aircraft.
- WED distinguishes "this airline has a model", "real airline, nothing modelled yet"
  and "unknown code", and never persists which — it is recomputed on every load, so a
  model shipped later simply starts appearing.

Three defects in the shipped asset library turned up while building the index, offered
here as a by-product rather than a complaint:

- 38 cases where an old and a new asset for the same aircraft are both `EXPORT_EXTEND`ed
  (e.g. `AT45_FDX_static.obj` and `ATR42-500_FedEx.obj`).
- 6 objects on disk that `library.txt` never exports.
- `heavy/B772_AAL/` and `heavy/B772_AAl/` — byte-identical folders, ~13 MB wasted,
  whose two `library.txt` lines point at *different* paths. Fine on Windows and macOS,
  broken on a case-sensitive filesystem.
