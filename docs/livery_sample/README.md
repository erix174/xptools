# Sample apt.dat — row 1313 to play with

Two droppable scenery packages demonstrating the format proposed in
`WED_LiveryFormatSpec.md`. Sixteen stands in a row, each demonstrating exactly
one thing, named so you can see which is which from the ground.

> **This package was built against draft 6 and has not been regenerated.**
> Draft 7 deleted the `1312` refinement row entirely (spec §8.6), so six stands
> here demonstrate something the format no longer has: `07-EXCLUDE`,
> `08-WHITELIST`, `09-MIXED-SIGILS`, `10-PLUS-WINS`, `14-BAD-REFINE` and
> `16-EXCL-NO-WEIGHTS`. Their rows below are wrong about draft 7 and are kept
> only so the load evidence in `docs/livery_evidence/` still describes a real
> file.
>
> The remaining ten stands are current and complete: they cover every rule draft
> 7 defines. Regeneration waits on Jim's row code, because every `1313` in the
> package changes when that number does — doing it before then means doing it
> twice.
>
> What a regenerated package needs that this one lacks: a stand carrying **two
> `1313` rows**, to exercise R24's first-wins rule.

```
ZZZ_livery_format_sample/           the real thing, with the new rows
ZZZ_livery_format_sample_stripped/  same 16 stands, new rows removed
```

## Install

Copy either folder into `Custom Scenery/`. Nothing else is needed — airport data
only, no DSF, no objects.

The airport is **ZZLI**, at 38.900 N 101.012 W (flat western Kansas, default
scenery, nothing nearby to collide with). Pick it from Flight Configuration by
ICAO. Stands run west to east, 95 m apart, all facing north.

## The point of the file

**It is version 1200, and it contains the new row codes.** Drop it into an
unmodified X-Plane 12 today and it loads silently — no warning, no error, and all
sixteen stands park aircraft from `1301` exactly as they do now.

**Verified, not assumed.** Loaded by X-Plane **12.4.3-r2 (build 124311)**, flight
started at ZZLI, 28 `APT` diagnostics in the whole run and **none naming ZZLI,
this file, or any row here** — in a run where the sim *did* name an unknown
metadata key in Global Airports, with file and line. Log:
`docs/livery_evidence/XPlane12.4.3-r2_livery_sample_load_Log.txt`.

**No WED can open the unstripped file — including ours.** It stops at the first
new row with `Illegal unknown record` and imports nothing. That is expected:
reading the new rows is phase 4 on our side, gated on the row-code decision. It is
the unsolved half described in §7.3, and it is a WED problem rather than a sim one.

Use `ZZZ_livery_format_sample_stripped/` to open the 16 stands in any WED. That
package is also a working demonstration of §7.3 option 1 — Gateway serving an old
client a stripped apt.dat. Nothing is lost but refinements the old client could
not have used.

### A mistake worth not repeating: apt.dat has no comments

The first version of this package was commented for readability. **apt.dat has no
comment syntax** — not `#`, not anything — and WED refused the file at the first
comment line, before reaching any row this proposal defines.

The mechanism is quieter than "the reader rejects `#`". `TextScanner_FormatScan`
converts a line's first token with `atoi()`, and `atoi("#")` returns **0** while
the function still reports having read one token — so the caller's skip guard
never fires and the line is processed as **record code 0**, which falls through to
`Illegal unknown record`. **Blank lines are fine**, by the same code read the other
way: they tokenize to nothing, the guard fires, and they are skipped. Use them
freely.

X-Plane loaded the commented file **without complaint**, which is why the mistake
survived a successful sim test. A silent sim load does not mean a well-formed file.

## What each stand demonstrates

Stands 01–10 are what WED will actually emit. **Stands 11–15 are deliberately
malformed** — the conformance vectors from §5.2, which WED would never write. They
are here so you can watch the soft-fail rule work.

