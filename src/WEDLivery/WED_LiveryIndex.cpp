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
#include <cmath>

#include "WED_MandatoryHeader.h"
#include "WED_PackageMgr.h"
#include "PlatformUtils.h"
#include "MemFileUtils.h"		// MF_GetFileType, for the X-Plane root check
#include "WED_LibraryMgr.h"		// WED_clean_rpath - separator normalisation, see WED_LiveryObjectPath()

#include <fstream>
#include <sstream>
#include <algorithm>
#include <chrono>
#include <map>
#include <cstring>			// strncmp / strlen / memchr

using std::string;
using std::vector;

// The note that means "this livery has no annotation" - written out rather than
// left blank so a row always has seven fields and an empty cell always means
// missing data.
static const char * kDefaultNote = "Default";

// The ONE reserved value in an otherwise free-text, open-vocabulary column. It
// means "never spawn this livery" (spec R25, §6.6) and it is the only note any
// reader interprets rather than displays. Everything else in this column is a
// caption - Peony, Mixue, Peacock, Retro - and a reader that starts validating
// the column against a list silently deletes exactly the variants it exists to
// carry. Compared case-sensitively, as the generator writes it.
static const char * kObsoleteNote = "Obsolete";

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

static bool IsXPlaneRoot(const string & root);

// <X-Plane root>/Global Scenery/Global Airports/Earth nav data/apt.dat, or "".
static string GlobalAirportsAptDat(void)
{
	if (gPackageMgr == NULL) return string();
	string root;
	if (!gPackageMgr->GetXPlaneFolder(root) || !IsXPlaneRoot(root)) return string();
	return root + DIR_STR "Global Scenery" DIR_STR "Global Airports" DIR_STR "Earth nav data" DIR_STR "apt.dat";
}

// Splits [b, e) on spaces and tabs into at most max_tok tokens.
static void SplitLine(const char * b, const char * e, vector<string> & out, size_t max_tok)
{
	out.clear();
	while (b < e && out.size() < max_tok)
	{
		while (b < e && (*b == ' ' || *b == '\t')) ++b;
		const char * t = b;
		while (b < e && *b != ' ' && *b != '\t') ++b;
		if (b > t) out.push_back(string(t, b));
	}
}

// Hub ICAO -> (lat, lon) for every code in `wanted`, read off Global Airports -
// the same airports the sim has loaded, so WED and the sim measure R26 from the
// same point. The index names hubs by ICAO on its OPERATOR records and nowhere
// carries a coordinate: a column of numbers nobody can check by eye is a column
// nobody maintains.
//
// Where an airport is: its 1302 datum, else the midpoint of its first runway.
// Which airport a code means: the one whose 1302 icao_code says so, else the one
// whose header ident is that code. The metadata wins because the ident is not
// always the ICAO code - Ezhou (ZHEC) and Chengdu Tianfu (ZUTF) are filed under
// placeholder idents, and ZSQD the ident is the closed Liuting while ZSQD the
// icao_code is the new Jiaodong.
static void ResolveHubIcaos(const string & apt_dat, const set<string> & wanted,
							std::map<string, std::pair<double,double> > & out)
{
	out.clear();
	if (wanted.empty() || apt_dat.empty()) return;
	MFMemFile * f = MemFile_Open(apt_dat.c_str());
	if (!f)
	{
		LOG_MSG("W/LiveryIndex cannot open %s - hubs unresolved, range rule off\n", apt_dat.c_str());
		return;
	}

	std::map<string, std::pair<double,double> > by_ident, by_code;
	string ident, code;
	double lat = 0, lon = 0, rlat = 0, rlon = 0;
	bool has_lat = false, has_lon = false, has_rwy = false;

	auto flush = [&]() {
		bool want_i = !ident.empty() && wanted.count(ident);
		bool want_c = !code.empty()  && wanted.count(code);
		if (!want_i && !want_c) return;
		std::pair<double,double> ll;
		if (has_lat && has_lon)	ll = std::make_pair(lat, lon);
		else if (has_rwy)		ll = std::make_pair(rlat, rlon);
		else					return;
		if (want_c && !by_code.count(code))    by_code[code] = ll;
		if (want_i && !by_ident.count(ident))  by_ident[ident] = ll;
	};

	vector<string> tok;
	const char * p = MemFile_GetBegin(f);
	const char * end = MemFile_GetEnd(f);
	while (p < end)
	{
		const char * eol = (const char *) memchr(p, '\n', end - p);
		if (!eol) eol = end;
		const char * le = eol;
		if (le > p && le[-1] == '\r') --le;

		// 1 land airport, 16 seaplane base, 17 heliport: each starts a new block.
		bool header = (le - p >= 2 && p[0] == '1' && (p[1] == ' ' || p[1] == '\t')) ||
					  (le - p >= 3 && p[0] == '1' && (p[1] == '6' || p[1] == '7') && (p[2] == ' ' || p[2] == '\t'));
		if (header)
		{
			flush();
			SplitLine(p, le, tok, 5);
			ident = tok.size() > 4 ? tok[4] : string();
			code.clear();
			has_lat = has_lon = has_rwy = false;
		}
		else if (!ident.empty() && le - p > 5 && strncmp(p, "1302 ", 5) == 0)
		{
			SplitLine(p, le, tok, 3);
			if (tok.size() == 3)
			{
				if      (tok[1] == "icao_code") code = ToUpper(tok[2]);
				else if (tok[1] == "datum_lat") { lat = atof(tok[2].c_str()); has_lat = true; }
				else if (tok[1] == "datum_lon") { lon = atof(tok[2].c_str()); has_lon = true; }
			}
		}
		else if (!ident.empty() && !has_rwy && le - p > 4 && strncmp(p, "100 ", 4) == 0)
		{
			SplitLine(p, le, tok, 20);
			if (tok.size() >= 20)
			{
				rlat = (atof(tok[9].c_str())  + atof(tok[18].c_str())) * 0.5;
				rlon = (atof(tok[10].c_str()) + atof(tok[19].c_str())) * 0.5;
				has_rwy = true;
			}
		}
		p = eol + 1;
	}
	flush();
	MemFile_Close(f);

	out.swap(by_ident);
	for (std::map<string, std::pair<double,double> >::const_iterator i = by_code.begin(); i != by_code.end(); ++i)
		out[i->first] = i->second;
}

