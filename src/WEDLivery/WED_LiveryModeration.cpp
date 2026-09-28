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

#include "WED_LiveryModeration.h"
#include "WED_LiveryRules.h"
#include "WED_LiveryIndex.h"
#include "WED_Version.h"
#include "WED_Document.h"
#include "WED_Globals.h"		// gModeratorMode
#include "GISUtils.h"
#include "WED_Airport.h"
#include "WED_RampPosition.h"
#include "WED_Entity.h"
#include "WED_ToolUtils.h"		// WED_GetCurrentAirport, WED_GetSelect
#include "ISelection.h"
#include "IOperation.h"
#include "WED_EnumSystem.h"
#include "PlatformUtils.h"		// ConfirmMessage
#include "GUI_Help.h"			// GUI_LaunchURL
#if !IBM
	#include <unistd.h>				// access()
#endif
#if LIN
	#include <spawn.h>				// posix_spawnp
	#include <sys/wait.h>
	#include <thread>
	extern char ** environ;
#endif

#include <algorithm>
#include <chrono>
#include <thread>
#include <cmath>
#include <map>
#include <set>
#include <sstream>
#include <cctype>
#include <cstdio>

using std::string;
using std::wstring;
using std::set;
using std::vector;

bool	WED_ModerationEnabled(void)
{
	return gModeratorMode != 0;
}

static string Upper(string s)
{
	for (size_t i = 0; i < s.size(); ++i) s[i] = (char) toupper((unsigned char) s[i]);
	return s;
}

static string UrlEncode(const string & s)
{
	static const char * hex = "0123456789ABCDEF";
	string out;
	for (size_t i = 0; i < s.size(); ++i)
	{
		unsigned char c = (unsigned char) s[i];
		if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~')	out += (char) c;
		else if (c == ' ')												out += '+';
		else { out += '%'; out += hex[c >> 4]; out += hex[c & 15]; }
	}
	return out;
}

// The question as a person would type it (Eric, 2026-09-27): "Does <operator>
// fly to <ICAO> Now" - "Now" steers the results to current schedules rather
// than history. NOT the airport's name, which is whatever the author typed and
// is noise to a search engine; the city is not needed with the ICAO code.
string	WED_ModerationSearchURL(const string & operator_name, const string & city, const string & icao)
{
	(void) city;
	string q = "Does " + operator_name + " fly to " + icao + " Now";
	return "https://www.google.com/search?q=" + UrlEncode(q);
}

// The airport's ICAO (icao_code metadata, else its ID) and city (1302 city).
static void AirportIds(WED_Airport * apt, string & icao, string & city)
{
	string ident;
	apt->GetICAO(ident);
	string meta = apt->ContainsMetaDataKey("icao_code") ? apt->GetMetaDataValue("icao_code") : string();
	icao = Upper(!meta.empty() ? meta : ident);
	city = apt->ContainsMetaDataKey("city") ? apt->GetMetaDataValue("city") : string();
}

void	WED_ModerationNotes(WED_RampPosition * ramp, WED_Airport * apt, vector<WED_ModerationNote> & out)
{
	out.clear();
	if (!ramp || !apt) return;

	if (ramp->IsAutoFilled())
		out.push_back({ WED_ModerationNote::note_AutoFilled, "", "Auto-filled; nobody has edited this stand since.", "" });
	if (ramp->GetRampOperationType() == ramp_operation_None)
		out.push_back({ WED_ModerationNote::note_NoStaticAircraft, "", "Operation type None: no static aircraft will park here.", "" });

	WED_LiveryData * d = WED_GetLiveryData(false);
	if (!d) return;						// no index: nothing to check operators against

	string icao, apt_city;
	AirportIds(apt, icao, apt_city);
	vector<string> served;
	if (!d->airports.GetAirlines(icao, served))
	{
		string ident;							// the metadata code is not in the database: try the ID
		apt->GetICAO(ident);
		d->airports.GetAirlines(Upper(ident), served);
	}
	std::set<string> served_set;
	for (size_t i = 0; i < served.size(); ++i) served_set.insert(Upper(served[i]));
	const bool db_knows_airport = !served.empty();

	std::istringstream ss(ramp->GetAirlines());
	string code;
	while (ss >> code)
	{
		string uc = Upper(code);
		WED_AirlineDirectoryEntry e;
		if (!d->directory.Lookup(uc, e))
		{
			out.push_back({ WED_ModerationNote::note_UnknownOperator, uc,
							uc + " is not an operator in the livery index.",
							WED_ModerationSearchURL(uc + " airline", apt_city, icao) });
			continue;
		}
		// Only for airlines: military, government and GA are not in the
		// airport database's served lists, and the pseudo-codes serve nowhere.
		if (e.op_class != WED_AirlineDirectoryEntry::op_Pax && e.op_class != WED_AirlineDirectoryEntry::op_Cargo) continue;
		if (WED_IsGenericAirlinerCode(uc)) continue;
		if (db_knows_airport && !served_set.count(uc))
			out.push_back({ WED_ModerationNote::note_NotServedHere, uc,
							uc + " " + e.name + " is not listed as serving " + icao + ".",
							WED_ModerationSearchURL(e.name, apt_city, icao) });
	}
}

static void CollectRamps(WED_Thing * t, vector<WED_RampPosition *> & out)
{
	if (WED_RampPosition * r = dynamic_cast<WED_RampPosition *>(t)) out.push_back(r);
	for (int i = 0; i < t->CountChildren(); ++i) CollectRamps(t->GetNthChild(i), out);
}

static int FindSelected(ISelection * sel, const vector<WED_RampPosition *> & ramps)
{
	for (size_t i = 0; i < ramps.size(); ++i)
		if (sel->IsSelected(ramps[i])) return (int) i;
	return -1;
}

