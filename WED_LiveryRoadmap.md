# Ramp Livery Picker — implementation roadmap

Target: **WED 2.8.0 alpha**, after 2.7.3 ships. Written 2026-09-16, at commit `5423a7e`
plus the fixes that followed.

This is the sequencing document. `WED_ProjectNotes.md` holds the facts that are not
recoverable from the code; `WED_LiveryAudit.md` holds the end-to-end audit; the plan
file in `~/.claude/plans/` holds the detailed wiring design and the agreed apt.dat
format. This one answers only: **what to do, in what order, and why that order.**

---

## Where the project stands

**Built and hardened.** The Liveries tab, the recommendation list with tri-state
multi-select checkboxes, the size-range slider, the off-screen thumbnail renderer, the
flag pipeline, and three shipped data files. Audited end to end, swept for portability,
two heap-corruption bugs and one old-Mac crash fixed.

**Built but never run.** `WED_LiveryIndex` — 298 liveries, loader, availability
tri-state, real-path object loading — has **no consumer**. To be exact, since
"zero callers" was overstated: `WED_LiveryIndexDefaultPath()` has exactly one
caller, at `WED_LiveryPane.cpp:2717`, and it uses only the resolved *path*, as a
cache key for discarding thumbnails when the X-Plane root changes. Nothing loads
the file or queries it. Everything about the index is verified by reading, not by
running.

Which is what the preview strip still shows: `PickPlaceholderObjectVpaths()`
(`WED_LiveryPane.cpp:835`) grabs the first four `res_Object` vpaths anywhere in
the library, captioned with the literal strings `"ICAO"`, `"Placeholder
Airline"`, `"USA"`. Nothing on those cards relates to the ramp being edited. The
rest of the tab — airline checkboxes, recommendations, size range, flags — is
real.

**Designed, not built.** The apt.dat format: `1301` untouched, two additive rows written
inline at each stand — `1312` refinements and `1313` class weights — subtractive
storage, no grouping and no names. Draft 5 dropped the named-policy machinery it had
carried since draft 1; see spec §8.2 for the measurement that killed it.

**Not started, and not ours.** The X-Plane side. Jim K. cannot begin without a written
format from us.

---

## The ordering principle

Two things drive the sequence, and neither is "what is most fun to build":

1. **Lead time.** The X-Plane side is the long pole and it is blocked on a document we
   have not written. Every day that document does not exist is a day Jim cannot start.
   The wiring is entirely ours to schedule; the spec is not.
2. **Irreversibility.** A format mistake is globally fatal — apt.dat's version check is
   a whitelist (`AptIO.cpp:313-320`), so an old X-Plane refuses a file it does not
   recognise outright. Wiring mistakes cost an afternoon. Get the irreversible thing
   reviewed first.

So the spec goes before the code, even though the code is more obviously "progress".

---

## Phase 0 — Unblock Jim  *(DONE 2026-09-16)*

Two documents now, deliberately split, with `docs/livery_evidence/` alongside them:

- **`WED_LiveryFormat_Brief.md`** — for Jim. The three decisions he owns, a
  recommendation on each, and the one behaviour we are asking the sim not to add.
  ~140 lines, and nothing in it requires reading the other one.
- **`WED_LiveryFormatSpec.md`** — draft 5, the implementation manual, written to
  be handed to an assistant: numbered normative rules, ABNF, reader pseudocode,
  22 conformance vectors, and the index's guarantees.

The split exists because the two readers need opposite things. A decision-maker
needs the three questions isolated from 700 lines of grammar; an implementer needs
the grammar and cannot act on prose.

Two things changed while writing it, both because they were tested rather than assumed:

- **The version gate is a WED problem, not a sim problem.** We expected an unrecognised
  row or version to be refused, because WED refuses both (`AptIO.cpp:313-320`, `:1208`)
  and row `1301` shipped gated at version 1050 (`:738`). X-Plane 12.4.4 does neither: it
  ignores unknown rows, accepts an unknown version, and treats even a genuinely corrupt
  row as non-fatal. So the new rows can live in a 1200 file and old sims keep working.
  The unsolved half is the reverse — an **older WED cannot open** a file containing them
  at all, which is a Gateway policy question rather than a format one.
