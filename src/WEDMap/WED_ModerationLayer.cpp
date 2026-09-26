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

#include "WED_ModerationLayer.h"
#include "WED_Airport.h"
#include "WED_RampPosition.h"
#include "WED_ToolUtils.h"
#include "WED_EnumSystem.h"
#include "WED_MapZoomerNew.h"
#include "WED_LiveryRules.h"
#include "WED_FlagIndex.h"
#include "WED_FlagAssets.h"
#include "GUI_Fonts.h"
#include "GUI_GraphState.h"
#include "GUI_Pane.h"
#include <chrono>
#include "WED_Map.h"
#include "GUI_Clipboard.h"
#include "ISelection.h"
#include "IOperation.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <set>

#if APL
	#include <OpenGL/gl.h>
#else
	#include "glew.h"		// GL_BGRA for the flag upload, as the Liveries tab does
#endif

using std::string;
using std::vector;
using std::set;

static bool				sModerationView = false;
static std::set<int>	sIssueIDs;			// Moderation View, last frame: the stands to check

bool	WED_ModerationViewOn(void)			{ return sModerationView; }
void	WED_SetModerationView(bool on)		{ sModerationView = on; }

bool	WED_ModerationTintFor(const WED_RampPosition * ramp, float out_rgb[3], float * alpha_scale)
{
	if (!ramp || !WED_ModerationEnabled()) return false;
	if (alpha_scale) *alpha_scale = 1.0f;
	// op type None parks nothing: a grey outline, whatever its list says
	if (ramp->GetRampOperationType() == ramp_operation_None)
	{
		out_rgb[0] = out_rgb[1] = out_rgb[2] = 0.62f;
		if (alpha_scale) *alpha_scale = 0.6f;
		return true;
	}
	if (sModerationView && !sIssueIDs.count(ramp->GetID()))
	{
		out_rgb[0] = out_rgb[1] = out_rgb[2] = 0.55f;
		if (alpha_scale) *alpha_scale = 0.45f;
		return true;
	}
	float rgba[4];
	WED_ModerationColour(WED_ModerationSignature(const_cast<WED_RampPosition *>(ramp)), rgba);
	out_rgb[0] = rgba[0]; out_rgb[1] = rgba[1]; out_rgb[2] = rgba[2];
	return true;
}

// Density tiers, by how many stands are selected (see the header).
static const size_t kMaxCards = 5;
static const size_t kMaxChips = 40;
static const size_t kMaxLegendRows = 24;
// The map's own pan/zoom buttons sit in its top-right corner; the chip column
// and the legend start below them.
static const float kTopClear = 64.0f;

static const float kWhite[4]  = { 0.93f, 0.93f, 0.93f, 1.0f };
static const float kMuted[4]  = { 0.66f, 0.66f, 0.68f, 1.0f };
static const float kGreen[4]  = { 0.45f, 0.85f, 0.45f, 1.0f };
static const float kRed[4]    = { 1.00f, 0.45f, 0.40f, 1.0f };
static const float kAmber[4]  = { 1.00f, 0.75f, 0.30f, 1.0f };
static const float kFill[4]   = { 0.0f, 0.0f, 0.0f, 0.4f };		// the card body: 40% black

// Not std::max: <windows.h>, force-included through XDefs.h, defines min/max macros.
static inline float	Max(float a, float b) { return a > b ? a : b; }
static inline float	Min(float a, float b) { return a < b ? a : b; }

static float	TextW(const string & s)
{
	return s.empty() ? 0.0f : GUI_MeasureRange(font_UI_Basic, s.c_str(), s.c_str() + s.size());
}

static string	Elide(const string & s, float w)
{
	if (TextW(s) <= w) return s;
	string t = s;
	while (!t.empty() && TextW(t + "...") > w) t.erase(t.size() - 1);
	return t + "...";
}

// Text over the map: a 1-pixel black shadow keeps it legible on the 40% fill and
// on the bare imagery behind a header row.
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

static void	Circle(float cx, float cy, float r, bool filled)
{
	glBegin(filled ? GL_TRIANGLE_FAN : GL_LINE_LOOP);
	if (filled) glVertex2f(cx, cy);
	const int n = 20;
	for (int i = 0; i < (filled ? n + 1 : n); ++i)
	{
		float a = (float) i / (float) n * 6.2831853f;
		glVertex2f(cx + cosf(a) * r, cy + sinf(a) * r);
	}
	glEnd();
}

// The mouse where the overlays are: in the screen's frame. The host (WED_Map)
// reports map pixels, which differ once the view is turned.
static void	MouseOnScreen(WED_MapZoomerNew * z, GUI_Pane * host, int& x, int& y)
{
	host->GetMouseLocNow(&x, &y);
	Point2 p = z->MapPixelToScreen(Point2(x, y));
	x = (int) floor(p.x() + 0.5); y = (int) floor(p.y() + 0.5);
}

static bool	Inside(float x, float y, float x0, float y0, float x1, float y1)
{
	return x >= x0 && x <= x1 && y >= y0 && y <= y1;
}

static float	LineH(void)		{ return GUI_GetLineHeight(font_UI_Basic); }
static float	Asc(void)		{ return GUI_GetLineAscent(font_UI_Basic); }
static float	RowH(void)		{ return LineH() + 3.0f; }
static float	HeadH(void)		{ return LineH() + 8.0f; }
static const float kPad = 6.0f;

// THE NECK: every leader of a callout meets at one hub, and a single segment - the
// neck - runs from the hub to the card or chip. Its length and weight say how
// many stands share the entry: 2.5x the base for one stand, 1.5x more for each
// further stand, capped at 20x, so a popular entry stands out before a word is
// read. Its weight runs 3 -> 16 px, and the receiving bar on the card or chip
// takes the same weight, growing outward (left) so it never covers the content.
static float	NeckLen(size_t stands, float base)
{
	float f = 2.5f + 1.5f * (float) (stands - 1);
	return base * (f > 20.0f ? 20.0f : f);
}

static float	NeckWidth(size_t stands)
{
	float w = 3.0f + 0.7f * (float) (stands - 1);
	return w > 16.0f ? 16.0f : w;
}

// Leaders from every stand to the hub, an arrowhead at each stand, and the neck
// from the hub to end_x (either side: the neck runs toward end_x).
//
// Each leader is a cubic that LEAVES ITS STAND HEADING FOR THE HUB and bends
// only at the far end, to arrive level and run on into the neck. Two earlier
// shapes were worse: a straight line met the level neck at a hard corner, and
// an S-curve that left every stand level ran the leaders along the row of
// stands on top of one another before they split, and doubled back on itself
// for any stand past the hub. The level run-in is only as long as the stand is
// far from the hub on the neck's side - never behind it - so no leader turns
// back; a stand at or past the hub gets a straight line.
static void	DrawLeaders(const vector<std::pair<float, float> > & stands, float hub_x, float hub_y, float end_x,
							const float col[4], float line_w, float arrow)
{
	const float dir = end_x >= hub_x ? 1.0f : -1.0f;		// the way the neck runs
	glColor4fv(col);
	glLineWidth(line_w);
	for (size_t k = 0; k < stands.size(); ++k)
	{
		const float sx = stands[k].first, sy = stands[k].second;
		const float c1x = sx + (hub_x - sx) * 0.35f, c1y = sy + (hub_y - sy) * 0.35f;
		float run = dir * (hub_x - sx) * 0.5f;				// > 0: the stand is on the far side, as it should be
		if (run < 0.0f) run = 0.0f;
		if (run > 220.0f) run = 220.0f;
		const float c2x = hub_x - dir * run, c2y = hub_y;

		// the arrowhead points along the leader's first stretch
		float vx = c1x - sx, vy = c1y - sy, vl = sqrtf(vx * vx + vy * vy);
		if (vl < 1.0f) { vx = dir; vy = 0; vl = 1.0f; }
		vx /= vl; vy /= vl;
		glBegin(GL_TRIANGLES);
			glVertex2f(sx, sy);
			glVertex2f(sx + vx * arrow - vy * arrow * 0.5f, sy + vy * arrow + vx * arrow * 0.5f);
			glVertex2f(sx + vx * arrow + vy * arrow * 0.5f, sy + vy * arrow - vx * arrow * 0.5f);
		glEnd();

		glBegin(GL_LINE_STRIP);
		for (int i = 0; i <= 32; ++i)
		{
			const float t = (float) i / 32.0f, u = 1.0f - t;
			const float x = u * u * u * sx + 3 * u * u * t * c1x + 3 * u * t * t * c2x + t * t * t * hub_x;
			const float y = u * u * u * sy + 3 * u * u * t * c1y + 3 * u * t * t * c2y + t * t * t * hub_y;
			glVertex2f(x, y);
		}
		glEnd();
	}
	// the neck as a quad, so its weight is exact at any line-width limit
	const float hw = NeckWidth(stands.size()) * 0.5f;
	glBegin(GL_QUADS);
		glVertex2f(hub_x, hub_y - hw); glVertex2f(end_x, hub_y - hw);
		glVertex2f(end_x, hub_y + hw); glVertex2f(hub_x, hub_y + hw);
	glEnd();
	glLineWidth(1.0f);
}

