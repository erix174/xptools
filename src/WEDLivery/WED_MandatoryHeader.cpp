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

#include "WED_MandatoryHeader.h"
#include "PlatformUtils.h"
#include "FileUtils.h"
#include <string>

using std::string;

string	WedDataFileDir(void)
{
#if APL
	// GetApplicationPath() is the BUNDLE path on Mac (.../WED.app), not the
	// executable - so this appends into the bundle rather than taking a dirname
	// off it. See the header for why beside-the-bundle is not an option.
	return GetApplicationPath() + DIR_STR "Contents" DIR_STR "Resources" DIR_STR;
#else
	// Windows and Linux both return the executable's own path, so its directory
	// is where the build drops these files.
	return FILE_get_dir_name(GetApplicationPath());
#endif
}

bool	CheckWedMandatoryHeader(std::istream & f)
{
	string line1;
	if (!std::getline(f, line1)) return false;
	if (!line1.empty() && line1.back() == '\r') line1.pop_back();

	string line2;
	if (!std::getline(f, line2)) return false;
	if (!line2.empty() && line2.back() == '\r') line2.pop_back();

	return line1 == "I" && line2 == "1 WED Aviation Database";
}
