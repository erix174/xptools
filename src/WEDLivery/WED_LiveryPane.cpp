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

#include "WED_LiveryPaneInternal.h"

// State, selection, the cards and the coverage readout.


WED_LiveryPane::WED_LiveryPane(
						IResolver *		resolver,
						WED_Archive *	archive,
						GUI_TabPane *	host_tabs) :
	mLiveryIndex(WED_SharedLiveryIndex()),
	// The host tab pane is itself a commander (GUI_TabPane.h:35), so it is the
	// natural parent: focus flows window -> tab pane -> this pane -> mSearchField.
	GUI_Commander(host_tabs),
	mResolver(resolver),
	mArchive(archive),
	mHostTabs(host_tabs),
	mTrackRow(-1),
	mTrackFilterChip(-1),
	mHoverFilterChip(-1),
	mHoverRow(-1),
	mHoverSliderHandle(-1),
	mSortDescending(false),
	mHoverSortButton(false),
	mTrackSortButton(false),
	mHoverRecommendButton(false),
	mTrackRecommendButton(false),
	mDragHandle(-1),
	mDragAnchorIndex(-1),
	mDragCurrentIndex(-1),
	mFlagTexId(0),
	mFlagTexW(0),
	mFlagTexH(0),
	mSearchField(NULL),
	mHoverClearButton(false),
	mTrackClearButton(false),
	mScrollOffset(0),
	mContentDragStartY(-1),
	mContentDragStartX(-1),		// declared later in the header (after mCachedStatusLines) -
	mContentDragStartOffset(0),	// listed here anyway so all the "simple scalar" inits stay together
	mCycleShow(0),
	mRowsDirty(true),
	mCardsByType(false),
	mCoverageLineCount(3),
	mCoverageHasDetail(false),
	mHoverCoverageToggle(false),
	mLastAnimClock(0.0),
	mTrayHoverIdx(-1),
	mHoverX(0),
	mHoverY(0),
	mCycleAccum(0.0f),
	mCyclePrevShow(0),
	mCycleNextReady(true),
	mCycleFade(1.0f),
	mTrayOpen(0.0f),
	mTrayClosingOpen(0.0f),
	mDragWeightBar(-1),
	mHoverWeightBar(-1),
	mHoverWeightButton(false),
	mTrackWeightButton(false),
	mHoverPopulate(false),
	mTrackPopulate(false),
	mPopulateFlashUntil(0.0),
	mCoverageDirty(true)
{
	// Zeroed rather than left indeterminate: Draw() reads mCoverage before the
	// first RecomputeCoverage() can run if a frame lands before any selection
	// change, and index_ready == false makes that frame say "index not loaded"
	// instead of printing garbage counts.
	// Field by field, NOT memset. Coverage carries std::strings now
	// (index_version, sole_operator), and zeroing the bytes of a std::string is
	// undefined behaviour - it overwrites the object's own bookkeeping, so the
	// first assignment or destruction afterwards is working from a state the
	// implementation never produced. It happened to survive on MSVC's small
	// string; that is luck, not a guarantee.
	mCoverage.index_ready      = false;
	mCoverage.weighted         = false;
	mCoverage.p_occupied       = 0.0f;
	mCoverage.empty_cause      = Coverage::empty_None;
	mCoverage.stands           = 0;
	mCoverage.stands_empty     = 0;
	mCoverage.classes_in_range = 0;
	// "All Airlines" is the only tier with no filter behind it, so it can be
	// hundreds of cards. It starts closed: rendering those unasked is the one way
	// this pane can stall, and a user who scrolls to the bottom should not be the
	// one who discovers that.
	mCollapsedSections.insert("All Airlines");

	mCoverage.classes_filled   = 0;
	mCoverage.airlines_listed  = 0;
	mCoverage.airlines_eligible= 0;
	mCoverage.lo_class         = 'A';
	mCoverage.hi_class         = 'F';

	// Seeds the "Popular Airlines" weighted shuffle (see GetPopularAirlinesCodes()) once
	// per WED run, not once per pane/document - a static guard rather than reseeding
	// every time a new document window is opened, which would just make same-second opens
	// reproduce the same draw.
	static bool sRandSeeded = false;
	if (!sRandSeeded)
	{
		srand((unsigned int) time(NULL));
		sRandSeeded = true;
	}

	mArchive->AddListener(this);

	// Owned as a child pane (auto-deleted by ~GUI_Pane) rather than in our
	// own member list of drawables - GUI_Pane's own child dispatch gives it
	// first refusal on mouse/key events for free, so this is the only widget
	// in this whole custom-drawn pane that doesn't need manual hit-testing.
	mSearchField = new GUI_TextField(0, mHostTabs);
	mSearchField->SetParent(this);
	mSearchField->Show();
	mSearchField->SetKeyMsg(GUI_TEXT_FIELD_TEXT_CHANGED, 0);
	mSearchField->AddListener(this);

	// Airline names/ICAO codes are plain ASCII - lock the field down to
	// exactly that (deny-by-default, then re-allow only the specific chars
	// this lookup actually needs) rather than trying to enumerate every
	// character to reject. char 0 doubles as OGLE's "extended" bit (see
	// GUI_TextField::HandleKeyPress), so disallowing it blocks every
	// codepoint above 255 in one shot - Chinese/Japanese/Korean/emoji/etc
	// included, without needing to know their actual code points.
	for (int i = 0; i < 256; ++i)
	{
		bool keep =  (i >= 'a' && i <= 'z')
				  || (i >= 'A' && i <= 'Z')
				  || (i >= '0' && i <= '9')
				  || i == ' '
				  || i == GUI_KEY_BACK || i == GUI_KEY_DELETE
				  || i == GUI_KEY_LEFT || i == GUI_KEY_RIGHT || i == GUI_KEY_UP || i == GUI_KEY_DOWN;
		mSearchField->SetKeyAllowed((char) i, keep);
	}

	RebuildSelection();

	// Cards are built from the livery index on the first Draw and rebuilt
	// whenever the selection, the operators or the weights change - see
	// RebuildAirlineCards(). There is nothing to seed here.
}