- **Selection became class-first, in three stages.** Class, then airline, then livery,
  each decided by whoever should decide it. Flattening it lets the size of the asset
  library override the author: 120 B738 liveries against 42 A359 means two classes set
  to equal probability still come out overwhelmingly 737s.

Still open with Jim: row code allocation, whether to bump the version for signalling,
and which apt.dat version Gateway serves to which client.

---

## Phase 1 — Make the preview real  *(commit A)*

The wiring. One card per checked airline, its liveries filtered to the ramp's size
range, the three availability states, selection re-keyed off the airline code, tray
opening instantly with no animation.

This is the first time the feature shows anything true. Until it runs, every
conclusion in this repo about it is theoretical.

Detail is in the plan file; the short version is: expose the asset root, add a
range-aware index query, build the card list in `RebuildSelection()`, delete
`kCardCount` and the placeholder picker, re-key `mSelectedCards`.

**Verify on the B738** — 120 of the 298 liveries, and it covers every hard case at
once: `UAL` Legacy/Modern, `JYH`'s eleven colours, `CCA`'s five Peony tail numbers,
`RYR`'s four separate AOCs.

### The coverage readout belongs here, not in phase 5

Spec §4.5 specifies a `P(empty)` readout. Its weighted form needs `1313`, so it waits
for phase 4 — but **the part that catches the actual defect does not**, and building it
here costs almost nothing extra.

The range-aware index query this phase adds already answers "which of this stand's
airlines can fill class *c*". Run it across the stand's existing `width_min..width`
range and you get, from data WED has **today**, the same signal the 17.2% measures:
*no listed airline can fill any class in this stand's declared range.* No weights
required. Show it per stand as coverage ("4 of 6 airlines, classes C-E"), and it
upgrades to a probability in phase 4 by weighting the same per-class terms with `1313`
instead of treating the range uniformly.

Two reasons to do it now rather than defer the whole thing:

- **It gives `WED_LiveryIndex` a second consumer with a visible symptom.** The preview
  strip alone fails quietly when the index is stale or missing - §6.4's exact
  complaint. A number that reads *index not loaded* does not.
- **It lands the display vocabulary before the numbers get interesting.** Deciding what
  "empty" means on screen is cheaper against coverage than against a weighted
  distribution and a bulk rollup at the same time.

---

## Phase 2 — Build on the other two platforms  *(do not defer this)*

Everything so far is Windows Debug on one machine. Mac and Linux have never been
compiled, let alone run — and the portability sweep found that the failures unique to
those platforms are exactly the ones already lurking: bundle-relative resource paths,
`GL_DEPTH_COMPONENT` unsized under EXT-only drivers, GLEW function pointers, the
case-sensitive filesystem walk on a failed open.

A compile on each is the minimum. A run on a Mac is worth far more.

The longer this waits, the more code sits on top of an unverified assumption.

---

## Phase 2.5 — The `+` whitelist, and the lock icon that shows it

Format side landed as R21 (spec §2 grammar, §4.1, vectors V23-V26): `+air:type`
closes an airline's set to exactly the listed types, `-air:type` leaves it open.
**The UI for it is not built**, and this is the pair Eric deferred as "items 1 and
2" on 2026-09-16.

- **Item 1 — how an author says it.** The airline checklist today is a two-state
  tick. A closed set needs a third state: "this airline, but only these types".
  Needs a per-airline type list to pick from, which is exactly what the tray in
  phase 3 shows — so these two should be designed together, not separately.
- **Item 2 — how an author sees it.** Eric asked for a small **lock icon** reusing
  the `+`/`-` sigil design: a locked row reads "only this one type". The lock is
  the right metaphor because the semantics really are a freeze — and unlike the
  freezing that ruled out candidates A and B, this one is opt-in and local, which
  is worth conveying rather than hiding.

**Do not ship `+` in the writer before this UI exists.** An author who cannot see
that a set is closed cannot tell why a stand stopped spawning the aircraft it used
to, and a closed set is exactly the kind of state that goes stale silently.

