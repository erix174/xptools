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

	Loaded from WED_AirportDatabase.txt's sibling WED_AirlineDirectory.txt, same
	loose-file-next-to-WED.exe convention (see WED_AirportDatabase.h). Format, one
	airline per line:

		<CODE> *** <Name> *** <IOC country> *** <Pax|Cargo> *** <Fleet count>

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
		                       Pax/Cargo column below can tell the two apart
		                       without a collision.

	<IOC country> is the 3-letter IOC-style code (see WED_IocCountryCodes.h -
	same scheme, so an airport's normalized country compares directly against
	this field for the "same country" recommendation tier).

	The Pax/Cargo field is a first-pass, name-based classification (does this
	airline's name contain "Cargo"?) added specifically to stop an all-cargo
	operator's livery from being recommended at a passenger gate - it is NOT
	meant to be the long-term authority on Airline vs Cargo vs GA vs Military
	(that's still WED_LiveryData.h's op_type, sourced from what liveries
	actually exist in the future local static livery database/its own export
	metadata, which will eventually supersede this column). Until that
	exists, this field is what the picker has to go on.
*/

#ifndef WED_AIRLINEDIRECTORY_H
#define WED_AIRLINEDIRECTORY_H

#include <string>
#include <vector>
#include <unordered_map>

struct WED_AirlineDirectoryEntry
{
	std::string	code;		// as stored - see the 3/4/5-char scheme above
	std::string	name;
	std::string	country;	// IOC 3-letter code, uppercase
	bool		is_cargo;	// true for the "Cargo" column value - see the Pax/Cargo doc above
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

	// Exact-match lookup, case-insensitive. False (out_entry untouched) if code
	// isn't in the database at all.
	bool				Lookup(const std::string & code, WED_AirlineDirectoryEntry & out_entry) const;

	// Convenience wrapper over Lookup() for callers that only want the display
	// name. Returns "" if not found - callers should fall back to showing the
	// bare code, same convention as WED_LiveryPane's existing FindPlaceholderName().
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