void	WED_ModerationRamps(WED_Airport * apt, vector<WED_RampPosition *> & out)
{
	out.clear();
	if (apt) CollectRamps(apt, out);
}

bool	WED_ModerationHasIssue(const WED_ModerationEntry & e)
{
	return e.n_to_check > 0 || e.verify == WED_ModerationEntry::verify_NoData || e.parks_nothing;
}

WED_RampPosition *	WED_ModerationStep(IResolver * resolver, int dir, bool issues_only)
{
	WED_Airport * apt = WED_GetCurrentAirport(resolver);
	ISelection * sel = WED_GetSelect(resolver);
	if (!apt || !sel) return NULL;

	vector<WED_RampPosition *> ramps;
	CollectRamps(apt, ramps);
	if (ramps.empty()) return NULL;

	int cur = FindSelected(sel, ramps);
	int n = (int) ramps.size();
	int next = -1;
	const int step = dir >= 0 ? 1 : -1;
	int at = cur < 0 ? (dir >= 0 ? -1 : n) : cur;
	for (int tries = 0; tries < n; ++tries)
	{
		at = ((at + step) % n + n) % n;
		if (!issues_only) { next = at; break; }
		WED_ModerationEntry e;
		WED_ModerationDescribe(ramps[at], apt, e);
		if (WED_ModerationHasIssue(e)) { next = at; break; }
	}
	if (next < 0) return NULL;						// nothing needs checking

	IOperation * op = dynamic_cast<IOperation *>(sel);
	if (op) op->StartOperation("Select Ramp Start");
	sel->Clear();
	sel->Select(ramps[next]);
	if (op) op->CommitOperation();
	return ramps[next];
}

void	WED_ModerationPrompt(WED_RampPosition * ramp, WED_Airport * apt)
{
	vector<WED_ModerationNote> notes;
	WED_ModerationNotes(ramp, apt, notes);

	vector<const WED_ModerationNote *> to_check;
	for (size_t i = 0; i < notes.size(); ++i)
		if (!notes[i].search_url.empty()) to_check.push_back(&notes[i]);
	if (to_check.empty()) return;

	string name;
	ramp->GetName(name);
	string msg = "Ramp start '" + name + "' lists operators that need checking:\n";
	for (size_t i = 0; i < to_check.size(); ++i)
		msg += "\n  - " + to_check[i]->text;
	for (size_t i = 0; i < notes.size(); ++i)
		if (notes[i].search_url.empty()) msg += "\n  (" + notes[i].text + ")";

	// Opening a browser tab per operator: capped, so a stand stuffed with thirty
	// codes does not open thirty tabs.
	const size_t kMaxTabs = 5;
	msg += to_check.size() > kMaxTabs ? "\n\nSearch the web for the first five?" : "\n\nSearch the web for them?";
	if (ConfirmMessage(msg.c_str(), "Search", "Skip"))
		for (size_t i = 0; i < to_check.size() && i < kMaxTabs; ++i)
			WED_ModerationOpenSearch(to_check[i]->search_url);
}

// Hidden things under t. top_only: the topmost only (a hidden folder is listed,
// not its contents) - for the message; otherwise all of them, nested ones too,
// so Show All leaves nothing hidden inside a folder it un-hid.
static void	CollectHidden(WED_Thing * t, vector<WED_Thing *> & out, bool top_only = true)
{
	WED_Entity * e = dynamic_cast<WED_Entity *>(t);
	if (e && e->GetHidden()) { out.push_back(t); if (top_only) return; }
	for (int n = 0; n < t->CountChildren(); ++n)
		CollectHidden(t->GetNthChild(n), out, top_only);
}

bool	WED_ModerationConfirmHidden(WED_Thing * root)
{
	if (!root || !WED_ModerationEnabled()) return true;
	vector<WED_Thing *> hidden;
	CollectHidden(root, hidden);
	if (hidden.empty()) return true;

	string names;
	const size_t kShow = 8;
	for (size_t i = 0; i < hidden.size() && i < kShow; ++i)
	{
		string nm;
		hidden[i]->GetName(nm);
		names += "\n  - " + nm;
	}
	if (hidden.size() > kShow) names += "\n  and " + std::to_string(hidden.size() - kShow) + " more";
	const string msg = std::to_string(hidden.size()) + (hidden.size() == 1 ? " item is" : " items are") +
		" hidden, and hidden items are not exported:" + names +
		"\n\nShow everything before exporting?";

	const int ans = ConfirmMessage(msg.c_str(), "Show All and Export", "Cancel", "Export As Is");
	if (ans == 0) return false;
	if (ans == 1)
	{
		vector<WED_Thing *> all;
		CollectHidden(root, all, false);
		hidden.swap(all);
		hidden[0]->StartCommand("Show All");
		for (size_t i = 0; i < hidden.size(); ++i)
		{
			int idx = hidden[i]->FindProperty("Hidden");
			if (idx == -1) continue;
			PropertyVal_t val;
			val.prop_kind = prop_Bool;
			val.int_val = 0;
			hidden[i]->SetNthProperty(idx, val);
		}
		hidden[0]->CommitCommand();
	}
	return true;
}

