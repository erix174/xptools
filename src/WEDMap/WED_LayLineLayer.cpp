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
#include "GUI_Prefs.h"
#include "WED_Document.h"
#include "WED_Globals.h"

#include <cmath>
#include <cstdlib>
#include <cstring>
#include <cctype>
#include <algorithm>
#include <iterator>
#include <map>

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
static const int	kAnyShape = -2;				// mLastShape: the first click selected what it hit

static const float kWhite[4]  = { 0.93f, 0.93f, 0.93f, 1.0f };
static const float kMuted[4]  = { 0.62f, 0.62f, 0.64f, 1.0f };
static const float kRule[4]   = { 0.62f, 0.62f, 0.64f, 0.55f };
static const float kAmber[4]  = { 1.00f, 0.75f, 0.30f, 1.0f };
static const float kHoverBg[4] = { 1.00f, 0.75f, 0.30f, 0.18f };
static const float kFill[4]   = { 0.08f, 0.08f, 0.10f, 0.88f };	// a dark card: the list is read, not glanced at
static const float kPad = 8.0f;
static const float kIndent = 14.0f;
static const float kCardW = 540.0f;
static const float kGap = 48.0f;		// from the anchor to the card
static const int   kMaxLines = 22;	// list lines, when the map has the room
static const int   kMaxRecent = 40;	// kept in the prefs; shown: as many as fit
static const int   kAllEdgesHit = -2;		// Hit::entry of the "All edges" choice
static const int   kExposedEdgesHit = -3;	// Hit::entry of "Exposed edges only"
static const int   kCloseHit = -4;			// Hit::entry of the X in the corner

// Not std::max: <windows.h>, force-included through XDefs.h, defines min/max macros.
static inline float	Max(float a, float b) { return a > b ? a : b; }
static inline float	Min(float a, float b) { return a < b ? a : b; }

static float	TextW(const string & s)
{
	return s.empty() ? 0.0f : GUI_MeasureRange(font_UI_Basic, s.c_str(), s.c_str() + s.size());
}

// A name too long for its cell ends in "..." rather than running off the card.
static string	Elide(const string & s, float w)
{
	if (TextW(s) <= w) return s;
	string t = s;
	while (!t.empty() && TextW(t + "...") > w) t.erase(t.size() - 1);
	return t + "...";
}

static float	LineH(void)		{ return GUI_GetLineHeight(font_UI_Basic); }
static float	Asc(void)		{ return GUI_GetLineAscent(font_UI_Basic); }
static float	EntryH(void)	{ return LineH() + 3.0f; }
static float	FolderH(void)	{ return LineH() + 1.0f; }
static float	HeadH(void)		{ return LineH() + 8.0f; }
static float	BarH(void)		{ return LineH() + 8.0f; }	// the search field, a row of tabs

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

// The container folders above the meaningful ones: dropped, not classified.
static const char * kContainers[] = { "lib/airport/ground/", "lib/airport/", "lib/g10/", "lib/" };
// Not listed: apt.dat's own painted taxi lines, drawn with the Taxilines tool, and
// the XP10 terrain lines, kept for old sceneries but not for new work.
static const char * kLeftOut[] = { "lib/airport/lines/", "lib/g10/terrain10/" };

static string	Lower(const string & s)
{
	string o(s);
	for (char & c : o) c = (char) tolower((unsigned char) c);
	return o;
}

