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

#include "WED_LiveryThumbnailCache.h"

#if APL
	#include <OpenGL/gl.h>
#else
	// GL_FRAMEBUFFER/glFramebufferTexture2D/etc aren't in plain <GL/gl.h> on Windows -
	// glew.h is this codebase's existing way to get them (see e.g.
	// WED_LibraryPreviewPane.cpp's MSAA FBO, which this file's rendering is modeled on).
	#include "glew.h"
#endif

#include "ITexMgr.h"
#include "WED_ResourceMgr.h"
#include "WED_PreviewLayer.h"		// draw_obj_at_xyz
#include "XObjDefs.h"				// XObj8
#include "GUI_GraphState.h"
#include "MathUtils.h"				// fltmax3
#include <algorithm>				// std::max - parenthesised at every call site, see below
#include <cmath>

// <windows.h> (force-included via XDefs.h) defines min/max as macros, which swallow
// std::max(...) into a compile error - every call here is written as (std::max)(...)
// to defeat that, the same workaround WED_FlagProjector.cpp and WED_LiveryPane.cpp use.

// 32:9 - a side-on airliner is a long, thin subject, so a letterbox that matches its
// own proportions wastes far less of the card than a 16:9 one. Big enough to stay
// sharp scaled down to a card's real on-screen size, small enough that a screenful of
// cards' worth of these is trivial GPU memory. MUST stay in sync with
// kCardImageAspect in WED_LiveryPane.cpp, which sizes the card that displays it.
static const int kThumbW = 1024;
static const int kThumbH = 288;

// Dead-level side elevation, nose to the left - these cards are meant to read as a
// consistent catalogue of liveries, so every one is shot from the identical angle
// rather than a 3/4 "hero" view.
//
// X-Plane OBJ models point their nose down -Z, which is also the direction the camera
// looks, so an unrotated view stares straight at the tail (you see the APU). Yawing
// 90 degrees about Y swings the nose (0,0,-1) round to (-1,0,0) - screen left, dead
// side-on. 270 would give the mirrored (nose-right) side.
static const float kCamThe = 0.0f;
static const float kCamPsi = 90.0f;

// Defensive ceiling - not something the normal visible-range+margin math in
// WED_LiveryPane should ever bump into, just a backstop against unbounded GPU memory
// if that math ever miscounts.
static const size_t kMaxCachedThumbnails = 32;

bool WED_LiveryThumbnailCache::IsCached(const string & obj_vpath) const
{
	return mCache.find(obj_vpath) != mCache.end();
}

// True when this GL context can do render-to-texture at all.
//
// On Windows and Linux the FBO entry points are GLEW function POINTERS. A driver
// without the extension leaves them NULL, and calling one is an immediate crash -
// it never reaches glCheckFramebufferStatus, so the completeness check below is
// no protection whatsoever. This is not theoretical: remote desktop sessions,
// virtual machines and software rasterisers all show up without it.
//
// Latched on first use. The answer cannot change without a new GL context, and a
// new context means a new pane and a new cache.
static bool FBOAvailable(void)
{
	static int s_state = -1;			// -1 unknown, 0 no, 1 yes
	if (s_state >= 0) return s_state != 0;

#if APL
	// Mac links the ARB entry points directly out of the system GL framework;
	// there is no pointer to be null.
	s_state = 1;
#else
	s_state = (glGenFramebuffers  != NULL && glBindFramebuffer        != NULL &&
			   glGenRenderbuffers != NULL && glFramebufferTexture2D   != NULL &&
			   glCheckFramebufferStatus != NULL) ? 1 : 0;
	if (!s_state)
		LOG_MSG("E/LiveryThumb no framebuffer-object support in this GL context - preview cards disabled.\n");
#endif
	return s_state != 0;
}

