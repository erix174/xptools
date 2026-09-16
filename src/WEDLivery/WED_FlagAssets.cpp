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

#include "WED_FlagAssets.h"
#include "BitmapUtils.h"

#include <fstream>
#include <sstream>

using std::string;
using std::vector;

// !!! MUST FIX BEFORE OPENING THE LIVERY-PICKER PR !!!
// TEMPORARY dev-machine path, NOT the final asset location, and NOT portable
// to any other machine, worktree, or CI runner. WED's real resource pipeline
// embeds shipped files into WED.rc (Windows) / the app bundle (Mac) /
// objcopy'd symbols (Linux) - see src/GUI/GUI_Resources.cpp and the
// WED_RESOURCE_FILES list in cmake/WED.cmake. Wiring these ~210 files (206
// country flags + 3 fixed layers + the CSV) into that pipeline is real
// follow-up work, deliberately deferred so this first pass is buildable and
// testable today - but it CANNOT ship like this. Both this and
// WED_FlagIndex.cpp share the same root; currently pointed at the
// xptools-livery worktree for local dev only.
static const char * kFlagAssetRoot =
	"C:\\Users\\Eric\\Desktop\\Laminar Misc Project\\WED\\xptools-livery\\src\\WEDLivery\\flags\\";

bool	WED_LoadPngTopDownARGB(const string & path, vector<uint32_t> & out, int & out_w, int & out_h)
{
	ImageInfo img;
	// target_gamma = 0 disables gamma recoloring - we need the supplied art's
	// exact pixel values, not a display-corrected reinterpretation of them.
	if (CreateBitmapFromPNG(path.c_str(), &img, false, 0.0f) != 0)
		return false;

	bool ok = (img.channels == 3 || img.channels == 4);
	if (ok)
	{
		out.assign((size_t) (img.width * img.height), 0xFF000000u);
		int row_bytes = img.width * img.channels + img.pad;
		for (long y = 0; y < img.height; ++y)
		{
			// ImageInfo is bottom-up (OpenGL convention); we want row 0 = top,
			// to match uv_nodes.csv's top-left/y-down coordinate space.
			const unsigned char * src_row = img.data + (size_t) (img.height - 1 - y) * row_bytes;
			uint32_t * dst_row = &out[(size_t) y * img.width];
			for (long x = 0; x < img.width; ++x)
			{
				const unsigned char * p = src_row + x * img.channels;
				unsigned char b = p[0], g = p[1], r = p[2];
				unsigned char a = (img.channels == 4) ? p[3] : 255;
				dst_row[x] = ((uint32_t) a << 24) | ((uint32_t) r << 16) | ((uint32_t) g << 8) | (uint32_t) b;
			}
		}
		out_w = (int) img.width;
		out_h = (int) img.height;
	}
	DestroyBitmap(&img);
	return ok;
}

static bool LoadFixedSizePng(const string & path, vector<uint32_t> & out, int expect_w, int expect_h)
{
	int w = 0, h = 0;
	if (!WED_LoadPngTopDownARGB(path, out, w, h)) return false;
	return (w == expect_w && h == expect_h);
}

WED_FlagAssets::WED_FlagAssets() :
	mLoadAttempted(false),
	mLoaded(false),
	mRows(0),
	mColumns(0)
{
}

WED_FlagAssets &	WED_FlagAssets::Get(void)
{
	static WED_FlagAssets sInstance;
	sInstance.EnsureLoaded();
	return sInstance;
}

