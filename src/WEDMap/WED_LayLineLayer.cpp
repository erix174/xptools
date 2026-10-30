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

#include "WED_LayLineLayer.h"
#include "WED_Map.h"
#include "WED_MapZoomerNew.h"
#include "WED_VertexTool.h"
#include "WED_ToolUtils.h"
#include "WED_LibraryMgr.h"
#include "WED_GroupCommands.h"
#include "WED_Thing.h"
#include "IGIS.h"
#include "ISelection.h"
#include "GUI_Fonts.h"
#include "GUI_GraphState.h"
#include "GUI_Pane.h"

#include <cmath>
#include <cstdlib>
#include <cstring>

#if APL
	#include <OpenGL/gl.h>
#else
	#include "glew.h"
#endif

using std::string;
using std::vector;

// As the drawing tools tell a double click (WED_CreateToolBase).
static const float	kDoubleClickTime = 0.2f;
static const int	kDoubleClickDist = 3;

static const float kWhite[4]  = { 0.93f, 0.93f, 0.93f, 1.0f };
static const float kMuted[4]  = { 0.62f, 0.62f, 0.64f, 1.0f };
static const float kRule[4]   = { 0.62f, 0.62f, 0.64f, 0.55f };
static const float kAmber[4]  = { 1.00f, 0.75f, 0.30f, 1.0f };
static const float kHoverBg[4] = { 1.00f, 0.75f, 0.30f, 0.18f };
static const float kFill[4]   = { 0.08f, 0.08f, 0.10f, 0.88f };	// a dark card: the list is read, not glanced at
static const float kPad = 8.0f;
static const float kIndent = 18.0f;
static const float kGap = 48.0f;		// from the anchor to the card
static const int   kMaxRows = 24;

// Not std::max: <windows.h>, force-included through XDefs.h, defines min/max macros.
static inline float	Max(float a, float b) { return a > b ? a : b; }
static inline float	Min(float a, float b) { return a < b ? a : b; }

static float	TextW(const string & s)
{
	return s.empty() ? 0.0f : GUI_MeasureRange(font_UI_Basic, s.c_str(), s.c_str() + s.size());
}

static float	LineH(void)		{ return GUI_GetLineHeight(font_UI_Basic); }
static float	Asc(void)		{ return GUI_GetLineAscent(font_UI_Basic); }
static float	EntryH(void)	{ return LineH() + 3.0f; }
static float	FolderH(void)	{ return LineH() + 1.0f; }
static float	HeadH(void)		{ return LineH() + 8.0f; }

static void	Txt(GUI_GraphState * g, const float col[4], float x, float y, const char * s, int align = align_Left)
{
	const float shadow[4] = { 0, 0, 0, 0.85f * col[3] };
	GUI_FontDraw(g, font_UI_Basic, shadow, x + 1, y - 1, s, align);
	GUI_FontDraw(g, font_UI_Basic, col, x, y, s, align);
}

static void	Fill(float x0, float y0, float x1, float y1, const float c[4])
{
	glColor4fv(c);
	glBegin(GL_QUADS);
	glVertex2f(x0, y0); glVertex2f(x1, y0); glVertex2f(x1, y1); glVertex2f(x0, y1);
	glEnd();
}

static bool	Inside(float x, float y, const float b[4])
{
	return x >= b[0] && x <= b[2] && y >= b[1] && y <= b[3];
}

// The overlays live in the screen's frame; the map hands us map pixels, which
// differ once the view is turned (see WED_ModerationLayer).
static Point2	ToScreen(WED_MapZoomerNew * z, int x, int y)
{
	return z->MapPixelToScreen(Point2(x, y));
}

// "lib/airport/ground/pavement_FX/edge_D" -> "pavement_FX / edge_D"
static string	FolderLabel(const string & dir)
{
	static const char * strip[] = { "lib/airport/ground/", "lib/airport/", "lib/" };
	string d = dir;
	for (const char * s : strip)
		if (d.compare(0, strlen(s), s) == 0) { d.erase(0, strlen(s)); break; }
	string out;
	for (char c : d)
		if (c == '/') out += " / ";
		else out += c;
	return out.empty() ? dir : out;
}

