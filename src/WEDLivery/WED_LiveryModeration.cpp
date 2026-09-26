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
#include "WED_Airport.h"
#include "WED_RampPosition.h"
#include "WED_ToolUtils.h"		// WED_GetCurrentAirport, WED_GetSelect
#include "ISelection.h"
#include "IOperation.h"
#include "WED_EnumSystem.h"
#include "PlatformUtils.h"		// ConfirmMessage
#include "GUI_Help.h"			// GUI_LaunchURL

#include <algorithm>
#include <set>
#include <sstream>
#include <cctype>
#include <cstdio>

using std::string;
using std::set;
using std::vector;

bool	WED_ModerationEnabled(void)
{
	return true;
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

string	WED_ModerationSearchURL(const string & operator_name, const string & airport_name, const string & icao)
{
	string q = "Does " + operator_name + " fly to " + airport_name;
	if (!icao.empty()) q += " " + icao;
	return "https://www.google.com/search?q=" + UrlEncode(q);
}

static void AirportIds(WED_Airport * apt, string & icao, string & name)
{
	string ident;
	apt->GetICAO(ident);
	string meta = apt->ContainsMetaDataKey("icao_code") ? apt->GetMetaDataValue("icao_code") : string();
	icao = Upper(!meta.empty() ? meta : ident);
	apt->GetName(name);
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

	string icao, apt_name;
	AirportIds(apt, icao, apt_name);
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
							WED_ModerationSearchURL(uc + " airline", apt_name, icao) });
			continue;
		}
		// Only for airlines: military, government and GA are not in the
		// airport database's served lists, and the pseudo-codes serve nowhere.
		if (e.op_class != WED_AirlineDirectoryEntry::op_Pax && e.op_class != WED_AirlineDirectoryEntry::op_Cargo) continue;
		if (WED_IsGenericAirlinerCode(uc)) continue;
		if (db_knows_airport && !served_set.count(uc))
			out.push_back({ WED_ModerationNote::note_NotServedHere, uc,
							uc + " " + e.name + " is not listed as serving " + icao + ".",
							WED_ModerationSearchURL(e.name, apt_name, icao) });
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

WED_RampPosition *	WED_ModerationStep(IResolver * resolver, int dir)
{
	WED_Airport * apt = WED_GetCurrentAirport(resolver);
	ISelection * sel = WED_GetSelect(resolver);
	if (!apt || !sel) return NULL;

	vector<WED_RampPosition *> ramps;
	CollectRamps(apt, ramps);
	if (ramps.empty()) return NULL;

	int cur = FindSelected(sel, ramps);
	int n = (int) ramps.size();
	int next = cur < 0 ? (dir >= 0 ? 0 : n - 1) : ((cur + (dir >= 0 ? 1 : -1)) % n + n) % n;

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
			GUI_LaunchURL(to_check[i]->search_url.c_str());
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
	out.ramp_type   = ENUM_Desc(ramp->GetType());
	out.auto_filled = ramp->IsAutoFilled();
	out.updated     = ramp->GetClassWeights(out.weights);
	const char * letter = ENUM_Desc(ramp->GetWidth());
	out.size_letter = (letter && *letter) ? letter[0] : '?';

	set<int> eq;
	ramp->GetEquipment(eq);
	for (set<int>::const_iterator e = eq.begin(); e != eq.end(); ++e)
		out.equipment += string(out.equipment.empty() ? "" : ", ") + ENUM_Desc(*e);

	string apt_name;
	if (apt) AirportIds(apt, out.icao, apt_name);

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

	vector<string> sorted_codes;
	std::istringstream ss(ramp->GetAirlines());
	string code;
	while (ss >> code)
	{
		WED_ModerationCode c;
		c.code    = Upper(code);
		c.verdict = WED_ModerationCode::v_Plain;
		sorted_codes.push_back(c.code);

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
				c.search_url = WED_ModerationSearchURL(name, apt_name, out.icao);
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

	std::sort(sorted_codes.begin(), sorted_codes.end());
	sorted_codes.erase(std::unique(sorted_codes.begin(), sorted_codes.end()), sorted_codes.end());
	out.signature = out.op_label + "|";
	for (size_t i = 0; i < sorted_codes.size(); ++i) out.signature += sorted_codes[i] + " ";
	out.signature += "|";
	out.signature += out.updated ? WED_ModerationWeightsText(out.weights) : string(1, out.size_letter);
}
