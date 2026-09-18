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
#include <cstring>				// strstr, for the APL extension check

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

// ...except that five shipped assets do NOT point their nose down -Z. The MD-80
// family (Delta, Alitalia, SAS) and the CRJ-100 pair are modelled yawed 90
// degrees in their own files, so the fixed camera above stares straight up their
// nose and the card shows a head-on aircraft among 293 side-on ones.
//
// The fix is a measurement, not a list of paths: a list would be wrong the day
// Laminar re-exports one of them, and would not cover an add-on library.
//
// WHAT IT MEASURES: the highest point of an aircraft lies on its CENTRELINE -
// the fin tip, or the middle of a T-tail's stabiliser - and sits near one end of
// the fuselage. So take the top 1% of vertices and ask, for each horizontal
// axis, how far off that axis's centre they sit. The span axis gives ~0.0 (they
// hug it) and the fuselage axis ~0.9 (they are out at one end). Across the whole
// shipped library the two numbers come out 0.00 vs 0.90 for a normal aircraft
// and 0.79 vs 0.00 for a yawed one, which is not a threshold anyone has to tune.
//
// WHY IT ASKS THAT rather than "is the model longer than it is wide": a glider
// is wider than it is long, and would be rotated wrongly. Earlier drafts tested
// how the top vertices SPREAD instead, which reads exactly backwards on a T-tail
// - a fin spreads along the fuselage, a stabiliser across it - and both families
// that are actually broken here have T-tails.
//
// WHEN IT CANNOT TELL, IT DOES NOTHING. Twin tails (F-15, F/A-18), V-tails
// (SF50) and helicopters have no single centreline high point, so neither axis
// hugs and both bands below reject. All seven such objects in the library are
// correctly oriented already, so leaving the convention alone is the right
// answer for them - and staying still is the only safe failure here, since a
// wrong rotation is worse than the wrong convention it was trying to repair.
// IT CANNOT BE DONE FROM THE BOUNDING BOX. "Longer than it is wide" sounds like
// the whole answer and is not: sorted by X/Z aspect, the shipped library puts the
// PA-28 at 1.47 and the MD-80 at 1.35, because a Cherokee really is wider than it
// is long. Any threshold that rotates the MD-80 breaks eight correctly-oriented
// light aircraft, so the vertices have to be looked at.
//
// AND THEY CANNOT BE READ FROM THE LOADED OBJECT EITHER, which is what made the
// first version of this a no-op: ObjDraw.cpp:284 does geo_tri.clear(8) once the
// mesh is in VRAM to free the RAM, so anything WED has already drawn - and the
// map draws static aircraft - arrives here with an empty point pool. We take that
// pool when it is still full and fall back to the file when it is not, memoised,
// because the answer is a property of the asset and never changes.
static bool	CentrelineOffsets(const XObj8 * o, const string & obj_path, double & off_x, double & off_z, double & sign_x)
{
	double y0 = o->xyz_min[1], y1 = o->xyz_max[1];
	double hx = (o->xyz_max[0] - o->xyz_min[0]) * 0.5;
	double hz = (o->xyz_max[2] - o->xyz_min[2]) * 0.5;
	if (y1 <= y0 || hx <= 0.0 || hz <= 0.0) return false;

	double cx  = (o->xyz_max[0] + o->xyz_min[0]) * 0.5;
	double cz  = (o->xyz_max[2] + o->xyz_min[2]) * 0.5;
	double cut = y0 + 0.99 * (y1 - y0);			// the top 1% - fin tip or T-tail centre

	double sum_x = 0.0, sum_z = 0.0, signed_x = 0.0;
	int    top   = 0;

	int n = o->geo_tri.count();
	if (n >= 32)
	{
		for (int i = 0; i < n; ++i)
		{
			const float * v = o->geo_tri.get(i);
			if (v[1] < cut) continue;
			sum_x    += fabs(v[0] - cx) / hx;
			signed_x +=     (v[0] - cx) / hx;
			sum_z    += fabs(v[2] - cz) / hz;
			++top;
		}
	}
	else
	{
		// The pool was freed. Re-read the VT records off disk. One pass, nothing
		// retained: the Y cut comes from the bounding box, which survives, so
		// there is no need to find the maximum first. Note VT is followed by a
		// TAB in most shipped assets and a space in others.
		FILE * fi = fopen(obj_path.c_str(), "r");
		if (!fi) return false;
		char line[512];
		while (fgets(line, sizeof(line), fi))
		{
			if (line[0] != 'V' || line[1] != 'T' || (line[2] != ' ' && line[2] != '\t')) continue;
			double x, y, z;
			if (sscanf(line + 3, "%lf %lf %lf", &x, &y, &z) != 3) continue;
			if (y < cut) continue;
			sum_x    += fabs(x - cx) / hx;
			signed_x +=     (x - cx) / hx;
			sum_z    += fabs(z - cz) / hz;
			++top;
		}
		fclose(fi);
	}

	if (top < 4) return false;
	off_x  = sum_x    / (double) top;
	off_z  = sum_z    / (double) top;
	sign_x = signed_x / (double) top;
	return true;
}

