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

## Phase 0 — Unblock Jim  *(do this first)*

**Write the apt.dat format spec and send it with a sample file.**

Contents, in order of importance:

1. **The version gate, first and loudest.** apt.dat's reader whitelists versions
   (`AptIO.cpp:313-320`) and refuses anything else — it does not tolerate unknown rows.
   WED's own reader also hard-fails on an unknown row code (`:1208`, `:1217`). Row
   `1301` itself shipped this way, gated at version 1050 (`:738`). Jim needs to confirm
   the sim behaves the same, because it decides whether new rows require a version bump
   — and they almost certainly do.
2. The row layout: `1301` untouched; `1310`/`1311` named policy and reference;
   `1312` exclusions; `1313` class weights. Inline form uses `-` as the policy name.
3. The invariants: `1301` stands alone and is authoritative; policies are scoped to
   their airport block and valid anywhere within it; unlisted classes fall back to the
   `1301` letter rather than meaning zero; new rows soft-fail, never failing the file.
4. The measurements that justify it — 220,610 explicit pairs vs ~0.9 MB of intent,
   53.9% of airline codes currently resolving to no model, 83% of stands sharing an
   airline list with four or more neighbours.
5. Sample apt.dat, updated from the one already sent.

**Also raise, because it is a process question and not a technical one:** new WED will
write a version old X-Plane refuses. Gateway serves both. Who generates which version
for whom? This needs Jim *and* the release manager, and it has no answer today.

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

## Phase 5 — One-click fill

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
