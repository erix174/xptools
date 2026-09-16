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

#include "WED_FlagProjector.h"
#include "WED_FlagAssets.h"

#include <algorithm>
#include <cmath>

using std::vector;

namespace {

	// Approved product defaults - see WED_FlagProjector.h. Not exposed as
	// parameters; this is a fixed display feature, not the tunable tester.
	const double kUCurve        = 0.75;
	const double kVCurve        = 0.75;
	const double kInkOpacity    = 0.65;
	const int    kMaskThreshold = 16;

	inline int Alpha(uint32_t p) { return (int) ((p >> 24) & 0xFF); }
	inline int Red(uint32_t p)   { return (int) ((p >> 16) & 0xFF); }
	inline int Green(uint32_t p) { return (int) ((p >> 8)  & 0xFF); }
	inline int Blue(uint32_t p)  { return (int) (p & 0xFF); }

	inline int ClampByte(int v) { return v < 0 ? 0 : (v > 255 ? 255 : v); }
	inline double Clamp01(double v) { return v < 0.0 ? 0.0 : (v > 1.0 ? 1.0 : v); }

	uint32_t BilinearSample(const vector<uint32_t> & pixels, int width, int height, double x, double y)
	{
		// Parenthesized as (std::max)/(std::min) throughout this file: XDefs.h
		// (force-included into every WED translation unit) pulls in
		// <windows.h>, whose function-like min/max MACROS would otherwise
		// swallow these calls - the extra parens stop macro expansion
		// without needing a project-wide NOMINMAX change.
		x = (std::max)(0.0, (std::min)((double) width  - 1.0, x));
		y = (std::max)(0.0, (std::min)((double) height - 1.0, y));
		int x0 = (int) x, y0 = (int) y;
		int x1 = (std::min)(x0 + 1, width  - 1);
		int y1 = (std::min)(y0 + 1, height - 1);
		double fx = x - x0, fy = y - y0;
		double w00 = (1.0 - fx) * (1.0 - fy);
		double w10 = fx * (1.0 - fy);
		double w01 = (1.0 - fx) * fy;
		double w11 = fx * fy;

		uint32_t p00 = pixels[(size_t) y0 * width + x0];
		uint32_t p10 = pixels[(size_t) y0 * width + x1];
		uint32_t p01 = pixels[(size_t) y1 * width + x0];
		uint32_t p11 = pixels[(size_t) y1 * width + x1];

		int a = ClampByte((int) (Alpha(p00) * w00 + Alpha(p10) * w10 + Alpha(p01) * w01 + Alpha(p11) * w11 + 0.5));
		int r = ClampByte((int) (Red(p00)   * w00 + Red(p10)   * w10 + Red(p01)   * w01 + Red(p11)   * w11 + 0.5));
		int g = ClampByte((int) (Green(p00) * w00 + Green(p10) * w10 + Green(p01) * w01 + Green(p11) * w11 + 0.5));
		int b = ClampByte((int) (Blue(p00)  * w00 + Blue(p10)  * w10 + Blue(p01)  * w01 + Blue(p11)  * w11 + 0.5));
		return ((uint32_t) a << 24) | ((uint32_t) r << 16) | ((uint32_t) g << 8) | (uint32_t) b;
	}

	// Straight-alpha "source over destination" - unlike the reference
	// tester (which always flattens onto opaque white for its own preview/
	// export), WED keeps the composite's background genuinely transparent,
	// so the pane's own dark background shows through instead of a white
	// box. The handoff's own contract explicitly allows this ("acceptable
	// to keep a transparent intermediate texture inside WED"); WED has no
	// separate export path yet, so there's no white-flattened output to
	// preserve elsewhere.
	uint32_t BlendOver(uint32_t destination, uint32_t source, double opacity)
	{
		int src_a = ClampByte((int) (Alpha(source) * opacity + 0.5));
		if (src_a <= 0) return destination;
		int dst_a = Alpha(destination);
		int out_a = src_a + dst_a * (255 - src_a) / 255;
		if (out_a <= 0) return 0;

		int dst_factor = dst_a * (255 - src_a) / 255;
		int r = (Red(source)   * src_a + Red(destination)   * dst_factor) / out_a;
		int g = (Green(source) * src_a + Green(destination) * dst_factor) / out_a;
		int b = (Blue(source)  * src_a + Blue(destination)  * dst_factor) / out_a;
		return ((uint32_t) out_a << 24) | ((uint32_t) r << 16) | ((uint32_t) g << 8) | (uint32_t) b;
	}

