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
#include "WED_LiveryModeration.h"		// WED_LiveryLegacyUpdateWeights

// Mouse input, and the edits it makes.


void	WED_LiveryPane::ApplyWeightDrag(void)
{
	for (size_t i = 0; i < mSelectedRamps.size(); ++i)
		mSelectedRamps[i]->SetClassWeights(mDragWeights);
	mCoverageDirty = true;
}

void	WED_LiveryPane::AbortWeightDrag(void)
{
	if (mDragWeightBar < 0) return;
	mArchive->AbortCommand();
	mDragWeightBar = -1;
}

// The largest class with a weight, as an index 0-5: the size letter the
// weights stand for (R23). -1 for six zeros, which say nothing about size.
static int	TopWeightedClass(const int w[6])
{
	for (int i = 5; i >= 0; --i)
		if (w[i] > 0) return i;
	return -1;
}

// The ONLY path that gives a stand a 1313 row. Brings back the weights the
// stand already holds if its size letter is still the one they stand for;
// otherwise - a new stand, or one whose letter the author changed in Simple
// Mode (Eric, 2026-09-28) - writes today's step-down from the letter it has now.
void	WED_LiveryPane::SeedWeightsFromSizeRange(void)
{
	if (mSelectedRamps.empty()) return;

	mArchive->StartCommand("Set Weightings");
	for (size_t i = 0; i < mSelectedRamps.size(); ++i)
	{
		WED_RampPosition * r = mSelectedRamps[i];

		// The stand may still hold a distribution from before it was switched to
		// the size range - in this session or in a saved document. Bring it back
		// rather than reseeding over it.
		int stored[6];
		if (r->HasStoredWeights(stored))
		{
			const int top = TopWeightedClass(stored);
			if (top < 0 || IndexToWidthEnum(top) == r->GetWidth())
			{
				r->SetWeightsInUse(true);
				continue;
			}
		}

		// UPDATE from the legacy format: today's step-down, written out as weights
		// (0.75 x 0.25^k from the letter down) - the stand starts exactly where
		// the sim had it, and from here the author tunes it.
		int w[6];
		WED_LiveryLegacyUpdateWeights(r, WED_GetParentAirport(r), w);
		r->SetClassWeights(w);
	}
	mArchive->CommitCommand();

	mCoverageDirty = true;
	Refresh();
}


// Fill this one stand from the database against the sizes it allows NOW - its
// weights, or its size range if it has none - without touching either. Same
// engine and the same extend-only rule as Airport > Auto-Populate.
// Empties the airline list of every selected stand, as one undo step. A stand
// that listed nothing is left alone, so undo only covers what really changed.
void	WED_LiveryPane::ClearAirlines(void)
{
	int n = 0;
	for (size_t i = 0; i < mSelectedRamps.size(); ++i)
		if (!mSelectedRamps[i]->GetAirlines().empty()) ++n;
	if (n == 0)
	{
		mClearFlash = "Nothing to clear";
	}
	else
	{
		mArchive->StartCommand(n == 1 ? "Clear Airlines" : "Clear Airlines of Ramp Starts");
		for (size_t i = 0; i < mSelectedRamps.size(); ++i)
			if (!mSelectedRamps[i]->GetAirlines().empty())
			{
				mSelectedRamps[i]->SetAirlines("");
				mSelectedRamps[i]->MarkLiverySet();
			}
		mArchive->CommitCommand();
		mClearFlash = "Cleared";
	}
	mClearFlashUntil = PaneClockNow() + 2.0;
	mCoverageDirty = true;
	Refresh();
}

void	WED_LiveryPane::PopulateThisRamp(void)
{
	if (mSelectedRamps.empty()) return;
	// Grouped by airport - one plan each - and applied as one undo step.
	std::map<WED_Airport *, vector<WED_RampPosition *> > by_apt;
	for (size_t i = 0; i < mSelectedRamps.size(); ++i)
		if (WED_Airport * a = WED_GetParentAirport(mSelectedRamps[i]))
			by_apt[a].push_back(mSelectedRamps[i]);
	if (by_apt.empty()) return;

	vector<WED_AutoFillPlan> plans;
	WED_AutoFillPlan plan;					// the summary the caption reports
	for (auto & kv : by_apt)
	{
		plans.push_back(WED_PlanLiveryAutoFill(kv.first, &kv.second, false));
		LOG_MSG("I/AutoFill %s", WED_DescribeAutoFill(plans.back()).c_str());
		if (!plans.back().error.empty() && plan.error.empty()) plan.error = plans.back().error;
		plan.changed += plans.back().changed;
		plan.ramps.insert(plan.ramps.end(), plans.back().ramps.begin(), plans.back().ramps.end());
	}

	if (!plan.error.empty())
	{
		mPopulateFlash = "Can't populate";
		mPopulateDetail = plan.error;
	}
	else if (plan.changed == 0)
	{
		mPopulateFlash = "Nothing to add";
		mPopulateDetail = plan.ramps.size() == 1 ? "Nothing added: " + plan.ramps[0].skipped + "." : "Nothing to add to any of them.";
	}
	else
	{
		mArchive->StartCommand(plans.size() == 1 && plan.ramps.size() == 1 ? "Populate This Ramp" : "Populate Ramps");
		for (size_t i = 0; i < plans.size(); ++i) WED_ApplyLiveryAutoFill(plans[i], false);
		mArchive->CommitCommand();

		std::set<string> codes;
		size_t n_added = 0;
		for (size_t i = 0; i < plan.ramps.size(); ++i)
		{
			n_added += plan.ramps[i].added.size();
			codes.insert(plan.ramps[i].added.begin(), plan.ramps[i].added.end());
		}
		char buf[48];
		if (plan.ramps.size() == 1) snprintf(buf, sizeof(buf), "Added %d", (int) n_added);
		else                        snprintf(buf, sizeof(buf), "%d of %d changed", plan.changed, (int) plan.ramps.size());
		mPopulateFlash = buf;
		mPopulateDetail = plan.ramps.size() == 1 ? "Added:" : "Added across them:";
		for (auto & c : codes) mPopulateDetail += " " + c;
	#if APL
	mPopulateDetail += ". Cmd+Z reverts it.";
#else
	mPopulateDetail += ". Ctrl+Z reverts it.";
#endif
		mCoverageDirty = true;
	}
	mPopulateFlashUntil = PaneClockNow() + 3.0;
	Refresh();
}

