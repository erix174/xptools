# Static aircraft at ramp stands — the short version

For Jim K. 2026-09-17. Targets WED 2.8.0 / a matching X-Plane.

**This is the 5-minute version.** The full specification is
`WED_LiveryFormatSpec.md` — it is long on purpose, written to be handed to an
assistant, and you should not need to read it to answer the questions below.

---

## The ask, in one paragraph

WED is gaining a UI where a scenery author says which airlines park at a stand,
and which ICAO size classes may spawn there and in what proportion. That second
half needs somewhere to live in apt.dat. We propose **one** new row, `1313`,
written at the stand it describes: six integers, a relative weight per wingspan
class A–F. **`1301` is not modified and not deprecated.** A reader that ignores
the new row behaves exactly as today.

There is a second ask, and it is not an apt.dat change: **one reserved value in
the livery index** (§4 below). Between them they replace everything the earlier
two-row draft was trying to do.

---

## What we need from you

### 1. A row code — 10 minutes

We used `1313`. **Any unused number works**; we need yours before we write the
importer. This is the only one blocking us.

It was four rows, then two, now one. The grouping machinery went when we measured
what it actually shared and found it was six integers (spec §8.2). The
refinement row went when we measured how often an operator has more than one
aircraft at a given size class: **15 of 168 pairs in the shipped library**, and
90% of operators have assets at exactly one class anyway (spec §8.6). Each time
the measurement said the mechanism was bigger than the thing it managed.

### 2. Do we bump the apt.dat version — your call, no wrong answer

We assumed a bump was forced. **It is not.** We tested it rather than asking
you, on two builds — 12.4.4 in the beta lane and 12.4.3-r2 in the release lane.
Both ignore unknown row codes *and* accept a version number that does not exist.
So the new rows can ship in a `1200` file and every existing sim keeps working.

The sample package (below) is the proof you can repeat in five minutes: it is a
`1200` file carrying the new rows, it starts a flight, and the sim says
nothing — in a run where it *did* complain about an unknown metadata key
elsewhere. Evidence: `docs/livery_evidence/`.

So a bump is now a question about *signalling*, not compatibility. Our
recommendation: **don't bump.** It buys nothing and costs the Gateway a
two-version problem.

### 3. One that is not yours alone — needs the release manager

An **old WED cannot open a file containing these rows at all.** Not degraded —
it refuses the whole file and imports nothing. That is a WED behaviour
(`AptIO.cpp:1208`), not a sim one, and it is the reverse of the sim's tolerance.

| | opening 2.8-era scenery |
|---|---|
| X-Plane 12.2.x | fine — reads `1301`, ignores the rest |
| WED 2.7.x | refuses the file |

Three ways out: Gateway strips the new rows for old clients; a 2.7.x patch that
skips unknown rows; or accept that 2.8-era scenery needs 2.8-era WED. We lean
to the third with the first as a safety net, but it is a Gateway policy call and
we are not making it for you.

---

## Something to play with first

`docs/livery_sample/ZZZ_livery_format_sample/` — copy it into `Custom Scenery/`.
No DSF, no objects, airport data only.

Sixteen stands in a line at a fictional **ZZLI** (flat western Kansas), each
demonstrating exactly one thing and **named so you can read it off the ground**:
`01-CONTROL`, `04-ALL-ZERO`, `05-CLASS-F`, and so on. Stands 01–10 are what WED
will emit; 11–15 are deliberately broken and must all end up behaving identically
to `01-CONTROL`.

> **The package predates draft 7 and six of its stands are stale.** `07-EXCLUDE`,
> `08-WHITELIST`, `09-MIXED-SIGILS`, `10-PLUS-WINS`, `14-BAD-REFINE` and
> `16-EXCL-NO-WEIGHTS` all exercise the `1312` refinement row that draft 7
> deleted — ignore them, and note that their `1312` rows are now simply an
> unknown row code, which is itself a valid test of "ignore what you don't
> recognise". The other ten stands are current. We will regenerate the package
> once you give us a row code, since every `1313` in it has to change anyway.

`docs/livery_sample/README.md` has a table of every stand and its expected
result, so you can check an implementation against it line by line.

Two stands are worth looking at before anything else:

- **`05-CLASS-F`** asks for class F. **No F-class livery exists anywhere in the
  library**, so it is permanently empty — a data gap, not a format error.
- **`06-UNFILLABLE`** lists only BAW and asks for class D. BAW has liveries at C
  and E and **none at D**. This is the 17.2% case, live. WED will refuse to
  export it.

It already runs: we loaded it in 12.4.3-r2 and started a flight there, with no
complaint from the sim.

---

## What we are asking the sim to do

One behaviour change, and it is the part worth your attention:

```
1.  pick a CLASS      weighted by the six integers in 1313
2.  pick an AIRLINE   uniformly among those in 1301 that HAVE a livery in
                      that class  (not merely "have one somewhere")
3.  pick a LIVERY     uniformly among that airline's liveries in that class,
                      skipping any marked Obsolete, honouring EXPORT_RATIO
```