// Where the hub goes: the neck's full length from the receiver, but never
// behind a member stand - the neck shortens rather than send a leader back.
static float	HubX(const vector<std::pair<float, float> > & stands, float receiver_x, float dir, float neck)
{
	float hub = receiver_x - dir * neck;
	for (size_t k = 0; k < stands.size(); ++k)
	{
		const float lim = stands[k].first + dir * 40.0f;			// 40 px clear of the stand, toward the receiver
		if (dir > 0 ? hub < lim : hub > lim) hub = lim;
	}
	// ...but a stand AT or past the receiver cannot be helped: keep a stub of neck
	if (dir > 0 ? hub > receiver_x - 20 : hub < receiver_x + 20) hub = receiver_x - dir * 20.0f;
	return hub;
}

// "Worth noting": a solid amber disc with a dark "!" centred on it.
static void	DrawNote(GUI_GraphState * g, float cx, float cy, float r)
{
	g->SetState(0, 0, 0, 0, 1, 0, 0);
	const float ink[4] = { 0.10f, 0.08f, 0.02f, 1.0f };
	glColor4f(0, 0, 0, 0.5f);
	Circle(cx, cy, r + 1.5f, true);
	glColor4fv(kAmber);
	Circle(cx, cy, r, true);
	glColor4fv(ink);
	glBegin(GL_QUADS);								// the stroke
		glVertex2f(cx - r * 0.14f, cy - r * 0.05f); glVertex2f(cx + r * 0.14f, cy - r * 0.05f);
		glVertex2f(cx + r * 0.18f, cy + r * 0.62f); glVertex2f(cx - r * 0.18f, cy + r * 0.62f);
	glEnd();
	Circle(cx, cy - r * 0.42f, r * 0.17f, true);	// the dot
}

// Verdict marks, drawn rather than typed: the UI font has no check or cross.
static void	DrawMark(GUI_GraphState * g, int verdict, float x, float cy, float s)
{
	g->SetState(0, 0, 0, 0, 1, 0, 0);
	glLineWidth(2.0f);
	switch (verdict) {
	case WED_ModerationCode::v_Ok:
	case WED_ModerationCode::v_Assumed:
		glColor4fv(verdict == WED_ModerationCode::v_Ok ? kGreen : kMuted);
		glBegin(GL_LINE_STRIP);
		glVertex2f(x, cy); glVertex2f(x + s * 0.35f, cy - s * 0.4f); glVertex2f(x + s, cy + s * 0.45f);
		glEnd();
		break;
	case WED_ModerationCode::v_Foreign:
		glColor4fv(kAmber);
		glBegin(GL_LINES);
		glVertex2f(x, cy - s * 0.45f); glVertex2f(x + s, cy + s * 0.45f);
		glVertex2f(x, cy + s * 0.45f); glVertex2f(x + s, cy - s * 0.45f);
		glEnd();
		break;
	case WED_ModerationCode::v_Check:
	{
		// A filled badge with a dark "?": a ring round the glyph read as a
		// no-entry sign at this size.
		glColor4fv(kAmber);
		Circle(x + s * 0.5f, cy, s * 0.72f, true);
		const float ink[4] = { 0.10f, 0.08f, 0.02f, 1.0f };
		GUI_FontDraw(g, font_UI_Basic, ink, x + s * 0.5f, cy - Asc() * 0.36f, "?", align_Center);
		break;
	}
	default:
		break;
	}
	glLineWidth(1.0f);
}

static void	DrawFlag(GUI_GraphState * g, unsigned int tex, int w, int h, float x, float y_bot, float hgt)
{
	float wid = hgt * (float) w / (float) h;
	g->SetState(0, 1, 0, 0, 1, 0, 0);
	glColor4f(1, 1, 1, 1);
	g->BindTex((int) tex, 0);
	glBegin(GL_QUADS);		// loaded top-down: t=0 at the top vertex
		glTexCoord2f(0, 0); glVertex2f(x,       y_bot + hgt);
		glTexCoord2f(1, 0); glVertex2f(x + wid, y_bot + hgt);
		glTexCoord2f(1, 1); glVertex2f(x + wid, y_bot);
		glTexCoord2f(0, 1); glVertex2f(x,       y_bot);
	glEnd();
	g->SetState(0, 0, 0, 0, 1, 0, 0);
}

// ---- text of one entry ----

static string	AirlinesText(const WED_ModerationEntry & e)
{
	if (e.codes.empty())
		return e.op_type == ramp_operation_GeneralAviation || e.op_type == ramp_operation_Military
				? "none listed - any of the size" : "none listed";
	if (e.codes.size() == 1) return e.codes[0].code + " only";
	string s;
	for (size_t i = 0; i < e.codes.size(); ++i) s += (i ? " " : "") + e.codes[i].code;
	return s;
}

static string	StateText(const WED_ModerationEntry & e)
{
	string s = e.updated ? "Updated" : "Legacy";
	if (e.updated || e.auto_filled) s += e.auto_filled ? " (A)" : " (M)";
	return s;
}

static string	SizeText(const WED_ModerationEntry & e)
{
	if (!e.updated) return string("size ") + e.size_letter;
	string w = WED_ModerationWeightsText(e.weights);
	return w.empty() ? string("all weights zero - nothing parks") : w;
}

static string	KindText(const WED_ModerationEntry & e)
{
	string s = e.op_label;
	if (!e.equipment.empty()) s += "  |  " + e.equipment;
	if (!e.ramp_type.empty()) s += "  |  " + e.ramp_type;
	return s;
}

// The tray row's one-line summary, and its colour.
static string	VerifyText(const WED_ModerationEntry & e, const float ** col)
{
	*col = kMuted;
	char buf[96];
	switch (e.verify) {
	case WED_ModerationEntry::verify_Assumed:
		return "Operators: auto-filled, assumed correct";
	case WED_ModerationEntry::verify_Database:
		if (e.n_to_check == 0) { *col = kGreen; return "Operators: all listed as serving " + e.icao; }
		*col = kAmber;
		snprintf(buf, sizeof(buf), "Operators: %d to check", e.n_to_check);
		return buf;
	case WED_ModerationEntry::verify_NoData:
		*col = kAmber;
		return "Operators: unable to verify - no airport data";
	case WED_ModerationEntry::verify_Country:
		if (e.n_to_check == 0) return e.codes.empty() ? string("Operators: any military of the size") : "Operators: all from " + (e.country.empty() ? string("here") : e.country);
		*col = kAmber;
		snprintf(buf, sizeof(buf), "Operators: %d from another country", e.n_to_check);
		return buf;
	default:
		if (e.op_type == ramp_operation_None) return "No static aircraft (operation type None)";
		return "Operators and spawn weights";
	}
}

// ---- the layer ----

WED_ModerationLayer::WED_ModerationLayer(GUI_Pane * host, WED_MapZoomerNew * zoomer, IResolver * resolver) :
	WED_MapLayer(host, zoomer, resolver), mPinnedID(-1), mTrayID(-1), mOpenID(-1), mLegendRow(-1), mListScroll(0),
	mListFilter(0), mListSort(0), mOverviewBottom(-1), mCopiedUntil(0)
{
	mListBox[0] = mListBox[1] = mListBox[2] = mListBox[3] = 0;
}

WED_ModerationLayer::~WED_ModerationLayer()
{
}

void	WED_ModerationLayer::GetCaps(bool& draw_ent_v, bool& draw_ent_s, bool& cares_about_sel, bool& wants_clicks)
{
	draw_ent_v = false;
	draw_ent_s = false;
	cares_about_sel = false;
	wants_clicks = true;
}

const WED_ModerationLayer::Flag *	WED_ModerationLayer::FlagFor(const string & ioc)
{
	if (ioc.empty()) return NULL;
	std::map<string, Flag>::iterator it = mFlags.find(ioc);
	if (it != mFlags.end()) return it->second.tex ? &it->second : NULL;

	Flag f = { 0, 0, 0 };
	vector<uint32_t> argb;
	int w = 0, h = 0;
	if (WED_LoadPngTopDownARGB(WED_FlagSourcePathForCountry(ioc), argb, w, h) && !argb.empty() && w > 0 && h > 0)
	{
		GLuint tex = 0;
		glGenTextures(1, &tex);
		glBindTexture(GL_TEXTURE_2D, tex);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
		glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_BGRA, GL_UNSIGNED_BYTE, &argb[0]);
		f.tex = tex; f.w = w; f.h = h;
	}
	Flag & stored = mFlags[ioc] = f;
	return stored.tex ? &stored : NULL;
}

