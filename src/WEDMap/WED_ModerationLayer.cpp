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
#include "GUI_Help.h"
#include "GUI_Pane.h"

#include <algorithm>
#include <chrono>
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

// Callouts are for reading a handful of stands; a whole airport's worth would
// only be noise on top of the map.
static const size_t kMaxCallouts = 40;

static const float kWhite[4]  = { 0.93f, 0.93f, 0.93f, 1.0f };
static const float kMuted[4]  = { 0.62f, 0.62f, 0.64f, 1.0f };
static const float kGreen[4]  = { 0.45f, 0.85f, 0.45f, 1.0f };
static const float kRed[4]    = { 1.00f, 0.45f, 0.40f, 1.0f };
static const float kAmber[4]  = { 1.00f, 0.75f, 0.30f, 1.0f };

// Not std::max: <windows.h>, force-included through XDefs.h, defines min/max macros.
static inline float	Max(float a, float b) { return a > b ? a : b; }
static inline float	Min(float a, float b) { return a < b ? a : b; }

static void	HsvToRgb(float h, float s, float v, float out[4])
{
	h = h - floorf(h);
	float r, g, b;
	int i = (int) (h * 6.0f);
	float f = h * 6.0f - (float) i;
	float p = v * (1 - s), q = v * (1 - f * s), t = v * (1 - (1 - f) * s);
	switch (i % 6) {
	case 0: r = v; g = t; b = p; break;
	case 1: r = q; g = v; b = p; break;
	case 2: r = p; g = v; b = t; break;
	case 3: r = p; g = q; b = v; break;
	case 4: r = t; g = p; b = v; break;
	default: r = v; g = p; b = q; break;
	}
	out[0] = r; out[1] = g; out[2] = b; out[3] = 1.0f;
}

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

static void	Rect(float x0, float y0, float x1, float y1, const float c[4], bool fill)
{
	glColor4fv(c);
	glBegin(fill ? GL_QUADS : GL_LINE_LOOP);
	glVertex2f(x0, y0); glVertex2f(x1, y0); glVertex2f(x1, y1); glVertex2f(x0, y1);
	glEnd();
}

static bool	Inside(float x, float y, float x0, float y0, float x1, float y1)
{
	return x >= x0 && x <= x1 && y >= y0 && y <= y1;
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
		const float r = s * 0.72f;
		glColor4fv(kAmber);
		// Wound clockwise: counter-clockwise shapes are back faces to WED's
		// culling here (the Liveries tab hit the same thing).
		glBegin(GL_TRIANGLE_FAN);
		glVertex2f(x + s * 0.5f, cy);
		for (int i = 0; i <= 20; ++i)
		{
			float a = -(float) i / 20.0f * 6.2831853f;
			glVertex2f(x + s * 0.5f + cosf(a) * r, cy + sinf(a) * r);
		}
		glEnd();
		const float ink[4] = { 0.10f, 0.08f, 0.02f, 1.0f };
		GUI_FontDraw(g, font_UI_Basic, ink, x + s * 0.5f, cy - GUI_GetLineAscent(font_UI_Basic) * 0.36f, "?", align_Center);
		break;
	}
	default:
		break;
	}
	glLineWidth(1.0f);
}

