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

// Where everything is: section heights, rectangles, hit tests.


// Always exactly half the tab's horizontal width, flush to the pane's left
// border and flush to the strip's own top edge (no inset either side) -
// height follows from the fixed master aspect. AirportInfoHeight() sizes the
// dark tray to match this exactly (same width-derived formula), so the two
// can never drift apart. Pure geometry, no GL calls, so both the text-x
// computation (early in Draw()) and the actual draw (issued last, so it
// paints over whatever it now overlaps) always agree.
void	WED_LiveryPane::FlagBannerRect(int bounds[4], float strip_top, float strip_bot,
						float & out_x, float & out_y, float & out_w, float & out_h) const
{
	out_w = (bounds[2] - bounds[0]) * 0.5f;
	out_h = out_w * ((float) mFlagTexH / (float) mFlagTexW);
	out_x = (float) bounds[0];
	out_y = strip_top - out_h;
}

// Bars are relative, so the track has no natural ceiling. Ten is the idiom the
// format itself uses - §4.2 notes "3 and 7" means the same as "30 and 70" - but
// a file that arrived carrying 700/300 has to stay both visible and draggable,
// so the track grows to fit whatever is already there.
int		WED_LiveryPane::WeightTrackMax(void) const
{
	int w[6], hi = 10;
	if (SelectionWeights(w))
		for (int i = 0; i < 6; ++i) if (w[i] > hi) hi = w[i];
	return hi;
}

void	WED_LiveryPane::WeightBarRect(int bounds[4], int idx, float r_out[4]) const
{
	float line_h = GUI_GetLineHeight(font_UI_Basic);

	// The bars get their OWN inset track rather than the slider's. Sharing the
	// slider's extents put bar A's centre on the leftmost tick, which meant its
	// left half hung 12px outside the pane - and bar F's right half likewise.
	// Six bars of real width simply do not fit between two endpoints that were
	// laid out for two circular handles.
	const float pad  = 4;
	float span       = (float) (bounds[2] - bounds[0]) - pad * 2;
	float step       = span / 6.0f;					// six slots, not five gaps
	float half       = (std::min)(step * 0.40f, 22.0f);
	float track_x0   = bounds[0] + pad + step * 0.5f;	// centre of the first slot

	float top, bot;
	WeightsYRange(bounds, top, bot);

	float cx = track_x0 + step * idx;
	r_out[0] = cx - half;
	r_out[2] = cx + half;
	r_out[1] = bot + line_h * 2 + 4;			// leaves the label and percentage rows below
	r_out[3] = top - line_h - 6;				// leaves the section title above
}

int		WED_LiveryPane::WeightBarForXY(int bounds[4], int x, int y) const
{
	if (WeightsHeight() <= 0) return -1;
	for (int i = 0; i < 6; ++i)
	{
		float r[4];
		WeightBarRect(bounds, i, r);
		// Generous vertically: the whole column is the target, not just the
		// filled part, or dragging a zero-height bar back up would be
		// impossible.
		if (x >= r[0] && x <= r[2] && y >= r[1] - 4 && y <= r[3] + 4) return i;
	}
	return -1;
}

int		WED_LiveryPane::WeightValueForY(int bounds[4], int y) const
{
	float r[4];
	WeightBarRect(bounds, 0, r);
	float h = r[3] - r[1];
	if (h <= 0) return 0;

	float frac = ((float) y - r[1]) / h;
	if (frac < 0.0f) frac = 0.0f;
	if (frac > 1.0f) frac = 1.0f;

	int v = (int) (frac * (float) WeightTrackMax() + 0.5f);	// snap to an integer
	if (v < 0)    v = 0;
	if (v > 1000) v = 1000;									// R11 ceiling
	return v;
}

void	WED_LiveryPane::PopulateButtonRect(int bounds[4], float b_out[4]) const
{
	float top, bot;
	HeaderYRange(bounds, top, bot);
	const float pad = 4;
	string cap = PopulateCaption(mSelectedRamps.size());
	float w = GUI_MeasureRange(font_UI_Basic, cap.c_str(), cap.c_str() + cap.size()) + 16;
	b_out[2] = (float) bounds[2] - pad;
	b_out[0] = b_out[2] - w;
	float mid = (top + bot) * 0.5f, h = GUI_GetLineHeight(font_UI_Basic) + 6;
	b_out[1] = mid - h * 0.5f;
	b_out[3] = mid + h * 0.5f;
}