// What WED_DoLayLineAlong lays along: a polygon, or a line, string or shape of
// its own - a polygon's ring goes with the polygon.
static bool	IsOutline(IGISEntity * e, WED_Thing * t)
{
	if (!e || !t) return false;
	switch (e->GetGISClass()) {
	case gis_Polygon:
		return true;
	case gis_PointSequence:
	case gis_Ring:
	case gis_Chain:
		return dynamic_cast<IGISPolygon *>(t->GetParent()) == NULL;
	default:
		return false;
	}
}

WED_LayLineLayer::WED_LayLineLayer(GUI_Pane * host, WED_MapZoomerNew * zoomer, IResolver * resolver) :
	WED_MapLayer(host, zoomer, resolver), mLastTime(-9.9e9f), mLastX(0), mLastY(0), mLastShape(-1),
	mOpen(false), mShapeID(-1), mScroll(0), mHover(-1), mListTop(0), mShown(0)
{
	mCard[0] = mCard[1] = mCard[2] = mCard[3] = 0;
}

WED_LayLineLayer::~WED_LayLineLayer()
{
}

void	WED_LayLineLayer::GetCaps(bool& draw_ent_v, bool& draw_ent_s, bool& cares_about_sel, bool& wants_clicks)
{
	draw_ent_v = false;
	draw_ent_s = false;
	cares_about_sel = false;
	wants_clicks = true;
}

bool	WED_LayLineLayer::ToolAllows(void) const
{
	WED_Map * map = dynamic_cast<WED_Map *>(GetHost());
	return map && dynamic_cast<WED_VertexTool *>(map->GetTool()) != NULL;
}

// The selected polygon, line or shape under the mouse: inside it, or within a
// few pixels of its outline - as the select tool picks.
bool	WED_LayLineLayer::SelectedShapeAt(const Point2& ll, int& out_id) const
{
	ISelection * sel = WED_GetSelect(GetResolver());
	if (!sel) return false;
	vector<ISelectable *> selected;
	sel->GetSelectionVector(selected);
	const double frame_dist = fabs(GetZoomer()->YPixelToLat(0) - GetZoomer()->YPixelToLat(4));
	for (ISelectable * s : selected)
	{
		IGISEntity * e = dynamic_cast<IGISEntity *>(s);
		WED_Thing * t = dynamic_cast<WED_Thing *>(s);
		if (!IsOutline(e, t)) continue;
		if ((e->GetGISClass() == gis_Polygon && e->PtWithin(gis_Geo, ll)) || e->PtOnFrame(gis_Geo, ll, frame_dist))
		{
			out_id = t->GetID();
			return true;
		}
	}
	return false;
}

void	WED_LayLineLayer::BuildRows(void)
{
	mRows.clear();
	WED_LibraryMgr * lib = WED_GetLibraryMgr(GetResolver());
	if (!lib) return;
	// depth first: a folder's own lines, then its sub-folders, in library order
	vector<string> todo(1, string());
	while (!todo.empty())
	{
		string dir = todo.back();
		todo.pop_back();
		vector<string> kids, dirs;
		lib->GetResourceChildren(dir, pack_Library, kids);
		bool head = false;
		for (const string & k : kids)
		{
			const res_type t = lib->GetResourceType(k);
			if (t == res_Directory) { dirs.push_back(k); continue; }
			if (t != res_Line || !lib->IsResourceLibrary(k) || lib->IsResourceDeprecatedOrPrivate(k)) continue;
			if (!head)
			{
				Row f = { true, FolderLabel(dir), string() };
				mRows.push_back(f);
				head = true;
			}
			string name = k.substr(k.find_last_of('/') + 1);
			if (name.size() > 4 && name.compare(name.size() - 4, 4, ".lin") == 0)
				name.erase(name.size() - 4);
			Row r = { false, name, k };
			mRows.push_back(r);
		}
		todo.insert(todo.end(), dirs.rbegin(), dirs.rend());
	}
}

