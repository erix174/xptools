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

// Drawing and animation.


// Loads the country's flag source raster, projects it through the fixed
// pole/mask/ink UV mesh (WED_FlagProjector - see that file's header for the
// approved-defaults contract this ports), and uploads the result as a GL
// texture. No-ops if the requested country is already the one currently
// uploaded - this is a ~2048x768 warp+composite, not something to redo on
// every Draw() call. Pass "" to release the texture (nothing to show).
void	WED_LiveryPane::EnsureFlagTexture(const string & ioc_country_code)
{
	if (ioc_country_code == mFlagTexCountry) return;

	if (mFlagTexId != 0)
	{
		glDeleteTextures(1, &mFlagTexId);
		mFlagTexId = 0;
		mFlagTexW = mFlagTexH = 0;
	}
	mFlagTexCountry = ioc_country_code;
	if (ioc_country_code.empty()) return;

	string path = WED_FlagSourcePathForCountry(ioc_country_code);
	LOG_MSG("I/EnsureFlagTexture: country=%s path=%s\n", ioc_country_code.c_str(), path.c_str());
		LOG_FLUSH();
	vector<uint32_t> source_argb;
	int src_w = 0, src_h = 0;
	if (!WED_LoadPngTopDownARGB(path, source_argb, src_w, src_h))
	{
		LOG_MSG("I/EnsureFlagTexture: source PNG load FAILED\n");
		LOG_FLUSH();
		return;
	}
	LOG_MSG("I/EnsureFlagTexture: source PNG loaded %dx%d\n", src_w, src_h);
		LOG_FLUSH();

	vector<uint32_t> composited;
	if (!WED_ProjectFlag(source_argb, src_w, src_h, composited))
	{
		LOG_MSG("I/EnsureFlagTexture: WED_ProjectFlag FAILED\n");
		LOG_FLUSH();
		return;
	}
	LOG_MSG("I/EnsureFlagTexture: projected OK, %d pixels\n", (int) composited.size());
		LOG_FLUSH();

	// composited is top-down 0xAARRGGBB; glTexImage2D wants a byte order we
	// can address directly, so hand it the buffer as GL_BGRA/GL_UNSIGNED_BYTE
	// (matches this machine's 0xAARRGGBB-in-a-uint32 layout on little-endian,
	// same "OpenGL convention" BitmapUtils.h already documents) and flip the
	// V texture-coordinate at draw time instead of flipping row order here.
	// &v[0] on an empty vector is undefined - and the decoder sizes its output
	// from the PNG's own dimensions, which come from the file.
	if (composited.empty()) return;

	glGenTextures(1, &mFlagTexId);
	glBindTexture(GL_TEXTURE_2D, mFlagTexId);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, WED_FlagAssets::Width(), WED_FlagAssets::Height(), 0,
					GL_BGRA, GL_UNSIGNED_BYTE, &composited[0]);
	mFlagTexW = WED_FlagAssets::Width();
	mFlagTexH = WED_FlagAssets::Height();
}

// Small, unmasked (plain rectangular, natural aspect ratio) flag icon for a card
// header - deliberately NOT the WED_ProjectFlag() pole/cloth-mask pipeline above
// (that's for the one big banner; this is a per-card icon). Cached for the pane's
// lifetime (freed in Hide()/~WED_LiveryPane()) rather than evicted per-scroll like the
// card thumbnails - there are far fewer distinct countries than distinct cards.
// Returns an entry with tex == 0 if the source PNG couldn't be loaded - never draw it.
const WED_LiveryThumbnail *	WED_LiveryPane::EnsureRawFlagTexture(const string & ioc_country_code)
{
	map<string, WED_LiveryThumbnail>::iterator it = mRawFlagTex.find(ioc_country_code);
	if (it != mRawFlagTex.end())
		return &it->second;

	WED_LiveryThumbnail entry;

	string path = WED_FlagSourcePathForCountry(ioc_country_code);
	vector<uint32_t> source_argb;
	int src_w = 0, src_h = 0;
	if (WED_LoadPngTopDownARGB(path, source_argb, src_w, src_h) &&
		!source_argb.empty() && src_w > 0 && src_h > 0)
	{
		glGenTextures(1, &entry.tex);
		glBindTexture(GL_TEXTURE_2D, entry.tex);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
		glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, src_w, src_h, 0, GL_BGRA, GL_UNSIGNED_BYTE, &source_argb[0]);
		entry.w = src_w;
		entry.h = src_h;
	}

	return &mRawFlagTex.emplace(ioc_country_code, entry).first->second;
}

// ANIMATION IS DRIVEN FROM Draw(), NOT FROM A TIMER. GUI_Timer's Windows path is
// SetTimer(NULL, 0, ...) - a thread timer whose WM_TIMER only arrives if the
// message loop dispatches messages with a NULL hwnd. It does not here: the log
// showed Start() being called on every hover (busy=1) and TimerFired running
// exactly zero times, which is why neither the hover cycle nor the tray ever
// moved while both looked correct in every other respect.
//
// Advancing on wall-clock time inside Draw() and asking for another frame while
// anything is still moving needs no platform support and is the same pattern the
// progressive thumbnail fill in this pane already uses successfully. It also
// self-limits: when nothing is animating no extra frame is requested.
//
// Returns true when something is still in motion, so Draw() knows to come back.
bool	WED_LiveryPane::StepAnimation(void)
{
	// Wall-clock time. clock() is CPU time everywhere but Windows, so on a Mac or
	// Linux an idle WED barely advanced it and every animation crawled.
	double now = PaneClockNow();
	if (mLastAnimClock == 0.0) { mLastAnimClock = now; return false; }

	float dt = (float) (now - mLastAnimClock);
	mLastAnimClock = now;

	// A frame that took a long time - the window was hidden, or a thumbnail parse
	// ran long - must not teleport the animation to its end.
	if (dt < 0.0f)  dt = 0.0f;
	if (dt > 0.1f)  dt = 0.1f;

	bool moving = false;

	const float kTraySpeed = 4.0f;				// full travel in a quarter second
	if (!mTrayAirline.empty() && mTrayOpen < 1.0f)
	{
		mTrayOpen = (std::min)(1.0f, mTrayOpen + dt * kTraySpeed);
		moving = true;
	}
	if (!mTrayClosing.empty())
	{
		mTrayClosingOpen -= dt * kTraySpeed;
		if (mTrayClosingOpen <= 0.0f) { mTrayClosingOpen = 0.0f; mTrayClosing.clear(); }
		moving = true;
	}

	// One aircraft per second on the hovered card. mCycleShow is advanced blind and
	// wrapped by the drawer against the card's actual length, because the card can
	// change under the cursor - a weight drag can remove a whole class - and
	// clamping here would need the card, which this does not have.
	if (!mCycleAirline.empty() && mTrayHoverIdx < 0)
	{
		mCycleAccum += dt;
		if (mCycleAccum >= 1.0f)
		{
			if (mCycleNextReady)
			{
				mCycleAccum -= 1.0f;
				mCyclePrevShow = mCycleShow++;
				mCycleFade = 0.0f;				// the new face fades in over the old one
			}
			else
				mCycleAccum = 1.0f;				// hold on this face until the next one has loaded
		}
		moving = true;
	}
	if (mCycleFade < kCycleFadeSec)
	{
		mCycleFade = (std::min)(kCycleFadeSec, mCycleFade + dt);
		moving = true;
	}

	return moving;
}

// The list under an open card: every aircraft that operator can park at this
// stand, biggest first, boxed one per line. Clipped to the animated height, so it
// is revealed rather than scaled - text that scaled would shimmer.
void	WED_LiveryPane::DrawCardTray(GUI_GraphState * state, const RowSlot & slot,
									 const AirlineCard & card, float open_frac, int lit_row)
{
	if (open_frac <= 0.0f || card.abs_paths.empty()) return;

	float full = TrayFullHeight(card.abs_paths.size());
	float h    = full * open_frac;
	float top  = slot.bot;
	float bot  = top - h;

	state->SetState(0,0,0,0,1,0,0);
	glColor4f(0.16f, 0.16f, 0.18f, 0.97f);
	glBegin(GL_QUADS);
		glVertex2f(slot.x0, bot);  glVertex2f(slot.x1, bot);
		glVertex2f(slot.x1, top);  glVertex2f(slot.x0, top);
	glEnd();

	glColor4f(0.40f, 0.40f, 0.44f, 1.0f);
	glBegin(GL_LINE_LOOP);
		glVertex2f(slot.x0 + 0.5f, bot + 0.5f);  glVertex2f(slot.x1 - 0.5f, bot + 0.5f);
		glVertex2f(slot.x1 - 0.5f, top - 0.5f);  glVertex2f(slot.x0 + 0.5f, top - 0.5f);
	glEnd();

	float line_h = GUI_GetLineHeight(font_UI_Basic);
	float txt[4] = { 0.86f, 0.86f, 0.88f, 1.0f };
	for (size_t i = 0; i < card.labels.size(); ++i)
	{
		float ry = top - kTrayPad - (float) (i + 2) * kTrayRowH;	// +1 for the band
		if (ry < bot) break;					// still sliding open - the rest is not revealed yet

		// The lit row is whatever is on the card's face right now, so the tray and
		// the picture above it always agree about which aircraft is being shown.
		if ((int) i == lit_row) glColor4f(0.30f, 0.46f, 0.36f, 1.0f);
		else                    glColor4f(0.24f, 0.24f, 0.27f, 1.0f);
		glBegin(GL_QUADS);
			glVertex2f(slot.x0 + kTrayPad,          ry + 1);
			glVertex2f(slot.x1 - kTrayPad,          ry + 1);
			glVertex2f(slot.x1 - kTrayPad,          ry + kTrayRowH - 2);
			glVertex2f(slot.x0 + kTrayPad,          ry + kTrayRowH - 2);
		glEnd();

		GUI_FontDraw(state, font_UI_Basic, txt, slot.x0 + kTrayPad + 5,
					 ry + (kTrayRowH - line_h) * 0.5f + 1, card.labels[i].c_str());
	}

	// The tray shows what this operator HAS; it is not a second place to choose
	// from. Saying so is cheaper than letting someone discover it by clicking - the
	// per-stand aircraft whitelist that would have made these selectable is exactly
	// what draft 7 removed (spec 8.6).
	if (open_frac > 0.6f)
	{
		float pc[4] = { 0.62f, 0.62f, 0.66f, 1.0f };
		const char * only = "Preview ONLY";
		float ow = GUI_MeasureRange(font_UI_Basic, only, only + strlen(only));
		GUI_FontDraw(state, font_UI_Basic, pc, slot.x1 - kTrayPad - ow,
					 top - kTrayPad - kTrayRowH + (kTrayRowH - line_h) * 0.5f + 1.0f, only);
	}
}


// The operator's full aircraft list, at the cursor. The caption can only fit
// "(and 3 more)" - this is what says WHICH three, without making the reader open
// the tray to find out that none of them was what they wanted.
//
// Drawn LAST in Draw(), after the scissor is popped, for the usual reason a
// tooltip is: it has to be allowed outside the box that spawned it, and a card
// near the bottom of the list has nowhere else to put it.
void	WED_LiveryPane::DrawHoverTip(GUI_GraphState * state, int b[4])
{
	if (mHoverTipText.empty()) return;
	const string & text = mHoverTipText;

	float line_h = GUI_GetLineHeight(font_UI_Basic);
	float pad    = 6.0f;
	// Wrapped to the pane: a tip wider than the pane - the list Populate added,
	// say - was cut off at the border, and the end of it is usually the point.
	float max_tw = (float) (b[2] - b[0]) - 4.0f - pad * 2.0f;
	vector<string> lines = WrapText(font_UI_Basic, text, max_tw);
	if (lines.empty()) lines.push_back(text);
	float tw = 0.0f;
	for (size_t i = 0; i < lines.size(); ++i)
		tw = (std::max)(tw, GUI_MeasureRange(font_UI_Basic, lines[i].c_str(), lines[i].c_str() + lines[i].size()));
	float w      = tw + pad * 2.0f;
	float h      = line_h * (float) lines.size() + pad * 2.0f - 2.0f;

	// Flip to the other side of the cursor rather than being clipped - a tip that
	// runs off the pane is worse than no tip, because the part that falls off is
	// the end of the list.
	float x0 = (float) mHoverX + 14.0f;
	if (x0 + w > (float) b[2] - 2.0f) x0 = (float) mHoverX - 14.0f - w;
	if (x0 < (float) b[0] + 2.0f)     x0 = (float) b[0] + 2.0f;

	float y1 = (float) mHoverY + 6.0f + h;
	if (y1 > (float) b[3] - 2.0f) y1 = (float) mHoverY - 6.0f;
	float y0 = y1 - h;

	state->SetState(0,0,0,0,1,0,0);
	glColor4f(0.0f, 0.0f, 0.0f, 0.30f);
	glBegin(GL_QUADS);
		glVertex2f(x0+2, y0-2);  glVertex2f(x0+w+2, y0-2);
		glVertex2f(x0+w+2, y1-2); glVertex2f(x0+2, y1-2);
	glEnd();
	glColor4f(0.13f, 0.13f, 0.16f, 0.98f);
	glBegin(GL_QUADS);
		glVertex2f(x0, y0);  glVertex2f(x0+w, y0);
		glVertex2f(x0+w, y1); glVertex2f(x0, y1);
	glEnd();
	glColor4f(0.45f, 0.45f, 0.50f, 1.0f);
	glBegin(GL_LINE_LOOP);
		glVertex2f(x0+0.5f, y0+0.5f);   glVertex2f(x0+w-0.5f, y0+0.5f);
		glVertex2f(x0+w-0.5f, y1-0.5f); glVertex2f(x0+0.5f, y1-0.5f);
	glEnd();

	float tc[4] = { 0.90f, 0.90f, 0.93f, 1.0f };
	for (size_t i = 0; i < lines.size(); ++i)
		GUI_FontDraw(state, font_UI_Basic, tc, x0 + pad,
					 y1 - pad - line_h * (float) (i + 1) + 1.0f, lines[i].c_str());
}

