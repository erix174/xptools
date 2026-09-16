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
	WED_FlagIndex - THEORY OF OPERATION

	Resolves an IOC-style 3-letter country code (as produced by
	NormalizeCountryToIoc() / IcaoPrefixIocOverride(), see
	WED_IocCountryCodes.h) to the on-disk path of its source flag raster
	(sourced from the IOC's own flag CDN - see project notes for the
	download process; every code that mechanism produces has a real flag
	file EXCEPT "ATA", Antarctica, which isn't an IOC member).

	Any code with no real flag file - currently just "ATA", and this is
	deliberately the SAME path taken for any future/unexpected code this
	project hasn't seen - falls back to a plain white placeholder. This is
	intentional, not a bug: an unrecognized/unmapped country gets a neutral
	blank rather than a guess, consistent with this project's "no political
	stance beyond IOC's own" rule (see WED_IocCountryCodes.h).
*/

#ifndef WED_FLAGINDEX_H
#define WED_FLAGINDEX_H

#include <string>

// Never empty - always returns a usable path, falling back to the white
// placeholder if `ioc_code` doesn't have a real flag on disk.
std::string	WED_FlagSourcePathForCountry(const std::string & ioc_code);

#endif /* WED_FLAGINDEX_H */
