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

#include "WED_LiveryAutoFill.h"
#include "WED_LiveryRules.h"
#include "WED_LiveryIndex.h"
#include "WED_AirlineDirectory.h"
#include "WED_AirportDatabase.h"
#include "WED_MandatoryHeader.h"		// WedDataFileDir
#include "WED_Airport.h"
#include "WED_RampPosition.h"
#include "WED_Archive.h"
#include "WED_EnumSystem.h"
#include "GISUtils.h"
#include "WED_ToolUtils.h"		// WED_GetCurrentAirport
#include "PlatformUtils.h"		// ConfirmMessage, DoUserAlert

#include <set>
#include <sstream>
#include <algorithm>
#include <cctype>

using std::string;
using std::vector;
using std::set;

// Gateway rejects an airline string of 100 characters or more; auto-fill stops
// adding codes before it would get there, so it can never make a stand invalid.
static const size_t kMaxAirlinesChars = 99;

static string Upper(string s)
{
	for (size_t i = 0; i < s.size(); ++i) s[i] = (char) toupper((unsigned char) s[i]);
	return s;
}

static void CollectRamps(WED_Thing * t, vector<WED_RampPosition *> & out)
{
	if (WED_RampPosition * r = dynamic_cast<WED_RampPosition *>(t))
		out.push_back(r);
	for (int i = 0; i < t->CountChildren(); ++i)
		CollectRamps(t->GetNthChild(i), out);
}

// The data every stand is checked against. Loaded once per session and kept,
// like the Liveries tab's own copies; the index reloads if the X-Plane folder
// changes, which is when its path does.
struct AutoFillData {
	WED_LiveryIndex			index;
	WED_AirlineDirectory	directory;
	WED_AirportDatabase		airports;
};

static AutoFillData & Data(void)
{
	static AutoFillData d;
	return d;
}

// Does `code` have a usable livery for this stand - a weighted class, the
// stand's equipment, allowed here (range, home soil)?
static bool HasFittingLivery(AutoFillData & d, const string & code, const bool classes[6],
							 const set<int> & equipment, const string & country, double lat, double lon)
{
	const vector<const WED_LiveryIndexEntry *> * all = d.index.GetForAirline(code);
	if (!all) return false;
	for (size_t i = 0; i < all->size(); ++i)
	{
		const WED_LiveryIndexEntry & e = *(*all)[i];
		if (e.size_class < 'A' || e.size_class > 'F' || !classes[e.size_class - 'A']) continue;
		int eq = WED_LiveryEquipment(e);
		if (eq != -1 && !equipment.empty() && !equipment.count(eq)) continue;
		if (WED_LiveryAllowedAt(e, d.directory, country, lat, lon) != livery_allow_Yes) continue;
		return true;
	}
	return false;
}

static bool OperatorIs(AutoFillData & d, const string & code, int ramp_op)
{
	WED_AirlineDirectoryEntry e;
	if (!d.directory.Lookup(code, e)) return false;		// only operators we know anything about
	switch (ramp_op) {
	case ramp_operation_Airline:	return e.op_class == WED_AirlineDirectoryEntry::op_Pax;
	case ramp_operation_Cargo:		return e.op_class == WED_AirlineDirectoryEntry::op_Cargo;
	case ramp_operation_Military:	return e.op_class == WED_AirlineDirectoryEntry::op_Military ||
										   e.op_class == WED_AirlineDirectoryEntry::op_Gov;
	}
	return false;
}

