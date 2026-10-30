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

#ifndef WED_LAYLINELAYER_H
#define WED_LAYLINELAYER_H

/*
	WED_LayLineLayer - THEORY OF OPERATION

	Lay a line (.lin) along a shape someone has already drawn: a double click on
	a selected polygon, line or shape, with the select (Vertex) tool, pops out a
	card beside it, a white callout line from the shape to the card:

	                 Lay line along shape                 75
	                 [ soil 1m_                          ]   <- type to search
	                  Recent  pavement_FX  roads  terrain_FX ...
	   shape -------| edge_D
	                |   cracked   elevated   soft            <- hover: amber
	                | taxiway_seams / dark
	                |   1   2   3   4   5   6   7            <- wheel scrolls

	WHERE THE TABS COME FROM. Only from the library itself: the virtual path of
	each public .lin. X-Plane's library has no other classification (PUBLIC,
	PRIVATE and DEPRECATED say who may use an asset, REGION where), so nothing
	here is a list of our own. Leading container folders carry no meaning and
	are dropped (lib/airport/ground/, lib/airport/, lib/g10/, lib/); the next
	folder is the tab, the rest the group. A folder holding a single line is
	folded into its parent: taxi_sign_base/dark/2.2m/strip shows as "2.2m strip"
	under taxi_sign_base / dark.
	lib/airport/lines is left out: those are the painted taxi lines behind
	apt.dat's own line types - the Taxilines tool draws them.

	Recent: the lines picked last, kept in WED's prefs, as many as the card has
	room for.

	Picking an entry runs WED_DoLayLineAlong: a new line on exactly the shape's
	nodes and Bezier handles, one per ring, one undo step.

	Nothing shows until it is asked for. A click anywhere else, Escape (once the
	search is empty), another tool or a different selection closes it. The card
	is level with the screen however the view is turned, like the moderation
	callouts.
*/

#include "WED_MapLayer.h"
#include "CompGeomDefs2.h"
#include <string>
#include <vector>

class	WED_LayLineLayer : public WED_MapLayer {
public:

						 WED_LayLineLayer(GUI_Pane * host, WED_MapZoomerNew * zoomer, IResolver * resolver);
	virtual				~WED_LayLineLayer();

	virtual	int			HandleClickDown(int inX, int inY, int inButton, GUI_KeyFlags modifiers);
	virtual	int			HandleScrollWheel(int inX, int inY, int inDist);
	virtual	int			HandleLayerKeyPress(uint32_t inKey, int inVK, GUI_KeyFlags inFlags);
	virtual	void		DrawSelected(bool inCurrent, GUI_GraphState * g);
	virtual	void		GetCaps(bool& draw_ent_v, bool& draw_ent_s, bool& cares_about_sel, bool& wants_clicks);

private:

	struct Entry {
		std::string		vpath;
		std::string		tab;		// the first meaningful folder
		std::string		group;		// the folders below it, " / " between; may be empty
		std::string		label;		// file name without .lin, plus any folded folders
		std::string		key;		// lower case, for the search
	};

	// One line of the list as laid out for the card's width: a group's name,
	// or a row of entries side by side.
	struct Line {
		std::string		header;
		std::vector<int>	cells;		// into mEntries
		bool			full = false;	// Recent: one entry, named with its tab and group
		int				cols = 1;		// the group's entries to a row
	};

	struct Hit {
		float			x0, y0, x1, y1;
		int				entry;			// >= 0: an entry
		int				tab;			// >= 0 (and entry < 0): a tab, 0 = Recent
	};

			bool		SelectedShapeAt(const Point2& ll, int& out_id) const;
			bool		ToolAllows(void) const;
			void		Open(int shape_id, const Point2& click_ll);
			void		Close(void);
			void		BuildEntries(void);
			void		LayOut(float inner_w, std::vector<Line>& out) const;
			void		Pick(int entry);
			void		DrawCard(GUI_GraphState * g);

	// double click detection, as the drawing tools do it
	float				mLastTime;
	int					mLastX, mLastY;
	int					mLastShape;		// the selected shape under the first click, -1 = none

	bool				mOpen;
	int					mShapeID;		// the shape the card belongs to
	Point2				mAnchor;		// where the callout starts, lat/lon
	std::vector<Entry>	mEntries;
	std::vector<std::string>	mTabs;	// in library order; shown after Recent
	int					mTab;			// 0 = Recent, n = mTabs[n-1]
	std::string			mQuery;			// typed; when not empty it searches every tab
	int					mScroll;		// first list line shown
	float				mCard[4];		// x0 y0 x1 y1 last frame, screen frame
	std::vector<Hit>	mHits;			// last frame, screen frame
};

#endif /* WED_LAYLINELAYER_H */