| stand | shows | expected result |
|---|---|---|
| `01-CONTROL` | no new rows at all | today's behaviour. The control — everything else is measured against this |
| `02-PIN-C` | `1313` pinning one class | always class C: an A320, 737 or MD-80 from DAL or UAL |
| `03-MIX-CDE` | a real distribution | C 60% / D 30% / E 10%. The thing the old step-down could not express |
| `04-ALL-ZERO` | all six weights zero | **nothing spawns, ever. This is legal**, not an error — same as `ramp_operation_none` |
| `05-CLASS-F` | weights on class F | nothing spawns. **The library ships no F-class livery at all** — a data gap, not a format error |
| `06-UNFILLABLE` | **the 17.2% case** | nothing spawns. BAW has liveries at C and E but **none at D**, and this stand asks for D. WED refuses to export this — hard error plus one-click repair |
| `07-EXCLUDE` | `1312` with `-` | DAL at class C **minus the 737** → only A320 and MD-80 |
| `08-WHITELIST` | `1312` with `+` (R21) | `+dal:B772` closes Delta's set. Delta contributes **only** the 777; the other three airlines are untouched |
| `09-MIXED-SIGILS` | `+` and `-`, different airlines | independent per airline — Delta closed to the 777, United minus the 747 |
| `10-PLUS-WINS` | `+` and `-`, **same** airline | writer error. `+` is authoritative, the `-` is ignored — not subtracted from the closed set (R21) |
| `11-BAD-5-WEIGHTS` | five weights, not six | drop the row **whole** → behaves like 01. Must **not** be read as `0 0 10 0 0 0` |
| `12-BAD-DECIMAL` | `1.5` | drop whole → behaves like 01. No decimal point is ever legal here |
| `13-BAD-NEGATIVE` | `-5` | drop whole → behaves like 01 |
| `14-BAD-REFINE` | one bad token among two good | drop **only** `garbage`; both real refinements still apply |
| `15-UNKNOWN-ROW` | a `1314` row | ignored, not rejected (R15) |
| `16-EXCL-NO-WEIGHTS` | `1312` with no `1313` | **today's behaviour still applies.** A refinement alone does not switch on three-stage selection (R17) |

### The one that matters most

`01-CONTROL` versus everything else. **R2 says a reader that discards every new row
must produce exactly today's behaviour** — so stands 11 through 15, all of which
contain something broken, must end up behaving identically to stand 01. If any of
them differs, the soft-fail rule has a hole.

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

### Stands 08–10 are the `+` closed set

`+` exists because subtraction cannot say "only this". Writing "only the A380 parks
here" as "exclude every other type this airline operates" gives a list with no
upper bound that also **goes stale** — a type added to the library next year is not
in it and starts parking there by itself.

`10-PLUS-WINS` is the conflict case, and it is deliberately a writer error. WED
will never emit it; it is here so a reader can be checked against R21's answer,
which is that `+` wins and the `-` is ignored rather than applied.

## Everything about a stand is at the stand

There are no shared definitions and nothing to resolve. Draft 5 had named policies
defined once and referenced per stand; §8.2 of the spec measures that as both
larger on the real global apt.dat and more complex to read, so it is gone. Two
stands wanting the same thing simply write the same two rows.

## Airlines used, and why those

Picked because they actually resolve against the shipped index, so the stands
spawn real aircraft rather than demonstrating nothing:

| airline | classes with a livery today |
|---|---|
| `dal` | B (CRJ2), C (A320, B738, MD82), D (B752, B763), E (B772) |
| `ual` | C (A320, B738), D (B752, B763), E (B744) |
| `baw` | C (A320), E (B772) — **no D**, which is what stand 06 exploits |
| `aal` | C (A320, B738), E (B772) |

Counted from `livery_index.txt` at data version `20260916-r1`, generated from
**X-Plane 12.4.3-r2**. A different build has a different asset set (§6.4), so if a
stand comes up empty that the table says should not, check the index's `source`
header against your install before suspecting the reader.

## Regenerating

Stand geometry is a straight line — latitude fixed at 38.900, longitude stepping
by 95 m — so adding a stand means copying the last `1300`/`1301` pair and advancing
the longitude by `0.0010965`.

