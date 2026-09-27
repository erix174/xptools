# WED Architecture & Code Reference

A developer-onboarding guide to the WorldEditor (WED) source tree. Audience: a C++ developer
who is new to this specific codebase. The goal is to give you a *map* — where things live,
how subsystems fit together, and what to read first when you need to fix or extend something.

For build instructions see `Building.md`. For the user-facing manual see `src/WEDDocs/`.

---

## 1. What WED Is

WED (WorldEditor) is the GUI scenery / airport editor for X-Plane. It edits:

- **Airports** — runways, taxiways, taxi signs, beacons, windsocks, ATC flows/frequencies.
- **Overlays** — facade (building) placements, forest placements, line/string placements,
  polygon placements, draped orthophotos, exclusion zones, autogen placements.
- **Documents** persisted as XML (modern) or SQLite (legacy) via WED's own archive layer,
  with full undo/redo.

WED reads/writes `apt.dat`, X-Plane DSF scenery, and talks to the X-Plane Scenery Gateway.

The xptools tree also builds *other* tools (RenderFarm, MeshTool, DSF2Text, ObjView, etc.).
Most of WED's source lives in `src/WED*` directories; everything else is shared infrastructure
or unrelated tools.

---

## 2. Repository Top Level

```
xptools/
├── Building.md            Build instructions (read this first to get compiling)
├── README.md              Project overview & licensing
├── CMakeLists.txt         Top-level CMake build
├── cmake/                 CMake helpers
├── conanfile.py           Conan dependency manifest
├── SDK/                   X-Plane SDK headers
├── scripts/               Packaging / release scripts
├── test/                  Regression test fixtures for WED
└── src/                   All source. WED-specific dirs are prefixed WED*.
```

Inside `src/` (only WED-relevant entries shown here; see `src/README.txt` for the full list):

| Directory          | Role                                                              |
|--------------------|-------------------------------------------------------------------|
| `WEDCore/`         | Document, archive, undo, validation, library/resource/texture mgrs |
| `WEDEntities/`     | Concrete entity classes (airport, runway, taxiway, placements…)   |
| `WEDMap/`          | 2D map view, layers, interactive tools                            |
| `WEDProperties/`   | Property table UI                                                  |
| `WEDWindows/`      | Top-level windows and dialogs                                      |
| `WEDTCE/`          | Texture coordinate editor (UV editor for orthophotos)             |
| `WEDLibrary/`      | Library / asset browser pane                                       |
| `WEDImportExport/` | apt.dat, DSF, gateway, scenery-pack import/export                  |
| `WEDLivery/`       | Static-aircraft liveries (2.8): the Static Liveries tab, auto-fill, the moderation model (its map overlays live in `WEDMap/`) |
| `WEDFileCache/`    | Disk cache for downloaded assets                                   |
| `WEDNetwork/`      | Gateway client / live-collab server                                |
| `WEDResources/`    | Icons, fonts, splash, line/pavement art                            |
| `WEDDocs/`         | Source for the user manual (markdown → HTML)                       |
| `Interfaces/`      | Abstract interfaces (`I*.h`) — the contracts everything talks to   |
| `GUI/`             | Cross-platform widget framework                                    |
| `Utils/`           | Geometry, math, file utilities                                     |
| `XESCore/`         | GIS engine — used by RenderFarm; little overlap with WED            |
| `DSF/`, `Obj/`     | DSF and OBJ format read/write libraries                             |

> **Naming gotcha** — the directory called `WorldEditor` (if you find references to it) and
> classes named `GISTool_*` are usually for *RenderFarm*, not WED. Old vocabulary.
> See `src/README.txt` for context.

---

## 3. Architectural Overview — The Five Big Ideas

If you understand these five patterns you can navigate the rest of WED:

### 3.1 Persistent object hierarchy: `WED_Thing`

Everything in a WED document is a `WED_Thing` — runways, taxiways, even the document
root and selection marker. `WED_Thing` provides:

- **Hierarchy** — parent + indexed children. The document is a tree of `WED_Thing`s.
- **Properties** — typed, named, introspectable, editable. Drives the property UI.
- **Sources / viewers** — observer pattern for change notification.
- **Persistence** — every property change is captured by the archive for undo/redo.

`WED_Entity : WED_Thing` adds spatial concerns: cached bounds, locked/hidden flags, and
implements the GIS interfaces.

### 3.2 The GIS abstraction: `IGISEntity`

The map view, selection, validation, geometric tools, etc. don't know about `WED_Runway`
or `WED_Taxiway` — they talk to `IGISEntity`, `IGISPoint`, `IGISPolygon`, etc.
`GISClass_t` (in `Interfaces/IGIS.h`) enumerates the kinds: point, point with heading,
line, ring, polygon, composite, …

This is *the* layer that decouples spatial algorithms from the airport-domain model.
Whenever you see code iterating `GetGISClass()` and switching, you're using it.

### 3.3 Properties + reflection: `IPropertyObject` / `WED_PropertyHelper`

Every editable object exposes its fields by name and type through `IPropertyObject`.
Concrete entities use `WED_PropertyHelper` and `WED_DEFINE_PROP_*` macros to declare
properties; the same machinery serves UI editing, multi-select merging, XML I/O, and
undo capture. **You don't write custom serialization for new entities — declare the
properties and the framework does the rest.**

### 3.4 Undo: archive + `WED_UndoMgr`

All mutations happen inside a command:

```cpp
doc->StartCommand("Move runway");
runway->SetLocation(...);          // captured automatically
doc->CommitCommand();               // or AbortCommand() to roll back
```