void	WED_LiveryPane::WeightButtonRect(int bounds[4], float b_out[4]) const
{
	// Lives on the size slider's row, right-aligned, because that is where the
	// author is when they decide this stand needs a distribution rather than a
	// range. Same derive-from-the-section idiom as SortButtonRect().
	float top, bot;
	if (SelectionHasWeights()) WeightsYRange(bounds, top, bot);	// the slider has collapsed - see SliderHeight()
	else                       SliderYRange(bounds, top, bot);
	const float pad = 4;
	const float w   = 124;

	b_out[2] = (float) bounds[2] - pad;
	b_out[0] = b_out[2] - w;
	b_out[3] = top - 3;
	b_out[1] = b_out[3] - (GUI_GetLineHeight(font_UI_Basic) + 6);
}

// ---------------------------------------------------------------------------------------------
// layout
// ---------------------------------------------------------------------------------------------

float	WED_LiveryPane::AirportInfoHeight(int bounds[4]) const
{
	// The dark "tray" wraps whichever is taller: the flag banner (flush top,
	// tiny bottom pad - the banner's own width, half the tab's horizontal
	// space, see FlagBannerRect(), drives its height via the fixed master
	// aspect) or the word-wrapped text block (mCachedInfoLines/
	// mCachedStatusLines - recomputed just before this is called each Draw(),
	// see the top of Draw() for why that ordering matters). Falls back to a
	// single compact line when there's neither a flag nor any text yet
	// (nothing selected).
	float line_h = GUI_GetLineHeight(font_UI_Basic);
	int total_lines = (int) mCachedInfoLines.size() + (int) mCachedStatusLines.size();
	float text_h = total_lines > 0 ? total_lines * line_h + 8 : line_h + 8;

	if (mFlagTexId != 0)
	{
		float banner_w = (bounds[2] - bounds[0]) * 0.5f;
		float banner_h = banner_w * ((float) mFlagTexH / (float) mFlagTexW);
		return (std::max)(banner_h + 4, text_h);
	}
	return text_h;
}

float	WED_LiveryPane::HeaderHeight(void) const
{
	return GUI_GetLineHeight(font_UI_Basic) + 8;
}

float	WED_LiveryPane::FilterRowHeight(void) const
{
	return GUI_GetLineHeight(font_UI_Basic) + 8;
}

float	WED_LiveryPane::SliderHeight(void) const
{
	// Gone entirely once the stand has weights: the six bars ARE the size control
	// then, and a greyed slider saying "derived from the weights below" was a row
	// of dead space explaining its own absence. The Simple Mode button moves onto
	// the weights section's title row - see WeightButtonRect().
	if (SelectionHasWeights()) return 0;
	// title row + A-F label row + track/ball row, plus padding
	return GUI_GetLineHeight(font_UI_Basic) * 3 + 16;
}

float	WED_LiveryPane::WeightsHeight(void) const
{
	// Collapses to nothing when there is nothing to show. A stand with no 1313
	// row keeps today's behaviour (R17) and should not be carrying an empty
	// control that implies otherwise.
	if (!SelectionHasWeights()) return 0;

	// title row + bar track + the A-F label row + the percentage row
	return GUI_GetLineHeight(font_UI_Basic) * 3 + 44;
}

float	WED_LiveryPane::CoverageHeight(void) const
{
	// Two lines: the headline, and one line of detail. Fixed rather than
	// content-derived so the sections below it never shift as the numbers change -
	// a readout that moves the airline list every time you tick a checkbox is
	// worse than no readout.
	// Content-derived after all: the detail and the range clause wrap to the
	// pane's width, so a narrow panel needs more lines than a wide one and a
	// fixed count either clipped or wasted. mCoverageLineCount is set by Draw()
	// from the wrapped text BEFORE it lays the section out, so there is no frame
	// of lag. The list below does move when the count changes; that is the price
	// of legible text, and it changes only when the stand's situation does.
	return GUI_GetLineHeight(font_UI_Basic) * mCoverageLineCount + 10;
}