**Class first, and the order is the whole point.** Do it as one flat weighted
draw and the *size of the asset library* decides the outcome: 120 B738 liveries
against 42 A359 means two classes set to equal probability still come out
overwhelmingly 737s. The author's intent gets silently overridden by how much
art happens to exist. Same bias one level down, which is why airline is its own
stage.

**Stage 2 must ask about *this* class, not about the operator in general.**
United's only class-E aircraft in the library is a 747-400 they retired in 2017.
Ask "does United have a livery?" and they get drawn at class E, find nothing, and
the stand parks nothing 14% of the time. Ask "at class E?" and they are simply
absent there while still parking at D. It is one line of difference and it is the
single easiest thing to get wrong.

**Gate all of this on `1313` being present.** No `1313` row → today's behaviour,
untouched. Otherwise you change the look of all 20,108 airports that have ramp
starts, none of whose authors asked for it.

### The second ask: one reserved value in the index

`Obsolete` in the index's NOTE column means **never spawn this livery** — not at
stage 2, not at stage 3. One string comparison when you load the index.

It exists because the object must stay on disk. Twelve liveries in the shipped
library are old assets superseded by newer ones for the same real aircraft, and
both are exported, so those aircraft currently spawn at double the rate of their
neighbours. Deleting the old export fixes that and breaks every scenery pack
referencing it by hard path. A note fixes it and breaks nothing.

The same mechanism retires an airframe: an operator who no longer flies a type
stops parking one everywhere at once, with no apt.dat edit anywhere. That is
what earlier drafts were spending a whole second row code on, one stand at a
time.

### The one thing we want you *not* to do

Class-first gives up the step-down's one virtue: it always found *something*.
A stand can now be legitimately empty. **Please don't add a fallback.** An empty
stand is the correct reading of what the author wrote, and a step-down takes
back the expressiveness this whole change exists to provide.

We know the scale of that, because we measured it against the real global
apt.dat: naively migrating every stand to its own declared class would empty
**7,604 of 44,242 stands (17.2%)**, at **42% of airports**. Almost never
because the airline has no models — because it has none *in that class*.

**We absorb that, entirely on the WED side**, and in two places rather than one:

- **While the author edits** — an always-visible sentence saying what the stand
  will actually do, recomputed on every change:

  > *This ramp will spawn Emirates aircraft, size D-E, 70% of the time.*

  The percentage is occupancy, so "70%" **is** the empty-stand number, stated
  where an author cannot miss it, and the delta shows at the moment of the click
  (`empty 3% → 31%` when an operator is deselected). An empty stand is invisible
  in the sim; this is what makes it visible before the file is ever written.
- **At export** — weights pointing at a class none of the listed airlines can
  fill is a hard error, not a dismissible warning, with one-click repair offered
  at the point of failure.

The readout matters more than the error, because a file's stands are usually
filled in bulk across a whole airport rather than one at a time, and an error at
the end of that has nothing useful to say about which of 300 stands the author
actually meant to leave empty.

---

## What you get from us, and when

| | state |
|---|---|
| `livery_index.txt` — 298 liveries with type, class, operator, registration, country, livery note | **exists**, generated + hand-maintained |
| The format spec | **this document set** |
| WED reads the index, previews real aircraft | index loader written; first consumer wired this week |
| WED reads/writes `1313` | blocked on your row code |
| Validator + one-click fill | designed, sized against real data |

The index is the piece you may not have expected. `library.txt` buckets objects
by (operation type, size class, airline) and carries **no aircraft type and no
livery axis** — one `heavy_e` bucket mixes A359, A35K and B772. So "United's 737
in the retro livery" cannot be named today even though both objects have shipped
for years. The index is the missing half, it lives next to the assets at
`Resources/default scenery/sim objects/apt_aircraft/`, and whatever ships it
must ship it from the same build as the assets — it is install-specific and a
mismatch fails silently.

---

## Three defects we found in the shipped library

By-product of building the index, offered as data rather than complaint:

- **38** cases where an old and a new asset for the same aircraft are *both*
  `EXPORT_EXTEND`ed (`AT45_FDX_static.obj` and `ATR42-500_FedEx.obj`). Under
  stage 3 above, each of these gets **double the spawn probability** of its
  neighbours.
- **6** objects on disk that `library.txt` never exports.
- `heavy/B772_AAL/` and `heavy/B772_AAl/` — byte-identical folders, ~13 MB
  wasted, whose two `library.txt` lines point at **different** paths. Fine on
  Windows and macOS, broken on a case-sensitive filesystem.

---

## If you only remember five things

1. `1301` is untouched and authoritative. Everything new is additive and
   discardable.
2. The new row must **soft-fail** — a bad one is dropped whole, never fails the
   file. This is the opposite of the rest of `AptIO.cpp` and it is deliberate.
3. Selection is **three stages, class first**, gated on `1313` existing — and
   stage 2 asks about the class drawn, not about the operator in general.
4. `Obsolete` in the index NOTE column means never spawn. One comparison, and it
   is what lets us leave `library.txt` alone.
5. We need **one row code** from you. Everything else can proceed without you.