// R14, as a warning: can anything park here at all? Only the static aircraft are
// at stake - the airline list still drives ATC and AI parking whatever the index
// says - so this never blocks an export. Needs livery_index.txt; without one
// (an X-Plane before 12.5) there is nothing to check against and it stays quiet.
//
// Airline and cargo stands draw from their list. GA stands never read it (R28),
// and a military stand with nothing listed draws any military livery of its size
// (spec 4.1): those two are checked against the whole library.
//
// One analysis feeds the warning, its wording (R14's table) and the fix, so the
// three cannot drift apart.
namespace {
struct StandAnalysis {
	bool	checked = false;		// false: nothing to judge (None, no list, all zero, no index)
	bool	weighted = false;
	int		wts[6] = { 0, 0, 0, 0, 0, 0 };
	int		lo = 0, hi = 0;			// size range when not weighted
	bool	allowed[6] = { false, false, false, false, false, false };	// classes the stand opens
	bool	fits[6] = { false, false, false, false, false, false };		// classes some candidate livery fits HERE
	bool	listed_at_allowed = false;	// a candidate has a livery at an open class, range/equipment aside
	bool	library_at_allowed = false;	// X-Plane has ANY livery at an open class
	bool	pool = false;
	int		op = 0;
	string	country;
};
}

static void	AnalyseStand(WED_RampPosition * ramp, WED_Airport * apt, StandAnalysis & a)
{
	a = StandAnalysis();
	if (!ramp || !apt) return;
	const string airlines = ramp->GetAirlines();
	a.weighted = ramp->GetClassWeights(a.wts);
	a.op = ramp->GetRampOperationType();
	if (a.op == ramp_operation_None) return;				// parks nothing by definition (R29)
	a.pool = a.op == ramp_operation_GeneralAviation || (a.op == ramp_operation_Military && airlines.empty());
	if (!a.pool && airlines.empty()) return;				// an airline stand with no list: nothing was asked for

	// The same data and the same rule as the Liveries tab - range (R26) and the
	// stand's equipment type included - so the two never disagree about a stand.
	WED_LiveryData * d = WED_GetLiveryData(true);
	if (!d) return;

	bool any = false;
	if (a.weighted)
	{
		for (int k = 0; k < 6; ++k) { a.allowed[k] = a.wts[k] > 0; any |= a.allowed[k]; }
		if (!any) return;			// all zero: the author said nothing parks here (V2)
	}
	else
	{
		// Legacy format (no 1313): the sim steps down from the letter and keeps
		// going past any class with nothing to park, so every class at or below
		// the letter counts. WED's own lower size bound never reaches apt.dat.
		a.hi = ENUM_Export(ramp->GetWidth());
		a.lo = 0;
		for (int k = 0; k < 6; ++k) a.allowed[k] = (k <= a.hi);
	}
	a.checked = true;

	{
		string icao;
		apt->GetICAO(icao);
		string meta = apt->ContainsMetaDataKey("icao_code") ? apt->GetMetaDataValue("icao_code") : string();
		for (auto & c : icao) c = (char) toupper((unsigned char) c);
		for (auto & c : meta) c = (char) toupper((unsigned char) c);
		if (!d->airports.GetCountry(!meta.empty() ? meta : icao, a.country)) d->airports.GetCountry(icao, a.country);
	}
	Point2 here;
	ramp->GetLocation(gis_Geo, here);
	set<int> equipment;
	ramp->GetEquipment(equipment);

	for (int k = 0; k < 6; ++k)
		if (a.allowed[k] && d->index.CountAtClass((char) ('A' + k)) > 0) a.library_at_allowed = true;

	vector<string> candidates;
	if (a.pool)
		d->index.GetAirlineCodes(candidates);
	else
	{
		std::istringstream codes(airlines);
		string code;
		while (codes >> code)
		{
			for (auto & c : code) c = (char) toupper((unsigned char) c);
			candidates.push_back(code);
		}
	}
	for (size_t n = 0; n < candidates.size(); ++n)
	{
		const string & code = candidates[n];
		if (!WED_LiveryOperatorFitsRampOp(code, a.op, d->directory)) continue;
		const vector<const WED_LiveryIndexEntry *> * all = d->index.GetForAirline(code);
		if (!all) continue;
		for (size_t i = 0; i < all->size(); ++i)
		{
			const WED_LiveryIndexEntry & e = *(*all)[i];
			if (e.size_class < 'A' || e.size_class > 'F') continue;
			const int k = e.size_class - 'A';
			if (a.allowed[k]) a.listed_at_allowed = true;
			if (!a.fits[k] && WED_LiveryFitsStand(e, d->directory, a.country, here.y(), here.x(), equipment) == livery_allow_Yes)
				a.fits[k] = true;
		}
	}
}

static bool	ParksNothing(const StandAnalysis & a)
{
	if (!a.checked) return false;
	for (int k = 0; k < 6; ++k) if (a.allowed[k] && a.fits[k]) return false;
	return true;
}

// The largest class the stand may already hold: a fix never makes a stand bigger.
// 1301's letter is the stand's physical size for ATC and AI parking (R23 derives
// it from the largest weight), so moving aircraft onto a larger class would
// quietly let bigger aircraft onto a stand drawn for smaller ones.
static int	StandTop(const StandAnalysis & a)
{
	if (!a.weighted) return a.hi;
	for (int k = 5; k >= 0; --k) if (a.wts[k] > 0) return k;
	return -1;
}

// The nearest class at or below the stand's top that something fits; -1 if none.
// On a tie the smaller class: a smaller aircraft still fits a stand drawn larger.
static int	NearestFit(const StandAnalysis & a, int from)
{
	const int top = StandTop(a);
	for (int dist = 1; dist < 6; ++dist)
	{
		const int lower = from - dist, upper = from + dist;
		if (lower >= 0 && a.fits[lower]) return lower;
		if (upper <= top && upper < 6 && a.fits[upper]) return upper;
	}
	return -1;
}

