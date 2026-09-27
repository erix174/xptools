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

#ifndef WED_MODERATIONLAYER_H
#define WED_MODERATIONLAYER_H

/*
	WED_ModerationLayer - THEORY OF OPERATION

	What a Gateway moderator reads off the map, stand by stand. Three pieces:

	COLOUR IS SIMILARITY. Every ramp start's aircraft silhouette is drawn in the
	colour of its signature (operation type, airline set, size or weights,
	equipment - see
	WED_ModerationSignature) instead of the default green: stands that would park
	the same thing share a colour, different ones get colours spread round the
	hue circle. A moderator sees at a glance which stands were copied from which,
	selected or not.

	CALLOUTS for the selected stands (an airport or group counts as its ramp
	starts), at a density that fits how many there are:
	Stands with the same signature on screen share ONE callout, with a leader
	from each: they are the same entry, and saying it once is the point. The
	tier below counts those shared callouts, not stands.
	  up to 5    a full card per entry, beside it:
	                 [flag] ZBAA  02-PIN-C                      (pin)
	                 ------------------------------------------------ top edge
	                 | Airlines   DAL UAL
	                 | Updated (M)   C100
	                 | Passenger | Jets | Gate
	                 | v 1 to check     <- hover: the operator tray
	  6 to 40    a one-line chip per entry in a column at the map's right edge,
	             with a leader back to its stand; hovering a chip opens its card
	  more       a legend instead: one row per distinct signature, with a count
	             and the issues; hovering a row rings its stands, clicking it
	             selects them
	The tray lists operators three to a row with their flags and a verdict (see
	WED_ModerationDescribe); a "?" opens a web search in a small browser window.

	COMPARE. The pin makes a stand the base. Every other card then shows what it
	adds (+), lacks (-) and changes (~); chips show the counts. The base stays on
	screen while other stands are selected.

	Everything here asks WED_ModerationEnabled() - the Moderator Mode preference,
	read live.
*/

#include "WED_MapLayer.h"
#include "WED_LiveryModeration.h"
#include <map>
#include <set>
#include <string>
#include <vector>

class WED_RampPosition;

// The silhouette colour for a ramp start: its signature's colour while
// moderation is on - grey for op type None, and in Moderation View grey for
// every stand that needs nothing. False when moderation is off: draw the
// default green. alpha_scale (optional) dims the stands the view steps back.
bool	WED_ModerationTintFor(const WED_RampPosition * ramp, float out_rgb[3], float * alpha_scale = NULL);

// MODERATION VIEW - the toolbar's first button. Highlights the stands that need
// checking, greys the rest, and opens the airport overview: how many stands,
// how many reviewed this session, and the list of stands to check, each one a
// click away. Off by default.
bool	WED_ModerationViewOn(void);
void	WED_SetModerationView(bool on);

class	WED_ModerationLayer : public WED_MapLayer {
public:

						 WED_ModerationLayer(GUI_Pane * host, WED_MapZoomerNew * zoomer, IResolver * resolver);
	virtual				~WED_ModerationLayer();

	virtual	int			HandleClickDown(int inX, int inY, int inButton, GUI_KeyFlags modifiers);
	virtual	void		HandleClickUp  (int inX, int inY, int inButton, GUI_KeyFlags modifiers);
	virtual	int			HandleScrollWheel(int inX, int inY, int inDist);
	virtual	void		DrawSelected(bool inCurrent, GUI_GraphState * g);
			void		DrawOverlays(GUI_GraphState * g);	// in the screen's frame, however the view is turned
	virtual	void		GetCaps(bool& draw_ent_v, bool& draw_ent_s, bool& cares_about_sel, bool& wants_clicks);

private:

	struct Flag { unsigned int tex; int w, h; };

	struct Hit {
		enum Kind { hit_Card, hit_Pin, hit_Tray, hit_Search, hit_Chip, hit_Legend, hit_Focus, hit_Panel, hit_Filter, hit_Sort, hit_Copy };
		int				kind;
		float			x0, y0, x1, y1;
		int				ramp_id;		// or the legend row
		std::string		url;			// hit_Search
	};

	struct Callout {
		WED_RampPosition *		ramp;
		int						id;
		WED_ModerationEntry		e;
		bool					on_screen;
		float					ax, ay;			// the stand, in pixels
		float					x0, y0, x1, y1;	// card: y1 is the header's top
		std::vector<std::string>	diff;		// against the pinned stand
		std::vector<int>			diff_kind;	// 1 added, -1 removed, 0 changed, 2 same
		int						n_add, n_rem, n_chg;
		// One callout speaks for every on-screen stand with the same signature:
		// their anchors hang off it, and the label says how many.
		std::string				label;			// the name shown: "03-MIX-CDE x4", "01-CONTROL +3"
		std::vector<std::pair<float, float> >	others;	// the other stands' anchors
	};

	void				Collect(std::vector<Callout> & out);
	void				Diff(const WED_ModerationEntry & base, Callout & c) const;
	void				Group(std::vector<Callout> & cs, std::vector<Callout> & out) const;
	const Flag *		FlagFor(const std::string & ioc);
	void				SizeCard(Callout & c) const;
	float				TrayLines(const Callout & c, std::vector<std::string> & lines) const;
	void				DrawCard(GUI_GraphState * g, Callout & c, bool pinned, bool leader, float tray_h);
	void				DrawTray(GUI_GraphState * g, Callout & c, const std::vector<std::string> & lines);
	void				DrawCards(GUI_GraphState * g, std::vector<Callout> & cs);
	void				DrawChips(GUI_GraphState * g, std::vector<Callout> & cs);
	void				DrawLegend(GUI_GraphState * g, std::vector<Callout> & cs);
	void				DrawOverview(GUI_GraphState * g);
	void				DrawCopyButton(GUI_GraphState * g, float x, float y_top);
	void				Focus(int ramp_id);

	std::vector<Hit>				mHits;			// from the last frame, for clicks and hover
	int								mPinnedID;		// WED_Persistent id, -1 = none
	int								mTrayID;		// the card whose tray is open, -1 = none
	int								mOpenID;		// chips: the chip whose card is open
	int								mLegendRow;		// legend: the row under the mouse
	std::vector<std::vector<int> >	mLegendIDs;		// legend: each row's ramp ids, for the click
	std::string						mPendingURL;	// a "?" pressed: opened on the mouse-up...
	float							mPendingBox[4];	// ...if it is still over the "?" (screen frame)
	std::map<std::string, Flag>		mFlags;
	// Moderation View: what has been looked at this session. By SETUP, not by
	// stand: stands with one signature are the same entry, so reviewing one
	// reviews them all - whether each sits in the right place is the
	// moderator's own look at the map.
	std::set<std::string>			mReviewed;
	int								mListFilter;	// overview: 0 all, 1 not listed here, 2 no data, 3 foreign, 4 parks nothing
	int								mListSort;		// overview: 0 by name, 1 most to verify first
	double							mCopiedUntil;		// the "Copied!" flash, steady-clock seconds
	float							mOverviewBottom;	// the overview's lower edge this frame, screen y; <0 = not shown
	int								mListScroll;	// overview: first row shown
	float							mListBox[4];	// overview: the list's rectangle, for the wheel
};

#endif /* WED_MODERATIONLAYER_H */
