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
#include "MemFileUtils.h"		// MF_GetFileType, for the X-Plane root check
#include "WED_LibraryMgr.h"		// WED_clean_rpath - separator normalisation, see WED_LiveryObjectPath()

#include <fstream>
#include <algorithm>
#include <cstring>			// strncmp / strlen, for the header stamps

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

size_t	WED_LiveryIndex::CountAtClass(char size_class) const
{
	if (size_class < 'A' || size_class > 'F') return 0;
	return mAtClass[size_class - 'A'];
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
