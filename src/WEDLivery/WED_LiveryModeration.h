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

#include <set>
#include <string>
#include <vector>

class IResolver;
class WED_Airport;
class WED_RampPosition;

// The switch: the Moderator Mode preference (gModeratorMode), read live. Every
// caller asks it at draw, click or command time, so ticking or clearing the box
// takes effect at once in open documents - no restart, no reopen.
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
std::string	WED_ModerationSearchURL(const std::string & operator_name, const std::string & city,
									const std::string & icao);

// Selects the next (dir > 0) or previous ramp start of the current airport,
// wrapping, in hierarchy order. Starts from the selected ramp start, or the
// first one. Returns the ramp now selected, or NULL when the airport has none.
// issues_only: skip the stands WED_ModerationHasIssue() passes.
WED_RampPosition *	WED_ModerationStep(IResolver * resolver, int dir, bool issues_only = false);

// Every ramp start of the airport, in hierarchy order.
void	WED_ModerationRamps(WED_Airport * apt, std::vector<WED_RampPosition *> & out);

// The pop-out: if the stand lists operators that need checking, say which
// and offer to open a web search for each. Nothing is shown otherwise.
void	WED_ModerationPrompt(WED_RampPosition * ramp, WED_Airport * apt);

// ---- One stand, everything a moderator reads off it (the map callout) ----
//
// Verification depends on the stand's operation type and on where its list came
// from:
//   Airline / Cargo, auto-filled   the list came from the airport database: assumed right
//   Airline / Cargo, by hand       each code checked against the database's list for this
//                                  airport; a code it does not list gets a search. Listing
//                                  FEWER than the database is fine.
//   Airline / Cargo, no database   nothing to check against - said so
//   Military                       each operator's country against the airport's
//   GA / None                      no check; GA shows its spawn weights
struct WED_ModerationCode {
	enum Verdict {
		v_Plain,		// nothing to check (GA, no database)
		v_Assumed,		// auto-filled from the database
		v_Ok,			// the database lists it here / same country
		v_Check,		// not listed here, or not an operator at all: search it
		v_Foreign		// a military operator of another country
	};
	std::string		code;			// upper case
	std::string		country;		// IOC, "" if unknown
	int				verdict;
	std::string		search_url;		// for v_Check
};

struct WED_ModerationEntry {
	enum Verify { verify_None, verify_Assumed, verify_Database, verify_NoData, verify_Country };

	std::string		icao;			// the airport's icao_code metadata, else its ID
	std::string		country;		// the airport's IOC country, "" if unknown
	std::string		ramp_name;
	int				op_type;		// ramp_operation_*
	std::string		op_label;		// "Passenger", "Military/Gov" - the Liveries tab's words
	std::string		equipment;		// "Heavy Jets, Jets"
	std::string		ramp_type;		// "Gate"
	bool			updated;		// carries 1313 weights; false = legacy size letter only
	bool			auto_filled;	// the watermark
	char			size_letter;	// the 1301 letter, 'A'..'F'
	int				weights[6];		// valid when updated
	std::vector<WED_ModerationCode>	codes;
	int				verify;
	int				n_to_check;		// v_Check + v_Foreign
	// The validator's own finding for this stand (WED_LiveryParksNothing), word
	// for word: the overview and the report show exactly what Validate lists.
	bool			parks_nothing;
	std::string		parks_nothing_msg;
	// Equal for two stands that would park the same thing: operation type, the
	// airline set, the size letter or weights, and equipment. The callout
	// colours by it, and reviewing one stand reviews every stand that shares it.
	std::string		signature;
};

void	WED_ModerationDescribe(WED_RampPosition * ramp, WED_Airport * apt, WED_ModerationEntry & out);

// Needs a moderator: an operator to check (not listed here, or from another
// country), an airline stand with no airport data to check it against, or a
// stand the validator says parks nothing.
bool	WED_ModerationHasIssue(const WED_ModerationEntry & e);

// Before an export or a Gateway submission, in Moderator Mode: hidden items are
// not exported, and the moderator's hierarchy shortcuts hide whole folders. If
// anything under root is hidden, ask: show everything (one undo step) and go
// on, go on as it is, or stop. Returns false to stop. Outside Moderator Mode,
// or with nothing hidden, returns true without asking.
class WED_Thing;
bool	WED_ModerationConfirmHidden(WED_Thing * root);

// THE livery check behind Validate's warn_ramp_livery_parks_nothing (R14): true
// when nothing in the livery index can park at this stand, with the message
// the validator lists. The Moderation View calls the same function, so the two
// never disagree. It reads only the stand, the airport and the shipped data
// (livery_index.txt, WED_AirportDatabase.txt), so any WED of the same version
// on the same X-Plane reproduces it - on the Gateway too.
bool	WED_LiveryParksNothing(WED_RampPosition * ramp, WED_Airport * apt, std::string & out_msg);

// Codes in the stand's airline list that are no operator at all: no OPERATOR
// record in the livery index and no livery. Usually a typo ("dla" for "dal"),
// sometimes filler. The pseudo-operators (XPGA, XPMI, XPZZ_*) are known by
// definition. False, and nothing said, without a 12.5 index or when the
// directory failed to load - then every code would look unknown.
bool	WED_LiveryUnknownOperators(WED_RampPosition * ramp, std::string & out_msg);

// R14's one-click fix. Fixable when something the stand may list fits it at a
// class at or below its current top (a fix never enlarges a stand: the 1301
// letter is its physical size for ATC and AI). The fix moves weights - or the
// size range's lower end - onto the nearest such class. Call inside a command;
// out_what says what changed.
bool	WED_LiveryParksNothingFixable(WED_RampPosition * ramp, WED_Airport * apt);

// The weights a legacy stand (no 1313) is updated to: today's step-down from its
// letter (WED_LegacyStepDownWeights) with the fall-through folded in, so it parks
// what it parked before. Before the update, the stand must still be legacy.
void	WED_LiveryLegacyUpdateWeights(WED_RampPosition * ramp, WED_Airport * apt, int out_w[6]);
bool	WED_LiveryFixParksNothing(WED_RampPosition * ramp, WED_Airport * apt, std::string * out_what);

// The Moderation View's report, as plain text for the clipboard: airport,
// counts, the data it was checked against, rows the import skipped, then each
// stand that needs a look with what the validator and the operator check say.
std::string	WED_ModerationReport(WED_Airport * apt, const std::set<std::string> & reviewed_setups);

// "C60 D30 E10" - the weights as whole percentages, or "" when all zero.
std::string	WED_ModerationWeightsText(const int w[6]);

// What the stand would park, as a string: operation type, airline set, weights or
// size letter. Two stands with the same signature are the same entry.
std::string	WED_ModerationSignature(WED_RampPosition * ramp);

// The colour for a signature, shared by every map layer and stable for the
// session (see WED_ModerationLayer.h: "colour is similarity").
void	WED_ModerationColour(const std::string & signature, float out_rgba[4]);

// Opens a search in a small, chromeless browser window beside the cursor (Edge
// or Chrome in app mode), or the default browser when neither is found. Call it
// only once a click is over - see WED_ModerationLayer::HandleClickUp.
void	WED_ModerationOpenSearch(const std::string & url);

#endif /* WED_LIVERYMODERATION_H */
