/*
 * Copyright (c) 2026, Laminar Research.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 *
 */

/*
	WED_LiveryPane - THEORY OF OPERATION

	Self-contained "Liveries" tab for the property-side GUI_TabPane. Hosts the
	three pillars of ramp-start livery selection, migrated off the generic
	"Selection" property grid (see WED_RampPosition.cpp - properties prefixed
	with "." are hidden from that grid by an existing WED convention):

	1. Ramp Operation Type - a row of filter chips (None/GA/Airline/Cargo/
	   Military) at the top. Same property as before; its value now ALSO
	   filters the airline list below.
	2. Size - a dual-handle range slider over the six ICAO width categories
	   (A-F), backed by the existing "width" property (now the max) plus a
	   new "width_min" property. Not yet exported to apt.dat - see
	   WED_RampPosition.cpp/.h for why.
	3. Airlines - the checklist picker (unchanged from the prior pass),
	   filtered by the Ramp Operation Type chip.

	Selection tracking, tab enable/disable, and the input-blocking mask are
	unchanged from the prior pass.

	The airline dataset in WED_LiveryData.h is a placeholder - see that file.
*/

#ifndef WED_LIVERYPANE_H
#define WED_LIVERYPANE_H

#include "GUI_Pane.h"
#include "GUI_Listener.h"
#include "GUI_Commander.h"
#include "WED_AirportDatabase.h"
#include "WED_AirlineDirectory.h"
#include "WED_LiveryIndex.h"
#include "WED_LiveryThumbnailCache.h"
#include <vector>
#include <string>
#include <map>
#include <set>

class	IResolver;
class	WED_Archive;
class	WED_RampPosition;
class	GUI_TabPane;
class	GUI_Broadcaster;
class	GUI_GraphState;
class	GUI_TextField;

// GUI_Commander is not decoration. Every pane handed to GUI_TabPane::AddPane()
// ends up as a child of GUI_ChangeView, and GUI_ChangeView::SetSubView()
// dynamic_casts it to GUI_Commander so it can hand the tab keyboard focus
// (GUI_ChangeView.cpp:47). Without that base this pane fired
// DebugAssert(c != NULL) on every switch INTO the Liveries tab, and - past the
// assert - mSearchField could never join the focus chain, because the chain has
// no route into a pane that is not a commander. WED_PropertyPane and WED_TCEPane
// are both commanders for exactly this reason; this pane was the odd one out.
class	WED_LiveryPane : public GUI_Pane, public GUI_Commander, public GUI_Listener {
public:

						 WED_LiveryPane(
								IResolver *		resolver,
								WED_Archive *	archive,
								GUI_TabPane *	host_tabs);
	virtual				~WED_LiveryPane();

	virtual	void		Draw(GUI_GraphState * state);
	virtual	int			MouseMove(int x, int y);
	virtual	int			MouseDown(int x, int y, int button);
	virtual	void		MouseDrag(int x, int y, int button);
	virtual	void		MouseUp  (int x, int y, int button);
	virtual	int			ScrollWheel(int x, int y, int dist, int axis);

	// Discards every cached card thumbnail/flag texture - called both when this
	// pane's tab is switched away from (GUI_TabPane calls Hide() on the outgoing
	// tab) and from the destructor. See WED_LiveryThumbnailCache's own header for
	// why this eviction is the cache's caller's job, not something it does itself.
	virtual	void		Hide(void);

	virtual	void		ReceiveMessage(
								GUI_Broadcaster *	inSrc,
								intptr_t			inMsg,
								intptr_t			inParam);

	// Re-reads the current selection and syncs the tab's enabled state. Public so
	// WED_DocumentWindow can call it right after AddPane(), once this pane is
	// actually findable in the tab strip - see that call site for why.
	void				RebuildSelection(void);

private:

	// ---- layout ----
	// Single source of truth for section Y-ranges, so hit-testing and drawing
	// can never drift apart. Each section is separated from the next by
	// GapHeight() of empty space.
	float				AirportInfoHeight(int bounds[4]) const;
	float				HeaderHeight(void) const;
	float				FilterRowHeight(void) const;
	float				SliderHeight(void) const;
	// 0 when the selection has no weights at all, which collapses the whole
	// section away - see WeightsYRange(). A stand only gets weights by being
	// asked for them, so an author who never presses the button never gets a
	// 1313 row on anything (R17).
	float				WeightsHeight(void) const;
	float				CoverageHeight(void) const;
	float				ListToolbarHeight(void) const;
	float				GapHeight(void) const;
	void				AirportInfoYRange(int bounds[4], float & top, float & bot) const;
	void				HeaderYRange(int bounds[4], float & top, float & bot) const;
	void				FilterYRange(int bounds[4], float & top, float & bot) const;
	void				SliderYRange(int bounds[4], float & top, float & bot) const;
	void				WeightsYRange(int bounds[4], float & top, float & bot) const;
	void				CoverageYRange(int bounds[4], float & top, float & bot) const;

	// ---- spawn weight bars (apt.dat row 1313) ----
	// Six draggable bars, one per ICAO wingspan class, sharing the size
	// slider's horizontal track so each bar sits directly under its own A-F
	// tick label. Hand-drawn, like everything else in this pane: WED has no
	// multi-value numeric control anywhere to reuse.
	bool				SelectionWeights(int out_w[6]) const;	// false if none, or if the selection disagrees
	bool				SelectionHasWeights(void) const;
	int					WeightBarForXY(int bounds[4], int x, int y) const;	// -1 if not on one
	int					WeightValueForY(int bounds[4], int y) const;		// snapped to an integer, clamped
	int					WeightTrackMax(void) const;			// 10, or higher if an imported file needs it
	void				WeightBarRect(int bounds[4], int idx, float r_out[4]) const;
	void				ApplyWeightDrag(void);				// writes mDragWeights to every selected ramp
	void				AbortWeightDrag(void);				// rolls back an unfinished drag; no-op if none

	// The two ways in and out of having weights at all. Seeding is the ONLY
	// path that creates a 1313 row, and clearing returns the stand to "no row",
	// which is NOT the same as six zeros - that is a legal way to say nothing
	// parks here (spec §4.2), and the UI has to offer both.
	// The button toggles a MODE, it does not delete data. Going to simple mode
	// stashes the stand's weights in mWeightCache first, so coming back restores
	// what the author had rather than re-seeding from the size range and losing
	// their distribution. Keyed by the persistent ID, not by pointer: an undo
	// can destroy and rebuild the object, and a pointer key would then either
	// miss or, worse, hit a recycled address.
	//
	// Session-only on purpose. It is a convenience for toggling back and forth
	// while editing, not a second place where weights live - the entity property
	// remains the single source of truth, and nothing here reaches apt.dat.
	void				SeedWeightsFromSizeRange(void);
	void				SwitchToSimpleMode(void);
	void				WeightButtonRect(int bounds[4], float b_out[4]) const;
	std::map<int, std::string>	mWeightCache;
	void				ListToolbarYRange(int bounds[4], float & top, float & bot) const;
	float				ContentTop(int bounds[4]) const;		// top Y of the airline checklist