WED_AutoFillPlan	WED_PlanLiveryAutoFill(WED_Airport * apt)
{
	WED_AutoFillPlan plan;
	plan.airport = apt;
	if (!apt) { plan.error = "No airport."; return plan; }

	AutoFillData & d = Data();
	const string index_path = WED_LiveryIndexDefaultPath();
	if (index_path.empty() || !d.index.EnsureLoaded(index_path))
	{
		plan.error = "There is no livery index in this X-Plane folder. Static aircraft need X-Plane 12.5 or later.";
		return plan;
	}
	d.index.WaitForHubs();
	if (!d.directory.IsLoaded()) d.directory.EnsureLoaded(index_path);
	if (!d.airports.IsLoaded() && !d.airports.LoadFailed())
		d.airports.EnsureLoaded(WedDataFileDir() + "WED_AirportDatabase.txt");

	// The ICAO metadata before the airport ID: a placeholder ID with a real code
	// in its metadata is common, and the database is keyed by the real code.
	string ident;
	apt->GetICAO(ident);
	string meta = apt->ContainsMetaDataKey("icao_code") ? apt->GetMetaDataValue("icao_code") : string();
	plan.icao = Upper(!meta.empty() ? meta : ident);
	if (!d.airports.GetAirlines(plan.icao, plan.recommended) && Upper(ident) != plan.icao)
		d.airports.GetAirlines(Upper(ident), plan.recommended);
	if (!d.airports.GetCountry(plan.icao, plan.country))
		d.airports.GetCountry(Upper(ident), plan.country);
	for (size_t i = 0; i < plan.recommended.size(); ++i) plan.recommended[i] = Upper(plan.recommended[i]);

	// The airport country's own military and government operators, fleet order.
	vector<string> home_forces;
	if (!plan.country.empty())
	{
		vector<const WED_AirlineDirectoryEntry *> here;
		d.directory.GetByCountry(plan.country, here);
		for (size_t i = 0; i < here.size(); ++i)
			if (here[i]->op_class == WED_AirlineDirectoryEntry::op_Military ||
				here[i]->op_class == WED_AirlineDirectoryEntry::op_Gov)
				home_forces.push_back(Upper(here[i]->code));
	}

	vector<WED_RampPosition *> ramps;
	CollectRamps(apt, ramps);

	for (size_t r = 0; r < ramps.size(); ++r)
	{
		WED_RampPosition * ramp = ramps[r];
		WED_AutoFillRamp out;
		out.ramp = ramp;
		ramp->GetName(out.name);
		out.airlines_before = out.airlines_after = ramp->GetAirlines();

		int type = ramp->GetType();
		int op   = ramp->GetRampOperationType();
		if (type != atc_Ramp_Gate && type != atc_Ramp_TieDown)
			out.skipped = "not a gate or tie-down";
		else if (op == ramp_operation_None)
			out.skipped = "operation type None - no static aircraft";

		if (!out.skipped.empty()) { plan.ramps.push_back(out); continue; }

		// Weights: keep the author's; convert a legacy single letter.
		int w[6];
		if (!ramp->GetClassWeights(w))
		{
			WED_LegacyClassWeights(ENUM_Export(ramp->GetWidth()), w);
			out.set_weights = true;
			for (int k = 0; k < 6; ++k) out.weights[k] = w[k];
		}
		bool classes[6];
		for (int k = 0; k < 6; ++k) classes[k] = w[k] > 0;

		set<int> equipment;
		ramp->GetEquipment(equipment);
		Point2 here;
		ramp->GetLocation(gis_Geo, here);

		// Candidates by operation type; GA takes none (see the header).
		const vector<string> * candidates = nullptr;
		if (op == ramp_operation_Airline || op == ramp_operation_Cargo)	candidates = &plan.recommended;
		else if (op == ramp_operation_Military)							candidates = &home_forces;

		std::istringstream have_ss(out.airlines_before);
		set<string> have;
		string tok;
		while (have_ss >> tok) have.insert(Upper(tok));

		string after = out.airlines_after;
		if (candidates)
			for (size_t c = 0; c < candidates->size(); ++c)
			{
				const string & code = (*candidates)[c];
				if (have.count(code) || WED_IsGenericAirlinerCode(code)) continue;
				if (!OperatorIs(d, code, op)) continue;
				if (!HasFittingLivery(d, code, classes, equipment, plan.country, here.y(), here.x())) continue;

				string lower = code;
				for (size_t k = 0; k < lower.size(); ++k) lower[k] = (char) tolower((unsigned char) lower[k]);
				string next = after.empty() ? lower : after + " " + lower;
				if (next.size() > kMaxAirlinesChars) break;
				after = next;
				have.insert(code);
				out.added.push_back(code);
			}
		out.airlines_after = WED_RampPosition::CorrectAirlinesString(after);

		if (!out.Changes())
			out.skipped = candidates && candidates->empty()
				? (op == ramp_operation_Military ? "no military operator of this country has a livery" : "no recommendation for this airport")
				: "nothing to add";
		else
			++plan.changed;
		plan.ramps.push_back(out);
	}
	return plan;
}