void	WED_LayLineLayer::Open(int shape_id, const Point2& click_ll)
{
	mOpen = true;
	mShapeID = shape_id;
	mAnchor = click_ll;
	// a polygon's callout starts in its middle, if the middle is on it
	WED_Thing * wrl = WED_GetWorld(GetResolver());
	if (IGISEntity * e = dynamic_cast<IGISEntity *>(wrl ? wrl->FetchPeer(shape_id) : NULL))
		if (e->GetGISClass() == gis_Polygon)
		{
			Bbox2 b;
			e->GetBounds(gis_Geo, b);
			if (e->PtWithin(gis_Geo, b.centroid())) mAnchor = b.centroid();
		}
	BuildRows();
	mScroll = 0;
	mHover = -1;
	mShown = 0;
	mCard[0] = mCard[1] = mCard[2] = mCard[3] = 0;
	GetHost()->Refresh();
}

void	WED_LayLineLayer::Close(void)
{
	if (!mOpen) return;
	mOpen = false;
	mShapeID = -1;
	mRows.clear();
	GetHost()->Refresh();
}

int		WED_LayLineLayer::HandleClickDown(int inX, int inY, int inButton, GUI_KeyFlags modifiers)
{
	if (inButton != 0) return 0;
	if (mOpen)
	{
		const Point2 sp = ToScreen(GetZoomer(), inX, inY);
		const float x = sp.x(), y = sp.y();
		if (Inside(x, y, mCard))
		{
			// rows from the top, as the last frame laid them out
			if (y <= mListTop)
			{
				float top = mListTop;
				for (int i = mScroll; i < (int) mRows.size() && i < mScroll + mShown; ++i)
				{
					const float h = mRows[i].folder ? FolderH() : EntryH();
					if (y > top - h)
					{
						if (!mRows[i].folder)
						{
							const string vpath = mRows[i].vpath;
							Close();
							WED_DoLayLineAlong(GetResolver(), vpath);
						}
						break;
					}
					top -= h;
				}
			}
			return 1;		// a click on the card is ours, whatever it hit
		}
		Close();			// anywhere else: closes it, and the click goes on
	}

	if (!ToolAllows() || (modifiers & (gui_ShiftFlag | gui_ControlFlag | gui_OptionAltFlag)))
	{
		mLastShape = -1;
		return 0;
	}
	const float now = GetHost()->GetTimeNow();
	int id = -1;
	const bool on = SelectedShapeAt(GetZoomer()->PixelToLL(Point2(inX, inY)), id);
	if (on && id == mLastShape && now - mLastTime < kDoubleClickTime &&
		abs(inX - mLastX) <= kDoubleClickDist && abs(inY - mLastY) <= kDoubleClickDist)
	{
		mLastShape = -1;
		Open(id, GetZoomer()->PixelToLL(Point2(inX, inY)));
		return 1;
	}
	// The first click goes on to the tool. It counts towards a double click
	// only when the shape was already selected.
	mLastTime = now;
	mLastX = inX;
	mLastY = inY;
	mLastShape = on ? id : -1;
	return 0;
}

int		WED_LayLineLayer::HandleScrollWheel(int inX, int inY, int inDist)
{
	if (!mOpen) return 0;
	const Point2 sp = ToScreen(GetZoomer(), inX, inY);
	if (!Inside(sp.x(), sp.y(), mCard)) return 0;
	mScroll -= inDist * 3;				// wheel up: towards the top of the list
	if (mScroll < 0) mScroll = 0;		// the lower end is clamped when drawn
	return 1;
}

int		WED_LayLineLayer::HandleLayerKeyPress(uint32_t inKey, int inVK, GUI_KeyFlags inFlags)
{
	if (!mOpen || inKey != GUI_KEY_ESCAPE) return 0;
	Close();
	return 1;
}

