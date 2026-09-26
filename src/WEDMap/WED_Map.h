/*
 * Copyright (c) 2007, Laminar Research.
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

#ifndef WED_MAP_H
#define WED_MAP_H

#include "CompGeomDefs2.h"
#include "GUI_Pane.h"
#include "GUI_Button.h"
#include "WED_MapZoomerNew.h"
#include "GUI_Listener.h"
#include "GUI_Commander.h"
#include <stdint.h>
#include "WED_MapLayer.h"

class	WED_MapLayer;
class	WED_MapToolNew;
class	IResolver;
class	IGISEntity;
class	ISelection;

class	WED_Map : public GUI_Pane, public WED_MapZoomerNew, public GUI_Listener, public GUI_Commander {
public:

						 WED_Map(IResolver * in_resolver, GUI_Commander * cmdr);
	virtual				~WED_Map();

			void		SetTool(WED_MapToolNew * tool);

	// VIEW ROTATION - moderation only, and only for looking and selecting.
	// The guard rails: it is on only while the select tool (Vertex) is the
	// tool - picking any other tool turns the view north up again - and while
	// turned, tool drags are not passed on, so nothing can be moved or drawn in
	// a rotated frame. Held by this window alone: a document opens north up.
			void		SetSelectTool(WED_MapToolNew * t) { mSelectTool = t; }
			bool		IsRotateMode(void) const { return mRotateMode; }
			void		SetRotateMode(bool on);
	virtual	void		GetMouseLocNow(int * out_x, int * out_y);
			void		AddLayer(WED_MapLayer * layer);
	
			void		SetFilter(const string& name, const MapFilter_t& hide_filter, const MapFilter_t& lock_filter);

	virtual void		SetBounds(int x1, int y1, int x2, int y2);
	virtual void		SetBounds(int inBounds[4]);

	virtual	void		Draw(GUI_GraphState * state);

	virtual	int			MouseDown(int x, int y, int button);
	virtual	void		MouseDrag(int x, int y, int button);
	virtual	void		MouseUp  (int x, int y, int button);
	virtual	int			MouseMove(int x, int y);
	virtual	int			ScrollWheel(int x, int y, int dist, int axis);

	virtual	int			HandleKeyPress(uint32_t inKey, int inVK, GUI_KeyFlags inFlags);

	virtual	void		ReceiveMessage(
							GUI_Broadcaster *		inSrc,
							intptr_t				inMsg,
							intptr_t				inParam);

private:

			void		DrawVisFor(WED_MapLayer * layer, int current, const Bbox2& bounds, IGISEntity * what, GUI_GraphState * g, ISelection * sel, int depth);
			void		DrawStrFor(WED_MapLayer * layer, int current, const Bbox2& bounds, IGISEntity * what, bool what_locked, GUI_GraphState * g, ISelection * sel, int depth);

		IGISEntity *	GetGISBase();
		ISelection *	GetSel();


	vector<WED_MapLayer *>			mLayers;
	WED_MapToolNew *				mTool;
	IResolver *						mResolver;

	MapFilter_t						mHideFilter;
	MapFilter_t						mLockFilter;
	string							mFilterName;

	WED_MapLayer *	mClickLayer;
	int				mX;
	int				mY;

	int				mX_Orig;
	int				mY_Orig;
	int				mIsDownCount;
	int				mIsDownExtraCount;
	
	GUI_Button *	mTiltButton[4];

	WED_MapToolNew *	mSelectTool;
	bool				mRotateMode;
	bool				mRotating;			// a Shift+right-drag is turning the view
	double				mRotStartAngle;		// screen angle of that drag's start, about the centre
	double				mRotStartView;		// the view rotation when it began
	// The measuring arrow: in rotate mode a left-drag draws an arrow, like a
	// placed object's heading, and on release the view turns to put it on the
	// nearest of 0/90/180/270. A left-click without a drag still selects - the
	// click is held back from the tool until it is known not to be a drag, so
	// measuring never clears the selection.
	bool				mClickHeld;			// a left-down given to no layer, not yet to the tool
	bool				mArrowOn;			// ...and it became a drag: the arrow is out
	int					mArrowX0, mArrowY0, mArrowX1, mArrowY1;	// screen coords
	// The reference the last arrow set: the view rotation that put it on an
	// axis. Shift+right-drag then detents at it and every 90 from it - and has
	// no detent at all until an arrow has been drawn: 90 degrees from nothing
	// means nothing. A new arrow replaces it; leaving rotate mode clears it.
	bool				mHasRef;
	double				mRefRotation;

			void		ToMap(int& x, int& y) const;
			double		ScreenAngle(int x, int y) const;
};


#endif

