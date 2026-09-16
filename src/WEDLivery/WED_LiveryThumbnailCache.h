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

#include <map>
#include <set>
#include <string>

class WED_ResourceMgr;
class ITexMgr;
class GUI_GraphState;

// One off-screen-rendered 16:9 snapshot of a library .obj resource. tex == 0 means
// "failed to render" - never draw it.
struct WED_LiveryThumbnail {
	unsigned int	tex = 0;
	int				w = 0, h = 0;
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

	WED_LiveryThumbnailCache() {}
	~WED_LiveryThumbnailCache() { DiscardAll(); }

	// True if obj_vpath already has a cached texture - i.e. calling GetThumbnail() for
	// it right now is a cheap map lookup, NOT a fresh off-screen render. Callers use
	// this to budget how many actual renders happen in a single frame (see
	// WED_LiveryPane::Draw()'s card strip block) - a burst of newly-visible,
	// never-rendered cards (e.g. a big scrollbar jump) is throttled to a handful of
	// new renders per frame rather than rendering all of them in one frame, which is
	// what caused the visible stutter/thrash on fast repeated scrolling.
	bool	IsCached(const std::string & obj_vpath) const;

	// Returns the cached thumbnail for obj_vpath, rendering it first if this is the
	// first time it's been asked for. Returns NULL if the resource couldn't be loaded
	// as an .obj (caller should just skip drawing that card's image this frame), or if
	// the cache is already at its hard capacity (see the .cpp) and this would be a new
	// entry - a defensive ceiling, not something normal use should ever actually hit.
	const WED_LiveryThumbnail *	GetThumbnail(WED_ResourceMgr * res_mgr, ITexMgr * tex_mgr,
									GUI_GraphState * g, const std::string & obj_vpath);

	// Frees the GL texture for every cached entry whose vpath is NOT in
	// currently_visible - call once per Draw() with the vpaths of cards actually in
	// the on-screen scroll range right now.
	void	EvictNotVisible(const std::set<std::string> & currently_visible);

	// Frees every cached GL texture.
	void	DiscardAll();

private:

	std::map<std::string, WED_LiveryThumbnail>	mCache;
};

#endif /* WED_LIVERYTHUMBNAILCACHE_H */