void	WED_ModerationLayer::Collect(vector<Callout> & out)
{
	out.clear();
	set<WED_Thing *> sel;
	WED_GetSelectionRecursive(GetResolver(), sel);

	vector<WED_RampPosition *> ramps;
	for (set<WED_Thing *>::const_iterator t = sel.begin(); t != sel.end(); ++t)
		if (WED_RampPosition * r = dynamic_cast<WED_RampPosition *>(*t))
			ramps.push_back(r);

	// The pinned stand stays while others are selected - that is what makes it a
	// base to compare against. Deleted (gone from the archive, or orphaned): unpin.
	if (mPinnedID >= 0)
	{
		WED_Thing * wrl = WED_GetWorld(GetResolver());
		WED_RampPosition * pinned = wrl ? dynamic_cast<WED_RampPosition *>(wrl->FetchPeer(mPinnedID)) : NULL;
		if (!pinned || !pinned->GetParent()) mPinnedID = -1;
		else if (std::find(ramps.begin(), ramps.end(), pinned) == ramps.end()) ramps.insert(ramps.begin(), pinned);
	}

	double b[4];
	GetZoomer()->GetPixelBounds(b[0], b[1], b[2], b[3]);
	out.reserve(ramps.size());
	for (size_t i = 0; i < ramps.size(); ++i)
	{
		Callout c;
		c.ramp = ramps[i];
		c.id   = ramps[i]->GetID();
		Point2 ll;
		ramps[i]->GetLocation(gis_Geo, ll);
		Point2 px = GetZoomer()->MapPixelToScreen(GetZoomer()->LLToPixel(ll));
		c.ax = (float) px.x();
		c.ay = (float) px.y();
		c.on_screen = !(c.ax < b[0] || c.ax > b[2] || c.ay < b[1] || c.ay > b[3]);
		WED_ModerationDescribe(ramps[i], WED_GetParentAirport(ramps[i]), c.e);
		c.x0 = c.y0 = c.x1 = c.y1 = 0;
		c.n_add = c.n_rem = c.n_chg = 0;
		c.label = c.e.ramp_name;
		out.push_back(c);
	}
}

void	WED_ModerationLayer::Diff(const WED_ModerationEntry & base, Callout & c) const
{
	set<string> a, b;
	for (size_t i = 0; i < base.codes.size(); ++i) a.insert(base.codes[i].code);
	for (size_t i = 0; i < c.e.codes.size(); ++i)  b.insert(c.e.codes[i].code);
	string added, removed;
	for (set<string>::const_iterator i = b.begin(); i != b.end(); ++i) if (!a.count(*i)) { added   += " " + *i; ++c.n_add; }
	for (set<string>::const_iterator i = a.begin(); i != a.end(); ++i) if (!b.count(*i)) { removed += " " + *i; ++c.n_rem; }
	if (!added.empty())   { c.diff.push_back("+" + added);   c.diff_kind.push_back(1); }
	if (!removed.empty()) { c.diff.push_back("-" + removed); c.diff_kind.push_back(-1); }

	const string sa = SizeText(base), sb = SizeText(c.e);
	if (sa != sb)							{ c.diff.push_back("~ " + sa + "  ->  " + sb); c.diff_kind.push_back(0); ++c.n_chg; }
	if (base.op_label != c.e.op_label)		{ c.diff.push_back("~ " + base.op_label + "  ->  " + c.e.op_label); c.diff_kind.push_back(0); ++c.n_chg; }
	if (base.equipment != c.e.equipment)	{ c.diff.push_back("~ " + base.equipment + "  ->  " + c.e.equipment); c.diff_kind.push_back(0); ++c.n_chg; }
	if (base.ramp_type != c.e.ramp_type)	{ c.diff.push_back("~ " + base.ramp_type + "  ->  " + c.e.ramp_type); c.diff_kind.push_back(0); ++c.n_chg; }
	if (c.diff.empty())						{ c.diff.push_back("= same setup as the pinned stand"); c.diff_kind.push_back(2); }
}

// On-screen stands with the same signature become one callout. It stands at the
// topmost of them (or at the pinned one, which must keep its own card), and the
// rest hang their leaders off it. Off-screen stands are dropped - nothing here
// could point at them.
void	WED_ModerationLayer::Group(vector<Callout> & cs, vector<Callout> & out) const
{
	out.clear();
	std::map<string, vector<size_t> > by_sig;
	vector<string> order;
	for (size_t i = 0; i < cs.size(); ++i)
	{
		if (!cs[i].on_screen) continue;
		vector<size_t> & v = by_sig[cs[i].e.signature];
		if (v.empty()) order.push_back(cs[i].e.signature);
		v.push_back(i);
	}
	for (size_t o = 0; o < order.size(); ++o)
	{
		vector<size_t> & m = by_sig[order[o]];
		// topmost first; stands in a row tie on y, so then by name - the card is
		// headed by 01-CONTROL, not by whichever copy the selection set gave first
		std::sort(m.begin(), m.end(), [&cs](size_t a, size_t b) {
			if (cs[a].ay != cs[b].ay) return cs[a].ay > cs[b].ay;
			return cs[a].e.ramp_name < cs[b].e.ramp_name; });
		size_t rep = m[0];
		for (size_t k = 0; k < m.size(); ++k) if (cs[m[k]].id == mPinnedID) rep = m[k];

		Callout c = cs[rep];
		bool same_name = true;
		vector<string> names;
		for (size_t k = 0; k < m.size(); ++k)
		{
			names.push_back(cs[m[k]].e.ramp_name);
			if (cs[m[k]].e.ramp_name != c.e.ramp_name) same_name = false;
			if (m[k] != rep) c.others.push_back(std::make_pair(cs[m[k]].ax, cs[m[k]].ay));
		}
		if (m.size() > 1)
		{
			char buf[32];
			snprintf(buf, sizeof(buf), same_name ? " x%d" : " +%d", same_name ? (int) m.size() : (int) m.size() - 1);
			c.label = c.e.ramp_name + buf;

			// Say it in words too, and name them when the names differ.
			string line;
			snprintf(buf, sizeof(buf), "= the same setup at %d stands", (int) m.size());
			line = buf;
			if (!same_name)
			{
				std::sort(names.begin(), names.end());
				names.erase(std::unique(names.begin(), names.end()), names.end());
				line += ":";
				for (size_t k = 0; k < names.size(); ++k) line += (k ? ", " : " ") + names[k];
			}
			c.diff.insert(c.diff.begin(), line);
			c.diff_kind.insert(c.diff_kind.begin(), 3);
		}
		out.push_back(c);
	}
}

// Width and height of a card; x0/y1 are left for the caller to place.
void	WED_ModerationLayer::SizeCard(Callout & c) const
{
	const WED_ModerationEntry & e = c.e;
	float w = TextW(e.icao) + TextW(c.label) + LineH() * 1.6f + 40 + (c.id == mPinnedID ? TextW("BASE") + 6 : 0);
	w = Max(w, TextW("Airlines") + 10 + TextW(AirlinesText(e)));
	w = Max(w, TextW(StateText(e)) + 20 + TextW(SizeText(e)));
	w = Max(w, TextW(KindText(e)));
	const float * vc;
	w = Max(w, TextW(VerifyText(e, &vc)) + 14);
	for (size_t d = 0; d < c.diff.size(); ++d) w = Max(w, TextW(c.diff[d]));
	w = Min(Max(w + kPad * 2, 240.0f), 420.0f);
	const float h = HeadH() + kPad + 3 * RowH() + c.diff.size() * LineH() + RowH() + kPad;
	c.x1 = c.x0 + w;
	c.y0 = c.y1 - h;
}

// The tray's text lines (wrapped) and its height, so the card's fill can run
// down through it: open, the tray is part of the card.
float	WED_ModerationLayer::TrayLines(const Callout & c, vector<string> & lines) const
{
	const WED_ModerationEntry & e = c.e;
	lines.clear();
	vector<string> notes;
	char buf[128];
	if (e.verify == WED_ModerationEntry::verify_NoData)
		notes.push_back("Unable to verify: no airport data for " + (e.icao.empty() ? string("this airport") : e.icao) + ".");
	if (e.verify == WED_ModerationEntry::verify_Database && e.n_to_check)
		notes.push_back("? = not listed as serving " + e.icao + ". Click ? to search the web.");
	if (e.op_type == ramp_operation_GeneralAviation || e.op_type == ramp_operation_Military)
	{
		int w[6];
		string src;
		if (e.updated) { for (int k = 0; k < 6; ++k) w[k] = e.weights[k]; }
		else
		{
			WED_LegacyClassWeights(e.size_letter >= 'A' && e.size_letter <= 'F' ? e.size_letter - 'A' : 0, w);
			snprintf(buf, sizeof(buf), " (from size %c)", e.size_letter);
			src = buf;
		}
		string wt = WED_ModerationWeightsText(w);
		notes.push_back("Spawn weights: " + (wt.empty() ? string("none - nothing parks") : wt) + src);
	}
	if (e.op_type == ramp_operation_GeneralAviation)
		notes.push_back("Private stands are not checked: GA comes from all over the world.");

	const float text_w = c.x1 - c.x0 - kPad * 2;
	for (size_t n = 0; n < notes.size(); ++n)
	{
		string rest = notes[n];
		while (!rest.empty())
		{
			if (TextW(rest) <= text_w) { lines.push_back(rest); break; }
			size_t cut = rest.size();
			while (cut > 0 && (rest[cut - 1] != ' ' || TextW(rest.substr(0, cut - 1)) > text_w)) --cut;
			if (cut == 0) { lines.push_back(Elide(rest, text_w)); break; }
			lines.push_back(rest.substr(0, cut - 1));
			rest = rest.substr(cut);
		}
	}
	const int rows = ((int) e.codes.size() + 2) / 3;
	return kPad * 2 + lines.size() * LineH() + rows * (LineH() + 5);
}