void	WED_LiveryPane::SetRampOpFilter(int wed_ramp_op_enum)
{
	if (mSelectedRamps.empty()) return;

	mArchive->StartCommand("Set Ramp Operation Type");
	for (size_t i = 0; i < mSelectedRamps.size(); ++i)
	{
		mSelectedRamps[i]->SetRampOperationType(wed_ramp_op_enum);
		mSelectedRamps[i]->MarkLiverySet();
	}
	mArchive->CommitCommand();

	mCoverageDirty = true;
	Refresh();
}

// The slider is the LEGACY stand's control (a stand with weights shows the bars instead), and dragging it
// changes only the size letter: the stand stays legacy, nothing is written as 1313, no M mark (Eric,
// 2026-09-30). It used to convert the stand to weights on the first pixel of a drag (2026-09-29), which
// swapped the slider for the weight bars under the cursor mid-drag, and turned any touch of a ball into an
// author-owned 1313 stand the export upgrade would never re-plan. Converting is Set Weightings' job, or the
// 12.5 export's. The lower end stays at A: a legacy stand's step-down always runs there.
void	WED_LiveryPane::ApplyDragRange(void)
{
	if (mSelectedRamps.empty()) return;

	// Whichever of anchor/current is smaller is min, larger is max - recomputed
	// fresh every call, so dragging the "moving" ball straight past the anchor
	// and out the other side just flips which one is which, automatically and
	// without a special case.
	int lo = (mDragAnchorIndex < mDragCurrentIndex) ? mDragAnchorIndex : mDragCurrentIndex;
	int hi = (mDragAnchorIndex > mDragCurrentIndex) ? mDragAnchorIndex : mDragCurrentIndex;

	for (size_t i = 0; i < mSelectedRamps.size(); ++i)
	{
		WED_RampPosition * r = mSelectedRamps[i];
		int w[6];
		if (!r->GetClassWeights(w))
		{
			r->SetWidth(IndexToWidthEnum(hi));
			continue;
		}
		r->SetWidthMin(IndexToWidthEnum(lo));
		r->SetWidth(IndexToWidthEnum(hi));
		WED_LiveryRangeUpdateWeights(r, WED_GetParentAirport(r), lo, hi, w);
		r->SetClassWeights(w);
		r->MarkLiverySet();
	}

	// Live during the drag, not just on mouse-up: watching the covered-class
	// count fall as you narrow the range is the whole point of the readout.
	mCoverageDirty = true;
}

// Only vertical (axis 0) scrolling means anything for this single-column list.
// A few rows per notch, same "small multiple of one line" feel as most native
// scrollable lists. The upper bound on mScrollOffset is enforced in Draw() (it
// needs the current row count/bounds, which this function doesn't have) - a
// value briefly larger than the real max is harmless, never used to position
// anything until Draw() re-clamps it first.
int		WED_LiveryPane::ScrollWheel(int x, int y, int dist, int axis)
{
	if (axis != 0) return 0;

	// ONLY THE CARD LIST SCROLLS, and only when the cursor is actually over it.
	// This pane answered the wheel anywhere in the tab, so rolling over the size
	// slider or the weight bars - controls with nothing scrollable about them -
	// moved the list underneath instead of doing nothing, and the whole tab felt
	// like one scrolling surface when only its bottom section is one.
	int b[4];  GetBounds(b);
	if ((float) y > ContentTop(b) || y < b[1]) return 0;

	float line_h = GUI_GetLineHeight(font_UI_Basic);
	float row_h  = line_h + 4;

	// CLAMPED HERE, not only in Draw(). Draw() owning the clamp alone meant every
	// wheel notch past the end still wrote an out-of-range offset and asked for a
	// repaint, which Draw then undid - the log showed a hundred "clamp 54 -> 0" in
	// a list whose content (602px) is shorter than its viewport (636px), i.e. one
	// that cannot scroll at all. That is the jerk: a repaint per notch, changing
	// nothing.
	float content_h = 0.0f;
	if (!mSelectedRamps.empty())
	{
		EnsureRows();

		vector<float> tray_h;  TrayHeights(mRowIcaos, tray_h);
		vector<RowSlot> slots;
		content_h = LayoutRows(b, mRowIsCard, tray_h, slots);
	}

	// ONE SCROLL, TWO PARTS (Dellanie, 2026-09-30: on a 1080p screen the list got
	// a sliver under the flag, size controls and readout). Down: the top of the
	// tab rolls away first, then the list moves. Up: the list returns to its
	// start first, then the top rolls back in. The top rolls only as far as the
	// list needs - a list that already fits leaves the tab as it is.
	const float page_before = mPageScroll, list_before = mScrollOffset;
	float step = -dist * row_h * 3;			// > 0 = further down
	if (step > 0)
	{
		const float overflow = content_h - (ContentTop(b) - (float) b[1]) - mScrollOffset;
		const float room     = PageScrollMax(b) - mPageScroll;
		const float to_page  = (std::min)(step, (std::min)(room, overflow));
		if (to_page > 0) { mPageScroll += to_page; step -= to_page; }

		// the list is taller now by what the top gave up, so measure again
		float list_max = content_h - (ContentTop(b) - (float) b[1]);
		if (list_max < 0) list_max = 0;
		if (mScrollOffset < list_max) mScrollOffset = (std::min)(mScrollOffset + step, list_max);
	}
	else
	{
		float up = -step;
		const float from_list = (std::min)(up, mScrollOffset);
		mScrollOffset -= from_list;  up -= from_list;
		if (up > 0) mPageScroll = (std::max)(0.0f, mPageScroll - up);
	}

	if (mPageScroll == page_before && mScrollOffset == list_before)
		return 0;							// nowhere to go - let whoever is behind us have it
	Refresh();
	return 1;
}

