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

#include "WED_IocCountryCodes.h"
#include <unordered_map>
#include <cctype>

using std::string;

namespace {

// apt.dat's "1302 country" field is USUALLY an ISO3 code, but ~20 distinct
// dirty values were found in a real-world install's apt.dat (typos /
// non-standard abbreviations / old names). Matched by exact string first,
// before any case-folding, so this mirrors the source data byte-for-byte.
// "R\xC3\xA9union" is "Réunion" as raw UTF-8 bytes - written as an escape so
// it matches regardless of this .cpp's own source encoding.
const std::unordered_map<string, string> & DirtyAptDatToIoc(void)
{
	static const std::unordered_map<string, string> table = {
		{ "US",  "USA" }, { "USA", "USA" }, { "Usa", "USA" }, { "United", "USA" }, { "Unied", "USA" },
		{ "Unites", "USA" }, { "UnitedStates", "USA" }, { "U.S", "USA" }, { "U.S.", "USA" },
		{ "U", "USA" }, { "S", "USA" },		// look like "U S America" / "U.S." split by a stray space
		{ "U,S,", "USA" },					// "U.S." with commas instead of periods (seen in a real apt.dat entry)
		{ "BG", "BUL" },					// Bulgaria (BGR is the real ISO3; BG collides with its ISO2 by mistake)
		{ "IN", "IND" },					// India
		{ "RP", "PHI" },					// Philippines ("Republic of the Philippines", a common non-standard abbreviation)
		{ "ZW", "ZIM" },					// Zimbabwe (should have been ZWE)
		{ "Bostwana", "BOT" },				// Botswana, misspelled
		{ "British", "GBR" },				// too vague to be sure which territory was meant - falls back to UK itself
		{ "Burma", "MYA" },					// old name for Myanmar
		{ "Celebes", "INA" },				// old (Dutch colonial) name for Sulawesi, Indonesia
		{ "R\xC3\xA9union", "FRA" },		// "Réunion" - Reunion Island, written as a name rather than a code
		{ "Sanaa", "YEM" },					// Yemen's capital, written in place of the country
		{ "Slovensko", "SVK" },				// Slovakia's own name for itself
		{ "Swaziland", "SWZ" },				// old name for Eswatini
	};
	return table;
}

// apt.dat's real ISO3 codes need their own table too - not every ISO3 equals
// its IOC code (non-sovereign territories folded into a parent country/seat).
//
// Corrected against the actual IOC member list (206 NOCs) rather than just
// "does this territory have its own ISO3 code": three entries below differ
// from the first pass of this table (build_icao_country_list.py, which this
// was originally ported from) because that pass conflated "has its own ISO3
// code" with "has its own IOC seat" - they're not the same list:
//   - GUM (Guam) is NOT folded into USA - Guam has competed under its own
//     IOC seat "GUM" since 1988, so it's omitted here entirely and falls
//     through to the "already a clean 3-letter code" pass-through below.
//   - ASM (American Samoa) is now mapped to "ASA" - American Samoa DOES have
//     its own IOC seat, but (unlike Guam) its IOC code isn't the same string
//     as its ISO3 code, so it still needs an explicit override. This entry
//     was simply missing before.
//   - AIA (Anguilla) is now folded into GBR, not kept as its own code -
//     Anguilla has never had a seat of its own (unlike Bermuda/Cayman/BVI,
//     which do), the same as Montserrat/Turks and Caicos/Gibraltar/Falklands.
const std::unordered_map<string, string> & Iso3ToIocOverrides(void)
{
	static const std::unordered_map<string, string> table = {
		{ "TWN", "TPE" }, { "MAC", "CHN" }, { "PRI", "PUR" },
		// Hong Kong (HKG) is deliberately NOT folded into CHN here - unlike
		// Macau, Hong Kong is a real, current IOC member with its own seat
		// and flag ("Hong Kong, China"), and its ISO3 code already equals its
		// IOC code, so plain pass-through already does the right thing.
		{ "ASM", "ASA" },
		{ "MNP", "USA" }, { "UMI", "USA" }, { "VGB", "IVB" }, { "VIR", "ISV" }, { "CYM", "CAY" },
		{ "BMU", "BER" }, { "GIB", "GBR" }, { "FLK", "GBR" }, { "MSR", "GBR" }, { "AIA", "GBR" },
		{ "TCA", "GBR" }, { "SHN", "GBR" }, { "IOT", "GBR" }, { "GGY", "GBR" }, { "JEY", "GBR" },
		{ "IMN", "GBR" }, { "PCN", "GBR" }, { "GLP", "FRA" }, { "MTQ", "FRA" }, { "GUF", "FRA" },
		{ "REU", "FRA" }, { "MYT", "FRA" }, { "SPM", "FRA" }, { "WLF", "FRA" }, { "PYF", "FRA" },
		{ "NCL", "FRA" }, { "BLM", "FRA" }, { "MAF", "FRA" }, { "ATF", "FRA" }, { "ABW", "ARU" },
		{ "CUW", "NED" }, { "SXM", "NED" }, { "BES", "NED" }, { "ALA", "FIN" }, { "FRO", "DEN" },
		{ "GRL", "DEN" }, { "SJM", "NOR" }, { "ESH", "MAR" }, { "ATA", "ATA" }, { "COK", "COK" },
		{ "NIU", "NZL" }, { "TKL", "NZL" }, { "CCK", "AUS" }, { "CXR", "AUS" }, { "NFK", "AUS" },
		{ "VAT", "ITA" }, { "PSE", "PLE" }, { "XKX", "KOS" },

		// Sovereign countries, not folded territories - IOC just uses a
		// different 3-letter code than ISO 3166-1 alpha-3 for these ~70, so
		// apt.dat's plain, correctly-formed ISO3 value (the common case) still
		// needs remapping. Found missing entirely from this table by cross-
		// referencing every IOC/ISO3 pair on Wikipedia's "Comparison of IOC,
		// FIFA, and ISO 3166 country codes" - none of these are political
		// judgement calls, just IOC's own long-standing traditional codes.
		{ "DZA", "ALG" }, { "AGO", "ANG" }, { "ATG", "ANT" }, { "BHS", "BAH" },
		{ "BHR", "BRN" }, { "BGD", "BAN" }, { "BRB", "BAR" }, { "BLZ", "BIZ" },
		{ "BTN", "BHU" }, { "BWA", "BOT" }, { "BRN", "BRU" }, { "BGR", "BUL" },
		{ "BFA", "BUR" }, { "KHM", "CAM" }, { "TCD", "CHA" }, { "CHL", "CHI" },
		{ "COG", "CGO" }, { "CRI", "CRC" }, { "HRV", "CRO" }, { "DNK", "DEN" },
		{ "SLV", "ESA" }, { "GNQ", "GEQ" }, { "FJI", "FIJ" }, { "GMB", "GAM" },
		{ "DEU", "GER" }, { "GRC", "GRE" }, { "GRD", "GRN" }, { "GTM", "GUA" },
		{ "GIN", "GUI" }, { "GNB", "GBS" }, { "HTI", "HAI" }, { "HND", "HON" },
		{ "IDN", "INA" }, { "IRN", "IRI" }, { "LVA", "LAT" }, { "LSO", "LES" },
		{ "LBY", "LBA" }, { "MDG", "MAD" }, { "MWI", "MAW" }, { "MYS", "MAS" },
		{ "MRT", "MTN" }, { "MUS", "MRI" }, { "MCO", "MON" }, { "MNG", "MGL" },
		{ "MMR", "MYA" }, { "NPL", "NEP" }, { "NLD", "NED" }, { "NIC", "NCA" },
		{ "NER", "NIG" }, { "NGA", "NGR" }, { "OMN", "OMA" }, { "PRY", "PAR" },
		{ "PHL", "PHI" }, { "PRT", "POR" }, { "KNA", "SKN" }, { "VCT", "VIN" },
		{ "WSM", "SAM" }, { "SAU", "KSA" }, { "SYC", "SEY" }, { "SVN", "SLO" },
		{ "SLB", "SOL" }, { "ZAF", "RSA" }, { "LKA", "SRI" }, { "SDN", "SUD" },
		{ "CHE", "SUI" }, { "TZA", "TAN" }, { "TGO", "TOG" }, { "TON", "TGA" },
		{ "ARE", "UAE" }, { "URY", "URU" }, { "VUT", "VAN" }, { "VNM", "VIE" },
		{ "ZMB", "ZAM" }, { "ZWE", "ZIM" }, { "KWT", "KUW" },
	};
	return table;
}

// ISO-3166-1 alpha-2 -> IOC three-letter code, covering every code appearing
// in OurAirports' countries.csv (250 entries). Used only for the rare case
// apt.dat's own country field holds a 2-letter code instead of 3. Where IOC
// has no seat for a territory, this falls back to a documented parent country
// - see tools/scripts/build_icao_country_list.py's NOTES for the reasoning
// behind every non-obvious one (this table is a verbatim port of that one).
const std::unordered_map<string, string> & Iso2ToIoc(void)
{
	static const std::unordered_map<string, string> table = {
		{ "AD","AND" }, { "AE","UAE" }, { "AF","AFG" }, { "AG","ANT" }, { "AI","GBR" },
		{ "AL","ALB" }, { "AM","ARM" }, { "AO","ANG" }, { "AQ","ATA" },
		{ "AR","ARG" }, { "AS","ASA" }, { "AT","AUT" }, { "AU","AUS" }, { "AW","ARU" },
		{ "AX","FIN" },
		{ "AZ","AZE" }, { "BA","BIH" }, { "BB","BAR" }, { "BD","BAN" }, { "BE","BEL" },
		{ "BF","BUR" }, { "BG","BUL" }, { "BH","BRN" }, { "BI","BDI" }, { "BJ","BEN" },
		{ "BL","FRA" },
		{ "BM","BER" }, { "BN","BRU" }, { "BO","BOL" }, { "BQ","NED" },
		{ "BR","BRA" }, { "BS","BAH" }, { "BT","BHU" }, { "BW","BOT" }, { "BY","BLR" },
		{ "BZ","BIZ" }, { "CA","CAN" }, { "CC","AUS" },
		{ "CD","COD" }, { "CF","CAF" }, { "CG","CGO" }, { "CH","SUI" }, { "CI","CIV" },
		{ "CK","COK" }, { "CL","CHI" }, { "CM","CMR" }, { "CN","CHN" }, { "CO","COL" },
		{ "CR","CRC" }, { "CU","CUB" }, { "CV","CPV" }, { "CW","NED" },
		{ "CX","AUS" },
		{ "CY","CYP" }, { "CZ","CZE" }, { "DE","GER" }, { "DJ","DJI" }, { "DK","DEN" },
		{ "DM","DMA" }, { "DO","DOM" }, { "DZ","ALG" }, { "EC","ECU" }, { "EE","EST" },
		{ "EG","EGY" }, { "EH","MAR" },
		{ "ER","ERI" }, { "ES","ESP" }, { "ET","ETH" }, { "FI","FIN" }, { "FJ","FIJ" },
		{ "FK","GBR" },
		{ "FM","FSM" }, { "FO","DEN" },
		{ "FR","FRA" }, { "GA","GAB" }, { "GB","GBR" }, { "GD","GRN" }, { "GE","GEO" },
		{ "GF","FRA" },
		{ "GG","GBR" },
		{ "GH","GHA" }, { "GI","GBR" },
		{ "GL","DEN" },
		{ "GM","GAM" }, { "GN","GUI" }, { "GP","FRA" },
		{ "GQ","GEQ" }, { "GR","GRE" }, { "GT","GUA" }, { "GU","GUM" }, { "GW","GBS" },
		{ "GY","GUY" }, { "HK","HKG" }, { "HN","HON" }, { "HR","CRO" }, { "HT","HAI" },
		{ "HU","HUN" }, { "ID","INA" }, { "IE","IRL" }, { "IL","ISR" }, { "IM","GBR" },
		{ "IN","IND" }, { "IO","GBR" },
		{ "IQ","IRQ" }, { "IR","IRI" }, { "IS","ISL" }, { "IT","ITA" }, { "JE","GBR" },
		{ "JM","JAM" }, { "JO","JOR" }, { "JP","JPN" }, { "KE","KEN" }, { "KG","KGZ" },
		{ "KH","CAM" }, { "KI","KIR" }, { "KM","COM" }, { "KN","SKN" }, { "KP","PRK" },
		{ "KR","KOR" }, { "KW","KUW" }, { "KY","CAY" }, { "KZ","KAZ" }, { "LA","LAO" },
		{ "LB","LBN" }, { "LC","LCA" }, { "LI","LIE" }, { "LK","SRI" }, { "LR","LBR" },
		{ "LS","LES" }, { "LT","LTU" }, { "LU","LUX" }, { "LV","LAT" }, { "LY","LBA" },
		{ "MA","MAR" }, { "MC","MON" }, { "MD","MDA" }, { "ME","MNE" }, { "MF","FRA" },
		{ "MG","MAD" }, { "MH","MHL" }, { "MK","MKD" }, { "ML","MLI" }, { "MM","MYA" },
		{ "MN","MGL" }, { "MO","CHN" },
		{ "MP","USA" },
		{ "MQ","FRA" },
		{ "MR","MTN" }, { "MS","GBR" },
		{ "MT","MLT" }, { "MU","MRI" }, { "MV","MDV" }, { "MW","MAW" }, { "MX","MEX" },
		{ "MY","MAS" }, { "MZ","MOZ" }, { "NA","NAM" }, { "NC","FRA" },
		{ "NE","NIG" }, { "NF","AUS" },
		{ "NG","NGR" }, { "NI","NCA" }, { "NL","NED" }, { "NO","NOR" }, { "NP","NEP" },
		{ "NR","NRU" }, { "NU","NZL" },
		{ "NZ","NZL" }, { "OM","OMA" }, { "PA","PAN" }, { "PE","PER" }, { "PF","FRA" },
		{ "PG","PNG" }, { "PH","PHI" }, { "PK","PAK" }, { "PL","POL" }, { "PM","FRA" },
		{ "PN","GBR" },
		{ "PR","PUR" }, { "PS","PLE" }, { "PT","POR" }, { "PW","PLW" }, { "PY","PAR" },
		{ "QA","QAT" }, { "RE","FRA" },
		{ "RO","ROU" }, { "RS","SRB" }, { "RU","RUS" }, { "RW","RWA" }, { "SA","KSA" },
		{ "SB","SOL" }, { "SC","SEY" }, { "SD","SUD" }, { "SE","SWE" }, { "SG","SGP" },
		{ "SH","GBR" },
		{ "SI","SLO" }, { "SJ","NOR" },
		{ "SK","SVK" }, { "SL","SLE" }, { "SM","SMR" }, { "SN","SEN" }, { "SO","SOM" },
		{ "SR","SUR" }, { "SS","SSD" },
		{ "ST","STP" }, { "SV","ESA" }, { "SX","NED" },
		{ "SY","SYR" }, { "SZ","SWZ" },
		{ "TC","GBR" },
		{ "TD","CHA" }, { "TF","FRA" },
		{ "TG","TOG" }, { "TH","THA" }, { "TJ","TJK" }, { "TK","NZL" },
		{ "TL","TLS" }, { "TM","TKM" }, { "TN","TUN" }, { "TO","TGA" }, { "TR","TUR" },
		{ "TT","TTO" }, { "TV","TUV" }, { "TW","TPE" },
		{ "TZ","TAN" }, { "UA","UKR" }, { "UG","UGA" }, { "UM","USA" },
		{ "US","USA" }, { "UY","URU" }, { "UZ","UZB" }, { "VA","ITA" },
		{ "VC","VIN" }, { "VE","VEN" }, { "VG","IVB" }, { "VI","ISV" }, { "VN","VIE" },
		{ "VU","VAN" }, { "WF","FRA" },
		{ "WS","SAM" }, { "XK","KOS" },
		{ "YE","YEM" }, { "YT","FRA" },
		{ "ZA","RSA" }, { "ZM","ZAM" }, { "ZW","ZIM" },
	};
	return table;
}

bool IsAllAlpha(const string & s)
{
	for (size_t i = 0; i < s.size(); ++i)
		if (!std::isalpha((unsigned char) s[i])) return false;
	return true;
}

bool IsAllUpper(const string & s)
{
	for (size_t i = 0; i < s.size(); ++i)
		if (!std::isupper((unsigned char) s[i])) return false;
	return true;
}

string ToUpper(const string & s)
{
	string out = s;
	for (size_t i = 0; i < out.size(); ++i)
		out[i] = (char) std::toupper((unsigned char) out[i]);
	return out;
}

} // anonymous namespace

