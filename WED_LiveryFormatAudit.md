# Format audit — row `1313` measured against the real world

Audit of `WED_LiveryFormatSpec.md` draft 7, 2026-09-17. Three questions:
**is it robust, is it comprehensive, and does it actually improve on what
X-Plane does today.**

This is not a review of the prose. Every claim below is measured against the
shipped global `apt.dat` (380 MB, 12,351,496 lines, X-Plane 12.4.4) crossed with
the shipped livery index (298 rows, 12.4.3-r2). Where the audit and the spec
disagree, the numbers are here to be re-run.

`WED_LiveryAudit.md` is the *code* audit and is a different document.

---

## 0. The spec's own numbers hold up

Re-derived from the source data rather than taken on trust. Every headline figure
reproduces exactly:

| spec claims | audit measures | |
|---|---|---|
| 38,888 airports | 38,888 | ✓ (`1` + `16` + `17` — 31,269 land, 682 seaplane, 6,937 heliport) |
| 200,102 ramp starts | 200,102 | ✓ |
| 44,242 carrying airlines | 44,242 | ✓ |
| 17.2% would spawn nothing | 15.4% + 1.8% = **17.2%** | ✓ — 6,814 cannot fill their declared class, 790 have no asset at all |
| 12,351,496-line apt.dat | 12,351,496 | ✓ |

One caution about the first row: **38,888 is not the relevant denominator.**
Heliports and seaplane bases do not have terminal stands. The population this
format acts on is the **3,958 airports carrying airline data**, and the document
uses both figures in different places, which reads as broader coverage than the
change actually has.

---

## 1. Robustness

**The format's strongest robustness property is that it has almost no surface.**
One row code, six integers, no name, no reference, no shared state. Most of what
could go wrong was designed out rather than defended against.

| input | handling | vector |
|---|---|---|
| malformed weights — 5, 7, negative, decimal, out of range | R5, dropped **whole**, never partially applied | V8–V12 |
| two `1313` on one stand | R24, first in file order wins | V17 |
| `1313` with no preceding `1300` | R20, dropped | V32 |
| unrecognised row code | R15, ignored | V18 |
| all six weights zero | legal — "nothing parks here" | V2 |
| comment line | R19 forbids emitting one; apt.dat has no comment syntax | — |
| every new row stripped | R2, byte-identical to today | V22 |

### Four real gaps, in severity order

1. **No diagnostic channel at all.** A row dropped at parse time is gone
   silently and permanently. This is a deliberate trade (spec §9) — `AptIO.cpp`
   has no non-fatal diagnostic and adding one touches a parser shared with
   MeshTool, DSF2Text and RenderFarm — but it means a hand-edited file loses data
   without a word.
2. **An index/install mismatch is undetectable in-band.** Nothing in `apt.dat`
   references the index, so the same file legitimately means different things on
   different installs, by design, with no way to notice. The index carries
   `# schema` / `# data` / `# source` / `# assets`, but only a reader that
   chooses to look will see them — and an index produced outside the generator
   may carry none at all, as one on this machine does.
3. **`Obsolete` has global blast radius guarded only by a lint a human must
   read.** One careless line withdraws a livery from every airport at once, with
   no per-stand undo. Marking `B752` would empty twenty (operator, class) pairs.
   The guard works; it is not enforced.
4. **R23 rewrites the one field old readers depend on.** Auto-correcting the
   `1301` size letter to the highest weighted class is reasoned (an old sim
   should step down from the largest class the author allows) but it is R1's only
   exception, and it mutates the single value the entire backward-compatibility
   story rests on.

**Assessment: strong.** The gaps are known, argued and — except (2) — bounded.

---

## 2. Comprehensiveness

### The need is real, and it is the common case

For every stand carrying airlines, how many distinct wingspan classes its listed
operators can actually fill:

```
   0 classes:    790   1.8%
   1 class:   11,524  26.0%
   2 classes:  3,701   8.4%
   3 classes: 17,898  40.5%   <-- most common
   4 classes: 10,326  23.3%
   5 classes:      3   0.0%
```

**72.2% of airline-carrying stands have operators spanning two or more classes**,
and 63.8% span three or more. Today the author describes that with **one letter
and a fixed step-down** — no control over proportion, no floor, no way to say
"this apron is Bs". The six weights give six degrees of freedom to a situation
that is multi-class in nearly three stands out of four.

This is the audit's clearest finding in the format's favour. It is not an edge
case being engineered for; it is the normal shape of a terminal.

### The art is far behind the format

```
shipped liveries by wingspan class
   A:  47    B:  39    C: 156    D:  48    E:   8    F:   0
```

**Class F has no livery at all. Class E has eight.** An author can now write "40%
class E" against eight objects worldwide, and "class F" against nothing. R14
correctly treats this as a warning rather than an error — the author is ahead of
the art and late binding is the whole design — but it calibrates what adopting
this format buys in the short term: **the ceiling is the asset library, not the
format.**

### What it still cannot say

- **Aircraft type within a class** — deliberate (§8.6); 15 of 168 (operator,
  class) pairs are affected.
- **Per-operator weighting** — "mostly Delta, occasionally United". §4.3 notes a
  future row could carry it alongside `1301`.
- **Time of day or season** — never proposed, and worth noting only because it is
  the most obvious thing a reviewer will ask for next.

---

## 3. Does it improve on today?

Three separate claims, scored separately, because they do not all hold equally.