void	WED_LayLineLayer::DrawSelected(bool inCurrent, GUI_GraphState * g)
{
	if (!mOpen) return;
	// Gone, unselected, or another tool: the card has nothing left to act on.
	WED_Thing * wrl = WED_GetWorld(GetResolver());
	ISelection * sel = WED_GetSelect(GetResolver());
	ISelectable * shape = dynamic_cast<ISelectable *>(wrl ? wrl->FetchPeer(mShapeID) : NULL);
	if (!shape || !sel || !sel->IsSelected(shape) || !ToolAllows() || mRows.empty())
	{
		mOpen = false;
		mRows.clear();
		return;
	}

	const double rot = GetZoomer()->GetViewRotation();
	if (rot != 0)
	{
		double b[4];
		GetZoomer()->GetPixelBounds(b[0], b[1], b[2], b[3]);
		const double cx = (b[0] + b[2]) * 0.5, cy = (b[1] + b[3]) * 0.5;
		glMatrixMode(GL_MODELVIEW);
		glPushMatrix();
		glTranslated(cx, cy, 0);
		glRotated(-rot, 0, 0, 1);
		glTranslated(-cx, -cy, 0);
	}
	glDisable(GL_CULL_FACE);
	DrawCard(g);
	glEnable(GL_CULL_FACE);
	g->SetState(0, 0, 0, 0, 0, 0, 0);
	glLineWidth(1.0f);
	if (rot != 0)
	{
		glMatrixMode(GL_MODELVIEW);
		glPopMatrix();
	}
}

