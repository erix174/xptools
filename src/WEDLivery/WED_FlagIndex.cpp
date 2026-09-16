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

#include "WED_FlagIndex.h"
#include <cctype>
#include <fstream>

using std::string;

// !!! MUST FIX BEFORE OPENING THE LIVERY-PICKER PR !!!
// See WED_FlagAssets.cpp for why this is a temporary hardcoded dev-machine
// path rather than going through WED's real resource pipeline yet. Currently
// pointed at the xptools-livery worktree for local dev only - not portable.
static const char * kFlagAssetRoot =
	"C:\\Users\\Eric\\Desktop\\Laminar Misc Project\\WED\\xptools-livery\\src\\WEDLivery\\flags\\";

static bool FileExists(const string & path)
{
	std::ifstream f(path.c_str());
	return (bool) f;
}

string	WED_FlagSourcePathForCountry(const string & ioc_code)
{
	string root(kFlagAssetRoot);
	string fallback = root + "ioc_source\\_fallback_white.png";

	// Defensive: only ever build a path out of exactly 3 upper-case letters -
	// ioc_code always comes from NormalizeCountryToIoc()/IcaoPrefixIocOverride(),
	// which only ever return that shape, but this function has no way to
	// enforce that on its caller, and a filesystem path is not the place to
	// find out otherwise.
	if (ioc_code.size() != 3) return fallback;
	string code;
	for (string::const_iterator c = ioc_code.begin(); c != ioc_code.end(); ++c)
	{
		if (!std::isalpha((unsigned char) *c)) return fallback;
		code += (char) std::toupper((unsigned char) *c);
	}

	string candidate = root + "ioc_source\\" + code + ".png";
	if (FileExists(candidate)) return candidate;

	return fallback;
}