WED_LiveryPane::~WED_LiveryPane()
{
	// Release through Hide(), which is the context-safe path: it is called on tab
	// switch, while the window's GL context is still current and guaranteed valid.
	// Deleting textures directly from a destructor runs at document-window
	// teardown, where the context may already be gone - a silent no-op on every
	// driver we ship against, but undefined by the spec, and free to avoid.
	//
	// Hide() clears both containers, so anything it released cannot be released
	// twice by the sweep below; that sweep only exists for a texture created
	// after the last Hide().
	Hide();

	if (mFlagTexId != 0)
	{
		glDeleteTextures(1, &mFlagTexId);
		mFlagTexId = 0;
	}
	for (map<string, WED_LiveryThumbnail>::iterator i = mRawFlagTex.begin(); i != mRawFlagTex.end(); ++i)
		if (i->second.tex != 0)
			glDeleteTextures(1, &i->second.tex);
	mRawFlagTex.clear();
}

// Called by GUI_TabPane when the user switches to a different property tab (see
// GUI_TabPane.cpp) - discards every cached card thumbnail/flag texture so this tab
// isn't paying GPU memory for cards nobody can see. Cards regenerate lazily the next
// time this tab (and thus Draw()) becomes visible again.
void	WED_LiveryPane::Hide(void)
{
	// A size-slider drag opens its command in MouseDown and closes it in MouseUp,
	// so the command is live across everything in between - and Hide() IS
	// reachable in between, via the tab switch that SetTab broadcasts. Leaving it
	// open would strand the archive: the NEXT StartCommand anywhere in WED trips
	// the undo manager's "a command is already open" assert, with no clue that a
	// hidden tab caused it. Abort rather than commit - a drag the user never
	// finished should not land in the undo stack.
	AbortSizeDrag();
	AbortWeightDrag();		// same reason - see AbortSizeDrag()'s caller comment above

	// Nothing may keep animating behind a hidden tab: the timer would go on
	// rebuilding the row list and re-laying it out every frame, forever, for a
	// pane nobody is looking at.
	mCycleAirline.clear();  mCycleShow = 0;  mCycleAccum = 0.0f;
	mTrayClosing.clear();   mTrayClosingOpen = 0.0f;
	mLastAnimClock = 0.0;

	// Everything else the mouse was in the middle of, too. Only the two drags
	// above own an archive command, so only they can strand it - but the rest of
	// the gesture state is just as live, and it is what made the pane follow the
	// cursor with no button held after switching away mid-drag and back: the
	// tab that gets hidden never receives the MouseUp that would have cleared
	// it, so the pane came back still believing a gesture was in progress.
	mContentDragStartY    = -1;
	mContentDragStartX    = -1;
	mTrackRow             = -1;
	mTrackFilterChip      = -1;
	mTrackSortButton      = false;
	mTrackRecommendButton = false;
	mTrackClearButton     = false;
	mTrackWeightButton    = false;
	mTrackPopulate        = false;

	// Hover highlights too, or the pane repaints with a lit-up control under a
	// cursor that is somewhere else entirely.
	mHoverRow             = -1;
	mHoverFilterChip      = -1;
	mHoverSliderHandle    = -1;
	mHoverWeightBar       = -1;
	mHoverWeightButton    = false;
	mHoverPopulate        = false;
	mHoverSortButton      = false;
	mHoverRecommendButton = false;
	mHoverClearButton     = false;

	GUI_Pane::Hide();
	mThumbCache.DiscardAll();
	for (map<string, WED_LiveryThumbnail>::iterator i = mRawFlagTex.begin(); i != mRawFlagTex.end(); ++i)
		if (i->second.tex != 0)
			glDeleteTextures(1, &i->second.tex);
	mRawFlagTex.clear();
}