`WED_UndoMgr` records the diff via `WED_UndoLayer`. There is no manual "remember the old
value" code in normal entity logic — the archive snapshots property changes for you.
Forgetting to wrap a mutation in `StartCommand/Commit` will not crash, but the change
will not be undoable and may not persist.

### 3.5 The resolver pattern: paths instead of pointers

UI code that survives across undo/redo can't hold raw `WED_Thing*` pointers (they may
be deleted and re-created). Instead, panes ask the document (an `IResolver`) to
resolve a string path like `"world.airport[2].runway[0]"`. This is also how the
property pane and selection survive document edits.

---

## 4. The `Interfaces/` Directory — Read This First

Almost every cross-cutting boundary in WED runs through one of these abstract interfaces.
Knowing them is non-negotiable.

| Header               | Purpose                                                                            |
|----------------------|------------------------------------------------------------------------------------|
| `IBase.h`            | Root interface; ref-counting (`AddRef`/`Release`).                                  |
| `IGIS.h`             | `GISClass_t`, `IGISEntity`, `IGISPoint`, `IGISPoint_Bezier`, `IGISPoint_Heading`, `IGISQuad`. The spatial abstraction. |
| `IPropertyObject.h`  | `PropertyInfo_t`, `PropertyVal_t`, named/typed property access.                     |
| `IResolver.h`        | Resolve a path string to an `IBase*`. Implemented by `WED_Document`.               |
| `IArray.h`           | `Count`/`GetNth` — generic indexed iteration over children.                         |
| `IDirectory.h`       | Lookup-child-by-name.                                                               |
| `ISelection.h`       | Selection state: `IsSelected`, `Iterate`, etc.                                      |
| `IDocPrefs.h`        | Read/write doc-scoped or global prefs (int/double/string/int-set).                  |
| `ILibrarian.h`       | Library asset path/type/status lookup. Implemented via `WED_LibraryMgr`.            |
| `ITexMgr.h`          | OpenGL texture caching: `GetTexture`, `ReleaseTexture`.                             |
| `IHasResource.h`     | Marker — "this object references a library resource".                               |
| `IOperation.h`       | Nested undo: `__StartOperation` / `CommitOperation` / `AbortOperation`.             |
| `IControlHandles.h`  | Bezier control handle accessors for curved geometry.                                |

If you're touching the map, validation, or property pane, expect to be working through
`IGISEntity` and `IPropertyObject`, not concrete classes.

---

## 5. WEDCore — Document, Archive, Undo, Managers

Where the document model lives. Read `WED_Thing.h` and `WED_Document.h` first.

| Class / File           | What it does                                                            |
|------------------------|-------------------------------------------------------------------------|
| `WED_Thing`            | Root persistent object. Hierarchy, properties, observers, XML I/O.       |
| `WED_Entity`           | Spatial subclass; bounds caching, lock/hide, GIS interface plumbing.     |
| `WED_Persistent`       | Abstract `ReadFrom` / `WriteTo` / `ToXML` / `FromXML` contract.          |
| `WED_PropertyHelper`   | Mixin that implements `IPropertyObject` from declared properties.        |
| `WED_Document`         | The document. Owns archive, undo mgr, library mgr, resource mgr, prefs. Implements `IResolver`, `ILibrarian`, `IDocPrefs`. |
| `WED_Archive`          | Persistent storage layer. SQLite-backed; serializes things via `IOReader`/`IOWriter`. |
| `WED_UndoMgr`          | Undo/redo stack. `StartCommand` / `CommitCommand` / `AbortCommand`.      |
| `WED_UndoLayer`        | One undoable transaction's worth of property/hierarchy diffs.            |
| `WED_XMLReader/Writer` | Streaming XML I/O for documents.                                         |
| `WED_Application`      | App singleton; main window, doc lifecycle, file open/save.               |
| `WED_LibraryMgr`       | Resolves virtual library paths (objects, facades, forests, lines…) to disk and tracks asset status (public / deprecated / private). |
| `WED_ResourceMgr`      | Caches loaded `.obj` / `.fac` / `.for` / `.pol` definitions.             |
| `WED_TexMgr`           | Caches OpenGL textures (PNG/JPG via `BitmapUtils`).                      |
| `WED_Validate`         | Validation entry point; produces `WED_ValidateList` of issues.           |

**Where to start:** `WED_Thing.h` → `WED_Entity.h` → `WED_Document.h` → `WED_UndoMgr.h`.

---

## 6. WEDEntities — The Class Hierarchy

This is the domain model. Each class corresponds to something a user can place
on the map and that maps onto an apt.dat or DSF concept.

### 6.1 GIS shape bases (used as parents by concrete entities)

| Class                              | Shape                                                |
|------------------------------------|------------------------------------------------------|
| `WED_GISPoint`                     | Single position.                                     |
| `WED_GISPoint_Heading`             | Position + heading (towers, signs, windsocks).       |
| `WED_GISPoint_HeadingWidthLength`  | Position + heading + width + length (runway endpoints). |
| `WED_GISChain`                     | Sequenced polyline of child points.                  |
| `WED_GISLine_Width`                | Polyline with variable pavement width.               |
| `WED_GISEdge`                      | Edge between two endpoints (taxi routes, ATC nets).  |
| `WED_GISRing`                      | Closed ring (polygon boundary).                      |
| `WED_GISPolygon`                   | Polygon = outer ring + holes.                         |
| `WED_GISComposite`                 | Container of arbitrary `IGISEntity` children.        |

