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

	                 Lay line along shape          345
	                 ---------------------------------
	   shape -------| pavement_FX / edge_D -----------
	                |   edge_D_dark
	                |   edge_D_light                    <- hover: amber
	                | pavement_FX / seams ------------
	                |   ...                             <- wheel scrolls

	Every public library .lin is listed; the library's folders are thin divider
	rows with the folder's name at the left - for naming and sorting only, not
	collapsible. Picking an entry runs WED_DoLayLineAlong: a new line on exactly
	the shape's nodes and Bezier handles, one per ring, one undo step.

	Nothing shows until it is asked for. A click anywhere else, Escape, another
	tool or a different selection closes it. The card is level with the screen
	however the view is turned, like the moderation callouts.
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

	struct Row {
		bool			folder;		// a divider row: label is the folder's name
		std::string		label;		// the entry's name, without .lin
		std::string		vpath;		// entries only
	};

			bool		SelectedShapeAt(const Point2& ll, int& out_id) const;
			bool		ToolAllows(void) const;
			void		Open(int shape_id, const Point2& click_ll);
			void		Close(void);
			void		BuildRows(void);
			void		DrawCard(GUI_GraphState * g);

	// double click detection, as the drawing tools do it
	float				mLastTime;
	int					mLastX, mLastY;
	int					mLastShape;		// the selected shape under the first click, -1 = none

	bool				mOpen;
	int					mShapeID;		// the shape the card belongs to
	Point2				mAnchor;		// where the callout starts, lat/lon
	std::vector<Row>	mRows;
	int					mScroll;		// first row shown
	int					mHover;			// row under the mouse, -1 = none
	float				mCard[4];		// x0 y0 x1 y1 last frame, screen frame
	float				mListTop;		// the first row's top edge last frame
	int					mShown;			// how many rows fit last frame
};

#endif /* WED_LAYLINELAYER_H */
