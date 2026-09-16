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

#include "WED_AirportDatabase.h"
#include "WED_MandatoryHeader.h"
#include "WED_IocCountryCodes.h"
#include <fstream>

using std::string;
using std::vector;

WED_AirportDatabase::WED_AirportDatabase() :
	mLoadAttempted(false),
	mLoaded(false)
{
}

bool	WED_AirportDatabase::EnsureLoaded(const string & db_path)
{
	if (mLoaded) return true;
	if (mLoadAttempted) return false;		// already tried once this session and failed - don't retry every Draw()

	mLoadAttempted = true;

	std::ifstream f(db_path.c_str());
	if (!f) return false;
	if (!CheckWedMandatoryHeader(f)) return false;		// missing/altered stamp - untrusted file, refuse it outright

	mTable.clear();

	// Format (see WED_AirportDatabase.txt for the authoritative description):
	//   line 1-2:  mandatory header, already consumed/validated above
	//   line 3+:   blank or "#"-prefixed lines are comments, ignored
	//              otherwise: <ICAO> *** <ISO_COUNTRY> *** <code>, <code>, ...
	// Any line that doesn't match that shape is skipped rather than causing a
	// parse failure - a malformed row should never prevent every OTHER row in
	// the file from loading, let alone crash WED.
	string line;
	while (std::getline(f, line))
	{
		if (!line.empty() && line.back() == '\r') line.pop_back();	// tolerate a CRLF-saved file

		size_t p0 = line.find_first_not_of(" \t");
		if (p0 == string::npos) continue;			// blank line
		if (line[p0] == '#') continue;				// comment line

		size_t p1 = line.find_first_of(" \t", p0);
		if (p1 == string::npos) continue;			// no room for anything after the ICAO - malformed
		string icao = line.substr(p0, p1 - p0);

		size_t p2 = line.find_first_not_of(" \t", p1);
		if (p2 == string::npos) continue;
		size_t p3 = line.find_first_of(" \t", p2);
		string marker1 = line.substr(p2, p3 == string::npos ? string::npos : p3 - p2);
		if (marker1 != "***") continue;			// missing first sentinel - skip defensively
		if (p3 == string::npos) continue;			// sentinel present but no country/airlines follow

		size_t p4 = line.find_first_not_of(" \t", p3);
		if (p4 == string::npos) continue;			// nothing after the first "***"
		size_t p5 = line.find_first_of(" \t", p4);
		string raw_country = line.substr(p4, p5 == string::npos ? string::npos : p5 - p4);
		if (raw_country.empty()) continue;

		vector<string> airlines;
		bool has_airline_section = false;
		if (p5 != string::npos)
		{
			size_t p6 = line.find_first_not_of(" \t", p5);
			if (p6 != string::npos)
			{
				size_t p7 = line.find_first_of(" \t", p6);
				string marker2 = line.substr(p6, p7 == string::npos ? string::npos : p7 - p6);
				if (marker2 == "***")
				{
					has_airline_section = true;
					if (p7 != string::npos)
					{
						string rest = line.substr(p7);
						size_t pos = rest.find_first_not_of(" \t");
						while (pos != string::npos && pos < rest.size())
						{
							size_t comma = rest.find(',', pos);
							string code = rest.substr(pos, comma == string::npos ? string::npos : comma - pos);
							size_t a = code.find_first_not_of(" \t");
							size_t b = code.find_last_not_of(" \t");
							if (a != string::npos)
								airlines.push_back(code.substr(a, b - a + 1));
							if (comma == string::npos) break;
							pos = rest.find_first_not_of(" \t", comma + 1);
						}
					}
				}
			}
		}
		if (!has_airline_section) continue;		// missing second sentinel - skip defensively

		if (icao.empty()) continue;

		// Normalize the country once, here, the same way the apt.dat-backed
		// index this class replaces used to (see WED_IocCountryCodes.h) - an
		// ICAO-prefix override (currently just Kosovo/Hong Kong) always wins
		// over whatever the raw field says.
		string icao_upper;
		for (string::const_iterator c = icao.begin(); c != icao.end(); ++c)
			icao_upper += (char) toupper((unsigned char) *c);
		string country = IcaoPrefixIocOverride(icao_upper);
		if (country.empty())
			country = NormalizeCountryToIoc(raw_country);

		// "<NA>" is a literal sentinel (see WED_AirportDatabase.txt's header)
		// meaning "confirmed, after actually checking, zero scheduled
		// service" - store it as an EMPTY airline list plus na_confirmed=true,
		// which IsCommercial() below treats as an override to "not commercial",
		// rather than as a garbage one-element "airline" named "<NA>".
		bool is_confirmed_none = (airlines.size() == 1 && airlines[0] == "<NA>");
		if (is_confirmed_none)
			airlines.clear();

		Entry entry;
		entry.country = country;
		entry.airlines = airlines;
		entry.na_confirmed = is_confirmed_none;
		mTable[icao] = entry;
	}

	mLoaded = true;		// true even if the file had zero data rows - "loaded, just empty" is a valid state
	return true;
}

bool	WED_AirportDatabase::IsCommercial(const string & icao) const
{
	if (!mLoaded) return false;
	std::unordered_map<string, Entry>::const_iterator it = mTable.find(icao);
	if (it == mTable.end()) return false;
	return !it->second.na_confirmed;
}

bool	WED_AirportDatabase::GetCountry(const string & icao, string & out_country) const
{
	if (!mLoaded) return false;
	std::unordered_map<string, Entry>::const_iterator it = mTable.find(icao);
	if (it == mTable.end()) return false;
	if (it->second.country.empty()) return false;
	out_country = it->second.country;
	return true;
}

bool	WED_AirportDatabase::GetAirlines(const string & icao, vector<string> & out_airlines) const
{
	if (!mLoaded) return false;
	std::unordered_map<string, Entry>::const_iterator it = mTable.find(icao);
	if (it == mTable.end()) return false;
	out_airlines = it->second.airlines;
	return true;
}