### 6.2 Airport furniture & geometry

| Class                       | Represents                                            |
|-----------------------------|-------------------------------------------------------|
| `WED_Airport`               | Top-level airport. ICAO, type, scenery ID, metadata kv pairs. Holds runways, taxiways, ATC, signs, etc. |
| `WED_Runway` + `WED_RunwayNode` | Runway (two endpoint nodes).                       |
| `WED_Taxiway`               | Pavement polygon with surface/markings/lighting.      |
| `WED_AirportChain`          | Linear features (markings, lights). Base for chained airport features. |
| `WED_AirportSign`           | Taxiway sign (position, heading, label string).       |
| `WED_AirportBeacon`         | Rotating beacon.                                      |
| `WED_Windsock`              | Windsock (lit flag).                                   |
| `WED_Helipad`               | Helipad.                                               |
| `WED_LightFixture`          | Light fixture (PAPI, VASI, etc.).                      |
| `WED_TowerViewpoint`        | ATC tower viewpoint.                                   |
| `WED_TruckDestination`, `WED_TruckParkingLocation` | Ground vehicle furniture.       |
| `WED_RampPosition`          | Aircraft parking spot.                                 |

### 6.3 ATC

| Class                                                                    | Role                          |
|--------------------------------------------------------------------------|-------------------------------|
| `WED_ATCFlow`                                                            | Named flow — composite of rules and runway uses. |
| `WED_ATCRunwayUse`                                                       | Per-runway use rule within a flow. |
| `WED_ATCWindRule`, `WED_ATCTimeRule`                                     | Wind / time predicates for flows. |
| `WED_ATCFrequency`                                                       | Frequency assignment.          |
| `WED_TaxiRouteNode`, `WED_TaxiRoute`                                     | Taxi route graph (nodes + edges). |

### 6.4 Overlay placements

| Class                       | Represents                                            |
|-----------------------------|-------------------------------------------------------|
| `WED_FacadePlacement`       | Building / facade. Resource path + ring of points.    |
| `WED_ForestPlacement`       | Forest area.                                          |
| `WED_StringPlacement`       | Linear string (fence, lights along a path).           |
| `WED_LinePlacement`         | Painted line.                                          |
| `WED_PolygonPlacement`      | Generic polygon overlay.                               |
| `WED_DrapedOrthophoto`      | Image overlay with explicit UVs (edited via TCE).      |
| `WED_AutogenPlacement`      | Autogen pack placement.                                |
| `WED_ObjPlacement`          | Single OBJ instance.                                   |
| `WED_ExclusionZone`         | Suppresses autogen / specific resource types in a box. |

### 6.5 Other

| Class                       | Role                                                  |
|-----------------------------|-------------------------------------------------------|
| `WED_Group`                 | User-created grouping container.                       |
| `WED_Select`                | Current selection. Lives in the document tree like everything else; implements `ISelection`. |
| `WED_Root`                  | Document root container.                               |

**Where to start:** open `WED_Airport.h` and trace down to `WED_Runway.h` to see how a
domain class is composed from GIS bases plus declared properties.

---

## 7. WEDMap — 2D Map View, Layers, Tools

Two parallel hierarchies: **layers** render, **tools** edit. Both derive from `WED_MapLayer`
(tools are layers that also handle input).

### 7.1 Core dispatch

| Class                | Role                                                                |
|----------------------|---------------------------------------------------------------------|
| `WED_Map`            | The map pane. Owns layers + active tool, dispatches draw and input. |
| `WED_MapPane`        | Wraps `WED_Map` with toolbar, tool buttons, preview pane.            |
| `WED_MapZoomerNew`   | Zoom/pan state and screen↔world coordinate transforms; moderation's view rotation (§12c). |
| `WED_MapBkgnd`       | Background layer (orthophoto / elevation tiles).                     |

### 7.2 Layers (rendering)

`WED_MapLayer::GetCaps` advertises whether a layer draws structure (handles, outlines),
visualization (filled shapes, icons), or selection. `WED_Map` walks the entity tree
and calls `DrawEntityVisualization` / `DrawEntityStructure` per layer per entity.

| Class                | Renders                                |
|----------------------|----------------------------------------|
| `WED_StructureLayer` | Vertices, edges, control handles.       |
| `WED_PreviewLayer`   | Photoreal preview of placements.        |
| `WED_ATCLayer`       | ATC taxi routes / flow visualization.   |
| `WED_BoundaryLayer`  | Airport boundaries.                     |
| `WED_DebugLayer`     | Bounding boxes, etc.                    |
| `WED_ModerationLayer`| Moderator overlays, screen space (§12c). |

### 7.3 Tools (input)

| Class                       | What it does                                  |
|-----------------------------|-----------------------------------------------|
| `WED_MapToolNew`            | Abstract base. Click/drag/key handlers, status text, undo. |
| `WED_HandleToolBase`        | Base for tools that drag existing vertices/handles.        |
| `WED_CreatePointTool`       | Single-click point creation.                  |
| `WED_CreateLineTool`        | Multi-click polyline.                          |
| `WED_CreatePolygonTool`     | Closed polygon.                                |
| `WED_CreateBoxTool`         | Click-drag rectangle / quad.                   |
| `WED_CreateEdgeTool`        | Edge in a graph (taxi network).                |
| `WED_VertexTool`            | Drag vertices / Bezier handles.                |
| `WED_MarqueeTool`           | Rectangle multi-select.                        |