static float	ModelYawCorrection(const XObj8 * o, const string & obj_path)
{
	static map<string, float> memo;
	map<string, float>::const_iterator m = memo.find(obj_path);
	if (m != memo.end()) return m->second;

	double off_x = 0.0, off_z = 0.0, sign_x = 0.0;
	float  psi   = 0.0f;
	if (CentrelineOffsets(o, obj_path, off_x, off_z, sign_x) && off_z < 0.25 && off_x > 0.5)
	{
		// The axis alone is not enough - the DIRECTION along it decides which way to
		// turn, and getting that wrong leaves the aircraft side-on but facing the
		// opposite way to every other card, which is what shipped first. The top 1%
		// of vertices ARE the tail, so the sign of their offset says which end it is
		// on: tail at +X means the nose already points screen-left once the standard
		// 90 is undone; tail at -X needs the full half turn.
		//
		// This is the same rule every model goes through, not an exception for these
		// - a normal aircraft has its tail at +Z and keeps kCamPsi for exactly the
		// same reason. 293 of the 298 shipped objects come out of here with 0.
		psi = (sign_x > 0.0) ? -90.0f : 90.0f;
	}

	LOG_MSG("I/LiveryThumb yaw %s: offX=%.2f offZ=%.2f signX=%+.2f -> %+.0f\n",
			obj_path.c_str(), off_x, off_z, sign_x, psi);
	memo[obj_path] = psi;
	return psi;
}

// Defensive ceiling - not something the normal visible-range+margin math in
// WED_LiveryPane should ever bump into, just a backstop against unbounded GPU memory
// if that math ever miscounts.
static const size_t kMaxCachedThumbnails = 32;

