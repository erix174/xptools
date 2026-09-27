# WED 2.8.0 - what's new and what changed

For X-Plane 12.5. WED 2.8 is a feature release built on 2.7.3. Its headline
features are static aircraft liveries and Gateway moderation tools. The short
version for the release notes is in `src/WEDCore/README.WorldEditor`; the user
manual (`src/WEDDocs/index.markdown`) covers each feature in full.

---

## New features

### Static aircraft liveries (the Static Liveries tab)

Starting with X-Plane 12.5, the static aircraft parked at gates and tie-downs
wear the liveries of real operators. Authors choose which operators and which
sizes park at each stand.

- **Static Liveries tab.** It opens for the selected ramp starts: automatically
  if **Prompt Up Static Liveries Tab** is on in Preferences, otherwise from the
  tab strip. It shows:
  - the airport, with its country flag and whether WED has airline data for it;
  - the operation type: None, Private/BizJet, Passenger, Cargo, Military/Gov;
  - the size range (ICAO wingspan classes A-F), or per-class **spawn weights**
    (Set Spawn Weights / Simple Mode);
  - a one-line **coverage readout** saying what will actually park, and why
    anything was left out (out of range, wrong size, parks nothing);
  - **operator cards** with a picture of each aircraft, in Recommended /
    Popular Airlines / Same Country / All Airlines sections, with search and
    sort. Click a card to add or remove the operator.
- **None** is a new operation type, meaning no static aircraft at all. It is
  the new default for the ramp start tool.
- **Auto-populate.** Airport > Auto-Populate Static Aircraft (Selected Ramps
  Only), and Populate This Ramp on the tab.
  - Adds the operators WED's airport data lists for the airport, filtered by
    operation class, size, equipment type and range.
  - Gives stands that have only a size letter a default set of weights.
  - Only ever adds, stops before the Gateway's 100-character limit, and is one
    undo step.
  - Stands it filled are marked as auto-filled in the WED project.
- **Where aircraft may park.** WED applies the same rules X-Plane 12.5 does:
  - an airliner parks only within range of one of its operator's hubs;
  - a few government and military aircraft park only in their home country;
  - retired liveries (marked Obsolete in the index) never park.
- **Operator codes.** The Airlines field accepts the operator codes of X-Plane's
  livery index: 3-4 letters or digits with an optional `_suffix`, for example
  `dal`, `afr_f`, `ryr_1`, `xpzz_b752`.
- **Next / Previous Ramp Start** (Airport menu, Ctrl+Shift+. / Ctrl+Shift+,)
  steps through the airport's ramp starts, centres each one and shows its tab.
- **One source, one rule.** The tab, auto-fill, the validator and the Moderation
  summary judge a stand with the same function and read the same data: the X-Plane
  install's `livery_index.txt` plus `WED_AirportDatabase.txt` shipped with WED.
  They cannot disagree.

### apt.dat

- **Row `1313`**: six whole-number spawn weights (0-1000) for classes A-F, one
  row per stand, written right after its `1301`.
  - Written only for stands that use weights, on X-Plane 12 exports, Gateway
    included.
  - Older X-Plane versions ignore the row and park aircraft as before.
- **Operation type `none`** in `1301`.
- **Unknown rows no longer fail a file.** An apt.dat row WED does not recognise
  is skipped, and WED lists it after import and in Validate. WED 2.7 refused the
  whole file.

### Validation

- **"parks nothing"** (a warning, never blocks an export). Validate warns about
  any airline, cargo, GA or military stand where nothing in X-Plane's livery
  index can park, and says why. Needs X-Plane 12.5's livery index.
- **Rows not imported.** A warning lists apt.dat rows that were skipped on
  import (unknown rows, or a malformed or duplicate `1313`).
- **set_AGL is no longer discouraged** on the Gateway. XP12 assets such as the
  3D sidewalks need it. set_MSL is still refused, and the +/-100 m limit on
  set_AGL still applies.

### Moderator Mode (for Airport Scenery Gateway moderators)

Everything below appears only with **Moderator Mode** on in Preferences. The
setting is now saved and takes effect immediately in open documents.

- **Gateway import.** Each imported airport is collapsed in the hierarchy, so a
  batch import lists one line per airport.
