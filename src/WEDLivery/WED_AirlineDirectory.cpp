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

#include "WED_AirlineDirectory.h"
#include "WED_MandatoryHeader.h"
#include <fstream>
#include <algorithm>
#include <cctype>
#include <cstdlib>

using std::string;
using std::vector;

namespace
{
	string ToUpper(const string & s)
	{
		string r = s;
		for (string::iterator c = r.begin(); c != r.end(); ++c)
			*c = (char) toupper((unsigned char) *c);
		return r;
	}

	bool ByFleetDescending(const WED_AirlineDirectoryEntry & a, const WED_AirlineDirectoryEntry & b)
	{
		return a.fleet > b.fleet;
	}
}

WED_AirlineDirectory::WED_AirlineDirectory() :
	mLoadAttempted(false),
	mLoaded(false)
{
}

bool	WED_AirlineDirectory::EnsureLoaded(const string & db_path)
{
	if (mLoaded) return true;
	if (mLoadAttempted) return false;		// already tried once this session and failed - don't retry every Draw()

	mLoadAttempted = true;

	std::ifstream f(db_path.c_str());
	if (!f) return false;
	if (!CheckWedMandatoryHeader(f)) return false;		// missing/altered stamp - untrusted file, refuse it outright

	mEntries.clear();
	mByCode.clear();

	// Format (see WED_AirlineDirectory.h for the full description, including
	// what the CODE column's 3/4/5-char length means):
	//   line 1-2:  mandatory header, already consumed/validated above
	//   line 3+:   blank or "#"-prefixed lines are comments, ignored
	//              otherwise: <CODE> *** <Name> *** <IOC country> *** <Pax|Cargo> *** <Fleet count>
	// Any line that doesn't match that shape is skipped rather than causing a
	// parse failure - a malformed row should never prevent every OTHER row in
	// the file from loading, let alone crash WED.
	const string kSep = " *** ";
	string line;
	while (std::getline(f, line))
	{
		if (!line.empty() && line.back() == '\r') line.pop_back();		// tolerate a CRLF-saved file

		size_t p0 = line.find_first_not_of(" \t");
		if (p0 == string::npos) continue;			// blank line
		if (line[p0] == '#') continue;				// comment line

		size_t sep1 = line.find(kSep, p0);
		if (sep1 == string::npos) continue;
		string code = line.substr(p0, sep1 - p0);

		size_t name_start = sep1 + kSep.size();
		size_t sep2 = line.find(kSep, name_start);
		if (sep2 == string::npos) continue;
		string name = line.substr(name_start, sep2 - name_start);

		size_t country_start = sep2 + kSep.size();
		size_t sep3 = line.find(kSep, country_start);
		if (sep3 == string::npos) continue;
		string country = line.substr(country_start, sep3 - country_start);

		size_t service_start = sep3 + kSep.size();
		size_t sep4 = line.find(kSep, service_start);
		if (sep4 == string::npos) continue;
		string service = line.substr(service_start, sep4 - service_start);
		if (service != "Pax" && service != "Cargo") continue;		// not one of the two documented values - malformed

		size_t fleet_start = sep4 + kSep.size();
		string fleet_str = line.substr(fleet_start);
		size_t fe = fleet_str.find_last_not_of(" \t");
		if (fe == string::npos) continue;
		fleet_str = fleet_str.substr(0, fe + 1);

		if (code.empty() || name.empty() || country.empty() || fleet_str.empty()) continue;

		WED_AirlineDirectoryEntry e;
		e.code     = code;
		e.name     = name;
		e.country  = ToUpper(country);
		e.is_cargo = (service == "Cargo");
		e.fleet    = atoi(fleet_str.c_str());

		mByCode[ToUpper(code)] = (int) mEntries.size();
		mByCountry[e.country].push_back((int) mEntries.size());
		mEntries.push_back(e);
	}

	mSortedByFleet = mEntries;
	std::sort(mSortedByFleet.begin(), mSortedByFleet.end(), ByFleetDescending);

	mLoaded = true;		// true even if the file had zero data rows - "loaded, just empty" is a valid state
	return true;
}

bool	WED_AirlineDirectory::Lookup(const string & code, WED_AirlineDirectoryEntry & out_entry) const
{
	if (!mLoaded) return false;

	std::unordered_map<string, int>::const_iterator it = mByCode.find(ToUpper(code));
	if (it == mByCode.end()) return false;

	out_entry = mEntries[it->second];
	return true;
}

string	WED_AirlineDirectory::GetName(const string & code) const
{
	WED_AirlineDirectoryEntry e;
	if (!Lookup(code, e)) return string();
	return e.name;
}

void	WED_AirlineDirectory::GetByCountry(const string & ioc_country, vector<const WED_AirlineDirectoryEntry *> & out) const
{
	out.clear();
	if (!mLoaded) return;

	std::unordered_map<string, vector<int>>::const_iterator it = mByCountry.find(ToUpper(ioc_country));
	if (it == mByCountry.end()) return;

	out.reserve(it->second.size());
	for (size_t i = 0; i < it->second.size(); ++i)
		out.push_back(&mEntries[it->second[i]]);
}