void	WED_LiveryPane::ReceiveMessage(
							GUI_Broadcaster *		inSrc,
							intptr_t				inMsg,
							intptr_t				inParam)
{
	if (inMsg == msg_ArchiveChangedEphemerally)
	{
		// Sent on every mouse move of a map drag. Rebuilding every card and the
		// coverage on each one is what made dragging stutter; the drag's end sends
		// msg_ArchiveChanged, and that recomputes everything once. Only a change in
		// WHICH ramps are selected is worth acting on mid-drag.
		vector<WED_RampPosition *> now;
		ISelection * sel = WED_GetSelect(mResolver);
		if (sel) sel->IterateSelectionOr(CollectRamps, &now);
		if (now != mSelectedRamps)
		{
			RebuildSelection();
			Refresh();
		}
	}
	else if (inMsg == msg_ArchiveChanged)
	{
		RebuildSelection();
		Refresh();
	}
	else if (inMsg == GUI_TEXT_FIELD_TEXT_CHANGED && inSrc == mSearchField)
	{
		mSearchField->GetDescriptor(mSearchQuery);
		SetRowsDirty();			// the query filters the list
		Refresh();
	}
}

void	WED_LiveryPane::RebuildSelection(void)
{
	vector<WED_RampPosition *> old_selection = mSelectedRamps;

	mSelectedRamps.clear();
	ISelection * sel = WED_GetSelect(mResolver);
	if (sel) sel->IterateSelectionOr(CollectRamps, &mSelectedRamps);

	// Unconditional, not gated on (mSelectedRamps != old_selection): the same set
	// of ramps can come back with different airlines or a different size range
	// after an undo, a property-grid edit, or a change made on another tab.
	mCoverageDirty = true;

	// A genuinely different ramp selection (different ramp, or a different airport
	// entirely) means the checklist content just changed out from under whatever
	// scroll position was left over from before - snap back to the top rather than
	// risk leaving the user scrolled past a short "Recommended"/"Manual" section (or
	// the whole list) for the new selection. An unrelated Draw() call caused by
	// something else entirely (e.g. just moving the mouse) leaves this alone.
	SetRowsDirty();		// a new selection is a new list, always

	if (mSelectedRamps != old_selection)
	{
		mScrollOffset = 0;

		// The lock, the open tray and the running slideshow are all statements
		// about the stand being edited, not about the document. Carrying them to a
		// different stand would dim a list the user has not touched yet, and the
		// lock in particular would arrive with no indication of where it came from.
		mLockedAirline.clear();
		mTrayAirline.clear();   mTrayOpen = 0.0f;
		mTrayClosing.clear();   mTrayClosingOpen = 0.0f;
		mCycleAirline.clear();  mCycleShow = 0;  mCycleAccum = 0.0f;
		mTrayHoverIdx = -1;
	}

	// No more SetPaneEnabled() lock - the tab stays clickable even with
	// nothing selected (the greyed-out mask + warning text in Draw() carries
	// that state instead). Only auto-navigate the user OFF this tab the
	// first time it goes empty while they're actually looking at it; once
	// they've manually clicked back in with nothing selected, leave them be
	// until selection is non-empty again.
	if (mSelectedRamps.empty())
	{
		// Deliberately NO auto-navigate away. Clicking a different ramp start
		// clears the old selection before setting the new one, so the selection
		// passes through empty on the way - and bouncing to the Selection tab at
		// that instant threw the author off this one every single time they
		// picked another stand, whether or not they had asked to be brought here.
		//
		// The empty state is already carried by Draw()'s greyed mask and its
		// warning text, which is what that mask is FOR. Leaving the tab up and
		// masked for a moment is the correct behaviour; navigating away from the
		// thing the author is working in is not.
		mLastAutoSwitchedInRamps.clear();		// selection's gone - a later re-selection counts as "new" again
	}
	else
	{

		// Opt-in (WED Preferences > "When Selecting Ramp Start" > "Prompt Up
		// Static Liveries Tab", off by default - see WED_Application.cpp).
		// Only fires for a selection that's ENTIRELY ramp starts (no other
		// entity types mixed in) - GetSelectionCount() covers everything
		// selected, mSelectedRamps only what CollectRamps recognized, so
		// they only match when every selected item was a ramp. Fires once
		// per DIFFERENT ramp selection (tracked via mLastAutoSwitchedInRamps,
		// compared by pointer identity) - the user can freely navigate away
		// afterward without getting yanked back until they select some other
		// (or newly re-selected) set of ramps.
		bool pure_ramp_selection = sel && sel->GetSelectionCount() == (int) mSelectedRamps.size();
		if (!pure_ramp_selection)
			mLastAutoSwitchedInRamps.clear();
		else if (gPromptLiveriesOnRampSelect && mHostTabs && mSelectedRamps != mLastAutoSwitchedInRamps)
		{
			int my_tab = mHostTabs->GetTabForPane(this);
			if (my_tab >= 0) mHostTabs->SetTab(my_tab);
			mLastAutoSwitchedInRamps = mSelectedRamps;
		}
	}
}

// ---------------------------------------------------------------------------------------------
// preview cards
// ---------------------------------------------------------------------------------------------

// One card per livery that can actually spawn on the selected stand.
//
// The question asked here is deliberately the same one RecomputeCoverage() asks
// - which classes can spawn, and which of the listed operators has a livery at
// each - so the cards and the sentence above them are answers to one query
// rather than two that can disagree. They used to disagree spectacularly: a
// stand reading "this stand parks nothing" had four aircraft pictured directly
// underneath it, because the cards were the first four objects in the library
// and had nothing to do with the ramp at all.
//
// Obsolete liveries need no filtering here. R25 keeps them out of the index's
// lookup tables entirely, so a query cannot return one.