static bool	FixableAnalysis(const StandAnalysis & a)
{
	if (!ParksNothing(a)) return false;
	// A legacy stand already falls through to anything smaller; what is left
	// when it parks nothing is not a size question. Update it to weights first.
	if (!a.weighted) return false;
	for (int k = 0; k < 6; ++k) if (a.allowed[k] && NearestFit(a, k) >= 0) return true;
	return false;
}

bool	WED_LiveryUnknownOperators(WED_RampPosition * ramp, string & out_msg)
{
	out_msg.clear();
	WED_LiveryData * d = WED_GetLiveryData(false);
	if (!d || d->directory.LoadFailed()) return false;

	vector<string> unknown;
	std::istringstream ss(ramp->GetAirlines());
	string code;
	while (ss >> code)
	{
		const string uc = Upper(code);
		if (uc == "XPGA" || uc == "XPMI" || WED_IsGenericAirlinerCode(uc)) continue;
		WED_AirlineDirectoryEntry e;
		if (d->directory.Lookup(uc, e)) continue;
		const vector<const WED_LiveryIndexEntry *> * rows = d->index.GetForAirline(uc);
		if (rows && !rows->empty()) continue;
		unknown.push_back(code);
	}
	if (unknown.empty()) return false;

	string name, list;
	ramp->GetName(name);
	for (size_t i = 0; i < unknown.size(); ++i) list += (i ? ", " : "") + unknown[i];
	out_msg = "Ramp start '" + name + "' lists " + list + (unknown.size() == 1 ? ", which is not a known operator" : ", which are not known operators") +
			  " (no record in X-Plane's livery index). Check the spelling; X-Plane parks nothing for an unknown code.";
	return true;
}

bool	WED_LiveryParksNothing(WED_RampPosition * ramp, WED_Airport * apt, string & out_msg)
{
	out_msg.clear();
	StandAnalysis a;
	AnalyseStand(ramp, apt, a);
	if (!ParksNothing(a)) return false;

	const string airlines = ramp->GetAirlines();
	string classes, name, fit_classes;
	ramp->GetName(name);
	const int top = StandTop(a);
	for (int k = 0; k < 6; ++k)
	{
		if (a.allowed[k]) classes += (char) ('A' + k);
		if (a.fits[k] && k <= top) fit_classes += (char) ('A' + k);
	}
	const bool fixable = FixableAnalysis(a);
	const char * weights_or_size = a.weighted ? "its spawn weights" : "its size (legacy format, stepping down to A)";

	// R14's cases, which must not read alike.
	string why;
	if (!a.library_at_allowed)
		// ahead of the art: nothing to fix - it starts working the day one ships
		why = string("X-Plane has no static aircraft at size ") + classes + " yet - nothing can park here until one ships";
	else if (a.pool)
	{
		const char * kind = a.op == ramp_operation_GeneralAviation ? "general aviation" : "military";
		why = string("no ") + kind + " aircraft matches its size (" + classes + ")";
		if (a.listed_at_allowed)
			why += a.op == ramp_operation_Military && !a.country.empty()
				? " that may park in " + a.country + " and fits its equipment type"
				: " and fits its equipment type";
	}
	else if (a.listed_at_allowed)
		// they fly this size, but nothing reaches from a hub or fits the equipment
		why = "none of its operators (" + airlines + ") has a static livery at size " + classes +
			  " that can reach this airport and fits its equipment type";
	else
		// the weights (or the range) point where these operators do not fly: a mistake
		why = string(weights_or_size) + " (" + classes + ") point where none of its operators (" + airlines +
			  ") has a static livery";

	out_msg = string("Ramp start '") + name + "': " + why +
		", so X-Plane will park no static aircraft here. ATC and AI parking are unaffected.";
	if (fixable)
	{
		int w[6], new_top = -1;
		for (int k = 0; k < 6; ++k) w[k] = a.wts[k];
		for (int k = 0; k < 6; ++k)
			if (a.wts[k] > 0 && !a.fits[k]) { const int t = NearestFit(a, k); if (t >= 0) { w[t] += a.wts[k]; w[k] -= a.wts[k]; } }
		for (int k = 5; k >= 0; --k) if (w[k] > 0) { new_top = k; break; }
		out_msg += " Fixable: select it and press Fix - " + string(weights_or_size) + " move onto " + fit_classes;
		if (new_top >= 0 && new_top != top)
			out_msg += string(", and the stand's size becomes ") + (char) ('A' + new_top) + " (was " + (char) ('A' + top) + "; AI and ATC follow it)";
		out_msg += ".";
	}
	return true;
}

// Updating a legacy stand to weights: today's step-down, with its fall-through
// folded in - a class nothing can park at hands its share to the next class
// below that something can, which is where the legacy draw would have gone. A
// share with nothing below it stays where it is: legacy parked nothing there
// either. So the stand parks exactly what it parked before the update.
void	WED_LiveryLegacyUpdateWeights(WED_RampPosition * ramp, WED_Airport * apt, int out_w[6])
{
	const int top = ramp ? ENUM_Export(ramp->GetWidth()) : 2;
	WED_LegacyStepDownWeights(top, out_w);
	StandAnalysis a;
	AnalyseStand(ramp, apt, a);
	if (!a.checked) return;				// nothing to judge against: the plain step-down
	for (int k = top; k >= 1; --k)
		if (out_w[k] > 0 && !a.fits[k])
			for (int j = k - 1; j >= 0; --j)
				if (a.fits[j])
				{
					// The top class keeps a token 1: under R23 the 1301 letter is the
					// largest weighted class, and an update is not the author saying
					// the stand got smaller - AI and ATC must still see its size.
					const int keep = (k == top) ? 1 : 0;
					out_w[j] += out_w[k] - keep;
					out_w[k] = keep;
					break;
				}
}