	// ---- coverage readout (WED_LiveryFormatSpec.md §4.5) ----
	// The pre-`1313` form of the P(empty) readout. The spec's weighted version
	// needs class weights, which no entity carries yet (roadmap phase 4) - but the
	// defect those weights would expose is already measurable from data WED has
	// today: a stand whose listed operators have no model in ANY class of its own
	// size range parks nothing, every time, with no symptom in the sim. That is
	// the 17.2% case, and this is what makes it visible before it is written.
	//
	// When phase 4 lands, the same per-class terms get weighted by `1313` instead
	// of treated as a flat range, and this becomes a probability. The shape of the
	// computation does not change.
	struct Coverage {
		bool	index_ready;		// false => index missing/unreadable. MUST be shown as
									//   such and never as "0%" - see spec §4.5 and §6.4.
		std::string	index_version;	// what the numbers were resolved against (§4.5)
		int		stands;				// stands examined
		int		stands_empty;		// ...of which park nothing, over their own range
		// Set when the single selected stand carries a 1313 row. Then p_occupied
		// is the real §4.5 quantity - 1 - P(empty), weighted by the author's own
		// class distribution - rather than the flat-range approximation.
		bool	weighted;
		float	p_occupied;			// 0..1, only meaningful when `weighted`
		// Why a weighted stand parks nothing. Spec §4.5 point 2 notes the causes
		// are indistinguishable from outside the editor, and §4.5's whole claim
		// is that the readout is where they get told apart - so it has to
		// actually tell them apart. R14's table says the last two must not be
		// treated alike: one is an author error a click repairs, the other is an
		// author correctly working ahead of the art.
		enum EmptyCause {
			empty_None = 0,		// something spawns
			empty_ByChoice,		// all six weights zero - the author said so (§4.2)
			empty_Unfillable,	// the listed operators have nothing at a weighted class
			empty_NoArtYet		// NOTHING in the library has anything at those classes
		};
		int		empty_cause;
		// Single-stand detail. Meaningless (and not drawn) when stands != 1.
		int		classes_in_range;
		int		classes_filled;
		int		airlines_listed;
		int		airlines_eligible;	// at least one model somewhere in the range
		// The operator that will park here when it is the ONLY one that can.
		// Empty unless airlines_eligible == 1 and more than one was listed -
		// see the audit: 17.5% of multi-operator stands collapse to exactly one
		// airline forever, which produces aircraft, looks fine, and is invisible
		// to every P(empty) check in the design.
		std::string	sole_operator;
		char	lo_class, hi_class;	// 'A'..'F'
	};
	void				RecomputeCoverage(void);
	// Cheap because the index is already resident; still not something to run per
	// frame at CDG (326 stands x 6 classes x n airlines), hence the dirty flag.
	Coverage					mCoverage;
	bool						mCoverageDirty;

	// ---- livery preview cards (framework/scaffolding only - see WED_LiveryThumbnailCache.h) ----
	// The cards are the FIRST ROWS of the same scrolled content as the airline
	// checklist, not a separate viewport with its own scrollbar - one mScrollOffset
	// moves cards and checklist together, so the checklist can't get stranded off
	// the bottom of the tab. Layout within that content is:
	//     ContentTop() -> [ kCardCount cards ] -> [ checklist rows ] -> bottom
	// Cards are currently populated from mPreviewObjVpaths - an arbitrary handful of
	// library .obj resources, not real airline/aircraft matches (that bridge doesn't
	// exist yet - see this pane's own top-of-file THEORY OF OPERATION comment).
	//
	// CardHeight() takes bounds because the image's height derives from the pane's
	// ACTUAL width (fixed 16:9) - a card is exactly as tall as its full-width image
	// plus one text row, so it must never be computed from a nominal/guessed width
	// (that mismatch once rendered the whole block as one oversized black slab - see
	// CardRectForIndex()'s image_h, which must use this SAME bounds-derived width).
	float				CardWidth(int bounds[4]) const;			// one column's width, gaps already taken out
	float				CardHeight(int bounds[4]) const;
	float				CardsBlockHeight(int bounds[4]) const;	// total height the cards occupy within the scrolled content
	void				CardRectForIndex(int bounds[4], int index, float r_out[4]) const;
	int					CardForXY(int bounds[4], int x, int y) const;	// -1 if the point isn't on a card (gaps included)
	const WED_LiveryThumbnail *	EnsureRawFlagTexture(const std::string & ioc_country_code);	// no masking - see .cpp

