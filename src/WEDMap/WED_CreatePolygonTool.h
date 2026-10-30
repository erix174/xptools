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

#ifndef WED_CREATEPOLYGONTOOL_H
#define WED_CREATEPOLYGONTOOL_H

#include "WED_CreateToolBase.h"

class	WED_Thing;

enum CreateTool_t {

	create_Taxi = 0,
	create_Boundary,
	create_Marks,
	create_Hole,
	create_Shape,

	create_ExcludePol,
	create_Facade,
	create_Forest,
	create_String,
	create_Line,

	create_Autogen,
	create_Polygon,
};

// A check box in the tool defaults pane bound to a global setting in WED's prefs
// instead of its own value; a change is saved at once, as the snap radius is.
class	WED_PropBoolGlobal : public WED_PropBoolText {
public:
	WED_PropBoolGlobal(WED_PropertyHelper * parent, const char * title, int offset, int& global) :
		WED_PropBoolText(parent, title, offset, 0), mGlobal(global) { }

	virtual void		GetProperty(PropertyVal_t& val) const;
	virtual void		SetProperty(const PropertyVal_t& val, WED_PropertyHelper * parent);
private:
	int&				mGlobal;
};

class	WED_CreatePolygonTool : public WED_CreateToolBase {
public:

						 WED_CreatePolygonTool(
									const char *		tool_name,
									GUI_Pane *			host,
									WED_MapZoomerNew *	zoomer,
									IResolver *			resolver,
									WED_Archive *		archive,
									CreateTool_t		tool_type);
	virtual				~WED_CreatePolygonTool();

			void				SetResource(const string& r);

	// WED_MapToolNew
	virtual	const char *		GetStatusText(void);

	virtual	void		GetNthPropertyDict(int n, PropertyDict_t& dict) const;

	// Snap To Vertices and Match Bezier Handles live in WED's global prefs,
	// not in the document's.
	virtual	bool		IsDocPref(int n) const;

	// WED_MapLayer: the snap target under the mouse, while snapping is on
	virtual	void		DrawStructure(bool inCurrent, GUI_GraphState * g);

protected:

		WED_PropIntEnum					mPavement;
		WED_PropDoubleText				mRoughness;
		WED_PropDoubleText				mHeading;
		WED_PropIntEnumSet				mMarkings;
		WED_PropIntEnumSetFilterVal		mMarkingsLines;
		WED_PropIntEnumSetFilterVal		mMarkingsLights;

		WED_PropStringText				mResource;
		WED_PropDoubleTextMeters		mHeight;
		WED_PropDoubleText				mDensity;

		WED_PropDoubleTextMeters		mSpacing;
		WED_PropDoubleTextMeters		mAgsHght;
		WED_PropIntEnumSet				mExclude;

		WED_PropBoolText				mUVMap;
		WED_PropBoolText				mPickWalls;

		WED_PropStringText				mShapeText;

		// Off unless the author turns them on (2026-10-29, Julian): new nodes go
		// onto polygon, line and string vertices nearby; on finishing, the new
		// shape takes their Bezier handles (as Match Bezier Handles). Both show
		// and set gSnapToVertices / gMatchBezierHandles: one setting for every tool.
		WED_PropBoolGlobal				mSnapNodes;
		WED_PropBoolGlobal				mMatchHandles;

	virtual	void		AcceptPath(
							const vector<Point2>&	pts,
							const vector<Point2>&	dirs_lo,
							const vector<Point2>&	dirs_hi,
							const vector<int>		has_dirs,
							const vector<int>		has_split,
							int						closed);
	virtual	bool		CanCreateNow(void);
	virtual	void		SnapNewNode(Point2& io_pt);

			bool		FindSnapVertex(const Point2& pt, Point2& out_vertex);
			void		CollectSnapVertices(IGISEntity * e);

		vector<Point2>	mSnapVertices;			// every outline vertex shown, per document change
		long long		mSnapKeyArchive;


			WED_Thing *	GetHost(int& idx);

		CreateTool_t	mType;

};

#endif /* WED_CREATEPOLYGONTOOL_H */
