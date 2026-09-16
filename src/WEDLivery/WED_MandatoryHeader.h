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
	WED_MandatoryHeader - THEORY OF OPERATION

	Every WED_Livery*.txt loose data file (WED_AirportDatabase.txt,
	WED_AirlineDirectory.txt, and any future one - WED_AircraftSizeReference.txt,
	WED_StaticLiveryDatabase.txt, etc.) starts
	with this exact, identical two-line stamp:

		I
		1 WED Aviation Database

	This is deliberately NOT a per-file descriptive title - it's the SAME two
	lines in every one of these files, project-wide. Its only job is proving
	"this is a real WED data file, not something else that happens to have the
	right extension" before any of the format-specific parsing below it even
	starts. A file missing this stamp, or with so much as one character of it
	altered, is treated as untrusted/wrong-version and its EnsureLoaded()
	fails outright - it is NOT parsed defensively line-by-line the way this
	project's data ROWS are (see e.g. WED_AirportDatabase.cpp's own comment on
	that). Getting the header itself wrong is not the kind of error a
	best-effort per-row parse should quietly paper over.

	Every loader in this family calls CheckWedMandatoryHeader() first, right
	after opening the file, before reading anything else - one shared
	implementation so the exact two lines being checked for can never drift
	between loaders.
*/

#ifndef WED_MANDATORYHEADER_H
#define WED_MANDATORYHEADER_H

#include <istream>
#include <string>

// Directory holding this family's loose .txt data files, WITH a trailing
// separator - prepend a bare filename to it.
//
// This is not the same directory on every platform, which is why it exists as a
// function instead of being open-coded at each call site the way it used to be:
//
//   Windows / Linux : beside the executable, which is what
//                     FILE_get_dir_name(GetApplicationPath()) already gives.
//   macOS           : INSIDE the bundle, at WED.app/Contents/Resources/. On Mac
//                     GetApplicationPath() returns the BUNDLE path, so
//                     FILE_get_dir_name() of it lands next to WED.app rather
//                     than in it - and data files sitting beside a .app do not
//                     survive a user dragging the bundle somewhere. Putting them
//                     in Contents/Resources is the only placement that travels
//                     with the application.
//
// Getting this wrong is silent: the loaders simply report LoadFailed() and the
// Liveries tab comes up empty, which is exactly how these files went undeployed
// on Mac and Linux for as long as they did.
std::string	WedDataFileDir(void);

// Why a loader in this family failed, so the message shown to the user can be
// true. "Not found" for a file that is sitting right there, merely with a
// damaged header, sends people looking for the wrong thing.
enum WedDataFileError {
	wed_data_ok = 0,
	wed_data_no_file,		// could not be opened - missing, or no permission
	wed_data_bad_header,	// opened, but the mandatory two-line stamp is wrong
	wed_data_no_rows		// opened and stamped, but not one usable row parsed
};

// One sentence describing `err`, suitable for putting in front of a user. The
// caller supplies the path; this supplies the diagnosis.
const char *	WedDataFileErrorText(WedDataFileError err);

// Reads and consumes exactly the first two lines of `f` (CRLF-tolerant).
// Returns true only if they are exactly "I" then "1 WED Aviation Database" -
// false for anything else, including a file with fewer than two lines. On a
// false return, the caller's own EnsureLoaded() should fail the same way it
// already does for a file that couldn't be opened at all.
bool	CheckWedMandatoryHeader(std::istream & f);

#endif /* WED_MANDATORYHEADER_H */