	// ---- ramp operation filter (row of chips) ----
	int					FilterChipForXY(int bounds[4], int x, int y) const;	// -1 if none
	void				SetRampOpFilter(int wed_ramp_op_enum);

	// ---- size range slider ----
	// Handle IDs used for hit-testing/hover only: -1 none, 0 = nearer the min ball,
	// 1 = nearer the max ball, 2 = the two balls currently overlap. Dragging itself
	// doesn't care which ball you grabbed - see the anchor/moving design below.
	int					SliderHandleForXY(int bounds[4], int x, int y) const;
	float				SliderContinuousIndexForX(int bounds[4], int x) const;	// float in [0,5], unsnapped

	// A drag tracks the OTHER (unmoving) ball as an anchor and the grabbed ball's
	// live position as "current". min/max are re-derived every update as
	// min(anchor,current)/max(anchor,current), so dragging straight through the
	// other ball and out the far side "just works" - no special-casing needed.
	void				ApplyDragRange(void);

	// ---- list toolbar (sort + show-recommendation buttons) ----
	// Both buttons live in one row, right-aligned as a pair against the tab's
	// right border: [Show Recommendation][Sort A-Z/Z-A]|. Rects computed from
	// bounds alone (no stored state) so hit-testing and drawing never drift.
	void				SortButtonRect(int bounds[4], float b_out[4]) const;
	void				RecommendButtonRect(int bounds[4], float b_out[4]) const;
	// Everything left of RecommendButtonRect(), down to the box's own left
	// edge - the search field auto-fills whatever room that leaves.
	void				SearchFieldRect(int bounds[4], float b_out[4]) const;
	// Small square carved out of SearchFieldRect()'s own right edge - only
	// meaningful (and only drawn/hit-tested) while the field has text in it;
	// see Draw()'s search-box block for why this doesn't overlap the actual
	// GUI_TextField's bounds.
	void				SearchClearButtonRect(int bounds[4], float b_out[4]) const;

	// ---- airline checklist ----
	int					RowForY(int bounds[4], int y) const;
	void				ToggleCode(const std::string& icao);
	void				AbortSizeDrag(void);	// rolls back an unfinished slider drag; no-op if none
	int					CountRampsWithCode(const std::string & icao) const;	// for the tri-state checkbox

	IResolver *					mResolver;
	WED_Archive *				mArchive;
	GUI_TabPane *				mHostTabs;

	std::vector<WED_RampPosition *>	mSelectedRamps;


	// Opt-in mirror image of the above (see gPromptLiveriesOnRampSelect in
	// WED_Globals.h): the last ramp-only selection we auto-switched INTO this
	// tab for, so a repeat of the exact same selection doesn't re-trigger but
	// any different one (or a fresh reselection after going empty) does.
	std::vector<WED_RampPosition *>	mLastAutoSwitchedInRamps;

	int							mTrackRow;			// airline row being clicked, -1 if none
	int							mTrackFilterChip;	// filter chip being clicked, -1 if none
	int							mHoverFilterChip;	// filter chip currently under the mouse, -1 if none
	int							mHoverRow;			// airline row currently under the mouse, -1 if none
	int							mHoverSliderHandle;	// slider handle currently under the mouse (0/1), -1 if none

	// Sort direction for the airline list - in-memory only (not persisted;
	// resets to the default A-Z each time WED starts). Applies to both
	// halves of the list when "Show Recommendation" is on.
	bool						mSortDescending;
	bool						mHoverSortButton, mTrackSortButton;
	bool						mHoverRecommendButton, mTrackRecommendButton;

	// Live-filter search box, left of the Recommend button. GUI_TextField
	// always paints its own opaque box, so there's no native placeholder -
	// we hide it and hand-draw a "Lookup Operators" placeholder instead
	// whenever it's both empty AND unfocused (see Draw()/MouseDown()).
	// mSearchQuery mirrors the field's live text (kept in sync via
	// ReceiveMessage(GUI_TEXT_FIELD_TEXT_CHANGED)) so BuildDisplayRows() can
	// filter without asking the widget directly.
	GUI_TextField *				mSearchField;
	std::string					mSearchQuery;
	bool						mHoverClearButton, mTrackClearButton;

