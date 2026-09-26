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
	WED_AirlineDirectory - THEORY OF OPERATION

	This is the global airline reference list: every airline WED knows about,
	independent of any one airport. It answers "what's this code called, what
	country is it from, how big is its fleet" - it does NOT answer "which
	airlines serve this specific airport" (that's WED_AirportDatabase, a
	different, much smaller, per-airport hand-researched file).

	Loaded from the OPERATOR records at the top of the install's
	livery_index.txt - the one file both WED and X-Plane read; there is no
	WED-side copy and no fallback. Format, one airline per line:

		OPERATOR *** <CODE> *** <Name> *** <IOC country> *** <Pax|Cargo|GA|Military|Gov> *** <Fleet count> *** <hub ICAOs>

	Cells are split on "***" and trimmed of spaces and tabs; the section is
	tab-aligned for reading (tab width 4), so the whitespace is layout, not data.

	CODE's length tells you how it was determined - this is deliberate, so a
	human skimming the raw text file (or code reading it) can tell at a glance
	how much to trust it, without a separate "confidence" column:

		3 chars              - a real ICAO code, confirmed against multiple
		                       independent sources. Direct hit, no ambiguity.
		4 chars, starts "XP" - no real ICAO code could be confirmed after
		                       research (conflicting sources, or genuinely
		                       never assigned one). Fictional placeholder,
		                       unique per airline, NOT a real ICAO code.
		5 chars, "XXX_N"     - a brand/subsidiary of parent airline code XXX
		                       that has its own distinct livery but no ICAO
		                       code of its own (N disambiguates when a parent
		                       has more than one such subsidiary).
		5 chars, "XXX_F"     - the all-cargo division of parent airline code
		                       XXX, in the real world sharing that SAME ICAO
		                       code/callsign with the passenger side - split
		                       into its own row here (this project's own key,
		                       not a real distinct ICAO code) purely so the
		                       operation class can tell the two apart
		                       without a collision.
		"XPZZ_<TYPE>"        - a generic, unpainted airliner of that type
		                       (XPZZ_B752), usable by any airline or cargo
		                       stand. The full grammar is R10 of the spec:
		                       [a-z0-9]{3,4}(_[a-z0-9]{1,6})?

	<IOC country> is the 3-letter IOC-style code (see WED_IocCountryCodes.h -
	same scheme, so an airport's normalized country compares directly against
	this field for the "same country" recommendation tier).

	The operation class (Pax, Cargo, GA, Military, Gov) is the authority for
	which stands an operator may appear on - a cargo operator never parks at a
	passenger gate - for the Liveries tab, auto-fill and the validator alike.
	It is hand-maintained in the index with everything else about the
	operator.
*/

#ifndef WED_AIRLINEDIRECTORY_H
#define WED_AIRLINEDIRECTORY_H

#include "WED_MandatoryHeader.h"
#include <cctype>
#include <string>
#include <vector>
#include <unordered_map>

// The generic unpainted airliners: XPZZ_<TYPE>, one pseudo-operator per type
// (XPZZ_B752, XPZZ_DC10), so a stand can list the one white airframe it means.
// Bare XPZZ is still recognised - an older index has only that. Either case.
inline bool	WED_IsGenericAirlinerCode(const std::string & code)
{
	if (code.size() < 4) return false;
	for (int i = 0; i < 4; ++i)
		if (toupper((unsigned char) code[i]) != "XPZZ"[i]) return false;
	return code.size() == 4 || code[4] == '_';
}

struct WED_AirlineDirectoryEntry
{
	std::string	code;		// as stored - see the 3/4/5-char scheme above
	std::string	name;
	std::string	country;	// IOC 3-letter code, uppercase
	bool		is_cargo;	// true for the "Cargo" column value - see the Pax/Cargo doc above
	// The fourth column, which the ramp's operation-type filter matches against.
	// Pax -> Airline, Cargo -> Cargo, GA -> GA, Military and Gov -> Military.
	enum OpClass { op_Pax, op_Cargo, op_GA, op_Military, op_Gov };
	OpClass		op_class;
	int			fleet;
};

class	WED_AirlineDirectory {
public:

						WED_AirlineDirectory();

	// Loads exactly once per instance; subsequent calls are no-ops (including
	// after a failed attempt - see LoadFailed()). Returns true if the table is
	// usable after this call.
	bool				EnsureLoaded(const std::string & db_path);

	bool				IsLoaded(void) const { return mLoaded; }
	bool				LoadFailed(void) const { return mLoadAttempted && !mLoaded; }

	// Why the last attempt failed - so a damaged file is not reported as a
	// missing one. See WedDataFileErrorText() in WED_MandatoryHeader.h.
	WedDataFileError	LoadError(void) const { return mLoadError; }

	// Exact-match lookup, case-insensitive. False (out_entry untouched) if code
	// isn't in the database at all.
	bool				Lookup(const std::string & code, WED_AirlineDirectoryEntry & out_entry) const;

	// Convenience wrapper over Lookup() for callers that only want the display
	// name. Returns "" if not found - callers should fall back to showing the
	// bare code.
	std::string			GetName(const std::string & code) const;

	// Every entry whose country field exact-matches ioc_country (case-
	// insensitive). Pointers are valid as long as this WED_AirlineDirectory
	// instance is alive and EnsureLoaded() isn't called again. Used for the
	// "same country" half-hit recommendation tier. Order is insertion order
	// (i.e. the order they appear in the source file) - callers sort as needed.
	void				GetByCountry(const std::string & ioc_country, std::vector<const WED_AirlineDirectoryEntry *> & out) const;

	// Every entry, ordered by fleet size descending. Used for the "large,
	// well-known operator, but no closer match" recommendation tier.
	const std::vector<WED_AirlineDirectoryEntry> &		GetAllSortedByFleetDesc(void) const { return mSortedByFleet; }

	int					Count(void) const { return (int) mEntries.size(); }

private:

	WedDataFileError						mLoadError;
	bool									mLoadAttempted;
	bool									mLoaded;
	std::vector<WED_AirlineDirectoryEntry>	mEntries;
	std::unordered_map<std::string, int>	mByCode;		// uppercase code -> index into mEntries
	std::vector<WED_AirlineDirectoryEntry>	mSortedByFleet;

	// uppercase IOC country -> indices into mEntries. Built once in EnsureLoaded() so
	// GetByCountry() - called on every "Same Country" tier rebuild, i.e. every Draw() while the
	// Static Liveries tab is open - is an O(1) average hash lookup instead of an O(n) scan over
	// the whole directory each time.
	std::unordered_map<std::string, std::vector<int>>	mByCountry;

};

#endif /* WED_AIRLINEDIRECTORY_H */