int		WED_ApplyLiveryAutoFill(const WED_AutoFillPlan & plan)
{
	if (!plan.airport || plan.changed == 0) return 0;
	WED_Archive * archive = plan.airport->GetArchive();
	archive->StartCommand("Auto-fill Static Aircraft");
	int n = 0;
	for (size_t i = 0; i < plan.ramps.size(); ++i)
	{
		const WED_AutoFillRamp & r = plan.ramps[i];
		if (!r.Changes()) continue;
		if (r.set_weights) r.ramp->SetClassWeights(r.weights);
		if (!r.added.empty()) r.ramp->SetAirlines(r.airlines_after);
		r.ramp->SetAutoFilled(true);			// last: the setters above clear it
		++n;
	}
	archive->CommitCommand();
	return n;
}

string	WED_DescribeAutoFill(const WED_AutoFillPlan & plan)
{
	std::ostringstream o;
	if (!plan.error.empty()) { o << plan.error; return o.str(); }
	o << plan.icao << (plan.country.empty() ? "" : " (" + plan.country + ")") << ": "
	  << plan.changed << " of " << plan.ramps.size() << " ramp starts would change; "
	  << plan.recommended.size() << " recommended operators.\n";
	for (size_t i = 0; i < plan.ramps.size(); ++i)
	{
		const WED_AutoFillRamp & r = plan.ramps[i];
		o << "  " << r.name << ": ";
		if (!r.Changes()) { o << "unchanged (" << r.skipped << ")\n"; continue; }
		if (r.set_weights)
		{
			o << "weights";
			for (int k = 0; k < 6; ++k) o << ' ' << r.weights[k];
			o << (r.added.empty() ? "" : "; ");
		}
		if (!r.added.empty())
		{
			o << "adds";
			for (size_t k = 0; k < r.added.size(); ++k) o << ' ' << r.added[k];
		}
		o << "\n";
	}
	return o.str();
}

int		WED_CanLiveryAutoFill(IResolver * resolver)
{
	return WED_GetCurrentAirport(resolver) != nullptr;
}

void	WED_DoLiveryAutoFill(IResolver * resolver)
{
	WED_Airport * apt = WED_GetCurrentAirport(resolver);
	if (!apt) return;

	WED_AutoFillPlan plan = WED_PlanLiveryAutoFill(apt);
	string text = WED_DescribeAutoFill(plan);
	LOG_MSG("I/AutoFill %s", text.c_str());

	if (!plan.error.empty())		{ DoUserAlert(plan.error.c_str()); return; }
	if (plan.changed == 0)
	{
		DoUserAlert(("Nothing to add for " + plan.icao + ". Details are in WED_Log.txt.").c_str());
		return;
	}

	// A dialog cannot show two hundred ramps: the first lines, and the log has all.
	const size_t kLines = 22;
	size_t pos = 0;
	for (size_t n = 0; n < kLines && pos != string::npos; ++n)
		pos = text.find('\n', pos + 1);
	if (pos != string::npos) text = text.substr(0, pos) + "\n  ... (the full list is in WED_Log.txt)";

	if (ConfirmMessage((text + "\n\nApply? One undo step reverts all of it.").c_str(), "Apply", "Cancel"))
		WED_ApplyLiveryAutoFill(plan);
}