	// Vertical scroll position of the airline checklist, in pixels (0 = top,
	// larger = scrolled further down). ScrollWheel() adjusts it; Draw() re-
	// clamps it to [0, max] every frame against the CURRENT row count (so a
	// filter/search change that shrinks the list can never leave it scrolled
	// past the new end), and RowForY() reads it to keep hit-testing in sync
	// with whatever Draw() last actually painted.
	float						mScrollOffset;

	// The airline code actually feeding the country/flag lookup this Draw()
	// call (already the metadata-preferred, fallback-resolved code - see the
	// top of Draw()) - cached so the "Show Recommendation" button's enabled
	// state can be checked from MouseDown() too, not just at Draw() time.
	// Empty when nothing resolved (no flag/no recommendation data either).
	std::string					mCurrentAirportIcao;

	// "Popular Airlines" tier's per-airport, session-stable random draw - see
	// GetPopularAirlinesCodes()'s doc comment in the .cpp for the full algorithm.
	// Computed once per airport ICAO the first time it's needed, then reused for
	// the rest of this WED run (not re-shuffled on every Draw(), and not reset
	// when switching away and back to the same airport) - keyed by airport ICAO
	// so different airports naturally get independent draws.
	std::map<std::string, std::vector<std::string>>	mPopularAirlinesShuffleCache;

	// Drag state. mDragAnchorIndex is the OTHER ball's fixed position for the
	// whole gesture; mDragCurrentIndex is the grabbed ball's live, detent-snapped
	// position. mDragHandle just tracks which side "current" is presently on
	// (0 = acting as min, 1 = acting as max, 2 = coincides with the anchor) so the
	// hover/highlight ring follows the right ball - it has no effect on the
	// min/max values actually written, which always come from
	// min(anchor,current)/max(anchor,current).
	int							mDragHandle;		// -1 when not dragging
	int							mDragAnchorIndex;
	int							mDragCurrentIndex;

	// Weight-bar drag. mDragWeightBar is the grabbed bar, -1 when idle; the
	// gesture is locked to it, so sliding sideways never paints across its
	// neighbours. mDragWeights is the live vector being written; mDragWeights0
	// is what it was when the mouse went down, so MouseUp can abort instead of
	// commit when nothing actually moved - a click that lands on a bar's
	// existing height must not leave an entry on the undo stack.
	int							mDragWeightBar;
	int							mDragWeights[6];
	int							mDragWeights0[6];
	int							mHoverWeightBar;	// -1 when the cursor is off the bars
	bool						mHoverWeightButton, mTrackWeightButton;

	// Single reference table for everything the picker needs to know about
	// an airport before looking at any one ramp: country (flag banner +
	// "same country" recommendation tier) and researched airlines (the
	// recommendation list itself), both keyed by ICAO. Loaded once, on
	// first need, from WED_AirportDatabase.txt shipped next to WED.exe -
	// no local X-Plane install/apt.dat involved. See WED_AirportDatabase.h.
	WED_AirportDatabase			mAirportDb;

	// Global airline reference data (WED_AirlineDirectory.txt, shipped loose
	// next to WED.exe like the two indexes above) - name/country/fleet size
	// for every airline WED knows about, independent of any one airport. Used
	// to resolve a friendly name for a recommended code that isn't one of
	// WED_LiveryData.h's ~26 hardcoded placeholder entries. See
	// WED_AirlineDirectory.h.
	WED_AirlineDirectory		mAirlineDirectory;

