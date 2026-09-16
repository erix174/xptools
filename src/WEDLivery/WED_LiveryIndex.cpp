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

#include "WED_LiveryIndex.h"

#include "WED_MandatoryHeader.h"
#include "WED_PackageMgr.h"
#include "PlatformUtils.h"

#include <fstream>
#include <algorithm>

using std::string;
using std::vector;

// The note that means "this livery has no annotation" - written out rather than
// left blank so a row always has seven fields and an empty cell always means
// missing data.
static const char * kDefaultNote = "Default";

static string ToUpper(const string & s)
{
	string r(s);
	for (size_t i = 0; i < r.size(); ++i)
		r[i] = toupper((unsigned char) r[i]);
	return r;
}

static string Trim(const string & s)
{
	size_t b = s.find_first_not_of(" \t");
	if (b == string::npos) return string();
	size_t e = s.find_last_not_of(" \t");
	return s.substr(b, e - b + 1);
}

// Splits a row on "***" SEPARATORS, tolerating any run of spaces or tabs around
// them. Matching a literal " *** " would work today but breaks the moment the
// file is column-padded for readability - which it is, deliberately. Empty cells
// are preserved: a livery with no registration legitimately has two blank fields.
static void SplitOnStars(const string & line, vector<string> & out)
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

WED_LiveryIndex::WED_LiveryIndex() :
	mLoadAttempted(false),
	mLoaded(false)
{
}

bool	WED_LiveryIndex::EnsureLoaded(const string & index_path)
{
	if (mLoaded) return true;
	if (mLoadAttempted) return false;	// tried once this session and failed - don't retry every Draw()

	mLoadAttempted = true;

	if (index_path.empty()) return false;	// no X-Plane folder selected yet

	std::ifstream f(index_path.c_str());
	if (!f) return false;
	if (!CheckWedMandatoryHeader(f)) return false;	// missing/altered stamp - untrusted file, refuse outright

	mEntries.clear();
	mByAirline.clear();
	mByKey.clear();

	// Rows are parsed best-effort: anything that doesn't have seven fields, or is
	// missing the two that identify it, is skipped. A malformed row must never
	// prevent every OTHER row from loading - same policy as WED_AirportDatabase.
	vector<string> cells;
	string line;
	vector<WED_LiveryIndexEntry> parsed;

	while (std::getline(f, line))
	{
		if (!line.empty() && line.back() == '\r') line.pop_back();	// tolerate a CRLF-saved file

		size_t p0 = line.find_first_not_of(" \t");
		if (p0 == string::npos) continue;		// blank
		if (line[p0] == '#') continue;			// comment

		SplitOnStars(line, cells);
		if (cells.size() < 7) continue;			// not a data row

		WED_LiveryIndexEntry e;
		e.type        = ToUpper(cells[0]);
		string cls    = ToUpper(cells[1]);
		e.size_class  = (cls.size() == 1 && cls[0] >= 'A' && cls[0] <= 'F') ? cls[0] : 0;
		e.airline     = ToUpper(cells[2]);
		e.reg         = ToUpper(cells[3]);
		e.reg_country = ToUpper(cells[4]);
		e.note        = cells[5];				// free text - case preserved for display
		e.obj_path    = cells[6];

		// "????" is the generator's TODO marker, not a value. A row still
		// carrying one is unfinished data; skip it rather than surfacing a
		// nonsense airline or type in the UI.
		if (e.type.empty() || e.airline.empty() || e.obj_path.empty()) continue;
		if (e.type == "????" || e.airline == "????") continue;

		if (e.note.empty()) e.note = kDefaultNote;

		parsed.push_back(e);
	}

	// Build the indices only after the vector has stopped growing - it holds the
	// entries by value, so pointers taken during the parse loop would dangle on
	// the next reallocation.
	mEntries.swap(parsed);
	for (size_t i = 0; i < mEntries.size(); ++i)
	{
		const WED_LiveryIndexEntry * e = &mEntries[i];
		mByAirline[e->airline].push_back(e);
		// First writer wins on a duplicate key. Duplicates shouldn't exist, but
		// the resource library does ship byte-identical assets under two paths,
		// so don't assume.
		mByKey.insert(std::make_pair(WED_MakeLiveryKey(e->type, e->airline, e->note), e));
	}

	mLoaded = true;
	return true;
}