// Rolls back an in-flight size-slider drag, if there is one. Safe to call when
// there isn't - that is the point, so callers don't have to know.
// How many of the selected ramps carry `icao`. The caller turns this into the
// checkbox's three states: none of them, all of them, or somewhere in between.
int		WED_LiveryPane::CountRampsWithCode(const string & icao) const
{
	int n = 0;
	for (size_t i = 0; i < mSelectedRamps.size(); ++i)
		if (ParseCodes(mSelectedRamps[i]->GetAirlines()).count(icao))
			++n;
	return n;
}

void	WED_LiveryPane::AbortSizeDrag(void)
{
	if (mDragHandle < 0) return;

	mArchive->AbortCommand();
	mDragHandle = -1;
	mDragAnchorIndex = -1;
	mDragCurrentIndex = -1;
}

void	WED_LiveryPane::ToggleCode(const string & icao)
{
	if (mSelectedRamps.empty()) return;

	// The checkbox is tri-state across a multi-selection, and the transition rule
	// is what keeps it safe: only a box that is solid for EVERY selected ramp
	// clears. Mixed and empty both fill. So a click can never remove a code the
	// user was not shown as set - which is what the old "read ramp 0, write all"
	// version did, silently deleting airlines from ramps whose box was drawn
	// unchecked the whole time.
	//
	// Consequence, and it is the intended one: clicking a mixed box UNIFIES the
	// selection. That can rewrite hundreds of ramps at once, which is why it all
	// happens inside a single command - one Ctrl+Z puts every one of them back.
	const int n_with = CountRampsWithCode(icao);
	const bool clear_all = (n_with == (int) mSelectedRamps.size());

	// The Gateway refuses an airline string of 100 characters or more (validator,
	// R10). A code that would push a stand past it is not added there - whole
	// codes only, never a truncated one - and the button line says so.
	const size_t kGatewayMaxChars = kGatewayAirlinesMaxChars;
	int refused = 0;
	mArchive->StartCommand("Set Ramp Start Airlines");
	for (size_t i = 0; i < mSelectedRamps.size(); ++i)
	{
		set<string> codes = ParseCodes(mSelectedRamps[i]->GetAirlines());
		if (clear_all)	codes.erase(icao);
		else			codes.insert(icao);
		const string next = WED_RampPosition::CorrectAirlinesString(CodesToString(codes));
		if (!clear_all && gExportTarget == wet_gateway && next.size() > kGatewayMaxChars) { ++refused; continue; }
		mSelectedRamps[i]->SetAirlines(next);
		mSelectedRamps[i]->MarkLiverySet();
	}
	mArchive->CommitCommand();
	if (refused)
	{
		string lc = icao;
		for (size_t c = 0; c < lc.size(); ++c) lc[c] = (char) tolower((unsigned char) lc[c]);
		mPopulateFlash = refused == 1 ? "Gateway limit: " + lc + " not added"
									  : "Gateway limit: " + lc + " not added on " + std::to_string(refused) + " stands";
		mPopulateDetail = "The Gateway takes at most " + std::to_string(kGatewayMaxChars) + " characters of airline codes per stand.";
		mPopulateFlashUntil = PaneClockNow() + 4.0;
	}

	// This is the edit the readout exists for: ticking an operator off is the
	// cheapest way to empty a stand without noticing.
	mCoverageDirty = true;
	Refresh();
}

// ---------------------------------------------------------------------------------------------
// input
// ---------------------------------------------------------------------------------------------