WED_ModerationLayer::WED_ModerationLayer(GUI_Pane * host, WED_MapZoomerNew * zoomer, IResolver * resolver) :
	WED_MapLayer(host, zoomer, resolver), mPinnedID(-1), mTrayID(-1)
{
	long long t = std::chrono::steady_clock::now().time_since_epoch().count();
	mHueSeed = (float) ((t / 1000) % 1000) / 1000.0f;
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

void	WED_ModerationLayer::ColourFor(const string & signature, float rgba[4])
{
	std::map<string, int>::iterator i = mSigIndex.find(signature);
	int slot = i != mSigIndex.end() ? i->second : (mSigIndex[signature] = (int) mSigIndex.size());
	// Golden-ratio steps round the hue circle: each new signature lands as far
	// as it can from the ones before it, so neighbouring slots never look alike.
	HsvToRgb(mHueSeed + (float) slot * 0.6180339f, 0.62f, 0.97f, rgba);
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

void	WED_ModerationLayer::Collect(vector<Callout> & out)
{
	out.clear();
	set<WED_Thing *> sel;
	WED_GetSelectionRecursive(GetResolver(), sel);

	vector<WED_RampPosition *> ramps;
	for (set<WED_Thing *>::const_iterator t = sel.begin(); t != sel.end(); ++t)
		if (WED_RampPosition * r = dynamic_cast<WED_RampPosition *>(*t))
			ramps.push_back(r);

	// The pinned stand stays on screen while others are selected - that is
	// what makes it a base to compare against. A stand that has been deleted
	// is no longer in the archive, or no longer has a parent: unpin it.
	WED_RampPosition * pinned = NULL;
	if (mPinnedID >= 0)
	{
		WED_Thing * wrl = WED_GetWorld(GetResolver());
		pinned = wrl ? dynamic_cast<WED_RampPosition *>(wrl->FetchPeer(mPinnedID)) : NULL;
		if (!pinned || !pinned->GetParent()) { pinned = NULL; mPinnedID = -1; }
		else if (std::find(ramps.begin(), ramps.end(), pinned) == ramps.end()) ramps.insert(ramps.begin(), pinned);
	}
	if (ramps.size() > kMaxCallouts) ramps.resize(kMaxCallouts);

	double b[4];
	GetZoomer()->GetPixelBounds(b[0], b[1], b[2], b[3]);
	for (size_t i = 0; i < ramps.size(); ++i)
	{
		Callout c;
		c.ramp = ramps[i];
		c.id   = ramps[i]->GetID();
		Point2 ll;
		ramps[i]->GetLocation(gis_Geo, ll);
		Point2 px = GetZoomer()->LLToPixel(ll);
		c.ax = (float) px.x();
		c.ay = (float) px.y();
		if (c.ax < b[0] || c.ax > b[2] || c.ay < b[1] || c.ay > b[3]) continue;		// off screen
		WED_ModerationDescribe(ramps[i], WED_GetParentAirport(ramps[i]), c.e);
		c.x0 = c.y0 = c.x1 = c.y1 = c.tray_y = 0;
		out.push_back(c);
	}
}

static string	SizeText(const WED_ModerationEntry & e)
{
	if (!e.updated) return string("size ") + e.size_letter;
	string w = WED_ModerationWeightsText(e.weights);
	return w.empty() ? string("all weights zero - nothing parks") : w;
}

void	WED_ModerationLayer::Diff(const WED_ModerationEntry & base, Callout & c) const
{
	set<string> a, b;
	for (size_t i = 0; i < base.codes.size(); ++i) a.insert(base.codes[i].code);
	for (size_t i = 0; i < c.e.codes.size(); ++i)  b.insert(c.e.codes[i].code);
	string added, removed;
	for (set<string>::const_iterator i = b.begin(); i != b.end(); ++i) if (!a.count(*i)) added   += " " + *i;
	for (set<string>::const_iterator i = a.begin(); i != a.end(); ++i) if (!b.count(*i)) removed += " " + *i;
	if (!added.empty())   { c.diff.push_back("+" + added);   c.diff_kind.push_back(1); }
	if (!removed.empty()) { c.diff.push_back("-" + removed); c.diff_kind.push_back(-1); }

	const string sa = SizeText(base), sb = SizeText(c.e);
	if (sa != sb)								{ c.diff.push_back("~ " + sa + "  ->  " + sb); c.diff_kind.push_back(0); }
	if (base.op_label != c.e.op_label)			{ c.diff.push_back("~ " + base.op_label + "  ->  " + c.e.op_label); c.diff_kind.push_back(0); }
	if (base.equipment != c.e.equipment)		{ c.diff.push_back("~ " + base.equipment + "  ->  " + c.e.equipment); c.diff_kind.push_back(0); }
	if (base.ramp_type != c.e.ramp_type)		{ c.diff.push_back("~ " + base.ramp_type + "  ->  " + c.e.ramp_type); c.diff_kind.push_back(0); }
	if (c.diff.empty())							{ c.diff.push_back("= same entry as the pinned stand"); c.diff_kind.push_back(2); }
}

// ---- one card ----

static const float kPad = 6.0f;

static float	LineH(void)		{ return GUI_GetLineHeight(font_UI_Basic); }
static float	RowH(void)		{ return LineH() + 3.0f; }
static float	HeadH(void)		{ return LineH() + 8.0f; }

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

void	WED_ModerationLayer::DrawCard(GUI_GraphState * g, Callout & c, bool pinned)
{
	const WED_ModerationEntry & e = c.e;
	float stroke[4];
	ColourFor(e.signature, stroke);

	const float lh = LineH(), asc = GUI_GetLineAscent(font_UI_Basic);
	const float x0 = c.x0, x1 = c.x1, y1 = c.y1, y0 = c.y0;
	const float label_w = TextW("Airlines") + 10.0f;

	// leader: stand -> elbow -> card, with the arrowhead at the stand
	const float head_mid = y1 - HeadH() * 0.5f;
	g->SetState(0, 0, 0, 0, 1, 0, 0);
	glColor4fv(stroke);
	glLineWidth(2.0f);
	glBegin(GL_LINE_STRIP);
		glVertex2f(c.ax + 6, c.ay);
		glVertex2f(x0 - 16, c.ay);
		glVertex2f(x0 - 16, head_mid);
		glVertex2f(x0, head_mid);
	glEnd();
	glBegin(GL_TRIANGLES);
		glVertex2f(c.ax, c.ay);				// clockwise, see DrawMark
		glVertex2f(c.ax + 10, c.ay - 5);
		glVertex2f(c.ax + 10, c.ay + 5);
	glEnd();

	// card body, stroke, and a stroke-tinted header strip
	const float bg[4] = { 0.09f, 0.09f, 0.10f, 0.93f };
	Rect(x0, y0, x1, y1, bg, true);
	const float tint[4] = { stroke[0] * 0.35f, stroke[1] * 0.35f, stroke[2] * 0.35f, 0.95f };
	Rect(x0, y1 - HeadH(), x1, y1, tint, true);
	glLineWidth(pinned ? 3.0f : 2.0f);
	Rect(x0, y0, x1, y1, stroke, false);
	glLineWidth(1.0f);

	// header: flag, ICAO, ramp name - left-aligned and tight
	float hx = x0 + kPad;
	const float hb = y1 - HeadH() + (HeadH() - lh) * 0.5f;		// header row bottom
	if (const Flag * f = FlagFor(e.country))
	{
		DrawFlag(g, f->tex, f->w, f->h, hx, hb + 1, lh - 2);
		hx += (lh - 2) * (float) f->w / (float) f->h + 5;
	}
	const float base_y = hb + (lh - asc) * 0.5f + 1;
	GUI_FontDraw(g, font_UI_Basic, kWhite, hx, base_y, e.icao.c_str());
	hx += TextW(e.icao) + 7;
	const float pin_x = x1 - kPad - 7;
	float name_end = pin_x - 10;
	if (pinned)
	{
		const float bw = TextW("BASE");
		name_end -= bw + 6;
		GUI_FontDraw(g, font_UI_Basic, stroke, pin_x - 10 - bw, base_y, "BASE");
	}
	GUI_FontDraw(g, font_UI_Basic, kWhite, hx, base_y, Elide(e.ramp_name, name_end - hx).c_str());

	// pin: filled when this is the base
	g->SetState(0, 0, 0, 0, 1, 0, 0);
	glColor4fv(pinned ? stroke : kMuted);
	glBegin(pinned ? GL_TRIANGLE_FAN : GL_LINE_LOOP);
	if (pinned) glVertex2f(pin_x, head_mid + 2);
	for (int i = 0; i < (pinned ? 17 : 16); ++i)
	{
		float a = -(float) i / 16.0f * 6.2831853f;		// clockwise, see DrawMark
		glVertex2f(pin_x + cosf(a) * 4.5f, head_mid + 2 + sinf(a) * 4.5f);
	}
	glEnd();
	glBegin(GL_LINES);
		glVertex2f(pin_x, head_mid - 2.5f); glVertex2f(pin_x, head_mid - 7.5f);
	glEnd();
	Hit ph = { Hit::hit_Pin, pin_x - 9, head_mid - 9, pin_x + 9, head_mid + 9, c.id, "" };
	mHits.push_back(ph);

	// rows
	float y = y1 - HeadH() - kPad;
	const float vx = x0 + kPad + label_w;
	const float room = x1 - kPad - vx;
	GUI_FontDraw(g, font_UI_Basic, kMuted, x0 + kPad, y - asc, "Airlines");
	GUI_FontDraw(g, font_UI_Basic, kWhite, vx, y - asc, Elide(AirlinesText(e), room).c_str());
	y -= RowH();

	string st = StateText(e);
	GUI_FontDraw(g, font_UI_Basic, e.updated ? kWhite : kAmber, x0 + kPad, y - asc, st.c_str());
	GUI_FontDraw(g, font_UI_Basic, kWhite, x0 + kPad + Max(label_w, TextW(st) + 10), y - asc, SizeText(e).c_str());
	y -= RowH();

	GUI_FontDraw(g, font_UI_Basic, kMuted, x0 + kPad, y - asc, Elide(KindText(e), x1 - x0 - kPad * 2).c_str());
	y -= RowH();

	for (size_t i = 0; i < c.diff.size(); ++i)
	{
		const int k = c.diff_kind[i];
		const float * col = k == -1 ? kRed : k == 0 ? kAmber : kGreen;		// +, same: green
		GUI_FontDraw(g, font_UI_Basic, col, x0 + kPad, y - asc, Elide(c.diff[i], x1 - x0 - kPad * 2).c_str());
		y -= lh;
	}

	// the tray row: a disclosure triangle and the verification summary
	c.tray_y = y;
	const float * vcol;
	string vt = VerifyText(e, &vcol);
	const bool open = mTrayID == c.id;
	const float ty = y - lh * 0.5f - 1;
	g->SetState(0, 0, 0, 0, 1, 0, 0);
	glColor4fv(vcol);
	glBegin(GL_TRIANGLES);
	if (open) { glVertex2f(x0 + kPad, ty + 3); glVertex2f(x0 + kPad + 8, ty + 3); glVertex2f(x0 + kPad + 4, ty - 3); }
	else      { glVertex2f(x0 + kPad + 1, ty + 4); glVertex2f(x0 + kPad + 7, ty); glVertex2f(x0 + kPad + 1, ty - 4); }
	glEnd();
	GUI_FontDraw(g, font_UI_Basic, vcol, x0 + kPad + 14, y - asc, Elide(vt, x1 - x0 - kPad * 2 - 14).c_str());
	Hit th = { Hit::hit_Tray, x0, y0, x1, y + 1, c.id, "" };
	mHits.push_back(th);

	Hit ch = { Hit::hit_Card, x0, y0, x1, y1, c.id, "" };
	mHits.push_back(ch);
}

void	WED_ModerationLayer::DrawTray(GUI_GraphState * g, Callout & c)
{
	const WED_ModerationEntry & e = c.e;
	const float lh = LineH(), asc = GUI_GetLineAscent(font_UI_Basic);
	const float x0 = c.x0, x1 = c.x1;
	const float item_w = (x1 - x0 - kPad * 2) / 3.0f;
	const float item_h = lh + 5;

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

	// Notes wrap to the tray's width - an elided "Click..." hid the instruction.
	vector<string> lines;
	const float text_w = x1 - x0 - kPad * 2;
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
	const float h = kPad * 2 + lines.size() * lh + rows * item_h;
	const float top = c.y0 + 1, bot = top - h;

	float stroke[4];
	ColourFor(e.signature, stroke);
	const float bg[4] = { 0.12f, 0.12f, 0.13f, 0.97f };
	g->SetState(0, 0, 0, 0, 1, 0, 0);
	Rect(x0, bot, x1, top, bg, true);
	glLineWidth(2.0f);
	Rect(x0, bot, x1, top, stroke, false);
	glLineWidth(1.0f);
	Hit panel = { Hit::hit_Tray, x0, bot, x1, top, c.id, "" };
	mHits.push_back(panel);

	float y = top - kPad;
	for (size_t i = 0; i < lines.size(); ++i)
	{
		GUI_FontDraw(g, font_UI_Basic, kMuted, x0 + kPad, y - asc, lines[i].c_str());
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
		GUI_FontDraw(g, font_UI_Basic, kWhite, x, iy_top - asc - 2, k.code.c_str());
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
			GUI_FontDraw(g, font_UI_Basic, kMuted, x, iy_top - asc - 2, ",");
	}
}

void	WED_ModerationLayer::DrawSelected(bool inCurrent, GUI_GraphState * g)
{
	if (!WED_ModerationEnabled()) { mHits.clear(); return; }

	// Which tray is open: the one the mouse was over last frame.
	int mx, my;
	GetHost()->GetMouseLocNow(&mx, &my);
	mTrayID = -1;
	for (size_t i = 0; i < mHits.size(); ++i)
		if (mHits[i].kind == Hit::hit_Tray && Inside((float) mx, (float) my, mHits[i].x0, mHits[i].y0, mHits[i].x1, mHits[i].y1))
			{ mTrayID = mHits[i].ramp_id; break; }
	mHits.clear();

	vector<Callout> cs;
	Collect(cs);
	if (cs.empty()) return;

	const WED_ModerationEntry * base = NULL;
	for (size_t i = 0; i < cs.size(); ++i) if (cs[i].id == mPinnedID) base = &cs[i].e;
	if (base)
		for (size_t i = 0; i < cs.size(); ++i)
			if (cs[i].id != mPinnedID) Diff(*base, cs[i]);

	// Size every card, then stack them to the right of their stands, top down,
	// pushing a card down past any it would overlap.
	for (size_t i = 0; i < cs.size(); ++i)
	{
		Callout & c = cs[i];
		const WED_ModerationEntry & e = c.e;
		float w = TextW(e.icao) + TextW(e.ramp_name) + LineH() * 1.6f + 40;
		w = Max(w, TextW("Airlines") + 10 + TextW(AirlinesText(e)));
		w = Max(w, TextW(StateText(e)) + 20 + TextW(SizeText(e)));
		w = Max(w, TextW(KindText(e)));
		const float * vc;
		w = Max(w, TextW(VerifyText(e, &vc)) + 14);
		for (size_t d = 0; d < c.diff.size(); ++d) w = Max(w, TextW(c.diff[d]));
		w = Min(Max(w + kPad * 2, 240.0f), 420.0f);
		const float h = HeadH() + kPad + 3 * RowH() + c.diff.size() * LineH() + RowH() + kPad;
		c.x0 = c.ax + 48;
		c.x1 = c.x0 + w;
		c.y1 = c.ay + HeadH() * 0.5f;
		c.y0 = c.y1 - h;
	}
	vector<size_t> order(cs.size());
	for (size_t i = 0; i < order.size(); ++i) order[i] = i;
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
	for (size_t i = 0; i < cs.size(); ++i)
	{
		DrawCard(g, cs[i], cs[i].id == mPinnedID);
		if (cs[i].id == mTrayID) open = &cs[i];
	}
	if (open) DrawTray(g, *open);		// last, over everything

	g->SetState(0, 0, 0, 0, 0, 0, 0);
	glLineWidth(1.0f);
}

int		WED_ModerationLayer::HandleClickDown(int inX, int inY, int inButton, GUI_KeyFlags modifiers)
{
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
				GUI_LaunchURL(h.url.c_str());
				return 1;
			}
			if (pass == 1 && h.kind == Hit::hit_Pin)
			{
				mPinnedID = mPinnedID == h.ramp_id ? -1 : h.ramp_id;
				GetHost()->Refresh();
				return 1;
			}
			// A click on a card or its tray is the card's - the map tool under it
			// must not take it as a click on empty ground and drop the selection.
			if (pass == 2 && (h.kind == Hit::hit_Card || h.kind == Hit::hit_Tray))
				return 1;
		}
	return 0;
}