float	WED_LiveryPane::ListToolbarHeight(void) const
{
	return GUI_GetLineHeight(font_UI_Basic) + 8;
}

float	WED_LiveryPane::GapHeight(void) const
{
	return 10;
}

void	WED_LiveryPane::AirportInfoYRange(int bounds[4], float & top, float & bot) const
{
	top = (float) bounds[3];
	bot = top - AirportInfoHeight(bounds);
}

void	WED_LiveryPane::HeaderYRange(int bounds[4], float & top, float & bot) const
{
	float atop, abot;
	AirportInfoYRange(bounds, atop, abot);
	top = abot - GapHeight();
	bot = top - HeaderHeight();
}

void	WED_LiveryPane::FilterYRange(int bounds[4], float & top, float & bot) const
{
	float htop, hbot;
	HeaderYRange(bounds, htop, hbot);
	top = hbot - GapHeight();
	bot = top - FilterRowHeight();
}

void	WED_LiveryPane::SliderYRange(int bounds[4], float & top, float & bot) const
{
	float ftop, fbot;
	FilterYRange(bounds, ftop, fbot);
	top = fbot - GapHeight();
	bot = top - SliderHeight();
}

void	WED_LiveryPane::WeightsYRange(int bounds[4], float & top, float & bot) const
{
	float stop, sbot;
	SliderYRange(bounds, stop, sbot);
	const float h = WeightsHeight();
	// A hidden section takes no gap either, or every stand without weights
	// would carry a stripe of dead space where the bars would have been.
	top = sbot - (h > 0 ? GapHeight() : 0);
	bot = top - h;
}

bool	WED_LiveryPane::CoverageToggleHit(int bounds[4], int x, int y) const
{
	if (!mCoverageHasDetail || mSelectedRamps.empty()) return false;
	float top, bot;
	CoverageYRange(bounds, top, bot);
	float row_bot = top - GUI_GetLineHeight(font_UI_Basic) * 1.3f;
	return y <= top && y >= row_bot && x >= bounds[0] && x <= bounds[2];
}

void	WED_LiveryPane::CoverageYRange(int bounds[4], float & top, float & bot) const
{
	float wtop, wbot;
	WeightsYRange(bounds, wtop, wbot);
	top = wbot - GapHeight();
	bot = top - CoverageHeight();
}

void	WED_LiveryPane::ListToolbarYRange(int bounds[4], float & top, float & bot) const
{
	float ctop, cbot;
	CoverageYRange(bounds, ctop, cbot);
	top = cbot - GapHeight();
	bot = top - ListToolbarHeight();
}

// Fixed aspect (16:9) image on top, one line of header text (ICAO - name - country +
// flag icon) below it - see the .h comment on why this section's content is placeholder.

// One column's width: the pane minus a gap on both outer edges and between columns.
float	WED_LiveryPane::CardWidth(int bounds[4]) const
{
	float avail = (float) (bounds[2] - bounds[0]) - kCardGap * (float) (kCardCols + 1);
	return (std::max)(1.0f, avail / (float) kCardCols);
}

float	WED_LiveryPane::CardHeight(int bounds[4]) const
{
	// MUST derive from CardWidth(), same as Draw()'s image quad does - the image is
	// as wide as the card and its height follows from the fixed aspect. (Computing
	// this from a nominal/guessed width instead is exactly what made the whole block
	// render as one oversized black slab the first time this was tried: the image
	// then draws taller than the slot this function claims it needs, overflowing
	// into the next card and the checklist below.)
	float image_h = CardWidth(bounds) / kCardImageAspect;
	return image_h + GUI_GetLineHeight(font_UI_Basic) + 8;
}