const WED_LiveryThumbnail * WED_LiveryThumbnailCache::GetThumbnail(WED_ResourceMgr * res_mgr, ITexMgr * tex_mgr,
	GUI_GraphState * g, const string & obj_vpath)
{
	// Checked before the cache lookup is even worth doing: without FBOs nothing
	// will ever land in the cache, and the caller already handles a null return by
	// drawing the card without a picture.
	if (!FBOAvailable()) return nullptr;

	auto it = mCache.find(obj_vpath);
	if (it != mCache.end())
		return &it->second;

	if (mFailed.count(obj_vpath))
		return nullptr;		// already tried this one - see mFailed's comment

	if (mCache.size() >= kMaxCachedThumbnails)
		return nullptr;

	const XObj8 * o = nullptr;
	if (!res_mgr || !res_mgr->GetObj(obj_vpath, o, 0) || !o)
	{
		// Logged ONCE per path - mFailed short-circuits every later attempt, so
		// this stops being a per-frame log write and a per-frame file open.
		LOG_MSG("E/LiveryThumb GetObj FAILED for %s (res_mgr=%p)\n", obj_vpath.c_str(), (void *) res_mgr);
		LOG_FLUSH();
		mFailed.insert(obj_vpath);
		return nullptr;
	}

	double real_radius = fltmax3(
		o->xyz_max[0] - o->xyz_min[0],
		o->xyz_max[1] - o->xyz_min[1],
		o->xyz_max[2] - o->xyz_min[2]);
	if (real_radius <= 0.0)
		real_radius = 1.0;
	double xyz_off[3] = {
		-(o->xyz_max[0] + o->xyz_min[0]) * 0.5,
		-(o->xyz_max[1] + o->xyz_min[1]) * 0.5,
		-(o->xyz_max[2] + o->xyz_min[2]) * 0.5 };

	// Remember what was bound/current before hijacking it, so this always leaves the
	// caller's own on-screen rendering state exactly as it found it, success or not.
	GLint prev_viewport[4];
	glGetIntegerv(GL_VIEWPORT, prev_viewport);
	GLint prev_fbo = 0;
	glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prev_fbo);

	// GUI_Pane::InternalDraw() (see GUI_Pane.cpp) leaves GL_SCISSOR_TEST enabled with
	// a rect clipped to this pane's on-screen WINDOW pixel coordinates for the whole
	// duration of Draw(). Our FBO has its own unrelated 0..kThumbW/kThumbH coordinate
	// space, so that leftover window-space scissor rect clips out this entire
	// off-screen render (even glClear() respects it) - the first version of this
	// rendered nothing at all into the texture (came out solid black) for exactly
	// this reason. glPushAttrib(GL_SCISSOR_BIT) saves both the enable flag and the
	// rect in one shot; glPopAttrib() below restores them afterward.
	// GL_LIGHTING_BIT and GL_COLOR_BUFFER_BIT join the scissor state here because
	// the render below sets a light, a light model with an ambient of 2.0, and its
	// own clear colour - all of which are GLOBAL. Left behind, anything later in
	// the frame that enables lighting inherits this pane's ambient and washes out.
	glPushAttrib(GL_SCISSOR_BIT | GL_LIGHTING_BIT | GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
	glDisable(GL_SCISSOR_TEST);

	GLuint tex = 0;
	glGenTextures(1, &tex);
	glBindTexture(GL_TEXTURE_2D, tex);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, kThumbW, kThumbH, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);

	GLuint depth_rb = 0;
	glGenRenderbuffers(1, &depth_rb);
	glBindRenderbuffer(GL_RENDERBUFFER, depth_rb);
	glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT, kThumbW, kThumbH);

	GLuint fbo = 0;
	glGenFramebuffers(1, &fbo);
	glBindFramebuffer(GL_FRAMEBUFFER, fbo);
	glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tex, 0);
	glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, depth_rb);

	bool complete = glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
	LOG_MSG("I/LiveryThumb %s: fbo_complete=%d radius=%.3f\n", obj_vpath.c_str(), (int) complete, real_radius);
	LOG_FLUSH();
	if (complete)
	{
		glViewport(0, 0, kThumbW, kThumbH);

		// SetState MUST come before the glClear below, exactly as it does in
		// WED_LibraryPreviewPane's res_Object case. Its last argument (write=true)
		// is what re-enables glDepthMask - and glClear(GL_DEPTH_BUFFER_BIT) is
		// SILENTLY A NO-OP while the depth mask is off. We run nested inside
		// WED_LiveryPane::Draw()'s 2D GUI drawing, which leaves glDepthMask(GL_FALSE)
		// behind, so clearing first (as an earlier version of this file did) left the
		// depth renderbuffer full of uninitialized garbage - zeros on this driver.
		// With GUI_GraphState::Init()'s glDepthFunc(GL_LEQUAL), every fragment of the
		// object then failed the depth test and nothing was ever drawn, while the
		// depth-test-disabled diagnostic shapes drew just fine. That asymmetry is
		// exactly what this ordering fixes.
		g->SetState(false, 1, false, true, true, true, true);

		glClearColor(0, 0, 0, 0);
		glClearDepth(1.0);			// explicit rather than inherited - see above
		glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
		CHECK_GL_ERR

		// AUTO-FILL: fit the projection to the object's ACTUAL on-screen silhouette
		// rather than to a generic bounding sphere, so the model touches the left and
		// right edges of the card instead of floating in a sea of empty space (a
		// side-on airliner fills maybe a third of a radius-fitted frame).
		//
		// Because the camera orientation is a known constant, this needs no pixel/edge
		// detection: run the centred bounding box's 8 corners through the SAME rotation
		// the modelview below applies, and the extremes of the results are exactly the
		// projected silhouette's half-width/half-height. GL applies the two rotations
		// below as Rx(the) * Ry(psi) * v, so Ry goes first here too.
		const double kDeg2Rad = 0.0174532925199432957;
		double cp = cos(kCamPsi * kDeg2Rad), sp = sin(kCamPsi * kDeg2Rad);
		double ct = cos(kCamThe * kDeg2Rad), st = sin(kCamThe * kDeg2Rad);

		double half_w = 0.0, half_h = 0.0;
		for (int corner = 0; corner < 8; ++corner)
		{
			double mx = ((corner & 1) ? o->xyz_max[0] : o->xyz_min[0]) + xyz_off[0];
			double my = ((corner & 2) ? o->xyz_max[1] : o->xyz_min[1]) + xyz_off[1];
			double mz = ((corner & 4) ? o->xyz_max[2] : o->xyz_min[2]) + xyz_off[2];

			double rx =  mx * cp + mz * sp;		// Ry(psi)
			double rz = -mx * sp + mz * cp;
			double fx =  rx;					// Rx(the) leaves x alone
			double fy =  my * ct - rz * st;

			half_w = (std::max)(half_w, fabs(fx));
			half_h = (std::max)(half_h, fabs(fy));
		}

		// A hair of padding so a wingtip/nose pixel can't be clipped by rounding.
		const double kFillPad = 1.02;
		half_w *= kFillPad;
		half_h *= kFillPad;
		if (half_w <= 0.0) half_w = real_radius;		// degenerate object - fall back to something sane
		if (half_h <= 0.0) half_h = real_radius;

		// Grow whichever axis is slack so the card's fixed aspect is preserved (never
		// stretch the model), then the tighter axis is the one that ends up touching
		// its edges - for a side-on aircraft in a 32:9 frame that's the length.
		double aspect  = (double) kThumbW / (double) kThumbH;
		double ortho_h = (std::max)(half_h, half_w / aspect);
		double ortho_w = ortho_h * aspect;

		// Orthographic, not perspective - a catalogue of liveries wants every aircraft
		// drawn without foreshortening so two cards can be compared directly.
		// (WED_LibraryPreviewPane uses a frustum instead because it's an interactive
		// "look at this object" view, a different job.) real_radius as the depth range
		// always covers the object, whatever the rotation.
		glMatrixMode(GL_PROJECTION);
		glPushMatrix();
		glLoadIdentity();
		glOrtho(-ortho_w, ortho_w, -ortho_h, ortho_h, -real_radius, real_radius);
		glMatrixMode(GL_MODELVIEW);
		glPushMatrix();
		glLoadIdentity();
		glRotatef(kCamThe, 1, 0, 0);
		glRotatef(kCamPsi, 0, 1, 0);

		GLfloat light_pos[4] = { -1, 1, 1, 0 };
		glLightfv(GL_LIGHT0, GL_POSITION, light_pos);
		glEnable(GL_LIGHT0);
		GLfloat ambient_color[4] = { 2, 2, 2, 2 };
		glLightModelfv(GL_LIGHT_MODEL_AMBIENT, ambient_color);
		glLightModeli(GL_LIGHT_MODEL_LOCAL_VIEWER, false);
		glEnable(GL_LIGHTING);

		draw_obj_at_xyz(tex_mgr, o, xyz_off[0], xyz_off[1], xyz_off[2], 0, g);
		CHECK_GL_ERR
		glDisable(GL_LIGHTING);

		glPopMatrix();
		glMatrixMode(GL_PROJECTION);
		glPopMatrix();
	}
	else
	{
		LOG_MSG("E/LiveryThumb offscreen FBO incomplete for %s\n", obj_vpath.c_str());
		LOG_FLUSH();
	}

	glBindFramebuffer(GL_FRAMEBUFFER, (GLuint) prev_fbo);
	glViewport(prev_viewport[0], prev_viewport[1], prev_viewport[2], prev_viewport[3]);
	glPopAttrib();		// restores GL_SCISSOR_TEST enable + rect to whatever they were on entry
	glDeleteFramebuffers(1, &fbo);
	glDeleteRenderbuffers(1, &depth_rb);

	if (!complete)
	{
		glDeleteTextures(1, &tex);
		return nullptr;
	}

	WED_LiveryThumbnail entry;
	entry.tex = tex;
	entry.w = kThumbW;
	entry.h = kThumbH;
	auto ins = mCache.emplace(obj_vpath, entry);
	return &ins.first->second;
}

void WED_LiveryThumbnailCache::EvictNotVisible(const set<string> & currently_visible)
{
	for (auto it = mCache.begin(); it != mCache.end(); )
	{
		if (currently_visible.count(it->first) == 0)
		{
			glDeleteTextures(1, &it->second.tex);
			it = mCache.erase(it);
		}
		else
			++it;
	}
}

void WED_LiveryThumbnailCache::DiscardAll()
{
	for (auto & kv : mCache)
		glDeleteTextures(1, &kv.second.tex);
	mCache.clear();
}