const vector<const WED_LiveryIndexEntry *> *
		WED_LiveryIndex::GetForAirline(const string & airline_code) const
{
	std::unordered_map<string, vector<const WED_LiveryIndexEntry *> >::const_iterator i =
		mByAirline.find(ToUpper(airline_code));
	return i == mByAirline.end() ? NULL : &i->second;
}

void	WED_LiveryIndex::GetForAirlineAndClass(const string & airline_code,
											   char size_class,
											   vector<const WED_LiveryIndexEntry *> & out) const
{
	out.clear();
	const vector<const WED_LiveryIndexEntry *> * all = GetForAirline(airline_code);
	if (!all) return;
	for (size_t i = 0; i < all->size(); ++i)
	{
		// An unknown class (0 - the data file had "?" there) is treated as not
		// fitting anything specific. Better to leave such a row out of a sized
		// query than to guess and put an A380 on a class-B stand.
		if (size_class != 0 && (*all)[i]->size_class != size_class) continue;
		out.push_back((*all)[i]);
	}
}

WED_LiveryIndex::Availability
		WED_LiveryIndex::GetAvailability(const string & airline_code,
										 char size_class,
										 bool known_airline) const
{
	vector<const WED_LiveryIndexEntry *> hits;
	GetForAirlineAndClass(airline_code, size_class, hits);
	if (!hits.empty()) return livery_Hit;

	// Nothing that fits. If the code is a real airline this is simply a gap in
	// X-Plane's asset set - the author can still pick it. Only a code that
	// nothing anywhere recognises is an error.
	return known_airline ? livery_Ignore : livery_Faulty;
}

void	WED_LiveryIndex::GetForAirlineAndType(const string & airline_code,
											  const string & type,
											  vector<const WED_LiveryIndexEntry *> & out) const
{
	out.clear();
	const vector<const WED_LiveryIndexEntry *> * all = GetForAirline(airline_code);
	if (!all) return;
	string want = ToUpper(type);
	for (size_t i = 0; i < all->size(); ++i)
		if ((*all)[i]->type == want)
			out.push_back((*all)[i]);
}

const WED_LiveryIndexEntry * WED_LiveryIndex::Lookup(const string & key) const
{
	std::unordered_map<string, const WED_LiveryIndexEntry *>::const_iterator i = mByKey.find(key);
	return i == mByKey.end() ? NULL : i->second;
}

string	WED_MakeLiveryKey(const string & type, const string & airline, const string & note)
{
	string n = note.empty() ? string(kDefaultNote) : note;
	// apt.dat is whitespace-delimited, so the key must not contain spaces - a
	// note of "Pink Peony" has to travel as "Pink_Peony" and come back the same
	// way. WED_LiveryDisplayName() reverses this for the UI.
	for (size_t i = 0; i < n.size(); ++i)
		if (n[i] == ' ' || n[i] == '\t') n[i] = '_';
	return ToUpper(type) + "_" + ToUpper(airline) + "_" + n;
}

string	WED_LiveryDisplayName(const string & airline_name, const string & note)
{
	if (note.empty() || note == kDefaultNote) return airline_name;

	string n(note);
	for (size_t i = 0; i < n.size(); ++i)
		if (n[i] == '_') n[i] = ' ';

	if (airline_name.empty()) return n;
	return airline_name + " (" + n + ")";
}

string	WED_LiveryIndexDefaultPath(void)
{
	if (gPackageMgr == NULL) return string();

	string root;
	if (!gPackageMgr->GetXPlaneFolder(root)) return string();	// nothing selected yet
	if (root.empty()) return string();

	// SetXPlaneFolder() already refused any root without "Resources/default
	// scenery", so this is guaranteed to be a real directory whenever
	// HasSystemFolder() is true - no need to re-validate the prefix here.
	return root + DIR_STR "Resources" DIR_STR "default scenery" DIR_STR
				  "sim objects" DIR_STR "apt_aircraft" DIR_STR "livery_index.txt";
}
