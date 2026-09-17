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
