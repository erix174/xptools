# Livery conformance kit

What WED 2.8's rule says can park at every ramp start, for comparing with what
X-Plane 12.5 actually parks. The rule is the one in `WED_LiveryFormatSpec.md`
(§4.1, R17-R33); WED's Liveries tab, auto-fill, Validate and Moderation View all
use it, so this is also what authors and moderators are told.

## Files

| file | airport |
|---|---|
| `ZBAA_sample.tsv` | `docs/livery_sample/` - one stand per rule, including the malformed ones |
| `LFPG.tsv` | Paris Charles de Gaulle from Global Airports - 500+ stands, every operation type |

Each header states the WED build and the livery index version it was made
with; compare only against the same index.

## Format

Tab-separated, sorted by stand, class, airline, path. Columns:

`stand lat lon op equipment weights class airline type path verdict`

- One line per livery row at a class the stand opens (its weights, or for a
  legacy stand every class at or below its `1301` letter), with the verdict of
  `eligible()`: `yes`, `equipment` (P3), `out_of_range` (R26), `home_only` (R27).
- Code-level lines (`class` = `-`): `wrong_operation_class`, `no_livery`.
- A final `STAND` line per stand: `parks at C D`, `PARKS NOTHING`,
  `none: nothing parks (R29)`, or `not judged`.
- `Obsolete` rows never appear (R25). GA stands and military stands with no
  airline list try the whole library (R28, §4.1).

## How to use it

Have the sim print, for the same airport, the eligible rows per stand in the
same columns (the `verdict` of a row the sim does not consider can be left
out), sort it the same way, and diff. Every difference is a bug on one side or
an open item in spec §0 (P2 country source and P3 equipment are the likely
ones).

## Regenerating

In WED 2.8 with Moderator Mode on: open the airport, make it current, turn on
Moderation View, and **Shift+click** "Copy Summary to Clipboard" in the airport
overview. The clipboard then holds this report instead of the moderation
summary (`WED_LiveryConformanceReport`, `src/WEDLivery/WED_LiveryModeration.cpp`).

`tools/scripts/airline_research/livery_sample_expect.py` is **not** the
reference: it predates equipment, HOME on every class, GA without range, pool
stands and the legacy step-down, and disagrees with WED on all of them.