// One card. The header row (flag, ICAO, name, pin) floats above the card's top
// edge; the card is its top and left edges over a 40% black fill, which runs on
// down through the tray when the tray is open (tray_h > 0).
void	WED_ModerationLayer::DrawCard(GUI_GraphState * g, Callout & c, bool pinned, bool leader, float tray_h)
{
	const WED_ModerationEntry & e = c.e;
	float stroke[4];
	WED_ModerationColour(e.signature, stroke);

	const float lh = LineH(), asc = Asc();
	const float x0 = c.x0, x1 = c.x1, y1 = c.y1, y0 = c.y0;
	const float label_w = TextW("Airlines") + 10.0f;
	const float head_mid = y1 - HeadH() * 0.5f;
	const float edge = y1 - HeadH();
	const float bottom = y0 - tray_h;

	g->SetState(0, 0, 0, 0, 1, 0, 0);
	if (leader)
	{
		// every stand -> one hub -> the neck -> the card's top-left corner
		vector<std::pair<float, float> > stands(1, std::make_pair(c.ax, c.ay));
		stands.insert(stands.end(), c.others.begin(), c.others.end());
		DrawLeaders(stands, HubX(stands, x0, 1.0f, NeckLen(stands.size(), 16.0f)), edge, x0, stroke, 2.0f, 10.0f);
	}

	Fill(x0, bottom, x1, edge, kFill);
	glColor4fv(stroke);
	glLineWidth(pinned ? 3.0f : 2.0f);
	glBegin(GL_LINES);
		glVertex2f(x0, edge); glVertex2f(x1, edge);
	glEnd();
	glLineWidth(1.0f);
	{
		// the left edge receives the neck: as heavy as it is
		const float bw = Max(pinned ? 3.0f : 2.0f, NeckWidth(1 + c.others.size()));
		Fill(x0 - bw + 1.0f, bottom, x0 + 1.0f, edge + (pinned ? 1.5f : 1.0f), stroke);
	}

	// header: flag, ICAO, ramp name - left-aligned and tight
	float hx = x0 + 1;
	const float hb = edge + (HeadH() - lh) * 0.5f;
	if (const Flag * f = FlagFor(e.country))
	{
		DrawFlag(g, f->tex, f->w, f->h, hx, hb + 1, lh - 2);
		hx += (lh - 2) * (float) f->w / (float) f->h + 5;
	}
	const float base_y = hb + (lh - asc) * 0.5f + 1;
	Txt(g, kWhite, hx, base_y, e.icao.c_str());
	hx += TextW(e.icao) + 7;
	const float pin_x = x1 - 7;
	float name_end = pin_x - 10;
	if (pinned)
	{
		const float bw = TextW("BASE");
		name_end -= bw + 6;
		Txt(g, stroke, pin_x - 10 - bw, base_y, "BASE");
	}
	Txt(g, kWhite, hx, base_y, Elide(c.label, name_end - hx).c_str());

	// pin: filled when this is the base
	g->SetState(0, 0, 0, 0, 1, 0, 0);
	glColor4fv(pinned ? stroke : kMuted);
	Circle(pin_x, head_mid + 2, 4.5f, pinned);
	glBegin(GL_LINES);
		glVertex2f(pin_x, head_mid - 2.5f); glVertex2f(pin_x, head_mid - 7.5f);
	glEnd();
	Hit ph = { Hit::hit_Pin, pin_x - 9, head_mid - 9, pin_x + 9, head_mid + 9, c.id, "" };
	mHits.push_back(ph);

	// rows
	float y = edge - kPad;
	const float vx = x0 + kPad + label_w;
	Txt(g, kMuted, x0 + kPad, y - asc, "Airlines");
	Txt(g, kWhite, vx, y - asc, Elide(AirlinesText(e), x1 - kPad - vx).c_str());
	y -= RowH();

	string st = StateText(e);
	Txt(g, e.updated ? kWhite : kAmber, x0 + kPad, y - asc, st.c_str());
	Txt(g, kWhite, x0 + kPad + Max(label_w, TextW(st) + 10), y - asc, SizeText(e).c_str());
	y -= RowH();

	Txt(g, kMuted, x0 + kPad, y - asc, Elide(KindText(e), x1 - x0 - kPad * 2).c_str());
	y -= RowH();

	for (size_t i = 0; i < c.diff.size(); ++i)
	{
		const int k = c.diff_kind[i];
		const float * col = k == -1 ? kRed : k == 0 ? kAmber : k == 3 ? kMuted : kGreen;	// +, same: green; group note: muted
		Txt(g, col, x0 + kPad, y - asc, Elide(c.diff[i], x1 - x0 - kPad * 2).c_str());
		y -= lh;
	}

	// the tray row: a disclosure triangle and the verification summary
	const float * vcol;
	string vt = VerifyText(e, &vcol);
	const bool open = tray_h > 0;
	const float ty = y - lh * 0.5f - 1;
	g->SetState(0, 0, 0, 0, 1, 0, 0);
	glColor4fv(vcol);
	glBegin(GL_TRIANGLES);
	if (open) { glVertex2f(x0 + kPad, ty + 3); glVertex2f(x0 + kPad + 8, ty + 3); glVertex2f(x0 + kPad + 4, ty - 3); }
	else      { glVertex2f(x0 + kPad + 1, ty + 4); glVertex2f(x0 + kPad + 7, ty); glVertex2f(x0 + kPad + 1, ty - 4); }
	glEnd();
	Txt(g, vcol, x0 + kPad + 14, y - asc, Elide(vt, x1 - x0 - kPad * 2 - 14).c_str());
	Hit th = { Hit::hit_Tray, x0, y0, x1, y + 1, c.id, "" };
	mHits.push_back(th);
	if (open)
	{
		Hit tp = { Hit::hit_Tray, x0, bottom, x1, y0, c.id, "" };
		mHits.push_back(tp);
	}
	Hit ch = { Hit::hit_Card, x0, bottom, x1, y1, c.id, "" };
	mHits.push_back(ch);
}

// The open tray's content, below the card (its fill and edge are the card's).
void	WED_ModerationLayer::DrawTray(GUI_GraphState * g, Callout & c, const vector<string> & lines)
{
	const WED_ModerationEntry & e = c.e;
	const float lh = LineH(), asc = Asc();
	const float x0 = c.x0, x1 = c.x1;
	const float item_w = (x1 - x0 - kPad * 2) / 3.0f;
	const float item_h = lh + 5;

	// a hairline between the card rows and the tray
	float stroke[4];
	WED_ModerationColour(e.signature, stroke);
	stroke[3] = 0.45f;
	g->SetState(0, 0, 0, 0, 1, 0, 0);
	glColor4fv(stroke);
	glBegin(GL_LINES);
		glVertex2f(x0 + kPad, c.y0); glVertex2f(x1 - kPad, c.y0);
	glEnd();

	float y = c.y0 - kPad;
	for (size_t i = 0; i < lines.size(); ++i)
	{
		Txt(g, kMuted, x0 + kPad, y - asc, lines[i].c_str());
		y -= lh;
	}

	// three to a row: [flag] CODE mark, comma-separated
	for (size_t i = 0; i < e.codes.size(); ++i)
	{
		const WED_ModerationCode & k = e.codes[i];
		const float ix = x0 + kPad + (float) (i % 3) * item_w;
		const float iy_top = y - (float) (i / 3) * item_h;
		const float iy_bot = iy_top - item_h;
		float x = ix;
		if (const Flag * f = FlagFor(k.country))
		{
			DrawFlag(g, f->tex, f->w, f->h, x, iy_bot + 3, lh - 4);
			x += (lh - 4) * (float) f->w / (float) f->h + 4;
		}
		Txt(g, kWhite, x, iy_top - asc - 2, k.code.c_str());
		x += TextW(k.code) + 4;
		const float ms = lh * 0.55f;
		DrawMark(g, k.verdict, x, iy_top - item_h * 0.5f, ms);
		if (k.verdict == WED_ModerationCode::v_Check && !k.search_url.empty())
		{
			Hit sh = { Hit::hit_Search, x - 3, iy_bot, x + ms + 3, iy_top, c.id, k.search_url };
			mHits.push_back(sh);
		}
		if (k.verdict != WED_ModerationCode::v_Plain) x += ms + 2;
		if (i + 1 < e.codes.size())
			Txt(g, kMuted, x, iy_top - asc - 2, ",");
	}
}

// ---- tier 1: a full card beside each stand ----

