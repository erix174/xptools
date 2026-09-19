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

/*
	WED_LiveryIndex - THEORY OF OPERATION

	Reads livery_index.txt, the catalogue of every static-aircraft livery that
	ships with X-Plane. One row per livery:

		<TYPE> *** <CLASS> *** <AIRLINE> *** <REG> *** <REG COUNTRY> *** <NOTE> *** <path>

		B738 *** C *** UAL *** N78540 *** USA *** Retro   *** jet/B738_UAL_Legacy/...
		B738 *** C *** UAL *** N79521 *** USA *** Default *** jet/B738_UAL_Modern/...

	CLASS is the ICAO wingspan class A-F - which ramp size the aircraft needs. It
	is carried per row rather than in a separate type-to-class file on purpose: at
	this data volume a second file buys nothing and costs a hard dependency, where
	one changed type designator stalls both files at once. One row holds every
	fact about one livery.

	THIS FILE LIVES ON THE X-PLANE SIDE, not in WED:

		<X-Plane root>/Resources/default scenery/sim objects/apt_aircraft/livery_index.txt

	which is what LiveryIndexDefaultPath() below builds. The inventory is a
	property of the X-Plane installation, not of WED - shipping WED's own copy
	would mean WED carrying an opinion about another product's asset library and
	going stale the moment X-Plane ships a new livery. Over there it sits next to
	the assets it describes, so the PR that adds a livery is already touching that
	directory. Reading it costs us no new dependency either: the user must already
	have selected an X-Plane root before any livery can be loaded at all, and
	WED_PackageMgr::SetXPlaneFolder() refuses a root without
	"Resources/default scenery", so the path always resolves when
	HasSystemFolder() is true.

	WHY THIS EXISTS AT ALL: X-Plane's library.txt publishes static aircraft
	through EXPORT_EXTEND buckets keyed only by (operation type, size class,
	airline) - lib/airport/aircraft/airliners/jet_c_ual.obj. There is no aircraft
	type in that name (one "heavy_e" bucket mixes A359, A35K and B772) and no
	livery axis, so "United's 737-800 in the retro livery" could not be named,
	even though both liveries have shipped for years. This file adds exactly those
	two missing axes and nothing else.

	THE NOTE COLUMN IS FREE TEXT, deliberately not an enum. Anything other than
	"Default" is rendered in parentheses after the airline name by
	LiveryDisplayName() below - "United (Retro)", "Air China (Pink Peony)",
	"Hainan Airlines (Mixue)". Making a caption more specific is therefore a
	one-word edit to the data file and needs no code change here, which is the
	whole point: whoever maintains the liveries should not have to touch C++ to
	describe one.

	Load protocol and failure policy match the other files in this family
	(WED_AirportDatabase, WED_AirlineDirectory): one attempt per instance, the
	shared two-line stamp checked by WED_MandatoryHeader is a hard refusal, and
	individual malformed rows are skipped so one bad row cannot take out the file.
*/

#ifndef WED_LIVERYINDEX_H
#define WED_LIVERYINDEX_H

#include <string>
#include <vector>
#include <unordered_map>

// One livery. `obj_path` is relative to apt_aircraft/, exactly as stored.
struct WED_LiveryIndexEntry {
	std::string		type;			// ICAO type designator, e.g. "B738"
	char			size_class;		// ICAO wingspan class 'A'..'F', or 0 if unknown
	std::string		airline;		// ICAO airline code, or an XP-prefixed pseudo code
	std::string		reg;			// registration without the dash; may be empty
	std::string		reg_country;	// IOC 3-letter code; empty when reg is empty
	std::string		note;			// free text; "Default" means "no annotation"
	// Schema 2. 0 / empty mean "unknown", and unknown is never filtered.
	int				range_km;		// typical operating range of `type`
	std::vector<std::pair<double,double> >	hubs;	// operator's hub positions, (lat, lon)
	// HUBS == "HOME": a military/government livery that parks only on home soil.
	// Every other military row parks anywhere - see livery_home_only.txt.
	bool			home_only;
	std::string		obj_path;		// relative to apt_aircraft/
	WED_LiveryIndexEntry() : size_class(0), range_km(0), home_only(false) {}
};