// ONE CARD. Everything it needs is passed in: it is called from the row loop now,
// once per airline row, rather than from a block of its own above the checklist.
// `show` picks which of the operator's liveries is on the face - see AirlineCard
// in the .h for the ordering, and why index 0 is what a card shows at rest.
void	WED_LiveryPane::DrawAirlineCard(GUI_GraphState * state, const RowSlot & slot,
										const AirlineCard & card, int show,
										bool is_selected, bool is_hover, bool is_pressed,
										bool is_locked, bool is_dimmed,
										float tray_open,
										int & renders_this_frame,
										int fade_from, float fade)
{
	if (card.abs_paths.empty()) return;
	if (show < 0 || show >= (int) card.abs_paths.size()) show = 0;
	const string & abs_path = card.abs_paths[show];
	const string & type_str = card.labels[show];

	float line_h = GUI_GetLineHeight(font_UI_Basic);

	WED_ResourceMgr * res_mgr = WED_GetResourceMgr(mResolver);
	ITexMgr *         tex_mgr = WED_GetTexMgr(mResolver);

	float card_x0   = slot.x0, card_x1 = slot.x1;
	float card_bot  = slot.bot, card_top = slot.top;
	float image_h   = (card_x1 - card_x0) / kCardImageAspect;	// must match CardHeight()
	float image_top = card_top;
	float image_bot = image_top - image_h;
	float text_bot  = card_bot;

	// --- drop shadow: a few offset, increasingly transparent slabs down
	// and to the right. Cheap stand-in for a real blur (no shader/FBO
	// pass needed) and enough to lift the card off the panel so the grid
	// reads as separate cards rather than one tiled sheet. ---
	state->SetState(0,0,0,0,1,0,0);		// blend on, no texture
	for (int s = 3; s >= 1; --s)
	{
		glColor4f(0, 0, 0, 0.13f);
		glBegin(GL_QUADS);
			glVertex2f(card_x0 + s, card_bot - s);
			glVertex2f(card_x1 + s, card_bot - s);
			glVertex2f(card_x1 + s, card_top - s);
			glVertex2f(card_x0 + s, card_top - s);
		glEnd();
	}

	// --- card body: the image sits directly on this, and the thumbnail's
	// transparent background lets it show through (see the blend note on
	// the image quad below), so this IS the picture's backdrop. Selected
	// cards sit "pressed in" (darker); hover lifts it slightly. ---
	// Cards are previews, not a picker - there is no selected state left
	// to draw. Hover survives because pointing at a card and having it
	// respond is how the strip reads as a list of distinct things.
	// NO PRESSED STATE. A press that is not also a release means nothing here - one
	// click already does the whole job - so darkening the card mid-gesture only
	// made it flicker on the way to the thing the user wanted.
	(void) is_pressed;
	bool is_hovered  = is_hover;

	// A selected card swaps its whole body from neutral grey to the
	// picker's green (0x639875). The sheen drawn later is plain white at
	// low alpha, so it lightens whatever is underneath - over the green
	// that reads as a brighter green band, which is what makes the
	// selected state obvious at a glance rather than subtle.
	float body_r, body_g, body_b;
	if (is_selected)
	{
		body_r = 0.388f; body_g = 0.596f; body_b = 0.459f;		// 0x639875
		float k = is_hovered ? 1.12f : 1.0f;
		body_r = (std::min)(1.0f, body_r * k);
		body_g = (std::min)(1.0f, body_g * k);
		body_b = (std::min)(1.0f, body_b * k);
	}
	else
	{
		float g = is_hovered ? 0.21f : 0.17f;
		body_r = g; body_g = g; body_b = g + 0.02f;
	}

	state->SetState(0,0,0,0,0,0,0);
	glColor4f(body_r, body_g, body_b, 1.0f);
	glBegin(GL_QUADS);
		glVertex2f(card_x0, card_bot);
		glVertex2f(card_x1, card_bot);
		glVertex2f(card_x1, card_top);
		glVertex2f(card_x0, card_top);
	glEnd();

	// Footer plate behind the caption, a touch darker than the body so the
	// card reads as "picture above, label below" like a real trading card.
	glColor4f(body_r * 0.78f, body_g * 0.78f, body_b * 0.78f, 1.0f);
	glBegin(GL_QUADS);
		glVertex2f(card_x0, card_bot);
		glVertex2f(card_x1, card_bot);
		glVertex2f(card_x1, image_bot);
		glVertex2f(card_x0, image_bot);
	glEnd();

	// --- sheen: the "printed plastic" gloss of a credit/trading card. Two
	// white gradients (a top-down wash plus a diagonal sweep), both fading
	// to fully transparent, drawn with per-vertex alpha so no texture is
	// needed.
	//
	// ORDER MATTERS: this goes ABOVE the body colour but BELOW the
	// thumbnail. The gloss belongs to the card's own surface - letting it
	// wash over the aircraft makes the photo itself look hazy/greasy
	// instead of making the card look laminated. ---
	state->SetState(0,0,0,0,1,0,0);		// blend on, no texture

	// White has very little headroom over the selected card's light green
	// (0x639875), so the same alpha that looks right on the dark grey body
	// is invisible there - the sheen is deliberately stronger when
	// selected so it stays legible on both.
	float sheen_top_a = is_selected ? 0.10f : 0.040f;
	float sheen_bot = card_bot + (card_top - card_bot) * 0.45f;
	glBegin(GL_QUADS);
		glColor4f(1,1,1,0.0f);        glVertex2f(card_x0, sheen_bot);
		glColor4f(1,1,1,0.0f);        glVertex2f(card_x1, sheen_bot);
		glColor4f(1,1,1,sheen_top_a); glVertex2f(card_x1, card_top);
		glColor4f(1,1,1,sheen_top_a); glVertex2f(card_x0, card_top);
	glEnd();

	// The diagonal sweep: a band that peaks along its centre line and fades
	// out on BOTH sides (two gradient quads meeting at that line), leaning
	// across the full card. A single-sided fade is invisible against the
	// body colour; a soft core with wide falloff is what reads as light on
	// a laminated surface. Wide and weak on purpose - a narrow, bright
	// version of exactly this looks greasy rather than glossy.
	float kStreak = is_selected ? 0.20f : 0.065f;	// see the sheen note above re: green
	float band_w  = (card_x1 - card_x0) * 0.30f;
	float band_cx = card_x0 + (card_x1 - card_x0) * 0.42f;
	float skew    = (card_top - card_bot) * 0.75f;
	glBegin(GL_QUADS);
		glColor4f(1,1,1,0.0f);     glVertex2f(band_cx - band_w,        card_bot);
		glColor4f(1,1,1,kStreak);  glVertex2f(band_cx,                 card_bot);
		glColor4f(1,1,1,kStreak);  glVertex2f(band_cx + skew,          card_top);
		glColor4f(1,1,1,0.0f);     glVertex2f(band_cx - band_w + skew, card_top);
	glEnd();
	glBegin(GL_QUADS);
		glColor4f(1,1,1,kStreak);  glVertex2f(band_cx,                 card_bot);
		glColor4f(1,1,1,0.0f);     glVertex2f(band_cx + band_w,        card_bot);
		glColor4f(1,1,1,0.0f);     glVertex2f(band_cx + band_w + skew, card_top);
		glColor4f(1,1,1,kStreak);  glVertex2f(band_cx + skew,          card_top);
	glEnd();
	glColor4f(1,1,1,1);
	state->SetState(0,0,0,0,0,0,0);

	// 3D snapshot - rendered/cached by mThumbCache, off-screen (see that
	// class - never a live 3D view). Texcoords use the plain GL
	// render-to-texture convention (t=0 at the bottom) since this texture
	// came from our own FBO render, not a loaded image file.
	// Only an upload-and-render costs the frame's budget. A path still being
	// read, or one that failed, is a cheap call - charging it made one missing
	// livery take the only slot every frame and starve every card below it.
	bool already_cached = mThumbCache.IsCached(abs_path);
	bool will_render    = !already_cached && mThumbCache.IsReady(abs_path);
	const WED_LiveryThumbnail * thumb = nullptr;
	if (!will_render || renders_this_frame < kMaxRendersPerFrame)
	{
		thumb = mThumbCache.GetThumbnail(res_mgr, tex_mgr, state, abs_path);
		if (will_render) ++renders_this_frame;
	}
	// Blend ON so the thumbnail's transparent background (the cache clears its
	// FBO to alpha 0 and only the model itself writes opaque pixels) lets the
	// card body above show through, rather than stamping a black rectangle.
	auto draw_thumb = [&](const WED_LiveryThumbnail * t, float alpha) {
		if (!t || t->tex == 0 || alpha <= 0.0f) return;
		state->SetState(0,1,0,0,1,0,0);
		glColor4f(1,1,1,alpha);
		state->BindTex((int) t->tex, 0);
		glBegin(GL_QUADS);
			glTexCoord2f(0,0); glVertex2f(card_x0, image_bot);
			glTexCoord2f(1,0); glVertex2f(card_x1, image_bot);
			glTexCoord2f(1,1); glVertex2f(card_x1, image_top);
			glTexCoord2f(0,1); glVertex2f(card_x0, image_top);
		glEnd();
		glColor4f(1,1,1,1);
		state->SetState(0,0,0,0,0,0,0);
	};

	// Crossfade: the outgoing face under the incoming one. Only an image that is
	// already cached is used for it - a fade must never start a render - and if
	// the incoming one is still loading, the outgoing one simply stays.
	const WED_LiveryThumbnail * from = nullptr;
	if (fade_from >= 0 && fade_from < (int) card.abs_paths.size() && fade_from != show &&
		fade < 1.0f && mThumbCache.IsCached(card.abs_paths[fade_from]))
		from = mThumbCache.GetThumbnail(res_mgr, tex_mgr, state, card.abs_paths[fade_from]);
	bool have_new = thumb && thumb->tex != 0;
	if (from)	draw_thumb(from, have_new ? 1.0f - fade : 1.0f);
	if (have_new)	draw_thumb(thumb, from ? fade : 1.0f);

	// Nothing to show yet: say so, centred where the aircraft will be, so a card
	// that is loading is not mistaken for one that is broken - or for a hang.
	if (!from && !have_new)
	{
		const char * t = (mThumbCache.IsFailed(abs_path) || !WED_LiveryThumbnailCache::RenderingAvailable())
							? "No preview" : "Loading...";
		float tw = GUI_MeasureRange(font_UI_Basic, t, t + strlen(t));
		float muted[4] = { 0.62f, 0.62f, 0.64f, 1.0f };
		GUI_FontDraw(state, font_UI_Basic, muted,
					 (card_x0 + card_x1) * 0.5f - tw * 0.5f,
					 (image_bot + image_top) * 0.5f - line_h * 0.35f, t);
	}

	// Flag icon first - the caption is truncated to whatever room is left
	// beside it, so a narrow pane can never overlap the two.
	float text_room_x1 = card_x1 - 4;
	// The registration country of THIS aircraft, not the airport's -
	// an operator's fleet can be registered anywhere, and the index
	// carries the IOC code per livery for exactly this.
	const WED_LiveryThumbnail * flag = card.ioc_country.empty()
										? NULL
										: EnsureRawFlagTexture(card.ioc_country);
	if (flag && flag->tex != 0 && flag->w > 0 && flag->h > 0)
	{
		float icon_h = (image_bot - text_bot) - 6;
		float icon_w = icon_h * ((float) flag->w / (float) flag->h);
		float fx0 = card_x1 - 4 - icon_w;
		float fy_bot = text_bot + 3;
		float fy_top = fy_bot + icon_h;
		text_room_x1 = fx0 - 4;

		// Loaded via WED_LoadPngTopDownARGB (same as the country banner
		// above) - t=0 belongs at the screen-top vertex, same reasoning
		// as that banner's own draw call.
		state->SetState(0,1,0,0,1,0,0);
		glColor4f(1,1,1,1);
		state->BindTex((int) flag->tex, 0);
		glBegin(GL_QUADS);
			glTexCoord2f(0,0); glVertex2f(fx0,          fy_top);
			glTexCoord2f(1,0); glVertex2f(fx0 + icon_w, fy_top);
			glTexCoord2f(1,1); glVertex2f(fx0 + icon_w, fy_bot);
			glTexCoord2f(0,1); glVertex2f(fx0,          fy_bot);
		glEnd();
		state->SetState(0,0,0,0,0,0,0);
	}

	// Caption line: "AAL - B772 (and 3 more)" on the left, registration country
	// right-aligned just inside the flag.
	//
	// OPERATOR FIRST, THEN THE AIRCRAFT. Both are needed - a card is one operator
	// but the picture is one aircraft, and neither alone explains what is on
	// screen. Leading with the operator is what makes the tail safe to elide: the
	// code is fixed-width, so "(and N more)" always lands in the same place
	// regardless of how long the operator's name would have been. Leading with the
	// full name instead put the interesting part behind an unpredictable amount of
	// text, which is how "B772 - American ... " ate its own suffix.
	const char * card_country = card.ioc_country.empty() ? "" : card.ioc_country.c_str();

	float text_col[4] = { 0.88f, 0.88f, 0.90f, 1.0f };
	float text_y = text_bot + ((image_bot - text_bot) - line_h) * 0.5f + 2;

	// Country code sits immediately left of the flag; the left-hand phrase
	// gets whatever is left over.
	float cc_w = GUI_MeasureRange(font_UI_Basic, card_country, card_country + strlen(card_country));
	float cc_x = text_room_x1 - cc_w;
	GUI_FontDraw(state, font_UI_Basic, text_col, cc_x, text_y, card_country);

	// Ellipsis truncation: trim characters off the END until the string
	// PLUS the "..." fits, so the dots are always the last three glyphs
	// and always land inside the bound (the country code's left edge) -
	// never hanging over it or getting clipped themselves.
	// The text starts after the arrow's gutter when there is an arrow, and at the
	// card's edge when there is not, so a single-aircraft card does not carry an
	// indent for a control it does not have.
	float text_x0    = card_x0 + (card.abs_paths.size() > 1 ? kTrayGutterW : 5.0f);
	float left_avail = (cc_x - 6) - text_x0;

	// A per-type card (GA, military) is one type by construction, so its face is
	// the type and the count - "PC12 (+11)". What differs between its entries is
	// the paint, and that is what the tray is for.
	string head = mCardsByType ? card.icao : card.icao + " - " + type_str;
	string tail, tail_short;
	if (card.abs_paths.size() > 1)
	{
		char m[40];
		snprintf(m, sizeof(m), "  (and %d more)", (int) card.abs_paths.size() - 1);
		tail = m;
		snprintf(m, sizeof(m), "  (+%d)", (int) card.abs_paths.size() - 1);
		tail_short = m;
	}

	// THE SUFFIX ALWAYS SURVIVES. It is the only thing on the face that says the
	// card opens; a user who cannot see "(+8)" has no way to know eight more
	// aircraft are behind it. So it is reserved FIRST - long form if it fits with
	// the whole head, compact "(+N)" otherwise - and the head is cut to whatever
	// is left, with an ellipsis. The earlier fitting order (head first, suffix if
	// room) produced exactly the card this comment is written against: a full
	// operator name and no hint at all that it was a stack.
	float head_w = GUI_MeasureRange(font_UI_Basic, head.c_str(), head.c_str() + head.size());
	float tail_w = tail.empty() ? 0.0f
				 : GUI_MeasureRange(font_UI_Basic, tail.c_str(), tail.c_str() + tail.size());
	if (!tail.empty() && head_w + tail_w > left_avail)
	{
		tail   = tail_short;
		tail_w = GUI_MeasureRange(font_UI_Basic, tail.c_str(), tail.c_str() + tail.size());
	}

	float head_avail = left_avail - tail_w;
	if (head_w > head_avail)
	{
		const string ell = "...";
		while (!head.empty())
		{
			head.pop_back();
			while (!head.empty() && head[head.size() - 1] == ' ')
				head.pop_back();			// no "Air ..." - tuck the dots up against the text
			string probe = head + ell;
			if (GUI_MeasureRange(font_UI_Basic, probe.c_str(), probe.c_str() + probe.size()) <= head_avail)
				break;
		}
		head = head.empty() ? string() : head + ell;
	}
	string caption = head + tail;
	if (!caption.empty())
		GUI_FontDraw(state, font_UI_Basic, text_col, text_x0, text_y, caption.c_str());

	// --- disclosure triangle, left end of the bottom bar. ONLY on cards that have
	// more than one livery: on a single-aircraft card there is nothing to page
	// through and nothing for a tray to list, so an arrow there would promise a
	// slideshow that never comes. Its presence is therefore the answer to "is this
	// card supposed to be cycling" - which is unanswerable without it, since a
	// still card and a card with one aircraft look identical.
	//
	// Points right when shut and rotates to point down as the tray extends, driven
	// by the same 0..1 the tray height uses, so the arrow and the drawer are never
	// out of step. Long and narrow rather than equilateral - a stubby triangle at
	// this size reads as a blob.
	if (card.abs_paths.size() > 1)
	{
		const float cx  = card_x0 + kTrayGutterW * 0.5f;
		const float cy  = card_bot + (image_bot - card_bot) * 0.5f;
		const float lon = 5.5f, lat = 3.2f;		// along the pointing axis, and across it

		// Defined pointing RIGHT, then rotated to wherever the tray has got to: a
		// real rotation rather than a lerp between two shapes, so the triangle keeps
		// its proportions all the way round instead of flattening in the middle.
		float t = (tray_open < 0.0f) ? 0.0f : (tray_open > 1.0f ? 1.0f : tray_open);
		float a = -1.57079633f * t;				// 0 = right, -90 deg = down
		float ca = cosf(a), sa = sinf(a);

		const float px[3] = {  lon, -lon * 0.55f, -lon * 0.55f };
		const float py[3] = { 0.0f, -lat,          lat         };

		state->SetState(0,0,0,0,1,0,0);
		glColor4f(0.82f, 0.82f, 0.86f, is_dimmed ? 0.35f : 0.90f);
		glBegin(GL_TRIANGLES);
			for (int i = 0; i < 3; ++i)
				glVertex2f(cx + px[i] * ca - py[i] * sa,
						   cy + px[i] * sa + py[i] * ca);
		glEnd();
	}

	// --- another card holds the lock, so this one is out of the running. A flat
	// wash over the finished card rather than a different set of colours for every
	// element: it reads as "disabled" without needing a second palette, and it
	// cannot get out of step with the card art underneath. ---
	if (is_dimmed)
	{
		state->SetState(0,0,0,0,1,0,0);
		glColor4f(0.10f, 0.10f, 0.12f, 0.66f);
		glBegin(GL_QUADS);
			glVertex2f(card_x0, card_bot);  glVertex2f(card_x1, card_bot);
			glVertex2f(card_x1, card_top);  glVertex2f(card_x0, card_top);
		glEnd();
	}

	// --- lock badge, top right. PLACEHOLDER ART: a plain square with the same
	// offset-slab shadow the card itself uses, standing in until there is an icon.
	// Deliberately drawn even when unlocked, at low contrast, because a control
	// that only appears once you have used it cannot be discovered. ---
	{
		float lr[4];
		lr[2] = card_x1 - 5.0f;  lr[0] = lr[2] - kLockSize;
		lr[3] = card_top - 5.0f; lr[1] = lr[3] - kLockSize;

		state->SetState(0,0,0,0,1,0,0);
		for (int sh = 2; sh >= 1; --sh)
		{
			glColor4f(0, 0, 0, 0.22f);
			glBegin(GL_QUADS);
				glVertex2f(lr[0]+sh, lr[1]-sh);  glVertex2f(lr[2]+sh, lr[1]-sh);
				glVertex2f(lr[2]+sh, lr[3]-sh);  glVertex2f(lr[0]+sh, lr[3]-sh);
			glEnd();
		}
		if (is_locked) glColor4f(0.98f, 0.80f, 0.25f, 1.00f);	// held - amber, unmistakable
		else           glColor4f(0.72f, 0.72f, 0.76f, 0.55f);	// available
		glBegin(GL_QUADS);
			glVertex2f(lr[0], lr[1]);  glVertex2f(lr[2], lr[1]);
			glVertex2f(lr[2], lr[3]);  glVertex2f(lr[0], lr[3]);
		glEnd();
		glColor4f(0.08f, 0.08f, 0.10f, 0.85f);
		glBegin(GL_LINE_LOOP);
			glVertex2f(lr[0]+0.5f, lr[1]+0.5f);  glVertex2f(lr[2]-0.5f, lr[1]+0.5f);
			glVertex2f(lr[2]-0.5f, lr[3]-0.5f);  glVertex2f(lr[0]+0.5f, lr[3]-0.5f);
		glEnd();
	}

	// --- selected tick, dead centre, only once the toggle is actually on
	// (i.e. after a completed press-and-release) ---

	// --- frame last, so it sits over the picture, footer and sheen ---
	state->SetState(0,0,0,0,0,0,0);
	if (is_selected)		glColor4f(0.60f, 0.82f, 0.69f, 1.0f);	// a lighter tint of the body green, so the edge still reads as an edge
	else if (is_hovered)	glColor4f(0.52f, 0.52f, 0.56f, 1.0f);
	else					glColor4f(0.34f, 0.34f, 0.38f, 1.0f);
	glBegin(GL_LINE_LOOP);
		glVertex2f(card_x0 + 0.5f, card_bot + 0.5f);
		glVertex2f(card_x1 - 0.5f, card_bot + 0.5f);
		glVertex2f(card_x1 - 0.5f, card_top - 0.5f);
		glVertex2f(card_x0 + 0.5f, card_top - 0.5f);
	glEnd();
	glColor4f(0.26f, 0.26f, 0.29f, 1.0f);		// hairline between picture and caption
	glBegin(GL_LINES);
		glVertex2f(card_x0 + 1, image_bot);
		glVertex2f(card_x1 - 1, image_bot);
	glEnd();
}