void	WED_ModerationLayer::DrawCards(GUI_GraphState * g, vector<Callout> & cs)
{
	vector<size_t> order;
	for (size_t i = 0; i < cs.size(); ++i)
		if (cs[i].on_screen)
		{
			float rightmost = cs[i].ax;									// the rightmost member
			for (size_t k = 0; k < cs[i].others.size(); ++k) rightmost = Max(rightmost, cs[i].others[k].first);
			cs[i].x0 = rightmost + 32 + NeckLen(1 + cs[i].others.size(), 16.0f);
			cs[i].y1 = cs[i].ay + HeadH();
			SizeCard(cs[i]);
			// Keep the card on the map: a stand near the right edge (or a far
			// zoom, where the neck is long) pushed it under the property pane.
			// Shifted, it may sit over some of its stands; the leaders still meet.
			double b[4];
			GetZoomer()->GetPixelBounds(b[0], b[1], b[2], b[3]);
			float dx = 0, dy = 0;
			if (cs[i].x1 > b[2] - 8) dx = (float) b[2] - 8 - cs[i].x1;
			if (cs[i].x0 + dx < b[0] + 8) dx = (float) b[0] + 8 - cs[i].x0;
			if (cs[i].y1 > b[3] - 4) dy = (float) b[3] - 4 - cs[i].y1;
			cs[i].x0 += dx; cs[i].x1 += dx; cs[i].y0 += dy; cs[i].y1 += dy;
			order.push_back(i);
		}
	// top down, pushing a card down past any it would overlap
	std::sort(order.begin(), order.end(), [&cs](size_t a, size_t b) { return cs[a].ay > cs[b].ay; });
	vector<size_t> placed;
	for (size_t oi = 0; oi < order.size(); ++oi)
	{
		Callout & c = cs[order[oi]];
		for (int guard = 0; guard < 64; ++guard)
		{
			bool moved = false;
			for (size_t p = 0; p < placed.size(); ++p)
			{
				const Callout & o = cs[placed[p]];
				if (c.x0 < o.x1 + 4 && c.x1 > o.x0 - 4 && c.y0 < o.y1 + 4 && c.y1 > o.y0 - 4)
				{
					float h = c.y1 - c.y0;
					c.y1 = o.y0 - 6;
					c.y0 = c.y1 - h;
					moved = true;
				}
			}
			if (!moved) break;
		}
		placed.push_back(order[oi]);
	}

	Callout * open = NULL;
	for (size_t oi = 0; oi < order.size(); ++oi)
	{
		Callout & c = cs[order[oi]];
		if (c.id == mTrayID) { open = &c; continue; }		// drawn last, over the others
		DrawCard(g, c, c.id == mPinnedID, true, 0);
	}
	if (open)
	{
		vector<string> lines;
		float th = TrayLines(*open, lines);
		DrawCard(g, *open, open->id == mPinnedID, true, th);
		DrawTray(g, *open, lines);
	}
}

// ---- tier 2: a chip per stand in a column at the right edge ----

void	WED_ModerationLayer::DrawChips(GUI_GraphState * g, vector<Callout> & cs)
{
	double b[4];
	GetZoomer()->GetPixelBounds(b[0], b[1], b[2], b[3]);
	const float lh = LineH(), asc = Asc();
	const float ch = HeadH() + 2;

	vector<size_t> order;
	float chip_w = 200;
	for (size_t i = 0; i < cs.size(); ++i)
		if (cs[i].on_screen)
		{
			order.push_back(i);
			chip_w = Max(chip_w, TextW(cs[i].e.icao) + TextW(cs[i].label) + lh * 1.6f + 110);
		}
	chip_w = Min(chip_w, 340.0f);


	// The column hides whatever it sits on, so it goes on the side where it
	// covers fewer stands; its bar and neck face the map.
	int cover_r = 0, cover_l = 0;
	for (size_t i = 0; i < order.size(); ++i)
	{
		const Callout & c = cs[order[i]];
		vector<std::pair<float, float> > st(1, std::make_pair(c.ax, c.ay));
		st.insert(st.end(), c.others.begin(), c.others.end());
		for (size_t k = 0; k < st.size(); ++k)
		{
			if (st[k].first > (float) b[2] - 12 - chip_w - 30) ++cover_r;
			if (st[k].first < (float) b[0] + 12 + chip_w + 30) ++cover_l;
		}
	}
	const bool left = cover_l < cover_r;
	const float cx0 = left ? (float) b[0] + 12 : (float) b[2] - 12 - chip_w, cx1 = cx0 + chip_w;
	const float recv = left ? cx1 : cx0, dir = left ? -1.0f : 1.0f;

	// The column's order decides how many leaders cross. Top to bottom by the
	// stands' height, and among stands at one height - a row of gates - nearest
	// the column first: that stand's leader is the short, level one at the top,
	// and each further one drops below the last instead of cutting across it.
	vector<float> key(cs.size(), 0.0f);
	for (size_t i = 0; i < order.size(); ++i)
	{
		const Callout & c = cs[order[i]];
		// a group sorts by its member nearest the column: that is where its
		// leaders leave the pack, and a centroid put spread groups mid-column
		float best = c.ay - 0.25f * fabsf(c.ax - recv);
		for (size_t k = 0; k < c.others.size(); ++k)
			best = Max(best, c.others[k].second - 0.25f * fabsf(c.others[k].first - recv));
		key[order[i]] = best;
	}
	std::sort(order.begin(), order.end(), [&key](size_t a, size_t b) { return key[a] > key[b]; });
	float next_top = (float) b[3] - kTopClear;
	if (left && mOverviewBottom > 0) next_top = Min(next_top, mOverviewBottom - 12);	// under the overview, which is top left
	vector<float> tops(order.size());
	for (size_t oi = 0; oi < order.size(); ++oi)
	{
		const Callout & c = cs[order[oi]];
		float top = Min(next_top, c.ay + ch * 0.5f);
		tops[oi] = top;
		next_top = top - ch - 3;
	}
	// ran off the bottom: pack from the bottom up instead
	if (!tops.empty() && tops.back() - ch < (float) b[1] + 8)
	{
		float bot_top = (float) b[1] + 8 + ch;
		for (size_t oi = order.size(); oi-- > 0; )
		{
			if (tops[oi] >= bot_top) break;
			tops[oi] = bot_top;
			bot_top += ch + 3;
		}
	}

	Callout * open = NULL;
	for (size_t oi = 0; oi < order.size(); ++oi)
	{
		Callout & c = cs[order[oi]];
		const WED_ModerationEntry & e = c.e;
		float stroke[4];
		WED_ModerationColour(e.signature, stroke);
		const float top = tops[oi], bot = top - ch, mid = (top + bot) * 0.5f;
		const bool pinned = c.id == mPinnedID;

		// leader from the stand to the chip
		g->SetState(0, 0, 0, 0, 1, 0, 0);
		float lead[4] = { stroke[0], stroke[1], stroke[2], 0.85f };
		vector<std::pair<float, float> > stands(1, std::make_pair(c.ax, c.ay));
		stands.insert(stands.end(), c.others.begin(), c.others.end());
		DrawLeaders(stands, HubX(stands, recv, dir, NeckLen(stands.size(), 14.0f)), mid, recv, lead, 1.5f, 8.0f);

		Fill(cx0, bot, cx1, top, kFill);
		{
			// the bar receives the neck: as heavy as it is
			const float bw = Max(pinned ? 3.0f : 2.0f, NeckWidth(1 + c.others.size()));
			if (left)	Fill(cx1 - 1.0f, bot, cx1 + bw - 1.0f, top, stroke);
			else		Fill(cx0 - bw + 1.0f, bot, cx0 + 1.0f, top, stroke);
		}

		float x = cx0 + kPad;
		if (const Flag * f = FlagFor(e.country))
		{
			DrawFlag(g, f->tex, f->w, f->h, x, bot + (ch - lh) * 0.5f + 1, lh - 2);
			x += (lh - 2) * (float) f->w / (float) f->h + 5;
		}
		const float by = bot + (ch - asc) * 0.5f;
		Txt(g, kWhite, x, by, e.icao.c_str());
		x += TextW(e.icao) + 6;

		// right side: the issues, and against the base the diff counts
		string right;
		char buf[48];
		if (pinned) right = "BASE";
		else if (mPinnedID >= 0)
		{
			if (c.n_add + c.n_rem + c.n_chg == 0) right = "=";
			else { snprintf(buf, sizeof(buf), "+%d -%d ~%d", c.n_add, c.n_rem, c.n_chg); right = buf; }
		}
		float rx = cx1 - kPad;
		if (!right.empty())
		{
			rx -= TextW(right);
			Txt(g, pinned ? stroke : kMuted, rx, by, right.c_str());
			rx -= 8;
		}
		if (e.n_to_check)
		{
			snprintf(buf, sizeof(buf), "%d to verify", e.n_to_check);
			rx -= TextW(buf);
			Txt(g, kAmber, rx, by, buf);
			rx -= 8;
		}
		if (e.auto_filled || e.op_type == ramp_operation_None)
		{
			const char * tag = e.op_type == ramp_operation_None ? "None" : "A";		// the watermark, as on the card
			rx -= TextW(tag);
			Txt(g, kMuted, rx, by, tag);
			rx -= 8;
		}
		Txt(g, kWhite, x, by, Elide(c.label, rx - x).c_str());

		Hit hc = { Hit::hit_Chip, cx0, bot, cx1, top, c.id, "" };
		mHits.push_back(hc);
		if (c.id == mOpenID) { open = &c; c.y1 = top; }
	}

	// the hovered chip's card, to the chip's left, its header level with the chip
	if (open)
	{
		open->x0 = 0;
		SizeCard(*open);						// width, and y0 from the chip-level y1
		const float w = open->x1 - open->x0;
		if (left)	{ open->x0 = cx1 + 20; open->x1 = open->x0 + w; }	// past the bar
		else		{ open->x1 = cx0 - 10; open->x0 = open->x1 - w; }
		vector<string> lines;
		float th = open->id == mTrayID ? TrayLines(*open, lines) : 0;
		DrawCard(g, *open, open->id == mPinnedID, false, th);
		if (th > 0) DrawTray(g, *open, lines);
	}
}