int		WED_LiveryPane::MouseMove(int x, int y)
{
	int b[4];
	GetBounds(b);

	bool changed = false;

	int chip = mSelectedRamps.empty() ? -1 : FilterChipForXY(b, x, y);
	if (chip != mHoverFilterChip)		{ mHoverFilterChip = chip;		changed = true; }

	int handle = mSelectedRamps.empty() ? -1 : SliderHandleForXY(b, x, y);
	if (handle != mHoverSliderHandle)	{ mHoverSliderHandle = handle;	changed = true; }

	bool over_sort = false, over_recommend = false;
	if (!mSelectedRamps.empty())
	{
		float r[4];
		SortButtonRect(b, r);
		over_sort = (x >= r[0] && x <= r[2] && y >= r[1] && y <= r[3]);
		RecommendButtonRect(b, r);
		over_recommend = (x >= r[0] && x <= r[2] && y >= r[1] && y <= r[3])
			&& AirportIsCommercial(AirportDb(), mCurrentAirportIcao);	// hovering a disabled button doesn't count
	}
	if (over_sort != mHoverSortButton)			{ mHoverSortButton = over_sort;			changed = true; }
	if (over_recommend != mHoverRecommendButton)	{ mHoverRecommendButton = over_recommend;	changed = true; }

	bool over_clear = false;
	if (!mSelectedRamps.empty() && !mSearchQuery.empty())
	{
		float r[4];
		SearchClearButtonRect(b, r);
		over_clear = (x >= r[0] && x <= r[2] && y >= r[1] && y <= r[3]);
	}
	if (over_clear != mHoverClearButton)			{ mHoverClearButton = over_clear;			changed = true; }

	int hover_wbar = mSelectedRamps.empty() ? -1 : WeightBarForXY(b, x, y);
	if (hover_wbar != mHoverWeightBar)	{ mHoverWeightBar = hover_wbar;	changed = true; }

	bool over_wbtn = false;
	if (!mSelectedRamps.empty())
	{
		float wb[4];
		WeightButtonRect(b, wb);
		over_wbtn = (x >= wb[0] && x <= wb[2] && y >= wb[1] && y <= wb[3]);
	}
	if (over_wbtn != mHoverWeightButton)	{ mHoverWeightButton = over_wbtn;	changed = true; }

	bool over_cov = CoverageToggleHit(b, x, y);
	if (over_cov != mHoverCoverageToggle)	{ mHoverCoverageToggle = over_cov;	changed = true; }

	bool over_pop = false;
	if (!mSelectedRamps.empty())
	{
		float pb[4];
		PopulateButtonRect(b, pb);
		over_pop = (x >= pb[0] && x <= pb[2] && y >= pb[1] && y <= pb[3]);
	}
	if (over_pop != mHoverPopulate)		{ mHoverPopulate = over_pop;		changed = true; }

	bool over_clr = false;
	if (!mSelectedRamps.empty())
	{
		float cb[4];
		ClearButtonRect(b, cb);
		over_clr = (x >= cb[0] && x <= cb[2] && y >= cb[1] && y <= cb[3]);
	}
	if (over_clr != mHoverClearAirlines)	{ mHoverClearAirlines = over_clr;	changed = true; }

	int row = -1;
	if (!mSelectedRamps.empty() && !over_sort && !over_recommend && !over_clear)
	{
		EnsureRows();
		vector<float> tray_h;  TrayHeights(mRowIcaos, tray_h);
		int r = RowForXY(b, mRowIsCard, tray_h, x, y);
		row = (r >= 0 && r < (int) mRows.size() && mRows[r].kind == wed_Row_Airline) ? r : -1;

		// Hovering a card starts it cycling; leaving stops it and drops the card
		// back to index 0. Keyed by icao so the cycle survives the row list being
		// rebuilt underneath it, which happens on this very call.
		mHoverX = x; mHoverY = y;

		vector<RowSlot> hover_slots;
		LayoutRows(b, mRowIsCard, tray_h, hover_slots);
		int tray_row = TrayRowForXY(mRowIcaos, hover_slots, x, y);
		if (tray_row != mTrayHoverIdx)
		{
			// Leaving a tray row resumes the sequence FROM that aircraft, so
			// stopping to look at one does not cost you your place.
			if (tray_row < 0 && mTrayHoverIdx >= 0) { mCycleShow = mTrayHoverIdx; mCycleAccum = 0.0f; }
			mTrayHoverIdx = tray_row;
			changed = true;
		}
		string want = (row >= 0) ? mRows[row].icao : string();
		if (want != mCycleAirline)
		{
			mCycleAirline = want;
			mCycleShow    = 0;
			mCycleAccum   = 0.0f;
			mCycleFade    = kCycleFadeSec;		// a new card starts on its face, no fade from the old one
			changed       = true;
			Refresh();
		}
	}
	else if (!mCycleAirline.empty())
	{
		mCycleAirline.clear(); mCycleShow = 0; mCycleAccum = 0.0f;
		changed = true;
	}
	if (row != mHoverRow)				{ mHoverRow = row;				changed = true; }

	if (changed) Refresh();
	return 0;
}