string	NormalizeCountryToIoc(const string & raw_country)
{
	if (raw_country.empty()) return string();

	const std::unordered_map<string, string> & dirty = DirtyAptDatToIoc();
	std::unordered_map<string, string>::const_iterator it = dirty.find(raw_country);
	if (it != dirty.end()) return it->second;

	const std::unordered_map<string, string> & iso3 = Iso3ToIocOverrides();
	it = iso3.find(raw_country);
	if (it != iso3.end()) return it->second;

	// Most ISO3 codes ARE already what we want (e.g. NOR, FRA, JPN) - only
	// the ones in Iso3ToIocOverrides() needed remapping.
	if (raw_country.size() == 3 && IsAllAlpha(raw_country) && IsAllUpper(raw_country))
		return raw_country;

	if (raw_country.size() == 2 && IsAllAlpha(raw_country))
	{
		const std::unordered_map<string, string> & iso2 = Iso2ToIoc();
		it = iso2.find(ToUpper(raw_country));
		if (it != iso2.end()) return it->second;
	}

	return string();
}

string	IcaoPrefixIocOverride(const string & icao)
{
	// ICAO region prefix "BK" is assigned exclusively to Kosovo. apt.dat's own
	// airports there carry "1302 country SRB" (Serbia) - not a data-entry
	// mistake NormalizeCountryToIoc could catch (SRB is a perfectly valid ISO3
	// code, just for the wrong side of a real, live dispute over this specific
	// territory). IOC itself has given Kosovo its own seat since 2014 ("KOS"),
	// so this table exists specifically to keep this feature internally
	// consistent with IOC's own stance rather than silently inheriting
	// apt.dat's community-contributed political framing on this one case.
	//
	// ICAO region prefix "VH" is Hong Kong's, in full - but apt.dat's own data
	// is internally INCONSISTENT about it: the flagship airport, VHHH (Hong
	// Kong Intl), carries "1302 country CHN" directly, while several minor
	// Hong Kong heliports (VHSS, XVH0002, XVH0003) carry "1302 country HKG"
	// instead - same territory, two different raw values, neither a typo,
	// just different community contributors' own framing. IOC has one
	// consistent, current, real seat for Hong Kong ("HKG", "Hong Kong,
	// China") with its own flag, separate from mainland China - see project
	// notes. Overriding by ICAO prefix (same mechanism as Kosovo below) makes
	// every Hong Kong airport agree with IOC's actual stance regardless of
	// which value that specific airport's own apt.dat entry happens to use.
	if (icao.size() >= 2 && icao[0] == 'V' && icao[1] == 'H')
		return string("HKG");

	// Deliberately NOT a general "disputed territory" table: every other
	// contested region checked during this feature's design (Crimea, Northern
	// Cyprus, Nagorno-Karabakh, Abkhazia, Somaliland) already produces IOC's
	// own stance either because apt.dat's raw country value already matches it,
	// or because IOC itself doesn't give that territory a separate seat either
	// - so there was nothing to override. Kosovo and Hong Kong are the two
	// confirmed cases where apt.dat's data and IOC's stance actually diverge.
	if (icao.size() >= 2 && icao[0] == 'B' && icao[1] == 'K')
		return string("KOS");

	return string();
}