- **Hierarchy shortcuts:**
  - A single click on an airport opens and shows all its folders, and switches
    the imagery to ESRI.
  - A double click on Taxiways or Draped Polygons shows only that folder, with
    the imagery off.
  - A double click on Ground Vehicles shows it together with Ground Routes, and
    switches to the Taxi Routes tab.
- **Export warning.** Before Export Scenery Pack or Submit to Gateway, WED lists
  anything hidden (hidden items are not exported) and offers Show All and
  Export, Export As Is, or Cancel.
- **Moderation View** (button at the bottom of the map's tool column):
  - stands that need checking get a "!" badge, and the others are greyed;
  - an airport overview lists the stands to check, with filters (Not listed /
    No data / Foreign / Parks nothing), sort, and a reviewed-by-setup count;
  - **Copy Summary to Clipboard** produces a text report for the Gateway review.
    It states the WED and livery index versions, and lists every static-aircraft
    validator warning and every stand to check.
- **Similarity colours and callouts.**
  - Every stand is drawn in the colour of its setup, so copies show at a
    glance.
  - Selected stands get callouts: a card for up to 5, a chip for up to 40, and
    a legend beyond that.
  - "?" opens a web search to check whether an operator serves the airport.
  - Pin a card to compare the others against it.
- **Next / Previous Stand to Check** (Shift+X / Ctrl+Shift+X).
- **Rotate view.**
  - Drag along a row of stands and the map turns so the row is level.
  - Once there is a reference line, a drag selects, level with the screen, and
    Alt+drag draws a new reference line.
  - Shift+right-drag turns in 90-degree steps from the reference line.
  - Any other tool puts the map back north up.

---

## Changed behaviour (read before upgrading)

- **Ramp start Size is set on the Static Liveries tab**, as a range of classes,
  and is no longer in the Selection tab's property list. apt.dat output is
  unchanged: the largest class is exported as the size letter.
- **New warnings on existing scenery.** With X-Plane 12.5's livery index
  installed, older airports can show "parks nothing" warnings on stands nobody
  has touched. Warnings never block an export.
- **Airline code validation is looser**: 4 characters, digits and a `_suffix`
  are accepted (see above). Codes are stored in lower case.
- **Operation types are relabelled in the UI**: Private/BizJet, Passenger,
  Cargo, Military/Gov. The stored names are unchanged, so WED 2.7 can still
  read 2.8 projects. Saving a 2.8 project in 2.7 drops the spawn weights and the
  auto-filled marks.
- **The Preferences window is larger** and has two new settings: Prompt Up
  Static Liveries Tab, and Moderator Mode (now saved).
- **The property tabs follow the map mode.** When moderator selection switches
  the map to Taxi Routes or Selection mode, the tab strip now switches too,
  except that the Static Liveries tab stays up once it is open.
- **Livery features need X-Plane 12.5** (its `apt_aircraft/livery_index.txt`).
  With an older X-Plane selected, the Static Liveries tab says so and the
  Airlines field still takes codes by hand.
- **Release packages** for Windows and Linux now include `WED_AirportDatabase.txt`
  and the `flags/` folder next to the executable. WED needs both.

---

## Bug fixes

- A click whose mouse-up went to another window (a browser opened from the
  click, a modal, an alt-tab mid-press) no longer terminates WED on the next
  click.
- Centring the map on the 3D preview's camera no longer zooms out a little each
  time.
- A save that fails no longer marks the document as saved.
- Double-click in the package list works reliably (wall-clock timing).
- Double-click in a text field selects a word; the table's edit field grows to
  fit.
- Crashes from signed characters passed to ctype functions, including typing a
  non-ASCII character in the hierarchy search.
- A bare "USA" country in airport metadata no longer aborts validation.
- Parking icons for classes D and E fill their frame like the others.
- A failed Gateway import (no airport in the download) no longer crashes.
- Moderation view: a ramp start with an unknown type name no longer crashes
  WED.
- Clicking a selected airport or folder in Moderator Mode no longer opens it for
  renaming by accident. Rename it on the Selection tab.

---

## For the X-Plane side (sim)

- **Reader rules.** `WED_LiveryFormatSpec.md` §1 and the appendix list what the
  sim must implement: R4/R5/R11, R17, R18, R20, R23-R29, and the index-code vs
  library-bucket note.
- **Row code 1313** is provisional until confirmed. It is one constant in WED
  (`AptDefs.h`).