int		WED_LiveryPane::MouseDown(int x, int y, int button)
{
	if (mSelectedRamps.empty())
		return 1;			// masked - swallow the click, do nothing

	// A second button pressed during a drag would open a second command inside
	// the first (asserts); the drag in progress owns the mouse until it ends.
	if (mDragWeightBar >= 0 || mDragHandle >= 0)
		return 1;

	int b[4];
	GetBounds(b);

	// Reaching this function at all means the click did NOT land on the
	// search field while it was visible - GUI_Pane's child dispatch already
	// gives a visible child first refusal on clicks (see GUI_Pane::
	// InternalMouseDown), so a click that WAS meant for an actively-focused,
	// on-screen field never gets here. Anything that does reach us is
	// therefore "somewhere else" by definition - drop the field's focus so
	// it collapses back to the plain placeholder as soon as this frame's
	// Draw() runs (see the empty-and-unfocused check there). Guarded by
	// IsFocused() because GUI_Commander::LoseFocus() doesn't check that
	// itself - calling it on a commander that ISN'T actually focused would
	// incorrectly go steal focus away from whatever else legitimately has it.
	if (mSearchField->IsFocused())
		mSearchField->LoseFocus(1);

	if (!mSearchQuery.empty())
	{
		float clear_r[4];
		SearchClearButtonRect(b, clear_r);
		if (x >= clear_r[0] && x <= clear_r[2] && y >= clear_r[1] && y <= clear_r[3])
		{
			mTrackClearButton = true;
			return 1;
		}
	}

	int chip = FilterChipForXY(b, x, y);
	if (chip >= 0)
	{
		mTrackFilterChip = chip;
		return 1;
	}

	if (CoverageToggleHit(b, x, y))
	{
		sCoverageExpanded = !sCoverageExpanded;
		Refresh();
		return 1;
	}

	if (!mSelectedRamps.empty())
	{
		float pb[4];
		PopulateButtonRect(b, pb);
		if (x >= pb[0] && x <= pb[2] && y >= pb[1] && y <= pb[3])
		{
			mTrackPopulate = true;
			Refresh();
			return 1;
		}
		float cb[4];
		ClearButtonRect(b, cb);
		if (x >= cb[0] && x <= cb[2] && y >= cb[1] && y <= cb[3])
		{
			mTrackClearAirlines = true;
			Refresh();
			return 1;
		}
	}

	// The add/clear button, and the bars, both before the slider - the button
	// overlaps the slider's row, and a press on a bar must not be read as a
	// press on anything underneath it.
	{
		float wb[4];
		WeightButtonRect(b, wb);
		if (x >= wb[0] && x <= wb[2] && y >= wb[1] && y <= wb[3])
		{
			mTrackWeightButton = true;
			Refresh();
			return 1;
		}
	}

	int wbar = WeightBarForXY(b, x, y);
	if (wbar >= 0)
	{
		int w[6];
		if (!SelectionWeights(w))
		{
			// Mixed selection: the first drag unifies it, which is the rule the
			// tri-state operator checkbox already uses. Start from the first
			// ramp's own weights so the gesture has somewhere to stand.
			// From the first stand that HAS weights - a legacy stand first in the
			// selection used to start the gesture from six zeros.
			bool found = false;
			for (size_t k = 0; k < mSelectedRamps.size() && !found; ++k)
				found = mSelectedRamps[k]->GetClassWeights(w);
			if (!found)
				for (int k = 0; k < 6; ++k) w[k] = 0;
		}
		memcpy(mDragWeights,  w, sizeof(w));
		memcpy(mDragWeights0, w, sizeof(w));	// what MouseUp compares against

		mDragTrackMax  = WeightTrackMax();		// before mDragWeightBar is set: the live scale
		mDragWeightBar = wbar;
		mArchive->StartCommand("Set Weightings");

		mDragWeights[wbar] = WeightValueForY(b, y);
		ApplyWeightDrag();
		Refresh();
		return 1;
	}

	// A stand carrying weights has its size derived from them (R23), so the
	// slider is a readout, not a control. Refusing the hit here is the other
	// half of drawing it greyed - a visual-only disable that still responds to
	// clicks is exactly the bug that two-part idiom exists to prevent.
	int handle = ShowWeightBars() ? -1 : SliderHandleForXY(b, x, y);
	if (handle >= 0)
	{
		// Two balls: blue is the top of the range (the 1301 letter), orange its bottom. The one not grabbed
		// is the anchor. A press on the track away from both moves the nearer one there.
		int lo, hi;
		SliderRange(lo, hi);
		int idx = (int) (SliderContinuousIndexForX(b, x) + 0.5f);
		if (idx < 0) idx = 0;
		if (idx > 5) idx = 5;
		bool grab_min = (handle == 2) || (handle == 0 && (idx < lo || (idx < hi && idx - lo < hi - idx)));
		// Legacy: only the letter moves. The orange ball is pinned to A, and a press
		// on the track moves the letter there.
		if (!SelectionHasWeights())
		{
			if (handle == 2) { Refresh(); return 1; }
			grab_min = false;
			lo = 0;
		}

		mDragAnchorIndex  = grab_min ? hi : lo;
		mDragCurrentIndex = grab_min ? lo : hi;
		mDragStartIndex   = mDragCurrentIndex;
		mDragHandle       = grab_min ? 0 : 1;

		mArchive->StartCommand("Set Ramp Start Size Range");

		if (handle == 0 && idx != mDragCurrentIndex)
		{
			mDragCurrentIndex = idx;
			ApplyDragRange();
		}
		Refresh();
		return 1;
	}

	float sort_r[4];
	SortButtonRect(b, sort_r);
	if (x >= sort_r[0] && x <= sort_r[2] && y >= sort_r[1] && y <= sort_r[3])
	{
		mTrackSortButton = true;
		return 1;
	}

	float rec_r[4];
	RecommendButtonRect(b, rec_r);
	if (x >= rec_r[0] && x <= rec_r[2] && y >= rec_r[1] && y <= rec_r[3]
		&& AirportIsCommercial(AirportDb(), mCurrentAirportIcao))
	{
		mTrackRecommendButton = true;
		return 1;
	}

	// Only reachable when the search field is currently HIDDEN (placeholder
	// showing) - GUI_Pane's own child dispatch gives the real field first
	// refusal on this click whenever it's visible, so this code never fires
	// then. Reveal it and focus it; the click that revealed it doesn't also
	// place a caret - the user's very next click/keystroke does that.
	float search_r[4];
	SearchFieldRect(b, search_r);
	if (x >= search_r[0] && x <= search_r[2] && y >= search_r[1] && y <= search_r[3])
	{
		mSearchField->Show();
		mSearchField->TakeFocus();
		Refresh();
		return 1;
	}

	// A press anywhere in the content area arms BOTH possible outcomes: a card
	// toggle (if the cursor barely moves before release) and a drag-scroll (if it
	// does). MouseUp decides which actually happened - see its slop check.
	//
	// The whole content scrolls this way now, not just a card block at the top:
	// cards ARE the rows, so restricting the gesture to "the cards" would have
	// meant restricting it to most of the list and then stopping arbitrarily at a
	// section header.
	EnsureRows();
	vector<float> tray_h;  TrayHeights(mRowIcaos, tray_h);

	if (y <= ContentTop(b))
	{
		mContentDragStartY = y;
		mContentDragStartX = x;
		mContentDragStartOffset = mScrollOffset;
	}

	int row = RowForXY(b, mRowIsCard, tray_h, x, y);
	mTrackRow = (row >= 0 && row < (int) mRows.size() &&
				(mRows[row].kind == wed_Row_Airline || mRows[row].kind == wed_Row_Header ||
				 mRows[row].kind == wed_Row_PoolClass || mRows[row].kind == wed_Row_PoolItem)) ? row : -1;
	Refresh();
	return 1;
}