bool	WED_LiveryParksNothingFixable(WED_RampPosition * ramp, WED_Airport * apt)
{
	StandAnalysis a;
	AnalyseStand(ramp, apt, a);
	return FixableAnalysis(a);
}

// Inside the caller's command. Weights: each weight on a class nothing fits here
// moves to the nearest class that something does, never above the stand's top -
// the total is kept. A size range: its lower end moves down to the nearest
// fitting class, so the 1301 letter (the top) is unchanged.
bool	WED_LiveryFixParksNothing(WED_RampPosition * ramp, WED_Airport * apt, string * out_what)
{
	StandAnalysis a;
	AnalyseStand(ramp, apt, a);
	if (!FixableAnalysis(a)) return false;

	string name;
	ramp->GetName(name);
	char buf[256];
	const int old_top = StandTop(a);
	{
		int w[6];
		for (int k = 0; k < 6; ++k) w[k] = a.wts[k];
		for (int k = 0; k < 6; ++k)
			if (a.wts[k] > 0 && !a.fits[k])
			{
				const int t = NearestFit(a, k);
				if (t < 0) continue;
				w[t] += a.wts[k];
				w[k] -= a.wts[k];
			}
		ramp->SetClassWeights(w);
		int new_top = -1;
		for (int k = 5; k >= 0; --k) if (w[k] > 0) { new_top = k; break; }
		if (out_what)
		{
			snprintf(buf, sizeof(buf), "Ramp start '%s': weights now %d %d %d %d %d %d (A-F)",
					 name.c_str(), w[0], w[1], w[2], w[3], w[4], w[5]);
			*out_what = buf;
			// R23: the 1301 letter follows the largest weight - say so, since
			// AI and ATC read it as the stand's size.
			if (new_top >= 0 && new_top != old_top)
			{
				snprintf(buf, sizeof(buf), "; stand size %c -> %c (AI and ATC follow)", (char) ('A' + old_top), (char) ('A' + new_top));
				*out_what += buf;
			}
		}
	}
	return true;
}

// ---- the callout model ----

string	WED_ModerationWeightsText(const int w[6])
{
	int total = 0;
	for (int k = 0; k < 6; ++k) total += w[k];
	if (total <= 0) return string();
	string out;
	for (int k = 0; k < 6; ++k)
	{
		if (w[k] <= 0) continue;
		char buf[16];
		snprintf(buf, sizeof(buf), "%s%c%d", out.empty() ? "" : " ", 'A' + k, (int) (100.0 * w[k] / total + 0.5));
		out += buf;
	}
	return out;
}

// The Liveries tab's names for the operation types, so the callout and the tab
// read the same. The file keeps the original enum names (see WED_Enums.h).
static const char * OpLabel(int op)
{
	switch (op) {
	case ramp_operation_GeneralAviation:	return "Private";
	case ramp_operation_Airline:			return "Passenger";
	case ramp_operation_Cargo:				return "Cargo";
	case ramp_operation_Military:			return "Military/Gov";
	default:								return "None";
	}
}

void	WED_ModerationDescribe(WED_RampPosition * ramp, WED_Airport * apt, WED_ModerationEntry & out)
{
	out = WED_ModerationEntry();
	out.verify = WED_ModerationEntry::verify_None;
	out.n_to_check = 0;
	for (int k = 0; k < 6; ++k) out.weights[k] = 0;
	if (!ramp) return;

	ramp->GetName(out.ramp_name);
	out.op_type     = ramp->GetRampOperationType();
	out.op_label    = OpLabel(out.op_type);
	// ENUM_Desc is NULL for -1, which an unknown ramp type name in earth.wed.xml
	// reads as (hand-edited, or from a newer WED)
	const char * rtype = ENUM_Desc(ramp->GetType());
	out.ramp_type   = rtype ? rtype : "";
	out.auto_filled = ramp->IsAutoFilled();
	out.updated     = ramp->GetClassWeights(out.weights);
	const char * letter = ENUM_Desc(ramp->GetWidth());
	out.size_letter = (letter && *letter) ? letter[0] : '?';

	set<int> eq;
	ramp->GetEquipment(eq);
	for (set<int>::const_iterator e = eq.begin(); e != eq.end(); ++e)
		out.equipment += string(out.equipment.empty() ? "" : ", ") + ENUM_Desc(*e);

	string apt_city;
	if (apt) AirportIds(apt, out.icao, apt_city);

	WED_LiveryData * d = WED_GetLiveryData(false);
	vector<string> served;
	if (d && apt)
	{
		string ident;
		apt->GetICAO(ident);
		if (!d->airports.GetCountry(out.icao, out.country)) d->airports.GetCountry(Upper(ident), out.country);
		if (!d->airports.GetAirlines(out.icao, served))     d->airports.GetAirlines(Upper(ident), served);
	}
	std::set<string> served_set;
	for (size_t i = 0; i < served.size(); ++i) served_set.insert(Upper(served[i]));

	const bool pax_cargo = out.op_type == ramp_operation_Airline || out.op_type == ramp_operation_Cargo;
	if (pax_cargo)
		out.verify = out.auto_filled   ? WED_ModerationEntry::verify_Assumed
				   : !served.empty()   ? WED_ModerationEntry::verify_Database
									   : WED_ModerationEntry::verify_NoData;
	else if (out.op_type == ramp_operation_Military)
		out.verify = WED_ModerationEntry::verify_Country;

	std::istringstream ss(ramp->GetAirlines());
	string code;
	while (ss >> code)
	{
		WED_ModerationCode c;
		c.code    = Upper(code);
		c.verdict = WED_ModerationCode::v_Plain;

		WED_AirlineDirectoryEntry e;
		const bool known = d && d->directory.Lookup(c.code, e);
		if (known) c.country = e.country;
		const string name = known ? e.name : c.code + " airline";
		const bool pseudo = WED_IsGenericAirlinerCode(c.code) || c.code == "XPGA" || c.code == "XPMI";

		switch (out.verify) {
		case WED_ModerationEntry::verify_Assumed:
			c.verdict = WED_ModerationCode::v_Assumed;
			break;
		case WED_ModerationEntry::verify_Database:
			if (pseudo)							c.verdict = WED_ModerationCode::v_Plain;
			else if (known && served_set.count(c.code))	c.verdict = WED_ModerationCode::v_Ok;
			else
			{
				c.verdict    = WED_ModerationCode::v_Check;
				c.search_url = WED_ModerationSearchURL(name, apt_city, out.icao);
			}
			break;
		case WED_ModerationEntry::verify_Country:
			if (pseudo || c.country.empty() || out.country.empty())	c.verdict = WED_ModerationCode::v_Plain;
			else c.verdict = c.country == out.country ? WED_ModerationCode::v_Ok : WED_ModerationCode::v_Foreign;
			break;
		default:
			break;
		}
		if (c.verdict == WED_ModerationCode::v_Check || c.verdict == WED_ModerationCode::v_Foreign) ++out.n_to_check;
		out.codes.push_back(c);
	}

	out.signature = WED_ModerationSignature(ramp);
	out.parks_nothing = WED_LiveryParksNothing(ramp, apt, out.parks_nothing_msg);
}

