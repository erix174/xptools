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
	WED_IocCountryCodes - THEORY OF OPERATION

	apt.dat's "1302 country <value>" line is USUALLY a clean ISO-3166-1 alpha-3
	code (e.g. "NOR", "JPN"), but not always usable as-is for our purposes:

	  - Governing principle: match the IOC's OWN current stance exactly, every
	    time, rather than this project making its own "is X a country" call.
	    Where IOC gives a territory its own seat and flag, we keep it separate
	    (Taiwan -> TPE "Chinese Taipei", Hong Kong -> HKG "Hong Kong, China" -
	    both are real, current IOC members with their own flags, verified
	    2026). Where IOC does NOT give a territory its own seat, we fold it
	    into its parent (Macau -> CHN, plus various small UK/France/
	    Netherlands/Denmark/Australia/NZ territories) - not because we've
	    decided those places "aren't countries", but because IOC hasn't. This
	    way any dispute about a specific code can be answered with "that's
	    IOC's own current classification, not ours."
	  - Real-world apt.dat data (community-sourced) has ~20 distinct dirty
	    values in this field: plain typos ("Bostwana"), old names ("Burma",
	    "Swaziland"), a name instead of a code ("Réunion"), or legacy/incorrect
	    abbreviations ("RP" for the Philippines, "BG" for Bulgaria where the
	    real ISO3 is "BGR").
	  - Rarely, the field holds a 2-letter ISO code instead of 3.

	NormalizeCountryToIoc() applies exactly these three lookup tables (ported
	1:1 from tools/scripts/build_icao_country_list.py's validated logic, then
	corrected against the real IOC member list - see the .cpp for the specific
	fixes) and falls through to "already a clean 3-letter code, use as-is" for
	the common case. Returns an empty string if nothing matched (caller should
	treat that the same as "no country info available").

	One thing this CANNOT fix: apt.dat's "1302 country" value is itself
	sometimes a different, fully-recognized sovereign country's code, for a
	territory IOC gives its own seat to - e.g. Kosovo's apt.dat entries say
	"SRB" (Serbia), not any Kosovo-ish code, because that's the community
	data's own political framing, not a data-entry error NormalizeCountryToIoc
	can detect from the string alone. IcaoPrefixIocOverride() is the (currently
	very short - just Kosovo) escape hatch for exactly that situation: cases
	discovered where apt.dat's raw country line, even after normalization,
	still doesn't match IOC's own stance, checked by the airport's ICAO region
	prefix instead of trusting the data's own country field at all. Same
	governing principle as above, just applied at the ICAO-prefix level
	instead of the country-value level: apt.dat's community data reflects
	whoever entered it, IOC's current stance is the one this project follows.
*/

#ifndef WED_IOCCOUNTRYCODES_H
#define WED_IOCCOUNTRYCODES_H

#include <string>

// Best-effort mapping from whatever apt.dat's "1302 country" line gave us to a
// 3-letter IOC-style code. Returns an empty string if raw is empty or nothing
// matched (unrecognized value - NOT the same as "unknown 3-letter code", which
// is passed through as-is on the assumption it's already a valid ISO3/IOC code).
std::string	NormalizeCountryToIoc(const std::string & raw_country);

// For the rare case where apt.dat's own country data, even normalized, still
// doesn't match IOC's stance for that airport's territory - checked by ICAO
// prefix so it applies regardless of what (if anything) apt.dat's country
// line says. Returns an empty string for every ICAO except the handful of
// known exceptions (see the .cpp) - callers should treat a non-empty result
// as taking priority over NormalizeCountryToIoc().
std::string	IcaoPrefixIocOverride(const std::string & icao);

#endif /* WED_IOCCOUNTRYCODES_H */