void	WED_LiveryPane::MouseDrag(int x, int y, int button)
{
	// Drag-scroll: the content follows the cursor ("grab and pull"), so dragging
	// downward reveals earlier content. Y increases UPWARD in this pane's coordinate
	// space, so a downward drag makes (y - start) negative, which decreases the
	// offset - exactly the direction we want. Derived from the gesture's TOTAL
	// movement, never accumulated per-move, so it can't drift. The upper clamp is
	// Draw()'s job (it's the one that knows the current content height).
	if (mContentDragStartY >= 0)
	{
		// NOTHING MOVES UNTIL THE GESTURE CLEARS THE SLOP. A click almost always
		// drifts a pixel or two, and scrolling by that much slides the content out
		// from under the cursor between press and release - which is why the tray
		// could not be opened: its bar is 14px tall, so a 2px shift was enough for
		// the release to land on the card body instead and be read as a tick. The
		// card body is 90px tall, so ticking kept working and hid the cause.
		const int kDragSlop = 3;
		if (abs(y - mContentDragStartY) <= kDragSlop &&
			(mContentDragStartX < 0 || abs(x - mContentDragStartX) <= kDragSlop))
			return;

		mScrollOffset = mContentDragStartOffset + (float) (y - mContentDragStartY);
		if (mScrollOffset < 0) mScrollOffset = 0;
		Refresh();
		return;
	}

	// The gesture is locked to the bar it started on. Sliding sideways does NOT
	// paint across the neighbours: an author correcting one class should not
	// discover they have flattened the other five.
	if (mDragWeightBar >= 0)
	{
		int bw[4];
		GetBounds(bw);
		int v = WeightValueForY(bw, y);
		if (v != mDragWeights[mDragWeightBar])
		{
			mDragWeights[mDragWeightBar] = v;
			ApplyWeightDrag();
			Refresh();
		}
		return;
	}

	if (mDragHandle < 0) return;

	int b[4];
	GetBounds(b);
	float idx_f = SliderContinuousIndexForX(b, x);

	const float kDetent = 0.1f;		// 10% of one grid interval, either side

	// Detent-snap the grabbed ball's own position - completely independent of
	// where the anchor ball sits, so dragging straight through it and beyond
	// requires no special handling at all.
	int cur = mDragCurrentIndex;
	int new_cur = cur;
	if (cur < 5 && fabs((double)(idx_f - (cur + 1))) <= kDetent)
		new_cur = cur + 1;
	else if (cur > 0 && fabs((double)(idx_f - (cur - 1))) <= kDetent)
		new_cur = cur - 1;
	else if (fabs((double)(idx_f - cur)) > 1.5)
		new_cur = (int) (idx_f + 0.5f);		// fast/discontinuous mouse motion - jump straight there

	if (new_cur == cur) return;

	mDragCurrentIndex = new_cur;

	// Which side is "current" acting as now - drives only the hover/highlight
	// ring; ApplyDragRange() re-derives real min/max from anchor/current itself.
	if (mDragCurrentIndex == mDragAnchorIndex)		mDragHandle = 2;
	else if (mDragCurrentIndex < mDragAnchorIndex)	mDragHandle = 0;
	else											mDragHandle = 1;

	ApplyDragRange();
	Refresh();
}