### 7.4 Drawing utilities

`WED_DrawUtils.h` (icons, lines, text) and `WED_Colors.h` (palette).

**Where to start:** `WED_Map.h` → `WED_MapLayer.h` → `WED_MapToolNew.h`. To learn the
*rendering* pipeline, read `WED_StructureLayer`. To learn *editing*, read
`WED_VertexTool` and `WED_HandleToolBase`.

---

## 8. WEDProperties — Property Table UI

| Class                | Role                                                                 |
|----------------------|----------------------------------------------------------------------|
| `WED_PropertyPane`   | Pane wrapping table + filter; modes: hierarchical / filtered / by-selection. |
| `WED_PropertyTable`  | Adapter from `IPropertyObject(s)` to a generic `GUI_TextTable`. Multi-select merge logic lives here. |

The pane reads selection from the document and walks `IPropertyObject` to populate cells.
Edits are written back through the same interface, capturing undo automatically.

---

## 9. WEDWindows — Top-Level Windows & Dialogs

| Class                  | Role                                                              |
|------------------------|-------------------------------------------------------------------|
| `WED_DocumentWindow`   | Main editing window. Splits map / properties / library / TCE.     |
| `WED_StartWindow`      | Launcher: recent files, new/open buttons.                          |
| `WED_AboutBox`         | About dialog.                                                      |
| `WED_Sign_Editor`      | Edits taxi sign text + style.                                      |
| `WED_Line_Selector`    | Picks a taxi/road line type.                                       |
| `WED_Road_Selector`    | Picks a road type (when road editing is on).                       |
| `WED_Menus`            | Menu / command definitions and dispatch.                           |

**Where to start:** `WED_DocumentWindow.h` is the layout assembly point — reading it
shows how the panes wire together.

---

## 10. WEDTCE — Texture Coordinate Editor

A second 2D canvas, mirroring `WED_Map`'s design, but used for editing UVs on
draped orthophotos.

| Class                | Role                                                  |
|----------------------|-------------------------------------------------------|
| `WED_TCEPane`        | Pane container; analogue of `WED_MapPane`.            |
| `WED_TCE`            | Canvas; analogue of `WED_Map`.                        |
| `WED_TCELayer`       | Layer base.                                            |
| `WED_TCEToolNew`     | Tool base.                                             |
| `WED_TCEVertexTool`  | Drag corner UVs.                                       |
| `WED_TCEMarqueeTool` | Marquee select UVs.                                    |
| `WED_TCEToolAdapter` | Bridges TCE selection to the property pane.            |

If you understood `WEDMap`, you've understood `WEDTCE`.

---

## 11. WEDImportExport — apt.dat, DSF, Gateway

This is where WED talks to the outside world.

| File                       | Role                                                    |
|----------------------------|---------------------------------------------------------|
| `WED_AptIE.{h,cpp}`        | apt.dat import/export. `WED_AptImport`, `WED_AptExport`, plus UI entry points. |
| `WED_DSFImport.{h,cpp}`    | Reads a DSF and creates placement objects.               |
| `WED_DSFExport.{h,cpp}`    | Writes WED objects out as DSF.                           |
| `WED_GatewayImport.{h,cpp}`| Downloads scenery from the X-Plane Gateway.              |
| `WED_GatewayExport.{h,cpp}`| Uploads / submits scenery; conflict detection.            |
| `WED_SceneryImport.{h,cpp}`| Generic X-Plane scenery package import.                  |
| `WED_SceneryPackExport.{h,cpp}` | Standard scenery pack export.                       |
| `WED_OrthoExport.{h,cpp}`  | Orthophoto-specific export.                              |
| `WED_AptTable`             | Airport-list table model used by the import dialog.       |
| `WED_MetaDataKeys`, `WED_MetaDataDefaults` | Airport metadata field definitions and defaults. |
| `WED_ICAOTable`            | ICAO airport code lookup.                                |

Apt.dat parsing relies on the `AptDefs.h` structures (in `Utils/`); WED translates
between those and the `WED_Airport` tree.

---

## 12. WEDLibrary, WEDFileCache, WEDNetwork

### WEDLibrary

| Class                       | Role                                                |
|-----------------------------|-----------------------------------------------------|
| `WED_LibraryPane`           | Library browser pane embedded in the document window.|
| `WED_LibraryListAdapter`    | Tree/list table adapter over `WED_LibraryMgr`.       |
| `WED_LibraryPreviewPane`    | Thumbnail / 3D preview.                              |
| `WED_LibraryFilterBar`      | Search input.                                         |

The actual asset discovery / path resolution lives in `WED_LibraryMgr` (in WEDCore).

### WEDFileCache

| Class                  | Role                                                   |
|------------------------|--------------------------------------------------------|
| `WED_FileCache`        | Public API. `request_file()` returns a status/path response. |
| `CACHE_CacheObject`    | Per-file cache entry (download state, error cool-down). |
| `CACHE_DomainPolicy`   | Per-domain age, cool-down, and bandwidth rules.         |

### WEDNetwork

| Class                  | Role                                                   |
|------------------------|--------------------------------------------------------|
| `WED_Server`           | TCP server lifecycle + send/receive.                    |
| `WED_Connection`       | Per-client connection state.                            |
| `WED_NWLinkAdapter`    | Bridges document changes to network sync.               |
| `WED_NWInfoLayer`      | Map layer for showing network status / conflicts.       |
| `WED_NWDefs`           | Protocol message definitions.                           |

---

## 12b. WEDLivery — Static Aircraft Liveries (2.8)

