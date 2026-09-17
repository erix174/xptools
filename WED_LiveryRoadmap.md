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

**Built but never run.** `WED_LiveryIndex` — 376 liveries, loader, availability
tri-state, real-path object loading — has **zero callers**. Everything about it is
verified by reading, not by running.

**Designed, not built.** The apt.dat format: `1301` untouched, named policies with
per-stand refinement, content-hash grouping, subtractive storage.

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

`WED_LiveryFormatSpec.md`, now at draft 3, with `docs/livery_evidence/` alongside it.

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

**Verify on the B738** — 120 of the 376 liveries, and it covers every hard case at
once: `UAL` Legacy/Modern, `JYH`'s eleven colours, `CCA`'s five Peony tail numbers,
`RYR`'s four separate AOCs.

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

Shape: `AptGate_t` gains an exclusion list and a weight list. Import copies a
referenced policy into each stand — WED models no "policy" object at all. Export
re-groups stands by content hash and emits definitions with content-derived names
(`g_a3f91c`, not `g1`, so a definition whose content did not change does not churn in
Julian's diffs). Old export targets write `1301` only, which is how an old X-Plane
still gets a usable file.

This is also where `width_min` finally survives export.

---

## Phase 5 — One-click fill, and the validator that must ship with it

**These two are one feature, not two.** Class-first selection gives an author the power
to say "only D spawns here", and with it the power to say it about a class none of their
airlines can fill. Nothing degrades that gracefully any more - the stand is simply empty,
every time, with no symptom except an empty apron.

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

- It produces uniform data across a whole airport, which is the best possible input for
  content-hash grouping. CDG's 326 airline stands should collapse to a handful of
  policies.
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