void	WED_LayLineLayer::DrawCard(GUI_GraphState * g)
{
	double b[4];
	GetZoomer()->GetPixelBounds(b[0], b[1], b[2], b[3]);
	const Point2 a = GetZoomer()->MapPixelToScreen(GetZoomer()->LLToPixel(mAnchor));
	const float lh = LineH(), asc = Asc();

	// width: the longest name, within reason
	float w = TextW("Lay line along shape") + TextW("0000") + 3 * kPad;
	for (const Row & r : mRows)
		w = Max(w, TextW(r.label) + (r.folder ? kPad * 2 + 24.0f : kIndent + kPad));
	w = Min(w, 360.0f);

	// height: up to kMaxRows entries, never past the map
	const float avail = (float) (b[3] - b[1]) - 2 * kPad;
	const float list_max = Min(kMaxRows * EntryH(), avail - HeadH() - 2 * kPad);
	// the lowest first row that still fills the list: the wheel stops there
	int max_scroll = (int) mRows.size();
	{
		float hsum = 0;
		while (max_scroll > 0)
		{
			const float h = mRows[max_scroll - 1].folder ? FolderH() : EntryH();
			if (hsum + h > list_max) break;
			hsum += h;
			--max_scroll;
		}
	}
	if (mScroll > max_scroll) mScroll = max_scroll;
	// the list as tall as all its rows, or list_max once they scroll - not only
	// the rows that fit from the top: folder rows are shorter, and the last
	// screenful has to fit as well
	float list_h = 0;
	for (const Row & r : mRows) list_h += r.folder ? FolderH() : EntryH();
	list_h = Min(list_h, list_max);
	const float h = HeadH() + list_h + kPad;

	// beside the shape, to the right if it fits, inside the map
	float x0 = a.x() + kGap;
	if (x0 + w > b[2] - kPad) x0 = a.x() - kGap - w;
	x0 = Max(x0, (float) b[0] + kPad);
	float y1 = a.y() + h * 0.5f;
	y1 = Min(y1, (float) b[3] - kPad);
	y1 = Max(y1, (float) b[1] + kPad + h);
	const float x1 = x0 + w, y0 = y1 - h;
	mCard[0] = x0; mCard[1] = y0; mCard[2] = x1; mCard[3] = y1;

	g->SetState(0, 0, 0, 0, 1, 0, 0);

	// the callout: a white line from the shape to the card's near edge
	{
		const float ex = a.x() < x0 ? x0 : x1;
		const float ey = Max(y0 + 4.0f, Min(y1 - 4.0f, a.y()));
		glColor4f(0, 0, 0, 0.6f);
		glLineWidth(4.0f);
		glBegin(GL_LINES); glVertex2f(a.x(), a.y()); glVertex2f(ex, ey); glEnd();
		glColor4fv(kWhite);
		glLineWidth(2.0f);
		glBegin(GL_LINES); glVertex2f(a.x(), a.y()); glVertex2f(ex, ey); glEnd();
		glLineWidth(1.0f);
		Fill(a.x() - 3, a.y() - 3, a.x() + 3, a.y() + 3, kWhite);
	}

	Fill(x0, y0, x1, y1, kFill);
	glColor4fv(kWhite);
	glBegin(GL_LINE_LOOP);
	glVertex2f(x0, y0); glVertex2f(x1, y0); glVertex2f(x1, y1); glVertex2f(x0, y1);
	glEnd();

	// header: what it does, and how many lines there are to pick from
	int n_lines = 0;
	for (const Row & r : mRows) if (!r.folder) ++n_lines;
	char count[16];
	snprintf(count, sizeof(count), "%d", n_lines);
	const float head_base = y1 - HeadH() + (HeadH() - asc) * 0.5f;
	Txt(g, kWhite, x0 + kPad, head_base, "Lay line along shape");
	Txt(g, kMuted, x1 - kPad, head_base, count, align_Right);
	g->SetState(0, 0, 0, 0, 1, 0, 0);
	glColor4fv(kRule);
	glBegin(GL_LINES); glVertex2f(x0, y1 - HeadH()); glVertex2f(x1, y1 - HeadH()); glEnd();

	// the hover row, from where the mouse is now
	int mx, my;
	GetHost()->GetMouseLocNow(&mx, &my);
	const Point2 mp = GetZoomer()->MapPixelToScreen(Point2(mx, my));
	mHover = -1;

	mListTop = y1 - HeadH();
	float top = mListTop;
	mShown = 0;
	for (int i = mScroll; i < (int) mRows.size(); ++i)
	{
		const Row & r = mRows[i];
		const float rh = r.folder ? FolderH() : EntryH();
		if (top - rh < y0 + kPad - 0.5f) break;
		const float base = top - rh + (rh - asc) * 0.5f;
		if (r.folder)
		{
			// a thin rule with the folder's name at its left
			Txt(g, kMuted, x0 + kPad, base, r.label.c_str());
			g->SetState(0, 0, 0, 0, 1, 0, 0);
			const float lx = x0 + kPad + TextW(r.label) + 6.0f;
			if (lx < x1 - kPad)
			{
				glColor4fv(kRule);
				glBegin(GL_LINES); glVertex2f(lx, top - rh * 0.5f); glVertex2f(x1 - kPad, top - rh * 0.5f); glEnd();
			}
		}
		else
		{
			const bool hover = mp.x() >= x0 && mp.x() <= x1 && mp.y() <= top && mp.y() > top - rh;
			if (hover)
			{
				mHover = i;
				g->SetState(0, 0, 0, 0, 1, 0, 0);
				Fill(x0 + 1, top - rh, x1 - 1, top, kHoverBg);
			}
			Txt(g, hover ? kAmber : kWhite, x0 + kIndent, base, r.label.c_str());
		}
		top -= rh;
		++mShown;
	}

	// a scroll bar when the list is longer than the card
	if (max_scroll > 0)
	{
		const float t0 = y1 - HeadH() - 2, t1 = y0 + kPad;
		const float frac = (float) mShown / (float) mRows.size();
		const float pos = (float) mScroll / (float) max_scroll;
		const float th = Max(12.0f, (t0 - t1) * frac);
		const float ty1 = t0 - (t0 - t1 - th) * pos;
		g->SetState(0, 0, 0, 0, 1, 0, 0);
		Fill(x1 - 4, ty1 - th, x1 - 2, ty1, kMuted);
	}
}