Which airlines' aircraft park at a ramp start, and at which sizes. The file format
(row `1313`, six per-class spawn weights) and every rule are in
`WED_LiveryFormatSpec.md`; this is the code map. Nothing here is on the export
path except through `WED_RampPosition` (`class_weights`, `auto_filled`) and
`AptIO.cpp` (row `1313`).

| File | Role |
|------|------|
| `WED_LiveryIndex` | Reads the install's `apt_aircraft/livery_index.txt` (schema 4): one row per livery, `Obsolete` rows held but not indexed, hubs placed from Global Airports on a worker thread. |
| `WED_AirlineDirectory` | The OPERATOR records of the same file: name, country, operation class, fleet. |
| `WED_AirportDatabase` | Per airport: country and the airlines that serve it (`WED_AirportDatabase.txt` beside WED). Feeds the Recommended tier. |
| `WED_LiveryRules` | **The one allow rule** (`WED_LiveryAllowedAt`: op class, range R26, HOME R27), equipment by asset folder, the legacy letter→weights table, and the shared index (`WED_GetLiveryData`). The tab, auto-fill and the validator all call it. |
| `WED_LiveryAutoFill` | Airport > Auto-Populate and the tab's Populate button. Extends, never overwrites; one undo step. |
| `WED_LiveryModeration` | The moderation model: one stand's entry and verdicts, the parks-nothing check the validator also uses, stepping, the report. See §12c. |
| `WEDMap/WED_ModerationLayer`, `…Toolbar` | The moderator's map overlays and tool buttons. See §12c. |
| `WED_LiveryThumbnailCache` | Renders a livery `.obj` to a card image on worker threads; LRU. |
| `WED_LiveryPane*` | The Static Liveries tab, split by concern: `WED_LiveryPane.cpp` state, selection, cards and the coverage readout; `…Layout` rectangles and hit tests; `…Input` mouse and the edits it makes; `…Draw` drawing and animation; `…Rows` the airline list tiers. `WED_LiveryPaneInternal.h` is private to them. |
| `WED_Flag*`, `WED_IocCountryCodes` | The country flag banner, and apt.dat country → IOC code. |
| `WED_MandatoryHeader` | The two-line stamp every hand-maintained data file in this family must start with. |

**Validation** lives in `WED_Validate.cpp`. `ValidateRampLiveries` is a thin
wrapper: the check itself is `WED_LiveryParksNothing` (in `WED_LiveryModeration`),
which raises `warn_ramp_livery_parks_nothing` when nothing in the index can park at
the stand - its listed operators for an airline/cargo stand, the whole library for
a GA stand or an unlisted military one. A warning, never an export block. Rows the
import could not read come from `WED_Document::DescribeDiscardedRowsFor`
(`warn_apt_dat_rows_not_imported`).

**Data tools** are in `tools/scripts/airline_research/`: `gen_livery_index.py`
(merges new assets into the hand-maintained index), `merge_airport_database.py`
(builds `WED_AirportDatabase.txt`), `livery_obsolete_radius.py` (what an
`Obsolete` mark would empty), `livery_sample_expect.py` (expected results of
`docs/livery_sample/`), `measure_empty_stands.py` (spec §4.5 against Global
Airports), `check_wed_export.py`, `check_1313_roundtrip.py`.

---

## 12c. Moderation Mode (2.8)

Tools for a Gateway moderator checking a submitted airport's stands without
clicking through each one. **One switch:** `WED_ModerationEnabled()` returns
`gModeratorMode`, the Preferences checkbox, persisted as `ModeratorMode` in
`WED_Document::Read/WriteGlobalPrefs`. Every caller asks it at draw, click or
command time - never caches it - so ticking or clearing the box applies at once
in open documents, no restart. (Older moderator code in `WED_PropertyTable` and
`WED_GatewayImport` reads `gModeratorMode` directly; same value.)

| File | Role |
|------|------|
| `WEDLivery/WED_LiveryModeration` | The model. No drawing. |
| `WEDMap/WED_ModerationLayer` | Everything drawn over the map: tint, callouts, Moderation View overview. |
| `WEDMap/WED_ModerationToolbar` | Two toggles in the foot of the map tool column; art `WEDResources/moderation_tools.png`. |
| `WEDMap/WED_Map`, `WED_MapZoomerNew` | View rotation (moderation only). |
| `WEDProperties/WED_PropertyTable` | Moderator hierarchy shortcuts. |

### The model — `WED_LiveryModeration.{h,cpp}`