void	WED_LiveryPane::EnsureRows(void)
{
	if (!mRowsDirty) return;
	mRowsDirty = false;

	mRows.clear();
	mRowIsCard.clear();
	mRowIcaos.clear();
	if (mSelectedRamps.empty()) return;

	mRows = BuildCurrentDisplayRows(mSelectedRamps[0], mSortDescending,
				gShowLiveryRecommendation != 0, mAirportDb, mCurrentAirportIcao,
				mSearchQuery, mAirlineDirectory, mPopularAirlinesShuffleCache, AllOperators());
	set<string> have_cards;
	CardKeys(have_cards);
	DropCardless(mRows, have_cards);
	PruneEmptySections(mRows);
	PinSelected(mRows, ParseCodes(mSelectedRamps[0]->GetAirlines()), have_cards);
	ApplyCollapse(mRows, mCollapsedSections);

	CardFlags(mRows, mRowIsCard);
	RowIcaos(mRows, mRowIcaos);
}



// MAY THIS LIVERY APPEAR AT THIS STAND. Three answers, by operation class:
//
//   General aviation  - anywhere. A private turboprop's range is short and its
//                       "hub" is wherever its owner lives; measuring either would
//                       filter out exactly the aircraft that turn up at every
//                       small field on earth. No range rule.
//   Military and Gov  - anywhere, never range-checked, EXCEPT a row marked HOME
//                       (R27): a head-of-state 757 or an air force's own-marked
//                       airliner parks only in its operator's country. An F-15
//                       at a foreign base is unremarkable; a C-32 is not. Either
//                       country unknown -> allowed, fail open.
//   Everything else   - the range rule, R26.
//
// The reason a livery was refused comes back so the readout can name it: only
// range refusals go into mRangeHidden, because that is the line's subject.
WED_LiveryPane::Allow	WED_LiveryPane::LiveryAllowedHere(const WED_LiveryIndexEntry & e, const Point2 & here) const
{
	// The rule lives in WED_LiveryRules so auto-fill applies exactly the same one.
	switch (WED_LiveryAllowedAt(e, mAirlineDirectory, mAirportCountry, here.y(), here.x())) {
	case livery_allow_OutOfRange:		return allow_OutOfRange;
	case livery_allow_ForeignMilitary:	return allow_ForeignMilitary;
	default:							return allow_Yes;
	}
}

string	WED_LiveryPane::OperatorCountry(const string & code_uc) const
{
	WED_AirlineDirectoryEntry e;
	if (mAirlineDirectory.Lookup(code_uc, e) && !e.country.empty()) return e.country;
	return code_uc;
}

// Which operation classes a ramp's operation type admits. Pseudo-codes are the
// index's own (see WED_LiveryIndex.h): XPGA general aviation - light aircraft
// and business jets alike, since the ramp draws no distinction between them -
// XPMI military, and XPZZ a generic unpainted airliner any commercial stand may
// use. A code this function does not know still gets an answer from its
// operator record's OP column, so retiring a pseudo-code costs nothing.
bool	WED_LiveryPane::OperatorMatchesRampOp(const string & code_uc, int ramp_op) const
{
	if (ramp_op == ramp_operation_None) return true;			// not stated - offer everything

	if (code_uc == "XPGA")                      return ramp_op == ramp_operation_GeneralAviation;
	if (code_uc == "XPMI")                      return ramp_op == ramp_operation_Military;
	if (WED_IsGenericAirlinerCode(code_uc))     return ramp_op == ramp_operation_Airline || ramp_op == ramp_operation_Cargo;

	WED_AirlineDirectoryEntry e;
	if (!mAirlineDirectory.Lookup(code_uc, e))
		return ramp_op == ramp_operation_Airline;			// unknown to the directory: assume airline, fail open

	switch (e.op_class)
	{
	case WED_AirlineDirectoryEntry::op_Pax:      return ramp_op == ramp_operation_Airline;
	case WED_AirlineDirectoryEntry::op_Cargo:    return ramp_op == ramp_operation_Cargo;
	case WED_AirlineDirectoryEntry::op_GA:       return ramp_op == ramp_operation_GeneralAviation;
	case WED_AirlineDirectoryEntry::op_Military:
	case WED_AirlineDirectoryEntry::op_Gov:      return ramp_op == ramp_operation_Military;
	}
	return true;
}