// THE SPAWN RULE, as X-Plane applies it and as WED previews it: false when the
// aircraft's range cannot cover the distance from the operator's nearest hub to
// this stand. Unknown range or unknown hubs -> true, always - a missing fact must
// never hide a livery. Both sides compute this from the same index row and the
// same stand position, so nothing about it is stored anywhere.
bool	WED_LiveryInRange(const WED_LiveryIndexEntry & e, double stand_lat, double stand_lon);

class	WED_LiveryIndex {
public:

						WED_LiveryIndex();

	// Loads the index at index_path, and remembers WHICH path it loaded.
	//
	// The result is "good for this path": calling it again with the same path is a
	// no-op, including after a failure, so a missing file doesn't re-stat on every
	// Draw(). Calling it with a DIFFERENT path throws the cache away and loads
	// again. That is what makes this survive the user changing their X-Plane
	// folder (WED_StartWindow's wed_ChangeSystem, which can happen at any time) -
	// the path is derived from the root, so a new root is a new path, and a plain
	// one-shot flag would have left the index dead until WED restarted.
	bool				EnsureLoaded(const std::string & index_path);

	bool				IsLoaded(void) const { return mLoaded; }
	bool				LoadFailed(void) const { return mLoadAttempted && !mLoaded; }

	// Every livery this airline has, in file order. Empty if the code is unknown.
	// The code is matched case-insensitively.
	const std::vector<const WED_LiveryIndexEntry *> *
						GetForAirline(const std::string & airline_code) const;

	// Liveries for one (airline, type) pair - e.g. every 737-800 United has.
	void				GetForAirlineAndType(const std::string & airline_code,
											 const std::string & type,
											 std::vector<const WED_LiveryIndexEntry *> & out) const;

	// Liveries this airline has that fit a stand of the given ICAO size class.
	// Pass 0 for size_class to mean "any size". This is the query the preview
	// cards actually make - an airline having SOME model is not the same as it
	// having one that fits THIS stand.
	void				GetForAirlineAndClass(const std::string & airline_code,
											  char size_class,
											  std::vector<const WED_LiveryIndexEntry *> & out) const;

	// Every airline code that has at least one usable livery, uppercase. This is
	// the right source for "which operators can have a preview card": the airline
	// directory lists thousands of operators, almost none of which are modelled,
	// and walking it instead would cost a class lookup per operator per rebuild to
	// discard nearly all of them. Obsolete rows are excluded, since they are not
	// in the lookup tables at all (R25).
	void				GetAirlineCodes(std::vector<std::string> & out) const;

	// Resolves a stable key of the form <TYPE>_<AIRLINE>_<NOTE> - the same string
	// apt.dat will eventually store per ramp. NULL if no such livery.
	const WED_LiveryIndexEntry *
						Lookup(const std::string & key) const;

	// Count() is every row parsed; UsableCount() excludes the ones marked
	// Obsolete, which are held but never indexed (R25). They differ exactly when
	// the library has assets it ships but no longer wants chosen, so reporting
	// both is how "298 rows, 286 usable" stays answerable instead of the file
	// silently appearing shorter than its own header claims.
	size_t				Count(void) const { return mEntries.size(); }
	size_t				UsableCount(void) const { return mUsable; }

	// How many usable liveries exist at one wingspan class, across the WHOLE
	// library rather than for one operator. This is what separates R14's two
	// look-alike failures: weights aimed at a class the listed operators cannot
	// fill is an author error one click repairs, while weights aimed at a class
	// NOTHING in the library can fill is the author being ahead of the art, and
	// blocking that would make it impossible to author for an aircraft that is
	// coming. Class F returns 0 today.
	size_t				CountAtClass(char size_class) const;