| Function | Does |
|----------|------|
| `WED_ModerationDescribe(ramp, apt, entry)` | Everything a moderator reads off one stand, as a `WED_ModerationEntry`: op type/label, equipment, weights or size letter, the auto-fill watermark, and a `WED_ModerationCode` verdict per listed operator. How it verifies depends on op type and origin: airline/cargo auto-filled → `v_Assumed`; by hand → each code against `WED_AirportDatabase`'s served list (`v_Ok` / `v_Check` + search URL; listing *fewer* is fine); no database row → `verify_NoData`; military → operator country vs airport country (`v_Foreign`); GA/None → nothing. Also fills `parks_nothing` from `WED_LiveryParksNothing`. |
| `WED_ModerationSignature(ramp)` | "The same setup": op type, sorted unique airline set, weights text (or size letter), equipment set. Two stands with equal signatures park the same thing; colour, grouping and "reviewed" all key on it. |
| `WED_ModerationColour(sig, rgba)` | Signature → colour. Session-stable slots, golden-ratio hue steps from a random seed, shared by every layer. |
| `WED_ModerationHasIssue(entry)` | Needs a look: an operator to check (`v_Check`/`v_Foreign`), `verify_NoData`, or `parks_nothing`. Drives stepping, the Moderation View highlight and the report. |
| `WED_LiveryParksNothing(ramp, apt, msg)` | **The** shared check behind the validator's `warn_ramp_livery_parks_nothing`, the overview's "Parks nothing" filter and the report. Same data and rule as the Liveries tab (`WED_LiveryAllowedAt`, `WED_LiveryEquipment`). One pipeline, so the three never disagree; it reads only the stand, the airport and shipped data, so any WED of the same version reproduces it. |
| `WED_ModerationRamps` / `WED_ModerationStep(res, dir, issues_only)` | All ramp starts of the current airport in hierarchy order; select the next/previous one (wrapping), optionally only those with an issue. |
| `WED_ModerationNotes` / `WED_ModerationPrompt` | The older per-stand notes list and the "operators to check - search the web?" dialog (capped at five searches), shown on Ctrl+Shift+. / , in moderator mode. |
| `WED_ModerationReport(apt, reviewed)` | Plain-text summary for the clipboard: counts, WED + index version, validator warnings verbatim, then stands to check grouped by setup. |
| `WED_ModerationSearchURL` / `WED_ModerationOpenSearch` | "Does <operator> fly to <ICAO> Now" as a Google URL; opened in a small chromeless Edge/Chrome `--app` window beside the cursor (Windows: placed afterwards with `SetWindowPos` from a worker thread), else the default browser. Call only after the click is over. |

**Commands** (`WED_Menus`, handled in `WED_DocumentWindow::HandleCommand`):
`wed_NextRampStart`/`wed_PrevRampStart` (Ctrl+Shift+. / ,, any mode) and
`wed_NextIssueStand`/`wed_PrevIssueStand` (Shift+X via `WED_MapPane::Map_KeyPress`
so it does not steal capital X from text fields; Ctrl+Shift+X; moderator only).
Both centre the stand with `WED_MapPane::CenterOnPoint` (zoom kept) and show the
Static Liveries tab.

### The map overlays — `WED_ModerationLayer.{h,cpp}`

A `WED_MapLayer` that draws nothing per entity (`GetCaps`: no vis/structure, wants
clicks); all its work is in `DrawSelected` → `DrawOverlays`, which undoes the view
rotation so every overlay is in **screen space**. Anchors are placed with
`MapPixelToScreen(LLToPixel(ll))`; incoming click/wheel points arrive in map pixels
and are turned back the same way.

- **Tint.** `WED_ModerationTintFor(ramp)` is called by `WED_StructureLayer` and
  `WED_ATCLayer` for the ramp silhouette: the signature's colour, grey for op
  type None, and in Moderation View grey/dimmed for stands without an issue.
- **Callouts** for the selected ramp starts (`Collect` → `Group`: on-screen stands
  with one signature share one callout; the tier counts those, not stands):
  **cards** for ≤ 5 (`kMaxCards`), **chips** in a column on whichever side covers
  fewer stands for ≤ 40 (`kMaxChips`; hovering opens the card), else a **legend**
  of distinct setups (≤ 24 rows; hover rings the stands, click selects them).
  Leaders from each member stand meet at one **hub**, and a single **neck**
  (`NeckLen`/`NeckWidth`, heavier for more stands) runs to the card or chip.
  A card's tray lists operators with flags and verdicts; "?" opens a search.
- **Pin and compare.** The pin (or Shift+click on a chip) makes a stand the base;
  `Diff` gives the others +/-/~ lines or counts. The base stays shown while other
  stands are selected.
- **Moderation View** (`WED_ModerationViewOn`, the toolbar's first button):
  `DrawOverview` puts an airport panel top-left - counts, reviewed progress,
  filter chips (All / Not listed / No data / Foreign / Parks nothing, as a bit
  mask), sort (name / most to verify), a scrolling list of stands to check (click
  = `Focus`: select and `CenterOn`), and **Copy Summary to Clipboard**
  (`WED_ModerationReport`). "Reviewed" is **by setup**, this session only: a setup
  counts once one of its stands has been shown alone or focused. It also refreshes
  `sIssueIDs`, which the tint reads next frame, and badges each stand to check.
- **Hit testing.** Each frame records `Hit` rectangles; hover state is read from
  last frame's hits. `HandleClickDown` tests in three passes (search, then
  controls, then any card/chip/panel body so the tool beneath does not drop the
  selection). A search is deferred to `HandleClickUp`: a browser that opens on
  mouse-down steals the up.

### The toolbar — `WED_ModerationToolbar.{h,cpp}`

A plain `GUI_Pane` (independent toggles, not a `GUI_ToolBar`), placed by
`WED_MapPane` at the bottom of the tool column. Draws, clicks and tips only while
`WED_ModerationEnabled()`. Tool 0 toggles Moderation View; tool 1 calls
`WED_MapPane::ToggleViewRotate`. `moderation_tools.png` is laid out like
`map_tools.png`: normal half, then selected half, one cell per tool.

### View rotation — `WED_MapZoomerNew` + `WED_Map`

- **Zoomer.** `mViewRotation` (degrees CCW about the centre of the pixel bounds).
  Everything else in the zoomer stays in unrotated **map pixels**, so tools, handles
  and hit tests are untouched. `ScreenToMapPixel` / `MapPixelToScreen` convert;
  `GetMapVisibleBounds` samples the rotated screen edge; `CenterOn(ll)` recentres
  at the exact zoom. `SetViewRotation` bumps the cache key.