bool WED_LiveryThumbnailCache::IsCached(const string & obj_path) const
{
	return mCache.find(obj_path) != mCache.end();
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
	// Mac links the ARB entry points straight out of the system GL framework, so
	// there is no pointer to be null - but that only means calling them cannot
	// crash, not that they WORK. An old Mac whose driver predates
	// ARB_framebuffer_object returns a GL error and an incomplete framebuffer,
	// and without this check we would rediscover that by allocating, attaching,
	// checking and deleting an FBO for every visible card, every frame, forever.
	// WED_LibraryPreviewPane.cpp:109 already does exactly this test.
	// EITHER extension is enough, and asking only for ARB was wrong. The
	// renderer below is deliberately written to satisfy the STRICTER of the two
	// specs - it passes the sized GL_DEPTH_COMPONENT24 precisely because
	// EXT_framebuffer_object rejects the unsized base format - so a driver
	// exposing only EXT can render these thumbnails perfectly well. Gating on
	// ARB alone turned "we hardened this for EXT" into "we refuse to run on
	// EXT", and the symptom would have been previews silently absent on an
	// older Mac with no hint that a one-word test was the reason.
	//
	// glGetString(GL_EXTENSIONS) is safe to read here: WED asks for no
	// NSOpenGLPFAOpenGLProfile, so Mac hands back a legacy 2.1 compatibility
	// context - see the note in TexUtils.cpp, which explains that requesting
	// 3.2 core would disable the immediate-mode drawing the whole UI is built
	// on. In a core context this call would return NULL instead.
	const char * ext_str = (const char *) glGetString(GL_EXTENSIONS);
	s_state = (ext_str && (strstr(ext_str, "GL_ARB_framebuffer_object") ||
						   strstr(ext_str, "GL_EXT_framebuffer_object"))) ? 1 : 0;
	if (!s_state)
		LOG_MSG("E/LiveryThumb no ARB/EXT_framebuffer_object - preview cards disabled.\n");
	else
		LOG_MSG("I/LiveryThumb framebuffer objects available (%s)\n",
				strstr(ext_str, "GL_ARB_framebuffer_object") ? "ARB" : "EXT only");
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
	GUI_GraphState * g, const string & obj_path)
{
	// Checked before the cache lookup is even worth doing: without FBOs nothing
	// will ever land in the cache, and the caller already handles a null return by
	// drawing the card without a picture.
	if (!FBOAvailable()) return nullptr;

	auto it = mCache.find(obj_path);
	if (it != mCache.end())
		return &it->second;

	if (mFailed.count(obj_path))
		return nullptr;		// already tried this one - see mFailed's comment

	if (mCache.size() >= kMaxCachedThumbnails)
		return nullptr;

	const XObj8 * o = nullptr;
	if (!res_mgr || !res_mgr->GetObjAbsolute(obj_path, o) || !o)
	{
		// Logged ONCE per path - mFailed short-circuits every later attempt, so
		// this stops being a per-frame log write and a per-frame file open.
		LOG_MSG("E/LiveryThumb GetObj FAILED for %s (res_mgr=%p)\n", obj_path.c_str(), (void *) res_mgr);
		LOG_FLUSH();
		mFailed.insert(obj_path);
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

	// Both the silhouette fit and the modelview below must use the SAME azimuth,
	// or the projection is fitted to a view that is never drawn and the model is
	// clipped. See ModelYawCorrection.
	float cam_psi = kCamPsi + ModelYawCorrection(o, obj_path);

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
	// GL_DEPTH_COMPONENT24, not the unsized GL_DEPTH_COMPONENT. ARB_framebuffer_object
	// accepts the base format, but the older EXT_framebuffer_object spec requires a
	// SIZED one - an older Mac or Linux driver exposing only EXT answers the unsized
	// token with GL_INVALID_ENUM, and the framebuffer then comes out incomplete for a
	// reason that looks nothing like its cause.
	glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, kThumbW, kThumbH);

	GLuint fbo = 0;
	glGenFramebuffers(1, &fbo);
	glBindFramebuffer(GL_FRAMEBUFFER, fbo);
	glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tex, 0);
	glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, depth_rb);

	bool complete = glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
	LOG_MSG("I/LiveryThumb %s: fbo_complete=%d radius=%.3f psi=%.1f tris=%d dx=%.1f dz=%.1f\n",
		obj_path.c_str(), (int) complete, real_radius, cam_psi, o->geo_tri.count(),
		o->xyz_max[0] - o->xyz_min[0], o->xyz_max[2] - o->xyz_min[2]);
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
		double cp = cos(cam_psi * kDeg2Rad), sp = sin(cam_psi * kDeg2Rad);
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
		glRotatef(cam_psi, 0, 1, 0);

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
		// Remember it, like the GetObj failure above. Without this the whole
		// gen/attach/check/delete cycle repeats every frame for every visible
		// card - on a machine where FBOs do not work at all, that is the steady
		// state, not an edge case.
		LOG_MSG("E/LiveryThumb offscreen FBO incomplete for %s\n", obj_path.c_str());
		LOG_FLUSH();
		mFailed.insert(obj_path);
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
	auto ins = mCache.emplace(obj_path, entry);
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

	// mFailed too, which this was not doing - and its own documentation says it
	// should ("switching tabs or changing the X-Plane folder gives a genuinely
	// missing file a fresh chance"). The blacklist outliving the cache meant a
	// livery that failed once stayed refused for the rest of the session, even
	// after the user pointed WED at an install that has it.
	mFailed.clear();
}
