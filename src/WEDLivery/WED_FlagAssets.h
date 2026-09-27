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

/*
	WED_FlagAssets - THEORY OF OPERATION

	Loads-once accessor for the fixed 2048x768 "flag on a pole" layers (pole,
	ink/fold overlay, cloth mask) and the 153-node / 8x16-cell UV mesh that
	every country's flag raster gets warped onto before compositing. This is
	the WED port of the assets described in the Codex "FlagUV-WED-Handoff"
	package - see that package's CLAUDE_HANDOFF.md and
	reference_app/COORDINATE_LAYER_SPEC.md for the full contract; this class
	only loads the raw layers, WED_FlagProjector.h does the actual warp/
	composite math.

	DELIBERATE DEVIATION from the handoff contract, disclosed here per its own
	"stop and explain" instruction: the reference pipeline intersects an
	SVG-rasterized mask with a PNG mask (`min(alpha(mask.svg), alpha(mask.png))`).
	WED has no SVG parser/rasterizer of its own (checked before starting this
	port - see project memory), and adding one is out of scope for a small
	flag-preview feature. This port uses the PNG mask ALONE as the effective
	cloth mask. The PNG mask is the "antialiased/detail" layer (the SVG mask
	is described as just the plain vector silhouette gate), so this should be
	visually equivalent or better at the mask boundary - it just means WED
	can't independently double-check the SVG mask's coverage against it.

	The artwork ships as a loose flags/ tree beside WED's data files and is
	found through WedDataFileDir() (see the .cpp and cmake/WED.cmake), not
	through the embedded-resource pipeline.
*/

#ifndef WED_FLAGASSETS_H
#define WED_FLAGASSETS_H

#include <string>
#include <vector>
#include <stdint.h>

// Directory holding the shipped flag artwork, WITH a trailing separator:
// 206 country PNGs under ioc_source/, plus the three fixed UV layers and
// uv_nodes.csv under uv_fixed/.
//
// These ride along next to WED as loose files, exactly like the .txt data files
// (see WedDataFileDir() in WED_MandatoryHeader.h), rather than being embedded
// through WED.rc / the Mac bundle / objcopy. That was a deliberate choice for
// ~210 small files: the loose form deploys with three lines of CMake instead of
// 210 resource entries per platform, and lets the artwork be corrected by
// replacing files rather than rebuilding WED. The trade is that they can be
// deleted by a user; every loader here already treats a missing file as a
// silent no-banner, which is the right behaviour for decoration.
//
// ONE definition, used by both WED_FlagAssets.cpp and WED_FlagIndex.cpp. It
// replaced two copies of a hardcoded absolute path into one developer's home
// directory - do not reintroduce a literal path here.
std::string	WED_FlagAssetRoot(void);

struct WED_UvNode
{
	int		row, column;
	double	u, v;
	double	x, y;
};

// Shared PNG loader: decodes `path` and repacks it as top-down (row 0 = top),
// straight-alpha 0xAARRGGBB pixels - the convention every flag-projection
// pixel buffer in this feature uses. Used both for the fixed 2048x768
// layers below and for loading an arbitrary-sized per-country flag source
// raster (see WED_FlagIndex.h) before projecting it.
bool	WED_LoadPngTopDownARGB(const std::string & path, std::vector<uint32_t> & out, int & out_w, int & out_h);

class	WED_FlagAssets {
public:

	// Process-wide lazily-loaded singleton - these are small, immutable,
	// read-only assets shared by every ramp start / every livery pane.
	static WED_FlagAssets &	Get(void);

	bool	IsLoaded(void) const { return mLoaded; }
	bool	LoadFailed(void) const { return mLoadAttempted && !mLoaded; }

	static int	Width(void)  { return kMasterWidth; }
	static int	Height(void) { return kMasterHeight; }

	// Each is Width()*Height() pixels, top-down row order (row 0 = top),
	// packed as 0xAARRGGBB (matches the packing WED_FlagProjector.cpp's
	// ported Alpha()/Red()/Green()/Blue() helpers expect).
	const std::vector<uint32_t> &	Pole(void)    const { return mPole; }
	const std::vector<uint32_t> &	Ink(void)     const { return mInk; }
	const std::vector<uint32_t> &	MaskPng(void) const { return mMaskPng; }	// see deviation note above

	const std::vector<WED_UvNode> &	Nodes(void) const { return mNodes; }	// flattened row-major, Rows()*Columns() entries
	int		Rows(void)    const { return mRows; }
	int		Columns(void) const { return mColumns; }

private:

	WED_FlagAssets();

	bool	EnsureLoaded(void);

	bool	mLoadAttempted;
	bool	mLoaded;

	std::vector<uint32_t>		mPole, mInk, mMaskPng;
	std::vector<WED_UvNode>	mNodes;
	int							mRows, mColumns;

	static const int kMasterWidth  = 2048;
	static const int kMasterHeight = 768;

};

#endif /* WED_FLAGASSETS_H */