string	WED_ModerationSignature(WED_RampPosition * ramp)
{
	if (!ramp) return string();
	vector<string> codes;
	std::istringstream ss(ramp->GetAirlines());
	string code;
	while (ss >> code) codes.push_back(Upper(code));
	std::sort(codes.begin(), codes.end());
	codes.erase(std::unique(codes.begin(), codes.end()), codes.end());

	string sig = string(OpLabel(ramp->GetRampOperationType())) + "|";
	for (size_t i = 0; i < codes.size(); ++i) sig += codes[i] + " ";
	sig += "|";
	int w[6];
	if (ramp->GetClassWeights(w)) sig += WED_ModerationWeightsText(w);
	else { const char * l = ENUM_Desc(ramp->GetWidth()); sig += (l && *l) ? l : "?"; }
	// equipment decides what can park (an E stand for jets only takes no 747),
	// so two stands that differ in it are not the same setup
	set<int> eq;
	ramp->GetEquipment(eq);
	sig += "|";
	for (set<int>::const_iterator e = eq.begin(); e != eq.end(); ++e) { char b[16]; snprintf(b, sizeof(b), "%d,", *e); sig += b; }
	return sig;
}

// Colour slots are shared by every map layer that asks, and stable for the
// session: the first signature seen gets slot 0, the next slot 1, and so on.
// Golden-ratio steps round the hue circle put each new slot as far as it can be
// from the ones before it, so neighbouring slots never look alike.
static void	HsvToRgb(float h, float s, float v, float out[4])
{
	h = h - floorf(h);
	float r, g, b;
	int i = (int) (h * 6.0f);
	float f = h * 6.0f - (float) i;
	float p = v * (1 - s), q = v * (1 - f * s), t = v * (1 - (1 - f) * s);
	switch (i % 6) {
	case 0: r = v; g = t; b = p; break;
	case 1: r = q; g = v; b = p; break;
	case 2: r = p; g = v; b = t; break;
	case 3: r = p; g = q; b = v; break;
	case 4: r = t; g = p; b = v; break;
	default: r = v; g = p; b = q; break;
	}
	out[0] = r; out[1] = g; out[2] = b; out[3] = 1.0f;
}

void	WED_ModerationColour(const string & signature, float out_rgba[4])
{
	static std::map<string, int> slots;
	static float seed = -1.0f;
	if (seed < 0.0f)
	{
		long long t = std::chrono::steady_clock::now().time_since_epoch().count();
		seed = (float) ((t / 1000) % 1000) / 1000.0f;
	}
	std::map<string, int>::iterator i = slots.find(signature);
	int slot = i != slots.end() ? i->second : (slots[signature] = (int) slots.size());
	HsvToRgb(seed + (float) slot * 0.6180339f, 0.62f, 0.97f, out_rgba);
}

// ---- the search window ----
//
// A small, chromeless browser window beside the cursor, not a tab in whatever
// browser happens to be the default: Edge or Chrome in --app mode. Windows ships
// Edge, so this almost always works there; elsewhere it tries Chrome/Chromium.
// When none is found, the system default browser, as before.
//
// It runs in the moderator's own browser profile, and WED places the window
// itself once it appears. An --app window handed to a browser that is already
// running goes to that process, which ignores the size and position flags (the
// first try opened full-size in the corner). A separate profile would honour
// them, but a profile with no cookies meets Google's "unusual traffic" check
// instead of the results - so: the real profile, and SetWindowPos afterwards.
#if IBM
// The search's query, decoded, for finding its window by title.
static wstring	QueryOf(const string & url)
{
	size_t q = url.find("q=");
	if (q == string::npos) return wstring();
	string enc = url.substr(q + 2, url.find('&', q) == string::npos ? string::npos : url.find('&', q) - q - 2), dec;
	for (size_t i = 0; i < enc.size(); ++i)
	{
		if (enc[i] == '+') dec += ' ';
		else if (enc[i] == '%' && i + 2 < enc.size()) { dec += (char) strtol(enc.substr(i + 1, 2).c_str(), NULL, 16); i += 2; }
		else dec += enc[i];
	}
	return wstring(dec.begin(), dec.end());
}

