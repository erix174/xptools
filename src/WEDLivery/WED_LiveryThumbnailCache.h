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

#ifndef WED_LIVERYTHUMBNAILCACHE_H
#define WED_LIVERYTHUMBNAILCACHE_H

#include <condition_variable>
#include <deque>
#include <mutex>
#include <thread>
#include <vector>
#include <map>
#include <set>
#include <string>
#include <vector>
#include "BitmapUtils.h"		// ImageInfo

class WED_ResourceMgr;
struct XObj8;
class ITexMgr;
class GUI_GraphState;

// One off-screen-rendered 16:9 snapshot of a library .obj resource. tex == 0 means
// "failed to render" - never draw it.
struct WED_LiveryThumbnail {
	unsigned int	tex = 0;
	int				w = 0, h = 0;
	unsigned long	last_used = 0;		// for least-recently-seen eviction
};

// Renders and caches small off-screen 3D snapshots of library .obj resources, for use
// as a lightweight "livery preview" card image - see WED_LiveryPane's preview cards.
//
// ---------------------------------------------------------------------------------
// DESIGN RULE - the cards are STATIC IMAGES, never live 3D views.
// A card draws one cached texture and nothing else: no live geometry, no per-frame
// re-render, no interaction, no visible 3D viewport anywhere in that panel. All
// rendering happens here, off-screen and invisible, exactly once per resource. Do not
// "simplify" this into drawing the model directly on the card - a screenful of live
// 3D previews is precisely the GPU cost this class exists to avoid.
// ---------------------------------------------------------------------------------
//
// Each snapshot is rendered straight into a GL texture via a throwaway FBO (no
// glReadPixels/CPU round-trip - the texture itself IS the cached result), on the SAME
// shared GL context/thread WED already draws everything else with. Nothing is ever
// blitted to the default framebuffer, so no 3D viewport is ever visibly shown - but
// this still runs synchronously on the main thread during a Draw() call (WED has no
// background-rendering thread/second GL context to build on).
//
// This class does no automatic eviction on its own - callers own that policy. Call
// EvictNotVisible() every frame with whatever's actually on screen right now, and
// DiscardAll() whenever the cards shouldn't be paying for GPU memory at all (tab
// hidden, pane destroyed).
class WED_LiveryThumbnailCache {
public:

	WED_LiveryThumbnailCache() : mFBO(0), mDepthRB(0), mFBOChecked(false), mFBOUsable(false) {}
	~WED_LiveryThumbnailCache();

	// True if obj_path already has a cached texture - i.e. calling GetThumbnail() for
	// it right now is a cheap map lookup, NOT a fresh off-screen render. Callers use
	// this to budget how many actual renders happen in a single frame (see
	// WED_LiveryPane::Draw()'s card strip block) - a burst of newly-visible,
	// never-rendered cards (e.g. a big scrollbar jump) is throttled to a handful of
	// new renders per frame rather than rendering all of them in one frame, which is
	// what caused the visible stutter/thrash on fast repeated scrolling.
	bool	IsCached(const std::string & obj_path) const;

	// Returns the cached thumbnail for obj_path, rendering it first if this is the
	// first time it's been asked for. Returns NULL if the resource couldn't be loaded
	// as an .obj (caller should just skip drawing that card's image this frame), or if
	// the cache is already at its hard capacity (see the .cpp) and this would be a new
	// entry - a defensive ceiling, not something normal use should ever actually hit.
	const WED_LiveryThumbnail *	GetThumbnail(WED_ResourceMgr * res_mgr, ITexMgr * tex_mgr,
									GUI_GraphState * g, const std::string & obj_path);

	// Frees the GL texture for every cached entry whose vpath is NOT in
	// currently_visible - call once per Draw() with the vpaths of cards actually in
	// the on-screen scroll range right now.
	void	EvictNotVisible(const std::set<std::string> & currently_visible);

	// True while an object or texture is still being read on a worker thread -
	// the caller keeps drawing until it lands.
	bool	HasPending(void);