## Phase 3 — The tray  *(commit B)*

Animation via `GUI_Timer`, nested wheel scrolling, overlay draw and hit-test priority.

**Decide before building:** the tray looks like a settings list but nothing it shows
can be saved — `WED_RampPosition` stores airline codes and nothing else, and apt.dat
has no field for "United, but not their 777" until phase 4. Either the tray is visibly
provisional, or it waits for phase 4 to give it somewhere to write.

---

## Phase 4 — apt.dat read and write  *(gated on Jim's answer)*

Only after the format is agreed in writing. Touches `src/XESCore/AptIO.cpp` and
`AptGate_t`, which are shared with the wider scenery toolchain.

Shape: `AptGate_t` gains a refinement list and a six-element weight list, and
`WED_RampPosition` gains the matching properties. Import and export are both
straightforward now that the format is inline — the rows read and write at the stand,
bound to the preceding `1300` (R20), with nothing to resolve and no shared object to
keep in sync. Old export targets write `1301` only, which is how an old X-Plane still
gets a usable file.

Two things the reader must get right, both of which are cheap here and expensive
later: soft-fail per R4/R5 — a bad row never fails the file, and a bad `1313` is
dropped **whole**, never partially applied — and a rule for a duplicate `1312`/`1313`
on one stand, which the spec's §3 currently leaves as a silent overwrite.

This is also where `width_min` finally survives export.

---

## Phase 5 — One-click fill, the readout, and the validator

**These three are one feature, not three.** Class-first selection gives an author the
power to say "only D spawns here", and with it the power to say it about a class none of
their airlines can fill. Nothing degrades that gracefully any more - the stand is simply
empty, every time, with no symptom except an empty apron.

### The scale, measured

Against the real global apt.dat crossed with the real shipped livery set:

| | |
|---|---|
| stands carrying airlines | 44,242 |
| **would go completely empty** if weighted to their own current class | **7,604 (17.2%)** |
| **airports with at least one such stand** | **1,675 of 3,958 (42%)** |
| airlines with no asset at *any* class | 790 stands (1.8%) |

That last row is the important nuance. The problem is almost never "this airline has no
models" - it is "this airline has no model **in this stand's class**". A gate marked E
whose airlines only fly C-class aircraft is the common case, and it is invisible today
because the current cascading step-down quietly substitutes something smaller.

So a naive migration - take each stand's existing letter, write it as the weight - breaks
one stand in six and touches nearly half of all commercially served airports.

### What this forces

1. **A validator that cannot be bypassed.** Not a warning the author can wave through:
   weights pointing at a class none of the listed airlines can fill is an export error,
   the same class as the existing gateway validation errors. The data is wrong and the
   symptom is silent, which is the combination that earns a hard stop.
2. **One-click fill has to be offered at the point of failure**, not buried in a menu.
   The validator knows exactly which stands are broken and what would fix them, so the
   error should carry the remedy: set the weights to the classes these airlines can
   actually fill.
3. **Fill has to be a bulk operation from the start.** At 42% of airports and up to 326
   affected stands at a single one (CDG), a per-stand fix is not a fix. Design it as
   "apply to selection" / "apply to airport" from day one - retrofitting bulk onto a
   single-stand action is how this ends up unusable at exactly the airports that matter.
4. **A live `P(empty)` readout, and an airport-level rollup after every bulk fill.**
   Spec §4.5. The validator is a gate at the end; the readout is the thing that stops
   an author reaching it. It answers the question the file format structurally cannot:
   §4.5 point 2 notes that "intentionally empty", "no airline has this class" and
   "everything is excluded" are indistinguishable from outside - but a person looking
   at *"this stand is empty 31% of the time"* can tell which one they meant.

   **The rollup is the half that is easy to skip and must not be.** Per-stand numbers
   have no eyes on them during a 326-stand fill that the author accepts wholesale, and
   that fill is the only mechanism in the product that would ever produce the 7,604
   stands above - R17 means import produces none. So the fill must end with
   *"47 stands filled. 12 will be empty more than half the time, 8 always."*

   Display rules, all from §4.5: `P(empty)` is first-class and not buried;
   de-emphasise per-livery odds, which are the library's business (§4.3), not the
   author's; show the **delta** at the moment of the click (`empty 3% → 31%`), since
   the author's question is "what did I just do", not "what is the number"; and name
   the index version the numbers were resolved against, distinguishing `0%` from
   *index not loaded* (§6.4).