// ---- tier 3: a legend of the distinct entries ----

void	WED_ModerationLayer::DrawLegend(GUI_GraphState * g, vector<Callout> & cs)
{
	struct Group { string sig; vector<size_t> members; int issues; };
	std::map<string, size_t> index;
	vector<Group> groups;
	for (size_t i = 0; i < cs.size(); ++i)
	{
		const string & s = cs[i].e.signature;
		std::map<string, size_t>::iterator it = index.find(s);
		if (it == index.end()) { index[s] = groups.size(); Group gr; gr.sig = s; gr.issues = 0; groups.push_back(gr); it = index.find(s); }
		groups[it->second].members.push_back(i);
		groups[it->second].issues += cs[i].e.n_to_check;
	}
	std::stable_sort(groups.begin(), groups.end(), [](const Group & a, const Group & b) { return a.members.size() > b.members.size(); });

	double b[4];
	GetZoomer()->GetPixelBounds(b[0], b[1], b[2], b[3]);
	const float lh = LineH(), asc = Asc();
	const float rh = lh + 6;
	const float w = 430;
	const float x1 = (float) b[2] - 12, x0 = x1 - w;
	const size_t shown = groups.size() < kMaxLegendRows ? groups.size() : kMaxLegendRows;
	float top = (float) b[3] - kTopClear;
	const float h = HeadH() + kPad + shown * rh + (groups.size() > shown ? lh : 0) + kPad;

	// a ring round every on-screen stand of the hovered row
	if (mLegendRow >= 0 && mLegendRow < (int) groups.size())
	{
		float stroke[4];
		WED_ModerationColour(groups[mLegendRow].sig, stroke);
		g->SetState(0, 0, 0, 0, 1, 0, 0);
		glColor4fv(stroke);
		glLineWidth(3.0f);
		for (size_t m = 0; m < groups[mLegendRow].members.size(); ++m)
		{
			const Callout & c = cs[groups[mLegendRow].members[m]];
			if (c.on_screen) Circle(c.ax, c.ay, 18, false);
		}
		glLineWidth(1.0f);
	}

	// header above the top edge, like a card's
	char buf[96];
	int issues = 0;
	for (size_t i = 0; i < groups.size(); ++i) issues += groups[i].issues;
	snprintf(buf, sizeof(buf), "%d stands, %d unique setups", (int) cs.size(), (int) groups.size());
	const float edge = top - HeadH();
	Txt(g, kWhite, x0 + 1, edge + (HeadH() - asc) * 0.5f, buf);
	if (issues)
	{
		snprintf(buf, sizeof(buf), "%d to verify", issues);
		Txt(g, kAmber, x1 - TextW(buf), edge + (HeadH() - asc) * 0.5f, buf);
	}
	g->SetState(0, 0, 0, 0, 1, 0, 0);
	Fill(x0, top - h, x1, edge, kFill);
	glColor4fv(kMuted);
	glLineWidth(2.0f);
	glBegin(GL_LINE_STRIP);
		glVertex2f(x0, top - h); glVertex2f(x0, edge); glVertex2f(x1, edge);
	glEnd();
	glLineWidth(1.0f);

	mLegendIDs.clear();
	float y = edge - kPad;
	for (size_t r = 0; r < shown; ++r)
	{
		const Group & gr = groups[r];
		const WED_ModerationEntry & e = cs[gr.members[0]].e;
		float stroke[4];
		WED_ModerationColour(gr.sig, stroke);
		const float rt = y, rb = y - rh;
		if ((int) r == mLegendRow)
		{
			const float hl[4] = { 1, 1, 1, 0.08f };
			g->SetState(0, 0, 0, 0, 1, 0, 0);
			Fill(x0 + 2, rb, x1, rt, hl);
		}
		g->SetState(0, 0, 0, 0, 1, 0, 0);
		Fill(x0 + kPad, rb + 4, x0 + kPad + 12, rt - 4, stroke);		// swatch
		float x = x0 + kPad + 18;
		const float by = rb + (rh - asc) * 0.5f;
		snprintf(buf, sizeof(buf), "x%d", (int) gr.members.size());
		Txt(g, kWhite, x, by, buf);
		x += TextW("x000") + 6;
		string rt_s;
		if (gr.issues) { snprintf(buf, sizeof(buf), "%d to verify", gr.issues); rt_s = buf; }
		const float rx = x1 - kPad - TextW(rt_s);
		if (!rt_s.empty()) Txt(g, kAmber, rx, by, rt_s.c_str());
		string summary = e.op_label + "  " + AirlinesText(e) + "  " + SizeText(e);
		Txt(g, kMuted, x, by, Elide(summary, rx - 8 - x).c_str());

		vector<int> ids;
		for (size_t m = 0; m < gr.members.size(); ++m) ids.push_back(cs[gr.members[m]].id);
		mLegendIDs.push_back(ids);
		Hit hl = { Hit::hit_Legend, x0, rb, x1, rt, (int) r, "" };
		mHits.push_back(hl);
		y = rb;
	}
	if (groups.size() > shown)
	{
		snprintf(buf, sizeof(buf), "... and %d more", (int) (groups.size() - shown));
		Txt(g, kMuted, x0 + kPad, y - asc - 2, buf);
	}
}