- **Map.** `WED_Map::Draw` wraps the layer passes in one `glRotated`; mouse points
  go through `ToMap` on arrival (`GetMouseLocNow` too). Rotate mode (`SetRotateMode`):
  a left-drag draws a reference arrow and turns the view to level it; once there is
  a reference, a left-drag is a screen-level marquee whose corners go through
  `ScreenToMapPixel` to `WED_HandleToolBase::SelectInQuad` (Alt+drag draws a new
  reference); Shift+right-drag turns freely, with 5° detents at the reference and
  every 90° from it. A plain click is held back and replayed to the select tool.
- **Guard rails.** Only with the select tool (`SetSelectTool`, the Vertex tool);
  `SetTool` to anything else, or `Draw` finding moderator mode cleared, puts it
  north up. While rotated, drags are not passed to the tool and its mouse-up is
  replayed at the down point, so nothing is moved in a rotated frame. Per window;
  documents open north up.
- **Layers that must stay level** undo the turn themselves: `WED_ModerationLayer`
  and `WED_SlippyMap`'s status line and attribution. The slippy tiles rotate with
  the map, and their coverage follows `GetMapVisibleBounds`.

### Hierarchy shortcuts and other glue

- `WED_PropertyTable` (moderator mode, hierarchy view only): single click on an
  airport → `ModeratorShowAirport` (open and unhide all its folders, ESRI imagery,
  zoom, Selection tab); double click on Taxiways / Draped Polygons / Ground Vehicles
  → `ModeratorFocusFolder` (hide sibling folders, imagery off, pick the tab). Both
  change Hidden flags inside an undoable command. The single click on an
  already-selected name goes through the new `GUI_TextTable` content hook
  `ClickSelectedCell`, which takes the click before the cell opens for renaming.
- `WED_GatewayImport`: in moderator mode each imported airport is collapsed too,
  so a multi-airport import lists one line per airport.
- `WED_DocumentWindow`: `wed_MapSelection` / `Pavement` / `ATC` / `3D` now switch the
  property tab strip as well as the map filter, so the shortcuts above land on the
  matching tab.

### How to: add a moderation check that shows in the validator, the overview and the report

1. If Validate should list it, write it as `bool WED_LiveryXxx(ramp, apt, string& msg)`
   beside `WED_LiveryParksNothing`, reading only the stand, the airport and shipped
   data. Add a `warn_*` code in `WED_Validate.h` and call it from
   `ValidateRampLiveries`.
2. Add the result to `WED_ModerationEntry` and fill it in `WED_ModerationDescribe`.
3. Include it in `WED_ModerationHasIssue`. Shift+X stepping, the Moderation View
   highlight, badges and the report's grouping follow from that.
4. Overview: give it a bit in `DrawOverview`'s mask, a name in the filter chips
   (grow `n_kind`), and text on its row.
5. Report: add the message to "Validator warnings" and a clause to the per-setup
   line in `WED_ModerationReport`. Use the same message string everywhere.

### How to: add something only moderators see

- Gate it on `WED_ModerationEnabled()` where it is used (`Draw`, `MouseDown`,
  `CanHandleCommand`), not when it is built, so the preference applies live.
- Map drawing goes in `WED_ModerationLayer::DrawOverlays` (screen space, already
  gated). Record a `Hit` for anything clickable.
- A command: an enum in `WED_Menus.h`, a row in `WED_Menus.cpp`, a case in
  `WED_DocumentWindow::HandleCommand`, and the gate in `CanHandleCommand`. Don't
  bind a bare letter as a menu accelerator: it takes that key from every text field
  (see Shift+X in `Map_KeyPress`).
- A toolbar button: raise `kTools`, add a tip, widen both halves of
  `moderation_tools.png`, and handle it in `MouseDown`/`IsOn`. Note `CellSize`
  divides the art by `Columns()`, so a third tool also needs that changed.

---

## 13. WEDResources

Static art and metadata: airport icons, parking-spot icons, line-marking textures,
pavement textures, vertex handle graphics, scrollbar/splitter/tab assets, the WED
icon, the Mac menu nib, fonts, the splash worldmap. Loaded via `GUI_Resources.h`.

---

## 14. The GUI Framework — Just Enough to Get Going

WED's UI is its own portable widget toolkit. All panes derive from `GUI_Pane`. Read
the comments at the top of `GUI_Pane.h` for the full theory of operation.

| Class                  | Role                                                   |
|------------------------|--------------------------------------------------------|
| `GUI_Pane`             | Base widget. Drawing, mouse/keyboard, sticky-edge layout. |
| `GUI_Window`           | Top-level window.                                       |
| `GUI_Application`      | App singleton, event loop, modal dialogs.               |
| `GUI_Broadcaster` / `GUI_Listener` | Pub/sub. Widgets `BroadcastMessage(msg, param)`; listeners override `ReceiveMessage`. |
| `GUI_Commander`        | Menu / keyboard command routing along the pane chain.    |
| `GUI_Control`          | Abstract value-bearing control (slider-style).           |
| `GUI_Button`, `GUI_TextField`, `GUI_Label`, `GUI_ScrollBar`, `GUI_PopupButton`, `GUI_TabControl`, `GUI_FilterBar` | Standard controls. |
| `GUI_Table`, `GUI_TextTable`, `GUI_Header` | Generic grid / two-column tables. |
| `GUI_ScrollerPane`, `GUI_Splitter`, `GUI_Packer` | Layout containers.    |
| `GUI_Destroyable`      | Mixin: defer `delete` until safe (e.g. event handler exit). |
| `GUI_DrawUtils`, `GUI_GraphState`, `GUI_Fonts` | Drawing primitives.  |