void	WED_LiveryPane::MouseUp(int x, int y, int button)
{
	int b[4];
	GetBounds(b);

	if (mTrackClearAirlines)
	{
		mTrackClearAirlines = false;
		float cb[4];
		ClearButtonRect(b, cb);
		if (x >= cb[0] && x <= cb[2] && y >= cb[1] && y <= cb[3])
			ClearAirlines();
		Refresh();
		return;
	}

	if (mTrackPopulate)
	{
		mTrackPopulate = false;
		float pb[4];
		PopulateButtonRect(b, pb);
		if (x >= pb[0] && x <= pb[2] && y >= pb[1] && y <= pb[3])
			PopulateThisRamp();
		Refresh();
		return;
	}

	if (mTrackWeightButton)
	{
		mTrackWeightButton = false;
		float wb[4];
		WeightButtonRect(b, wb);
		if (x >= wb[0] && x <= wb[2] && y >= wb[1] && y <= wb[3])
		{
			// Simple Mode and back only change the view. A legacy stand in the selection is updated to weights
			// first (the step-down it parks today), which keeps the stands that already have them.
			if (ShowWeightBars())				mSimpleView = true;
			else
			{
				if (!SelectionAllWeights())		SeedWeightsFromSizeRange();
				mSimpleView = false;
			}
		}
		Refresh();
		return;
	}

	if (mDragWeightBar >= 0)
	{
		// Commit only if something moved. A click that lands on a bar's
		// existing height is a no-op, and a no-op has no business on the undo
		// stack - the same call the map's handle tool makes when a drag turns
		// out to have moved zero pixels.
		if (memcmp(mDragWeights, mDragWeights0, sizeof(mDragWeights)) == 0)
			mArchive->AbortCommand();
		else
			mArchive->CommitCommand();

		mDragWeightBar = -1;
		mCoverageDirty = true;
		Refresh();
		return;
	}

	// A press in the content area arms a drag-scroll, and this used to swallow the
	// release unconditionally - which quietly disabled EVERY click in the list once
	// the cards became the list: section headers would not expand, the lock badge
	// and tray tab did nothing, and operators could not be ticked. Only consume the
	// release when the cursor actually travelled; otherwise it was a click, and the
	// handling below is what it was for.
	if (mContentDragStartY >= 0)
	{
		const int kDragSlop = 3;
		bool moved = (abs(y - mContentDragStartY) > kDragSlop) ||
					 (mContentDragStartX >= 0 && abs(x - mContentDragStartX) > kDragSlop);
		mContentDragStartY = -1;	// scrolling is view state, not document state - nothing to commit
		mContentDragStartX = -1;
		if (moved)
		{
			Refresh();
			return;
		}
	}

	if (mDragHandle >= 0)
	{
		// A press that ends where it began changed nothing - no undo entry.
		if (mDragCurrentIndex == mDragStartIndex)	mArchive->AbortCommand();
		else										mArchive->CommitCommand();
		mDragHandle = -1;
		mDragAnchorIndex = -1;
		mDragCurrentIndex = -1;
		Refresh();
		return;
	}

	if (mTrackFilterChip >= 0)
	{
		if (FilterChipForXY(b, x, y) == mTrackFilterChip)
			SetRampOpFilter(kFilterEnumTable[mTrackFilterChip]);
		mTrackFilterChip = -1;
		return;
	}

	if (mTrackSortButton)
	{
		float r[4];
		SortButtonRect(b, r);
		if (x >= r[0] && x <= r[2] && y >= r[1] && y <= r[3])
			mSortDescending = !mSortDescending;
			SetRowsDirty();
		mTrackSortButton = false;
		Refresh();
		return;
	}

	if (mTrackRecommendButton)
	{
		float r[4];
		RecommendButtonRect(b, r);
		if (x >= r[0] && x <= r[2] && y >= r[1] && y <= r[3]
			&& AirportIsCommercial(AirportDb(), mCurrentAirportIcao))
		{
			gShowLiveryRecommendation = !gShowLiveryRecommendation;
			SetRowsDirty();
		}
		mTrackRecommendButton = false;
		Refresh();
		return;
	}

	if (mTrackClearButton)
	{
		float r[4];
		SearchClearButtonRect(b, r);
		if (x >= r[0] && x <= r[2] && y >= r[1] && y <= r[3] && !mSearchQuery.empty())
		{
			// Same DoReplaceText(0,len,NULL,NULL) path a manual select-all-then-
			// delete would take - it flows through GUI_TextField::ReplaceText(),
			// which broadcasts GUI_TEXT_FIELD_TEXT_CHANGED for us, so
			// ReceiveMessage() re-syncs mSearchQuery to "" the normal way rather
			// than us poking it directly here.
			mSearchField->DoReplaceText(0, (int) mSearchQuery.size(), NULL, NULL);
			if (mSearchField->IsFocused())
				mSearchField->LoseFocus(1);		// "exit search mode" - collapses to the placeholder next Draw()
		}
		mTrackClearButton = false;
		Refresh();
		return;
	}

	if (mSelectedRamps.empty() || mTrackRow < 0)
	{
		mTrackRow = -1;
		return;
	}

	EnsureRows();
	vector<float> tray_h;  TrayHeights(mRowIcaos, tray_h);

	// Pool table: a class group opens and shuts; a type opens to its liveries.
	if (RowForXY(b, mRowIsCard, tray_h, x, y) == mTrackRow &&
		mTrackRow >= 0 && mTrackRow < (int) mRows.size() &&
		(mRows[mTrackRow].kind == wed_Row_PoolClass || mRows[mTrackRow].kind == wed_Row_PoolItem))
	{
		const WED_LiveryDisplayRow & r = mRows[mTrackRow];
		if (r.kind == wed_Row_PoolClass)
			mPoolClassOpen[r.hidden_count] = !PoolClassIsOpen(r.hidden_count);
		else if (mPoolExpanded.count(r.icao)) mPoolExpanded.erase(r.icao);
		else                                  mPoolExpanded.insert(r.icao);
		SetRowsDirty();
		mTrackRow = -1;
		Refresh();
		return;
	}

	if (RowForXY(b, mRowIsCard, tray_h, x, y) == mTrackRow &&
		mTrackRow >= 0 && mTrackRow < (int) mRows.size() && mRows[mTrackRow].kind == wed_Row_Header)
	{
		const string & h = mRows[mTrackRow].header_text;
		if (!h.empty() && h != "Selected")		// see the draw side - not a collapsible tier
		{
			if (mCollapsedSections.count(h)) mCollapsedSections.erase(h);
			else                             mCollapsedSections.insert(h);
			SetRowsDirty();
		}
		mTrackRow = -1;
		Refresh();
		return;
	}

	// The lock badge and the tray tab sit ON the card, so they are tested first -
	// otherwise either one would also toggle the operator underneath it.
	if (RowForXY(b, mRowIsCard, tray_h, x, y) == mTrackRow &&
		mTrackRow >= 0 && mTrackRow < (int) mRows.size() && mRows[mTrackRow].kind == wed_Row_Airline)
	{
		vector<RowSlot> slots;
		LayoutRows(b, mRowIsCard, tray_h, slots);
		const string & icao = mRows[mTrackRow].icao;
		const AirlineCard * tc = CardFor(icao);
		size_t ac_livery_count = tc ? tc->abs_paths.size() : 0;
		float lr[4], tr[4];
		LockIconRect(slots[mTrackRow], lr);
		TrayTabRect (slots[mTrackRow], tr);

		LOG_MSG("I/LiveryClick %s at (%d,%d)  tray=[%.0f..%.0f x %.0f..%.0f] hit=%d  lock=%d\n",
				icao.c_str(), x, y, tr[0], tr[2], tr[1], tr[3],
				(int)(x >= tr[0] && x <= tr[2] && y >= tr[1] && y <= tr[3]),
				(int)(x >= lr[0] && x <= lr[2] && y >= lr[1] && y <= lr[3]));

		if (x >= lr[0] && x <= lr[2] && y >= lr[1] && y <= lr[3])
		{
			if (tc && tc->preview) { mTrackRow = -1; return; }	// pool cards are previews - no lock
			// Exclusive by construction: holding the lock is a single string, so
			// taking it necessarily releases whoever had it.
			mLockedAirline = (mLockedAirline == icao) ? string() : icao;
			mTrackRow = -1;
			Refresh();
			return;
		}
		// THE WHOLE CAPTION ROW IS THE TRAY'S, not just the arrow's gutter, and the
		// picture above it is the only place that selects. Two zones that touch but
		// never overlap: a click either changes what parks here or asks what else
		// this operator has, and there is no pixel where it might do either.
		if (x >= tr[0] && x <= tr[2] && y >= tr[1] && y <= tr[3])
		{
			// A single-livery card has no tray, but its caption row still is not a
			// select target - otherwise the same strip would mean one thing on one
			// card and something else on its neighbour.
			if (ac_livery_count < 2) { mTrackRow = -1; Refresh(); return; }

			if (mTrayAirline == icao)
			{
				mTrayClosing = mTrayAirline;  mTrayClosingOpen = mTrayOpen;
				mTrayAirline.clear();         mTrayOpen = 0.0f;
			}
			else
			{
				// The one already open starts retracting from wherever it is, so
				// the two animations cross over instead of one snapping shut.
				if (!mTrayAirline.empty()) { mTrayClosing = mTrayAirline; mTrayClosingOpen = mTrayOpen; }
				mTrayAirline = icao;  mTrayOpen = 0.0f;
			}
			mTrackRow = -1;
			Refresh();
			return;
		}
	}

	if (RowForXY(b, mRowIsCard, tray_h, x, y) == mTrackRow && mTrackRow < (int) mRows.size() && mRows[mTrackRow].kind == wed_Row_Airline
		&& !(CardFor(mRows[mTrackRow].icao) && CardFor(mRows[mTrackRow].icao)->preview))	// pool previews are not a picker
	{
		// SCROLL ANCHORING. Ticking a card can create or grow the "Selected"
		// section ABOVE the viewport, and every row below it then slides down by
		// that much - so the card you just clicked walks out from under the cursor
		// and the whole page appears to jump. The height change is real and wanted;
		// what is not wanted is the viewport staying still while the content moves
		// past it.
		//
		// So the clicked card is the anchor: remember where it sits on screen, let
		// the list change, then shift the scroll offset by however far that same
		// card moved. It ends up exactly where it was, whatever happened above it.
		vector<RowSlot> before;
		LayoutRows(b, mRowIsCard, tray_h, before);
		const string anchor_icao = mRows[mTrackRow].icao;
		const float  anchor_y    = before[mTrackRow].top;

		ToggleCode(anchor_icao);
		SetRowsDirty();

		EnsureRows();

		vector<float> tray2;  TrayHeights(mRowIcaos, tray2);
		vector<RowSlot> after;
		LayoutRows(b, mRowIsCard, tray2, after);

		// The LAST match, not the first: ticking mirrors a copy into "Selected" at
		// the top, and the card the user actually clicked is the original further
		// down. Anchoring on the copy would jump the list to the top instead.
		int found = -1;
		for (size_t i = 0; i < mRows.size(); ++i)
			if (mRows[i].kind == wed_Row_Airline && mRows[i].icao == anchor_icao) found = (int) i;

		if (found >= 0)
		{
			mScrollOffset += anchor_y - after[found].top;
			if (mScrollOffset < 0) mScrollOffset = 0;	// Draw() owns the upper clamp
		}
	}

	mTrackRow = -1;
}