	// The stamps from the index's own header, empty when it carried none. See
	// DescribeVersion() - the readout has to say which install its numbers came
	// from, because §6.4's install mismatch has no other symptom.
	const std::string &	Schema(void) const      { return mSchema; }
	const std::string &	DataStamp(void) const   { return mDataStamp; }
	const std::string &	SourceBuild(void) const { return mSourceBuild; }
	std::string			DescribeVersion(void) const;

	// Whether WED can show anything for an airline the user picked.
	//
	// NEVER PERSIST THIS. The .wed document stores only the airline code the
	// author chose; availability is recomputed from the index on every load. That
	// is the whole mechanism behind "a model ships later and the selection just
	// lights up" - caching it would go stale on exactly the day it matters, and
	// would need a data migration to unstick.
	enum Availability {
		livery_Hit,		// the index has a model for this airline (and size) - render it
		livery_Ignore,	// a real airline, but nothing modelled yet. The author may
						// still select it: X-Plane skips what it cannot load, and the
						// day a model ships this silently becomes livery_Hit.
		livery_Faulty	// the code matches nothing at all - not in the index and not
						// in WED_AirlineDirectory either. Bad data, not a gap.
	};

	// `known_airline` is what WED_AirlineDirectory says about the code - pass true
	// when the directory has a row for it. Splitting it out keeps this class from
	// depending on the directory just to tell "not modelled yet" from "typo".
	Availability		GetAvailability(const std::string & airline_code,
										char size_class,
										bool known_airline) const;

private:

	void				NoteHeaderLine(const char * line);	// one '#' line, on the way past
	void				ForgetHeader(void);

	std::string			mSchema, mDataStamp, mSourceBuild;
	size_t				mUsable;			// mEntries minus the Obsolete rows
	size_t				mAtClass[6];		// usable liveries per class A..F, whole library

	std::vector<WED_LiveryIndexEntry>										mEntries;
	std::unordered_map<std::string, std::vector<const WED_LiveryIndexEntry *> >	mByAirline;
	std::unordered_map<std::string, const WED_LiveryIndexEntry *>			mByKey;

	std::string			mLoadedPath;	// what mLoaded/mLoadAttempted refer to
	bool				mLoadAttempted;
	bool				mLoaded;
};

// <TYPE>_<AIRLINE>_<NOTE>, with spaces in the note turned into underscores so the
// key stays a single whitespace-free token - it has to survive a trip through
// apt.dat, which is whitespace-delimited. "B738_UAL_Retro",
// "B738_CCA_Pink_Peony". Always build keys with this rather than concatenating by
// hand, or the two sides of a round-trip will disagree about the spaces.
std::string	WED_MakeLiveryKey(const std::string & type,
							  const std::string & airline,
							  const std::string & note);

// The caption WED shows for a livery: the airline's name on its own when the note
// is "Default" (or empty), otherwise the name with the note in parentheses. This
// is the "acceptor" for the NOTE column - it takes whatever text the data file
// carries, so a maintainer can refine "Peony" into "Pink Peony" without a code
// change. Underscores in the note are turned back into spaces for display.
std::string	WED_LiveryDisplayName(const std::string & airline_name,
								  const std::string & note);

// <X-Plane root>/Resources/default scenery/sim objects/apt_aircraft/livery_index.txt,
// or an empty string when no X-Plane folder has been selected yet.
std::string	WED_LiveryIndexDefaultPath(void);

// <X-Plane root>/Resources/default scenery/sim objects/apt_aircraft/, "" if no
// usable root is selected yet.
std::string	WED_LiveryAssetDir(void);

// An index row's obj_path made openable: joined onto the asset directory and
// separator-normalised. "" when there is no root, or the path was empty.
// ALWAYS build a livery's real path with this rather than concatenating - see
// the .cpp for the two ways a hand-built one fails, both of them silent.
std::string	WED_LiveryObjectPath(const std::string & obj_path);

#endif /* WED_LIVERYINDEX_H */