void	WED_LiveryPane::RebuildAirlineCards(void)
{
	mAirlineCards.clear();

	if (mSelectedRamps.size() != 1) return;		// a mixed selection has no single answer to preview
	if (!mLiveryIndex.IsLoaded())   return;

	WED_RampPosition * ramp = mSelectedRamps[0];

	// Which classes this stand can draw. Weights when it has them, otherwise the
	// size range - R17's two states, and the same branch RecomputeCoverage takes.
	bool use_class[6] = { false, false, false, false, false, false };
	int  w[6];
	if (ramp->GetClassWeights(w))
	{
		for (int k = 0; k < 6; ++k) use_class[k] = (w[k] > 0);
	}
	else
	{
		int lo = WidthEnumToIndex(ramp->GetWidthMin());
		int hi = WidthEnumToIndex(ramp->GetWidth());
		if (lo > hi) std::swap(lo, hi);
		for (int k = lo; k <= hi; ++k) use_class[k] = true;
	}

	// EVERY operator the directory knows, not just the ticked ones: a card has to
	// exist before it can be clicked, and clicking a card is now how an operator
	// gets ticked. Operators with nothing at this stand's classes get no card at
	// all, which is what keeps a section honest rather than showing an aircraft
	// that cannot park here.
	vector<string> codes;
	mLiveryIndex.GetAirlineCodes(codes);

	// Where this stand IS, for the range rule below. Same number the sim reads
	// off the 1300 row, so the two evaluate the identical predicate.
	Point2 here;
	ramp->GetLocation(gis_Geo, here);
	mRangeHidden.clear();

	// THE RAMP'S OPERATION TYPE IS A FILTER, not a label. A cargo stand offers
	// cargo operators; a GA stand the generic GA pseudo-code and the private and
	// corporate operators; a military stand the forces and the government fleets.
	// The class is the directory's fourth column. An operator the directory does
	// not know is treated as an airline - fail open, like the range rule - and
	// "None" leaves the list unfiltered, since it means the author has not said.
	const int ramp_op = ramp->GetRampOperationType();

	// GENERAL AVIATION HAS NO OPERATORS. "BTQ", "URF", "WML" are private owners
	// the generator had to give a code to; grouping by them puts one PC-12 on a
	// card of its own and eleven more, plus every Challenger and Cirrus, on the
	// generic XPGA card - a shape that says nothing about what parks here. A GA
	// stand groups by AIRCRAFT TYPE instead: one card per type, its registrations
	// and paints behind it. And nothing on a GA card is a picker - the sim draws
	// GA from the library by size, not from a 1301 list - so ticking and the lock
	// are switched off for them (see MouseUp), and the card is a preview only.
	const bool by_type = (ramp_op == ramp_operation_GeneralAviation || ramp_op == ramp_operation_Military);
	mCardsByType = by_type;

	for (size_t i = 0; i < codes.size(); ++i)
	{
		string code_uc = codes[i];
		for (size_t ci = 0; ci < code_uc.size(); ++ci)
			code_uc[ci] = (char) toupper((unsigned char) code_uc[ci]);

		if (!OperatorMatchesRampOp(code_uc, ramp_op)) continue;

		// BIGGEST CLASS FIRST, and reverse-alphabetically inside a class. Index 0 is
		// what the card shows at rest, so at rest a card shows the largest aircraft
		// that operator can park here - the most informative single frame, and a
		// stable one, since it does not move when an unrelated class is weighted out.
		// Per-operator grouping builds one card here; per-type grouping (GA) files
		// each livery under its type's card instead, so the card is looked up per
		// entry below rather than made once per operator.
		AirlineCard card;
		card.icao = code_uc;
		for (int k = 5; k >= 0; --k)
		{
			if (!use_class[k]) continue;

			vector<const WED_LiveryIndexEntry *> hits;
			mLiveryIndex.GetForAirlineAndClass(code_uc, (char) ('A' + k), hits);

			vector<pair<string, const WED_LiveryIndexEntry *> > sorted;
			for (size_t h = 0; h < hits.size(); ++h)
				sorted.push_back(make_pair(hits[h]->type, hits[h]));
			std::sort(sorted.begin(), sorted.end());
			std::reverse(sorted.begin(), sorted.end());

			for (size_t h = 0; h < sorted.size(); ++h)
			{
				const WED_LiveryIndexEntry * e = sorted[h].second;

				// No path, no entry. An index row whose object cannot be located -
				// no X-Plane root selected yet, for instance - would otherwise be a
				// frame in the cycle that can never draw anything.
				string abs_path = WED_LiveryObjectPath(e->obj_path);
				if (abs_path.empty()) continue;

				// THE RANGE RULE. A livery whose operator has no hub within the
				// aircraft's reach of this stand will not be spawned by the sim, so
				// it is not offered here either - not greyed, not annotated, simply
				// absent, exactly as it will be absent on the apron. What was
				// removed is remembered so the readout can say so; otherwise the
				// author sees United's card shrink to a 777 with no explanation.
				Allow a = LiveryAllowedHere(*e, here);
				if (a != allow_Yes)
				{
					if (a == allow_OutOfRange) mRangeHidden[code_uc].push_back(e->type);
					continue;
				}

				if (by_type)
				{
					string tkey = e->type;
					for (size_t c = 0; c < tkey.size(); ++c) tkey[c] = (char) tolower((unsigned char) tkey[c]);
					AirlineCard & tc = mAirlineCards[tkey];
					tc.icao = e->type;
					tc.name = e->type;
					if (tc.ioc_country.empty()) tc.ioc_country = e->reg_country;
					tc.abs_paths.push_back(abs_path);
					tc.types.push_back(e->type);
					// What distinguishes two PC-12s is the paint, so that is the label:
					// the registration when there is one, the note otherwise.
					string lab = !e->reg.empty() ? e->reg
							   : (e->note != "Default" ? e->note : string("Unmarked"));
					if (!e->reg.empty() && !e->note.empty() && e->note != "Default")
						lab += " (" + e->note + ")";
					// A GA/military card is one type; the operator still tells two
					// air forces' F-15s apart, so it goes on the label, not the face.
					if (code_uc != "XPGA" && code_uc != "XPMI")
						lab += "  " + code_uc;
					tc.labels.push_back(lab);
					continue;
				}

				card.abs_paths.push_back(abs_path);
				card.types.push_back(e->type);

				// "Default" means "no annotation" (see WED_LiveryIndex.h), so it
				// adds nothing; anything else is what separates two liveries of the
				// same type and has to be shown.
				string label = e->type;
				if (!e->note.empty() && e->note != "Default")
				{
					string n = e->note;
					for (size_t c = 0; c < n.size(); ++c)
						if (n[c] == '_') n[c] = ' ';		// stored underscored - see WED_MakeLiveryKey
					label += " (" + n + ")";
				}
				card.labels.push_back(label);
				if (card.ioc_country.empty()) card.ioc_country = e->reg_country;
			}
		}

		if (by_type) continue;						// filed per type above
		if (card.abs_paths.empty()) continue;		// nothing that fits - no card

		// The friendly name if the directory knows the code, the code itself if it
		// does not - a livery the index has is worth showing even when the operator
		// is missing from the name table.
		card.name = mAirlineDirectory.GetName(code_uc);
		if (card.name.empty()) card.name = code_uc;

		// No registration read off any of its liveries (United's 767) left the card
		// without a flag, beside Delta's with one. The operator's own country is
		// the same answer for an airline card.
		if (card.ioc_country.empty())
		{
			WED_AirlineDirectoryEntry d;
			if (mAirlineDirectory.Lookup(code_uc, d)) card.ioc_country = d.country;
		}

		string key = code_uc;
		for (size_t ci = 0; ci < key.size(); ++ci)
			key[ci] = (char) tolower((unsigned char) key[ci]);
		mAirlineCards[key] = card;
	}
}