WED_LiveryIndex::WED_LiveryIndex() :
	mUsable(0),
	mLoadAttempted(false),
	mLoaded(false)
{
	for (int k = 0; k < 6; ++k) mAtClass[k] = 0;
}

bool	WED_LiveryIndex::EnsureLoaded(const string & index_path)
{
	if (index_path != mLoadedPath)
	{
		// Different file than the one we hold - almost always because the user
		// pointed WED at another X-Plane install. Drop everything and start over.
		mLoadedPath    = index_path;
		mLoadAttempted = false;
		mLoaded        = false;
		mEntries.clear();
		mByAirline.clear();
		mByKey.clear();
		ForgetHeader();
	}

	if (mLoaded) return true;
	if (mLoadAttempted) return false;	// already tried THIS path and failed - don't re-stat every Draw()

	mLoadAttempted = true;

	if (index_path.empty()) return false;	// no X-Plane folder selected yet

	std::ifstream f(index_path.c_str());
	if (!f) return false;
	if (!CheckWedMandatoryHeader(f)) return false;	// missing/altered stamp - untrusted file, refuse outright

	mEntries.clear();
	mByAirline.clear();
	mByKey.clear();
	ForgetHeader();

	// Rows are parsed best-effort: anything that doesn't have seven fields, or is
	// missing the two that identify it, is skipped. A malformed row must never
	// prevent every OTHER row from loading - same policy as WED_AirportDatabase.
	vector<string> cells;
	string line;
	vector<WED_LiveryIndexEntry> parsed;
	std::map<string, vector<string> > op_hubs;		// operator code -> hub ICAOs

	while (std::getline(f, line))
	{
		if (!line.empty() && line.back() == '\r') line.pop_back();	// tolerate a CRLF-saved file

		size_t p0 = line.find_first_not_of(" \t");
		if (p0 == string::npos) continue;		// blank
		// Comments are skipped, but the four the generator writes at the top are
		// not decoration - they say which install this index describes. §6.4 of
		// the format spec calls an index/install mismatch the most likely way to
		// waste a day on this feature, because its only symptom is previews
		// quietly never appearing. Read them on the way past.
		if (line[p0] == '#') { NoteHeaderLine(line.c_str() + p0); continue; }

		SplitOnStars(line, cells);
		if (cells.size() < 7) continue;			// not a data row
		if (cells[0] == "OPERATOR")
		{
			// WED_AirlineDirectory reads the rest of the record; the hub ICAOs
			// are the index's, for the range rule.
			//   OPERATOR *** CODE *** NAME *** CTY *** OP *** FLEET *** HUB ICAOs
			if (cells.size() >= 7)
			{
				std::istringstream hs(cells[6]);
				string icao;
				vector<string> & dst = op_hubs[ToUpper(cells[1])];
				while (hs >> icao) dst.push_back(ToUpper(icao));
			}
			continue;
		}

		WED_LiveryIndexEntry e;
		e.type        = ToUpper(cells[0]);
		string cls    = ToUpper(cells[1]);
		e.size_class  = (cls.size() == 1 && cls[0] >= 'A' && cls[0] <= 'F') ? cls[0] : 0;
		e.airline     = ToUpper(cells[2]);
		e.reg         = ToUpper(cells[3]);
		e.reg_country = ToUpper(cells[4]);
		e.note        = cells[5];				// free text - case preserved for display
		// The path is ALWAYS the last cell, so a schema 1 row (7 cells) and a
		// schema 2 row (9) both read here. The two optional cells sit between.
		e.obj_path    = cells.back();
		if (cells.size() >= 10) e.op_class = cells[8];	// schema 3: Pax/Cargo/GA/Military/Gov, per row
		if (cells.size() >= 9)
		{
			e.range_km = atoi(cells[6].c_str());		// non-numeric -> 0 -> unknown
			// SCOPE: HOME on a military or government row, else empty. Schema 3
			// kept hub coordinates in this cell; they are ignored now, since the
			// hubs come from the OPERATOR record (below), which that file has too.
			e.home_only = (cells[7] == "HOME");
		}

		// "????" is the generator's TODO marker, not a value. A row still
		// carrying one is unfinished data; skip it rather than surfacing a
		// nonsense airline or type in the UI.
		if (e.type.empty() || e.airline.empty() || e.obj_path.empty()) continue;
		if (e.type == "????" || e.airline == "????") continue;

		if (e.note.empty()) e.note = kDefaultNote;

		parsed.push_back(e);
	}

	// Hubs, for R26: the operator's ICAOs, placed by Global Airports. A code that
	// does not resolve is dropped, not guessed, and an operator left with none is
	// never range-filtered - unknown fails open, as it always has.
	{
		set<string> wanted;
		for (std::map<string, vector<string> >::const_iterator i = op_hubs.begin(); i != op_hubs.end(); ++i)
			wanted.insert(i->second.begin(), i->second.end());
		std::map<string, std::pair<double,double> > pos;
		const string apt_dat = GlobalAirportsAptDat();
		auto t0 = std::chrono::steady_clock::now();
		ResolveHubIcaos(apt_dat, wanted, pos);
		long ms = (long) std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count();

		string missing;
		for (set<string>::const_iterator i = wanted.begin(); i != wanted.end(); ++i)
			if (!pos.count(*i)) missing += " " + *i;
		LOG_MSG("I/LiveryIndex hubs: %d of %d ICAOs placed from %s in %ld ms%s%s\n",
				(int) pos.size(), (int) wanted.size(), apt_dat.c_str(), ms,
				missing.empty() ? "" : "; not found:", missing.c_str());

		for (size_t i = 0; i < parsed.size(); ++i)
		{
			std::map<string, vector<string> >::const_iterator h = op_hubs.find(parsed[i].airline);
			if (h == op_hubs.end()) continue;
			for (size_t k = 0; k < h->second.size(); ++k)
			{
				std::map<string, std::pair<double,double> >::const_iterator p = pos.find(h->second[k]);
				if (p != pos.end()) parsed[i].hubs.push_back(p->second);
			}
		}
	}

	// Build the indices only after the vector has stopped growing - it holds the
	// entries by value, so pointers taken during the parse loop would dangle on
	// the next reallocation.
	mEntries.swap(parsed);
	mUsable = 0;
	for (int k = 0; k < 6; ++k) mAtClass[k] = 0;
	for (size_t i = 0; i < mEntries.size(); ++i)
	{
		const WED_LiveryIndexEntry * e = &mEntries[i];

		// R25: a livery marked Obsolete must never be chosen - not at stage 2,
		// not at stage 3, not in any count of what an operator can fill. The
		// cleanest way to mean "as if the row were absent" is to keep it out of
		// the lookup tables entirely: every accessor built on them inherits the
		// rule without knowing about it, and none of them can forget it.
		//
		// It stays in mEntries so the count is still answerable. "298 rows, 286
		// usable" is a diagnostic; a file that is silently shorter than its own
		// header claims is not.
		if (e->note == kObsoleteNote) continue;
		++mUsable;
		if (e->size_class >= 'A' && e->size_class <= 'F')
			++mAtClass[e->size_class - 'A'];

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

// Haversine on a 6371 km sphere. NOT LonLatDistMeters(): that is an
// equirectangular approximation for the map - it scales the raw longitude
// difference by the cosine of the mean latitude and never wraps it, so KSFO
// (-122) to Beijing (+116) came out as 238 degrees of longitude and every
// US operator failed the range check while Europe, at 116 degrees, passed.
// A great-circle over an ocean is the one job that function was never for.
static double GreatCircleKm(double lat1, double lon1, double lat2, double lon2)
{
	const double k = 0.017453292519943295;
	double p1 = lat1 * k, p2 = lat2 * k;
	double dp = (lat2 - lat1) * k, dl = (lon2 - lon1) * k;
	double a  = sin(dp * 0.5) * sin(dp * 0.5) + cos(p1) * cos(p2) * sin(dl * 0.5) * sin(dl * 0.5);
	if (a > 1.0) a = 1.0;
	return 6371.0 * 2.0 * asin(sqrt(a));
}

bool	WED_LiveryInRange(const WED_LiveryIndexEntry & e, double stand_lat, double stand_lon)
{
	if (e.range_km <= 0 || e.hubs.empty()) return true;		// unknown is never filtered
	double best = 1e12;
	for (size_t i = 0; i < e.hubs.size(); ++i)
	{
		double d = GreatCircleKm(e.hubs[i].first, e.hubs[i].second, stand_lat, stand_lon);
		if (d < best) best = d;
	}
	return best <= (double) e.range_km;
}

size_t	WED_LiveryIndex::CountAtClass(char size_class) const
{
	if (size_class < 'A' || size_class > 'F') return 0;
	return mAtClass[size_class - 'A'];
}

// mByAirline is keyed by the lowercased code, so the codes are recovered from the
// entries themselves to hand back the spelling the file uses - which is what the
// display and every other lookup expect.
void	WED_LiveryIndex::GetAirlineCodes(vector<string> & out) const
{
	out.clear();
	set<string> seen;
	for (size_t i = 0; i < mEntries.size(); ++i)
	{
		if (mEntries[i].note == kObsoleteNote) continue;		// never spawns, never offered (R25)
		if (seen.insert(mEntries[i].airline).second)
			out.push_back(mEntries[i].airline);
	}
}

void	WED_LiveryIndex::ForgetHeader(void)
{
	mSchema.clear();
	mDataStamp.clear();
	mSourceBuild.clear();
}

// Reads one comment line, looking only for the stamps the generator emits:
//
//     # schema 1
//     # data 20260916-r1
//     # source X-Plane 12.4.3-r2-15ff1e4d
//     # assets 298 liveries under apt_aircraft/
//
// `assets` is deliberately NOT stored. It is the generator's own count, and
// reporting it over the rows actually parsed would state a number that disagrees
// with what every query can see - mEntries.size() is the truth, and a file whose
// header disagrees with its body is exactly the case worth being able to notice.
//
// An index carrying none of these is not an error: it reads as unversioned,
// which is itself the signal. Every index this generator has produced carries
// them, so one that does not came from somewhere else.
void	WED_LiveryIndex::NoteHeaderLine(const char * line)
{
	const char * p = line;
	while (*p == '#' || *p == ' ' || *p == '\t') ++p;

	struct Field { const char * key; string * out; };
	const Field kFields[] = {
		{ "schema ", &mSchema },
		{ "data ",   &mDataStamp },
		{ "source ", &mSourceBuild },
	};

	for (size_t i = 0; i < sizeof(kFields)/sizeof(kFields[0]); ++i)
	{
		size_t n = strlen(kFields[i].key);
		if (strncmp(p, kFields[i].key, n) != 0) continue;
		if (!kFields[i].out->empty()) return;	// first wins; a later one is someone's prose
		string v(p + n);
		while (!v.empty() && (v[v.size()-1] == ' ' || v[v.size()-1] == '\t')) v.erase(v.size()-1);
		*kFields[i].out = v;
		return;
	}
}

// One line for the readout, which MUST name what it resolved against (spec
// §4.5). A probability computed from a stale index is worse than no probability,
// because it looks authoritative.
string	WED_LiveryIndex::DescribeVersion(void) const
{
	if (!mLoaded) return "not loaded";
	if (mSourceBuild.empty() && mDataStamp.empty()) return "unversioned index";

	string r = mSourceBuild;
	if (!mDataStamp.empty())
		r += (r.empty() ? string() : string(", ")) + "data " + mDataStamp;
	return r;
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

// Is `root` really the top of an X-Plane installation?
//
// The folder NAME is deliberately never consulted. Installs get renamed, and a
// nested copy - "X-Plane 12/X-Plane 12/" - would satisfy a name test while being
// the wrong directory entirely. What identifies the anchor is that the three
// things which only ever exist at the top level are siblings of each other: the
// application itself, Resources/ and Custom Scenery/.
//
// WED_PackageMgr::SetXPlaneFolder() already requires the latter two. This adds
// the application on top, for the one case that needs the certainty: we are
// about to descend into Resources/default scenery/sim objects/ and read a file.
// Being merely probably-right there yields a silent empty index, which is
// exactly the failure this feature exists to avoid.
static bool IsXPlaneRoot(const string & root)
{
	if (root.empty()) return false;

	const char * kApps[] = {
#if IBM
		"X-Plane.exe",
#elif APL
		"X-Plane.app",
#else
		"X-Plane-x86_64", "X-Plane",
#endif
	};

	bool found_app = false;
	for (size_t i = 0; i < sizeof(kApps) / sizeof(kApps[0]); ++i)
	{
		// mf_CheckType, not mf_File: the Mac "application" is a bundle, i.e. a
		// directory, while the Windows and Linux ones are ordinary files.
		if (MF_GetFileType((root + DIR_STR + kApps[i]).c_str(), mf_CheckType) != mf_BadFile)
		{
			found_app = true;
			break;
		}
	}
	if (!found_app) return false;

	if (MF_GetFileType((root + DIR_STR "Resources").c_str(), mf_CheckType) != mf_Directory)
		return false;
	if (MF_GetFileType((root + DIR_STR "Custom Scenery").c_str(), mf_CheckType) != mf_Directory)
		return false;

	return true;
}

string	WED_LiveryIndexDefaultPath(void)
{
	if (gPackageMgr == NULL) return string();

	string root;
	if (!gPackageMgr->GetXPlaneFolder(root)) return string();	// nothing selected yet

	if (!IsXPlaneRoot(root))
	{
		LOG_MSG("E/LiveryIndex '%s' is not an X-Plane root - need the application, "
				"Resources and Custom Scenery side by side.\n", root.c_str());
		return string();
	}

	return WED_LiveryAssetDir() + "livery_index.txt";
}

// <X-Plane root>/Resources/default scenery/sim objects/apt_aircraft/, or "" when
// no usable root is selected. Empty is a real answer rather than an error worth
// asserting on: WED runs perfectly well before a folder is chosen, and every
// caller here has something sensible to do with nothing.
string	WED_LiveryAssetDir(void)
{
	if (gPackageMgr == NULL) return string();

	string root;
	if (!gPackageMgr->GetXPlaneFolder(root)) return string();
	if (!IsXPlaneRoot(root)) return string();

	return root + DIR_STR "Resources" DIR_STR "default scenery" DIR_STR
				  "sim objects" DIR_STR "apt_aircraft" DIR_STR;
}

// Turns an index row's obj_path - stored relative to apt_aircraft/, with forward
// slashes - into something the OS can actually open.
//
// Two reasons this is a function rather than a concatenation at each call site.
// WED_ResourceMgr::GetObjAbsolute() prepends NOTHING, so a caller handing it the
// bare "jet/B738_UAL/..." resolves it against the process working directory and
// fails. And the separators have to be normalised: LoadObj passes this same path
// on to process_texture_path(), whose ".." unwinding scans for DIR_CHAR only, so
// a path mixing the index's '/' with Windows' '\' walks up the wrong component
// and the object's texture silently fails to resolve - a blank thumbnail with
// nothing in the log to explain it.
string	WED_LiveryObjectPath(const string & obj_path)
{
	if (obj_path.empty()) return string();
	const string dir = WED_LiveryAssetDir();
	if (dir.empty()) return string();

	// Clean the RELATIVE half only, then join. WED_clean_rpath maps '\', ':' and
	// '/' all onto DIR_CHAR, which is right for a library-relative path and
	// destructive for an absolute one: it turns "D:\SteamLibrary\..." into
	// "D\\SteamLibrary\..." and every load fails with a path that looks almost
	// correct in the log. The root already uses DIR_STR; only the index's
	// forward slashes need converting.
	string rel = obj_path;
	WED_clean_rpath(rel);
	return dir + rel;
}
