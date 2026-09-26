# Sample apt.dat — row 1313 to play with

Two droppable scenery packages for the format in `WED_LiveryFormatSpec.md`
(draft 8). 24 stands in a row, each demonstrating one thing, named so you can
tell them apart from the ground.

```
ZZZ_livery_format_sample/           the real thing: 1313, plus 1312/1314 as unknown rows
ZZZ_livery_format_sample_stripped/  same 24 stands, rows 1312-1314 removed
```

## Install

Copy either folder into `Custom Scenery/`. Airport data only - no DSF, no
objects.

**It shadows the real Beijing Capital.** The airport sits at 40.078 N 116.570 E
with Airport ID and `icao_code` both `ZBAA`, so that WED's airport database
resolves a real country (CHN) and a real recommendation list - a fictional code
resolves to nothing, and the range rule cannot be tested without a real place.
Disable the pack when you are not testing. Stands run west to east, 95 m apart,
all facing north.

## Who can open which

| reader | `ZZZ_livery_format_sample` | `_stripped` |
|---|---|---|
| X-Plane 12 (every build tested) | loads silently; unknown rows skipped | loads |
| WED 2.8 (this branch) | opens; 1313 read; 1312/1314 skipped and **listed** - one alert on File > Import, and a waivable validation warning (`warn_apt_dat_rows_not_imported`) | opens |
| WED 2.7.x and older | **refuses the whole file** at the first row it does not know (`Illegal unknown record`) | opens |

The sim side is verified, not assumed: X-Plane **12.4.3-r2** loaded the earlier
ZZLI build of this package with 28 `APT` diagnostics in the whole run and none
naming the airport, the file or any row in it - in a run where the sim *did*
name an unknown metadata key in Global Airports, with file and line
(`docs/livery_evidence/XPlane12.4.3-r2_livery_sample_load_Log.txt`, spec §7.2).
WED 2.7.3's refusal is `docs/livery_evidence/WED2.7.3_refuses_new_rows.txt`.

The stripped package is also a working demonstration of spec §7.3 option 1: a
server handing an old client a stripped apt.dat loses nothing but rows that
client could not have used.

### A mistake worth not repeating: apt.dat has no comments

The first version of this package was commented for readability. **apt.dat has
no comment syntax** - not `#`, not anything - and WED refused the file at the
first comment line.

The mechanism is quieter than "the reader rejects `#`". `TextScanner_FormatScan`
converts a line's first token with `atoi()`, and `atoi("#")` returns **0** while
the function still reports having read one token - so the line is processed as
record code 0. WED 2.8 skips it like any unknown row and lists it; 2.7 fails the
file. **Blank lines are fine** in both: they tokenize to nothing and are skipped.
X-Plane loaded the commented file without complaint, which is why the mistake
survived a successful sim test. A silent sim load does not mean a well-formed
file.

## What each stand should spawn

Computed by `tools/scripts/airline_research/livery_sample_expect.py` against the
schema 4 index (data `20260925`, 44 `Obsolete` marks) and the Global Airports of
X-Plane 12.4.3-r2 - op class, Obsolete (R25), range from the operator's nearest
hub (R26) and HOME (R27) all applied, the same predicate WED's Liveries tab uses.
**Regenerate this table with that script whenever the index changes**; it takes a
few seconds.

"Out of range" names the rows the range rule removed at this stand, which is
what WED's readout lists under its headline.

| stand | rows | should spawn | notes |
|---|---|---|---|
| `01-CONTROL` | `C airline dal ual`, no 1313 | today's behaviour (R17) | the control - see below |
| `02-PIN-C` | 1313 class C only | **nothing** | DAL and UAL narrowbodies cannot reach Beijing (out of range: A320, B738) |
| `03-MIX-CDE` | C 60 / D 30 / E 10 | DAL or UAL `B763` at D; **empty 70%** | C out of range; at E both types are now `Obsolete` (UAL `B744`, DAL `B772`) |
| `04-ALL-ZERO` | six zeros | nothing, **by choice** | legal - same as op type none (§4.2); no warning |
| `05-CLASS-F` | F only | nothing | the library ships no F-class livery at all - ahead of the art, not an error (R14) |
| `06-UNFILLABLE` | D only, `baw` | nothing | BA has C and E, **no D**. A waivable validation warning (`warn_ramp_livery_parks_nothing`), never an export block |
| `07-EXCLUDE` | C, `dal` + `1312` | nothing | `1312` is gone since draft 7: skipped and listed as an unknown row. DAL's C types are out of range |
| `08-WHITELIST` | E, four airlines + `1312` | AAL or BA `B772` | `1312` skipped; UAL and DAL have no current E type |
| `09-MIXED-SIGILS` | same, two `1312` tokens | AAL or BA `B772` | as 08 |
| `10-PLUS-WINS` | E, `dal ual` + `1312` | nothing | `1312` skipped; neither has a current E type |
| `11-BAD-5-WEIGHTS` | five weights | today's behaviour | 1313 dropped **whole** (R5), so it behaves like 01. Must not read as `0 0 10 0 0 0` |
| `12-BAD-DECIMAL` | `1.5` | today's behaviour | dropped whole (R5, V10) |
| `13-BAD-NEGATIVE` | `-5` | today's behaviour | dropped whole (R5, V12) |
| `14-BAD-REFINE` | C + bad `1312` | nothing | `1312` skipped as a whole row; the 1313 is valid, and C is out of range |
| `15-UNKNOWN-ROW` | C + `1314` | nothing | `1314` skipped, not fatal (R15); C out of range |
| `16-EXCL-NO-WEIGHTS` | `1312`, no 1313 | today's behaviour | a skipped row does not switch on three-stage selection (R17) |
| `20-CN-CONTROL` | C, `cca csn ces csz` | all four (A320, B738) | Chinese narrowbodies at a Chinese airport |
| `21-NEVER-HERE` | C, `swa eju` | nothing | neither reaches Beijing (out of range: SWA B738, EJU A320) |
| `22-FOREIGN-NARROW` | C, `ual dal aal baw` | nothing | the United 737 in Beijing - every narrowbody out of range |
| `23-FOREIGN-WIDE` | E, same four | AAL or BA `B772` | the other half of 22. UAL and DAL left it when their E types were marked `Obsolete` |
| `24-SPLIT-IN-CLASS` | D, `ual ups` | UAL `B763` only | UAL's `B752` (7,200 km) filtered, its `B763` (11,000 km) kept - both class D. UPS is cargo, not a candidate on an airline stand (V35) |
| `25-MIXED-STAND` | C 50 / E 50, `cca ual` | CCA at C; **empty 50%** | UAL's C types out of range; UAL has no current E type |
| `26-CARGO-SPLIT` | D, `cargo fdx` | FDX `B752` or `B763` | FedEx's Hong Kong and Tokyo hubs put both in range. Its `DC10` (MD-10) is `Obsolete` |
| `27-DOMESTIC-FAR` | C, `cca` | CCA A320, B738 | domestic control |

