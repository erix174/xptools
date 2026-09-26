# Livery picker — open items

The one live list for `feature/ramp-livery-picker`. The Roadmap, both audits and
ProjectNotes it replaced were archived on 2026-09-25 to
`WED/livery_history/2026-09-25_superseded_docs/`, outside the tree. Local
planning only: this file does not go upstream either.

**Checkpoint 2026-09-25**, taken before Moderation Mode starts. The tree is clean,
Release and Debug build, and everything is pushed to `local`.

## To tell colleagues — once everything is done, in one go

Nothing here is sent before the feature is complete (Eric's call, 2026-09-25).

| For | Item |
|---|---|
| Jim K. (sim) | **Row code 1313** is live in WED and exported everywhere, Gateway included, as of 2026-09-25. It does not crash X-Plane (unknown rows are skipped - measured, spec §7.2). Ask him to confirm the code, or give the one to use; changing it is one constant in `AptDefs.h`. |
| Jim K. (sim) | Whether the apt.dat **version** is bumped for this. Our recommendation: no (spec §9.2). |
| Jim K. (sim) | Reader rules the sim must implement: R25 (NOTE `Obsolete` never spawns), R26 (range from the operator's hub ICAOs, placed from Global Airports), R27 (`HOME` = only in the operator's own country), R28 (GA: 70% home-registered, no range), R29 (None = no static aircraft). All in `WED_LiveryFormatSpec.md` §1 and §6.6–6.7e. |
| Jim K. (sim) | Spec §4.5: 29.8% of airline/cargo stands would park nothing if weighted to their declared class, mostly because of two `Obsolete` marks (Delta CRJ-200, Air France CRJ-100). The sim must not add a fallback. |
| Laminar (release) | `livery_index.txt` (schema 4) must **ship with X-Plane 12.5** under `apt_aircraft/`; WED 2.8 turns livery features on when it finds it. |
| Gateway team | **The 100-character limit on a ramp start's airline string** (`WED_Validate.cpp`, Gateway target). About 24 three-letter codes. Auto-fill stops short of it, so a busy airport cannot list all the airlines that serve it; Eric would like 40–50. Ask whether it can be raised, and to what. |
| Gateway team | Airline codes now take the form `[a-z0-9]{3,4}(_[a-z0-9]{1,6})?` (`ryr_1`, `afr_f`, `xpzz_b752`) - check the server's own validation accepts them. |
| Release manager | 2.8 targets X-Plane 12.5 only. `earth.wed.xml` stays readable by 2.7 (operation types are stored under their original names). |
| WED maintainers | Six small upstream PRs, independent of the feature - see below. |

## Decided 2026-09-25 (evening)

- **The CRJ `Obsolete` marks stay**, Delta's CRJ-200 included. A batch of new CRJs is coming soon to fill those stands.
- **Military and GA stands that park nothing now warn**, with the same waivable `warn_ramp_livery_parks_nothing`. The message names the size as the cause. The check also gained the operation-class filter the tab always had: a cargo operator no longer satisfies a passenger stand.
- **The installed sample packages were replaced** by `docs/livery_sample/` in both installs. The old ones were kept in the session scratchpad only.

## Next: Moderation Mode

- **Map view, 2026-09-26.**
  - **Colour is similarity.** Every ramp start's silhouette is drawn in its signature colour while moderation is on, selected or not.
  - **Callouts for the selection come in three densities:**
    - Up to 5 stands: full cards.
    - 6 to 40: one-line chips in a right-edge column; hovering a chip opens its card.
    - More than 40: a legend of the distinct entries. Hovering a row rings its stands; clicking selects them.
  - **Cards:** the header floats above the top edge, the body has top and left edges over a 40% black fill, and the fill runs down through the open tray.
  - **Pin and compare:** + / - / ~. Chips show the counts; shift-click a chip to pin it.
  - **"?" opens a live web search** in a small Edge or Chrome `--app` window beside the cursor, in the moderator's own profile. A fresh profile hit Google's "unusual traffic" check. With no such browser it falls back to the default browser.
  - The query is "Does <operator> fly to <ICAO> <city>". The author's airport name was noise.
  - Everything is gated on `WED_ModerationEnabled()`: today always on, Moderation Mode only before release.
  - Same-signature stands share one callout (`03-MIX-CDE x4`). Every leader curves into one hub, and a neck runs from the hub to the callout. The neck's length grows 2.5x to 20x and its weight 3 to 16 px with the number of stands. Leaders head for the hub and never double back (2026-09-26).
  - The chip column takes whichever side covers fewer stands, ordered to minimise crossings.
- **Moderation toolbar, 2026-09-26.** Bottom of the map's tool column, aligned to the bottom and growing up. Art is `moderation_tools.png`.
  - **Moderation View** (plane with "!"):
    - Stands that need nothing are greyed; stands to check keep their colour and get an amber ring.
    - An airport overview opens top-left: stands, reviewed x / N, to check, distinct entries, auto-filled, None, and the list of stands to check. Click one to select and centre it.
  - **Rotate canvas**: placeholder button, nothing behind it yet.
  - **Next / previous stand to check.** Airport menu; Shift+X on the map, Ctrl+Shift+X anywhere.
    - Shift+X is deliberately not a menu accelerator: Windows accelerators are global and would eat capital X in every text field.
    - Greyed outside moderation.
  - **Op type None** stands draw a grey silhouette. Chips show "A" (auto-filled) and "None".
- **Review status, open (Eric to confirm first):**
  - "Reviewed" is session-only today.
  - To persist it we need to know whether a Gateway upload carries earth.wed.xml. If it does not, the file has no carrier for it, and the Gateway would need its own field.
- **Rotate canvas, research (2026-09-26):**
  - What works for us: `WED_MapZoomerNew::LLToPixel` / `PixelToLL` are the single projection every layer uses, so a rotation about the view centre there moves most drawing and all hit-testing at once. `GetMapVisibleBounds` already samples 8 edge points, so culling survives a rotated view.
  - Also needs work:
    - 6 files use the axis-split helpers (`LonToXPixel` / `YPixelToLat`...).
    - Heading-drawn icons and silhouettes need the angle added.
    - Pan and marquee selection happen in pixel space.
    - The tilt buttons already play with the projection matrix.
  - Suggested first step: rotation in the zoomer only, then fix what visibly breaks.

- **Map view, open:**
  - Mac and Linux search windows are untested: they use `open -na` and `--app`.
  - The legend tier was only seen with a lowered threshold: the sample has 24 stands.

## Open, no decision needed

- **Mac / Linux build and run** - never done. Eric tests on his Intel Mac before hand-over. The new code has nothing platform-specific (checked 2026-09-25).
- **Data:**
  - 419 country-level hubs ("approx") - barely matter for range.
  - AHY B752 is stored, not confirmed retired.
  - The CYH Peacock row lost its caption to its `Obsolete` mark.
- **Sample:** a stand carrying two `1313` rows (R24).

## Upstream PRs (ready, not sent)

- Local branches off `origin/wed_270_release`, each one built on its own, in worktree `WED/xptools-upstream`, backed up to `local`.
- When the feature itself goes up, rebase it onto these.

| Branch | What |
|---|---|
| `upstream/save-and-doubleclick` | A failed save no longer reads as saved; a package-list double-click works (0.4 s wall clock) |
| `upstream/text-field-word-select` | Double-click selects a word; the table edit field grows to fit. Stacked on the branch above |
| `upstream/ctype-and-validate-country` | Signed-char ctype calls; a bare "USA" country aborted validation |
| `upstream/exit-logging` | DEV log flush and exit-path logging |
| `upstream/apt-unknown-rows` | Unknown apt.dat rows are skipped and reported instead of failing the file. The 1313-free version, tested end to end on 2.7 |
| `upstream/parking-icons-d-e` | ClassD and ClassE icons fill their frame like the other four |
| *(to split)* | `GUI_Window::ClickDown` finishes a lost click instead of asserting (on the feature branch, `774cde5`) |

`archive/stash-2026-09-13-wip` preserves the pre-commit WIP stash of 2026-09-13. Everything in it was superseded by the branch; it was removed from the stash list.

## Done, 2026-09-25 (details in git log)

- **Pane:**
  - `WED_LiveryPane.cpp` split into five files.
  - The readout names the military countries a stand draws from, and says GA comes "from all over the world".
  - UAL's 767 card got its flag.
  - The placeholder airline list was removed.
- **Data:**
  - 7 suspicious operator records checked; 6 corrected.
  - 44 `Obsolete` marks: 12 superseded assets, 20 retired airframes, 12 rows of 11 defunct operators.
  - US military transports and trainers marked HOME.
  - Everything synced to both installs.
- **Docs:**
  - Spec draft 8, with §4.5 and §8.6 re-measured.
  - The Brief.
  - The sample README, regenerated by `livery_sample_expect.py`.
  - The WEDLivery section of `WED_Architecture.md`.
- **Eric's nine-point list (2026-09-17)** is closed.
  - Points 3 and 4 are waived in favour of the occupancy readout.
  - Points 5 and 6 are now built.