static double	NowSec(void)
{
	return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

// "Copy Summary to Clipboard", left-aligned at the foot of the overview; a
// green "Copied!" beside it for two seconds after a click.
void	WED_ModerationLayer::DrawCopyButton(GUI_GraphState * g, float x, float y_top)
{
	const char * cap = "Copy Summary to Clipboard";
	const float lh = LineH(), asc = Asc();
	const float w = TextW(cap) + 16, h = lh + 6;
	const float bx0 = x, bx1 = x + w, bt = y_top, bb = y_top - h;
	int mx, my;
	MouseOnScreen(GetZoomer(), GetHost(), mx, my);
	const bool over = Inside((float) mx, (float) my, bx0, bb, bx1, bt);
	const float face[4] = { 1, 1, 1, over ? 0.22f : 0.12f };
	g->SetState(0, 0, 0, 0, 1, 0, 0);
	Fill(bx0, bb, bx1, bt, face);
	glColor4fv(kMuted);
	glBegin(GL_LINE_LOOP);
		glVertex2f(bx0, bb); glVertex2f(bx1, bb); glVertex2f(bx1, bt); glVertex2f(bx0, bt);
	glEnd();
	Txt(g, kWhite, bx0 + 8, bb + (h - asc) * 0.5f, cap);
	Hit hc = { Hit::hit_Copy, bx0, bb, bx1, bt, 0, "" };
	mHits.push_back(hc);
	if (NowSec() < mCopiedUntil)
	{
		Txt(g, kGreen, bx1 + 10, bb + (h - asc) * 0.5f, "Copied!");
		GetHost()->Refresh();					// until the two seconds are up
	}
}

// Select one stand and bring it to the middle of the map, at this zoom.
void	WED_ModerationLayer::Focus(int ramp_id)
{
	WED_Thing * wrl = WED_GetWorld(GetResolver());
	ISelection * sel = WED_GetSelect(GetResolver());
	WED_RampPosition * r = wrl ? dynamic_cast<WED_RampPosition *>(wrl->FetchPeer(ramp_id)) : NULL;
	if (!r || !sel) return;
	IOperation * op = dynamic_cast<IOperation *>(sel);
	if (op) op->StartOperation("Select Ramp Start");
	sel->Clear();
	sel->Select(r);
	if (op) op->CommitOperation();
	Point2 ll;
	r->GetLocation(gis_Geo, ll);
	GetZoomer()->CenterOn(ll);			// keeps the zoom: see WED_MapZoomerNew::CenterOn
	mReviewed.insert(WED_ModerationSignature(r));
	GetHost()->Refresh();
}

// The Moderation View's opener: the airport at a glance, top left of the map.
//     [flag] ZBAA  Moderation View
//     ---------------------------------------------
//     | 34 ramp starts        12 / 34 reviewed  [====   ]
//     | 9 to check  7 distinct entries  5 auto-filled  2 None
//     | To check:
//     | [] 01-CONTROL                        ?1   (click: select and centre)
//     |    ...
//     | Shift+X / Ctrl+Shift+X step through them
// Reviewed = shown as the one selected stand while the view is on, this session
// only: keeping it needs a home in the file, which is an open question (TODO).
void	WED_ModerationLayer::DrawOverview(GUI_GraphState * g)
{
	WED_Airport * apt = WED_GetCurrentAirport(GetResolver());
	sIssueIDs.clear();
	if (!apt) return;

	vector<WED_RampPosition *> ramps;
	WED_ModerationRamps(apt, ramps);
	// mask: 1 not listed here, 2 no airport data, 4 foreign military, 8 parks nothing (validator)
	struct Row { int id; string name; string sig; int n; bool reviewed; int mask; bool parks; };
	vector<Row> issues;
	int n_kind[5] = { 0, 0, 0, 0, 0 };
	set<string> sigs;
	int n_auto = 0, n_none = 0;
	string icao, country;
	for (size_t i = 0; i < ramps.size(); ++i)
	{
		WED_ModerationEntry e;
		WED_ModerationDescribe(ramps[i], apt, e);
		if (icao.empty()) { icao = e.icao; country = e.country; }
		sigs.insert(e.signature);
		if (e.auto_filled) ++n_auto;
		if (e.op_type == ramp_operation_None) ++n_none;
		if (WED_ModerationHasIssue(e))
		{
			sIssueIDs.insert(ramps[i]->GetID());
			int mask = 0;
			if (e.n_to_check && e.verify == WED_ModerationEntry::verify_Database)	mask |= 1;
			if (e.verify == WED_ModerationEntry::verify_NoData)						mask |= 2;
			if (e.n_to_check && e.verify == WED_ModerationEntry::verify_Country)	mask |= 4;
			if (e.parks_nothing)														mask |= 8;
			++n_kind[0];
			for (int k = 1; k <= 4; ++k) if (mask & (1 << (k - 1))) ++n_kind[k];
			Row r = { ramps[i]->GetID(), e.ramp_name, e.signature, e.n_to_check, mReviewed.count(e.signature) > 0, mask, e.parks_nothing };
			if (mListFilter == 0 || (mask & (1 << (mListFilter - 1)))) issues.push_back(r);
		}
	}
	if (mListSort == 1)
		std::stable_sort(issues.begin(), issues.end(), [](const Row & a, const Row & b) { return a.n > b.n; });
	int n_rev = 0;
	for (size_t i = 0; i < ramps.size(); ++i) if (mReviewed.count(WED_ModerationSignature(ramps[i]))) ++n_rev;
	const int n_issue_total = n_kind[0];

	double b[4];
	GetZoomer()->GetPixelBounds(b[0], b[1], b[2], b[3]);
	const float lh = LineH(), asc = Asc();
	const float x0 = (float) b[0] + 10, w = 380, x1 = x0 + w;
	WED_Map * map = dynamic_cast<WED_Map *>(GetHost());
	const float top = (float) b[3] - 10 - (map && map->IsRotateMode() ? 5 : 3) * lh - 8;	// below the map's own lines
	const int kRows = 10;
	const int shown = (int) issues.size() < kRows ? (int) issues.size() : kRows;
	const int max_scroll = (int) issues.size() - shown;
	if (mListScroll > max_scroll) mListScroll = max_scroll;
	if (mListScroll < 0) mListScroll = 0;
	const float rh = lh + 4;
	const float body_h = kPad + 2 * RowH() + 6 + (n_issue_total == 0 ? RowH() : RowH() + lh + (issues.empty() ? rh : shown * rh) + lh) + 6 + RowH() + kPad;
	const float edge = top - HeadH(), bottom = edge - body_h;
	mOverviewBottom = bottom;

	// a solid "!" badge beside every stand to check - "worth noting" - so they
	// stand out from the greyed rest
	for (size_t i = 0; i < ramps.size(); ++i)
	{
		if (!sIssueIDs.count(ramps[i]->GetID())) continue;
		Point2 ll;
		ramps[i]->GetLocation(gis_Geo, ll);
		Point2 px = GetZoomer()->MapPixelToScreen(GetZoomer()->LLToPixel(ll));
		if (px.x() < b[0] || px.x() > b[2] || px.y() < b[1] || px.y() > b[3]) continue;
		DrawNote(g, (float) px.x() + 14, (float) px.y() + 14, 8);
	}

	// header above the top edge, as the cards
	float hx = x0 + 1;
	const float hb = edge + (HeadH() - lh) * 0.5f;
	if (const Flag * f = FlagFor(country))
	{
		DrawFlag(g, f->tex, f->w, f->h, hx, hb + 1, lh - 2);
		hx += (lh - 2) * (float) f->w / (float) f->h + 5;
	}
	const float by = hb + (lh - asc) * 0.5f + 1;
	Txt(g, kWhite, hx, by, icao.c_str());
	hx += TextW(icao) + 7;
	Txt(g, kAmber, hx, by, "Moderation View");

	g->SetState(0, 0, 0, 0, 1, 0, 0);
	Fill(x0, bottom, x1, edge, kFill);
	glColor4fv(kAmber);
	glLineWidth(2.0f);
	glBegin(GL_LINE_STRIP);
		glVertex2f(x0, bottom); glVertex2f(x0, edge); glVertex2f(x1, edge);
	glEnd();
	glLineWidth(1.0f);
	Hit hp = { Hit::hit_Panel, x0, bottom, x1, top, -1, "" };
	mHits.push_back(hp);

	char buf[128];
	float y = edge - kPad;
	snprintf(buf, sizeof(buf), "%d ramp starts", (int) ramps.size());
	Txt(g, kWhite, x0 + kPad, y - asc, buf);
	snprintf(buf, sizeof(buf), "%d / %d reviewed", n_rev, (int) ramps.size());
	const float bar_x1 = x1 - kPad, bar_x0 = bar_x1 - 90;
	Txt(g, kWhite, bar_x0 - 8 - TextW(buf), y - asc, buf);
	{
		const float bt = y - lh * 0.5f + 3, bb = bt - 6;
		const float track[4] = { 1, 1, 1, 0.18f };
		g->SetState(0, 0, 0, 0, 1, 0, 0);
		Fill(bar_x0, bb, bar_x1, bt, track);
		if (!ramps.empty()) Fill(bar_x0, bb, bar_x0 + (bar_x1 - bar_x0) * n_rev / (float) ramps.size(), bt, kGreen);
	}
	y -= RowH();
	snprintf(buf, sizeof(buf), "%d to check    %d unique setups    %d auto-filled    %d \"None\"",
		n_issue_total, (int) sigs.size(), n_auto, n_none);
	Txt(g, n_issue_total == 0 ? kMuted : kAmber, x0 + kPad, y - asc, Elide(buf, w - kPad * 2).c_str());
	y -= RowH() + 6;

	if (n_issue_total == 0)
	{
		Txt(g, kGreen, x0 + kPad, y - asc, "Nothing to check at this airport.");
		DrawCopyButton(g, x0 + kPad, y - RowH() - 6);
		return;
	}

	// filter chips and the sort toggle
	{
		const char * names[5] = { "All", "Not listed", "No data", "Foreign", "Parks nothing" };
		float fx = x0 + kPad;
		for (int f = 0; f < 5; ++f)
		{
			if (f > 0 && n_kind[f] == 0) continue;
			snprintf(buf, sizeof(buf), "%s %d", names[f], n_kind[f]);
			const float tw = TextW(buf), bx0 = fx - 3, bx1 = fx + tw + 3, bt = y + 1, bb = y - lh - 1;
			if (mListFilter == f)
			{
				const float on[4] = { 1.0f, 0.75f, 0.3f, 0.35f };
				g->SetState(0, 0, 0, 0, 1, 0, 0);
				Fill(bx0, bb, bx1, bt, on);
			}
			Txt(g, mListFilter == f ? kWhite : kMuted, fx, y - asc, buf);
			Hit hfl = { Hit::hit_Filter, bx0, bb, bx1, bt, f, "" };
			mHits.push_back(hfl);
			fx = bx1 + 8;
		}
		const char * sort = mListSort == 0 ? "Sort: name" : "Sort: most first";
		const float sx = x1 - kPad - TextW(sort);
		Txt(g, kMuted, sx, y - asc, sort);
		Hit hs = { Hit::hit_Sort, sx - 3, y - lh - 1, x1, y + 1, 0, "" };
		mHits.push_back(hs);
		y -= RowH();
	}
	Txt(g, kMuted, x0 + kPad, y - asc, issues.empty() ? "Nothing of this kind." : "Click one to select it:");
	if (max_scroll > 0)
	{
		snprintf(buf, sizeof(buf), "%d-%d of %d, scroll for more", mListScroll + 1, mListScroll + shown, (int) issues.size());
		Txt(g, kMuted, x1 - kPad - TextW(buf), y - asc, buf);
	}
	y -= lh;
	mListBox[0] = x0; mListBox[2] = x1; mListBox[3] = y; mListBox[1] = y - shown * rh;
	int mx, my;
	MouseOnScreen(GetZoomer(), GetHost(), mx, my);
	for (int r = 0; r < shown; ++r)
	{
		const Row & row = issues[r + mListScroll];
		const float rt = y, rb = y - rh;
		if (Inside((float) mx, (float) my, x0, rb, x1, rt))
		{
			const float hl[4] = { 1, 1, 1, 0.10f };
			g->SetState(0, 0, 0, 0, 1, 0, 0);
			Fill(x0 + 2, rb, x1, rt, hl);
		}
		float sw[4];
		WED_ModerationColour(row.sig, sw);
		g->SetState(0, 0, 0, 0, 1, 0, 0);
		Fill(x0 + kPad, rb + 3, x0 + kPad + 10, rt - 3, sw);
		const float ty = rb + (rh - asc) * 0.5f;
		Txt(g, row.reviewed ? kMuted : kWhite, x0 + kPad + 16, ty, row.name.c_str());
		// in words: "2 to verify", or why there is nothing to verify against
		string right;
		if (row.n) { snprintf(buf, sizeof(buf), "%d to verify", row.n); right = buf; }
		else if (row.mask & 2) right = "no airport data";
		if (row.parks) right += string(right.empty() ? "" : ", ") + "parks nothing";
		Txt(g, kAmber, x1 - kPad - TextW(right), ty, right.c_str());
		if (row.reviewed)
		{
			const float rx = x1 - kPad - TextW(right) - 12 - TextW("reviewed");
			Txt(g, kGreen, rx, ty, "reviewed");
		}
		Hit hf = { Hit::hit_Focus, x0, rb, x1, rt, row.id, "" };
		mHits.push_back(hf);
		y = rb;
	}
	if (max_scroll > 0)
	{
		// a thin scroll bar at the list's right edge
		const float track[4] = { 1, 1, 1, 0.15f };
		const float lt = mListBox[3], lb = mListBox[1];
		const float th = (lt - lb) * shown / (float) issues.size();
		const float tt = lt - (lt - lb - th) * mListScroll / (float) max_scroll;
		g->SetState(0, 0, 0, 0, 1, 0, 0);
		Fill(x1 - 4, lb, x1 - 1, lt, track);
		Fill(x1 - 4, tt - th, x1 - 1, tt, kMuted);
	}
	Txt(g, kMuted, x0 + kPad, y - asc - 2, "Shift+X / Ctrl+Shift+X: next / previous stand to check");
	y -= lh + 6;
	DrawCopyButton(g, x0 + kPad, y);
}

// Cards, chips, the overview and the badges stay level with the screen when the
// view is turned: undo the map's rotation for them, and place their anchors
// with MapPixelToScreen.
void	WED_ModerationLayer::DrawSelected(bool inCurrent, GUI_GraphState * g)
{
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
	DrawOverlays(g);
	if (rot != 0)
	{
		glMatrixMode(GL_MODELVIEW);
		glPopMatrix();
	}
}

void	WED_ModerationLayer::DrawOverlays(GUI_GraphState * g)
{
	if (!WED_ModerationEnabled()) { mHits.clear(); return; }

	// What the mouse was over last frame decides what is open this frame.
	int mx, my;
	MouseOnScreen(GetZoomer(), GetHost(), mx, my);
	const float fx = (float) mx, fy = (float) my;
	int tray = -1, chip = -1, card = -1, row = -1;
	for (size_t i = 0; i < mHits.size(); ++i)
	{
		const Hit & h = mHits[i];
		if (!Inside(fx, fy, h.x0, h.y0, h.x1, h.y1)) continue;
		if (h.kind == Hit::hit_Tray)   tray = h.ramp_id;
		if (h.kind == Hit::hit_Chip)   chip = h.ramp_id;
		if (h.kind == Hit::hit_Card)   card = h.ramp_id;
		if (h.kind == Hit::hit_Legend) row  = h.ramp_id;
	}
	mTrayID = tray;
	mOpenID = chip >= 0 ? chip : (card >= 0 && card == mOpenID ? card : -1);
	mLegendRow = row;
	mHits.clear();

	vector<Callout> cs;
	Collect(cs);

	// Counter-clockwise quads are back faces to WED's GL state (GUI_GraphState
	// sets glFrontFace(GL_CW)); none of this is 3-D, so culling only ever hides
	// fills. Off while drawing, on again after.
	glDisable(GL_CULL_FACE);

	mOverviewBottom = -1;
	if (sModerationView)
	{
		// one stand shown on its own counts as looked at
		if (cs.size() == 1) mReviewed.insert(cs[0].e.signature);
		const std::set<int> before = sIssueIDs;
		DrawOverview(g);
		if (before != sIssueIDs) GetHost()->Refresh();		// the silhouettes used last frame's set
	}
	else if (!sIssueIDs.empty())
		sIssueIDs.clear();

	if (cs.empty())
	{
		glEnable(GL_CULL_FACE);
		g->SetState(0, 0, 0, 0, 0, 0, 0);
		return;
	}

	const WED_ModerationEntry * base = NULL;
	for (size_t i = 0; i < cs.size(); ++i) if (cs[i].id == mPinnedID) base = &cs[i].e;
	if (base)
		for (size_t i = 0; i < cs.size(); ++i)
			if (cs[i].id != mPinnedID) Diff(*base, cs[i]);

	// The tier counts entries on screen, not stands: ten copies of one stand
	// are one card with ten leaders.
	vector<Callout> shared;
	Group(cs, shared);
	if (shared.size() <= kMaxCards)			DrawCards(g, shared);
	else if (shared.size() <= kMaxChips)	DrawChips(g, shared);
	else									DrawLegend(g, cs);

	glEnable(GL_CULL_FACE);
	g->SetState(0, 0, 0, 0, 0, 0, 0);
	glLineWidth(1.0f);
}

int		WED_ModerationLayer::HandleScrollWheel(int inX, int inY, int inDist)
{
	{ Point2 sp = GetZoomer()->MapPixelToScreen(Point2(inX, inY)); inX = (int) floor(sp.x() + 0.5); inY = (int) floor(sp.y() + 0.5); }
	if (!sModerationView) return 0;
	if (!Inside((float) inX, (float) inY, mListBox[0], mListBox[1], mListBox[2], mListBox[3])) return 0;
	mListScroll -= inDist;				// wheel up: towards the top of the list
	if (mListScroll < 0) mListScroll = 0;	// the upper clamp is DrawOverview's
	return 1;
}

void	WED_ModerationLayer::HandleClickUp(int inX, int inY, int inButton, GUI_KeyFlags modifiers)
{
	if (mPendingURL.empty()) return;
	string url;
	url.swap(mPendingURL);
	WED_ModerationOpenSearch(url);
}

int		WED_ModerationLayer::HandleClickDown(int inX, int inY, int inButton, GUI_KeyFlags modifiers)
{
	{ Point2 sp = GetZoomer()->MapPixelToScreen(Point2(inX, inY)); inX = (int) floor(sp.x() + 0.5); inY = (int) floor(sp.y() + 0.5); }
	if (!WED_ModerationEnabled() || inButton != 0) return 0;
	const float x = (float) inX, y = (float) inY;

	// Most specific first: a search mark or a pin sits on a card or a tray.
	for (int pass = 0; pass < 3; ++pass)
		for (size_t i = 0; i < mHits.size(); ++i)
		{
			const Hit & h = mHits[i];
			if (!Inside(x, y, h.x0, h.y0, h.x1, h.y1)) continue;
			if (pass == 0 && h.kind == Hit::hit_Search)
			{
				// Not now: a browser opened on the mouse-DOWN takes the focus,
				// the up goes to the browser, and WED's window is left holding
				// a click that never ends. Open it once this click is over.
				mPendingURL = h.url;
				return 1;
			}
			if (pass == 1 && h.kind == Hit::hit_Pin)
			{
				mPinnedID = mPinnedID == h.ramp_id ? -1 : h.ramp_id;
				GetHost()->Refresh();
				return 1;
			}
			if (pass == 1 && h.kind == Hit::hit_Copy)
			{
				GUI_SetTextToClipboard(WED_ModerationReport(WED_GetCurrentAirport(GetResolver()), mReviewed));
				mCopiedUntil = NowSec() + 2.0;
				GetHost()->Refresh();
				return 1;
			}
			if (pass == 1 && h.kind == Hit::hit_Filter)
			{
				mListFilter = h.ramp_id;
				mListScroll = 0;
				GetHost()->Refresh();
				return 1;
			}
			if (pass == 1 && h.kind == Hit::hit_Sort)
			{
				mListSort = 1 - mListSort;
				mListScroll = 0;
				GetHost()->Refresh();
				return 1;
			}
			if (pass == 1 && h.kind == Hit::hit_Focus)
			{
				Focus(h.ramp_id);
				return 1;
			}
			if (pass == 1 && h.kind == Hit::hit_Chip && (modifiers & gui_ShiftFlag))
			{
				// shift-click a chip: make it the base, as the card's pin does
				mPinnedID = mPinnedID == h.ramp_id ? -1 : h.ramp_id;
				GetHost()->Refresh();
				return 1;
			}
			if (pass == 1 && h.kind == Hit::hit_Legend && h.ramp_id >= 0 && h.ramp_id < (int) mLegendIDs.size())
			{
				// select that entry's stands
				ISelection * sel = WED_GetSelect(GetResolver());
				WED_Thing * wrl = WED_GetWorld(GetResolver());
				if (sel && wrl)
				{
					IOperation * op = dynamic_cast<IOperation *>(sel);
					if (op) op->StartOperation("Select Ramp Starts");
					sel->Clear();
					const vector<int> & ids = mLegendIDs[h.ramp_id];
					for (size_t k = 0; k < ids.size(); ++k)
						if (WED_RampPosition * r = dynamic_cast<WED_RampPosition *>(wrl->FetchPeer(ids[k])))
							sel->Insert(r);
					if (op) op->CommitOperation();
				}
				return 1;
			}
			// A click on a card, chip or tray is ours - the map tool under it
			// must not take it as a click on empty ground and drop the selection.
			if (pass == 2 && (h.kind == Hit::hit_Card || h.kind == Hit::hit_Tray || h.kind == Hit::hit_Chip || h.kind == Hit::hit_Panel))
				return 1;
		}
	return 0;
}
