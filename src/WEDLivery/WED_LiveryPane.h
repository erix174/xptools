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
	float				ListToolbarHeight(void) const;
	float				GapHeight(void) const;
	void				AirportInfoYRange(int bounds[4], float & top, float & bot) const;
	void				HeaderYRange(int bounds[4], float & top, float & bot) const;
	void				FilterYRange(int bounds[4], float & top, float & bot) const;
	void				SliderYRange(int bounds[4], float & top, float & bot) const;
	void				ListToolbarYRange(int bounds[4], float & top, float & bot) const;
	float				ContentTop(int bounds[4]) const;		// top Y of the airline checklist

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

	// This tab is always clickable now (no more SetPaneEnabled-based lock),
	// but the FIRST time selection goes empty while the user is actually
	// looking at it, we bounce them back to "Selection" once - true right
	// up until selection becomes non-empty again, so a second empty-out
	// (or the user manually clicking back in while still empty) doesn't
	// keep yanking them out.
	bool						mAutoSwitchedAwayOnEmpty;

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
	std::vector<std::string>	mPreviewObjVpaths;	// populated once in the constructor

	// Click-and-drag scrolling of the content area ("grab and pull", same feel as a
	// touch scroll) - moves mScrollOffset, so cards and checklist move together.
	// mContentDragStartY < 0 means no drag in progress; mContentDragStartOffset is
	// the scroll position the gesture started from, so the live offset always derives
	// from the gesture's TOTAL cursor movement rather than accumulated per-move
	// deltas (which would drift over a long drag).
	int							mContentDragStartY;
	float						mContentDragStartOffset;

	// Card interaction. A card is a toggle: click it to select that livery, click
	// again to deselect. mTrackCard is the one currently held down (-1 = none) and
	// only becomes a toggle on mouse-up, and only if the gesture didn't turn into a
	// drag-scroll (see MouseUp's slop check) - so dragging the grid to scroll never
	// accidentally flips a selection.
	int							mHoverCard;			// -1 if the cursor isn't over a card
	int							mTrackCard;			// -1 if no card is pressed
	std::set<int>				mSelectedCards;		// indices into mPreviewObjVpaths

	std::map<std::string, WED_LiveryThumbnail>	mRawFlagTex;	// unmasked flag icon textures (w/h = source PNG's own), keyed by IOC country code

};

#endif /* WED_LIVERYPANE_H */