// THE ONE PLACE ROW GEOMETRY IS COMPUTED. Cards are no longer a block above the
// checklist - they ARE the airline rows, laid out kCardCols to a line inside
// whichever section emitted them, with that section's header and divider still
// drawn as ordinary full-width lines in between.
//
// Every caller runs this rather than re-deriving y from a row index. The previous
// design wrote the formula out in Draw() and again in RowForY(), and they drifted:
// a click in the bottom row-height of the card strip toggled the first airline's
// checkbox, because one of the two forgot the card block was there.
float	WED_LiveryPane::LayoutRows(int bounds[4], const vector<bool> & is_card,
								   const vector<float> & tray_h,
								   vector<RowSlot> & out) const
{
	const float row_h  = GUI_GetLineHeight(font_UI_Basic) + 4;
	const float card_w = CardWidth(bounds);
	const float card_h = CardHeight(bounds);
	const float y_top  = ContentTop(bounds) + mScrollOffset;

	out.clear();
	out.resize(is_card.size());

	float y          = y_top;
	int   col        = 0;
	float line_extra = 0.0f;		// tray height added to the current line of cards

	for (size_t i = 0; i < is_card.size(); ++i)
	{
		RowSlot & s = out[i];
		s.is_card = is_card[i];

		if (is_card[i])
		{
			if (col == 0)
			{
				y -= kCardGap;						// the gap sits above each line of cards

				// The tallest tray anywhere on THIS grid line sets the line's extra
				// height. Applying it per card instead would slide one column's tray
				// out from under its neighbour and over the line below.
				line_extra = 0.0f;
				int scan_col = 0;
				for (size_t j = i; j < is_card.size() && is_card[j] && scan_col < kCardCols; ++j, ++scan_col)
					if (j < tray_h.size() && tray_h[j] > line_extra) line_extra = tray_h[j];
			}
			s.top      = y;
			s.bot      = y - card_h;				// the card itself - the tray hangs below it
			s.slot_bot = y - card_h - line_extra;
			s.x0  = (float) bounds[0] + kCardGap + col * (card_w + kCardGap);
			s.x1  = s.x0 + card_w;
			if (++col == kCardCols) { y -= card_h + line_extra; col = 0; }
		}
		else
		{
			// A header cannot share a line with the cards above it, so close any
			// partly-filled card line first. That is also what leaves a lone last
			// card sitting under column 0 with whitespace beside it, rather than
			// centred or stretched.
			if (col != 0) { y -= card_h + line_extra; col = 0; line_extra = 0.0f; }
			s.top      = y;
			s.bot      = y - row_h;
			s.slot_bot = s.bot;
			s.x0  = (float) bounds[0];
			s.x1  = (float) bounds[2];
			y -= row_h;
		}
	}
	if (col != 0) y -= card_h + line_extra;			// trailing partial line still takes its height

	// A bottom margin, counted into the content height so it can actually be
	// scrolled to. Without it the last section header - usually the collapsed "All
	// Airlines" - sits flush on the pane's edge with its box touching the frame.
	y -= kCardGap * 2.0f;

	return y_top - y;								// total content height
}

int		WED_LiveryPane::RowForXY(int bounds[4], const vector<bool> & is_card,
								 const vector<float> & tray_h, int x, int y) const
{
	if ((float) y > ContentTop(bounds)) return -1;	// above the content area entirely

	vector<RowSlot> slots;
	LayoutRows(bounds, mRowIsCard, tray_h, slots);

	for (size_t i = 0; i < slots.size(); ++i)
	{
		const RowSlot & s = slots[i];
		if ((float) y > s.top || (float) y <= s.slot_bot) continue;
		// Cards only answer for their own column - the gaps between and after them
		// are deliberately "not a card", so a click in the whitespace beside a lone
		// final card does nothing instead of toggling it.
		if (s.is_card && ((float) x < s.x0 || (float) x > s.x1)) continue;
		return (int) i;
	}
	return -1;
}

float	WED_LiveryPane::ContentTop(int bounds[4]) const
{
	float ttop, tbot;
	ListToolbarYRange(bounds, ttop, tbot);
	// Deliberately a small fixed gap, not the usual GapHeight() - the toolbar row and
	// the content below it (cards, then the checklist) are meant to read as one
	// bordered "workspace" (see Draw()'s border box), not separate sections.
	return tbot - 4;
}