### Quantity is a design input here, not an afterthought

Whoever implements this should size it against the numbers above before choosing an
approach. 20,108 airports have ramp starts; 3,958 carry airline data; the worst single
airport has 326 airline stands. An implementation that is fine at 10 stands and quadratic
at 300 will fail precisely at CDG, ICN, ORD and PEK.

## Phase 5 detail — one-click fill

Applies the recommendation strategy plus the default preset probability across an
airport. Mandatory, per the product decision — the author trusts our recommendations
wholesale.

Two consequences worth designing for up front:

- It produces uniform data across a whole airport — which, now that the format is
  inline, buys nothing at write time and everything at review time. CDG's 326 stands
  will each carry their own two rows; what makes that readable in a diff is that they
  are *identical* two rows, not that they were collapsed into one definition.
- **Record which strategy version filled an airport.** Storing the expanded result is
  correct — an author accepted a specific recommendation and changing it later behind
  their back is worse — but without a version stamp nobody can answer "should this
  airport be re-filled?" later.

Coverage is the prize here: of 20,108 airports with ramp starts, only **3,958 carry any
airline data at all**.

---

## Phase 6 — Weighted class spawning

The bar-chart UI over `1313`'s relative integer weights. Designed, not built. Needs
phase 4 first, since it has nowhere to write until then.

---

## Running alongside

**Index data review — Eric's, and it is his job at Laminar, not something to design
around.** 114 registrations came from an OCR pass at 69% raw accuracy and are marked
"NOT YET EYEBALLED"; 10 are self-flagged. Obvious noise (`DELTA`, `FEDEX`, `JAPAN`,
`BRASIL`) is still in there. Edit
`tools/scripts/airline_research/livery_reg_overrides.txt`, never the generated index.
Must be clean before ship.

**Six rows the generator cannot name, and should not try to.** `jet/B752/` ships
`757PW`, `757PW_winglet`, `757RR`, `757RR_winglet` — four genuinely different
airframes (engine type, winglets) — and `fighter/F15/` ships `F15EX` and
`F15EX_cft`. The variant is in the *filename*, but these folders are bare
`<TYPE>/` with no airline token, so the folder-driven variant rule finds nothing
and all six land on note `Default`. Deriving it from the filename instead would
have to distinguish a variant from an airline name and a registration
(`757_Delta_N654DL.obj`), which is a heuristic that would misfire across the
other 292 rows to fix six. **Hand-edit the note column.** This is the residue of
the same class of bug as the "Default" fallback regression — see §6.3 of the
spec, where it is disclosed rather than hidden.

**Third-party pack scanning.** `livery_index.txt` is currently found at one fixed path,
which means a third-party livery pack cannot contribute to it — the pack would be
invisible, and that kills the ecosystem the subtractive format exists to enable. It
should be scanned across every scenery pack the way `library.txt` already is. Small
change, large consequence; fold it into phase 1 or 4.

**Resource-library defects found by the index**, for Laminar rather than for us: 38
`_DUP` collisions where an old and a new asset are both exported, 6 orphan objects on
disk that `library.txt` never exports, and `heavy/B772_AAL/` vs `heavy/B772_AAl/` —
byte-identical folders wasting ~13 MB whose two `library.txt` lines point at *different*
paths, which breaks on a case-sensitive filesystem.

---

## Recommendation

**Do phase 0 now**, before writing another line of feature code. It is a day of writing,
it is the only thing blocking another engineer, and it is the one decision that cannot
be walked back.

Then phase 1, then phase 2 immediately after — not "eventually". Phases 3 onward can be
scheduled normally.
