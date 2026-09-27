/*
 * Copyright (c) 2007, Laminar Research.
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

#include "STLUtils.h"
#include <cctype>
#include <iostream>
#include <string>

bool ci_char_traits::eq(char c1, char c2)
{
	return std::toupper((unsigned char) c1) == std::toupper((unsigned char) c2);
}

bool ci_char_traits::lt(char c1, char c2)
{
	return std::toupper((unsigned char) c1) <  std::toupper((unsigned char) c2);
}

// toupper() and the rest of <cctype> take an INT that must be either EOF or a
// value representable as unsigned char. A plain `char` is signed on every
// platform WED builds for, so any byte from 0x80 up arrives negative and trips
// the library's own range assert - which aborts a debug build outright.
//
// That is not a theoretical concern here. These traits back ci_string, and
// ci_string backs the hierarchy pane's search filter
// (WED_PropertyTable.cpp:1186). Type one non-ASCII character into that box -
// any CJK text, any accented latin letter - and the UTF-8 bytes go straight
// into compare() and find(). WED died on the first keystroke.
//
// Casting through unsigned char also makes the comparison byte-wise for
// everything outside ASCII, which is the correct behaviour for this class: it
// promises ASCII case folding, not Unicode collation, and two identical UTF-8
// sequences still compare equal byte for byte.
static inline int ci_upper(char c)
{
	return std::toupper((unsigned char) c);
}

int ci_char_traits::compare(const char* s1, const char* s2, size_t n)
{
	while (n-- != 0) {
		if (ci_upper(*s1) < ci_upper(*s2)) return -1;
		if (ci_upper(*s1) > ci_upper(*s2)) return 1;
		++s1; ++s2;
	}
	return 0;
}

const char* ci_char_traits::find(const char* s, size_t n, char a)
{
	const int ua(ci_upper(a));
	while (n-- != 0)
	{
		if (ci_upper(*s) == ua)
			return s;
		s++;
	}
	return NULL;
}

std::ostream& operator<<(std::ostream& os, const ci_string& str)
{
	return os.write(str.data(), str.size());
}