// Rows carry a lowercase icao; cards are keyed by the same string, so this cannot
// return a card belonging to a different row - see the .h on why the two are not
// a pair of parallel vectors.
vector<pair<string,string> >	WED_LiveryPane::AllOperators(void) const
{
	vector<pair<string,string> > out;
	for (map<string, AirlineCard>::const_iterator i = mAirlineCards.begin(); i != mAirlineCards.end(); ++i)
		out.push_back(make_pair(i->first, i->second.name));
	return out;
}

void	WED_LiveryPane::CardKeys(set<string> & out) const
{
	out.clear();
	for (map<string, AirlineCard>::const_iterator i = mAirlineCards.begin(); i != mAirlineCards.end(); ++i)
		out.insert(i->first);
}

const WED_LiveryPane::AirlineCard *	WED_LiveryPane::CardFor(const string & icao_lower) const
{
	map<string, AirlineCard>::const_iterator i = mAirlineCards.find(icao_lower);
	return (i == mAirlineCards.end()) ? NULL : &i->second;
}

// ---------------------------------------------------------------------------------------------
// spawn weight bars  (apt.dat row 1313)
// ---------------------------------------------------------------------------------------------

bool	WED_LiveryPane::SelectionHasWeights(void) const
{
	int w[6];
	for (size_t i = 0; i < mSelectedRamps.size(); ++i)
		if (mSelectedRamps[i]->GetClassWeights(w)) return true;
	return false;
}

// The weights to draw, or false when the selection disagrees about them. A
// mixed selection still SHOWS the section - the author needs to see that the
// stands differ - it just draws indeterminate, the same answer the tri-state
// operator checkbox gives to the same question.
bool	WED_LiveryPane::SelectionWeights(int out_w[6]) const
{
	bool have = false;
	int  w[6];
	for (size_t i = 0; i < mSelectedRamps.size(); ++i)
	{
		int cur[6];
		if (!mSelectedRamps[i]->GetClassWeights(cur)) return false;	// one stand has none -> mixed
		if (!have) { memcpy(w, cur, sizeof(w)); have = true; }
		else if (memcmp(w, cur, sizeof(w)) != 0) return false;		// they disagree
	}
	if (!have) return false;
	memcpy(out_w, w, sizeof(w));
	return true;
}

// ---------------------------------------------------------------------------------------------
// coverage readout  (WED_LiveryFormatSpec.md §4.5)
// ---------------------------------------------------------------------------------------------

