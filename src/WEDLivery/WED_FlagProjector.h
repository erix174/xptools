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
	WED_FlagProjector - THEORY OF OPERATION

	C++ port of FlagUV-WED-Handoff/reference_app/source/ProjectionEngine.cs's
	WarpTriangle() / BilinearSample() / BlendOverOpaque() / Render(). Ported
	behavior, not WinForms types, per that package's own PORTING_MAP.md.

	Fixed to the package's APPROVED PRODUCT DEFAULTS - this is a display
	feature (show the selected ramp's country flag), not the tunable
	reference tester, so none of these are user-adjustable here:
		quality = Fast 1x (render directly at 2048x768, no supersampling)
		ink/shader opacity = 65%
		U curve = V curve = 0.75
		mask threshold = 16
		UV grid = off
	See DEFAULT_PROFILE.json in the handoff package for the source of these
	numbers, and GOLDEN_TEST.json / test_vectors/ for the approved reference
	output this must visually match.

	Pixel format throughout: 0xAARRGGBB, top-down row order (row 0 = top),
	straight (non-premultiplied) alpha - same convention WED_FlagAssets.h's
	loaded layers use.
*/

#ifndef WED_FLAGPROJECTOR_H
#define WED_FLAGPROJECTOR_H

#include <vector>
#include <stdint.h>

// Renders source_argb (an already-decoded, top-down ARGB flag raster, any
// size) through the fixed UV mesh plus pole/mask/ink layers from
// WED_FlagAssets, composited with a genuinely transparent background (straight
// alpha, not flattened onto opaque white - the handoff's own reference tester
// flattens to white for ITS preview/export, but explicitly allows a WED-side
// consumer to keep transparency instead), into out_argb (resized to
// WED_FlagAssets::Width() x Height() on success).
//
// Returns false if WED_FlagAssets failed to load, the mesh produced an
// inverted triangle, or the effective mask has an uncovered pixel - all three
// would indicate a problem with the fixed assets themselves (not the input
// flag), since the shipped uv_nodes.csv is supposed to guarantee zero of
// either per GOLDEN_TEST.json's structural_expectations.
bool	WED_ProjectFlag(
			const std::vector<uint32_t> &	source_argb,
			int								source_w,
			int								source_h,
			std::vector<uint32_t> &		out_argb);

#endif /* WED_FLAGPROJECTOR_H */
