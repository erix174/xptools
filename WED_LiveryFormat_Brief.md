# Static aircraft liveries - what the X-Plane side needs to do (short version)

WED 2.8.0, for X-Plane 12.5. 2026-09-28.

This is the one-page version for people. The detailed version, with all the
reasoning and written so an AI agent can work from it, is
`WED_LiveryFormat_SimHandoff.md`. The formal specification is
`WED_LiveryFormatSpec.md` (draft 10).

---

## The idea in three sentences

Scenery authors can now say, per stand, **which aircraft sizes park there and how
often**, on top of the airline list they could already give. X-Plane ships a
**livery index** that says which liveries exist, which are retired, how far each
aircraft type flies and where each operator is based. At load time the sim picks
a size, then an airline from the stand's list that has a usable livery at that
size, then the livery.

## What changes in apt.dat

One new row after each stand's `1301`, only where the author set sizes:

```
1300 40.07800000 116.57223200 000.0 gate heavy|jets 03-MIX-CDE
1301 E airline dal ual
1313 0 0 6 3 1 0        sizes A-F: C 60%, D 30%, E 10%
1315 M                  set by hand (A = auto-filled) - the sim ignores this row
```

- `1301` is unchanged. Without `1313`, a stand parks exactly as today.
- A broken `1313` is dropped and never breaks the file.
- Operation type `none` means no static aircraft.

## What the sim needs to do

1. **Read the index** (`apt_aircraft/livery_index.txt`) - one line per livery and
   one per operator, cells separated by `***`.
2. **Pick in three steps: size, airline, livery**, and use **one** test for "can
   this livery park here" in both the airline and livery step:
   - right size and right kind of operator for the stand,
   - not marked `Obsolete`,
   - close enough to one of the operator's hubs (the livery's `RANGE_KM`; military,
     government and GA are exempt),
   - `HOME` liveries only in their own country.
3. **GA stands:** pick a GA livery of the chosen size, 70% of the time one
   registered in the airport's country when there is one.
4. **If nothing fits, park nothing.** Please do not add a fallback - WED already
   warns the author.

## Please tell us

| | question | our suggestion |
|---|---|---|
| 1 | **Will the sim read `livery_index.txt` as described, and who regenerates it for each X-Plane release?** This is the only answer that could still change the file. | Yes; run the generator in `tools/scripts/airline_research/` when assets change |
| 2 | Are row codes `1313` and `1315` fine? | Keep them. Must be settled before WED 2.8 goes to authors |
| 3 | Where should the sim get an airport's country? (needed for `HOME` and GA) | The airport's `1302 country` plus a small country-code table beside the index |
| 4 | Should the sim also skip liveries that don't match the stand's equipment (jets, props, heavies, helicopters)? | Yes - WED does, and the two should agree |
| 5 | What does today's sim do with operation type `none`? | We assume: nothing parks |
| 6 | Is today's size step-down 75% / 75% of the rest...? | Please confirm the numbers |
| 7 | Is a 299-character airline list OK? (was 99) | Please confirm |

## What won't change under you

The row layout, the index columns and their meaning, and the rules are frozen.
From now on we only change **data** - operators, hubs, ranges, retirements, new
liveries. If a column is ever added to the index, it goes in a place where your
reader ignores it (spec rule R32).

## How to check your work

`docs/livery_conformance/` has WED's answer for every stand of a test airport and
of Paris CDG, one line per livery with "can park / can't, and why". Print the same
from the sim, sort, diff. The test airport itself is `docs/livery_sample/` - copy
it into `Custom Scenery/`; its stands are named after what they test.

## One thing about releases

WED 2.7 refuses apt.dat files with rows it doesn't know. Before 2.8 airports reach
the Gateway, the 2.7 line needs the "skip unknown rows" fix and the Gateway's
server needs WED 2.8. X-Plane itself already skips unknown rows (tested on 12.4).