	// What to read next, most wanted first - the cards on screen, then the next
	// face of the one being cycled, then the ones about to scroll in. Called once
	// per frame; it REPLACES the queue, so a card scrolled past before a worker
	// reached it is simply dropped and what is on screen now goes first.
	void	Want(const std::vector<std::string> & paths_in_priority_order);

	// Cached, or known not to load: nothing more will happen for this path.
	bool	IsSettled(const std::string & obj_path) const { return mCache.count(obj_path) || mFailed.count(obj_path); }
	bool	IsFailed(const std::string & obj_path) const  { return mFailed.count(obj_path) != 0; }
	// Read by a worker and waiting to be uploaded: the next GetThumbnail for it
	// renders. The one call that costs a frame its render budget.
	bool	IsReady(const std::string & obj_path);
	// False when this GL context cannot render off-screen at all.
	static bool	RenderingAvailable(void);

	// Frees every cached GL texture.
	void	DiscardAll();

private:

	std::map<std::string, WED_LiveryThumbnail>	mCache;

	// Paths that already failed to load or render. Without this a card whose
	// object is missing re-opens the file on EVERY frame - and each attempt also
	// costs a log write and flush. On Linux it is worse still: a failed open runs
	// FILE_case_correct(), which does an opendir plus a linear readdir for every
	// component of the path, so one broken livery turns into a directory walk per
	// frame, forever.
	//
	// Purely a performance memo, so it is dropped by DiscardAll() along with
	// everything else - switching tabs or changing the X-Plane folder gives a
	// genuinely missing file a fresh chance rather than blacklisting it for the
	// session.
	std::set<std::string>						mFailed;

	// THE SLOW HALF RUNS ON A WORKER. A 737-800 is a 7 MB .obj and a 4096-pixel
	// PNG per livery; parsing and decoding them on the UI thread is what made the
	// list stall as each new aircraft scrolled in. A worker reads the object and
	// its texture and shrinks the texture to thumbnail size; the UI thread only
	// uploads that and draws once. Neither touches WED_ResourceMgr or the texture
	// manager, which are not thread-safe - and which kept every one of those
	// objects and full-size textures loaded for the rest of the session.
public:
	struct Prepared {
		XObj8 *		obj = nullptr;
		ImageInfo	img = { nullptr, 0, 0, 0, 0 };
		bool		has_img = false;
		std::vector<char>	dds;	// a DDS is handed to the GPU as the file is, like WED_TexMgr does
		long		ms = 0;
	};
private:
	// The worker pool, started on first use. Workers take mQueue's front, read
	// it with no lock held, and leave the result in mDone for the UI thread.
	void										StartWorkers(void);
	void										WorkerLoop(void);
	std::mutex									mMutex;
	std::condition_variable						mWake;
	std::deque<std::string>						mQueue;
	std::set<std::string>						mInFlight;
	std::map<std::string, Prepared>				mDone;
	std::vector<std::thread>					mWorkers;
	bool										mStop = false;
	unsigned long								mTick = 0;

	// THE OFFSCREEN TARGET IS BUILT ONCE AND REUSED. It used to be created and
	// destroyed around every single thumbnail, which is the whole reason this pane
	// stuttered where the library preview does not: deleting a framebuffer and a
	// renderbuffer the GPU may still be writing forces the driver to block until the
	// GPU drains, so every thumbnail cost a full CPU-GPU sync on top of its own
	// work. The library preview never pays that - it draws straight to the back
	// buffer and allocates nothing per frame.
	//
	// Only the COLOUR texture is per-thumbnail, because it is the cached result.
	// Size and format never vary, so one depth buffer serves every render and
	// completeness only has to be checked when the pair is first built.
	unsigned int								mFBO;			// 0 until built
	unsigned int								mDepthRB;
	bool										mFBOChecked;	// completeness already verified
	bool										mFBOUsable;
};

#endif /* WED_LIVERYTHUMBNAILCACHE_H */
