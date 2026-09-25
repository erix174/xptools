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

#ifndef WED_LIVERYMODERATION_H
#define WED_LIVERYMODERATION_H

// MODERATION: checking a submitted airport's stands one at a time without
// clicking through each. Built to work in normal mode first; everything that
// should only happen for a moderator asks WED_ModerationEnabled(), the one
// switch the preference checkbox will drive.

#include <string>
#include <vector>

class IResolver;
class WED_Airport;
class WED_RampPosition;

// The switch. Today always on, so the feature can be used and tested in
// normal mode; hook it to the Moderation Mode preference when that exists.
bool	WED_ModerationEnabled(void);

// What a moderator should look at on one stand.
struct WED_ModerationNote {
	enum Kind {
		note_AutoFilled,		// the auto-fill watermark: no person has checked this stand
		note_NoStaticAircraft,	// operation type None - nothing parks here, by the author's choice
		note_UnknownOperator,	// a listed code that is not an operator in livery_index.txt
		note_NotServedHere		// a known operator the airport database does not list for this airport
	};
	Kind			kind;
	std::string		code;		// upper case; empty for the stand-level notes
	std::string		text;		// one line, for a dialog or a list
	std::string		search_url;	// set for the two operator notes - a web search to settle it
};

void	WED_ModerationNotes(WED_RampPosition * ramp, WED_Airport * apt, std::vector<WED_ModerationNote> & out);

// "Does <operator> fly to <airport>" as a Google search URL.
std::string	WED_ModerationSearchURL(const std::string & operator_name, const std::string & airport_name,
									const std::string & icao);

// Selects the next (dir > 0) or previous ramp start of the current airport,
// wrapping, in hierarchy order. Starts from the selected ramp start, or the
// first one. Returns the ramp now selected, or NULL when the airport has none.
WED_RampPosition *	WED_ModerationStep(IResolver * resolver, int dir);

// The pop-out: if the stand lists operators that need checking, say which
// and offer to open a web search for each. Nothing is shown otherwise.
void	WED_ModerationPrompt(WED_RampPosition * ramp, WED_Airport * apt);

#endif /* WED_LIVERYMODERATION_H */