struct PlaceWindow { wstring query; int x, y, w, h; HWND found; std::set<HWND> before; };

static BOOL CALLBACK	FindSearchWindow(HWND h, LPARAM ref)
{
	PlaceWindow * pw = (PlaceWindow *) ref;
	if (!IsWindowVisible(h)) return TRUE;
	wchar_t title[512];
	if (GetWindowTextW(h, title, 512) <= 0) return TRUE;
	wstring t(title);
	// the page title once loaded, or the address while it loads
	if (t.find(pw->query) == wstring::npos && t.find(L"google.com/search?q=") == wstring::npos) return TRUE;
	if (pw->before.count(h)) return TRUE;			// was there before this search: not ours
	pw->found = h;
	return FALSE;
}

// Waits up to eight seconds for the window, off the main thread, then moves it.
static void	PlaceSearchWindow(PlaceWindow pw)
{
	std::thread([pw]() mutable {
		for (int i = 0; i < 80 && !pw.found; ++i)
		{
			Sleep(100);
			EnumWindows(FindSearchWindow, (LPARAM) &pw);
		}
		if (pw.found) SetWindowPos(pw.found, NULL, pw.x, pw.y, pw.w, pw.h, SWP_NOZORDER);
	}).detach();
}

static bool	FindBrowserExe(wstring & out)
{
	const wchar_t * keys[] = {
		L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\App Paths\\msedge.exe",
		L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\App Paths\\chrome.exe" };
	for (int k = 0; k < 2; ++k)
		for (int hive = 0; hive < 2; ++hive)
		{
			wchar_t buf[MAX_PATH];
			DWORD len = sizeof(buf);
			if (RegGetValueW(hive ? HKEY_CURRENT_USER : HKEY_LOCAL_MACHINE, keys[k], NULL, RRF_RT_REG_SZ, NULL, buf, &len) == ERROR_SUCCESS &&
				GetFileAttributesW(buf) != INVALID_FILE_ATTRIBUTES)
			{
				out = buf;
				return true;
			}
		}
	return false;
}
#endif

void	WED_ModerationOpenSearch(const string & url)
{
	const int w = 620, h = 760;
#if IBM
	wstring exe;
	if (FindBrowserExe(exe))
	{
		POINT p = { 100, 100 };
		GetCursorPos(&p);
		HMONITOR mon = MonitorFromPoint(p, MONITOR_DEFAULTTONEAREST);
		MONITORINFO mi;
		mi.cbSize = sizeof(mi);
		int x = p.x + 24, y = p.y - h / 3;
		if (GetMonitorInfo(mon, &mi))
		{
			if (x + w > mi.rcWork.right)  x = p.x - 24 - w;
			if (x < mi.rcWork.left)       x = mi.rcWork.left;
			if (y + h > mi.rcWork.bottom) y = mi.rcWork.bottom - h;
			if (y < mi.rcWork.top)        y = mi.rcWork.top;
		}
		wchar_t tail[128];
		swprintf(tail, 128, L" --window-size=%d,%d --window-position=%d,%d", w, h, x, y);
		wstring cmd = L"\"" + exe + L"\" --app=\"" + wstring(url.begin(), url.end()) + L"\"" + tail;
		STARTUPINFOW si;
		PROCESS_INFORMATION pi;
		ZeroMemory(&si, sizeof(si));
		si.cb = sizeof(si);
		ZeroMemory(&pi, sizeof(pi));
		vector<wchar_t> line(cmd.begin(), cmd.end());
		line.push_back(0);
		// Any window already showing this search belongs to the moderator, not
		// to this click: note them so only the new one is moved.
		PlaceWindow pw = { QueryOf(url), x, y, w, h, NULL };
		for (;;)
		{
			pw.found = NULL;
			EnumWindows(FindSearchWindow, (LPARAM) &pw);
			if (!pw.found) break;
			pw.before.insert(pw.found);
		}
		if (CreateProcessW(NULL, &line[0], NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi))
		{
			CloseHandle(pi.hThread);
			CloseHandle(pi.hProcess);
			PlaceSearchWindow(pw);
			return;
		}
	}
#elif APL
	const char * apps[] = { "/Applications/Google Chrome.app", "/Applications/Microsoft Edge.app", "/Applications/Chromium.app" };
	for (int i = 0; i < 3; ++i)
		if (access(apps[i], F_OK) == 0)
		{
			char size[64];
			snprintf(size, sizeof(size), "--window-size=%d,%d", w, h);
			// No trailing '&': open(1) returns as soon as the app is launched, and
			// only then is its exit status the launch's - with '&' it was always
			// 0 and a failed launch never reached the default browser below. The
			// URL is percent-encoded, so it carries no quote to break the shell.
			string cmd = string("open -na \"") + apps[i] + "\" --args --app='" + url + "' " + size;
			if (system(cmd.c_str()) == 0) return;
		}
#else
	// Spawned, not system("... &"): posix_spawnp searches PATH and fails when
	// the browser is not there, so the default browser below is really the
	// fallback. A detached thread reaps the child, so no zombie is left.
	const char * bins[] = { "google-chrome", "chromium", "chromium-browser", "microsoft-edge" };
	char size[64];
	snprintf(size, sizeof(size), "--window-size=%d,%d", w, h);
	const string app = "--app=" + url;
	for (int i = 0; i < 4; ++i)
	{
		char * argv[] = { (char *) bins[i], (char *) app.c_str(), size, NULL };
		pid_t pid;
		if (posix_spawnp(&pid, bins[i], NULL, NULL, argv, environ) == 0)
		{
			std::thread([pid]() { int st; waitpid(pid, &st, 0); }).detach();
			return;
		}
	}
#endif
	GUI_LaunchURL(url.c_str());
}

// ---- the moderation report ----

string	WED_ModerationReport(WED_Airport * apt, const std::set<string> & reviewed)
{
	if (!apt) return string();
	const string NL = "\n";
	vector<WED_RampPosition *> ramps;
	WED_ModerationRamps(apt, ramps);

	string icao, city, name;
	AirportIds(apt, icao, city);
	apt->GetName(name);

	// setups in the order their first stand appears; each lists its stands
	struct Setup { WED_ModerationEntry e; vector<string> stands; vector<string> parks; bool reviewed; };
	vector<string> parks;			// the validator's message for each stand, in its own words
	vector<Setup> setups;
	std::map<string, size_t> at;
	int n_issue = 0, n_auto = 0, n_none = 0, n_rev = 0;
	for (size_t i = 0; i < ramps.size(); ++i)
	{
		WED_ModerationEntry e;
		WED_ModerationDescribe(ramps[i], apt, e);
		if (e.auto_filled) ++n_auto;
		if (e.op_type == ramp_operation_None) ++n_none;
		{	string um;											// Validate's unknown-operator warning, word for word
			if (WED_LiveryUnknownOperators(ramps[i], um)) parks.push_back(um); }
		const bool rev = reviewed.count(e.signature) > 0;
		if (rev) ++n_rev;
		if (!WED_ModerationHasIssue(e)) continue;
		++n_issue;
		std::map<string, size_t>::iterator f = at.find(e.signature);
		if (f == at.end()) { Setup su; su.e = e; su.reviewed = rev; at[e.signature] = setups.size(); setups.push_back(su); f = at.find(e.signature); }
		setups[f->second].stands.push_back(e.ramp_name);
		if (e.parks_nothing) { setups[f->second].parks.push_back(e.ramp_name); parks.push_back(e.parks_nothing_msg); }
	}
	std::set<string> all_setups;
	for (size_t i = 0; i < ramps.size(); ++i) all_setups.insert(WED_ModerationSignature(ramps[i]));

	char buf[256];
	string r = "WED moderation summary - " + icao + " " + name + NL;
	WED_LiveryData * d = WED_GetLiveryData(false);
	r += string("Checked with WED ") + WED_VERSION_STRING + ", livery index " + (d ? d->index.DescribeVersion() : string("(none)")) +
		 ". The same WED on the same X-Plane reproduces every line below." + NL + NL;
	snprintf(buf, sizeof(buf), "%d ramp starts, %d to check, %d unique setups, %d auto-filled, %d \"None\", %d reviewed this session.",
		(int) ramps.size(), n_issue, (int) all_setups.size(), n_auto, n_none, n_rev);
	r += buf + NL;

	// what Validate lists for this airport's liveries - the same functions
	string skipped;
	if (WED_Document * doc = dynamic_cast<WED_Document *>(apt->GetArchive()->GetResolver()))
	{
		// keyed by the airport ID, as the import recorded them and as Validate
		// looks them up - not the icao_code metadata the header shows
		string ident;
		apt->GetICAO(ident);
		skipped = doc->DescribeDiscardedRowsFor(ident);
	}
	if (!skipped.empty() || !parks.empty())
	{
		r += NL + "Validator warnings (static aircraft):" + NL;
		if (!skipped.empty()) r += "- " + skipped + NL;
		for (size_t i = 0; i < parks.size(); ++i) r += "- " + parks[i] + NL;
	}

	if (!setups.empty())
	{
		r += NL + "Stands to check, by setup:" + NL;
		for (size_t i = 0; i < setups.size(); ++i)
		{
			const Setup & su = setups[i];
			string st;
			const size_t kShow = 8;
			for (size_t k = 0; k < su.stands.size() && k < kShow; ++k) st += (k ? ", " : "") + su.stands[k];
			if (su.stands.size() > kShow) { snprintf(buf, sizeof(buf), " and %d more", (int) (su.stands.size() - kShow)); st += buf; }
			r += "- " + st + (su.reviewed ? "  [reviewed]" : "") + NL;
			r += "    " + su.e.op_label + ", " + (su.e.equipment.empty() ? string() : su.e.equipment + ", ") + (su.e.updated ? WED_ModerationWeightsText(su.e.weights) : string("size ") + su.e.size_letter) +
				 ", airlines: " + (su.e.codes.empty() ? string("none listed") : string()) ;
			for (size_t k = 0; k < su.e.codes.size(); ++k) r += (k ? " " : "") + su.e.codes[k].code;
			r += NL;
			string why;
			for (size_t k = 0; k < su.e.codes.size(); ++k)
			{
				const WED_ModerationCode & c = su.e.codes[k];
				if (c.verdict == WED_ModerationCode::v_Check)
					why += (why.empty() ? "" : "; ") + c.code + " not listed as serving " + icao;
				else if (c.verdict == WED_ModerationCode::v_Foreign)
					why += (why.empty() ? "" : "; ") + c.code + " is military of " + (c.country.empty() ? string("another country") : c.country);
			}
			if (su.e.verify == WED_ModerationEntry::verify_NoData) why += (why.empty() ? "" : "; ") + string("no airport data to check the operators against");
			if (!su.parks.empty())
			{
				string at;
				if (su.parks.size() == su.stands.size())
					at = "all of them";
				else
					for (size_t k = 0; k < su.parks.size(); ++k) at += string(k ? ", " : "") + su.parks[k];
				why += (why.empty() ? "" : "; ") + string("parks nothing (validator): ") + at;
			}
			r += "    " + why + NL;
		}
	}
	else
		r += NL + "Nothing to check." + NL;
	return r;
}