### The one that matters most

`01-CONTROL` against `11`, `12`, `13` and `16`. **R2 says a reader that discards
every new row must produce exactly today's behaviour**, so each of those four -
all carrying something broken or unknown - must behave identically to stand 01.
If one differs, the soft-fail rule has a hole. That is the regression test worth
automating.

Stands 02, 07, 14 and 15 used to be checked the same way when the package sat in
Kansas. At Beijing they park nothing because of the range rule, so what they
still test is **reading**: WED must accept the valid 1313 and list the skipped
row, and the sim must not fail the file.

### Kinds of empty, which the readout must tell apart

| kind | stands | what WED's readout says |
|---|---|---|
| the author chose it | 04 | "will not spawn any static aircraft" - grey, no warning |
| nothing exists yet | 05 | "no aircraft exists at size F" - grey; starts working the day one ships |
| the listed operators cannot fill the size | 06 | red, plus the validator warning |
| they fly it, but cannot reach | 02, 21, 22 | red, naming what was out of range |
| part of the distribution is unfillable | 03, 25 | the occupancy percentage |

None of these should trigger a fallback in the sim (§4.5).

## Why the range bench is at Beijing

**20 and 27 are the controls.** Chinese narrowbodies at a Chinese airport are
correct and common; any filter that touches them has broken more than it fixed.
Zeroing the stand's class-C weight - the first mechanism considered - fails
here, which is why it was abandoned.

**21 is the easy case.** Southwest and easyJet Europe each have one aircraft and
neither reaches, so the whole operator fails. Every mechanism handles this one.

**22 is the headline case**, and **23 is its other half** - it must stay filled.
A filter that empties 23 has confused "this operator cannot reach" with "this
aircraft cannot reach".

**24 is the one that kills the per-operator approach.** United's 757 cannot reach
and its 767 can, and both are class D: no action on the operator as a whole, and
no adjustment of the stand's weights, keeps one and drops the other (§6.7).

**25 is the mixed stand.** CCA's 737 is right here and United's is not, on the
same stand.

**26 shows why hubs beat country centroids.** Measured from the US centroid,
FedEx's 757 fails; from its nearest hub, Hong Kong (1,980 km), it passes - and
FedEx does fly into Beijing from there.

Distances are great-circle (haversine) from the operator's nearest hub, the hubs
being the HUB ICAOs on its OPERATOR record, placed by Global Airports: United
from KSFO 9,510 km, British Airways from EGLL 8,150 km, FedEx from VHHH 1,980 km,
Air China from ZBAA ~0. No domestic exemption is needed - a domestic operator's
nearest hub is close by definition.

## Everything about a stand is at the stand

There are no shared definitions and nothing to resolve. Draft 5 had named
policies defined once and referenced per stand; §8.2 measures that as both larger
on the real global apt.dat and harder to read. Two stands wanting the same thing
write the same two rows.

## Regenerating

- **Expected results:** `python tools/scripts/airline_research/livery_sample_expect.py "docs/livery_sample/ZZZ_livery_format_sample/Earth nav data/apt.dat" [<X-Plane root>]`.
- **The stripped package:** the full file with every `1312`, `1313` and `1314`
  line removed, and the `1200` header line saying so.
- **A new stand:** copy the last `1300`/`1301`/`1313` group and advance the
  longitude by `0.001116` (95 m).

Still missing: a stand carrying **two `1313` rows**, to exercise R24's
first-wins rule. `livery_sample_expect.py` reports it when one exists.