The two messaging patterns — **Broadcaster/Listener** (data changes) and **Commander**
(menu/keys) — are everywhere. Understanding them is the price of admission.

---

## 15. How-To Recipes

### 15.1 Add a new entity type

1. Create `WED_NewThing.{h,cpp}` in `WEDEntities/`, deriving from the appropriate GIS
   base (`WED_GISPoint`, `WED_GISPolygon`, …).
2. Declare persistent properties using `WED_PROPERTY_*` macros (see existing entities
   like `WED_AirportSign` for the pattern). The framework handles XML I/O, undo, and
   the property pane automatically.
3. Override `GetGISClass()` if needed.
4. Register the class in `WED_Entity.cpp`'s factory table so the archive can construct it.
5. Add import/export handling — usually a new clause in `WED_AptIE.cpp` or
   `WED_DSFImport/Export.cpp`.
6. Optional: add icon assets in `WEDResources/` and a render branch in the appropriate
   map layer if it needs custom drawing.

### 15.2 Add a map tool

1. Create `WED_MyTool.{h,cpp}` in `WEDMap/`, deriving from `WED_MapToolNew` (or
   `WED_HandleToolBase` if it manipulates existing handles).
2. Implement `HandleClickDown/Drag/Up`, `HandleToolKeyPress`, `DrawVisualization`,
   and the `GetStatusText` / `GetCaps` overrides.
3. Wrap mutations in `StartCommand` / `CommitCommand` (or `__StartOperation` for
   nested operations).
4. Register the tool in `WED_MapPane` or wherever the tool palette is built.

### 15.3 Add a property to an existing entity

1. Add a `WED_PROPERTY_*` declaration in the entity's header.
2. Initialize it in the constructor.
3. The property table, undo, and XML I/O pick it up automatically.

### 15.4 Add a validation check

Open `WED_Validate.cpp` and follow the existing pattern of walking the document and
emitting `WED_ValidateError` records into the issue list.

### 15.5 Trace a bug starting from a UI symptom

- **Map misbehavior:** start in the active layer's `DrawEntity*` or the active tool's
  click handlers.
- **Property pane wrong:** `WED_PropertyTable` — confirm what `IPropertyObject` is
  reporting for the selected object(s).
- **Save / load wrong:** `WED_XMLReader/Writer` for XML; `WED_Archive` for the
  legacy SQLite path.
- **Apt.dat wrong:** `WED_AptIE.cpp`.
- **Undo wrong:** confirm the mutation is wrapped in `StartCommand`/`CommitCommand`
  *and* goes through the property system rather than back-door pointer mutations.

---

## 16. Reading Order Recommendation

If you have an afternoon to spend reading code before touching anything, this is the
order with the highest payoff:

1. `src/README.txt` (legacy — context, naming history)
2. `Building.md` (get a build going)
3. `Interfaces/IGIS.h`, `Interfaces/IPropertyObject.h`, `Interfaces/IResolver.h`
4. `WEDCore/WED_Thing.h`, `WED_Entity.h`, `WED_Document.h`, `WED_UndoMgr.h`
5. `WEDEntities/WED_Airport.h`, `WED_Runway.h` — concrete examples of the patterns
6. `WEDMap/WED_Map.h`, `WED_MapLayer.h`, `WED_MapToolNew.h`
7. `WEDProperties/WED_PropertyPane.h`
8. `WEDWindows/WED_DocumentWindow.h` — see how it all snaps together
9. `WEDImportExport/WED_AptIE.h` — the most-touched I/O surface

Once those are familiar, the rest of the tree reads itself.

---

## 17. Conventions & Gotchas

- **Coordinate systems:** WED uses lat/lon in degrees for storage. The map converts to
  screen pixels via `WED_MapZoomerNew`. Beware of code that assumes meters.
  When the view is rotated (§12c), zoomer "pixels" are *map* pixels, not screen
  pixels: anything that must sit level on screen converts with `MapPixelToScreen`.
- **Bounds caching:** `WED_Entity` caches its bounding box. If you mutate geometry
  outside the property system, you may need to call the appropriate dirty-flag method.
- **Pointers vs. paths:** Don't cache `WED_Thing*` across operations that might delete
  and re-create the object (undo, reload). Use the resolver path.
- **Undo wrapping:** Every user-visible mutation must live inside a command block.
  Forgetting this produces silent loss-of-undo, not a crash.
- **GUI threading:** the GUI framework is single-threaded. File downloads (cache,
  gateway) marshal back to the main thread for UI updates.
- **Two persistence formats:** modern documents are XML; older code paths still touch
  SQLite via `WED_Archive`. Don't rip out the SQLite path without checking what
  still uses it.

---

## 18. Where This Doc Stops

- The `XESCore` / RenderFarm / DSF GIS engine is barely covered — WED uses very little
  of it directly.
- The `GUI` framework section is intentionally shallow; if you're doing serious widget
  work, read `GUI_Pane.h` end-to-end.
- Format-level details of apt.dat, DSF, OBJ are out of scope — see `Utils/AptDefs.h`,
  `DSF/`, and `Obj/` respectively.

When something here goes stale, fix it — this file is meant to be edited as you learn.