static string	Spaced(const string & path)		// "a/b" -> "a / b"
{
	string out;
	for (char c : path)
		if (c == '/') out += " / ";
		else out += c;
	return out;
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

static vector<string>	RecentList(void)
{
	vector<string> out;
	string cur;
	for (char c : gLayLineRecent)
		if (c == ';') { if (!cur.empty()) out.push_back(cur); cur.clear(); }
		else cur += c;
	if (!cur.empty()) out.push_back(cur);
	return out;
}

WED_LayLineLayer::WED_LayLineLayer(GUI_Pane * host, WED_MapZoomerNew * zoomer, IResolver * resolver) :
	WED_MapLayer(host, zoomer, resolver), mLastTime(-9.9e9f), mLastX(0), mLastY(0), mLastShape(-1),
	mOpen(false), mShapeID(-1), mStretch(false), mTab(0), mScroll(0)
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

// Every public library .lin, named from its virtual path alone (see the header).
void	WED_LayLineLayer::BuildEntries(void)
{
	mEntries.clear();
	mTabs.clear();
	WED_LibraryMgr * lib = WED_GetLibraryMgr(GetResolver());
	if (!lib) return;

	// depth first, in library order: a folder's own lines, then its sub-folders
	vector<string> vpaths, todo(1, string());
	while (!todo.empty())
	{
		string dir = todo.back();
		todo.pop_back();
		vector<string> kids, dirs;
		lib->GetResourceChildren(dir, pack_Library, kids);
		for (const string & k : kids)
		{
			const res_type t = lib->GetResourceType(k);
			if (t == res_Directory) { dirs.push_back(k); continue; }
			if (t != res_Line || !lib->IsResourceLibrary(k) || lib->IsResourceDeprecatedOrPrivate(k)) continue;
			if (std::any_of(std::begin(kLeftOut), std::end(kLeftOut),
							[&](const char * l) { return k.compare(0, strlen(l), l) == 0; })) continue;
			vpaths.push_back(k);
		}
		todo.insert(todo.end(), dirs.rbegin(), dirs.rend());
	}

	vector<string> dirs;		// each entry's folder below its tab, before folding
	for (const string & v : vpaths)
	{
		string rest = v;
		for (const char * c : kContainers)
			if (rest.compare(0, strlen(c), c) == 0) { rest.erase(0, strlen(c)); break; }
		Entry e;
		e.vpath = v;
		size_t slash = rest.find('/');
		string within;
		if (slash == string::npos) { e.tab = "other"; within = rest; }
		else { e.tab = rest.substr(0, slash); within = rest.substr(slash + 1); }
		size_t last = within.find_last_of('/');
		e.label = last == string::npos ? within : within.substr(last + 1);
		if (e.label.size() > 4 && e.label.compare(e.label.size() - 4, 4, ".lin") == 0)
			e.label.erase(e.label.size() - 4);
		dirs.push_back(last == string::npos ? string() : within.substr(0, last));
		mEntries.push_back(e);
		if (std::find(mTabs.begin(), mTabs.end(), e.tab) == mTabs.end())
			mTabs.push_back(e.tab);
	}

	// a folder with a single line folds into its parent, until none is left -
	// but not into the tab itself, where it would read as the group above it
	for (bool again = true; again; )
	{
		again = false;
		std::map<string, int> count;
		for (size_t i = 0; i < mEntries.size(); ++i) ++count[mEntries[i].tab + "/" + dirs[i]];
		for (size_t i = 0; i < mEntries.size(); ++i)
			if (dirs[i].find('/') != string::npos && count[mEntries[i].tab + "/" + dirs[i]] == 1)
			{
				size_t last = dirs[i].find_last_of('/');
				mEntries[i].label = (last == string::npos ? dirs[i] : dirs[i].substr(last + 1)) + " " + mEntries[i].label;
				dirs[i] = last == string::npos ? string() : dirs[i].substr(0, last);
				again = true;
			}
	}
	for (size_t i = 0; i < mEntries.size(); ++i)
	{
		mEntries[i].group = Spaced(dirs[i]);
		mEntries[i].key = Lower(mEntries[i].tab + "/" + dirs[i] + "/" + mEntries[i].label + " " + mEntries[i].vpath);
	}
}

// The list for what is shown now - search results, Recent, or one tab - cut
// into lines for the card's width: a group's name, then its entries side by
// side, as many to a row as the group's widest name allows.
void	WED_LayLineLayer::LayOut(float inner_w, vector<Line>& out) const
{
	out.clear();
	vector<int> picks;
	const bool searching = !mQuery.empty();
	if (searching)
	{
		vector<string> terms;
		string cur;
		for (char c : Lower(mQuery) + " ")
			if (c == ' ') { if (!cur.empty()) terms.push_back(cur); cur.clear(); }
			else cur += c;
		for (int i = 0; i < (int) mEntries.size(); ++i)
		{
			bool all = true;
			for (const string & t : terms)
				if (mEntries[i].key.find(t) == string::npos) { all = false; break; }
			if (all) picks.push_back(i);
		}
	}
	else if (mTab == 0)
	{
		for (const string & v : RecentList())
			for (int i = 0; i < (int) mEntries.size(); ++i)
				if (mEntries[i].vpath == v)
				{
					Line l;
					l.cells.push_back(i);
					l.full = true;
					out.push_back(l);
					break;
				}
		return;
	}
	else if (mTab <= (int) mTabs.size())
	{
		for (int i = 0; i < (int) mEntries.size(); ++i)
			if (mEntries[i].tab == mTabs[mTab - 1]) picks.push_back(i);
	}

	size_t g0 = 0;
	while (g0 < picks.size())
	{
		const Entry & first = mEntries[picks[g0]];
		size_t g1 = g0;
		float cw = 0;
		while (g1 < picks.size() && mEntries[picks[g1]].tab == first.tab && mEntries[picks[g1]].group == first.group)
			cw = Max(cw, TextW(mEntries[picks[g1++]].label));
		const string header = searching ? (first.group.empty() ? first.tab : first.tab + " / " + first.group) : first.group;
		if (!header.empty())
		{
			Line h;
			h.header = header;
			h.full = false;
			out.push_back(h);
		}
		int cols = (int) ((inner_w - kIndent) / (cw + 16.0f));
		if (cols < 1) cols = 1;
		for (size_t i = g0; i < g1; i += cols)
		{
			Line l;
			l.full = false;
			l.cols = cols;
			for (size_t j = i; j < g1 && j < i + cols; ++j) l.cells.push_back(picks[j]);
			out.push_back(l);
		}
		g0 = g1;
	}
}

void	WED_LayLineLayer::Open(int shape_id, const Point2& click_ll)
{
	mOpen = true;
	mShapeID = shape_id;
	mStretch = false;
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
	BuildEntries();
	mQuery.clear();
	mScroll = 0;
	// Recent first, once there is something in it
	mTab = 0;
	{
		vector<Line> l;
		LayOut(kCardW, l);
		if (l.empty() && !mTabs.empty()) mTab = 1;
	}
	mHits.clear();
	mCard[0] = mCard[1] = mCard[2] = mCard[3] = 0;
	GetHost()->Refresh();
}

void	WED_LayLineLayer::Close(void)
{
	if (!mOpen) return;
	mOpen = false;
	mShapeID = -1;
	mEntries.clear();
	mHits.clear();
	GetHost()->Refresh();
}

// The menu command is for polygons only (Julian): a selected line does not turn it on
static WED_Thing * first_selected_polygon(IResolver * resolver)
{
	ISelection * sel = WED_GetSelect(resolver);
	vector<ISelectable *> selected;
	sel->GetSelectionVector(selected);
	for (ISelectable * s : selected)
	{
		WED_Thing * t = dynamic_cast<WED_Thing *>(s);
		IGISPolygon * poly = dynamic_cast<IGISPolygon *>(s);
		if (!t || !poly || poly->GetGISClass() != gis_Polygon) continue;
		IGISPointSequence * ps = poly->GetOuterRing();
		if (ps && ps->GetNumPoints() >= 2) return t;
	}
	return NULL;
}

int		WED_LayLineLayer::CanOpenForSelection(void)
{
	return first_selected_polygon(GetResolver()) != NULL || WED_GetLayLineStretch(GetResolver(), NULL, NULL, NULL);
}

// The card for two selected nodes, its callout from the middle node of the stretch;
// else for the first selected polygon, its callout from the middle of its bounds
void	WED_LayLineLayer::OpenForSelection(void)
{
	IGISPointSequence * ps;
	int first, count;
	if (WED_GetLayLineStretch(GetResolver(), &ps, &first, &count))
	{
		const int np = ps->GetNumPoints();
		WED_Thing * node = dynamic_cast<WED_Thing *>(ps->GetNthPoint(first));
		Point2 mid;
		ps->GetNthPoint((first + count / 2) % np)->GetLocation(gis_Geo, mid);
		if (!node) return;
		Open(node->GetID(), mid);
		mStretch = true;
		return;
	}
	WED_Thing * t = first_selected_polygon(GetResolver());
	if (!t) return;
	Bbox2 b;
	dynamic_cast<IGISEntity *>(t)->GetBounds(gis_Geo, b);
	Open(t->GetID(), b.centroid());
}

void	WED_LayLineLayer::Pick(int entry)
{
	if (entry < 0 || entry >= (int) mEntries.size()) return;
	const string vpath = mEntries[entry].vpath;
	vector<string> recent = RecentList();
	recent.erase(std::remove(recent.begin(), recent.end(), vpath), recent.end());
	recent.insert(recent.begin(), vpath);
	if (recent.size() > (size_t) kMaxRecent) recent.resize(kMaxRecent);
	gLayLineRecent.clear();
	for (const string & r : recent)
	{
		if (!gLayLineRecent.empty()) gLayLineRecent += ";";
		gLayLineRecent += r;
	}
	WED_Document::WriteGlobalPrefs();
	GUI_Prefs_Write("WED");
	Close();
	WED_DoLayLineAlong(GetResolver(), vpath, gLayLineSkipShared != 0);
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
			for (const Hit & h : mHits)
			{
				if (x < h.x0 || x > h.x1 || y < h.y0 || y > h.y1) continue;
				if (h.entry >= 0) { Pick(h.entry); break; }
				if (h.entry == kCloseHit)
				{
					Close();
					break;
				}
				if (h.entry == kAllEdgesHit || h.entry == kExposedEdgesHit)
				{
					gLayLineSkipShared = h.entry == kExposedEdgesHit;
					WED_Document::WriteGlobalPrefs();
					GUI_Prefs_Write("WED");
					GetHost()->Refresh();
					break;
				}
				if (h.tab >= 0)
				{
					mTab = h.tab;
					mQuery.clear();
					mScroll = 0;
					GetHost()->Refresh();
					break;
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
	// The first click goes on to the tool, and counts towards a double click on
	// the shape it was already on, or on whatever the tool selects with it.
	if (on && mLastShape == kAnyShape && now - mLastTime < kDoubleClickTime &&
		abs(inX - mLastX) <= kDoubleClickDist && abs(inY - mLastY) <= kDoubleClickDist)
	{
		mLastShape = -1;
		Open(id, GetZoomer()->PixelToLL(Point2(inX, inY)));
		return 1;
	}
	mLastTime = now;
	mLastX = inX;
	mLastY = inY;
	mLastShape = on ? id : kAnyShape;
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

// While the card is open, typing goes to its search field - letters would
// otherwise pick tools, and Backspace delete the selection.
int		WED_LayLineLayer::HandleLayerKeyPress(uint32_t inKey, int inVK, GUI_KeyFlags inFlags)
{
	if (!mOpen) return 0;
	if (inFlags & (gui_ControlFlag | gui_OptionAltFlag)) return 0;		// menu shortcuts
	const bool down = (inFlags & gui_DownFlag) != 0;
	switch (inKey) {
	case GUI_KEY_ESCAPE:
		if (down)
		{
			if (mQuery.empty()) Close();
			else { mQuery.clear(); mScroll = 0; }
		}
		return 1;
	case GUI_KEY_BACK:
	case GUI_KEY_DELETE:
		if (down && !mQuery.empty()) { mQuery.erase(mQuery.size() - 1); mScroll = 0; }
		return 1;
	case GUI_KEY_RETURN:
		if (down && !mQuery.empty())
		{
			// the first match
			vector<Line> l;
			LayOut(kCardW, l);
			for (const Line & ln : l)
				if (!ln.cells.empty()) { Pick(ln.cells.front()); break; }
		}
		return 1;
	default:
		if (inKey >= 32 && inKey < 127)
		{
			if (down && mQuery.size() < 40) { mQuery += (char) inKey; mScroll = 0; }
			return 1;
		}
		return 0;
	}
}

void	WED_LayLineLayer::DrawSelected(bool inCurrent, GUI_GraphState * g)
{
	if (!mOpen) return;
	// Gone, unselected, or another tool: the card has nothing left to act on.
	WED_Thing * wrl = WED_GetWorld(GetResolver());
	ISelection * sel = WED_GetSelect(GetResolver());
	ISelectable * shape = dynamic_cast<ISelectable *>(wrl ? wrl->FetchPeer(mShapeID) : NULL);
	if (!shape || !sel || !sel->IsSelected(shape) || !ToolAllows() || mEntries.empty() ||
		(mStretch && !WED_GetLayLineStretch(GetResolver(), NULL, NULL, NULL)))
	{
		mOpen = false;
		mEntries.clear();
		mHits.clear();
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
	const float asc = Asc();
	const float w = Min(kCardW, (float) (b[2] - b[0]) - 2 * kPad);
	const float inner_w = w - 2 * kPad;

	// the tabs, wrapped onto as many rows as they need
	struct TabBox { int tab; string label; float x, row; };
	vector<TabBox> tabs;
	{
		float x = 0, row = 0;
		for (int t = 0; t <= (int) mTabs.size(); ++t)
		{
			const string label = t == 0 ? string("Recent") : mTabs[t - 1];
			const float tw = TextW(label) + 12.0f;
			if (x > 0 && x + tw > inner_w) { x = 0; row += 1; }
			TabBox tb = { t, label, x, row };
			tabs.push_back(tb);
			x += tw + 4.0f;
		}
	}
	const int tab_rows = (int) tabs.back().row + 1;
	const float top_h = HeadH() + BarH() + tab_rows * BarH() + 4.0f;

	vector<Line> lines;
	LayOut(inner_w, lines);

	// one size for every tab, so the card does not jump about: kMaxLines entry
	// lines, or what the map has room for
	const float avail = (float) (b[3] - b[1]) - 2 * kPad;
	const float list_h = Max(EntryH() * 3, Min(kMaxLines * EntryH(), avail - top_h - kPad));
	const float h = top_h + list_h + kPad;
	auto line_h = [](const Line & l) { return l.header.empty() ? EntryH() : FolderH(); };

	// the first line that still fills the list: the wheel stops there. Recent
	// shows as many as fit and does not scroll.
	int max_scroll = (int) lines.size();
	{
		float hsum = 0;
		while (max_scroll > 0 && hsum + line_h(lines[max_scroll - 1]) <= list_h)
			hsum += line_h(lines[--max_scroll]);
	}
	if (mQuery.empty() && mTab == 0) max_scroll = 0;
	if (mScroll > max_scroll) mScroll = max_scroll;

	// beside the shape, to the right if it fits, inside the map
	float x0 = a.x() + kGap;
	if (x0 + w > b[2] - kPad) x0 = a.x() - kGap - w;
	x0 = Max(x0, (float) b[0] + kPad);
	float y1 = a.y() + h * 0.5f;
	y1 = Min(y1, (float) b[3] - kPad);
	y1 = Max(y1, (float) b[1] + kPad + h);
	const float x1 = x0 + w, y0 = y1 - h;
	mCard[0] = x0; mCard[1] = y0; mCard[2] = x1; mCard[3] = y1;
	mHits.clear();

	int mx, my;
	GetHost()->GetMouseLocNow(&mx, &my);
	const Point2 mp = GetZoomer()->MapPixelToScreen(Point2(mx, my));
	auto over = [&](float bx0, float by0, float bx1, float by1) {
		return mp.x() >= bx0 && mp.x() <= bx1 && mp.y() >= by0 && mp.y() <= by1; };

	g->SetState(0, 0, 0, 0, 1, 0, 0);

	// a stretch: the edges the line will follow, amber
	IGISPointSequence * ps;
	int first, count;
	if (mStretch && WED_GetLayLineStretch(GetResolver(), &ps, &first, &count))
	{
		const int np = ps->GetNumPoints();
		for (int pass = 0; pass < 2; ++pass)
		{
			if (pass) glColor4fv(kAmber); else glColor4f(0, 0, 0, 0.6f);
			glLineWidth(pass ? 3.0f : 6.0f);
			glBegin(GL_LINE_STRIP);
			for (int k = 0; k < count; ++k)
			{
				Point2 ll;
				ps->GetNthPoint((first + k) % np)->GetLocation(gis_Geo, ll);
				const Point2 sp = GetZoomer()->MapPixelToScreen(GetZoomer()->LLToPixel(ll));
				glVertex2f(sp.x(), sp.y());
			}
			glEnd();
		}
		glLineWidth(1.0f);
	}

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

	// title, and an X in the corner that closes the card
	float y = y1;
	Txt(g, kWhite, x0 + kPad, y - HeadH() + (HeadH() - asc) * 0.5f, "Snap FX to Polygon");
	const float xs = HeadH() - 8.0f;
	{
		const float bx1 = x1 - kPad + 4.0f, bx0 = bx1 - xs, by1 = y - 4.0f, by0 = by1 - xs;
		const bool hov = over(bx0, by0, bx1, by1);
		g->SetState(0, 0, 0, 0, 1, 0, 0);
		if (hov) Fill(bx0, by0, bx1, by1, kHoverBg);
		const float m = xs * 0.28f;
		glColor4fv(hov ? kAmber : kMuted);
		glLineWidth(1.5f);
		glBegin(GL_LINES);
		glVertex2f(bx0 + m, by0 + m); glVertex2f(bx1 - m, by1 - m);
		glVertex2f(bx0 + m, by1 - m); glVertex2f(bx1 - m, by0 + m);
		glEnd();
		glLineWidth(1.0f);
		Hit hit = { bx0, by0, bx1, by1, kCloseHit, -1 };
		mHits.push_back(hit);
	}

	// "All edges" or "Exposed edges only", left of the X, kept in the prefs;
	// drawn like the tabs. A stretch is laid as picked.
	if (!mStretch)
	{
		struct Choice { const char * label; int hit; bool cur; };
		const Choice choices[2] = {
			{ "All edges", kAllEdgesHit, gLayLineSkipShared == 0 },
			{ "Exposed edges only", kExposedEdgesHit, gLayLineSkipShared != 0 } };
		float cx1 = x1 - kPad - xs - 10.0f;
		const float by1 = y - 4.0f, by0 = y - HeadH() + 4.0f;
		for (int c = 1; c >= 0; --c)
		{
			const float cx0 = cx1 - TextW(choices[c].label) - 12.0f;
			const bool hov = over(cx0, by0, cx1, by1);
			g->SetState(0, 0, 0, 0, 1, 0, 0);
			if (choices[c].cur || hov) Fill(cx0, by0, cx1, by1, choices[c].cur ? kRule : kHoverBg);
			Txt(g, choices[c].cur ? kWhite : (hov ? kAmber : kMuted), cx0 + 6, by0 + (by1 - by0 - asc) * 0.5f, choices[c].label);
			Hit hit = { cx0, by0, cx1, by1, choices[c].hit, -1 };
			mHits.push_back(hit);
			cx1 = cx0 - 2.0f;
		}
	}
	y -= HeadH();

	// the search field: typing goes here while the card is open
	{
		const float fy0 = y - BarH() + 2, fy1 = y - 2;
		g->SetState(0, 0, 0, 0, 1, 0, 0);
		Fill(x0 + kPad, fy0, x1 - kPad, fy1, kHoverBg);
		glColor4fv(mQuery.empty() ? kRule : kAmber);
		glBegin(GL_LINE_LOOP);
		glVertex2f(x0 + kPad, fy0); glVertex2f(x1 - kPad, fy0); glVertex2f(x1 - kPad, fy1); glVertex2f(x0 + kPad, fy1);
		glEnd();
		const float base = fy0 + (fy1 - fy0 - asc) * 0.5f;
		if (mQuery.empty())
			Txt(g, kMuted, x0 + kPad + 6, base, "Type to search all lines");
		else
			Txt(g, kWhite, x0 + kPad + 6, base, (mQuery + "_").c_str());
		y -= BarH();
	}

	// the tabs; a search looks through all of them, so then none is current
	{
		const float ty1 = y - 2;
		for (const TabBox & t : tabs)
		{
			const float tx0 = x0 + kPad + t.x, tx1 = tx0 + TextW(t.label) + 12.0f;
			const float by1 = ty1 - t.row * BarH(), by0 = by1 - BarH() + 4;
			const bool cur = mQuery.empty() && t.tab == mTab;
			const bool hov = over(tx0, by0, tx1, by1);
			g->SetState(0, 0, 0, 0, 1, 0, 0);
			if (cur || hov) Fill(tx0, by0, tx1, by1, cur ? kRule : kHoverBg);
			Txt(g, cur ? kWhite : (hov ? kAmber : kMuted), tx0 + 6, by0 + (by1 - by0 - asc) * 0.5f, t.label.c_str());
			Hit hit = { tx0, by0, tx1, by1, -1, t.tab };
			mHits.push_back(hit);
		}
		y -= tab_rows * BarH() + 4.0f;
		g->SetState(0, 0, 0, 0, 1, 0, 0);
		glColor4fv(kRule);
		glBegin(GL_LINES); glVertex2f(x0, y); glVertex2f(x1, y); glEnd();
	}

	const float list_top = y, list_bottom = y0 + kPad;
	if (lines.empty())
	{
		const char * none = !mQuery.empty() ? "No line matches" :
							mTab == 0 ? "Lines you lay show up here" : "";
		Txt(g, kMuted, x0 + kPad, list_top - EntryH() + (EntryH() - asc) * 0.5f, none);
	}
	float top = list_top;
	int shown = 0;
	for (int i = mScroll; i < (int) lines.size(); ++i)
	{
		const Line & l = lines[i];
		const float rh = line_h(l);
		if (top - rh < list_bottom - 0.5f) break;
		const float base = top - rh + (rh - asc) * 0.5f;
		if (!l.header.empty())
		{
			// a group: a thin rule with its folders' names at the left
			Txt(g, kMuted, x0 + kPad, base, l.header.c_str());
			g->SetState(0, 0, 0, 0, 1, 0, 0);
			const float lx = x0 + kPad + TextW(l.header) + 6.0f;
			if (lx < x1 - kPad)
			{
				glColor4fv(kRule);
				glBegin(GL_LINES); glVertex2f(lx, top - rh * 0.5f); glVertex2f(x1 - kPad, top - rh * 0.5f); glEnd();
			}
		}
		else
		{
			// the group's columns share the row evenly
			const float cell_w = l.full ? inner_w : (inner_w - kIndent) / (float) l.cols;
			float cx = x0 + kPad + (l.full ? 0.0f : kIndent);
			for (int c : l.cells)
			{
				const Entry & e = mEntries[c];
				const string label = !l.full ? e.label :
									 e.group.empty() ? e.tab + " / " + e.label : e.tab + " / " + e.group + " / " + e.label;
				const float bx0 = cx - 4, bx1 = Min(cx + cell_w - 4, x1 - 2);
				const bool hov = over(bx0, top - rh, bx1, top);
				if (hov)
				{
					g->SetState(0, 0, 0, 0, 1, 0, 0);
					Fill(bx0, top - rh, bx1, top, kHoverBg);
				}
				Txt(g, hov ? kAmber : kWhite, cx, base, Elide(label, bx1 - cx - 2).c_str());
				Hit hit = { bx0, top - rh, bx1, top, c, -1 };
				mHits.push_back(hit);
				cx += cell_w;
			}
		}
		top -= rh;
		++shown;
	}

	// a scroll bar when the list is longer than the card
	if (max_scroll > 0 && !lines.empty())
	{
		const float t0 = list_top - 2, t1 = list_bottom;
		const float frac = (float) shown / (float) lines.size();
		const float pos = (float) mScroll / (float) max_scroll;
		const float th = Max(12.0f, (t0 - t1) * frac);
		const float ty1 = t0 - (t0 - t1 - th) * pos;
		g->SetState(0, 0, 0, 0, 1, 0, 0);
		Fill(x1 - 4, ty1 - th, x1 - 2, ty1, kMuted);
	}
}
