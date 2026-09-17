# Sample apt.dat — rows 1310–1313 to play with

A droppable scenery package demonstrating the format proposed in
`WED_LiveryFormatSpec.md`. Seventeen stands in a row, each one demonstrating
exactly one thing, named so you can see which is which from the ground.

```
docs/livery_sample/ZZZ_livery_format_sample/Earth nav data/apt.dat
```

## Install

Copy `ZZZ_livery_format_sample/` into `Custom Scenery/`. Nothing else is needed —
the package is airport data only, no DSF, no objects.

The airport is **ZZLI**, at 38.900 N 101.012 W (flat western Kansas, default
scenery, nothing nearby to collide with). Pick it from Flight Configuration by
ICAO. Stands run west to east, 95 m apart, all facing north, so the whole set is
visible in one pass along the line.

## The point of the file

**It is version 1200, and it contains all four new row codes.** Drop it into an
unmodified X-Plane 12 today and it loads silently — no warning, no error, and all
seventeen stands park aircraft from `1301` exactly as they do now. That is
§7.2 of the spec, and this file lets you confirm it on your own machine instead of
taking our word for it.

**Verified, not assumed.** This package was loaded by X-Plane **12.4.3-r2 (build
124311)** and a flight started at ZZLI. Across the whole run the sim produced 28
`APT` diagnostics and **none of them names ZZLI, this file, or any row here** — in
a run where it *did* name an unknown metadata key in Global Airports, with file
and line. Log: `docs/livery_evidence/XPlane12.4.3-r2_livery_sample_load_Log.txt`.

One honest gap: that flight was started from the runway, so the seventeen ramp
starts are confirmed only as far as "they did not stop the airport loading". If
you start from a gate instead, the picker should list all seventeen by name —
that is the check we have not run.

**WED 2.7.x cannot open this file.** It will say `Illegal unknown record (Line N)`
and import nothing. That is expected, it is the unsolved half described in §7.3,
and it is a WED problem rather than a sim one.

## What each stand demonstrates

Stands 01–10 are what WED will actually emit. **Stands 11–16 are deliberately
malformed** — the conformance vectors from §5.2, which WED would never write. They
are here so you can watch the soft-fail rule work. Stand 17 is neither: it is
well-formed but self-contradictory, and tests which side wins.

| stand | shows | expected result |
|---|---|---|
| `01-CONTROL` | no new rows at all | today's behaviour. The control — everything else is measured against this |
| `02-PIN-C` | `1313` pinning one class | always class C: an A320, 737 or MD-80 from DAL or UAL |
| `03-MIX-CDE` | a real distribution | C 60% / D 30% / E 10%. The thing the old step-down could not express |
| `04-ALL-ZERO` | all six weights zero | **nothing spawns, ever. This is legal**, not an error — same as `ramp_operation_none` |
| `05-CLASS-F` | weights on class F | nothing spawns. **The library ships no F-class livery at all** — a data gap, not a format error |
| `06-UNFILLABLE` | **the 17.2% case** | nothing spawns. BAW has liveries at C and E but **none at D**, and this stand asks for D. WED refuses to export this — hard error plus one-click repair. Here so you can see the runtime symptom |
| `07-POLICY-A` | a referenced shared policy | UAL/DAL/BAW/AAL/AFR, C:E = 3:1, minus two excluded types. **Also the R18 case** — see below |
| `08-POLICY-B` | the same policy, second stand | identical to 07. This sharing is the 83%-of-stands case |
| `09-FORWARD-REF` | **R6** — policy defined *after* use | must resolve. The definition is at the bottom of the block |
| `10-EXCLUDE` | `1312` | DAL at class C **minus the 737** → only A320 and MD-80 |
| `11-BAD-5-WEIGHTS` | V8 — five weights, not six | drop the row **whole** → behaves like 01. Must **not** be read as `0 0 10 0 0 0` |
| `12-BAD-DECIMAL` | V12 — `1.5` | drop whole → behaves like 01. No decimal point is ever legal here |
| `13-BAD-NEGATIVE` | V10 — `-5` | drop whole → behaves like 01 |
| `14-DANGLING-REF` | V13 — reference to a policy that does not exist | drop the reference → behaves like 01 |
| `15-MIXED-EXCL` | V15 — one bad exclusion among two good | drop **only** `garbage`; both real exclusions still apply |
| `16-UNKNOWN-ROW` | V18 / R15 — a `1314` row | ignored, not rejected |
| `17-1301-WINS` | V17 — policy says `dal ual`, `1301` says `dal` | **`1301` is authoritative (R3)** — UAL must never spawn here |

### The one that matters most

`01-CONTROL` versus everything else. **R2 says a reader that discards every new
row must produce exactly today's behaviour** — so stands 11 through 16, all of
which contain something broken, must end up behaving identically to stand 01. If
any of them differs, the soft-fail rule has a hole.

That is the regression test worth automating, and it is why the control stand is
first.

### Two empty stands, two different reasons

`04-ALL-ZERO` and `05-CLASS-F` both park nothing, and telling them apart is the
whole argument of §4.5:

- **04 is the author saying "nothing here."** Correct, intended, should stay empty.
- **05 is the author asking for something that does not exist yet.** The day an
  F-class livery ships, this stand starts working with no apt.dat edit. That is
  late binding, and it is the reason the format stores intent rather than a
  resolved list.

Neither should trigger a fallback. `06-UNFILLABLE` is the third flavour — a
mistake — and it is WED's job to stop that one before it ever reaches a file.

### Stand 07 is a trap, and it found a bug in our own spec

Policy `g_a3f91c` excludes `-baw:A320`. **The A320 is BAW's only C-class livery.**
So if stage 2 picks an airline on "has any livery in this class" and stage 3 then
removes the excluded ones, drawing BAW at class C yields nothing and the stand
parks nothing that cycle — even though four other airlines were available.

The author wrote *"don't park BAW's A320 here"* and would have got *"sometimes
park nothing here"*. Drafts 1–4 of the spec had exactly that flaw; building this
sample and tracing the stand by hand is what surfaced it. It is now **R18**:
stages 2 and 3 apply the same exclusion filter.

Worth keeping as a regression test. `07` and `08` share a policy, so they must
also always agree with each other statistically — if one goes empty more often
than the other, something is keyed off stand identity that should not be.

## Airlines used, and why those

Picked because they actually resolve against the shipped index, so the stands
spawn real aircraft rather than demonstrating nothing:

| airline | classes with a livery today |
|---|---|
| `dal` | B (CRJ2), C (A320, B738, MD82), D (B752, B763), E (B772) |
| `ual` | C (A320, B738), D (B752, B763), E (B744) |
| `baw` | C (A320), E (B772) — **no D**, which is what stand 06 exploits |
| `aal` | C (A320, B738), E (B772) |
| `afr` | B (CRJ1), C (A320) |

Counted from `livery_index.txt` at data version `20260916-r1`, generated from
**X-Plane 12.4.3-r2**. A different build has a different asset set (§6.4), so if
a stand comes up empty that the table says should not, check the index's `source`
header against your install before suspecting the reader.

## Regenerating or extending it

The file is hand-maintained; there is no generator to run. Stand geometry is a
straight line — latitude fixed at 38.900, longitude stepping by 95 m — so adding
a stand means copying the last `1300`/`1301` pair and advancing the longitude by
`0.0010965`.
