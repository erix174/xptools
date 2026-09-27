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

#include "WED_LiveryRules.h"
#include "WED_LiveryIndex.h"
#include "WED_AirlineDirectory.h"
#include "WED_EnumSystem.h"
#include "WED_MandatoryHeader.h"		// WedDataFileDir
#include <memory>

WED_LiveryIndex &	WED_SharedLiveryIndex(void)
{
	static WED_LiveryIndex idx;
	return idx;
}

WED_LiveryData &	WED_SharedLiveryData(void)
{
	// Rebuilt whole when the X-Plane folder changes: the operator directory loads
	// once per instance and would otherwise keep the old install's operators.
	static std::unique_ptr<WED_LiveryData> d;
	static std::string d_path;
	const std::string index_path = WED_LiveryIndexDefaultPath();
	if (!d || d_path != index_path) { d.reset(new WED_LiveryData); d_path = index_path; }
	return *d;
}

WED_LiveryData *	WED_GetLiveryData(bool need_hubs)
{
	WED_LiveryData * d = &WED_SharedLiveryData();
	const std::string index_path = WED_LiveryIndexDefaultPath();
	if (index_path.empty() || !d->index.EnsureLoaded(index_path)) return NULL;
	if (need_hubs) d->index.WaitForHubs();
	if (!d->directory.IsLoaded() && !d->directory.LoadFailed()) d->directory.EnsureLoaded(index_path);
	if (!d->airports.IsLoaded() && !d->airports.LoadFailed())
		d->airports.EnsureLoaded(WedDataFileDir() + "WED_AirportDatabase.txt");
	return d;
}

using std::string;

WED_LiveryAllow	WED_LiveryAllowedAt(const WED_LiveryIndexEntry & e,
									const WED_AirlineDirectory & directory,
									const string & airport_country,
									double stand_lat, double stand_lon)
{
	const string & code = e.airline;

	WED_AirlineDirectoryEntry d;
	bool known = directory.Lookup(code, d);

	bool is_ga  = code == "XPGA" || (known && d.op_class == WED_AirlineDirectoryEntry::op_GA);
	bool is_mil = code == "XPMI" || (known && (d.op_class == WED_AirlineDirectoryEntry::op_Military ||
											   d.op_class == WED_AirlineDirectoryEntry::op_Gov));

	// General aviation parks anywhere: a private turboprop's "hub" is wherever
	// its owner lives, and measuring it would filter out exactly the aircraft
	// that turn up at every small field on earth.
	if (is_ga) return livery_allow_Yes;

	// Military and government: anywhere by default - an F-15 or a Seahawk at a
	// foreign base is unremarkable - and never range-checked (no country is wide
	// enough for range to matter at home). A HOME row is equipment that names
	// one operator so specifically it has no business abroad (a head-of-state
	// 757, an air force's own-marked airliner): its country must be the
	// airport's. Fail open when either country is unknown - then nothing about
	// the stand is known, and the readout already says so.
	if (is_mil)
	{
		if (!e.home_only) return livery_allow_Yes;
		string home = (known && !d.country.empty()) ? d.country : e.reg_country;
		if (home.empty() || airport_country.empty()) return livery_allow_Yes;
		return home == airport_country ? livery_allow_Yes : livery_allow_ForeignMilitary;
	}

	return WED_LiveryInRange(e, stand_lat, stand_lon) ? livery_allow_Yes : livery_allow_OutOfRange;
}

WED_LiveryAllow	WED_LiveryFitsStand(const WED_LiveryIndexEntry & e,
									const WED_AirlineDirectory & directory,
									const string & airport_country,
									double stand_lat, double stand_lon,
									const std::set<int> & equipment)
{
	int eq = WED_LiveryEquipment(e);
	if (eq != -1 && !equipment.empty() && !equipment.count(eq)) return livery_allow_Equipment;
	return WED_LiveryAllowedAt(e, directory, airport_country, stand_lat, stand_lon);
}

bool	WED_LiveryOperatorFitsRampOp(const string & code_uc, int ramp_op, const WED_AirlineDirectory & directory)
{
	if (ramp_op == ramp_operation_None) return false;
	if (code_uc == "XPGA") return ramp_op == ramp_operation_GeneralAviation;
	if (code_uc == "XPMI") return ramp_op == ramp_operation_Military;
	if (WED_IsGenericAirlinerCode(code_uc)) return ramp_op == ramp_operation_Airline || ramp_op == ramp_operation_Cargo;
	WED_AirlineDirectoryEntry e;
	if (!directory.Lookup(code_uc, e)) return ramp_op == ramp_operation_Airline;
	switch (e.op_class) {
	case WED_AirlineDirectoryEntry::op_Pax:		return ramp_op == ramp_operation_Airline;
	case WED_AirlineDirectoryEntry::op_Cargo:	return ramp_op == ramp_operation_Cargo;
	case WED_AirlineDirectoryEntry::op_GA:		return ramp_op == ramp_operation_GeneralAviation;
	case WED_AirlineDirectoryEntry::op_Military:
	case WED_AirlineDirectoryEntry::op_Gov:		return ramp_op == ramp_operation_Military;
	}
	return true;
}

int		WED_LiveryEquipment(const WED_LiveryIndexEntry & e)
{
	const string & p = e.obj_path;
	string top = p.substr(0, p.find_first_of("/\\"));
	if (top == "heavy")		return atc_Heavies;
	if (top == "jet")		return atc_Jets;
	if (top == "turboprop")	return atc_Turbos;
	if (top == "prop")		return atc_Props;
	if (top == "helo")		return atc_Helicopters;
	if (top == "fighter")	return atc_Fighters;
	return -1;
}

void	WED_LegacyStepDownWeights(int top_class, int out_w[6])
{
	if (top_class < 0 || top_class > 5) top_class = 2;
	for (int k = 0; k < 6; ++k) out_w[k] = 0;
	double left = 1.0;
	int sum = 0;
	for (int k = top_class; k >= 1; --k)
	{
		out_w[k] = (int) (left * 0.75 * 1000.0 + 0.5);
		sum += out_w[k];
		left *= 0.25;
	}
	out_w[0] = 1000 - sum;					// A takes the remainder
}

void	WED_LegacyClassWeights(int size_class, int out_w[6])
{
	static const int kTable[6][6] = {
		// A    B    C    D    E    F
		{ 100,   0,   0,   0,   0,   0 },	// A - A to A, in the old system too
		{  30,  70,   0,   0,   0,   0 },	// B
		{   0,  30,  70,   0,   0,   0 },	// C - the A320 / 737 field
		{   0,  10,  40,  50,   0,   0 },	// D
		{   0,   0,  10,  30,  60,   0 },	// E
		{   0,   0,   0,  10,  50,  40 },	// F
	};
	if (size_class < 0 || size_class > 5) size_class = 2;
	for (int k = 0; k < 6; ++k) out_w[k] = kTable[size_class][k];
}