// Top-right of the card. Carved off the slot rather than re-derived, so it cannot
// drift from what DrawAirlineCard paints.
void	WED_LiveryPane::LockIconRect(const RowSlot & slot, float r_out[4]) const
{
	r_out[2] = slot.x1 - 5.0f;
	r_out[0] = r_out[2] - kLockSize;
	r_out[3] = slot.top - 5.0f;
	r_out[1] = r_out[3] - kLockSize;
}

// The strip along the card's bottom edge that opens the tray. Deliberately the
// full card width: it is a "pull this open" affordance, and a narrow tab would be
// a worse target for no gain.
void	WED_LiveryPane::TrayTabRect(const RowSlot & slot, float r_out[4]) const
{
	// THE WHOLE CAPTION ROW, not a gutter under the arrow. A 22px target was the
	// reason this needed three attempts to hit; the row is the full card width and
	// still cannot be confused with the picture above it, which is what selects.
	r_out[0] = slot.x0;
	r_out[2] = slot.x1;
	r_out[1] = slot.bot;
	r_out[3] = slot.bot + kTrayTabH;
}


// The extra height each row's tray is currently claiming. Both the opening tray
// and the retracting one contribute, which is what keeps the total continuous
// while they cross over - if only the opening one counted, every row below would
// jump up by a tray's height the moment the other was dropped.
void	WED_LiveryPane::TrayHeights(const vector<string> & row_icaos, vector<float> & out) const
{
	out.assign(row_icaos.size(), 0.0f);
	for (size_t i = 0; i < row_icaos.size(); ++i)
	{
		if (row_icaos[i].empty()) continue;
		const AirlineCard * ac = CardFor(row_icaos[i]);
		if (!ac) continue;

		if (row_icaos[i] == mTrayAirline)
			out[i] = TrayFullHeight(ac->abs_paths.size()) * mTrayOpen;
		else if (row_icaos[i] == mTrayClosing)
			out[i] = TrayFullHeight(ac->abs_paths.size()) * mTrayClosingOpen;
	}
}


// -1 when no tray is open, when the point is elsewhere, or while the tray is
// still moving: a target sliding under the cursor is not a target, and treating
// it as one makes the preview flicker between aircraft as the drawer extends.
// Takes the caller's layout rather than building its own. MouseMove had already
// assembled the row list - 150-odd operators, sorted and filtered - and this
// rebuilt the identical thing a second time on every single mouse move.
int		WED_LiveryPane::TrayRowForXY(const vector<string> & row_icaos,
									 const vector<RowSlot> & slots, int x, int y)
{
	if (mTrayAirline.empty() || mTrayOpen < 1.0f) return -1;
	const AirlineCard * ac = CardFor(mTrayAirline);
	if (!ac) return -1;

	for (size_t vi = 0; vi < row_icaos.size() && vi < slots.size(); ++vi)
	{
		if (row_icaos[vi] != mTrayAirline) continue;
		const RowSlot & sl = slots[vi];
		if ((float) x < sl.x0 || (float) x > sl.x1) continue;

		float top = sl.bot;
		for (size_t i = 0; i < ac->labels.size(); ++i)
		{
			float ry = top - kTrayPad - (float) (i + 2) * kTrayRowH;
			if ((float) y >= ry && (float) y <= ry + kTrayRowH) return (int) i;
		}
	}
	return -1;
}

// Right-aligned pair, flush to the tab's right border: [Show Recommendation][Sort].
void	WED_LiveryPane::SortButtonRect(int bounds[4], float b_out[4]) const
{
	float top, bot;
	ListToolbarYRange(bounds, top, bot);
	const float pad = 4;
	const float w = 74;

	b_out[2] = (float) bounds[2] - pad;
	b_out[0] = b_out[2] - w;
	b_out[1] = bot + 2;
	b_out[3] = top - 2;
}

void	WED_LiveryPane::RecommendButtonRect(int bounds[4], float b_out[4]) const
{
	float sort_rect[4];
	SortButtonRect(bounds, sort_rect);
	const float w = 168;
	const float gap = 6;

	b_out[2] = sort_rect[0] - gap;
	b_out[0] = b_out[2] - w;
	b_out[1] = sort_rect[1];
	b_out[3] = sort_rect[3];
}