### Claim 1 — the author gains control of the class distribution

**Holds, and this is the real win.** Today: one letter, a fixed cascading
step-down, zero authorial control. New: six free parameters, needed by 72.2% of
stands. No qualification.

### Claim 2 — it removes the asset library's size bias

**Partly holds, and the spec credits the wrong mechanism.**

Measured inside class C, the largest bucket:

```
class C : 156 liveries across 111 operators
          120 of those liveries are B738  (76.9%)
          ...but they belong to 91 DIFFERENT operators
```

So the 737's dominance of class C is **not** one operator owning many liveries —
it is that most operators own exactly one aircraft and it happens to be a 737.
Consequences:

- The **airline stage** corrects operator-level skew. Worst case in the shipped
  library is Nine Air: **7.1% of class-C draws flattened, 0.9% staged — a 7.8×
  over-representation removed.** Real, but smaller than the document implies.
- **Within-class type monotony is not corrected by the airline stage and cannot
  be**, because it is the shape of the library.
- What actually fixes the cross-class bias §4.3 describes — "equal class
  probability still comes out overwhelmingly 737s" — is **the class weights
  themselves.** Weight class D at 50% and half the aircraft are 757s no matter
  how many 737s exist.

§4.3's argument is sound; the credit belongs to stage 1, not stage 2.

### Claim 3 — it is honest about stands that spawn nothing

**Holds, as a trade rather than a pure gain.** Today's step-down always finds
something. The new behaviour empties 17.2% of stands under naive migration, and
the mitigation — a continuous readout — exists only on the WED path. This was
accepted with the numbers in view; the audit confirms the numbers.

---

## 4. Findings not in the spec

### ★ Variety collapse — a failure mode with no name and no defence

```
stands listing two or more operators        : 39,247
   mean operators the author listed         : 7.8
   mean that can appear at the declared class: 4.0
   collapse to exactly ONE operator         : 6,879  (17.5%)
   collapse to none                         : 6,395  (16.3%)
```

**One multi-operator stand in six parks the same airline, every time, forever.**
The author listed eight carriers and gets one.

This is not the empty-stand case, so **none of §4.5's machinery sees it.** It
produces aircraft, so nothing looks broken; the symptom is "this whole pier is Air
France", which does not get reported as a bug. R18 is correct — an operator with
no asset at the class drawn must not be drawn — but its side effect is that the
author's list is silently truncated.

The statistic-field sentences catch it incidentally, because they name only the
operators that can appear. But §4.5 is organised entirely around `P(empty)` and
never says variety collapse is a thing to look at.

**Recommendation: promote it to a first-class readout beside `P(empty)`.** The
design has three layers of defence against "nothing parks here" and none against
"the same thing parks here forever", and the two affect comparable numbers of
stands.

### ★ The index, not the format, is the binding constraint

```
(stand, operator) pairs authors have written : 315,789
   resolve to at least one livery            :  78.3%
   resolve to nothing                        :  21.7%

stands where EVERY listed operator resolves  :  18.9%
```

And the gaps are not obscure regionals:

| operator | stands |
|---|---|
| IBE Iberia | 9,936 |
| SVA Saudia | 9,741 |
| CHH Hainan | 6,193 |
| KAL Korean Air | 3,928 |
| JBU JetBlue | 3,632 |
| NKS Spirit | 3,618 |

**This is not a criticism of the format — it is the strongest argument for it.**
Late binding means the day Iberia ships, 9,936 stands light up with no apt.dat
edit anywhere. But it calibrates the near-term benefit: adopting this format
changes what spawns at a number of stands bounded by the library, not by the
design. The `if available in X-Plane asset library` hedge in the statistic field
is carrying 21.7% of the load.

---

## 5. Verdict

| | |
|---|---|
| **Robustness** | **Strong.** Small surface by design. Four known gaps, all argued; only the undetectable index mismatch is structural |
| **Comprehensiveness** | **Matched to need, ahead of the art.** 72.2% of stands need multi-class expression; class F has zero liveries and class E has eight |
| **Improves on today** | **Yes** — but the gain is the class weights. The three-stage structure contributes a 7.8× worst-case correction, not the headline |

**The one thing most worth fixing:** variety collapse. Three layers of defence
against an empty stand, none against a monotonous one, affecting 17.5% and 16.3%
of multi-operator stands respectively.

---

## Method

Two streaming passes over the global `apt.dat`, joined against the index on
`(airline, class)`. Reproducible with the paths in
`tools/scripts/airline_research/wed_paths.py`:

- **Stand population** — count `1`/`16`/`17` for airports, `1300` for ramp
  starts, `1301` for stands carrying airlines. The size letter is field 2 and the
  operator list is fields 4 onward.
- **Fillability** — for each stand, the union of classes its listed operators
  have any livery at. "Cannot fill the declared class" is the letter on `1301`
  not being in that union; "no asset at all" is the union being empty.
- **Variety collapse** — for stands listing two or more operators, the count that
  have a livery *at the declared class*, which is the stage-2 pool R18 defines.
- **Library shape** — group the index by class and by type; the flattened-versus-
  staged comparison is `liveries(operator) / liveries(class)` against
  `1 / operators(class)`.

Everything here was measured against **12.4.4-pnl5** assets on the audit machine
while the index in the repository describes **12.4.3-r2**. The class-count and
operator-resolution figures move with the install; the stand-population and
fillability figures come from `apt.dat` and do not.