// "Can the operators listed on this stand actually fill it?"
//
// A stand is counted EMPTY when no listed operator has a model in any class of
// the stand's own size range. In the sim that stand parks nothing, every time,
// and produces no log line and no error - an empty gate is indistinguishable
// from a gate that did not happen to get an aircraft this time. Spec §4.5
// measures 7,604 of 44,242 stands (17.2%) in that state across the real global
// apt.dat, at 42% of airports, and notes the cause is almost never "this
// operator has no models" but "none in THIS class".
//
// The index is consulted per (airline, class) rather than per airline: an
// operator having SOME model is not the same as having one that fits here, and
// conflating the two is precisely the mistake that makes the 17.2% invisible.
void	WED_LiveryPane::RecomputeCoverage(void)
{
	mCoverageDirty = false;

	Coverage c;
	c.index_ready       = false;
	c.weighted          = false;
	c.p_occupied        = 0.0f;
	c.empty_cause       = Coverage::empty_None;
	c.stands            = (int) mSelectedRamps.size();
	c.stands_empty      = 0;
	c.classes_in_range  = 0;
	c.classes_filled    = 0;
	c.airlines_listed   = 0;
	c.airlines_eligible = 0;
	c.lo_class          = 'A';
	c.hi_class          = 'F';
	c.pool_mode         = false;
	c.op_type           = ramp_operation_None;
	c.pool_models       = 0;
	c.pool_home         = 0;

	// EnsureLoaded() is a no-op for a path it has already tried, success or
	// failure, so this is safe to call as often as the readout is refreshed - and
	// it re-loads by itself if the user has since pointed WED at a different
	// X-Plane folder, because the path is derived from the root.
	const string index_path = WED_LiveryIndexDefaultPath();
	if (!index_path.empty())
		mLiveryIndex.EnsureLoaded(index_path);

	// A missing or unreadable index MUST NOT read as "nothing fits" - see spec
	// §6.4, where an index/install mismatch is called out as failing silently.
	// Leaving index_ready false makes Draw() say so instead of printing a zero.
	if (!mLiveryIndex.IsLoaded())
	{
		c.index_version = mLiveryIndex.DescribeVersion();
		mCoverage = c;
		return;
	}
	c.index_ready   = true;
	c.index_version = mLiveryIndex.DescribeVersion();

	for (size_t i = 0; i < mSelectedRamps.size(); ++i)
	{
		WED_RampPosition * ramp = mSelectedRamps[i];

		int lo = WidthEnumToIndex(ramp->GetWidthMin());
		int hi = WidthEnumToIndex(ramp->GetWidth());
		if (lo > hi) std::swap(lo, hi);		// defensive; the slider cannot produce it

		// With a 1313 row the size letter is DERIVED - R23 writes only the largest
		// weighted class into 1301 - so width_min..width collapses to one class
		// and the eligibility scan below missed every operator whose aircraft sit
		// lower. A C+E stand listing Air China (737) and United (747) reported
		// "Only UAL will ever park here". Scan what the weights actually open.
		{
			int w[6];
			if (ramp->GetClassWeights(w))
			{
				int wlo = -1, whi = -1;
				for (int k = 0; k < 6; ++k) if (w[k] > 0) { if (wlo < 0) wlo = k; whi = k; }
				if (wlo >= 0) { lo = wlo; hi = whi; }
			}
		}

		set<string> codes = ParseCodes(ramp->GetAirlines());

		// The readout must count what the sim will actually draw from, so the
		// range rule applies here exactly as it does to the cards: an operator
		// whose only class-C aircraft cannot reach this stand does not "fill"
		// class C, and must not be reported as eligible. Same predicate, same
		// stand position, so the percentage and the cards agree.
		Point2 here;
		ramp->GetLocation(gis_Geo, here);

		int  filled_classes  = 0;
		set<string> eligible;
		for (int k = lo; k <= hi; ++k)
		{
			char size_class = (char) ('A' + k);
			bool any_here = false;
			for (set<string>::const_iterator it = codes.begin(); it != codes.end(); ++it)
			{
				vector<const WED_LiveryIndexEntry *> hits;
				mLiveryIndex.GetForAirlineAndClass(*it, size_class, hits);
				bool reachable = false;
				for (size_t h = 0; h < hits.size() && !reachable; ++h)
					reachable = LiveryAllowedHere(*hits[h], here) == allow_Yes;
				if (reachable)
				{
					any_here = true;
					eligible.insert(*it);
				}
			}
			if (any_here) ++filled_classes;
		}

		if (filled_classes == 0) ++c.stands_empty;

		// With a 1313 row the flat range stops being the question. The author
		// has said how often each class is drawn, so the quantity that matters
		// is the one §4.5 specifies: how much of that distribution lands on a
		// class no listed operator can fill. Everything above stays as the
		// fallback for a stand with no weights, which R17 keeps on today's
		// behaviour.
		int wts[6];
		if (mSelectedRamps.size() == 1 && ramp->GetClassWeights(wts))
		{
			int total = 0;
			for (int k = 0; k < 6; ++k) total += wts[k];

			if (total > 0)
			{
				int  fillable        = 0;
				bool any_art_at_all  = false;	// does the LIBRARY have anything at a weighted class?
				bool listed_have_art = false;	// do the LISTED operators, range aside?
				for (int k = 0; k < 6; ++k)
				{
					if (wts[k] == 0) continue;
					char size_class = (char) ('A' + k);

					if (mLiveryIndex.CountAtClass(size_class) > 0) any_art_at_all = true;

					for (set<string>::const_iterator it = codes.begin(); it != codes.end(); ++it)
					{
						vector<const WED_LiveryIndexEntry *> hits;
						mLiveryIndex.GetForAirlineAndClass(*it, size_class, hits);
						if (!hits.empty()) listed_have_art = true;
						// Same range rule as the cards and the flat loop above: a
						// class is only "filled" by an aircraft that can reach the
						// stand. Without this the weighted readout said "100% of the
						// time, from 0 of 2 listed operators" - both halves computed
						// honestly, from different definitions of eligible.
						bool reach = false;
						for (size_t h = 0; h < hits.size() && !reach; ++h)
							reach = LiveryAllowedHere(*hits[h], here) == allow_Yes;
						if (reach) { fillable += wts[k]; break; }
					}
				}
				c.weighted   = true;
				c.p_occupied = (float) fillable / (float) total;

				if (fillable == 0)
					// R14's distinction, and it decides whether the author is
					// being told they made a mistake or told they are early.
					// The third case is the range rule's: the aircraft exist and the
					// operators fly them, they just cannot get here. Saying "no
					// aircraft at size C" for that sends the author to the size
					// slider, the one control that cannot fix it.
					c.empty_cause = !any_art_at_all  ? Coverage::empty_NoArtYet
									: listed_have_art ? Coverage::empty_OutOfRange
													  : Coverage::empty_Unfillable;
			}
			else
			{
				// All six zero is legal and deliberate: the author said nothing
				// parks here. That is NOT the same as an unfillable stand, and
				// the readout must not accuse them of a mistake for it.
				c.weighted    = true;
				c.p_occupied  = 0.0f;
				c.empty_cause = Coverage::empty_ByChoice;
			}
		}

		if (mSelectedRamps.size() == 1)
		{
			c.classes_in_range  = hi - lo + 1;
			c.classes_filled    = filled_classes;
			c.airlines_listed   = (int) codes.size();
			c.airlines_eligible = (int) eligible.size();
			c.lo_class          = (char) ('A' + lo);
			c.hi_class          = (char) ('A' + hi);

			// Variety collapse. The author listed several operators and exactly
			// one of them can ever appear, so this stand parks the same airline
			// every single time. It is NOT the empty case - aircraft do spawn,
			// nothing looks broken - which is why nothing else in this readout
			// would ever mention it.
			if (eligible.size() == 1 && codes.size() > 1)
				c.sole_operator = *eligible.begin();

			// Once a stand is weighted, the size SLIDER is no longer what the
			// sentence should name - the author's weights are. They usually
			// agree (R23 derives the 1301 letter from the weights on export),
			// but a stand weighted for D alone inside a C-E range would
			// otherwise be described as C-E, which is not what will spawn.
			if (c.weighted)
			{
				int w_lo = -1, w_hi = -1;
				for (int k = 0; k < 6; ++k)
					if (wts[k] > 0) { if (w_lo < 0) w_lo = k; w_hi = k; }
				if (w_lo >= 0)
				{
					c.lo_class = (char) ('A' + w_lo);
					c.hi_class = (char) ('A' + w_hi);
				}
			}

			// General aviation never reads its list (R28), and a military stand
			// with nothing listed draws any military livery of its size (§4.1).
			// Both would otherwise read as "no operators listed - parks nothing",
			// which is exactly backwards. Military with a list gets the same
			// country count over the operators that can actually park.
			const int op = ramp->GetRampOperationType();
			c.op_type = op;
			const bool is_ga  = op == ramp_operation_GeneralAviation;
			const bool is_mil = op == ramp_operation_Military;
			if (is_ga || is_mil)
			{
				c.pool_mode = is_ga || codes.empty();
				vector<string> pool_codes;
				if (c.pool_mode) mLiveryIndex.GetAirlineCodes(pool_codes);
				else             pool_codes.assign(codes.begin(), codes.end());

				const bool weighted_here = c.weighted && c.empty_cause != Coverage::empty_ByChoice;
				int fill_w = 0, total_w = 0;
				for (int k = lo; k <= hi; ++k)
				{
					if (weighted_here) { if (wts[k] == 0) continue; total_w += wts[k]; }
					bool class_has = false;
					for (size_t i = 0; i < pool_codes.size(); ++i)
					{
						string uc = pool_codes[i];
						for (size_t n = 0; n < uc.size(); ++n) uc[n] = (char) toupper((unsigned char) uc[n]);
						if (!OperatorMatchesRampOp(uc, op)) continue;

						vector<const WED_LiveryIndexEntry *> hits;
						mLiveryIndex.GetForAirlineAndClass(uc, (char) ('A' + k), hits);
						for (size_t h = 0; h < hits.size(); ++h)
						{
							if (LiveryAllowedHere(*hits[h], here) != allow_Yes) continue;
							class_has = true;
							if (!c.pool_mode) { c.countries.insert(OperatorCountry(uc)); continue; }
							++c.pool_models;
							if (is_ga)
							{
								if (!mAirportCountry.empty() && hits[h]->reg_country == mAirportCountry) ++c.pool_home;
							}
							else
								c.countries.insert(uc == "XPMI" ? string() : OperatorCountry(uc));
						}
					}
					if (class_has && weighted_here) fill_w += wts[k];
				}

				if (c.pool_mode)
				{
					c.sole_operator.clear();		// the list is not what parks here
					if (weighted_here && total_w > 0)
					{
						c.p_occupied  = (float) fill_w / (float) total_w;
						c.empty_cause = fill_w ? Coverage::empty_None : Coverage::empty_Unfillable;
					}
				}
			}
		}
	}

	mCoverage = c;
}