// ---------------------------------------------------------------------------------------------
// draw
// ---------------------------------------------------------------------------------------------

void	WED_LiveryPane::Draw(GUI_GraphState * state)
{
	int b[4];
	GetBounds(b);

	// ============================================================================================
	// IF YOU ARE HERE BECAUSE SOMETHING ISN'T DRAWING, read this before spending a debugging
	// session rediscovering what already cost one. In rough order of "how likely to bite you":
	//
	// 1) BACK-FACE CULLING SILENTLY EATS FILLED SHAPES, NOT JUST THE FLAG BANNER.
	//    GUI_GraphState::Init() sets glFrontFace(GL_CW) + GL_CULL_FACE/GL_BACK globally for all of
	//    WED. Every filled shape in THIS pane (quads, the slider ball triangle fans) is wound the
	//    "natural" bottom-left-first way, which is counter-clockwise in this pane's Y-up coordinate
	//    space - i.e. back-facing under WED's convention, so every one of them was being silently
	//    culled. No GL error, nothing in the log - text (GUI_FontDraw) and GL_LINE_LOOP/GL_LINES
	//    outlines still rendered fine, which is what made this so confusing: the filter chips'
	//    colored backgrounds and the slider's fill bar/ball fills were ALSO invisible this whole
	//    time, before this feature ever touched the file - it just took drawing something new and
	//    obvious (a whole flag) to notice. Rather than re-winding every quad/fan by hand, culling is
	//    just turned off for this pane's own drawing (glPushAttrib/glDisable below) and restored at
	//    the very end of Draw() (glPopAttrib) so nothing else in WED is affected. If you add a new
	//    filled shape anywhere in this file and it doesn't show up, this is NOT what's wrong (culling
	//    is already off for the whole function) - but if this glPushAttrib/glDisable pair or the
	//    matching glPopAttrib at the end ever gets deleted/moved by mistake, this is exactly what
	//    will silently break, and it will look identical to a texture/coordinate bug.
	//
	// 2) <windows.h> min/max MACROS BREAK std::min/std::max IN THIS FILE'S FAMILY.
	//    XDefs.h is force-included (/FI) into every WED .cpp and pulls in <windows.h> on IBM/Windows,
	//    whose function-like min/max macros swallow std::min(...)/std::max(...) into a compile error
	//    (C2589/C2059) that has nothing to do with your actual logic. WED_FlagProjector.cpp works
	//    around this by parenthesizing every call as (std::min)(...)/(std::max)(...), which defeats
	//    macro expansion. Do the same in any new code here rather than adding a project-wide
	//    NOMINMAX (out of scope for this feature, and no other file in the codebase uses std::min/max
	//    at all - they avoid it entirely, which is also a valid way to sidestep this).
	//
	// 3) GL_BGRA NEEDS "glew.h", NOT PLAIN <GL/gl.h>, ON WINDOWS.
	//    Uploading the flag texture via GL_BGRA/GL_UNSIGNED_BYTE (so the composited buffer's byte
	//    order can be handed to glTexImage2D with no manual channel swap) needs the GL_BGRA token,
	//    which plain <GL/gl.h> doesn't define on Windows (it's a GL 1.2+ token). This file includes
	//    "glew.h" instead on non-Mac platforms for exactly that reason - see the #include block
	//    right below GetBounds(b). Reverting to <GL/gl.h> "to simplify" will silently break the
	//    Windows build's texture upload.
	//
	// 4) THE FLAG'S TRAY SIZE DEPENDS ON THE TEXTURE, SO ORDER OF OPERATIONS MATTERS.
	//    AirportInfoHeight()/FlagBannerRect() size the dark tray and the banner off mFlagTexW/H,
	//    which EnsureFlagTexture() sets. EnsureFlagTexture() MUST run before AirportInfoYRange() is
	//    first called each Draw() (see the airport/country info strip block below) - call it after
	//    and the tray is sized from last frame's texture, then "pops" into the right size on the
	//    next redraw. This bit us once already; the current ordering is deliberate, not incidental.
	//
	// 5) THIS PORT DELIBERATELY DEVIATES FROM THE CODEX HANDOFF SPEC IN A FEW PLACES - see
	//    WED_FlagAssets.h and WED_FlagProjector.h's own header comments for the full list (no SVG
	//    mask - WED has no SVG rasterizer, so the PNG mask alone is the effective cloth mask;
	//    transparent background instead of flattened-onto-white - WED has no export path to keep
	//    white for, and the handoff's own contract explicitly allows this; synchronous, main-thread
	//    projection - no background job/debounce infrastructure was added; fixed approved defaults
	//    only, no debug UI for quality/curves/opacity/threshold/UV grid). None of these were
	//    oversights - re-litigating them from scratch will just rediscover the same constraints.
	//
	// 6) ASSET PATHS ARE STILL A TEMPORARY HARDCODED DEV-MACHINE CONSTANT (see the top of
	//    WED_FlagAssets.cpp and WED_FlagIndex.cpp). Wiring the ~210 flag/pole/mask/ink/CSV files into
	//    WED's real resource pipeline (WED.rc's GUI_RES entries + cmake's WED_RESOURCE_FILES list,
	//    see src/GUI/GUI_Resources.cpp) is real, necessary follow-up work before this ships to
	//    anyone but this dev machine - it was deliberately deferred so this feature was buildable
	//    and testable without blocking on it. Do not mistake the hardcoded path for a design choice.
	//
	// 7) COUNTRY CODE CHOICES ARE POLICY, NOT BUGS WAITING TO BE "FIXED" - see the header comment in
	//    WED_IocCountryCodes.h before changing ANY mapping in that file. The short version: every
	//    code/flag choice here is deliberately IOC's own current, real classification, with zero
	//    exceptions carved out by this project - not a political stance WED is taking on its own.
	// ============================================================================================
	glPushAttrib(GL_ENABLE_BIT);
	glDisable(GL_CULL_FACE);

	float line_h    = GUI_GetLineHeight(font_UI_Basic);
	float pad       = 4;
	float header_h  = HeaderHeight();
	float row_h     = line_h + pad;

	// --- airport / country info strip ---
	{
		// Figured out (and EnsureFlagTexture() called) BEFORE AirportInfoYRange()
		// below - the tray's own height derives from the flag's now-current
		// mFlagTexW/H (see AirportInfoHeight()), so this must run first or the
		// tray is sized off last frame's texture and only self-corrects on the
		// next redraw (the "pops back into place a beat late" bug).
		string info_text;
		string status_text;		// "" = no commercial-status line to show (only set alongside a successful lookup)
		bool status_warn = false;	// true draws status_text in the same yellow as a warning info_text
		bool warn = false;
		string flag_country;		// "" = no flag banner to show for this state
		mCurrentAirportIcao.clear();	// only set below on a successful lookup - see the "Show Recommendation" button
		mAirportCountry.clear();

		if (!mSelectedRamps.empty())
		{
			WED_Airport * apt = WED_GetParentAirport(mSelectedRamps[0]);
			bool same_airport = true;
			for (size_t i = 1; i < mSelectedRamps.size(); ++i)
				if (WED_GetParentAirport(mSelectedRamps[i]) != apt) { same_airport = false; break; }

			if (!same_airport)
			{
				info_text = "Selected ramp starts belong to different airports.";
				warn = true;
			}
			else if (!apt)
			{
				info_text = "No parent airport found.";
				warn = true;
			}
			else
			{
				string icao_primary, apt_name, country;
				apt->GetICAO(icao_primary);	// primary apt.dat identifier ("Airport ID") - X-Plane's
												// own, always-present code; NOT the same thing as the
												// optional "ICAO Code" metadata key below
				apt->GetName(apt_name);

				// Prefer the airport's advertised ICAO Code metadata key when
				// it's actually set: some airports (mostly custom/third-party
				// scenery) carry a synthetic X-Plane identifier as their
				// primary ID (e.g. "XUK001K") while separately recording
				// their real ICAO in metadata (e.g. "UPKS") - that metadata
				// value is the one worth looking up first. The overwhelming
				// majority of real small airports have no ICAO metadata at
				// all - for those, the primary ID itself IS the only code
				// that exists, and is what we fall back to below.
				string icao_meta;
				if (apt->ContainsMetaDataKey("icao_code"))
					icao_meta = apt->GetMetaDataValue("icao_code");

				string icao = !icao_meta.empty() ? icao_meta : icao_primary;
				WED_IcaoLookupResult r = LookupIcaoCountry(mAirportDb, icao, country);

				// A metadata ICAO can be real (recognized by ICAO/IATA) without
				// ever appearing as a primary identifier in X-Plane's own
				// Global Airports database, so a miss there doesn't mean the
				// airport has no usable country info - the primary ID's own
				// apt.dat entry may still carry a perfectly good country line
				// that a metadata-only lookup would otherwise shadow. Give
				// that a second try before giving up.
				//
				// A PLACEHOLDER COUNTS AS A MISS. It used to retry only on
				// NotFound, so an airport whose metadata still held a reserved
				// code - "ZZLI" - kept reporting "placeholder, country unknown"
				// after its Airport ID had been corrected to a real one, and the
				// pane looked like it was failing to refresh when it was in fact
				// faithfully reporting stale metadata. A reserved code carries no
				// information by definition, so anything real must outrank it.
				if ((r == wed_Icao_NotFound || r == wed_Icao_Placeholder) &&
					!icao_meta.empty() && icao_meta != icao_primary)
				{
					string country2;
					WED_IcaoLookupResult r2 = LookupIcaoCountry(mAirportDb, icao_primary, country2);
					if (r2 == wed_Icao_Ok)
					{
						icao = icao_primary;
						country = country2;
						r = r2;
					}
				}

				if (r == wed_Icao_Ok)
				{
					info_text = apt_name + " (" + icao + ") - " + country;
					flag_country = country;		// already an IOC-normalized code - see WED_AirportDatabase.cpp
					mCurrentAirportIcao = icao;
					mAirportCountry     = country;	// for the military rule - see LiveryAllowedHere()

					// Specifically whether THIS airport has its own hand-researched Direct
					// Hit entry in WED_AirportDatabase.txt - not the broader "commercially
					// served at all" question AirportIsCommercial() answers (that one falls
					// back to the bulk OurAirports flag, which can say "yes" for an airport
					// this project just hasn't researched yet - a misleading "available"
					// promise when the checklist won't actually show anything for it).
					if (!mAirportDb.IsLoaded() && !mAirportDb.LoadFailed())
						mAirportDb.EnsureLoaded(WedDataFileDir() + "WED_AirportDatabase.txt");
					vector<string> direct_hit = GetRecommendedAirlineCodes(mAirportDb, icao);

					if (!direct_hit.empty())
					{
						status_text = "Livery recommendation is available at this airport";
					}
					else
					{
						status_text = "Recommendation might not be available at this airport";
						status_warn = true;
					}
				}
				else if (r == wed_Icao_Placeholder)
				{
					// Two different situations reach wed_Icao_Placeholder and they
					// need different sentences. Saying "not set" about a code the
					// user can see filled in reads as a bug in WED rather than as
					// a fact about the code - which is how it read for ZZLI, a
					// deliberate choice from ICAO's reserved range.
					if (icao.empty())
						info_text = "Airport ICAO not set - country unknown, can't weight liveries by region.";
					else
						info_text = "\"" + icao + "\" is a placeholder or reserved ICAO code - country unknown, can't weight liveries by region.";
					warn = true;
				}
				else if (r == wed_Icao_IndexUnavailable)
				{
					// Say WHICH way it failed. A file that is present but whose
					// header was edited used to report as "not found", sending
					// people to look for a file sitting in front of them.
					info_text = string("WED_AirportDatabase.txt: ") +
								WedDataFileErrorText(mAirportDb.LoadError()) +
								" - can't look up country.";
					warn = true;
				}
				else
				{
					info_text = "ICAO \"" + icao + "\" not found in the local airport database - country unknown.";
					warn = true;
				}
			}
		}

		EnsureFlagTexture(flag_country);

		// Word-wrap BEFORE AirportInfoYRange() below - the tray's height
		// (AirportInfoHeight()) has to account for however many lines this
		// turns into on the SAME frame the text changes, same ordering
		// requirement as the flag texture (see the top of Draw()). text_x's
		// own final value is recomputed after top/bot are known (just below)
		// since FlagBannerRect() needs them for the actual draw position,
		// but its WIDTH component alone (needed here) never depends on them.
		{
			float banner_w_only = (mFlagTexId != 0) ? (b[2] - b[0]) * 0.5f : 0.0f;
			float avail_text_w = (b[2] - b[0]) - banner_w_only - pad * (mFlagTexId != 0 ? 3 : 2);
			mCachedInfoLines   = WrapText(font_UI_Basic, info_text,   avail_text_w);
			mCachedStatusLines = WrapText(font_UI_Basic, status_text, avail_text_w);
		}

		float top, bot;
		AirportInfoYRange(b, top, bot);

		state->SetState(0,0,0,0,0,0,0);
		glColor4f(0.16f, 0.16f, 0.16f, 1.0f);
		glBegin(GL_QUADS);
			glVertex2f((float) b[0], bot);
			glVertex2f((float) b[2], bot);
			glVertex2f((float) b[2], top);
			glVertex2f((float) b[0], top);
		glEnd();

		float text_x = b[0] + pad;

		if (mFlagTexId != 0)
		{
			// Actual drawing happens at the very end of Draw() (see the
			// bottom of this function) - only the geometry is needed here,
			// to push the info text clear of the banner.
			float bx0, by0, banner_w, banner_h;
			FlagBannerRect(b, top, bot, bx0, by0, banner_w, banner_h);
			text_x = bx0 + banner_w + pad * 2;
		}

		if (!mCachedInfoLines.empty() || !mCachedStatusLines.empty())
		{
			float warn_col[4] = { 1.0f, 0.75f, 0.25f, 1.0f };
			float * text_col = warn ? warn_col : WED_Color_RGBA(wed_Header_Text);
			float status_muted_col[4] = { 0.68f, 0.68f, 0.68f, 1.0f };		// muted - an FYI, not a warning
			float * status_col = status_warn ? warn_col : status_muted_col;
			float mid = bot + (top - bot) * 0.5f;

			int total_lines = (int) mCachedInfoLines.size() + (int) mCachedStatusLines.size();
			float gap = (!mCachedInfoLines.empty() && !mCachedStatusLines.empty()) ? 2.0f : 0.0f;
			float block_h = total_lines * line_h + gap;

			float cursor_y = mid + block_h * 0.5f - line_h * 0.9f;
			for (size_t i = 0; i < mCachedInfoLines.size(); ++i)
			{
				// GUI_Fonts has no bold weight - approximate it with a 1px double-draw
				// (same trick already used below for the "N Ramp Starts Selected" count).
				GUI_FontDraw(state, font_UI_Basic, text_col, text_x,     cursor_y, mCachedInfoLines[i].c_str());
				GUI_FontDraw(state, font_UI_Basic, text_col, text_x + 1, cursor_y, mCachedInfoLines[i].c_str());
				cursor_y -= line_h;
			}
			cursor_y -= gap;
			for (size_t i = 0; i < mCachedStatusLines.size(); ++i)
			{
				GUI_FontDraw(state, font_UI_Basic, status_col, text_x, cursor_y, mCachedStatusLines[i].c_str());
				cursor_y -= line_h;
			}
		}
	}

	// --- header strip ---
	{
		float top, bot;
		HeaderYRange(b, top, bot);
		state->SetState(0,0,0,0,0,0,0);
		glColor4f(0.20f, 0.20f, 0.20f, 1.0f);
		glBegin(GL_QUADS);
			glVertex2f((float) b[0], bot);
			glVertex2f((float) b[2], bot);
			glVertex2f((float) b[2], top);
			glVertex2f((float) b[0], top);
		glEnd();
	}

	float htop, hbot;
	HeaderYRange(b, htop, hbot);
	float * header_col = WED_Color_RGBA(wed_Header_Text);
	float tx = b[0] + pad;
	float ty = htop - header_h * 0.5f - line_h * 0.4f;

	if (mSelectedRamps.empty())
	{
		GUI_FontDraw(state, font_UI_Basic, header_col, tx, ty, "Select a ramp start to edit its liveries.");
		if (mSearchField->IsFocused()) mSearchField->LoseFocus(1);
		mSearchField->Hide();		// no toolbar row to live in - see the search-box block further down
	}
	else if (mSelectedRamps.size() == 1)
	{
		string name;
		mSelectedRamps[0]->GetName(name);
		float pb[4];
		PopulateButtonRect(b, pb);			// drawn below, for any selection
		string header = ElideToWidth(font_UI_Basic, string("Ramp Start: ") + name, pb[0] - 8 - tx);
		GUI_FontDraw(state, font_UI_Basic, header_col, tx, ty, header.c_str());

	}
	else
	{
		char buf[16];
		snprintf(buf, sizeof(buf), "%d", (int) mSelectedRamps.size());
		string count_str(buf);

		// GUI_Fonts has no bold weight - approximate it with a 1px double-draw.
		GUI_FontDraw(state, font_UI_Basic, header_col, tx,     ty, count_str.c_str());
		GUI_FontDraw(state, font_UI_Basic, header_col, tx + 1, ty, count_str.c_str());

		float count_w = GUI_MeasureRange(font_UI_Basic, count_str.c_str(), count_str.c_str() + count_str.size());
		GUI_FontDraw(state, font_UI_Basic, header_col, tx + count_w + 4, ty, " Ramp Starts Selected");
	}

	if (!mSelectedRamps.empty())
	{
		float pb[4];
		PopulateButtonRect(b, pb);
		const bool flashing = PaneClockNow() < mPopulateFlashUntil;
		if (flashing) Refresh();					// until the caption goes back
		state->SetState(0,0,0,0,0,0,0);
		float k = mTrackPopulate ? 0.82f : (mHoverPopulate ? 1.15f : 1.0f);
		glColor4f(0.26f * k, 0.30f * k, 0.36f * k, 1.0f);
		glBegin(GL_QUADS);
			glVertex2f(pb[0], pb[1]); glVertex2f(pb[0], pb[3]);
			glVertex2f(pb[2], pb[3]); glVertex2f(pb[2], pb[1]);
		glEnd();
		glColor4f(0.55f, 0.55f, 0.58f, 1.0f);
		glBegin(GL_LINE_LOOP);
			glVertex2f(pb[0], pb[1]); glVertex2f(pb[0], pb[3]);
			glVertex2f(pb[2], pb[3]); glVertex2f(pb[2], pb[1]);
		glEnd();
		const string cap = flashing ? mPopulateFlash : PopulateCaption(mSelectedRamps.size());
		float cw = GUI_MeasureRange(font_UI_Basic, cap.c_str(), cap.c_str() + cap.size());
		GUI_FontDraw(state, font_UI_Basic, WED_Color_RGBA(wed_Table_Text),
					 (pb[0] + pb[2]) * 0.5f - cw * 0.5f, pb[1] + 4, cap.c_str());
	}

	int cur_op_enum = mSelectedRamps.empty() ? ramp_operation_None : mSelectedRamps[0]->GetRampOperationType();

	// --- ramp operation filter chips ---
	{
		float chip_top, chip_bot;
		FilterYRange(b, chip_top, chip_bot);
		float chip_w = (b[2] - b[0]) / 5.0f;

		for (int i = 0; i < 5; ++i)
		{
			float cx0 = b[0] + i * chip_w;
			float cx1 = cx0 + chip_w;
			bool active  = (kFilterEnumTable[i] == cur_op_enum);
			bool hovered = (mHoverFilterChip == i);

			state->SetState(0,0,0,0,0,0,0);
			if (active)			glColor4f(0.22f, 0.50f, 0.85f, 1.0f);	// selected: vivid blue
			else if (hovered)	glColor4f(0.44f, 0.44f, 0.48f, 1.0f);	// hover: clearly lighter grey
			else				glColor4f(0.18f, 0.18f, 0.18f, 1.0f);	// idle: plain dark grey
			glBegin(GL_QUADS);
				glVertex2f(cx0 + 1, chip_bot + 1);
				glVertex2f(cx1 - 1, chip_bot + 1);
				glVertex2f(cx1 - 1, chip_top - 1);
				glVertex2f(cx0 + 1, chip_top - 1);
			glEnd();

			// 2px inner shade (top+left dark, bottom+right dark too but slightly
			// lighter) so the chip reads as a recessed 3D button rather than a
			// flat color swatch. Capped at 50% black per spec.
			state->SetState(0,0,0,0,1,0,0);
			glColor4f(0.0f, 0.0f, 0.0f, active ? 0.5f : 0.35f);
			glBegin(GL_QUADS);	// top shade
				glVertex2f(cx0 + 1, chip_top - 3);
				glVertex2f(cx1 - 1, chip_top - 3);
				glVertex2f(cx1 - 1, chip_top - 1);
				glVertex2f(cx0 + 1, chip_top - 1);
			glEnd();
			glBegin(GL_QUADS);	// left shade
				glVertex2f(cx0 + 1, chip_bot + 1);
				glVertex2f(cx0 + 3, chip_bot + 1);
				glVertex2f(cx0 + 3, chip_top - 1);
				glVertex2f(cx0 + 1, chip_top - 1);
			glEnd();
			glColor4f(0.0f, 0.0f, 0.0f, active ? 0.25f : 0.18f);
			glBegin(GL_QUADS);	// bottom shade (lighter, so top/left reads as the "light source" side)
				glVertex2f(cx0 + 1, chip_bot + 1);
				glVertex2f(cx1 - 1, chip_bot + 1);
				glVertex2f(cx1 - 1, chip_bot + 3);
				glVertex2f(cx0 + 1, chip_bot + 3);
			glEnd();
			glBegin(GL_QUADS);	// right shade
				glVertex2f(cx1 - 3, chip_bot + 1);
				glVertex2f(cx1 - 1, chip_bot + 1);
				glVertex2f(cx1 - 1, chip_top - 1);
				glVertex2f(cx1 - 3, chip_top - 1);
			glEnd();
			state->SetState(0,0,0,0,0,0,0);

			if (hovered && !active)
			{
				// extra affordance beyond the color shift: a bright border
				glColor4f(1.0f, 1.0f, 1.0f, 1.0f);
				glBegin(GL_LINE_LOOP);
					glVertex2f(cx0 + 1, chip_bot + 1);
					glVertex2f(cx1 - 1, chip_bot + 1);
					glVertex2f(cx1 - 1, chip_top - 1);
					glVertex2f(cx0 + 1, chip_top - 1);
				glEnd();
			}

			if (active)
			{
				// bright underline so "selected" reads at a glance, not just a color tint
				glColor4f(1.0f, 0.8f, 0.2f, 1.0f);
				glBegin(GL_QUADS);
					glVertex2f(cx0 + 1, chip_bot + 1);
					glVertex2f(cx1 - 1, chip_bot + 1);
					glVertex2f(cx1 - 1, chip_bot + 4);
					glVertex2f(cx0 + 1, chip_bot + 4);
				glEnd();
			}

			float * txt_col = WED_Color_RGBA(wed_Table_Text);
			const char * lbl = kFilterLabels[i];
			float tw = GUI_MeasureRange(font_UI_Basic, lbl, lbl + strlen(lbl));
			if (tw > chip_w - 6.0f)			// too tight - fall back to the short form
			{
				lbl = kFilterLabelsShort[i];
				tw  = GUI_MeasureRange(font_UI_Basic, lbl, lbl + strlen(lbl));
			}
			GUI_FontDraw(state, font_UI_Basic, txt_col, cx0 + (chip_w - tw) * 0.5f, (chip_top + chip_bot) * 0.5f - line_h * 0.35f, lbl);
		}
	}

	// --- size range slider ---
	if (SliderHeight() > 0)		// collapsed to nothing while the stand has weights
	{
		float handle_r = line_h * 0.5f;
		float slider_top, slider_bot;
		SliderYRange(b, slider_top, slider_bot);
		float track_y  = slider_bot + line_h * 0.5f;
		float track_x0 = b[0] + pad + handle_r;
		float track_x1 = b[2] - pad - handle_r;

		int minIdx = 0, maxIdx = 5;
		if (!mSelectedRamps.empty())
		{
			minIdx = WidthEnumToIndex(mSelectedRamps[0]->GetWidthMin());
			maxIdx = WidthEnumToIndex(mSelectedRamps[0]->GetWidth());
		}

		float * lbl_col    = WED_Color_RGBA(wed_Table_Text);
		float * track_col  = WED_Color_RGBA(wed_Table_Gridlines);
		float * header_col2 = WED_Color_RGBA(wed_Header_Text);

		// zone background + border, so this reads as one distinct control block
		state->SetState(0,0,0,0,0,0,0);
		glColor4f(0.14f, 0.14f, 0.14f, 1.0f);
		glBegin(GL_QUADS);
			glVertex2f((float) b[0] + 1, slider_bot);
			glVertex2f((float) b[2] - 1, slider_bot);
			glVertex2f((float) b[2] - 1, slider_top);
			glVertex2f((float) b[0] + 1, slider_top);
		glEnd();
		glColor4f(0.40f, 0.40f, 0.40f, 1.0f);
		glBegin(GL_LINE_LOOP);
			glVertex2f((float) b[0] + 1, slider_bot);
			glVertex2f((float) b[2] - 1, slider_bot);
			glVertex2f((float) b[2] - 1, slider_top);
			glVertex2f((float) b[0] + 1, slider_top);
		glEnd();

		// title, so it's unmistakable what this control is
		GUI_FontDraw(state, font_UI_Basic, header_col2, b[0] + pad, slider_top - line_h * 0.9f, "Size (ICAO Wingspan Category)");

		for (int i = 0; i < 6; ++i)
		{
			float fx = track_x0 + (track_x1 - track_x0) * i / 5.0f;
			float tw = GUI_MeasureRange(font_UI_Basic, kWidthLabels[i], kWidthLabels[i] + 1);
			// Clamped: the outer two labels are centred on the track's own
			// endpoints, which sit close enough to the pane edge that half a
			// glyph fell outside it.
			float lx = fx - tw * 0.5f;
			if (lx < (float) b[0] + 2)          lx = (float) b[0] + 2;
			if (lx + tw > (float) b[2] - 2)     lx = (float) b[2] - 2 - tw;
			GUI_FontDraw(state, font_UI_Basic, lbl_col, lx, slider_top - line_h * 1.9f, kWidthLabels[i]);
		}

		state->SetState(0,0,0,0,0,0,0);
		glDisable(GL_TEXTURE_2D);	// belt-and-suspenders: make sure nothing textured/blended leaks in here
		glDisable(GL_BLEND);

		// faint tick marks at each of the six grid positions
		glColor4f(track_col[0], track_col[1], track_col[2], 1.0f);
		for (int i = 0; i < 6; ++i)
		{
			float fx = track_x0 + (track_x1 - track_x0) * i / 5.0f;
			glBegin(GL_LINES);
				glVertex2f(fx, track_y - 4);
				glVertex2f(fx, track_y + 4);
			glEnd();
		}

		float min_x = track_x0 + (track_x1 - track_x0) * minIdx / 5.0f;
		float max_x = track_x0 + (track_x1 - track_x0) * maxIdx / 5.0f;

		// bold white line filling the range between the two balls - drawn as a
		// filled quad PLUS an outline border, so it stays visible even if fills
		// alone were ever the problem (lines have proven reliable so far)
		glColor4f(1.0f, 1.0f, 1.0f, 1.0f);
		glBegin(GL_QUADS);
			glVertex2f(min_x, track_y - 5);
			glVertex2f(max_x, track_y - 5);
			glVertex2f(max_x, track_y + 5);
			glVertex2f(min_x, track_y + 5);
		glEnd();
		glColor4f(0.05f, 0.05f, 0.05f, 1.0f);
		glBegin(GL_LINE_LOOP);
			glVertex2f(min_x, track_y - 5);
			glVertex2f(max_x, track_y - 5);
			glVertex2f(max_x, track_y + 5);
			glVertex2f(min_x, track_y + 5);
		glEnd();

		// hover ring: whichever ball is currently under the mouse (or being
		// dragged) gets a bright highlight ring drawn around it. When the two
		// balls overlap, hit-testing reports "2" for either - light up both
		// (they're at the same spot, so this draws once in practice) so an
		// overlapped pair doesn't look unresponsive.
		bool ring_min = (mHoverSliderHandle == 0 || mDragHandle == 0 || mHoverSliderHandle == 2 || mDragHandle == 2);
		bool ring_max = (mHoverSliderHandle == 1 || mDragHandle == 1 || mHoverSliderHandle == 2 || mDragHandle == 2);

		if (ring_min)
		{
			glColor4f(1.0f, 0.85f, 0.3f, 1.0f);
			DrawCircleOutline(min_x, track_y, handle_r + 3);
		}
		if (ring_max)
		{
			glColor4f(1.0f, 0.85f, 0.3f, 1.0f);
			DrawCircleOutline(max_x, track_y, handle_r + 3);
		}

		// min ball (orange), max ball (blue), each with a dark outline for contrast -
		// min drawn first so the max ball wins on top when the two happen to overlap
		glColor4f(0.9f, 0.6f, 0.2f, 1.0f);
		DrawFilledCircle(min_x, track_y, handle_r);
		glColor4f(0.05f, 0.05f, 0.05f, 1.0f);
		DrawCircleOutline(min_x, track_y, handle_r);
		DrawGrabberDashes(min_x, track_y, handle_r);

		glColor4f(0.3f, 0.6f, 0.9f, 1.0f);
		DrawFilledCircle(max_x, track_y, handle_r);
		glColor4f(0.05f, 0.05f, 0.05f, 1.0f);
		DrawCircleOutline(max_x, track_y, handle_r);
		DrawGrabberDashes(max_x, track_y, handle_r);

		// Once weights exist the size letter is DERIVED from them (R23), so
		// this control is a readout. Mask it the way the disabled Recommend
		// button is masked - the hit tests already refuse it, and this is the
		// half that says so.
		if (SelectionHasWeights())
		{
			state->SetState(0,0,0,0,1,0,0);
			glColor4f(0.0f, 0.0f, 0.0f, 0.55f);
			glBegin(GL_QUADS);
				glVertex2f((float) b[0] + 1, slider_bot);
				glVertex2f((float) b[2] - 1, slider_bot);
				glVertex2f((float) b[2] - 1, slider_top);
				glVertex2f((float) b[0] + 1, slider_top);
			glEnd();
			// Centred in the zone rather than jammed against its bottom edge,
			// where it landed on the track and the A-F labels and was
			// unreadable through the mask.
			state->SetState(0,0,0,0,0,0,0);
			const char * drv = "Size is derived from the weights below";
			float dw = GUI_MeasureRange(font_UI_Basic, drv, drv + strlen(drv));
			float dcol[4] = { 0.92f, 0.92f, 0.94f, 1.0f };
			GUI_FontDraw(state, font_UI_Basic, dcol,
						 ((float) b[0] + (float) b[2]) * 0.5f - dw * 0.5f,
						 (slider_bot + slider_top) * 0.5f - line_h * 0.35f, drv);
		}
	}

	// --- spawn weight bars (apt.dat row 1313) ---
	// Only drawn when the selection actually has weights; WeightsHeight()
	// collapses the section to nothing otherwise, and the button below is how
	// an author gets here from there.
	if (!mSelectedRamps.empty() && WeightsHeight() > 0)
	{
		float wtop, wbot;
		WeightsYRange(b, wtop, wbot);

		int w[6];
		const bool uniform = SelectionWeights(w);
		if (!uniform) for (int i = 0; i < 6; ++i) w[i] = 0;
		const int track_max = WeightTrackMax();

		int total = 0;
		for (int i = 0; i < 6; ++i) total += w[i];

		// zone background, matching the slider and the readout above and below
		state->SetState(0,0,0,0,0,0,0);
		glColor4f(0.14f, 0.14f, 0.14f, 1.0f);
		glBegin(GL_QUADS);
			glVertex2f((float) b[0] + 1, wbot);
			glVertex2f((float) b[2] - 1, wbot);
			glVertex2f((float) b[2] - 1, wtop);
			glVertex2f((float) b[0] + 1, wtop);
		glEnd();
		glColor4f(0.40f, 0.40f, 0.40f, 1.0f);
		glBegin(GL_LINE_LOOP);
			glVertex2f((float) b[0] + 1, wbot);
			glVertex2f((float) b[2] - 1, wbot);
			glVertex2f((float) b[2] - 1, wtop);
			glVertex2f((float) b[0] + 1, wtop);
		glEnd();

		float * hdr_col = WED_Color_RGBA(wed_Header_Text);
		float * lbl_col = WED_Color_RGBA(wed_Table_Text);
		float   dim[4]  = { 0.62f, 0.62f, 0.62f, 1.0f };

		GUI_FontDraw(state, font_UI_Basic, hdr_col, b[0] + pad, wtop - line_h * 0.9f,
					 uniform ? "Spawn Distribution (relative)" : "Spawn Distribution - selection differs");

		for (int i = 0; i < 6; ++i)
		{
			float r[4];
			WeightBarRect(b, i, r);
			float full_h = r[3] - r[1];
			float frac   = (track_max > 0) ? (float) w[i] / (float) track_max : 0.0f;
			float fill_t = r[1] + full_h * frac;

			const bool hot = (i == mHoverWeightBar) || (i == mDragWeightBar);

			// the empty column, so a zero bar is still a visible drop target
			state->SetState(0,0,0,0,0,0,0);
			glColor4f(0.20f, 0.20f, 0.20f, 1.0f);
			glBegin(GL_QUADS);
				glVertex2f(r[0], r[1]); glVertex2f(r[0], r[3]);
				glVertex2f(r[2], r[3]); glVertex2f(r[2], r[1]);
			glEnd();

			if (w[i] > 0)
			{
				if (hot) glColor4f(0.42f, 0.72f, 1.00f, 1.0f);
				else     glColor4f(0.30f, 0.60f, 0.90f, 1.0f);
				glBegin(GL_QUADS);
					glVertex2f(r[0], r[1]); glVertex2f(r[0], fill_t);
					glVertex2f(r[2], fill_t); glVertex2f(r[2], r[1]);
				glEnd();
			}

			glColor4f(hot ? 0.85f : 0.45f, hot ? 0.85f : 0.45f, hot ? 0.85f : 0.45f, 1.0f);
			glBegin(GL_LINE_LOOP);
				glVertex2f(r[0], r[1]); glVertex2f(r[0], r[3]);
				glVertex2f(r[2], r[3]); glVertex2f(r[2], r[1]);
			glEnd();

			// class letter, then the share this bar represents - the share is
			// what the author is actually reasoning about, the integer is just
			// how it gets stored.
			float cx = (r[0] + r[2]) * 0.5f;
			float tw = GUI_MeasureRange(font_UI_Basic, kWidthLabels[i], kWidthLabels[i] + 1);
			GUI_FontDraw(state, font_UI_Basic, lbl_col, cx - tw * 0.5f, r[1] - line_h - 2, kWidthLabels[i]);

			char pct[16];
			if (!uniform)        snprintf(pct, sizeof(pct), "--");
			else if (total <= 0) snprintf(pct, sizeof(pct), "0%%");
			else                 snprintf(pct, sizeof(pct), "%d%%", (int) (100.0f * w[i] / total + 0.5f));
			float pw = GUI_MeasureRange(font_UI_Basic, pct, pct + strlen(pct));
			GUI_FontDraw(state, font_UI_Basic, (w[i] > 0) ? lbl_col : dim,
						 cx - pw * 0.5f, r[1] - line_h * 2 - 3, pct);
		}

		if (uniform && total == 0)
			GUI_FontDraw(state, font_UI_Basic, dim, b[0] + pad + 200, wtop - line_h * 0.9f,
						 "- all zero: nothing parks here, deliberately");
	}

	// --- the add/clear button, on the slider's row ---
	if (!mSelectedRamps.empty())
	{
		float wb[4];
		WeightButtonRect(b, wb);
		const bool has = SelectionHasWeights();

		state->SetState(0,0,0,0,0,0,0);
		float k = mTrackWeightButton ? 0.82f : (mHoverWeightButton ? 1.15f : 1.0f);
		glColor4f(0.26f * k, 0.30f * k, 0.36f * k, 1.0f);
		glBegin(GL_QUADS);
			glVertex2f(wb[0], wb[1]); glVertex2f(wb[0], wb[3]);
			glVertex2f(wb[2], wb[3]); glVertex2f(wb[2], wb[1]);
		glEnd();
		glColor4f(0.55f, 0.55f, 0.58f, 1.0f);
		glBegin(GL_LINE_LOOP);
			glVertex2f(wb[0], wb[1]); glVertex2f(wb[0], wb[3]);
			glVertex2f(wb[2], wb[3]); glVertex2f(wb[2], wb[1]);
		glEnd();

		// "Simple Mode", not "Clear": the weights are stashed, not destroyed,
		// and the button's job is to say which of the two controls is in charge.
		const char * cap = has ? "Simple Mode" : "Set Spawn Weights";
		float cw = GUI_MeasureRange(font_UI_Basic, cap, cap + strlen(cap));
		GUI_FontDraw(state, font_UI_Basic, WED_Color_RGBA(wed_Table_Text),
					 (wb[0] + wb[2]) * 0.5f - cw * 0.5f, wb[1] + 4, cap);
	}

	// --- coverage readout (WED_LiveryFormatSpec.md §4.5) ---
	// Sits directly under the size slider because those are its two inputs: the
	// operators ticked below, measured against the size range set above. An edit
	// to either updates this line on the same frame.
	{
		float cov_top, cov_bot;
		CoverageYRange(b, cov_top, cov_bot);

		// One flag drives both. The cards and the readout are answers to the
		// same query, so recomputing one without the other is exactly how they
		// would drift back into contradicting each other.
		// Hub positions arrive from a worker thread (WED_LiveryIndex::PollHubs);
		// until they do, keep drawing so the frame that attaches them comes soon.
		if (mLiveryIndex.PollHubs())			mCoverageDirty = true;
		else if (mLiveryIndex.HubsPending())	Refresh();

		if (mCoverageDirty) { RecomputeCoverage(); RebuildAirlineCards(); SetRowsDirty(); }

		float col_warn[4]  = { 1.00f, 0.45f, 0.35f, 1.0f };	// a stand that parks nothing
		float col_good[4]  = { 0.55f, 0.85f, 0.55f, 1.0f };
		float col_muted[4] = { 0.62f, 0.62f, 0.62f, 1.0f };

		char head[256]; char detail[256];
		float * head_col = col_muted;

		head[0] = 0; detail[0] = 0;

		if (mSelectedRamps.empty())
		{
			snprintf(head,   sizeof(head),   "Coverage");
			snprintf(detail, sizeof(detail), "Select a ramp start to see what can park on it.");
		}
		else if (mSelectedRamps.size() == 1 && mSelectedRamps[0]->GetRampOperationType() == ramp_operation_None)
		{
			// None is written to apt.dat as "none", which X-Plane reads as no static
			// aircraft - the same as six zero weights. Nothing to compute.
			snprintf(head,   sizeof(head),   "No static aircraft - operation type None");
		}
		else if (mCoverage.index_ready && mLiveryIndex.HubsPending())
		{
			// The range rule cannot be answered yet, and the cards below have not
			// had it applied - say that rather than print a number it will change.
			snprintf(head,   sizeof(head),   "Checking which operators can reach this airport...");
		}
		else if (!mCoverage.index_ready)
		{
			// Explicitly NOT "0 operators" - spec §6.4 calls out a missing or
			// mismatched index as the failure that costs a day precisely because
			// its only symptom is things quietly not appearing. A readout that
			// printed a confident zero here would be worse than none at all.
			snprintf(head,   sizeof(head),   "Coverage unavailable - no livery index");
			snprintf(detail, sizeof(detail), "%s", NoIndexSentence(mAirlineDirectory.LoadError()).c_str());
		}
		else if (mCoverage.stands == 1)
		{
			char range[8];
			if (mCoverage.lo_class == mCoverage.hi_class)
				snprintf(range, sizeof(range), "%c", mCoverage.lo_class);
			else
				snprintf(range, sizeof(range), "%c-%c", mCoverage.lo_class, mCoverage.hi_class);

			if (mCoverage.airlines_listed == 0)
			{
				snprintf(head,   sizeof(head),   "This stand parks nothing - no operators listed");
				head_col = col_warn;
				snprintf(detail, sizeof(detail), "Tick an operator below. Size range is %s.", range);
			}
			else if (mCoverage.weighted)
			{
				// The §4.5 sentence. The percentage is OCCUPANCY - how often the
				// stand has an aircraft at all - not "the chance of this
				// operator", which on a single-operator stand is always 100% and
				// carries no information.
				const int pct = (int) (mCoverage.p_occupied * 100.0f + 0.5f);
				if (pct == 0)
				{
					// Three ways to park nothing, and they are NOT the same
					// news. Saying "all zero, or nothing fits" would name both
					// and choose neither, which is the §4.5 complaint restated
					// rather than answered.
					switch (mCoverage.empty_cause)
					{
					case Coverage::empty_ByChoice:
						snprintf(head, sizeof(head), "This stand will not spawn any static aircraft");
						head_col = col_muted;			// deliberate - not a warning
						snprintf(detail, sizeof(detail),
							"Every class weight is zero. That is a valid way to say a stand stays empty.");
						break;

					case Coverage::empty_NoArtYet:
						snprintf(head, sizeof(head), "Nothing can park here yet - no aircraft exists at size %s", range);
						head_col = col_muted;			// ahead of the art, not wrong (R14)
						snprintf(detail, sizeof(detail),
							"X-Plane ships no aircraft at all at this size. The stand starts working the day one does, with no edit here.");
						break;

					case Coverage::empty_OutOfRange:
						snprintf(head, sizeof(head), "This stand parks nothing - nothing listed can reach it");
						head_col = col_warn;
						snprintf(detail, sizeof(detail),
							"The %d listed operators fly size %s, but none can reach here from a hub. List one based nearer.",
							mCoverage.airlines_listed, range);
						break;

					default:
						snprintf(head, sizeof(head), "This stand parks nothing - and that looks unintended");
						head_col = col_warn;
						snprintf(detail, sizeof(detail),
							"None of the %d listed operators has an aircraft at size %s, though other operators do. Widen the size range, or list one that flies it.",
							mCoverage.airlines_listed, range);
						break;
					}
				}
				else
				{
					snprintf(head, sizeof(head),
						"This stand will spawn aircraft %d%% of the time", pct);
					head_col = (pct >= 95) ? col_good : col_warn;
					snprintf(detail, sizeof(detail),
						"Weighted across %s, from %d of %d listed operators. Empty the other %d%%.",
						range, mCoverage.airlines_eligible, mCoverage.airlines_listed, 100 - pct);
				}
			}
			else if (mCoverage.stands_empty > 0)
			{
				snprintf(head,   sizeof(head),   "This stand parks nothing");
				head_col = col_warn;
				snprintf(detail, sizeof(detail),
					"None of the %d listed operators has a model at size %s. Widen the range, or list an operator that flies one.",
					mCoverage.airlines_listed, range);
			}
			else
			{
				snprintf(head,   sizeof(head),   "Parks aircraft from %d of %d listed operators",
					mCoverage.airlines_eligible, mCoverage.airlines_listed);
				head_col = col_good;
				snprintf(detail, sizeof(detail), "%d of %d sizes in %s can be filled.",
					mCoverage.classes_filled, mCoverage.classes_in_range, range);
			}
		}
		else if (mCoverage.stands_empty > 0)
		{
			// The bulk case, which is the one spec §4.5 says has no eyes on it.
			snprintf(head,   sizeof(head),   "%d of %d selected stands park nothing",
				mCoverage.stands_empty, mCoverage.stands);
			head_col = col_warn;
			snprintf(detail, sizeof(detail), "Each stand measured against its own size range and operator list.");
		}
		else
		{
			snprintf(head,   sizeof(head),   "All %d selected stands can be filled", mCoverage.stands);
			head_col = col_good;
			snprintf(detail, sizeof(detail), "Each stand measured against its own size range and operator list.");
		}

		// Variety collapse outranks the ordinary "it works" line, because from
		// the author's side nothing looks wrong: the stand spawns aircraft, the
		// occupancy reads high, and every one of them is the same airline. It
		// does NOT outrank an empty stand - that is still the worse news - so
		// only a healthy-looking readout gets replaced.
		if (!mCoverage.sole_operator.empty() && head_col != col_warn)
		{
			string nice = mCoverage.sole_operator;
			for (size_t i = 0; i < nice.size(); ++i) nice[i] = (char) toupper((unsigned char) nice[i]);

			char rng[8];
			if (mCoverage.lo_class == mCoverage.hi_class)
				snprintf(rng, sizeof(rng), "%c", mCoverage.lo_class);
			else
				snprintf(rng, sizeof(rng), "%c-%c", mCoverage.lo_class, mCoverage.hi_class);

			snprintf(head, sizeof(head), "Only %s will ever park here", nice.c_str());
			head_col = col_warn;
			snprintf(detail, sizeof(detail),
				"The other %d listed operators have no aircraft at size %s, so every aircraft on this stand is the same airline.",
				mCoverage.airlines_listed - 1, rng);
		}

		// What the range rule removed at THIS stand, named. Without this the author
		// sees United's card shrink to a 777 and has no way to know whether that is
		// the index, the weights, or a bug. One clause per operator, types joined.
		// Only the operators LISTED ON THIS STAND. mRangeHidden is filled for every
		// card the pane builds - the whole index - so unfiltered it opened with
		// American and Cargojet on a stand that lists Southwest and easyJet. The
		// line exists to explain why a listed operator's card shrank or vanished;
		// what happened to operators the author never chose is not its business.
		string range_line;
		if (!mRangeHidden.empty() && mSelectedRamps.size() == 1)
		{
			set<string> listed_lc = ParseCodes(mSelectedRamps[0]->GetAirlines());
			string s;
			int n = 0;
			for (map<string, vector<string> >::const_iterator i = mRangeHidden.begin(); i != mRangeHidden.end(); ++i)
			{
				string lc = i->first;
				for (size_t c = 0; c < lc.size(); ++c) lc[c] = (char) tolower((unsigned char) lc[c]);
				if (!listed_lc.count(lc)) continue;
				if (n == 4) { s += " ..."; break; }
				s += (n ? "; " : "") + i->first + " ";
				for (size_t k = 0; k < i->second.size(); ++k)
					s += (k ? "/" : "") + i->second[k];
				++n;
			}
			if (n) range_line = "Out of range from their hubs, not offered: " + s + ".";
		}

		// §4.5: the readout MUST name what it resolved against. Appended rather
		// than given its own line, because on a correctly generated index it is
		// reassurance, and on one carrying no stamps at all it is the only
		// warning §6.4's silent mismatch will ever produce.
		// The index version stamp used to be appended here. It is diagnostic, not
		// guidance, and it doubled the length of every readout; it still goes to
		// the log, which is where a build mismatch gets investigated anyway.

		// Wrap to the width we actually have, then size the section to the result
		// BEFORE laying it out - so the background, the lines and everything below
		// agree on this frame. (One long line ran off the right edge on every
		// stand that had anything to say.)
		float avail_w = (float) (b[2] - b[0]) - pad * 2;
		vector<string> body = WrapText(font_UI_Basic, detail, avail_w);
		if (!range_line.empty())
		{
			vector<string> more = WrapText(font_UI_Basic, range_line, avail_w);
			body.insert(body.end(), more.begin(), more.end());
		}
		mCoverageHasDetail = !body.empty();
		if (!sCoverageExpanded) body.clear();
		mCoverageLineCount = 1 + (int) body.size();
		CoverageYRange(b, cov_top, cov_bot);

		// zone background, matching the slider's so the two read as one stack
		state->SetState(0,0,0,0,0,0,0);
		glColor4f(0.14f, 0.14f, 0.14f, 1.0f);
		glBegin(GL_QUADS);
			glVertex2f((float) b[0] + 1, cov_bot);
			glVertex2f((float) b[2] - 1, cov_bot);
			glVertex2f((float) b[2] - 1, cov_top);
			glVertex2f((float) b[0] + 1, cov_top);
		glEnd();
		glColor4f(0.40f, 0.40f, 0.40f, 1.0f);
		glBegin(GL_LINE_LOOP);
			glVertex2f((float) b[0] + 1, cov_bot);
			glVertex2f((float) b[2] - 1, cov_bot);
			glVertex2f((float) b[2] - 1, cov_top);
			glVertex2f((float) b[0] + 1, cov_top);
		glEnd();

		// The toggle: a small triangle at the right of the headline - pointing
		// right while collapsed, down while open. Wound clockwise, since this
		// pane's Y-up counter-clockwise shapes are back faces to WED's culling.
		if (mCoverageHasDetail)
		{
			float tri = line_h * 0.55f;
			float cx = (float) b[2] - pad - tri * 0.5f;
			float cy = cov_top - line_h * 0.6f;
			float k = mHoverCoverageToggle ? 1.0f : 0.7f;
			state->SetState(0,0,0,0,1,0,0);
			glColor4f(0.8f * k, 0.8f * k, 0.82f * k, 1.0f);
			glBegin(GL_TRIANGLES);
			if (sCoverageExpanded)
			{
				glVertex2f(cx - tri * 0.5f, cy + tri * 0.3f);
				glVertex2f(cx + tri * 0.5f, cy + tri * 0.3f);
				glVertex2f(cx,              cy - tri * 0.4f);
			}
			else
			{
				glVertex2f(cx - tri * 0.3f, cy + tri * 0.5f);
				glVertex2f(cx + tri * 0.4f, cy);
				glVertex2f(cx - tri * 0.3f, cy - tri * 0.5f);
			}
			glEnd();
			state->SetState(0,0,0,0,0,0,0);
		}

		string head_text = ElideToWidth(font_UI_Basic, head, (float) b[2] - pad * 2 - line_h - (b[0] + pad));
		GUI_FontDraw(state, font_UI_Basic, head_col,  b[0] + pad, cov_top - line_h * 0.9f, head_text.c_str());
		for (size_t li = 0; li < body.size(); ++li)
			GUI_FontDraw(state, font_UI_Basic, col_muted, b[0] + pad,
						 cov_top - line_h * (1.9f + (float) li), body[li].c_str());
	}

	// --- airline list workspace: toolbar row (Show Recommendation + Sort) boxed
	// together with the checklist below it, so they read as one panel ---
	if (!mSelectedRamps.empty())
	{
		float * row_col = WED_Color_RGBA(wed_Table_Text);

		// Border around the whole workspace - toolbar row plus every row of
		// content below it, down to the bottom of the pane.
		float tb_top, tb_bot;
		ListToolbarYRange(b, tb_top, tb_bot);
		state->SetState(0,0,0,0,0,0,0);
		glColor4f(0.40f, 0.40f, 0.40f, 1.0f);
		glBegin(GL_LINE_LOOP);
			glVertex2f((float) b[0] + 1, (float) b[1] + 1);
			glVertex2f((float) b[2] - 1, (float) b[1] + 1);
			glVertex2f((float) b[2] - 1, tb_top - 1);
			glVertex2f((float) b[0] + 1, tb_top - 1);
		glEnd();
		// Divider between the toolbar row and the content below it.
		glBegin(GL_LINES);
			glVertex2f((float) b[0] + 1, tb_bot);
			glVertex2f((float) b[2] - 1, tb_bot);
		glEnd();

		bool recommend_available = AirportIsCommercial(mAirportDb, mCurrentAirportIcao);

		// "Show Recommendation" toggle - left of the sort button. Greyed out
		// (75% black mask, unclickable - see hit-testing in MouseDown/Up)
		// when this airport has no commercial data to recommend from at all.
		{
			float r[4];
			RecommendButtonRect(b, r);
			bool on = gShowLiveryRecommendation != 0;
			state->SetState(0,0,0,0,0,0,0);
			if (on)							glColor4f(0.22f, 0.50f, 0.85f, 1.0f);
			else if (mTrackRecommendButton)	glColor4f(0.14f, 0.14f, 0.16f, 1.0f);	// pushed: darker/inset
			else if (mHoverRecommendButton)	glColor4f(0.44f, 0.44f, 0.48f, 1.0f);
			else							glColor4f(0.22f, 0.22f, 0.22f, 1.0f);
			glBegin(GL_QUADS);
				glVertex2f(r[0]+1, r[1]+1); glVertex2f(r[2]-1, r[1]+1);
				glVertex2f(r[2]-1, r[3]-1); glVertex2f(r[0]+1, r[3]-1);
			glEnd();
			glColor4f(0.5f, 0.5f, 0.5f, 1.0f);
			glBegin(GL_LINE_LOOP);
				glVertex2f(r[0]+1, r[1]+1); glVertex2f(r[2]-1, r[1]+1);
				glVertex2f(r[2]-1, r[3]-1); glVertex2f(r[0]+1, r[3]-1);
			glEnd();

			const char * label = "Show Recommendation";
			float lw = GUI_MeasureRange(font_UI_Basic, label, label + strlen(label));
			GUI_FontDraw(state, font_UI_Basic, row_col, r[0] + ((r[2]-r[0])-lw)*0.5f, r[1] + ((r[3]-r[1])-line_h)*0.5f + 2, label);

			if (!recommend_available)
			{
				state->SetState(0,0,0,0,1,0,0);
				glColor4f(0.0f, 0.0f, 0.0f, 0.75f);
				glBegin(GL_QUADS);
					glVertex2f(r[0]+1, r[1]+1); glVertex2f(r[2]-1, r[1]+1);
					glVertex2f(r[2]-1, r[3]-1); glVertex2f(r[0]+1, r[3]-1);
				glEnd();
				state->SetState(0,0,0,0,0,0,0);
			}
		}

		// Sort A-Z / Z-A toggle - flush to the right border.
		{
			float r[4];
			SortButtonRect(b, r);
			state->SetState(0,0,0,0,0,0,0);
			if (mTrackSortButton)		glColor4f(0.14f, 0.14f, 0.16f, 1.0f);
			else if (mHoverSortButton)	glColor4f(0.44f, 0.44f, 0.48f, 1.0f);
			else						glColor4f(0.22f, 0.22f, 0.22f, 1.0f);
			glBegin(GL_QUADS);
				glVertex2f(r[0]+1, r[1]+1); glVertex2f(r[2]-1, r[1]+1);
				glVertex2f(r[2]-1, r[3]-1); glVertex2f(r[0]+1, r[3]-1);
			glEnd();
			glColor4f(0.5f, 0.5f, 0.5f, 1.0f);
			glBegin(GL_LINE_LOOP);
				glVertex2f(r[0]+1, r[1]+1); glVertex2f(r[2]-1, r[1]+1);
				glVertex2f(r[2]-1, r[3]-1); glVertex2f(r[0]+1, r[3]-1);
			glEnd();

			const char * label = mSortDescending ? "Z-A" : "A-Z";
			float lw = GUI_MeasureRange(font_UI_Basic, label, label + strlen(label));
			float arrow_w = line_h * 0.6f;
			float total_w = lw + 4 + arrow_w;
			float tx = r[0] + ((r[2]-r[0])-total_w)*0.5f;
			GUI_FontDraw(state, font_UI_Basic, row_col, tx, r[1] + ((r[3]-r[1])-line_h)*0.5f + 2, label);

			// Little vector arrow (no font-glyph-availability risk this way):
			// pointing up for A-Z (ascending), down for Z-A (descending).
			float ax = tx + lw + 4 + arrow_w * 0.5f;
			float ay = (r[1] + r[3]) * 0.5f;
			glColor4f(row_col[0], row_col[1], row_col[2], 1.0f);
			glBegin(GL_TRIANGLES);
				if (!mSortDescending)
				{
					glVertex2f(ax - arrow_w*0.5f, ay - arrow_w*0.35f);
					glVertex2f(ax + arrow_w*0.5f, ay - arrow_w*0.35f);
					glVertex2f(ax,                ay + arrow_w*0.5f);
				}
				else
				{
					glVertex2f(ax - arrow_w*0.5f, ay + arrow_w*0.35f);
					glVertex2f(ax + arrow_w*0.5f, ay + arrow_w*0.35f);
					glVertex2f(ax,                ay - arrow_w*0.5f);
				}
			glEnd();
		}

		// Live-filter search box, filling the toolbar row left of the
		// Recommend button. GUI_TextField always paints its own opaque
		// box+border regardless of content and has no native placeholder, so
		// we hide the real field and hand-draw a "Lookup Operators"
		// placeholder whenever it's both empty and unfocused, and let the
		// real field take over the moment there's text or it's being edited.
		// Repositioning every Draw() (rather than just once) keeps it glued
		// to RecommendButtonRect() if the pane is ever resized.
		{
			float r[4];
			SearchFieldRect(b, r);
			bool has_query = !mSearchQuery.empty();

			// With an entry to clear, the field's own bounds are trimmed to
			// leave the clear button its own non-overlapping strip on the
			// right - the button is drawn AFTER the field only in z-order
			// (this pane's own Draw() body runs before children - see the
			// culling gotcha note at the top of this function for the same
			// "draw order isn't what you'd assume" theme), so it needs its
			// own space rather than trying to paint over the field's opaque
			// background.
			float field_r2 = r[2];
			if (has_query)
			{
				float clear_r[4];
				SearchClearButtonRect(b, clear_r);
				field_r2 = clear_r[0] - 2;
			}
			int ib[4] = { (int) r[0], (int) r[1], (int) field_r2, (int) r[3] };
			// ONLY WHEN IT MOVED. GUI_TextField::SetBounds() calls Refresh() and
			// broadcasts GUI_SCROLL_CONTENT_SIZE_CHANGED, so setting the same bounds
			// from Draw() on every frame asked for the next frame, forever: this pane
			// redrew at full rate while idle (a core at 100%), GUI_Timer never got a
			// quiet queue to fire in, and a MessageBox - which is only shown once the
			// queue goes idle - stayed invisible behind a disabled WED, which looked
			// exactly like a hang.
			int cur[4];
			mSearchField->GetBounds(cur);
			if (cur[0] != ib[0] || cur[1] != ib[1] || cur[2] != ib[2] || cur[3] != ib[3])
				mSearchField->SetBounds(ib);

			bool show_real = has_query || mSearchField->IsFocused();
			if (show_real)
			{
				mSearchField->Show();

				if (has_query)
				{
					float cr[4];
					SearchClearButtonRect(b, cr);
					state->SetState(0,0,0,0,0,0,0);

					bool pressed = mTrackClearButton;
					float fill = pressed ? 0.70f : (mHoverClearButton ? 0.94f : 0.82f);
					glColor4f(fill, fill, fill, 1.0f);
					glBegin(GL_QUADS);
						glVertex2f(cr[0]+1, cr[1]+1); glVertex2f(cr[2]-1, cr[1]+1);
						glVertex2f(cr[2]-1, cr[3]-1); glVertex2f(cr[0]+1, cr[3]-1);
					glEnd();

					// Two-tone bevel stroke - light/dark corners swap when
					// pressed, for a simple "raised" vs "pushed in" read
					// without needing an actual icon resource.
					float light = pressed ? 0.35f : 0.95f;
					float dark  = pressed ? 0.95f : 0.35f;
					glBegin(GL_LINES);
						glColor4f(light, light, light, 1.0f);
						glVertex2f(cr[0]+1, cr[3]-1); glVertex2f(cr[0]+1, cr[1]+1);
						glVertex2f(cr[0]+1, cr[1]+1); glVertex2f(cr[2]-1, cr[1]+1);
						glColor4f(dark, dark, dark, 1.0f);
						glVertex2f(cr[2]-1, cr[1]+1); glVertex2f(cr[2]-1, cr[3]-1);
						glVertex2f(cr[2]-1, cr[3]-1); glVertex2f(cr[0]+1, cr[3]-1);
					glEnd();

					float x_r = (cr[3]-cr[1]) * 0.22f;
					glColor4f(0.25f, 0.25f, 0.25f, 1.0f);
					DrawX((cr[0]+cr[2])*0.5f + (pressed ? 0.5f : 0.0f), (cr[1]+cr[3])*0.5f - (pressed ? 0.5f : 0.0f), x_r);
				}
			}
			else
			{
				mSearchField->Hide();

				// Idle appearance is deliberately close to the real GUI_TextField's
				// own white background (see its constructor's mColorBkgnd) rather
				// than matching the dark Sort/Recommend buttons - this box IS a
				// text field, just not focused/typed-in yet.
				state->SetState(0,0,0,0,0,0,0);
				glColor4f(0.82f, 0.82f, 0.82f, 1.0f);
				glBegin(GL_QUADS);
					glVertex2f(r[0]+1, r[1]+1); glVertex2f(r[2]-1, r[1]+1);
					glVertex2f(r[2]-1, r[3]-1); glVertex2f(r[0]+1, r[3]-1);
				glEnd();
				glColor4f(0.45f, 0.45f, 0.45f, 1.0f);
				glBegin(GL_LINE_LOOP);
					glVertex2f(r[0]+1, r[1]+1); glVertex2f(r[2]-1, r[1]+1);
					glVertex2f(r[2]-1, r[3]-1); glVertex2f(r[0]+1, r[3]-1);
				glEnd();

				// Small hand-drawn magnifying glass (circle + diagonal handle,
				// bottom-right) - no icon font/resource anywhere in this
				// codebase to pull a real one from.
				float icon_r = line_h * 0.32f;
				float icon_cx = r[2] - 8 - icon_r;
				float icon_cy = (r[1] + r[3]) * 0.5f;
				glColor4f(0.35f, 0.35f, 0.35f, 1.0f);
				DrawCircleOutline(icon_cx, icon_cy, icon_r);
				glBegin(GL_LINES);
					glVertex2f(icon_cx + icon_r*0.7f, icon_cy - icon_r*0.7f);
					glVertex2f(icon_cx + icon_r*1.5f, icon_cy - icon_r*1.5f);
				glEnd();

				// Truncate (never overflow the field) when a narrow pane leaves less
				// room than the full placeholder needs - a search box that's still
				// wide enough for a couple of letters is more useful than one that
				// silently paints text past its own border.
				string placeholder = "Lookup Operators";
				float avail_w = (icon_cx - icon_r - 4) - (r[0] + 6);
				while (!placeholder.empty() && GUI_MeasureRange(font_UI_Basic, placeholder.c_str(), placeholder.c_str() + placeholder.size()) > avail_w)
					placeholder.pop_back();
				if (!placeholder.empty())
				{
					float ph_col[4] = { 0.35f, 0.35f, 0.35f, 1.0f };
					GUI_FontDraw(state, font_UI_Basic, ph_col, r[0] + 6, r[1] + ((r[3]-r[1])-line_h)*0.5f + 2, placeholder.c_str());
				}
			}
		}

		// --- livery preview card strip (framework/scaffolding only - see this pane's
		// .h comment on this section, and WED_LiveryThumbnailCache.h for the render/
		// cache/evict design). ---
		//
		// An empty strip is an ANSWER, not a blank. A stand whose operators have
		// nothing at its classes gets no cards, and saying so here is what stops
		// the strip contradicting the readout directly above it - which is
		// exactly what it used to do, showing four aircraft under the words
		// "this stand parks nothing".

		if (cur_op_enum == ramp_operation_None)
		{
			const char * msg = "No static aircraft will spawn at this spot";
			float msg_w = GUI_MeasureRange(font_UI_Basic, msg, msg + strlen(msg));
			float cx = ((float) b[0] + (float) b[2]) * 0.5f;
			float cy = ((float) b[1] + tb_bot) * 0.5f;
			GUI_FontDraw(state, font_UI_Basic, row_col, cx - msg_w * 0.5f, cy - line_h * 0.4f, msg);
		}
		else
		{
			EnsureRows();
			// How many of the selected ramps carry each code, computed ONCE per draw.
			// Asking per row would be O(rows x ramps) every frame - a few hundred
			// ramps against a few hundred rows is tens of thousands of string
			// parses, per frame, for a checkbox.
			map<string,int> code_counts;
			for (size_t i = 0; i < mSelectedRamps.size(); ++i)
			{
				set<string> c = ParseCodes(mSelectedRamps[i]->GetAirlines());
				for (set<string>::const_iterator j = c.begin(); j != c.end(); ++j)
					++code_counts[*j];
			}
			const int n_ramps = (int) mSelectedRamps.size();
			float top = ContentTop(b);

			// Re-clamp every Draw() against the CURRENT row count, not just when the
			// wheel moves it - a filter/search/sort change can shrink the list out
			// from under an existing scroll position (e.g. scrolled deep into "All
			// Airlines" with recommendations on, then narrowing the search query
			// away most of it) just as easily as a resize can. Airline rows are
			// cards and are several lines tall, so the total comes from the layout
			// pass rather than from a row count times a row height.
			vector<float> tray_h;  TrayHeights(mRowIcaos, tray_h);

			vector<RowSlot> slots;
			float content_h  = LayoutRows(b, mRowIsCard, tray_h, slots);
			float visible_h  = top - (float) b[1];
			float max_scroll = (content_h > visible_h) ? (content_h - visible_h) : 0.0f;
			if (mScrollOffset < 0)          mScrollOffset = 0;
			if (mScrollOffset > max_scroll)
			{
				LOG_MSG("I/LiveryScroll clamp %.0f -> %.0f  (content=%.0f visible=%.0f rows=%d)\n",
						mScrollOffset, max_scroll, content_h, visible_h, (int) mRows.size());
				mScrollOffset = max_scroll;
			}
			if (mScrollOffset != 0.0f)		// the clamp may have moved it - relay out
				content_h = LayoutRows(b, mRowIsCard, tray_h, slots);

			// Thumbnails are rendered at most kMaxRendersPerFrame per frame and
			// everything NOT on screen is evicted at the end, so scrolling a long
			// section does not render a hundred aircraft. The keep-alive set has to
			// hold the SAME strings GetThumbnail() is called with or every entry
			// evicts and re-renders every frame - which looks like a performance
			// mystery rather than a mismatch.
			// A LOCK MUST NOT OUTLIVE ITS CARD. The badge is the only way to
			// release it, so if its operator drops out of the list - a search term,
			// a weight drag that removes its class, a different X-Plane folder -
			// every other card stays dimmed forever with nothing left to click. The
			// same goes for a tray left open on a card that is no longer drawn.
			if (!mLockedAirline.empty() && !CardFor(mLockedAirline))  mLockedAirline.clear();
			if (!mTrayAirline.empty()   && !CardFor(mTrayAirline))    { mTrayAirline.clear();  mTrayOpen = 0.0f; }
			if (!mTrayClosing.empty()   && !CardFor(mTrayClosing))    { mTrayClosing.clear();  mTrayClosingOpen = 0.0f; }
			if (!mCycleAirline.empty()  && !CardFor(mCycleAirline))   mCycleAirline.clear();

			int         renders_this_frame = 0;
			set<string> keep_alive_paths;
			mHoverTipText.clear();		// re-decided below, per frame, by whatever is under the cursor

			// The operation chips sit above the card list and are drawn elsewhere,
			// so their tip has to be claimed here, before the cards get a chance.
			{
				int chip = FilterChipForXY(b, mHoverX, mHoverY);
				if (chip >= 0 && chip < 5) mHoverTipText = kFilterTips[chip];
			}
			if (mHoverCoverageToggle)
				mHoverTipText = sCoverageExpanded ? "Hide the details" : "Show the details: what fills this stand, and what was left out and why";
			if (mHoverPopulate)
				mHoverTipText = (PaneClockNow() < mPopulateFlashUntil && !mPopulateDetail.empty())
					? mPopulateDetail
					: "Adds the airport's operators that have a livery for the sizes each selected stand allows now. "
					  "Keeps everything already listed and leaves the weights alone.";

			// Clip to the content viewport. GUI_Pane::InternalDraw() only scissors to
			// the WHOLE PANE's bounds, not this section's, so without this a card
			// scrolled half past the top paints its full image quad straight over the
			// toolbar and slider above it - which is why the draw loop used to skip
			// anything not entirely inside, and why cards vanished at the border.
			// Clamp both extents to >= 0: ContentTop() subtracts a chain of fixed
			// section heights from the pane's top edge, so dragging the property panel
			// short can put it BELOW the pane bottom, and glScissor turns a negative
			// extent into GL_INVALID_VALUE and then an assert in a debug build.
			glPushAttrib(GL_SCISSOR_BIT);
			glEnable(GL_SCISSOR_TEST);
			{
				int sc_w = (int) (b[2] - b[0]);
				int sc_h = (int) (top - (float) b[1]);
				if (sc_w < 0) sc_w = 0;
				if (sc_h < 0) sc_h = 0;
				glScissor((int) b[0], (int) b[1], sc_w, sc_h);
			}

			// KEEP-ALIVE IS A WIDER WINDOW THAN WHAT IS DRAWN, and it is collected in
			// its own pass because the draw loop below breaks out the moment it goes
			// off the bottom. Built from the visible rows alone - which is what
			// shipped - a card evicted the instant it scrolled out was re-rendered
			// the instant it scrolled back in, so dragging the pane taller re-rendered
			// everything it revealed AND everything it had just pushed past. That is
			// the 0.5-0.8s stall: not one slow thumbnail, but the same thumbnails
			// being thrown away and rebuilt.
			//
			// A card and a half beyond the border in each direction - far enough that
			// a card begins rendering well before it is needed and is not dropped the
			// moment it leaves, which is the window the eviction below uses too.
			const float kOffscreenMargin = CardHeight(b) * 1.5f;
			{
				float keep_hi = top + kOffscreenMargin;
				float keep_lo = (float) b[1] - kOffscreenMargin;

				// What the workers read, most wanted first: the faces on screen,
				// then the cycled card's next face, then the cards about to scroll
				// in, then the rest of the cycled card. Looking somewhere loads
				// there first, rather than waiting for everything above it.
				vector<string> want_visible, want_near, want_cycle;
				mCycleNextReady = true;

				for (size_t vi = 0; vi < mRows.size(); ++vi)
				{
					if (mRows[vi].kind != wed_Row_Airline)  continue;
					if (slots[vi].bot > keep_hi)           continue;
					if (slots[vi].top < keep_lo)           break;		// everything below is further away
					const AirlineCard * ac = CardFor(mRows[vi].icao);
					if (!ac || ac->abs_paths.empty()) continue;

					const bool on_screen = slots[vi].top >= (float) b[1] && slots[vi].bot <= top;
					vector<string> & bucket = on_screen ? want_visible : want_near;
					const int n = (int) ac->abs_paths.size();
					if (mRows[vi].icao == mCycleAirline)
					{
						const string & face = ac->abs_paths[mCycleShow % n];
						const string & next = ac->abs_paths[(mCycleShow + 1) % n];
						bucket.push_back(face);
						bucket.push_back(next);
						want_cycle.insert(want_cycle.end(), ac->abs_paths.begin(), ac->abs_paths.end());
						// Ready = cached, failed, or read and waiting to upload -
						// showing it next frame does the upload, under the fade.
						mCycleNextReady = n <= 1 || mThumbCache.IsSettled(next) || mThumbCache.IsReady(next);
					}
					else
						bucket.push_back(ac->abs_paths[0]);

					// EVERY livery of the card being cycled, not just the one on its
					// face. Keeping only the visible one meant each tick of the hover
					// cycle evicted the aircraft it had just finished showing, and
					// wrapping round re-rendered it - 44 renders for 12 objects in one
					// short session. The parse is cached by then, but the texture
					// allocation and the offscreen pass are not.
					if (mRows[vi].icao == mCycleAirline)
						for (size_t k = 0; k < ac->abs_paths.size(); ++k)
							keep_alive_paths.insert(ac->abs_paths[k]);
					else
						keep_alive_paths.insert(ac->abs_paths[0]);
				}

				want_visible.insert(want_visible.end(), want_near.begin(), want_near.end());
				want_visible.insert(want_visible.end(), want_cycle.begin(), want_cycle.end());
				mThumbCache.Want(want_visible);
			}

			for (size_t vi = 0; vi < mRows.size(); ++vi)
			{
				const WED_LiveryDisplayRow & row = mRows[vi];
				float row_top = slots[vi].top;
				float row_bot = slots[vi].bot;
				// OVERLAP, not containment. Testing "is it entirely inside" made a
				// card vanish the instant its edge crossed the border instead of
				// being clipped by the scissor below, which is the whole reason that
				// scissor exists. The margin means it also starts rendering a card
				// and a half early, so the work is done before it is looked at.
				if (slots[vi].top < (float) b[1] - kOffscreenMargin) break;	// this and everything below are far off
				if (slots[vi].slot_bot > top + kOffscreenMargin) continue;	// far above; a later row may still be near

				if (row.kind == wed_Row_Gap)
					continue;

				if (row.kind == wed_Row_Airline)
				{
					const AirlineCard * ac = CardFor(row.icao);
					if (!ac) continue;							// nothing modelled - no card to draw

					map<string,int>::const_iterator cc = code_counts.find(row.icao);
					const int  n_with = (cc == code_counts.end()) ? 0 : cc->second;

					// At rest a card shows index 0 - the operator's largest aircraft
					// here. While hovered it steps through the rest, wrapping against
					// THIS card's length rather than whatever length it had when the
					// cursor arrived: a weight drag can shorten it mid-cycle.
					int show = 0;
					if (!ac->abs_paths.empty())
					{
						// Pointing at a tray row HOLDS the face on that aircraft;
						// otherwise the hover cycle drives it.
						if (row.icao == mTrayAirline && mTrayHoverIdx >= 0 &&
							mTrayHoverIdx < (int) ac->abs_paths.size())
							show = mTrayHoverIdx;
						else if (row.icao == mCycleAirline)
							show = mCycleShow % (int) ac->abs_paths.size();
					}

					const bool locked = (!mLockedAirline.empty() && row.icao == mLockedAirline);
					const bool dimmed = (!mLockedAirline.empty() && row.icao != mLockedAirline);

					// WHAT THE TIP SAYS, decided here because this is where the
					// card's sub-rectangles are known. Most specific target wins:
					// the lock and the tray tab both sit on the card, so a generic
					// "here is what they fly" would otherwise shadow the two
					// controls the cursor is actually on.
					if ((int) vi == mHoverRow)
					{
						float lr[4], tr[4];
						LockIconRect(slots[vi], lr);
						TrayTabRect (slots[vi], tr);

						// A lock badge is small, so its target is grown by a few
						// percent of the card - enough to forgive a near miss
						// without reaching the tray tab below it.
						float grow = (std::max)(3.0f, (slots[vi].x1 - slots[vi].x0) * 0.03f);

						if (mHoverX >= lr[0] - grow && mHoverX <= lr[2] + grow &&
							mHoverY >= lr[1] - grow && mHoverY <= lr[3] + grow)
						{
							mHoverTipText = locked ? "Spawn every listed operator again"
												   : "Spawn this operator only";
						}
						else if (mHoverX >= tr[0] && mHoverX <= tr[2] &&
								 mHoverY >= tr[1] && mHoverY <= tr[3] &&
								 ac->labels.size() > 1)
						{
							mHoverTipText = "Show all of this operator's aircraft";
						}
						else if (ac->labels.size() > 1)
						{
							string t = ac->name + ": ";
							for (size_t k = 0; k < ac->labels.size(); ++k)
							{
								if (k) t += ", ";
								t += ac->labels[k];
							}
							mHoverTipText = t;
						}
					}

					keep_alive_paths.insert(ac->abs_paths[show]);
					float tray_frac = (mRows[vi].icao == mTrayAirline)    ? mTrayOpen
									: (mRows[vi].icao == mTrayClosing)    ? mTrayClosingOpen
									: 0.0f;

					// The slideshow crossfades: the outgoing face stays under the
					// incoming one while it goes from transparent to solid.
					int fade_from = -1;
					float fade = 1.0f;
					if (row.icao == mCycleAirline && mTrayHoverIdx < 0 && mCycleFade < kCycleFadeSec &&
						!ac->abs_paths.empty())
					{
						fade_from = mCyclePrevShow % (int) ac->abs_paths.size();
						fade = mCycleFade / kCycleFadeSec;
					}

					DrawAirlineCard(state, slots[vi], *ac, show,
									n_with == n_ramps, (int) vi == mHoverRow,
									(int) vi == mTrackRow, locked, dimmed,
									tray_frac, renders_this_frame, fade_from, fade);

					if (row.icao == mTrayAirline && mTrayOpen > 0.0f)
						DrawCardTray(state, slots[vi], *ac, mTrayOpen, show);
					else if (row.icao == mTrayClosing && mTrayClosingOpen > 0.0f)
						DrawCardTray(state, slots[vi], *ac, mTrayClosingOpen, -1);
					continue;
				}

				if (row.kind == wed_Row_Note)
				{
					// Indented past the section header and dimmed - it is an
					// explanation, not a selectable entry. Elided against the row's
					// own width so a narrow property panel cannot push it outside
					// the border (same GUI_MeasureRange approach the card captions
					// use, no new machinery).
					float note_col[4] = { 0.62f, 0.62f, 0.64f, 1.0f };
					float note_x = b[0] + pad + 14;
					string note = ElideToWidth(font_UI_Basic, row.header_text, (float) b[2] - pad - note_x);
					GUI_FontDraw(state, font_UI_Basic, note_col, note_x,
									row_bot + (row_h - line_h) * 0.5f, note.c_str());
					continue;
				}

				if (row.kind == wed_Row_Divider)
				{
					// Near-full-width, centered.
					float dw = (b[2] - b[0]) * 0.85f;
					float dcx = ((float) b[0] + (float) b[2]) * 0.5f;
					float dy = (row_top + row_bot) * 0.5f;
					state->SetState(0,0,0,0,0,0,0);
					glColor4f(0.40f, 0.40f, 0.40f, 1.0f);
					glBegin(GL_LINES);
						glVertex2f(dcx - dw*0.5f, dy);
						glVertex2f(dcx + dw*0.5f, dy);
					glEnd();
					continue;
				}

				if (row.kind == wed_Row_Header)
				{
					// A COLLAPSIBLE SECTION IS DRAWN AS A CONTROL, not as a line of
					// text that happens to be clickable. It gets a band, a chevron
					// and its count, because the only thing that previously told the
					// reader it could be opened was a parenthetical - which reads as
					// a caption, not a button, and left the section looking like one
					// more label in a list of labels.
					// EVERY section is collapsible and every one looks it. Only "All
					// Airlines" drew the box and chevron before, yet MouseUp has
					// always toggled ANY header - so collapsing "Popular Airlines"
					// left a bare label with its cards gone and no hint that it was
					// shut, which reads as a section that lost its contents.
					bool collapsible = !row.header_text.empty() && row.header_text != "Selected";
					bool collapsed   = mCollapsedSections.count(row.header_text) != 0;
					float tx = (float) b[0] + pad;

					if (collapsible)
					{
						// A filled box with its own outline, full width. The faint
						// wash it had before was invisible against the panel, so the
						// section still read as a line of text - which is the thing
						// being fixed.
						state->SetState(0,0,0,0,1,0,0);
						glColor4f(1.0f, 1.0f, 1.0f, (int) vi == mHoverRow ? 0.16f : 0.09f);
						glBegin(GL_QUADS);
							glVertex2f((float) b[0] + kCardGap, row_bot);
							glVertex2f((float) b[2] - kCardGap, row_bot);
							glVertex2f((float) b[2] - kCardGap, row_top);
							glVertex2f((float) b[0] + kCardGap, row_top);
						glEnd();
						glColor4f(1.0f, 1.0f, 1.0f, 0.22f);
						glBegin(GL_LINE_LOOP);
							glVertex2f((float) b[0] + kCardGap + 0.5f, row_bot + 0.5f);
							glVertex2f((float) b[2] - kCardGap - 0.5f, row_bot + 0.5f);
							glVertex2f((float) b[2] - kCardGap - 0.5f, row_top - 0.5f);
							glVertex2f((float) b[0] + kCardGap + 0.5f, row_top - 0.5f);
						glEnd();

						// Same convention as the card's tray arrow: right when shut,
						// down when open. One gesture, one shape, two places.
						float acx = tx + 5.0f, acy = (row_top + row_bot) * 0.5f;
						float lon = 4.5f, lat = 2.8f;
						float ang = collapsed ? 0.0f : -1.57079633f;
						float ca = cosf(ang), sa = sinf(ang);
						const float px[3] = {  lon, -lon * 0.55f, -lon * 0.55f };
						const float py[3] = { 0.0f, -lat,          lat         };
						glColor4f(0.85f, 0.85f, 0.88f, 1.0f);
						glBegin(GL_TRIANGLES);
							for (int i = 0; i < 3; ++i)
								glVertex2f(acx + px[i] * ca - py[i] * sa,
										   acy + px[i] * sa + py[i] * ca);
						glEnd();
						tx += 16.0f;
					}

					string htxt = row.header_text;
					if (collapsible && collapsed)
					{
						char n[48];
						snprintf(n, sizeof(n), "   %d", row.hidden_count);
						htxt += n;
					}
					GUI_FontDraw(state, font_UI_Basic, row_col, tx, row_bot + (row_h - line_h) * 0.5f, htxt.c_str());
					// Every section except the last ("All Airlines") is some flavor of
					// recommendation (manual pin, direct hit, same country, or popular fleet) -
					// star all of them, same as the old two-tier layout starred "Recommended".
					if (row.header_text != "All Airlines" && row.header_text != "Selected")
					{
						float hw = GUI_MeasureRange(font_UI_Basic, row.header_text.c_str(), row.header_text.c_str() + row.header_text.size());
						glColor4f(1.0f, 0.85f, 0.2f, 1.0f);
						DrawStar(b[0] + pad + hw + line_h * 0.45f, (row_top + row_bot) * 0.5f, line_h * 0.4f, line_h * 0.17f);
					}
					continue;
				}

			}

			glPopAttrib();		// restores GL_SCISSOR_TEST enable + rect to whatever they were on entry

			// After the clip, deliberately: a tip has to be allowed outside the box
			// that spawned it, and a card near the bottom has nowhere else to put it.
			DrawHoverTip(state, b);

			mThumbCache.EvictNotVisible(keep_alive_paths);
			if (mThumbCache.HasPending()) Refresh();		// a picture is still being read

			// Rendering is capped per frame, so a screenful that is entirely cold
			// fills in over the next few frames instead of blocking one of them for
			// all of it. Asking for the next frame here is what keeps that going;
			// it stops on its own once everything visible is cached, because then
			// the cap is never reached.
			if (renders_this_frame >= kMaxRendersPerFrame) Refresh();

			// Same mechanism, different reason: while a tray is sliding or a card is
			// cycling, ask for the next frame. Nothing is scheduled when nothing
			// moves, so an idle pane goes quiet.
			if (StepAnimation()) Refresh();

			if (mRows.empty())
			{
				// Name the actual reason. One fixed sentence about operation types
				// used to be shown for every empty list, including one the user had
				// just emptied by typing in the search box - which reads as a bug in
				// the tool rather than as an answer.
				string why;
				if (!mAirlineDirectory.IsLoaded())
					why = NoIndexSentence(mAirlineDirectory.LoadError());
				else if (!mSearchQuery.empty())
					why = "No operator matches \"" + mSearchQuery + "\".";
				else if (cur_op_enum == ramp_operation_None)
					why = "No static aircraft at this stand (operation type None).";
				else
					why = "No operators are tagged for this operation type yet.";

				// Drawn BELOW the card strip, not at ContentTop - that is where the
				// cards themselves start, so this text used to be painted on top of
				// card one.
				float msg_y = top + mScrollOffset - line_h;
				string msg = ElideToWidth(font_UI_Basic, why, (float) b[2] - pad - (b[0] + pad));
				GUI_FontDraw(state, font_UI_Basic, row_col, b[0] + pad, msg_y, msg.c_str());
			}

			// Thin scrollbar affordance, drawn only once there's actually more to see
			// than fits - a plain proportional thumb (not draggable; mouse wheel is
			// the only scroll input this pane supports, same as everywhere else in
			// this custom-drawn class).
			if (max_scroll > 0.0f)
			{
				const float track_w = 4;
				float track_x1 = (float) b[2] - 2;
				float track_x0 = track_x1 - track_w;

				float thumb_h = visible_h * (visible_h / content_h);
				if (thumb_h < 20.0f) thumb_h = 20.0f;
				if (thumb_h > visible_h) thumb_h = visible_h;

				float scroll_frac = mScrollOffset / max_scroll;
				float thumb_top = top - scroll_frac * (visible_h - thumb_h);
				float thumb_bot = thumb_top - thumb_h;

				state->SetState(0,0,0,0,0,0,0);
				glColor4f(0.15f, 0.15f, 0.15f, 1.0f);
				glBegin(GL_QUADS);
					glVertex2f(track_x0, (float) b[1]); glVertex2f(track_x1, (float) b[1]);
					glVertex2f(track_x1, top);          glVertex2f(track_x0, top);
				glEnd();

				glColor4f(0.55f, 0.55f, 0.55f, 1.0f);
				glBegin(GL_QUADS);
					glVertex2f(track_x0, thumb_bot); glVertex2f(track_x1, thumb_bot);
					glVertex2f(track_x1, thumb_top); glVertex2f(track_x0, thumb_top);
				glEnd();
			}
		}
	}

	// --- 75%-black mask when this tab is active but nothing eligible is selected ---
	if (mSelectedRamps.empty())
	{
		state->SetState(0,0,0,0,1,0,0);
		glColor4f(0.0f, 0.0f, 0.0f, 0.75f);
		glBegin(GL_QUADS);
			glVertex2f((float) b[0], (float) b[1]);
			glVertex2f((float) b[2], (float) b[1]);
			glVertex2f((float) b[2], (float) b[3]);
			glVertex2f((float) b[0], (float) b[3]);
		glEnd();

		// Warning text ABOVE the mask (drawn after it, so it isn't itself
		// dimmed by it) - the tab is clickable now with nothing selected
		// (see RebuildSelection()), so this is what actually communicates
		// "nothing to edit yet", not a locked/unclickable tab button.
		float warn_col[4] = { 1.0f, 0.85f, 0.2f, 1.0f };
		float cx = (float) (b[0] + b[2]) * 0.5f;
		float line_h = GUI_GetLineHeight(font_UI_Basic);
		const char * msg = "Select a ramp start to edit its liveries";
		float msg_w = GUI_MeasureRange(font_UI_Basic, msg, msg + strlen(msg));
		GUI_FontDraw(state, font_UI_Basic, warn_col, cx - msg_w * 0.5f, (float) (b[1] + b[3]) * 0.5f - line_h * 0.4f, msg);
	}

	// --- country flag banner, drawn last so its enlarged, overlapping
	// footprint paints over the rows below it rather than being painted
	// under them ---
	if (mFlagTexId != 0)
	{
		float strip_top, strip_bot;
		AirportInfoYRange(b, strip_top, strip_bot);
		float bx0, by0, banner_w, banner_h;
		FlagBannerRect(b, strip_top, strip_bot, bx0, by0, banner_w, banner_h);

		state->SetState(0,1,0,0,1,0,0);		// blend on - the composite has a genuinely transparent background now
		glColor4f(1,1,1,1);
		state->BindTex((int) mFlagTexId, 0);
		glBegin(GL_QUADS);
			// Texture row 0 (the composited buffer's top row - see
			// EnsureFlagTexture()) lands at texture-space t=0, so t=0
			// belongs at this pane's screen-top vertex (Y increases
			// upward here, same as every other quad in this file).
			glTexCoord2f(0, 1); glVertex2f(bx0,            by0);
			glTexCoord2f(1, 1); glVertex2f(bx0 + banner_w, by0);
			glTexCoord2f(1, 0); glVertex2f(bx0 + banner_w, by0 + banner_h);
			glTexCoord2f(0, 0); glVertex2f(bx0,            by0 + banner_h);
		glEnd();
		state->SetState(0,0,0,0,0,0,0);
	}

	glPopAttrib();		// restore GL_CULL_FACE (and anything else in GL_ENABLE_BIT) for whatever draws next
}
