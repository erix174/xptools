# Static aircraft at ramp stands — the short version

For Jim K. 2026-09-16. Targets WED 2.8.0 / a matching X-Plane.

**This is the 5-minute version.** The full specification is
`WED_LiveryFormatSpec.md` — it is long on purpose, written to be handed to an
assistant, and you should not need to read it to answer the questions below.

---

## The ask, in one paragraph

WED is gaining a UI where a scenery author says which airlines park at a stand,
which ICAO size classes may spawn there and in what proportion, and — rarely —
which specific aircraft types to exclude. That needs somewhere to live in
apt.dat. We propose four new rows, `1310`–`1313`. **`1301` is not modified and
not deprecated.** A reader that ignores every new row behaves exactly as today.

---

## What we need from you

### 1. Row codes — 10 minutes

We used `1310`, `1311`, `1312`, `1313`. `1302` was taken, so `1310` was the
first free block. **Any four numbers work**; we need yours before we write the
importer. This is the only one blocking us.

### 2. Do we bump the apt.dat version — your call, no wrong answer

We assumed a bump was forced. **It is not.** We tested it rather than asking
you: X-Plane 12.4.4 ignores unknown row codes *and* accepts a version number
that does not exist. So the new rows can ship in a `1200` file and every
existing sim keeps working. Evidence: `docs/livery_evidence/`.

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

## What we are asking the sim to do

One behaviour change, and it is the part worth your attention:

```
1.  pick a CLASS      weighted by the six integers in 1313
2.  pick an AIRLINE   uniformly among those in 1301 having a livery in that class
3.  pick a LIVERY     uniformly among that airline's liveries in that class,
                      minus 1312's exclusions, honouring EXPORT_RATIO
```

**Class first, and the order is the whole point.** Do it as one flat weighted
draw and the *size of the asset library* decides the outcome: 120 B738 liveries
against 42 A359 means two classes set to equal probability still come out
overwhelmingly 737s. The author's intent gets silently overridden by how much
art happens to exist. Same bias one level down, which is why airline is its own
stage.

**Gate all of this on `1313` being present.** No `1313` row → today's behaviour,
untouched. Otherwise you change the look of all 20,108 airports that have ramp
starts, none of whose authors asked for it.

### The one thing we want you *not* to do

Class-first gives up the step-down's one virtue: it always found *something*.
A stand can now be legitimately empty. **Please don't add a fallback.** An empty
stand is the correct reading of what the author wrote, and a step-down takes
back the expressiveness this whole change exists to provide.

We know the scale of that, because we measured it against the real global
apt.dat: naively migrating every stand to its own declared class would empty
**7,604 of 44,242 stands (17.2%)**, at **42% of airports**. Almost never
because the airline has no models — because it has none *in that class*.

**We absorb that, entirely on the WED side**: weights pointing at a class none
of the listed airlines can fill is a hard export error, not a dismissible
warning, with one-click repair offered at the point of failure.

---

## What you get from us, and when

| | state |
|---|---|
| `livery_index.txt` — 298 liveries with type, class, operator, registration, country, livery note | **exists**, generated + hand-maintained |
| The format spec | **this document set** |
| WED reads the index, previews real aircraft | built, being wired now |
| WED reads/writes `1310`–`1313` | blocked on your row codes |
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

## If you only remember four things

1. `1301` is untouched and authoritative. Everything new is additive and
   discardable.
2. The new rows must **soft-fail** — a bad one is dropped, never fails the file.
   This is the opposite of the rest of `AptIO.cpp` and it is deliberate.
3. Selection is **three stages, class first**, gated on `1313` existing.
4. We need **row codes** from you. Everything else can proceed without you.