	// Warps one target-space triangle (nodeA/B/C, in master-canvas pixels)
	// back to source-space via inverse barycentric mapping, bilinearly
	// sampling `source` at each covered target pixel. Returns false (and
	// writes nothing) for a degenerate/inverted triangle - ProjectFlag()
	// below treats that as a hard failure since the shipped mesh must never
	// produce one (GOLDEN_TEST.json's inverted_triangles: 0 requirement).
	bool WarpTriangle(
		vector<uint32_t> & output, vector<unsigned char> & coverage,
		int out_w, int out_h,
		const vector<uint32_t> & source, int source_w, int source_h,
		const WED_UvNode & A, const WED_UvNode & B, const WED_UvNode & C)
	{
		double ax = A.x, ay = A.y, bx = B.x, by = B.y, cx = C.x, cy = C.y;

		double denominator = (by - cy) * (ax - cx) + (cx - bx) * (ay - cy);
		if (denominator <= 0.000001) return false;

		double au = std::pow(Clamp01(A.u), kUCurve) * (source_w - 1);
		double av = std::pow(Clamp01(A.v), kVCurve) * (source_h - 1);
		double bu = std::pow(Clamp01(B.u), kUCurve) * (source_w - 1);
		double bv = std::pow(Clamp01(B.v), kVCurve) * (source_h - 1);
		double cu = std::pow(Clamp01(C.u), kUCurve) * (source_w - 1);
		double cv = std::pow(Clamp01(C.v), kVCurve) * (source_h - 1);

		int xMin = (std::max)(0,         (int) std::floor((std::min)(ax, (std::min)(bx, cx))));
		int xMax = (std::min)(out_w - 1, (int) std::ceil ((std::max)(ax, (std::max)(bx, cx))));
		int yMin = (std::max)(0,         (int) std::floor((std::min)(ay, (std::min)(by, cy))));
		int yMax = (std::min)(out_h - 1, (int) std::ceil ((std::max)(ay, (std::max)(by, cy))));

		const double tolerance = -0.000000001;
		for (int y = yMin; y <= yMax; ++y)
		{
			double py = y + 0.5;
			int row_offset = y * out_w;
			for (int x = xMin; x <= xMax; ++x)
			{
				double px = x + 0.5;
				double wA = ((by - cy) * (px - cx) + (cx - bx) * (py - cy)) / denominator;
				double wB = ((cy - ay) * (px - cx) + (ax - cx) * (py - cy)) / denominator;
				double wC = 1.0 - wA - wB;
				if (wA < tolerance || wB < tolerance || wC < tolerance) continue;

				double source_x = wA * au + wB * bu + wC * cu;
				double source_y = wA * av + wB * bv + wC * cv;
				int index = row_offset + x;
				output[index] = BilinearSample(source, source_w, source_h, source_x, source_y);
				coverage[index] = 255;
			}
		}
		return true;
	}

} // anonymous namespace

bool	WED_ProjectFlag(
			const vector<uint32_t> &	source_argb,
			int							source_w,
			int							source_h,
			vector<uint32_t> &			out_argb)
{
	WED_FlagAssets & assets = WED_FlagAssets::Get();
	if (!assets.IsLoaded()) return false;
	if (source_argb.empty() || source_w <= 0 || source_h <= 0) return false;

	int w = assets.Width();
	int h = assets.Height();
	int pixel_count = w * h;

	vector<uint32_t>     projected(pixel_count, 0);
	vector<unsigned char> coverage(pixel_count, 0);

	const vector<WED_UvNode> & nodes = assets.Nodes();
	int rows = assets.Rows(), cols = assets.Columns();
	int inverted = 0;

	for (int row = 0; row < rows - 1; ++row)
	{
		for (int col = 0; col < cols - 1; ++col)
		{
			const WED_UvNode & nw = nodes[(size_t) row * cols + col];
			const WED_UvNode & ne = nodes[(size_t) row * cols + col + 1];
			const WED_UvNode & sw = nodes[(size_t) (row + 1) * cols + col];
			const WED_UvNode & se = nodes[(size_t) (row + 1) * cols + col + 1];

			if (((row + col) & 1) == 0)
			{
				if (!WarpTriangle(projected, coverage, w, h, source_argb, source_w, source_h, nw, ne, se)) ++inverted;
				if (!WarpTriangle(projected, coverage, w, h, source_argb, source_w, source_h, nw, se, sw)) ++inverted;
			}
			else
			{
				if (!WarpTriangle(projected, coverage, w, h, source_argb, source_w, source_h, nw, ne, sw)) ++inverted;
				if (!WarpTriangle(projected, coverage, w, h, source_argb, source_w, source_h, ne, se, sw)) ++inverted;
			}
		}
	}
	if (inverted > 0) return false;

	const vector<uint32_t> & pole     = assets.Pole();
	const vector<uint32_t> & ink      = assets.Ink();
	const vector<uint32_t> & mask_png = assets.MaskPng();

	out_argb.assign((size_t) pixel_count, 0u);		// transparent - see BlendOver()'s comment
	int gaps = 0;

	for (int i = 0; i < pixel_count; ++i)
	{
		uint32_t destination = 0u;					// transparent black
		destination = BlendOver(destination, pole[i], 1.0);

		// Effective mask = PNG mask alone (no SVG rasterizer available - see
		// the deviation note in WED_FlagAssets.h), then thresholded.
		int mask_alpha = Alpha(mask_png[i]);
		if (mask_alpha < kMaskThreshold) mask_alpha = 0;
		if (mask_alpha > 0 && coverage[i] == 0) ++gaps;

		uint32_t projected_pixel = projected[i];
		int projected_alpha = Alpha(projected_pixel) * mask_alpha / 255;
		if (projected_alpha > 0)
		{
			projected_pixel = (projected_pixel & 0x00FFFFFFu) | ((uint32_t) projected_alpha << 24);
			destination = BlendOver(destination, projected_pixel, 1.0);
		}

		destination = BlendOver(destination, ink[i], kInkOpacity);
		out_argb[i] = destination;
	}

	if (gaps > 0) return false;

	return true;
}
