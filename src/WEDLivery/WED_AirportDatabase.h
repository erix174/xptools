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
	WED_AirportDatabase - THEORY OF OPERATION

	Single, self-contained reference table WED reads for everything the
	livery picker needs to know about an airport BEFORE looking at any one
	specific ramp: its country (for the flag banner and the "same country"
	recommendation tier) and which airlines currently serve it (for the
	recommendation list itself). Replaces three previously-separate lookups
	(WED_AptDatCountryIndex, WED_AirlineIndex, WED_CommercialAirportIndex) -
	that split existed because the data used to come from two genuinely
	different sources (the user's own local apt.dat for country, a shipped
	text file for airlines); now that country data is ALSO hand-curated and
	shipped the same way, there's no reason left to keep them apart, and
	every airport in this file being present at all IS the "commercially
	served" fact - no separate population to reconcile against.

	Data file: WED_AirportDatabase.txt in this same folder, shipped loose
	next to WED.exe (see cmake/WED.cmake) exactly like the files it replaces.
	Format (see that file's own header for the authoritative description):
	    <ICAO> *** <ISO_COUNTRY> *** <AIRLINE_ICAO>, <AIRLINE_ICAO>, ...
	The country field is normalized to an IOC-style 3-letter code (see
	WED_IocCountryCodes.h) at load time, once, so every Lookup() after that
	is a plain O(1) map lookup with zero further processing - same shape as
	the apt.dat-backed index it replaces, just fed from a tiny (a few
	thousand row) file instead of a 380MB one, so there's no need for that
	index's disk-cache layer either.

	Small enough (a few thousand airports at most) that, like
	WED_AirlineIndex before it, there's no need for a disk cache layer - it's
	just parsed directly on first use.
*/

#ifndef WED_AIRPORTDATABASE_H
#define WED_AIRPORTDATABASE_H

#include "WED_MandatoryHeader.h"
#include <string>
#include <vector>
#include <unordered_map>

class	WED_AirportDatabase {
public:

						WED_AirportDatabase();

	// Loads exactly once per instance; subsequent calls are no-ops (including
	// after a failed attempt - see LoadFailed()). Returns true if the table is
	// usable after this call.
	bool				EnsureLoaded(const std::string & db_path);

	bool				IsLoaded(void) const { return mLoaded; }
	bool				LoadFailed(void) const { return mLoadAttempted && !mLoaded; }

	// Why the last attempt failed - so a damaged file is not reported as a
	// missing one. See WedDataFileErrorText() in WED_MandatoryHeader.h.
	WedDataFileError	LoadError(void) const { return mLoadError; }

	// True iff icao is in the database at all - i.e. an airport OurAirports
	// considers to have scheduled commercial service, UNLESS this specific
	// row has been hand-confirmed to have none (the "<NA>" sentinel - see
	// WED_AirportDatabase.txt's header), which overrides that default.
	bool				IsCommercial(const std::string & icao) const;

	// IOC-style 3-letter country code (already normalized - see
	// WED_IocCountryCodes.h) for this ICAO. False (out_country left
	// untouched) if icao isn't in the database, or its raw country value
	// didn't normalize to anything recognized.
	bool				GetCountry(const std::string & icao, std::string & out_country) const;

	// The specific airline codes (uppercase, as stored) this airport has, or
	// leaves out_airlines untouched (caller should treat it as empty) if
	// icao isn't in the database at all. A TRUE result with an EMPTY
	// out_airlines is a distinct, meaningful state covering two cases that
	// deliberately look the same to callers - "not yet researched" (nothing
	// after the second "***") and "researched, confirmed zero scheduled
	// service" (the "<NA>" sentinel) - see IsCommercial() for the one place
	// those two cases DO need to be told apart.
	bool				GetAirlines(const std::string & icao, std::vector<std::string> & out_airlines) const;

	int					AirportCount(void) const { return (int) mTable.size(); }

private:

	struct Entry
	{
		std::string					country;		// already IOC-normalized
		std::vector<std::string>	airlines;		// empty if unresearched OR confirmed-none
		bool						na_confirmed;	// true only for an explicit "<NA>" row
	};

	WedDataFileError						mLoadError;
	bool									mLoadAttempted;
	bool									mLoaded;
	std::unordered_map<std::string, Entry>	mTable;

};

#endif /* WED_AIRPORTDATABASE_H */