---

## Range / service-area test bench (stands 20-27)

The pack now sits at **ZBAA, Beijing** (40.078 N, 116.570 E, country CN) instead
of the old synthetic Kansas position. The move is the point: the stands below
only mean anything at an airport that is a long way from the operators listed on
them.

**IT SHADOWS THE REAL BEIJING CAPITAL.** The Airport ID and `icao_code` are both
`ZBAA` so that WED's airport database resolves a real country and a real
recommendation list - a fictional code resolves to nothing and none of this can
be tested. Disable the pack when you are not testing.

Stands 01-16 are unchanged and still test what they always did: weight parsing,
malformed rows, unknown row codes, and that `1312` is ignored now that draft 7
has deleted it. They are position-independent.

### What each new stand is for

| stand | classes | operators | should spawn | must NOT spawn |
|---|---|---|---|---|
| 20-CN-CONTROL | C | CCA CSN CES CSZ | all of them | - |
| 21-NEVER-HERE | C | SWA EZY | nothing | SWA:B738, EZY:A320 |
| 22-FOREIGN-NARROW | C | UAL DAL AAL BAW | nothing | UAL:B738, DAL:B738, AAL:B738, BAW:A320 ... |
| 23-FOREIGN-WIDE | E | UAL DAL AAL BAW | UAL:B744, DAL:B772, AAL:B772, BAW:B772 | - |
| 24-SPLIT-IN-CLASS | D | UAL UPS | UAL:B763, UPS:B763 | UAL:B752, UPS:B752 |
| 25-MIXED-STAND | C+E | CCA UAL | CCA:A320, CCA:B738, UAL:B744 | UAL:A320, UAL:B738 |
| 26-CARGO-SPLIT | D | FDX | FDX:B763, FDX:B752, FDX:DC10 (FedEx has a Hong Kong hub, 2,000 km away) | - |
| 27-DOMESTIC-FAR | C | CCA | CCA:A320, CCA:B738 | - |

### Why these eight

**20 and 27 are the controls.** Chinese narrowbodies at a Chinese airport are
correct and common, and any filter that touches them has broken far more than it
fixed. Zeroing the stand's class-C weight - the first mechanism considered -
fails here, which is why it was abandoned.

**21 is the easy case.** Southwest and easyJet have exactly one aircraft each
and neither can cross the Pacific, so the whole operator fails. Removing them
from the stand's `1301` list costs nothing. Every mechanism handles this one.

**22 is the headline case** - the United 737 in Beijing. All four operators are
listed legitimately at ZBAA and all four have widebodies that belong there; it
is only their narrowbodies that cannot reach.

**23 is the other half of 22** and must stay untouched. If a filter empties this
stand it has confused "this operator cannot reach" with "this aircraft cannot
reach".

**24 is the one that kills the per-operator approach.** UAL's B752 (7,200 km)
cannot reach and its B763 (11,000 km) can - and BOTH ARE CLASS D. No action
taken against the operator as a whole, and no adjustment of the stand's class
weights, can keep one and drop the other. UPS is the same.

**26 shows why hubs beat country centroids.** Measured from the US centroid,
FedEx's B752 and DC10 fail; measured from its nearest hub - Hong Kong - all three
pass, and FedEx really does fly them into Beijing from there. The rule is only as
good as the hub list, and the hub list is a fact, not an approximation.

**25 is the mixed stand.** CCA's 737 and UAL's 744 are both correct here; UAL's
737 is not. A filter working at stand or operator granularity has no move that
does not also break one of the two correct answers.

### Distances used

Great-circle (haversine) from the operator's NEAREST HUB to the stand, one way,
hubs from the sixth column of WED_AirlineDirectory.txt resolved against Global
Airports. United from KSFO 9,510 km; British Airways from EGLL 8,150 km; FedEx
from VHHH 1,980 km; Air China from ZBAA ~0. No domestic exemption is needed - a
domestic operator's nearest hub is close by definition.