static bool LoadNodesCsv(const string & path, vector<WED_UvNode> & out_nodes, int & out_rows, int & out_cols)
{
	std::ifstream f(path.c_str());
	if (!f) return false;

	string header;
	if (!std::getline(f, header)) return false;		// "row,column,id,u,v,x,y"

	vector<WED_UvNode> nodes;
	int max_row = -1, max_col = -1;

	string line;
	while (std::getline(f, line))
	{
		if (!line.empty() && line.back() == '\r') line.pop_back();
		if (line.empty()) continue;

		std::istringstream iss(line);
		string tok;
		WED_UvNode n;
		if (!std::getline(iss, tok, ',')) return false; n.row = atoi(tok.c_str());
		if (!std::getline(iss, tok, ',')) return false; n.column = atoi(tok.c_str());
		if (!std::getline(iss, tok, ',')) return false;		// id, unused
		if (!std::getline(iss, tok, ',')) return false; n.u = atof(tok.c_str());
		if (!std::getline(iss, tok, ',')) return false; n.v = atof(tok.c_str());
		if (!std::getline(iss, tok, ',')) return false; n.x = atof(tok.c_str());
		if (!std::getline(iss, tok, ',')) return false; n.y = atof(tok.c_str());

		nodes.push_back(n);
		if (n.row > max_row) max_row = n.row;
		if (n.column > max_col) max_col = n.column;
	}

	if (max_row < 0 || max_col < 0) return false;

	out_rows = max_row + 1;
	out_cols = max_col + 1;
	out_nodes.assign((size_t) out_rows * out_cols, WED_UvNode());
	for (size_t i = 0; i < out_nodes.size(); ++i) out_nodes[i].row = -1;	// sentinel to detect holes below

	for (size_t i = 0; i < nodes.size(); ++i)
	{
		const WED_UvNode & n = nodes[i];
		out_nodes[(size_t) n.row * out_cols + n.column] = n;
	}
	for (size_t i = 0; i < out_nodes.size(); ++i)
		if (out_nodes[i].row < 0) return false;		// missing grid node - malformed CSV

	return true;
}

bool	WED_FlagAssets::EnsureLoaded(void)
{
	if (mLoaded) return true;
	if (mLoadAttempted) return false;

	mLoadAttempted = true;

	string root(kFlagAssetRoot);
	bool ok = true;
	bool step;

	step = LoadFixedSizePng(root + "uv_fixed\\flag_pole_overlay.png", mPole, kMasterWidth, kMasterHeight);
	LOG_MSG("I/Flag pole overlay load: %s (path=%s)\n", step ? "OK" : "FAILED", (root + "uv_fixed\\flag_pole_overlay.png").c_str());
	LOG_FLUSH();
	ok = ok && step;

	step = LoadFixedSizePng(root + "uv_fixed\\flag_ink_overlay.png", mInk, kMasterWidth, kMasterHeight);
	LOG_MSG("I/Flag ink overlay load: %s\n", step ? "OK" : "FAILED");
	LOG_FLUSH();
	ok = ok && step;

	step = LoadFixedSizePng(root + "uv_fixed\\flag_mask.png", mMaskPng, kMasterWidth, kMasterHeight);
	LOG_MSG("I/Flag mask load: %s\n", step ? "OK" : "FAILED");
	LOG_FLUSH();
	ok = ok && step;

	step = LoadNodesCsv(root + "uv_fixed\\uv_nodes.csv", mNodes, mRows, mColumns);
	LOG_MSG("I/Flag uv_nodes.csv load: %s (rows=%d cols=%d nodes=%d)\n", step ? "OK" : "FAILED", mRows, mColumns, (int) mNodes.size());
	LOG_FLUSH();
	ok = ok && step;

	// Per the handoff's approved profile: 9 rows x 17 columns = 153 nodes.
	bool shape_ok = (mRows == 9 && mColumns == 17 && mNodes.size() == 153);
	if (!shape_ok) LOG_MSG("I/Flag mesh shape check FAILED: expected 9x17/153\n");
	LOG_FLUSH();
	ok = ok && shape_ok;

	LOG_MSG("I/WED_FlagAssets::EnsureLoaded overall: %s\n", ok ? "OK" : "FAILED");
	LOG_FLUSH();
	mLoaded = ok;
	if (!ok)
	{
		mPole.clear(); mInk.clear(); mMaskPng.clear(); mNodes.clear();
		mRows = mColumns = 0;
	}
	return mLoaded;
}
