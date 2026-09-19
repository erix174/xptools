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

	string Trim(const string & s)
	{
		size_t b = s.find_first_not_of(" \t");
		if (b == string::npos) return string();
		size_t e = s.find_last_not_of(" \t");
		return s.substr(b, e - b + 1);
	}

	// Splits a record on "***" separators, tolerating any run of spaces or tabs
	// around them - the same rule WED_LiveryIndex.cpp's SplitOnStars applies to
	// livery rows. The operator section of livery_index.txt is TAB-aligned (the
	// generator lays it out for a tab width of 4) while the livery rows are
	// space-padded, so a reader that matches a literal " *** " - as this one did
	// until 2026-09-19 - drops every operator record without a word.
	void SplitOnStars(const string & line, vector<string> & out)
	{
		out.clear();
		size_t pos = 0;
		while (true)
		{
			size_t star = line.find("***", pos);
			if (star == string::npos)
			{
				out.push_back(Trim(line.substr(pos)));
				return;
			}
			out.push_back(Trim(line.substr(pos, star - pos)));
			pos = star + 3;
		}
	}
}

WED_AirlineDirectory::WED_AirlineDirectory() :
	mLoadError(wed_data_ok),
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
	if (!f)                          { mLoadError = wed_data_no_file;   return false; }
	if (!CheckWedMandatoryHeader(f)) { mLoadError = wed_data_bad_header; return false; }		// missing/altered stamp - untrusted file, refuse it outright

	// Every container, not just the two the parse loop fills. mByCountry and
	// mSortedByFleet are derived below; leaving them populated here is harmless
	// only because mLoadAttempted makes this function single-shot today, and that
	// is exactly the kind of assumption a later reload path would break silently.
	mEntries.clear();
	mByCode.clear();
	mByCountry.clear();
	mSortedByFleet.clear();

	// Format (see WED_AirlineDirectory.h for the full description, including
	// what the CODE column's 3/4/5-char length means):
	//   line 1-2:  mandatory header, already consumed/validated above
	//   line 3+:   blank or "#"-prefixed lines are comments, ignored
	//              otherwise: OPERATOR *** <CODE> *** <Name> *** <IOC country> *** <Pax|Cargo|GA|Military|Gov> *** <Fleet count> *** <hub ICAOs>
	// Cells are split on "***" and trimmed of spaces and tabs (see SplitOnStars
	// above) - the whitespace between cells is layout, never data.
	// Any line that doesn't match that shape is skipped rather than causing a
	// parse failure - a malformed row should never prevent every OTHER row in
	// the file from loading, let alone crash WED.
	string line;
	vector<string> cells;
	while (std::getline(f, line))
	{
		if (!line.empty() && line.back() == '\r') line.pop_back();		// tolerate a CRLF-saved file

		size_t p0 = line.find_first_not_of(" \t");
		if (p0 == string::npos) continue;			// blank line
		if (line[p0] == '#') continue;				// comment line

		SplitOnStars(line, cells);

		// livery_index.txt (schema 3) carries the record with an "OPERATOR" tag in
		// front, so one file serves both readers - see the generator. The tag is
		// dropped and the rest parses exactly as a directory row does. A livery
		// row in that file never survives the service-column check below (its
		// fourth cell is a registration), so nothing else has to change.
		if (!cells.empty() && cells[0] == "OPERATOR") cells.erase(cells.begin());
		if (cells.size() < 5) continue;

		const string & code    = cells[0];
		const string & name    = cells[1];
		const string & country = cells[2];
		const string & service = cells[3];
		WED_AirlineDirectoryEntry::OpClass op;
		if      (service == "Pax")      op = WED_AirlineDirectoryEntry::op_Pax;
		else if (service == "Cargo")    op = WED_AirlineDirectoryEntry::op_Cargo;
		else if (service == "GA")       op = WED_AirlineDirectoryEntry::op_GA;
		else if (service == "Military") op = WED_AirlineDirectoryEntry::op_Military;
		else if (service == "Gov")      op = WED_AirlineDirectoryEntry::op_Gov;
		else continue;										// not a documented value - malformed

		// cells[5], the hub ICAOs, is for the generator, not for WED - the
		// coordinates it resolves to arrive on the livery rows of the same file.
		const string & fleet_str = cells[4];

		if (code.empty() || name.empty() || country.empty() || fleet_str.empty()) continue;

		WED_AirlineDirectoryEntry e;
		e.code     = code;
		e.name     = name;
		e.country  = ToUpper(country);
		e.is_cargo = (service == "Cargo");
		e.op_class = op;
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