// Fills whatever room the toolbar row has left of the Recommend button, all
// the way to the workspace box's own left edge - grows/shrinks with the
// pane instead of having its own fixed width.
void	WED_LiveryPane::SearchFieldRect(int bounds[4], float b_out[4]) const
{
	float rec_rect[4];
	RecommendButtonRect(bounds, rec_rect);
	const float pad = 4;
	const float gap = 6;

	b_out[0] = (float) bounds[0] + pad;
	b_out[2] = rec_rect[0] - gap;
	b_out[1] = rec_rect[1];
	b_out[3] = rec_rect[3];
}

// A square button flush to SearchFieldRect()'s own right edge, sized to the
// field's height. Only used while mSearchQuery is non-empty - see Draw().
void	WED_LiveryPane::SearchClearButtonRect(int bounds[4], float b_out[4]) const
{
	float r[4];
	SearchFieldRect(bounds, r);
	float w = r[3] - r[1];

	b_out[2] = r[2];
	b_out[0] = r[2] - w;
	b_out[1] = r[1];
	b_out[3] = r[3];
}

// ---------------------------------------------------------------------------------------------
// ramp operation filter chips
// ---------------------------------------------------------------------------------------------

int		WED_LiveryPane::FilterChipForXY(int bounds[4], int x, int y) const
{
	float top, bot;
	FilterYRange(bounds, top, bot);
	if (y > top || y < bot) return -1;

	float chip_w = (bounds[2] - bounds[0]) / 5.0f;
	int idx = (int) ((x - bounds[0]) / chip_w);
	if (idx < 0) idx = 0;
	if (idx > 4) idx = 4;
	return idx;
}

// ---------------------------------------------------------------------------------------------
// size range slider
// ---------------------------------------------------------------------------------------------

int		WED_LiveryPane::SliderHandleForXY(int bounds[4], int x, int y) const
{
	if (mSelectedRamps.empty()) return -1;

	float line_h    = GUI_GetLineHeight(font_UI_Basic);
	float handle_r  = line_h * 0.5f;
	float slider_top, slider_bot;
	SliderYRange(bounds, slider_top, slider_bot);
	float track_y   = slider_bot + line_h * 0.5f;
	float track_x0  = bounds[0] + 4 + handle_r;
	float track_x1  = bounds[2] - 4 - handle_r;

	int minIdx = WidthEnumToIndex(mSelectedRamps[0]->GetWidthMin());
	int maxIdx = WidthEnumToIndex(mSelectedRamps[0]->GetWidth());
	float min_x = track_x0 + (track_x1 - track_x0) * minIdx / 5.0f;
	float max_x = track_x0 + (track_x1 - track_x0) * maxIdx / 5.0f;

	if (y < track_y - handle_r*1.5f || y > track_y + handle_r*1.5f) return -1;

	if (minIdx == maxIdx)
	{
		// single overlapping ball - direction (min vs max) isn't known yet
		if (fabs((double)(x - min_x)) <= handle_r*1.5) return 2;
		return -1;
	}

	bool near_min = fabs((double)(x - min_x)) <= handle_r*1.5;
	bool near_max = fabs((double)(x - max_x)) <= handle_r*1.5;

	if (near_min && near_max)
		return (fabs((double)(x-min_x)) <= fabs((double)(x-max_x))) ? 0 : 1;
	if (near_min) return 0;
	if (near_max) return 1;
	return -1;
}

float	WED_LiveryPane::SliderContinuousIndexForX(int bounds[4], int x) const
{
	float line_h    = GUI_GetLineHeight(font_UI_Basic);
	float handle_r  = line_h * 0.5f;
	float track_x0  = bounds[0] + 4 + handle_r;
	float track_x1  = bounds[2] - 4 - handle_r;

	float frac = (track_x1 > track_x0) ? (x - track_x0) / (track_x1 - track_x0) : 0.0f;
	return frac * 5.0f;		// unsnapped - caller applies detent hysteresis
}