	// The shipped static-aircraft index (livery_index.txt, inside the selected
	// X-Plane folder - NOT next to WED.exe like the two above). This is the first
	// consumer the class has ever had; see the appendix of WED_LiveryFormatSpec.md.
	// Loaded lazily and re-loaded whenever the resolved path changes, which is how
	// it survives the user switching X-Plane folders mid-session.
	WED_LiveryIndex				mLiveryIndex;

	// ---- country flag banner (WED_FlagProjector / WED_FlagAssets) ----
	// Re-projected only when the displayed country actually changes (this is
	// a ~2048x768-pixel warp+composite, not something to redo every Draw()).
	// mFlagTexCountry == "" means "no texture uploaded yet / nothing to show".
	void			EnsureFlagTexture(const std::string & ioc_country_code);
	std::string		mFlagTexCountry;
	unsigned int	mFlagTexId;
	int				mFlagTexW, mFlagTexH;

	// Always exactly half the tab's width, flush to the pane's left border
	// and to the strip's own top edge. Single source of truth so the
	// info-text x-offset (computed early) and the actual draw (issued last,
	// so the banner paints over whatever it overlaps) always agree.
	void			FlagBannerRect(int bounds[4], float strip_top, float strip_bot,
						float & out_x, float & out_y, float & out_w, float & out_h) const;

	// Word-wrapped info_text/status_text lines, recomputed once near the top
	// of every Draw() call (before AirportInfoYRange() is first used) so
	// AirportInfoHeight() can size the tray to fit them on the SAME frame
	// they change, rather than a frame late - same reasoning as the flag
	// texture ordering requirement documented at the top of Draw().
	std::vector<std::string>	mCachedInfoLines;
	std::vector<std::string>	mCachedStatusLines;

	// ---- livery preview cards (framework/scaffolding only) ----
	WED_LiveryThumbnailCache	mThumbCache;
	std::string					mLiveryIndexPath;	// last resolved path; a change means a new X-Plane root

	// One card per livery that can actually spawn on the selected stand. Built
	// from the index by RebuildPreviewCards(), which asks it the SAME question
	// RecomputeCoverage() asks - so the cards and the readout above them cannot
	// end up describing different aircraft.
	//
	// Static previews, not a picker. The selection state these used to carry
	// served the per-stand aircraft-type whitelist that draft 7 deleted (spec
	// §8.6); a tick on a card has nothing left to mean, and since the list is
	// rebuilt whenever the operators or the weights change, a mark kept by index
	// would drift onto a different aircraft anyway.
	struct PreviewCard {
		std::string	obj_path;		// as the index stores it, relative to apt_aircraft/
		std::string	abs_path;		// joined and separator-normalised; THE CACHE KEY
		std::string	airline;		// ICAO code - grouping, and the caption's left half
		std::string	caption;		// WED_LiveryDisplayName(friendly name, note)
		std::string	ioc_country;	// reg_country, for the flag icon
	};
	std::vector<PreviewCard>	mPreviewCards;
	void						RebuildPreviewCards(void);

	// Click-and-drag scrolling of the content area ("grab and pull", same feel as a
	// touch scroll) - moves mScrollOffset, so cards and checklist move together.
	// mContentDragStartY < 0 means no drag in progress; mContentDragStartOffset is
	// the scroll position the gesture started from, so the live offset always derives
	// from the gesture's TOTAL cursor movement rather than accumulated per-move
	// deltas (which would drift over a long drag).
	int							mContentDragStartY;
	float						mContentDragStartOffset;

	// Hover only. Cards are previews of what this stand will spawn, not a
	// picker - the selection they used to carry belonged to the per-stand
	// aircraft-type whitelist that draft 7 removed (spec §8.6).
	int							mHoverCard;			// -1 if the cursor isn't over a card

	std::map<std::string, WED_LiveryThumbnail>	mRawFlagTex;	// unmasked flag icon textures (w/h = source PNG's own), keyed by IOC country code

};

#endif /* WED_LIVERYPANE_H */
