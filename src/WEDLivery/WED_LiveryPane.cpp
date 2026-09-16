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

#include "WED_LiveryPane.h"
#include "WED_LiveryData.h"
#include "WED_AirportDatabase.h"
#include "WED_FlagAssets.h"
#include "WED_FlagProjector.h"
#include "WED_FlagIndex.h"
#include "WED_LibraryMgr.h"		// card strip placeholder object picker - see PickPlaceholderObjectVpaths()

#include "WED_RampPosition.h"
#include "WED_Airport.h"
#include "WED_Archive.h"
#include "WED_Persistent.h"		// pulls in the StartCommand(x) convenience macro
#include "WED_Messages.h"
#include "WED_ToolUtils.h"		// WED_GetSelect, WED_GetParentAirport
#include "WED_EnumSystem.h"		// ramp_operation_*, width_A..width_F
#include "WED_MandatoryHeader.h"	// WedDataFileDir() - where the loose .txt data files live
#include "PlatformUtils.h"		// DIR_STR, GetApplicationPath()
#include "FileUtils.h"			// FILE_get_dir_name()
#include "WED_Globals.h"		// gPromptLiveriesOnRampSelect
#include "ISelection.h"
#include "GUI_TabPane.h"
#include "GUI_GraphState.h"
#include "GUI_Fonts.h"
#include "GUI_Resources.h"		// GUI_GetTextureResource - the selected-card tick
#include "TexUtils.h"			// tex_Linear / tex_Mipmap
#include "GUI_TextField.h"
#include "GUI_Messages.h"		// GUI_TEXT_FIELD_TEXT_CHANGED
#include "WED_Colors.h"

#include <sstream>
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <cmath>
#include <cstdlib>
#include <ctime>
#include <map>

#if APL
	#include <OpenGL/gl.h>
#else
	// GL_BGRA (used to upload the flag banner texture without a manual
	// channel swap) isn't in plain <GL/gl.h> on Windows - glew.h is this
	// codebase's existing way to get it (see e.g. WED_LibraryPreviewPane.cpp).
	#include "glew.h"
#endif

using std::string;
using std::set;
using std::vector;
using std::map;

// Forward declaration - defined below (right before WED_LiveryPane::ToggleCode()), needed
// by GetPopularAirlinesCodes() (in the anonymous namespace further down) to read the
// current ramp's already-checked codes.
static set<string> ParseCodes(const string & airlines);

namespace
{
	const int kFilterEnumTable[5]  = { ramp_operation_None, ramp_operation_GeneralAviation, ramp_operation_Airline, ramp_operation_Cargo, ramp_operation_Military };
	const char * kFilterLabels[5]  = { "None", "GA", "Airline", "Cargo", "Military" };
	const int kWidthOrder[6]       = { width_A, width_B, width_C, width_D, width_E, width_F };
	const char * kWidthLabels[6]   = { "A", "B", "C", "D", "E", "F" };

	// GUI_TabPane has no by-name pane lookup - tabs are purely positional
	// (whatever order WED_DocumentWindow.cpp's AddPane() calls happen in).
	// Keep these in sync with that file if its tab order ever changes.
	const int kSelectionTabIndex = 0;	// "Selection" - added first
	const int kLiveryTabIndex    = 5;	// "Static+Liveries" - this pane

	int WidthEnumToIndex(int enum_val)
	{
		for (int i = 0; i < 6; ++i)
			if (kWidthOrder[i] == enum_val) return i;
		return 2;
	}

	int IndexToWidthEnum(int idx)
	{
		if (idx < 0) idx = 0;
		if (idx > 5) idx = 5;
		return kWidthOrder[idx];
	}

	// Maps the ramp's own Ramp Operation Type onto this file's placeholder livery
	// categories. -1 means "no filter, show everything" (matches the "None" chip).
	int RampOpToLiveryCategory(int ramp_op_enum)
	{
		if (ramp_op_enum == ramp_operation_GeneralAviation) return wed_LiveryOp_GeneralAviation;
		if (ramp_op_enum == ramp_operation_Airline)         return wed_LiveryOp_Airline;
		if (ramp_op_enum == ramp_operation_Cargo)            return wed_LiveryOp_Cargo;
		if (ramp_op_enum == ramp_operation_Military)         return wed_LiveryOp_Military;
		return -1;
	}

	// One row of the airline checklist. Only wed_Row_Airline rows are
	// clickable/checkable (see RowForY() callers) - the others are purely
	// structural, used to lay out the "Show Recommendation" split (a section
	// header, a blank gap row, and a divider line row - see BuildDisplayRows()).
	enum WED_LiveryRowKind { wed_Row_Airline, wed_Row_Header, wed_Row_Gap, wed_Row_Divider };
	struct WED_LiveryDisplayRow
	{
		WED_LiveryRowKind	kind;
		string				icao;			// lowercase, matches WED_RampPosition::CorrectAirlinesString's convention - wed_Row_Airline only
		string				name;			// display name if known, else empty (falls back to just the code) - wed_Row_Airline only
		string				header_text;	// wed_Row_Header only
	};

	// Case-insensitive substring test. `needle_lower` must already be
	// lowercased by the caller (it's checked against many rows per call, no
	// sense lowercasing it more than once).
	bool ContainsCaseInsensitive(const string & haystack, const string & needle_lower)
	{
		if (needle_lower.empty()) return true;
		string h = haystack;
		for (string::iterator c = h.begin(); c != h.end(); ++c)
			*c = (char) tolower((unsigned char) *c);
		return h.find(needle_lower) != string::npos;
	}

	// kWED_PlaceholderAirlines only has friendly names for its own ~26 entries -
	// a per-airport Recommended code from WED_AirportDatabase.txt won't
	// usually be one of them. Case-insensitive; returns "" (not found) rather
	// than guessing.
	string FindPlaceholderName(const string & icao_lower)
	{
		for (int i = 0; i < kWED_PlaceholderAirlineCount; ++i)
			if (icao_lower == kWED_PlaceholderAirlines[i].icao)
				return kWED_PlaceholderAirlines[i].name;
		return string();
	}

	// Same idea as FindPlaceholderName(), but checked first against the ~1500-
	// entry global WED_AirlineDirectory.txt (which is what actually knows most
	// airlines' names) and only falls back to the small hardcoded placeholder
	// table above for the handful of well-known majors it lists. "" if neither
	// source has it - callers fall back to showing the bare code.
	string ResolveAirlineName(const string & icao_lower, const WED_AirlineDirectory & directory)
	{
		string name = directory.GetName(icao_lower);
		if (!name.empty()) return name;
		return FindPlaceholderName(icao_lower);
	}

	bool CompareRowsByIcao(const WED_LiveryDisplayRow & a, const WED_LiveryDisplayRow & b)
	{
		return a.icao < b.icao;
	}

	void SortAirlineRows(vector<WED_LiveryDisplayRow> & rows, bool descending)
	{
		std::sort(rows.begin(), rows.end(), CompareRowsByIcao);
		if (descending) std::reverse(rows.begin(), rows.end());
	}


	// Tier 1 data source: a small, hand-curated "always show this specific airline at this
	// specific airport" override list - e.g. Stratolaunch only ever operates from Mojave. Not
	// backed by a real file yet (that's a separate engineering item: a manual-recommendation
	// data file, same loose-file-next-to-WED.exe convention as WED_AirportDatabase.txt);
	// returns empty until that lands. Kept as its own function/call site now so wiring that
	// file in later is a one-function change, not a new plumbing pass through
	// BuildDisplayRows/BuildCurrentDisplayRows/Draw()/MouseDown()/MouseUp().
	vector<string> GetManualRecommendedCodes(const string & icao_raw)
	{
		(void) icao_raw;
		return vector<string>();
	}

	// Builds one recommendation section - header, then its airline rows - appending nothing at
	// all if it ends up empty (either `codes_upper` was empty to begin with, every code in it was
	// already claimed by a higher-priority tier via `seen`, or the search query filtered all of
	// them out). Codes are matched case-insensitively; whichever ones survive are added to `seen`
	// so no later tier shows the same airline again. `leading_divider` should be whatever the
	// caller's "have I emitted a section yet" flag currently is - true prints a blank-gap+divider
	// pair before this section's own header, exactly like the old fixed two-section layout did
	// between "Recommended" and "All Results". Returns true if it appended anything (i.e. the
	// caller's "emitted a section yet" flag should become/stay true).
	bool AppendAirlineSection(const string & label, const vector<string> & codes_upper,
								const WED_AirlineDirectory & directory, bool sort_descending,
								const string & query_lower, set<string> & seen, bool leading_divider,
								vector<WED_LiveryDisplayRow> & out, bool preserve_order = false)
	{
		vector<WED_LiveryDisplayRow> section;
		for (size_t i = 0; i < codes_upper.size(); ++i)
		{
			string lower = codes_upper[i];
			for (string::iterator c = lower.begin(); c != lower.end(); ++c)
				*c = (char) tolower((unsigned char) *c);

			if (seen.count(lower)) continue;		// already shown in a higher-priority tier

			WED_LiveryDisplayRow r;
			r.kind = wed_Row_Airline;
			r.icao = lower;
			r.name = ResolveAirlineName(lower, directory);		// "" if we don't have a friendly name for it yet
			section.push_back(r);
			seen.insert(lower);
		}
		// `preserve_order` is for a caller (Popular Airlines) whose input order is
		// itself the whole point (pinned-checked-first, then a weighted random draw) -
		// alphabetizing it the same as every other tier would throw that away.
		if (!preserve_order)
			SortAirlineRows(section, sort_descending);

		if (!query_lower.empty())
		{
			vector<WED_LiveryDisplayRow> filtered;
			for (size_t i = 0; i < section.size(); ++i)
				if (ContainsCaseInsensitive(section[i].icao, query_lower) || ContainsCaseInsensitive(section[i].name, query_lower))
					filtered.push_back(section[i]);
			section.swap(filtered);
		}

		if (section.empty()) return false;

		if (leading_divider)
		{
			WED_LiveryDisplayRow gap;	gap.kind = wed_Row_Gap;		out.push_back(gap);
			WED_LiveryDisplayRow div;	div.kind = wed_Row_Divider;	out.push_back(div);
		}

		WED_LiveryDisplayRow header;
		header.kind = wed_Row_Header;
		header.header_text = label;
		out.push_back(header);

		WED_LiveryDisplayRow gap2; gap2.kind = wed_Row_Gap; out.push_back(gap2);
		out.insert(out.end(), section.begin(), section.end());
		return true;
	}

	// "Popular Airlines" tier: up to 10 display slots drawn from the 30 largest fleets in
	// WED_AirlineDirectory (WED_AirlineDirectory::GetAllSortedByFleetDesc()). Order is
	// NOT alphabetical the way every other tier's is - it's a per-airport, per-WED-session
	// random draw, weighted 75% toward airlines sharing the airport's own country (so a big
	// domestic carrier is more likely to appear than an equally large foreign one, without
	// making foreign ones impossible to see).
	//
	// Any code already checked on the CURRENT ramp that's also in the 30-pool is pinned to
	// the front, in fleet-size order - so ticking a box can never make that airline vanish
	// from this tier on a later reshuffle/redraw (e.g. if JAL is already checked, it's
	// guaranteed a slot at the top, leaving 9 more slots for the random draw instead of 10).
	//
	// The random draw itself is computed ONCE per airport ICAO and cached in `cache` (owned
	// by the pane, keyed by airport) - re-viewing the same airport later in this session
	// reuses the exact same draw, so the list doesn't reshuffle out from under the user on
	// every unrelated redraw. A different airport gets its own independent draw.
	vector<string> GetPopularAirlinesCodes(const WED_AirlineDirectory & directory, const string & airport_country_ioc,
											const string & airport_icao, const set<string> & checked_lower,
											map<string, vector<string> > & cache)
	{
		const int kPoolSize     = 30;
		const int kDisplaySlots = 10;

		const vector<WED_AirlineDirectoryEntry> & by_fleet = directory.GetAllSortedByFleetDesc();
		int pool_n = (int) by_fleet.size();
		if (pool_n > kPoolSize) pool_n = kPoolSize;

		map<string, vector<string> >::iterator cache_it = cache.find(airport_icao);
		if (cache_it == cache.end() || (int) cache_it->second.size() != pool_n)
		{
			vector<string> domestic, foreign;
			for (int i = 0; i < pool_n; ++i)
			{
				if (!airport_country_ioc.empty() && by_fleet[i].country == airport_country_ioc)
					domestic.push_back(by_fleet[i].code);
				else
					foreign.push_back(by_fleet[i].code);
			}

			vector<string> shuffled;
			while (!domestic.empty() || !foreign.empty())
			{
				bool take_domestic;
				if (domestic.empty())      take_domestic = false;
				else if (foreign.empty())  take_domestic = true;
				else                       take_domestic = (rand() % 100) < 75;

				vector<string> & bucket = take_domestic ? domestic : foreign;
				int idx = rand() % (int) bucket.size();
				shuffled.push_back(bucket[idx]);
				bucket.erase(bucket.begin() + idx);
			}

			cache_it = cache.insert(std::make_pair(airport_icao, shuffled)).first;
		}
		const vector<string> & shuffle_order = cache_it->second;

		vector<string> pinned;
		for (int i = 0; i < pool_n; ++i)
		{
			string lower = by_fleet[i].code;
			for (string::iterator c = lower.begin(); c != lower.end(); ++c)
				*c = (char) tolower((unsigned char) *c);
			if (checked_lower.count(lower))
				pinned.push_back(by_fleet[i].code);
		}

		vector<string> result = pinned;
		set<string> result_set(pinned.begin(), pinned.end());
		for (size_t i = 0; i < shuffle_order.size() && (int) result.size() < kDisplaySlots; ++i)
			if (!result_set.count(shuffle_order[i]))
			{
				result.push_back(shuffle_order[i]);
				result_set.insert(shuffle_order[i]);
			}

		return result;
	}

	// Builds the full ordered row list for the checklist, honoring the ramp-op category filter,
	// the "None" special case (empty - caller shows a plain message instead of a list), the
	// current sort direction, and the "Show Recommendation" split.
	//
	// When "Show Recommendation" is on, the list is built as up to five sections, most to least
	// specific, each one only shown if it actually has something in it once the tiers above it
	// have claimed their codes:
	//   1. Manual Recommendations - GetManualRecommendedCodes() (empty today - see its own doc
	//      comment)
	//   2. Recommended             - this specific airport's own hand-researched airline list
	//                                (`direct_hit_codes_upper`, from WED_AirportDatabase)
	//   3. Popular Airlines        - see GetPopularAirlinesCodes()'s own doc comment: up to 10
	//                                slots, weighted-random per airport/session, drawn from the
	//                                30 largest fleets in WED_AirlineDirectory. Order is NOT
	//                                alphabetical like every other tier's (preserve_order=true)
	//   4. Same Country            - every WED_AirlineDirectory entry whose country matches the
	//                                airport's own IOC-normalized country (empty section if the
	//                                airport's country couldn't be determined at all)
	//   5. All Airlines            - the small hardcoded WED_LiveryData.h placeholder list (same
	//                                set "All Results" used to show unconditionally), minus
	//                                whatever's already appeared above
	// Tiers 3 and 4 draw from WED_AirlineDirectory, which has no op_type (Airline/Cargo/GA/
	// Military) classification - see WED_AirlineDirectory.h - so, like tier 2 always has, they
	// are NOT filtered by the ramp's operation category. Only tier 5 can be (and is), since
	// WED_LiveryData.h is the only source that carries an op_type at all.
	//
	// `search_query` is matched case-insensitively against both icao and name; pass "" to disable
	// filtering entirely. Filtering happens per-section, AFTER that section's own sort - a section
	// whose matches are filtered down to zero rows loses its header (and any gap/divider it would
	// have introduced) too, rather than showing an empty label with nothing under it.
	void BuildDisplayRows(int ramp_op_enum, bool sort_descending, bool show_recommendation,
							const vector<string> & manual_codes_upper, const vector<string> & direct_hit_codes_upper,
							const string & airport_country_ioc, const string & airport_icao,
							const set<string> & checked_lower, map<string, vector<string> > & popular_cache,
							const string & search_query,
							const WED_AirlineDirectory & directory, vector<WED_LiveryDisplayRow> & out)
	{
		out.clear();
		if (ramp_op_enum == ramp_operation_None)
			return;		// "No static aircraft will spawn at this spot" - see Draw()

		string query_lower = search_query;
		for (string::iterator c = query_lower.begin(); c != query_lower.end(); ++c)
			*c = (char) tolower((unsigned char) *c);

		int category = RampOpToLiveryCategory(ramp_op_enum);

		// The one tier with an op_type to filter by - see this function's own doc comment above.
		vector<WED_LiveryDisplayRow> all_rows;
		for (int i = 0; i < kWED_PlaceholderAirlineCount; ++i)
			if (category < 0 || kWED_PlaceholderAirlines[i].op_type == category)
			{
				WED_LiveryDisplayRow r;
				r.kind = wed_Row_Airline;
				r.icao = kWED_PlaceholderAirlines[i].icao;		// already lowercase
				r.name = kWED_PlaceholderAirlines[i].name;
				all_rows.push_back(r);
			}
		SortAirlineRows(all_rows, sort_descending);

		if (!query_lower.empty())
		{
			vector<WED_LiveryDisplayRow> filtered;
			for (size_t i = 0; i < all_rows.size(); ++i)
				if (ContainsCaseInsensitive(all_rows[i].icao, query_lower) || ContainsCaseInsensitive(all_rows[i].name, query_lower))
					filtered.push_back(all_rows[i]);
			all_rows.swap(filtered);
		}

		if (!show_recommendation)
		{
			out = all_rows;
			return;
		}

		// A code already placed by an earlier (higher-priority) tier is skipped by every later
		// one - see AppendAirlineSection()'s `seen` parameter - so nothing appears twice even
		// though the tiers' code lists can legitimately overlap (an airline can easily be both a
		// direct hit AND happen to also have one of the biggest fleets in the directory).
		set<string> seen;
		bool any = false;

		any |= AppendAirlineSection("Manual Recommendations", manual_codes_upper, directory,
										sort_descending, query_lower, seen, any, out);

		any |= AppendAirlineSection("Recommended", direct_hit_codes_upper, directory,
										sort_descending, query_lower, seen, any, out);

		vector<string> popular_codes = GetPopularAirlinesCodes(directory, airport_country_ioc, airport_icao,
																	checked_lower, popular_cache);
		any |= AppendAirlineSection("Popular Airlines", popular_codes, directory,
										sort_descending, query_lower, seen, any, out, /*preserve_order=*/true);

		vector<string> same_country_codes;
		if (!airport_country_ioc.empty())
		{
			vector<const WED_AirlineDirectoryEntry *> matches;
			directory.GetByCountry(airport_country_ioc, matches);
			for (size_t i = 0; i < matches.size(); ++i)
				same_country_codes.push_back(matches[i]->code);
		}
		any |= AppendAirlineSection("Same Country", same_country_codes, directory,
										sort_descending, query_lower, seen, any, out);

		// Tier 5 draws from all_rows (already category-filtered and search-filtered above, per
		// this function's own doc comment) rather than going through AppendAirlineSection() -
		// it needs no further name resolution (kWED_PlaceholderAirlines already has names) and
		// must NOT be re-filtered by search a second time.
		//
		// NOT YET COLLAPSIBLE: always rendered fully expanded, same as the old "All Results"
		// section was. A real expand/collapse toggle needs its own WED_LiveryRowKind plus matching
		// changes to RowForY()/MouseDown()/MouseUp()/Draw()'s row-kind switch - deliberately left
		// for a follow-up pass rather than bundled into this one.
		vector<WED_LiveryDisplayRow> remaining;
		for (size_t i = 0; i < all_rows.size(); ++i)
			if (!seen.count(all_rows[i].icao))
				remaining.push_back(all_rows[i]);

		if (!remaining.empty())
		{
			if (any)
			{
				WED_LiveryDisplayRow gap;	gap.kind = wed_Row_Gap;		out.push_back(gap);
				WED_LiveryDisplayRow div;	div.kind = wed_Row_Divider;	out.push_back(div);
			}
			WED_LiveryDisplayRow header2;
			header2.kind = wed_Row_Header;
			header2.header_text = "All Airlines";
			out.push_back(header2);

			WED_LiveryDisplayRow gap2; gap2.kind = wed_Row_Gap; out.push_back(gap2);
			out.insert(out.end(), remaining.begin(), remaining.end());
		}
	}

	enum WED_IcaoLookupResult
	{
		wed_Icao_Ok,				// found - out_country is the already IOC-normalized country for this ICAO
		wed_Icao_Placeholder,		// "xxxx" (WED's own default) or "zz.." (ICAO's reserved unassigned prefix)
		wed_Icao_NotFound,			// the database is loaded, but doesn't know this ICAO (or its country didn't normalize)
		wed_Icao_IndexUnavailable	// couldn't find/parse WED_AirportDatabase.txt at all (index stays empty)
	};

	// Lazily loads WED_AirportDatabase from the loose file shipped next to
	// WED.exe on first use, then does an exact-match O(1) lookup against it.
	// The country value it returns is already IOC-normalized (see
	// WED_AirportDatabase.cpp) - no local X-Plane install/apt.dat involved.
	WED_IcaoLookupResult LookupIcaoCountry(WED_AirportDatabase & db, const string & icao_raw, string & out_country)
	{
		string icao;
		for (string::const_iterator c = icao_raw.begin(); c != icao_raw.end(); ++c)
			icao += (char) toupper((unsigned char) *c);

		if (icao.empty() || icao == "XXXX")
			return wed_Icao_Placeholder;
		if (icao.size() >= 2 && icao.substr(0, 2) == "ZZ")
			return wed_Icao_Placeholder;

		if (!db.IsLoaded() && !db.LoadFailed())
			db.EnsureLoaded(WedDataFileDir() + "WED_AirportDatabase.txt");

		if (!db.IsLoaded())
			return wed_Icao_IndexUnavailable;

		if (db.GetCountry(icao, out_country))
			return wed_Icao_Ok;

		return wed_Icao_NotFound;
	}

	// Whether `icao` is commercially served at all - i.e. present in
	// WED_AirportDatabase.txt (every row there is, by construction, an
	// OurAirports-flagged scheduled-service airport) UNLESS that row has been
	// hand-confirmed to have none (the "<NA>" sentinel - see
	// WED_AirportDatabase.h's IsCommercial() doc comment).
	bool AirportIsCommercial(WED_AirportDatabase & db, const string & icao_raw)
	{
		string icao;
		for (string::const_iterator c = icao_raw.begin(); c != icao_raw.end(); ++c)
			icao += (char) toupper((unsigned char) *c);
		if (icao.empty()) return false;

		if (!db.IsLoaded() && !db.LoadFailed())
			db.EnsureLoaded(WedDataFileDir() + "WED_AirportDatabase.txt");

		return db.IsLoaded() && db.IsCommercial(icao);
	}

	// The specific airline codes (uppercase, as stored) WED_AirportDatabase.txt
	// has for this airport, or empty if it's not in there at all (no research
	// yet, or an explicit "<NA>" confirmed-none result - both look the same
	// here: "nothing to recommend").
	vector<string> GetRecommendedAirlineCodes(WED_AirportDatabase & db, const string & icao_raw)
	{
		string icao;
		for (string::const_iterator c = icao_raw.begin(); c != icao_raw.end(); ++c)
			icao += (char) toupper((unsigned char) *c);

		vector<string> out;
		if (!icao.empty() && db.IsLoaded())
			db.GetAirlines(icao, out);
		return out;
	}

	// One place that pulls together everything BuildDisplayRows() needs from
	// the pane's own state - MouseMove/MouseDown/MouseUp/Draw all call this
	// exact same function so hit-testing and drawing can never see different
	// row lists for the same frame.
	vector<WED_LiveryDisplayRow> BuildCurrentDisplayRows(WED_RampPosition * primary_ramp, bool sort_descending,
							bool show_recommendation, WED_AirportDatabase & airport_db, const string & current_icao,
							const string & search_query, WED_AirlineDirectory & directory,
							map<string, vector<string> > & popular_cache)
	{
		vector<WED_LiveryDisplayRow> rows;
		if (!primary_ramp) return rows;

		if (!directory.IsLoaded() && !directory.LoadFailed())
			directory.EnsureLoaded(WedDataFileDir() + "WED_AirlineDirectory.txt");

		vector<string> manual, direct_hit;
		string airport_country;
		set<string> checked_lower = ParseCodes(primary_ramp->GetAirlines());
		if (show_recommendation)
		{
			manual = GetManualRecommendedCodes(current_icao);
			direct_hit = GetRecommendedAirlineCodes(airport_db, current_icao);
			if (LookupIcaoCountry(airport_db, current_icao, airport_country) != wed_Icao_Ok)
				airport_country.clear();
		}

		BuildDisplayRows(primary_ramp->GetRampOperationType(), sort_descending, show_recommendation,
							manual, direct_hit, airport_country, current_icao, checked_lower, popular_cache,
							search_query, directory, rows);
		return rows;
	}

	// Greedy word-wrap: splits `text` into lines that each fit within max_w
	// pixels (measured with GUI_MeasureRange), breaking only at word
	// boundaries - never mid-word. A single word wider than max_w on its own
	// still gets its own line rather than being force-broken (an overfull
	// line reads better than a word split mid-letter).
	vector<string> WrapText(int font, const string & text, float max_w)
	{
		vector<string> lines;
		if (text.empty()) return lines;

		std::istringstream iss(text);
		string word, line;
		while (iss >> word)
		{
			string candidate = line.empty() ? word : line + " " + word;
			float w = GUI_MeasureRange(font, candidate.c_str(), candidate.c_str() + candidate.size());
			if (line.empty() || w <= max_w)
				line = candidate;
			else
			{
				lines.push_back(line);
				line = word;
			}
		}
		if (!line.empty()) lines.push_back(line);
		return lines;
	}

	void DrawFilledCircle(float cx, float cy, float r)
	{
		const int kSegs = 20;
		glBegin(GL_TRIANGLE_FAN);
			glVertex2f(cx, cy);
			for (int i = 0; i <= kSegs; ++i)
			{
				float a = (float) i / (float) kSegs * 6.2831853f;
				glVertex2f(cx + r * cosf(a), cy + r * sinf(a));
			}
		glEnd();
	}

	void DrawCircleOutline(float cx, float cy, float r)
	{
		const int kSegs = 20;
		glBegin(GL_LINE_LOOP);
			for (int i = 0; i < kSegs; ++i)
			{
				float a = (float) i / (float) kSegs * 6.2831853f;
				glVertex2f(cx + r * cosf(a), cy + r * sinf(a));
			}
		glEnd();
	}

	// Three short vertical "grip" dashes centered on a slider ball, signaling
	// it's a draggable handle (independent of hover/drag highlighting).
	void DrawGrabberDashes(float cx, float cy, float r)
	{
		float dash_h = r * 1.1f;
		glBegin(GL_LINES);
			for (int i = -1; i <= 1; ++i)
			{
				float x = cx + i * r * 0.4f;
				glVertex2f(x, cy - dash_h * 0.5f);
				glVertex2f(x, cy + dash_h * 0.5f);
			}
		glEnd();
	}

	// Small filled 5-point star - drawn as a vector shape (not a font glyph)
	// for the same reason the sort button's arrow is: no dependency on this
	// bitmap font actually having a star character available.
	void DrawStar(float cx, float cy, float r_outer, float r_inner)
	{
		const int kPoints = 5;
		glBegin(GL_TRIANGLE_FAN);
			glVertex2f(cx, cy);
			for (int i = 0; i <= kPoints * 2; ++i)
			{
				float a = (float) i / (float) (kPoints * 2) * 6.2831853f - 1.5707963f;	// start pointing up
				float r = (i % 2 == 0) ? r_outer : r_inner;
				glVertex2f(cx + r * cosf(a), cy + r * sinf(a));
			}
		glEnd();
	}

	// The search box's "clear" glyph, once it has an entry to clear.
	void DrawX(float cx, float cy, float r)
	{
		glBegin(GL_LINES);
			glVertex2f(cx - r, cy - r); glVertex2f(cx + r, cy + r);
			glVertex2f(cx - r, cy + r); glVertex2f(cx + r, cy - r);
		glEnd();
	}
}

static int CollectRamps(ISelectable * who, void * ref)
{
	vector<WED_RampPosition *> * out = (vector<WED_RampPosition *> *) ref;
	WED_RampPosition * r = SAFE_CAST(WED_RampPosition, who);
	if (r) out->push_back(r);
	return 0;		// keep iterating - we want the whole selection, not just the first hit
}

static set<string> ParseCodes(const string & airlines)
{
	set<string> out;
	std::istringstream iss(airlines);
	string tok;
	while (iss >> tok) out.insert(tok);
	return out;
}

static string CodesToString(const set<string> & codes)
{
	std::ostringstream oss;
	bool first = true;
	for (set<string>::const_iterator i = codes.begin(); i != codes.end(); ++i)
	{
		if (!first) oss << ' ';
		oss << *i;
		first = false;
	}
	return oss.str();
}

// How many preview cards sit above the checklist. Single source of truth: the
// placeholder picker, CardsBlockHeight() and Draw()'s range math all use it, so they
// can't drift out of sync with each other.
static const int kCardCount = 4;

// Aspect of a card's image area. MUST match kThumbW/kThumbH in
// WED_LiveryThumbnailCache.cpp - that's the shape the cached texture is rendered at,
// and the card is just a quad displaying it, so a mismatch would letterbox or stretch
// it. Used by both CardHeight() and Draw()'s image quad, which must also agree with
// each other (they once didn't - see CardHeight()'s comment).
static const float kCardImageAspect = 32.0f / 9.0f;

// Cards are laid out as a grid of "trading cards": kCardCols per row, with a fixed
// gap on every side and between them. The gap is what actually makes each card read
// as its own object rather than one continuous strip, so it stays a fixed pixel
// value rather than scaling with the pane.
static const int   kCardCols = 2;
static const float kCardGap  = 6.0f;

// Card placeholder (framework only - no airline/aircraft matching yet, see
// WED_LiveryThumbnailCache.h). Walks the library tree collecting the first `n` plain
// .obj resources it finds, with zero regard for whether they look anything like an
// aircraft - this exists purely to prove the render/cache/evict pipeline against SOME
// real .obj data. Recurses depth-first; gives up once `out` has n entries.
static void CollectPlaceholderObjectVpaths(WED_LibraryMgr * lib_mgr, const string & dir, int n, vector<string> & out)
{
	if ((int) out.size() >= n) return;

	vector<string> children;
	lib_mgr->GetResourceChildren(dir, pack_Library, children);
	for (vector<string>::const_iterator c = children.begin(); c != children.end() && (int) out.size() < n; ++c)
	{
		res_type t = lib_mgr->GetResourceType(*c);
		if (t == res_Directory)
			CollectPlaceholderObjectVpaths(lib_mgr, *c, n, out);
		else if (t == res_Object)
			out.push_back(*c);
	}
}

static vector<string> PickPlaceholderObjectVpaths(WED_LibraryMgr * lib_mgr, int n)
{
	vector<string> out;
	if (lib_mgr)
		CollectPlaceholderObjectVpaths(lib_mgr, string(), n, out);
	return out;
}

WED_LiveryPane::WED_LiveryPane(
						IResolver *		resolver,
						WED_Archive *	archive,
						GUI_TabPane *	host_tabs) :
	mResolver(resolver),
	mArchive(archive),
	mHostTabs(host_tabs),
	mAutoSwitchedAwayOnEmpty(false),
	mTrackRow(-1),
	mTrackFilterChip(-1),
	mHoverFilterChip(-1),
	mHoverRow(-1),
	mHoverSliderHandle(-1),
	mSortDescending(false),
	mHoverSortButton(false),
	mTrackSortButton(false),
	mHoverRecommendButton(false),
	mTrackRecommendButton(false),
	mDragHandle(-1),
	mDragAnchorIndex(-1),
	mDragCurrentIndex(-1),
	mFlagTexId(0),
	mFlagTexW(0),
	mFlagTexH(0),
	mSearchField(NULL),
	mHoverClearButton(false),
	mTrackClearButton(false),
	mScrollOffset(0),
	mContentDragStartY(-1),		// declared later in the header (after mCachedStatusLines) -
	mContentDragStartOffset(0),	// listed here anyway so all the "simple scalar" inits stay together
	mHoverCard(-1),
	mTrackCard(-1)
{
	// Seeds the "Popular Airlines" weighted shuffle (see GetPopularAirlinesCodes()) once
	// per WED run, not once per pane/document - a static guard rather than reseeding
	// every time a new document window is opened, which would just make same-second opens
	// reproduce the same draw.
	static bool sRandSeeded = false;
	if (!sRandSeeded)
	{
		srand((unsigned int) time(NULL));
		sRandSeeded = true;
	}

	mArchive->AddListener(this);

	// Owned as a child pane (auto-deleted by ~GUI_Pane) rather than in our
	// own member list of drawables - GUI_Pane's own child dispatch gives it
	// first refusal on mouse/key events for free, so this is the only widget
	// in this whole custom-drawn pane that doesn't need manual hit-testing.
	mSearchField = new GUI_TextField(0, mHostTabs);
	mSearchField->SetParent(this);
	mSearchField->Show();
	mSearchField->SetKeyMsg(GUI_TEXT_FIELD_TEXT_CHANGED, 0);
	mSearchField->AddListener(this);

	// Airline names/ICAO codes are plain ASCII - lock the field down to
	// exactly that (deny-by-default, then re-allow only the specific chars
	// this lookup actually needs) rather than trying to enumerate every
	// character to reject. char 0 doubles as OGLE's "extended" bit (see
	// GUI_TextField::HandleKeyPress), so disallowing it blocks every
	// codepoint above 255 in one shot - Chinese/Japanese/Korean/emoji/etc
	// included, without needing to know their actual code points.
	for (int i = 0; i < 256; ++i)
	{
		bool keep =  (i >= 'a' && i <= 'z')
				  || (i >= 'A' && i <= 'Z')
				  || (i >= '0' && i <= '9')
				  || i == ' '
				  || i == GUI_KEY_BACK || i == GUI_KEY_DELETE
				  || i == GUI_KEY_LEFT || i == GUI_KEY_RIGHT || i == GUI_KEY_UP || i == GUI_KEY_DOWN;
		mSearchField->SetKeyAllowed((char) i, keep);
	}

	RebuildSelection();

	// Card placeholder data - see PickPlaceholderObjectVpaths()'s own comment.
	mPreviewObjVpaths = PickPlaceholderObjectVpaths(WED_GetLibraryMgr(mResolver), kCardCount);
	LOG_MSG("I/LiveryPane found %d placeholder object(s) for the preview cards\n", (int) mPreviewObjVpaths.size());
	LOG_FLUSH();
}

WED_LiveryPane::~WED_LiveryPane()
{
	if (mFlagTexId != 0)
		glDeleteTextures(1, &mFlagTexId);
	// mThumbCache discards its own textures in its own destructor. Raw (unmasked)
	// flag icon textures are this pane's own map, so free them here the same way.
	for (map<string, WED_LiveryThumbnail>::iterator i = mRawFlagTex.begin(); i != mRawFlagTex.end(); ++i)
		if (i->second.tex != 0)
			glDeleteTextures(1, &i->second.tex);
}

// Called by GUI_TabPane when the user switches to a different property tab (see
// GUI_TabPane.cpp) - discards every cached card thumbnail/flag texture so this tab
// isn't paying GPU memory for cards nobody can see. Cards regenerate lazily the next
// time this tab (and thus Draw()) becomes visible again.
void	WED_LiveryPane::Hide(void)
{
	// A size-slider drag opens its command in MouseDown and closes it in MouseUp,
	// so the command is live across everything in between - and Hide() IS
	// reachable in between, via the tab switch that SetTab broadcasts. Leaving it
	// open would strand the archive: the NEXT StartCommand anywhere in WED trips
	// the undo manager's "a command is already open" assert, with no clue that a
	// hidden tab caused it. Abort rather than commit - a drag the user never
	// finished should not land in the undo stack.
	AbortSizeDrag();

	GUI_Pane::Hide();
	mThumbCache.DiscardAll();
	for (map<string, WED_LiveryThumbnail>::iterator i = mRawFlagTex.begin(); i != mRawFlagTex.end(); ++i)
		if (i->second.tex != 0)
			glDeleteTextures(1, &i->second.tex);
	mRawFlagTex.clear();
}

// Always exactly half the tab's horizontal width, flush to the pane's left
// border and flush to the strip's own top edge (no inset either side) -
// height follows from the fixed master aspect. AirportInfoHeight() sizes the
// dark tray to match this exactly (same width-derived formula), so the two
// can never drift apart. Pure geometry, no GL calls, so both the text-x
// computation (early in Draw()) and the actual draw (issued last, so it
// paints over whatever it now overlaps) always agree.
void	WED_LiveryPane::FlagBannerRect(int bounds[4], float strip_top, float strip_bot,
						float & out_x, float & out_y, float & out_w, float & out_h) const
{
	out_w = (bounds[2] - bounds[0]) * 0.5f;
	out_h = out_w * ((float) mFlagTexH / (float) mFlagTexW);
	out_x = (float) bounds[0];
	out_y = strip_top - out_h;
}

// Loads the country's flag source raster, projects it through the fixed
// pole/mask/ink UV mesh (WED_FlagProjector - see that file's header for the
// approved-defaults contract this ports), and uploads the result as a GL
// texture. No-ops if the requested country is already the one currently
// uploaded - this is a ~2048x768 warp+composite, not something to redo on
// every Draw() call. Pass "" to release the texture (nothing to show).
void	WED_LiveryPane::EnsureFlagTexture(const string & ioc_country_code)
{
	if (ioc_country_code == mFlagTexCountry) return;

	if (mFlagTexId != 0)
	{
		glDeleteTextures(1, &mFlagTexId);
		mFlagTexId = 0;
		mFlagTexW = mFlagTexH = 0;
	}
	mFlagTexCountry = ioc_country_code;
	if (ioc_country_code.empty()) return;

	string path = WED_FlagSourcePathForCountry(ioc_country_code);
	LOG_MSG("I/EnsureFlagTexture: country=%s path=%s\n", ioc_country_code.c_str(), path.c_str());
		LOG_FLUSH();
	vector<uint32_t> source_argb;
	int src_w = 0, src_h = 0;
	if (!WED_LoadPngTopDownARGB(path, source_argb, src_w, src_h))
	{
		LOG_MSG("I/EnsureFlagTexture: source PNG load FAILED\n");
		LOG_FLUSH();
		return;
	}
	LOG_MSG("I/EnsureFlagTexture: source PNG loaded %dx%d\n", src_w, src_h);
		LOG_FLUSH();

	vector<uint32_t> composited;
	if (!WED_ProjectFlag(source_argb, src_w, src_h, composited))
	{
		LOG_MSG("I/EnsureFlagTexture: WED_ProjectFlag FAILED\n");
		LOG_FLUSH();
		return;
	}
	LOG_MSG("I/EnsureFlagTexture: projected OK, %d pixels\n", (int) composited.size());
		LOG_FLUSH();

	// composited is top-down 0xAARRGGBB; glTexImage2D wants a byte order we
	// can address directly, so hand it the buffer as GL_BGRA/GL_UNSIGNED_BYTE
	// (matches this machine's 0xAARRGGBB-in-a-uint32 layout on little-endian,
	// same "OpenGL convention" BitmapUtils.h already documents) and flip the
	// V texture-coordinate at draw time instead of flipping row order here.
	glGenTextures(1, &mFlagTexId);
	glBindTexture(GL_TEXTURE_2D, mFlagTexId);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, WED_FlagAssets::Width(), WED_FlagAssets::Height(), 0,
					GL_BGRA, GL_UNSIGNED_BYTE, &composited[0]);
	mFlagTexW = WED_FlagAssets::Width();
	mFlagTexH = WED_FlagAssets::Height();
}

// Small, unmasked (plain rectangular, natural aspect ratio) flag icon for a card
// header - deliberately NOT the WED_ProjectFlag() pole/cloth-mask pipeline above
// (that's for the one big banner; this is a per-card icon). Cached for the pane's
// lifetime (freed in Hide()/~WED_LiveryPane()) rather than evicted per-scroll like the
// card thumbnails - there are far fewer distinct countries than distinct cards.
// Returns an entry with tex == 0 if the source PNG couldn't be loaded - never draw it.
const WED_LiveryThumbnail *	WED_LiveryPane::EnsureRawFlagTexture(const string & ioc_country_code)
{
	map<string, WED_LiveryThumbnail>::iterator it = mRawFlagTex.find(ioc_country_code);
	if (it != mRawFlagTex.end())
		return &it->second;

	WED_LiveryThumbnail entry;

	string path = WED_FlagSourcePathForCountry(ioc_country_code);
	vector<uint32_t> source_argb;
	int src_w = 0, src_h = 0;
	if (WED_LoadPngTopDownARGB(path, source_argb, src_w, src_h))
	{
		glGenTextures(1, &entry.tex);
		glBindTexture(GL_TEXTURE_2D, entry.tex);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
		glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, src_w, src_h, 0, GL_BGRA, GL_UNSIGNED_BYTE, &source_argb[0]);
		entry.w = src_w;
		entry.h = src_h;
	}

	return &mRawFlagTex.emplace(ioc_country_code, entry).first->second;
}

void	WED_LiveryPane::ReceiveMessage(
							GUI_Broadcaster *		inSrc,
							intptr_t				inMsg,
							intptr_t				inParam)
{
	if (inMsg == msg_ArchiveChanged || inMsg == msg_ArchiveChangedEphemerally)
	{
		RebuildSelection();
		Refresh();
	}
	else if (inMsg == GUI_TEXT_FIELD_TEXT_CHANGED && inSrc == mSearchField)
	{
		mSearchField->GetDescriptor(mSearchQuery);
		Refresh();
	}
}

void	WED_LiveryPane::RebuildSelection(void)
{
	vector<WED_RampPosition *> old_selection = mSelectedRamps;

	mSelectedRamps.clear();
	ISelection * sel = WED_GetSelect(mResolver);
	if (sel) sel->IterateSelectionOr(CollectRamps, &mSelectedRamps);

	// A genuinely different ramp selection (different ramp, or a different airport
	// entirely) means the checklist content just changed out from under whatever
	// scroll position was left over from before - snap back to the top rather than
	// risk leaving the user scrolled past a short "Recommended"/"Manual" section (or
	// the whole list) for the new selection. An unrelated Draw() call caused by
	// something else entirely (e.g. just moving the mouse) leaves this alone.
	if (mSelectedRamps != old_selection)
		mScrollOffset = 0;

	// No more SetPaneEnabled() lock - the tab stays clickable even with
	// nothing selected (the greyed-out mask + warning text in Draw() carries
	// that state instead). Only auto-navigate the user OFF this tab the
	// first time it goes empty while they're actually looking at it; once
	// they've manually clicked back in with nothing selected, leave them be
	// until selection is non-empty again.
	if (mSelectedRamps.empty())
	{
		if (mHostTabs && !mAutoSwitchedAwayOnEmpty && mHostTabs->GetTab() == kLiveryTabIndex)
		{
			mHostTabs->SetTab(kSelectionTabIndex);
			mAutoSwitchedAwayOnEmpty = true;
		}
		mLastAutoSwitchedInRamps.clear();		// selection's gone - a later re-selection counts as "new" again
	}
	else
	{
		mAutoSwitchedAwayOnEmpty = false;

		// Opt-in (WED Preferences > "When Selecting Ramp Start" > "Prompt Up
		// Static Liveries Tab", off by default - see WED_Application.cpp).
		// Only fires for a selection that's ENTIRELY ramp starts (no other
		// entity types mixed in) - GetSelectionCount() covers everything
		// selected, mSelectedRamps only what CollectRamps recognized, so
		// they only match when every selected item was a ramp. Fires once
		// per DIFFERENT ramp selection (tracked via mLastAutoSwitchedInRamps,
		// compared by pointer identity) - the user can freely navigate away
		// afterward without getting yanked back until they select some other
		// (or newly re-selected) set of ramps.
		bool pure_ramp_selection = sel && sel->GetSelectionCount() == (int) mSelectedRamps.size();
		if (!pure_ramp_selection)
			mLastAutoSwitchedInRamps.clear();
		else if (gPromptLiveriesOnRampSelect && mHostTabs && mSelectedRamps != mLastAutoSwitchedInRamps)
		{
			mHostTabs->SetTab(kLiveryTabIndex);
			mLastAutoSwitchedInRamps = mSelectedRamps;
		}
	}
}

// ---------------------------------------------------------------------------------------------
// layout
// ---------------------------------------------------------------------------------------------

float	WED_LiveryPane::AirportInfoHeight(int bounds[4]) const
{
	// The dark "tray" wraps whichever is taller: the flag banner (flush top,
	// tiny bottom pad - the banner's own width, half the tab's horizontal
	// space, see FlagBannerRect(), drives its height via the fixed master
	// aspect) or the word-wrapped text block (mCachedInfoLines/
	// mCachedStatusLines - recomputed just before this is called each Draw(),
	// see the top of Draw() for why that ordering matters). Falls back to a
	// single compact line when there's neither a flag nor any text yet
	// (nothing selected).
	float line_h = GUI_GetLineHeight(font_UI_Basic);
	int total_lines = (int) mCachedInfoLines.size() + (int) mCachedStatusLines.size();
	float text_h = total_lines > 0 ? total_lines * line_h + 8 : line_h + 8;

	if (mFlagTexId != 0)
	{
		float banner_w = (bounds[2] - bounds[0]) * 0.5f;
		float banner_h = banner_w * ((float) mFlagTexH / (float) mFlagTexW);
		return (std::max)(banner_h + 4, text_h);
	}
	return text_h;
}

float	WED_LiveryPane::HeaderHeight(void) const
{
	return GUI_GetLineHeight(font_UI_Basic) + 8;
}

float	WED_LiveryPane::FilterRowHeight(void) const
{
	return GUI_GetLineHeight(font_UI_Basic) + 8;
}

float	WED_LiveryPane::SliderHeight(void) const
{
	// title row + A-F label row + track/ball row, plus padding
	return GUI_GetLineHeight(font_UI_Basic) * 3 + 16;
}

float	WED_LiveryPane::ListToolbarHeight(void) const
{
	return GUI_GetLineHeight(font_UI_Basic) + 8;
}

float	WED_LiveryPane::GapHeight(void) const
{
	return 10;
}

void	WED_LiveryPane::AirportInfoYRange(int bounds[4], float & top, float & bot) const
{
	top = (float) bounds[3];
	bot = top - AirportInfoHeight(bounds);
}

void	WED_LiveryPane::HeaderYRange(int bounds[4], float & top, float & bot) const
{
	float atop, abot;
	AirportInfoYRange(bounds, atop, abot);
	top = abot - GapHeight();
	bot = top - HeaderHeight();
}

void	WED_LiveryPane::FilterYRange(int bounds[4], float & top, float & bot) const
{
	float htop, hbot;
	HeaderYRange(bounds, htop, hbot);
	top = hbot - GapHeight();
	bot = top - FilterRowHeight();
}

void	WED_LiveryPane::SliderYRange(int bounds[4], float & top, float & bot) const
{
	float ftop, fbot;
	FilterYRange(bounds, ftop, fbot);
	top = fbot - GapHeight();
	bot = top - SliderHeight();
}

void	WED_LiveryPane::ListToolbarYRange(int bounds[4], float & top, float & bot) const
{
	float stop, sbot;
	SliderYRange(bounds, stop, sbot);
	top = sbot - GapHeight();
	bot = top - ListToolbarHeight();
}

// Fixed aspect (16:9) image on top, one line of header text (ICAO - name - country +
// flag icon) below it - see the .h comment on why this section's content is placeholder.

// One column's width: the pane minus a gap on both outer edges and between columns.
float	WED_LiveryPane::CardWidth(int bounds[4]) const
{
	float avail = (float) (bounds[2] - bounds[0]) - kCardGap * (float) (kCardCols + 1);
	return (std::max)(1.0f, avail / (float) kCardCols);
}

float	WED_LiveryPane::CardHeight(int bounds[4]) const
{
	// MUST derive from CardWidth(), same as Draw()'s image quad does - the image is
	// as wide as the card and its height follows from the fixed aspect. (Computing
	// this from a nominal/guessed width instead is exactly what made the whole block
	// render as one oversized black slab the first time this was tried: the image
	// then draws taller than the slot this function claims it needs, overflowing
	// into the next card and the checklist below.)
	float image_h = CardWidth(bounds) / kCardImageAspect;
	return image_h + GUI_GetLineHeight(font_UI_Basic) + 8;
}

// Total height the card block occupies INSIDE the scrolled content - it is not a
// viewport of its own, just the first rows' worth of that content. Rows of cards,
// not cards: kCardCols of them sit side by side per row.
float	WED_LiveryPane::CardsBlockHeight(int bounds[4]) const
{
	int rows = (kCardCount + kCardCols - 1) / kCardCols;		// ceil
	return rows * (CardHeight(bounds) + kCardGap) + kCardGap;
}

float	WED_LiveryPane::ContentTop(int bounds[4]) const
{
	float ttop, tbot;
	ListToolbarYRange(bounds, ttop, tbot);
	// Deliberately a small fixed gap, not the usual GapHeight() - the toolbar row and
	// the content below it (cards, then the checklist) are meant to read as one
	// bordered "workspace" (see Draw()'s border box), not separate sections.
	return tbot - 4;
}

// Cards occupy the top of the scrolled content, so they shift with the SAME
// mScrollOffset (in pixels) the checklist below them uses - one scroll position for
// the whole tab. Index runs left-to-right, then down: 0 1 / 2 3 / ...
void	WED_LiveryPane::CardRectForIndex(int bounds[4], int index, float r_out[4]) const
{
	float cw  = CardWidth(bounds);
	float ch  = CardHeight(bounds);
	int   col = index % kCardCols;
	int   row = index / kCardCols;

	r_out[0] = (float) bounds[0] + kCardGap + col * (cw + kCardGap);
	r_out[2] = r_out[0] + cw;
	r_out[3] = ContentTop(bounds) + mScrollOffset - kCardGap - row * (ch + kCardGap);
	r_out[1] = r_out[3] - ch;
}

// Hit-test against the card rects themselves, so the gaps between/around them are
// correctly "not a card" - the grid is a set of separate objects, not a solid block.
int		WED_LiveryPane::CardForXY(int bounds[4], int x, int y) const
{
	if (y > ContentTop(bounds)) return -1;			// above the content area entirely

	for (int i = 0; i < (int) mPreviewObjVpaths.size(); ++i)
	{
		float r[4];
		CardRectForIndex(bounds, i, r);
		if (x >= r[0] && x <= r[2] && y >= r[1] && y <= r[3])
			return i;
	}
	return -1;
}

// Right-aligned pair, flush to the tab's right border: [Show Recommendation][Sort].
void	WED_LiveryPane::SortButtonRect(int bounds[4], float b_out[4]) const
{
	float top, bot;
	ListToolbarYRange(bounds, top, bot);
	const float pad = 4;
	const float w = 74;

	b_out[2] = (float) bounds[2] - pad;
	b_out[0] = b_out[2] - w;
	b_out[1] = bot + 2;
	b_out[3] = top - 2;
}

void	WED_LiveryPane::RecommendButtonRect(int bounds[4], float b_out[4]) const
{
	float sort_rect[4];
	SortButtonRect(bounds, sort_rect);
	const float w = 168;
	const float gap = 6;

	b_out[2] = sort_rect[0] - gap;
	b_out[0] = b_out[2] - w;
	b_out[1] = sort_rect[1];
	b_out[3] = sort_rect[3];
}

// Fills whatever room the toolbar row has left of the Recommend button, all
// the way to the workspace box's own left edge - grows/shrinks with the
// pane instead of having its own fixed width.
void	WED_LiveryPane::SearchFieldRect(int bounds[4], float b_out[4]) const
{
	float rec_rect[4];
	RecommendButtonRect(bounds, rec_rect);
	const float pad = 4;
	const float gap = 6;

	b_out[0] = (float) bounds[0] + pad;
	b_out[2] = rec_rect[0] - gap;
	b_out[1] = rec_rect[1];
	b_out[3] = rec_rect[3];
}

// A square button flush to SearchFieldRect()'s own right edge, sized to the
// field's height. Only used while mSearchQuery is non-empty - see Draw().
void	WED_LiveryPane::SearchClearButtonRect(int bounds[4], float b_out[4]) const
{
	float r[4];
	SearchFieldRect(bounds, r);
	float w = r[3] - r[1];

	b_out[2] = r[2];
	b_out[0] = r[2] - w;
	b_out[1] = r[1];
	b_out[3] = r[3];
}

// ---------------------------------------------------------------------------------------------
// ramp operation filter chips
// ---------------------------------------------------------------------------------------------

int		WED_LiveryPane::FilterChipForXY(int bounds[4], int x, int y) const
{
	float top, bot;
	FilterYRange(bounds, top, bot);
	if (y > top || y < bot) return -1;

	float chip_w = (bounds[2] - bounds[0]) / 5.0f;
	int idx = (int) ((x - bounds[0]) / chip_w);
	if (idx < 0) idx = 0;
	if (idx > 4) idx = 4;
	return idx;
}

void	WED_LiveryPane::SetRampOpFilter(int wed_ramp_op_enum)
{
	if (mSelectedRamps.empty()) return;

	mArchive->StartCommand("Set Ramp Operation Type");
	for (size_t i = 0; i < mSelectedRamps.size(); ++i)
		mSelectedRamps[i]->SetRampOperationType(wed_ramp_op_enum);
	mArchive->CommitCommand();

	Refresh();
}

// ---------------------------------------------------------------------------------------------
// size range slider
// ---------------------------------------------------------------------------------------------

int		WED_LiveryPane::SliderHandleForXY(int bounds[4], int x, int y) const
{
	if (mSelectedRamps.empty()) return -1;

	float line_h    = GUI_GetLineHeight(font_UI_Basic);
	float handle_r  = line_h * 0.5f;
	float slider_top, slider_bot;
	SliderYRange(bounds, slider_top, slider_bot);
	float track_y   = slider_bot + line_h * 0.5f;
	float track_x0  = bounds[0] + 4 + handle_r;
	float track_x1  = bounds[2] - 4 - handle_r;

	int minIdx = WidthEnumToIndex(mSelectedRamps[0]->GetWidthMin());
	int maxIdx = WidthEnumToIndex(mSelectedRamps[0]->GetWidth());
	float min_x = track_x0 + (track_x1 - track_x0) * minIdx / 5.0f;
	float max_x = track_x0 + (track_x1 - track_x0) * maxIdx / 5.0f;

	if (y < track_y - handle_r*1.5f || y > track_y + handle_r*1.5f) return -1;

	if (minIdx == maxIdx)
	{
		// single overlapping ball - direction (min vs max) isn't known yet
		if (fabs((double)(x - min_x)) <= handle_r*1.5) return 2;
		return -1;
	}

	bool near_min = fabs((double)(x - min_x)) <= handle_r*1.5;
	bool near_max = fabs((double)(x - max_x)) <= handle_r*1.5;

	if (near_min && near_max)
		return (fabs((double)(x-min_x)) <= fabs((double)(x-max_x))) ? 0 : 1;
	if (near_min) return 0;
	if (near_max) return 1;
	return -1;
}

float	WED_LiveryPane::SliderContinuousIndexForX(int bounds[4], int x) const
{
	float line_h    = GUI_GetLineHeight(font_UI_Basic);
	float handle_r  = line_h * 0.5f;
	float track_x0  = bounds[0] + 4 + handle_r;
	float track_x1  = bounds[2] - 4 - handle_r;

	float frac = (track_x1 > track_x0) ? (x - track_x0) / (track_x1 - track_x0) : 0.0f;
	return frac * 5.0f;		// unsnapped - caller applies detent hysteresis
}

void	WED_LiveryPane::ApplyDragRange(void)
{
	if (mSelectedRamps.empty()) return;

	// Whichever of anchor/current is smaller is min, larger is max - recomputed
	// fresh every call, so dragging the "moving" ball straight past the anchor
	// and out the other side just flips which one is which, automatically and
	// without a special case.
	int lo = (mDragAnchorIndex < mDragCurrentIndex) ? mDragAnchorIndex : mDragCurrentIndex;
	int hi = (mDragAnchorIndex > mDragCurrentIndex) ? mDragAnchorIndex : mDragCurrentIndex;

	for (size_t i = 0; i < mSelectedRamps.size(); ++i)
	{
		mSelectedRamps[i]->SetWidthMin(IndexToWidthEnum(lo));
		mSelectedRamps[i]->SetWidth(IndexToWidthEnum(hi));
	}
}

// ---------------------------------------------------------------------------------------------
// airline checklist
// ---------------------------------------------------------------------------------------------

int		WED_LiveryPane::RowForY(int bounds[4], int y) const
{
	float line_h = GUI_GetLineHeight(font_UI_Basic);
	float row_h  = line_h + 4;

	// `top` is the content area's fixed on-screen boundary - it does NOT move as
	// mScrollOffset changes (mScrollOffset shifts which content sits there, not
	// where "there" is), so the above-the-content bounds check stays unshifted.
	// The row math below adds mScrollOffset back in - see Draw()'s matching
	// row_top formula, which this must stay in sync with - and subtracts the card
	// block, since the checklist starts below the cards inside that same content.
	float top = ContentTop(bounds);
	if (y > top) return -1;

	int row = (int) ((top + mScrollOffset - CardsBlockHeight(bounds) - y) / row_h);
	return row;		// caller clamps against the visible-entry count (negative = inside the cards)
}

// Only vertical (axis 0) scrolling means anything for this single-column list.
// A few rows per notch, same "small multiple of one line" feel as most native
// scrollable lists. The upper bound on mScrollOffset is enforced in Draw() (it
// needs the current row count/bounds, which this function doesn't have) - a
// value briefly larger than the real max is harmless, never used to position
// anything until Draw() re-clamps it first.
int		WED_LiveryPane::ScrollWheel(int x, int y, int dist, int axis)
{
	if (axis != 0) return 0;

	float line_h = GUI_GetLineHeight(font_UI_Basic);
	float row_h  = line_h + 4;

	mScrollOffset -= dist * row_h * 3;
	if (mScrollOffset < 0) mScrollOffset = 0;

	Refresh();
	return 1;
}

// Rolls back an in-flight size-slider drag, if there is one. Safe to call when
// there isn't - that is the point, so callers don't have to know.
// How many of the selected ramps carry `icao`. The caller turns this into the
// checkbox's three states: none of them, all of them, or somewhere in between.
int		WED_LiveryPane::CountRampsWithCode(const string & icao) const
{
	int n = 0;
	for (size_t i = 0; i < mSelectedRamps.size(); ++i)
		if (ParseCodes(mSelectedRamps[i]->GetAirlines()).count(icao))
			++n;
	return n;
}

void	WED_LiveryPane::AbortSizeDrag(void)
{
	if (mDragHandle < 0) return;

	mArchive->AbortCommand();
	mDragHandle = -1;
	mDragAnchorIndex = -1;
	mDragCurrentIndex = -1;
}

void	WED_LiveryPane::ToggleCode(const string & icao)
{
	if (mSelectedRamps.empty()) return;

	// The checkbox is tri-state across a multi-selection, and the transition rule
	// is what keeps it safe: only a box that is solid for EVERY selected ramp
	// clears. Mixed and empty both fill. So a click can never remove a code the
	// user was not shown as set - which is what the old "read ramp 0, write all"
	// version did, silently deleting airlines from ramps whose box was drawn
	// unchecked the whole time.
	//
	// Consequence, and it is the intended one: clicking a mixed box UNIFIES the
	// selection. That can rewrite hundreds of ramps at once, which is why it all
	// happens inside a single command - one Ctrl+Z puts every one of them back.
	const int n_with = CountRampsWithCode(icao);
	const bool clear_all = (n_with == (int) mSelectedRamps.size());

	mArchive->StartCommand("Set Ramp Start Airlines");
	for (size_t i = 0; i < mSelectedRamps.size(); ++i)
	{
		set<string> codes = ParseCodes(mSelectedRamps[i]->GetAirlines());
		if (clear_all)	codes.erase(icao);
		else			codes.insert(icao);
		mSelectedRamps[i]->SetAirlines(WED_RampPosition::CorrectAirlinesString(CodesToString(codes)));
	}
	mArchive->CommitCommand();

	Refresh();
}

// ---------------------------------------------------------------------------------------------
// input
// ---------------------------------------------------------------------------------------------

int		WED_LiveryPane::MouseMove(int x, int y)
{
	int b[4];
	GetBounds(b);

	bool changed = false;

	int chip = mSelectedRamps.empty() ? -1 : FilterChipForXY(b, x, y);
	if (chip != mHoverFilterChip)		{ mHoverFilterChip = chip;		changed = true; }

	int handle = mSelectedRamps.empty() ? -1 : SliderHandleForXY(b, x, y);
	if (handle != mHoverSliderHandle)	{ mHoverSliderHandle = handle;	changed = true; }

	bool over_sort = false, over_recommend = false;
	if (!mSelectedRamps.empty())
	{
		float r[4];
		SortButtonRect(b, r);
		over_sort = (x >= r[0] && x <= r[2] && y >= r[1] && y <= r[3]);
		RecommendButtonRect(b, r);
		over_recommend = (x >= r[0] && x <= r[2] && y >= r[1] && y <= r[3])
			&& AirportIsCommercial(mAirportDb, mCurrentAirportIcao);	// hovering a disabled button doesn't count
	}
	if (over_sort != mHoverSortButton)			{ mHoverSortButton = over_sort;			changed = true; }
	if (over_recommend != mHoverRecommendButton)	{ mHoverRecommendButton = over_recommend;	changed = true; }

	bool over_clear = false;
	if (!mSelectedRamps.empty() && !mSearchQuery.empty())
	{
		float r[4];
		SearchClearButtonRect(b, r);
		over_clear = (x >= r[0] && x <= r[2] && y >= r[1] && y <= r[3]);
	}
	if (over_clear != mHoverClearButton)			{ mHoverClearButton = over_clear;			changed = true; }

	int hover_card = mSelectedRamps.empty() ? -1 : CardForXY(b, x, y);
	if (hover_card != mHoverCard)		{ mHoverCard = hover_card;		changed = true; }

	int row = -1;
	if (!mSelectedRamps.empty() && !over_sort && !over_recommend && !over_clear)
	{
		vector<WED_LiveryDisplayRow> rows = BuildCurrentDisplayRows(mSelectedRamps[0], mSortDescending,
											gShowLiveryRecommendation != 0, mAirportDb, mCurrentAirportIcao, mSearchQuery, mAirlineDirectory, mPopularAirlinesShuffleCache);
		int r = RowForY(b, y);
		row = (r >= 0 && r < (int) rows.size() && rows[r].kind == wed_Row_Airline) ? r : -1;
	}
	if (row != mHoverRow)				{ mHoverRow = row;				changed = true; }

	if (changed) Refresh();
	return 0;
}

int		WED_LiveryPane::MouseDown(int x, int y, int button)
{
	if (mSelectedRamps.empty())
		return 1;			// masked - swallow the click, do nothing

	int b[4];
	GetBounds(b);

	// Reaching this function at all means the click did NOT land on the
	// search field while it was visible - GUI_Pane's child dispatch already
	// gives a visible child first refusal on clicks (see GUI_Pane::
	// InternalMouseDown), so a click that WAS meant for an actively-focused,
	// on-screen field never gets here. Anything that does reach us is
	// therefore "somewhere else" by definition - drop the field's focus so
	// it collapses back to the plain placeholder as soon as this frame's
	// Draw() runs (see the empty-and-unfocused check there). Guarded by
	// IsFocused() because GUI_Commander::LoseFocus() doesn't check that
	// itself - calling it on a commander that ISN'T actually focused would
	// incorrectly go steal focus away from whatever else legitimately has it.
	if (mSearchField->IsFocused())
		mSearchField->LoseFocus(1);

	if (!mSearchQuery.empty())
	{
		float clear_r[4];
		SearchClearButtonRect(b, clear_r);
		if (x >= clear_r[0] && x <= clear_r[2] && y >= clear_r[1] && y <= clear_r[3])
		{
			mTrackClearButton = true;
			return 1;
		}
	}

	int chip = FilterChipForXY(b, x, y);
	if (chip >= 0)
	{
		mTrackFilterChip = chip;
		return 1;
	}

	int handle = SliderHandleForXY(b, x, y);
	if (handle >= 0)
	{
		int minIdx = WidthEnumToIndex(mSelectedRamps[0]->GetWidthMin());
		int maxIdx = WidthEnumToIndex(mSelectedRamps[0]->GetWidth());

		// Anchor = the ball NOT being grabbed (stays fixed all gesture); current =
		// the grabbed ball's own position. When they overlap it doesn't matter
		// which is "the" anchor since both equal the same value anyway.
		mDragAnchorIndex  = (handle == 0) ? maxIdx : minIdx;
		mDragCurrentIndex = (handle == 0) ? minIdx : maxIdx;
		mDragHandle = handle;

		mArchive->StartCommand("Set Ramp Start Size");
		return 1;
	}

	float sort_r[4];
	SortButtonRect(b, sort_r);
	if (x >= sort_r[0] && x <= sort_r[2] && y >= sort_r[1] && y <= sort_r[3])
	{
		mTrackSortButton = true;
		return 1;
	}

	float rec_r[4];
	RecommendButtonRect(b, rec_r);
	if (x >= rec_r[0] && x <= rec_r[2] && y >= rec_r[1] && y <= rec_r[3]
		&& AirportIsCommercial(mAirportDb, mCurrentAirportIcao))
	{
		mTrackRecommendButton = true;
		return 1;
	}

	// Only reachable when the search field is currently HIDDEN (placeholder
	// showing) - GUI_Pane's own child dispatch gives the real field first
	// refusal on this click whenever it's visible, so this code never fires
	// then. Reveal it and focus it; the click that revealed it doesn't also
	// place a caret - the user's very next click/keystroke does that.
	float search_r[4];
	SearchFieldRect(b, search_r);
	if (x >= search_r[0] && x <= search_r[2] && y >= search_r[1] && y <= search_r[3])
	{
		mSearchField->Show();
		mSearchField->TakeFocus();
		Refresh();
		return 1;
	}

	// A press anywhere on the card block arms BOTH possible outcomes: a card toggle
	// (if the cursor barely moves before release) and a drag-scroll (if it does).
	// MouseUp decides which actually happened - see its slop check. The checklist
	// below deliberately doesn't get drag-scroll: its rows are click targets, so
	// only the wheel (or a drag started up here) scrolls it.
	if (!mPreviewObjVpaths.empty())
	{
		float ctop = ContentTop(b);
		float cards_bot = ctop + mScrollOffset - CardsBlockHeight(b);
		if (y <= ctop && y >= cards_bot)
		{
			mContentDragStartY = y;
			mContentDragStartOffset = mScrollOffset;
			mTrackCard = CardForXY(b, x, y);		// -1 when the press landed in a gap
			Refresh();
			return 1;
		}
	}

	int row = RowForY(b, y);
	vector<WED_LiveryDisplayRow> rows = BuildCurrentDisplayRows(mSelectedRamps[0], mSortDescending,
										gShowLiveryRecommendation != 0, mAirportDb, mCurrentAirportIcao, mSearchQuery, mAirlineDirectory, mPopularAirlinesShuffleCache);
	mTrackRow = (row >= 0 && row < (int) rows.size() && rows[row].kind == wed_Row_Airline) ? row : -1;
	return 1;
}

void	WED_LiveryPane::MouseDrag(int x, int y, int button)
{
	// Drag-scroll: the content follows the cursor ("grab and pull"), so dragging
	// downward reveals earlier content. Y increases UPWARD in this pane's coordinate
	// space, so a downward drag makes (y - start) negative, which decreases the
	// offset - exactly the direction we want. Derived from the gesture's TOTAL
	// movement, never accumulated per-move, so it can't drift. The upper clamp is
	// Draw()'s job (it's the one that knows the current content height).
	if (mContentDragStartY >= 0)
	{
		mScrollOffset = mContentDragStartOffset + (float) (y - mContentDragStartY);
		if (mScrollOffset < 0) mScrollOffset = 0;
		Refresh();
		return;
	}

	if (mDragHandle < 0) return;

	int b[4];
	GetBounds(b);
	float idx_f = SliderContinuousIndexForX(b, x);

	const float kDetent = 0.1f;		// 10% of one grid interval, either side

	// Detent-snap the grabbed ball's own position - completely independent of
	// where the anchor ball sits, so dragging straight through it and beyond
	// requires no special handling at all.
	int cur = mDragCurrentIndex;
	int new_cur = cur;
	if (cur < 5 && fabs((double)(idx_f - (cur + 1))) <= kDetent)
		new_cur = cur + 1;
	else if (cur > 0 && fabs((double)(idx_f - (cur - 1))) <= kDetent)
		new_cur = cur - 1;
	else if (fabs((double)(idx_f - cur)) > 1.5)
		new_cur = (int) (idx_f + 0.5f);		// fast/discontinuous mouse motion - jump straight there

	if (new_cur == cur) return;

	mDragCurrentIndex = new_cur;

	// Which side is "current" acting as now - drives only the hover/highlight
	// ring; ApplyDragRange() re-derives real min/max from anchor/current itself.
	if (mDragCurrentIndex == mDragAnchorIndex)		mDragHandle = 2;
	else if (mDragCurrentIndex < mDragAnchorIndex)	mDragHandle = 0;
	else											mDragHandle = 1;

	ApplyDragRange();
	Refresh();
}

void	WED_LiveryPane::MouseUp(int x, int y, int button)
{
	int b[4];
	GetBounds(b);

	if (mContentDragStartY >= 0)
	{
		// Toggle only if this was a click rather than a drag-scroll: the cursor has
		// to have stayed within a few pixels of where it went down AND still be on
		// the same card. Anything looser and every scroll drag would flip whichever
		// card it started on.
		const int kClickSlop = 4;
		if (mTrackCard >= 0 &&
			abs(y - mContentDragStartY) <= kClickSlop &&
			CardForXY(b, x, y) == mTrackCard)
		{
			if (mSelectedCards.count(mTrackCard))	mSelectedCards.erase(mTrackCard);
			else									mSelectedCards.insert(mTrackCard);
		}
		mTrackCard = -1;
		mContentDragStartY = -1;	// nothing to commit - scrolling is view state, not document state
		Refresh();
		return;
	}

	if (mDragHandle >= 0)
	{
		mArchive->CommitCommand();
		mDragHandle = -1;
		mDragAnchorIndex = -1;
		mDragCurrentIndex = -1;
		Refresh();
		return;
	}

	if (mTrackFilterChip >= 0)
	{
		if (FilterChipForXY(b, x, y) == mTrackFilterChip)
			SetRampOpFilter(kFilterEnumTable[mTrackFilterChip]);
		mTrackFilterChip = -1;
		return;
	}

	if (mTrackSortButton)
	{
		float r[4];
		SortButtonRect(b, r);
		if (x >= r[0] && x <= r[2] && y >= r[1] && y <= r[3])
			mSortDescending = !mSortDescending;
		mTrackSortButton = false;
		Refresh();
		return;
	}

	if (mTrackRecommendButton)
	{
		float r[4];
		RecommendButtonRect(b, r);
		if (x >= r[0] && x <= r[2] && y >= r[1] && y <= r[3]
			&& AirportIsCommercial(mAirportDb, mCurrentAirportIcao))
		{
			gShowLiveryRecommendation = !gShowLiveryRecommendation;
		}
		mTrackRecommendButton = false;
		Refresh();
		return;
	}

	if (mTrackClearButton)
	{
		float r[4];
		SearchClearButtonRect(b, r);
		if (x >= r[0] && x <= r[2] && y >= r[1] && y <= r[3] && !mSearchQuery.empty())
		{
			// Same DoReplaceText(0,len,NULL,NULL) path a manual select-all-then-
			// delete would take - it flows through GUI_TextField::ReplaceText(),
			// which broadcasts GUI_TEXT_FIELD_TEXT_CHANGED for us, so
			// ReceiveMessage() re-syncs mSearchQuery to "" the normal way rather
			// than us poking it directly here.
			mSearchField->DoReplaceText(0, (int) mSearchQuery.size(), NULL, NULL);
			if (mSearchField->IsFocused())
				mSearchField->LoseFocus(1);		// "exit search mode" - collapses to the placeholder next Draw()
		}
		mTrackClearButton = false;
		Refresh();
		return;
	}

	if (mSelectedRamps.empty() || mTrackRow < 0)
	{
		mTrackRow = -1;
		return;
	}

	vector<WED_LiveryDisplayRow> rows = BuildCurrentDisplayRows(mSelectedRamps[0], mSortDescending,
										gShowLiveryRecommendation != 0, mAirportDb, mCurrentAirportIcao, mSearchQuery, mAirlineDirectory, mPopularAirlinesShuffleCache);
	if (RowForY(b, y) == mTrackRow && mTrackRow < (int) rows.size() && rows[mTrackRow].kind == wed_Row_Airline)
		ToggleCode(rows[mTrackRow].icao);

	mTrackRow = -1;
}

// ---------------------------------------------------------------------------------------------
// draw
// ---------------------------------------------------------------------------------------------

void	WED_LiveryPane::Draw(GUI_GraphState * state)
{
	int b[4];
	GetBounds(b);

	// ============================================================================================
	// IF YOU ARE HERE BECAUSE SOMETHING ISN'T DRAWING, read this before spending a debugging
	// session rediscovering what already cost one. In rough order of "how likely to bite you":
	//
	// 1) BACK-FACE CULLING SILENTLY EATS FILLED SHAPES, NOT JUST THE FLAG BANNER.
	//    GUI_GraphState::Init() sets glFrontFace(GL_CW) + GL_CULL_FACE/GL_BACK globally for all of
	//    WED. Every filled shape in THIS pane (quads, the slider ball triangle fans) is wound the
	//    "natural" bottom-left-first way, which is counter-clockwise in this pane's Y-up coordinate
	//    space - i.e. back-facing under WED's convention, so every one of them was being silently
	//    culled. No GL error, nothing in the log - text (GUI_FontDraw) and GL_LINE_LOOP/GL_LINES
	//    outlines still rendered fine, which is what made this so confusing: the filter chips'
	//    colored backgrounds and the slider's fill bar/ball fills were ALSO invisible this whole
	//    time, before this feature ever touched the file - it just took drawing something new and
	//    obvious (a whole flag) to notice. Rather than re-winding every quad/fan by hand, culling is
	//    just turned off for this pane's own drawing (glPushAttrib/glDisable below) and restored at
	//    the very end of Draw() (glPopAttrib) so nothing else in WED is affected. If you add a new
	//    filled shape anywhere in this file and it doesn't show up, this is NOT what's wrong (culling
	//    is already off for the whole function) - but if this glPushAttrib/glDisable pair or the
	//    matching glPopAttrib at the end ever gets deleted/moved by mistake, this is exactly what
	//    will silently break, and it will look identical to a texture/coordinate bug.
	//
	// 2) <windows.h> min/max MACROS BREAK std::min/std::max IN THIS FILE'S FAMILY.
	//    XDefs.h is force-included (/FI) into every WED .cpp and pulls in <windows.h> on IBM/Windows,
	//    whose function-like min/max macros swallow std::min(...)/std::max(...) into a compile error
	//    (C2589/C2059) that has nothing to do with your actual logic. WED_FlagProjector.cpp works
	//    around this by parenthesizing every call as (std::min)(...)/(std::max)(...), which defeats
	//    macro expansion. Do the same in any new code here rather than adding a project-wide
	//    NOMINMAX (out of scope for this feature, and no other file in the codebase uses std::min/max
	//    at all - they avoid it entirely, which is also a valid way to sidestep this).
	//
	// 3) GL_BGRA NEEDS "glew.h", NOT PLAIN <GL/gl.h>, ON WINDOWS.
	//    Uploading the flag texture via GL_BGRA/GL_UNSIGNED_BYTE (so the composited buffer's byte
	//    order can be handed to glTexImage2D with no manual channel swap) needs the GL_BGRA token,
	//    which plain <GL/gl.h> doesn't define on Windows (it's a GL 1.2+ token). This file includes
	//    "glew.h" instead on non-Mac platforms for exactly that reason - see the #include block
	//    right below GetBounds(b). Reverting to <GL/gl.h> "to simplify" will silently break the
	//    Windows build's texture upload.
	//
	// 4) THE FLAG'S TRAY SIZE DEPENDS ON THE TEXTURE, SO ORDER OF OPERATIONS MATTERS.
	//    AirportInfoHeight()/FlagBannerRect() size the dark tray and the banner off mFlagTexW/H,
	//    which EnsureFlagTexture() sets. EnsureFlagTexture() MUST run before AirportInfoYRange() is
	//    first called each Draw() (see the airport/country info strip block below) - call it after
	//    and the tray is sized from last frame's texture, then "pops" into the right size on the
	//    next redraw. This bit us once already; the current ordering is deliberate, not incidental.
	//
	// 5) THIS PORT DELIBERATELY DEVIATES FROM THE CODEX HANDOFF SPEC IN A FEW PLACES - see
	//    WED_FlagAssets.h and WED_FlagProjector.h's own header comments for the full list (no SVG
	//    mask - WED has no SVG rasterizer, so the PNG mask alone is the effective cloth mask;
	//    transparent background instead of flattened-onto-white - WED has no export path to keep
	//    white for, and the handoff's own contract explicitly allows this; synchronous, main-thread
	//    projection - no background job/debounce infrastructure was added; fixed approved defaults
	//    only, no debug UI for quality/curves/opacity/threshold/UV grid). None of these were
	//    oversights - re-litigating them from scratch will just rediscover the same constraints.
	//
	// 6) ASSET PATHS ARE STILL A TEMPORARY HARDCODED DEV-MACHINE CONSTANT (see the top of
	//    WED_FlagAssets.cpp and WED_FlagIndex.cpp). Wiring the ~210 flag/pole/mask/ink/CSV files into
	//    WED's real resource pipeline (WED.rc's GUI_RES entries + cmake's WED_RESOURCE_FILES list,
	//    see src/GUI/GUI_Resources.cpp) is real, necessary follow-up work before this ships to
	//    anyone but this dev machine - it was deliberately deferred so this feature was buildable
	//    and testable without blocking on it. Do not mistake the hardcoded path for a design choice.
	//
	// 7) COUNTRY CODE CHOICES ARE POLICY, NOT BUGS WAITING TO BE "FIXED" - see the header comment in
	//    WED_IocCountryCodes.h before changing ANY mapping in that file. The short version: every
	//    code/flag choice here is deliberately IOC's own current, real classification, with zero
	//    exceptions carved out by this project - not a political stance WED is taking on its own.
	// ============================================================================================
	glPushAttrib(GL_ENABLE_BIT);
	glDisable(GL_CULL_FACE);

	float line_h    = GUI_GetLineHeight(font_UI_Basic);
	float pad       = 4;
	float header_h  = HeaderHeight();
	float row_h     = line_h + pad;

	// --- airport / country info strip ---
	{
		// Figured out (and EnsureFlagTexture() called) BEFORE AirportInfoYRange()
		// below - the tray's own height derives from the flag's now-current
		// mFlagTexW/H (see AirportInfoHeight()), so this must run first or the
		// tray is sized off last frame's texture and only self-corrects on the
		// next redraw (the "pops back into place a beat late" bug).
		string info_text;
		string status_text;		// "" = no commercial-status line to show (only set alongside a successful lookup)
		bool status_warn = false;	// true draws status_text in the same yellow as a warning info_text
		bool warn = false;
		string flag_country;		// "" = no flag banner to show for this state
		mCurrentAirportIcao.clear();	// only set below on a successful lookup - see the "Show Recommendation" button

		if (!mSelectedRamps.empty())
		{
			WED_Airport * apt = WED_GetParentAirport(mSelectedRamps[0]);
			bool same_airport = true;
			for (size_t i = 1; i < mSelectedRamps.size(); ++i)
				if (WED_GetParentAirport(mSelectedRamps[i]) != apt) { same_airport = false; break; }

			if (!same_airport)
			{
				info_text = "Selected ramp starts belong to different airports.";
				warn = true;
			}
			else if (!apt)
			{
				info_text = "No parent airport found.";
				warn = true;
			}
			else
			{
				string icao_primary, apt_name, country;
				apt->GetICAO(icao_primary);	// primary apt.dat identifier ("Airport ID") - X-Plane's
												// own, always-present code; NOT the same thing as the
												// optional "ICAO Code" metadata key below
				apt->GetName(apt_name);

				// Prefer the airport's advertised ICAO Code metadata key when
				// it's actually set: some airports (mostly custom/third-party
				// scenery) carry a synthetic X-Plane identifier as their
				// primary ID (e.g. "XUK001K") while separately recording
				// their real ICAO in metadata (e.g. "UPKS") - that metadata
				// value is the one worth looking up first. The overwhelming
				// majority of real small airports have no ICAO metadata at
				// all - for those, the primary ID itself IS the only code
				// that exists, and is what we fall back to below.
				string icao_meta;
				if (apt->ContainsMetaDataKey("icao_code"))
					icao_meta = apt->GetMetaDataValue("icao_code");

				string icao = !icao_meta.empty() ? icao_meta : icao_primary;
				WED_IcaoLookupResult r = LookupIcaoCountry(mAirportDb, icao, country);

				// A metadata ICAO can be real (recognized by ICAO/IATA) without
				// ever appearing as a primary identifier in X-Plane's own
				// Global Airports database, so a miss there doesn't mean the
				// airport has no usable country info - the primary ID's own
				// apt.dat entry may still carry a perfectly good country line
				// that a metadata-only lookup would otherwise shadow. Give
				// that a second try before giving up.
				if (r == wed_Icao_NotFound && !icao_meta.empty() && icao_meta != icao_primary)
				{
					string country2;
					WED_IcaoLookupResult r2 = LookupIcaoCountry(mAirportDb, icao_primary, country2);
					if (r2 == wed_Icao_Ok)
					{
						icao = icao_primary;
						country = country2;
						r = r2;
					}
				}

				if (r == wed_Icao_Ok)
				{
					info_text = apt_name + " (" + icao + ") - " + country;
					flag_country = country;		// already an IOC-normalized code - see WED_AirportDatabase.cpp
					mCurrentAirportIcao = icao;

					// Specifically whether THIS airport has its own hand-researched Direct
					// Hit entry in WED_AirportDatabase.txt - not the broader "commercially
					// served at all" question AirportIsCommercial() answers (that one falls
					// back to the bulk OurAirports flag, which can say "yes" for an airport
					// this project just hasn't researched yet - a misleading "available"
					// promise when the checklist won't actually show anything for it).
					if (!mAirportDb.IsLoaded() && !mAirportDb.LoadFailed())
						mAirportDb.EnsureLoaded(WedDataFileDir() + "WED_AirportDatabase.txt");
					vector<string> direct_hit = GetRecommendedAirlineCodes(mAirportDb, icao);

					if (!direct_hit.empty())
					{
						status_text = "Livery recommendation is available at this airport";
					}
					else
					{
						status_text = "Recommendation might not be available at this airport";
						status_warn = true;
					}
				}
				else if (r == wed_Icao_Placeholder)
				{
					info_text = "Airport ICAO not set (\"" + icao + "\") - country unknown, can't weight liveries by region.";
					warn = true;
				}
				else if (r == wed_Icao_IndexUnavailable)
				{
					info_text = "WED_AirportDatabase.txt not found - can't look up country.";
					warn = true;
				}
				else
				{
					info_text = "ICAO \"" + icao + "\" not found in the local airport database - country unknown.";
					warn = true;
				}
			}
		}

		EnsureFlagTexture(flag_country);

		// Word-wrap BEFORE AirportInfoYRange() below - the tray's height
		// (AirportInfoHeight()) has to account for however many lines this
		// turns into on the SAME frame the text changes, same ordering
		// requirement as the flag texture (see the top of Draw()). text_x's
		// own final value is recomputed after top/bot are known (just below)
		// since FlagBannerRect() needs them for the actual draw position,
		// but its WIDTH component alone (needed here) never depends on them.
		{
			float banner_w_only = (mFlagTexId != 0) ? (b[2] - b[0]) * 0.5f : 0.0f;
			float avail_text_w = (b[2] - b[0]) - banner_w_only - pad * (mFlagTexId != 0 ? 3 : 2);
			mCachedInfoLines   = WrapText(font_UI_Basic, info_text,   avail_text_w);
			mCachedStatusLines = WrapText(font_UI_Basic, status_text, avail_text_w);
		}

		float top, bot;
		AirportInfoYRange(b, top, bot);

		state->SetState(0,0,0,0,0,0,0);
		glColor4f(0.16f, 0.16f, 0.16f, 1.0f);
		glBegin(GL_QUADS);
			glVertex2f((float) b[0], bot);
			glVertex2f((float) b[2], bot);
			glVertex2f((float) b[2], top);
			glVertex2f((float) b[0], top);
		glEnd();

		float text_x = b[0] + pad;

		if (mFlagTexId != 0)
		{
			// Actual drawing happens at the very end of Draw() (see the
			// bottom of this function) - only the geometry is needed here,
			// to push the info text clear of the banner.
			float bx0, by0, banner_w, banner_h;
			FlagBannerRect(b, top, bot, bx0, by0, banner_w, banner_h);
			text_x = bx0 + banner_w + pad * 2;
		}

		if (!mCachedInfoLines.empty() || !mCachedStatusLines.empty())
		{
			float warn_col[4] = { 1.0f, 0.75f, 0.25f, 1.0f };
			float * text_col = warn ? warn_col : WED_Color_RGBA(wed_Header_Text);
			float status_muted_col[4] = { 0.68f, 0.68f, 0.68f, 1.0f };		// muted - an FYI, not a warning
			float * status_col = status_warn ? warn_col : status_muted_col;
			float mid = bot + (top - bot) * 0.5f;

			int total_lines = (int) mCachedInfoLines.size() + (int) mCachedStatusLines.size();
			float gap = (!mCachedInfoLines.empty() && !mCachedStatusLines.empty()) ? 2.0f : 0.0f;
			float block_h = total_lines * line_h + gap;

			float cursor_y = mid + block_h * 0.5f - line_h * 0.9f;
			for (size_t i = 0; i < mCachedInfoLines.size(); ++i)
			{
				// GUI_Fonts has no bold weight - approximate it with a 1px double-draw
				// (same trick already used below for the "N Ramp Starts Selected" count).
				GUI_FontDraw(state, font_UI_Basic, text_col, text_x,     cursor_y, mCachedInfoLines[i].c_str());
				GUI_FontDraw(state, font_UI_Basic, text_col, text_x + 1, cursor_y, mCachedInfoLines[i].c_str());
				cursor_y -= line_h;
			}
			cursor_y -= gap;
			for (size_t i = 0; i < mCachedStatusLines.size(); ++i)
			{
				GUI_FontDraw(state, font_UI_Basic, status_col, text_x, cursor_y, mCachedStatusLines[i].c_str());
				cursor_y -= line_h;
			}
		}
	}

	// --- header strip ---
	{
		float top, bot;
		HeaderYRange(b, top, bot);
		state->SetState(0,0,0,0,0,0,0);
		glColor4f(0.20f, 0.20f, 0.20f, 1.0f);
		glBegin(GL_QUADS);
			glVertex2f((float) b[0], bot);
			glVertex2f((float) b[2], bot);
			glVertex2f((float) b[2], top);
			glVertex2f((float) b[0], top);
		glEnd();
	}

	float htop, hbot;
	HeaderYRange(b, htop, hbot);
	float * header_col = WED_Color_RGBA(wed_Header_Text);
	float tx = b[0] + pad;
	float ty = htop - header_h * 0.5f - line_h * 0.4f;

	if (mSelectedRamps.empty())
	{
		GUI_FontDraw(state, font_UI_Basic, header_col, tx, ty, "Select a ramp start to edit its liveries.");
		if (mSearchField->IsFocused()) mSearchField->LoseFocus(1);
		mSearchField->Hide();		// no toolbar row to live in - see the search-box block further down
	}
	else if (mSelectedRamps.size() == 1)
	{
		string name;
		mSelectedRamps[0]->GetName(name);
		string header = string("Ramp Start: ") + name;
		GUI_FontDraw(state, font_UI_Basic, header_col, tx, ty, header.c_str());
	}
	else
	{
		char buf[16];
		snprintf(buf, sizeof(buf), "%d", (int) mSelectedRamps.size());
		string count_str(buf);

		// GUI_Fonts has no bold weight - approximate it with a 1px double-draw.
		GUI_FontDraw(state, font_UI_Basic, header_col, tx,     ty, count_str.c_str());
		GUI_FontDraw(state, font_UI_Basic, header_col, tx + 1, ty, count_str.c_str());

		float count_w = GUI_MeasureRange(font_UI_Basic, count_str.c_str(), count_str.c_str() + count_str.size());
		GUI_FontDraw(state, font_UI_Basic, header_col, tx + count_w + 4, ty, " Ramp Starts Selected");
	}

	int cur_op_enum = mSelectedRamps.empty() ? ramp_operation_None : mSelectedRamps[0]->GetRampOperationType();

	// --- ramp operation filter chips ---
	{
		float chip_top, chip_bot;
		FilterYRange(b, chip_top, chip_bot);
		float chip_w = (b[2] - b[0]) / 5.0f;

		for (int i = 0; i < 5; ++i)
		{
			float cx0 = b[0] + i * chip_w;
			float cx1 = cx0 + chip_w;
			bool active  = (kFilterEnumTable[i] == cur_op_enum);
			bool hovered = (mHoverFilterChip == i);

			state->SetState(0,0,0,0,0,0,0);
			if (active)			glColor4f(0.22f, 0.50f, 0.85f, 1.0f);	// selected: vivid blue
			else if (hovered)	glColor4f(0.44f, 0.44f, 0.48f, 1.0f);	// hover: clearly lighter grey
			else				glColor4f(0.18f, 0.18f, 0.18f, 1.0f);	// idle: plain dark grey
			glBegin(GL_QUADS);
				glVertex2f(cx0 + 1, chip_bot + 1);
				glVertex2f(cx1 - 1, chip_bot + 1);
				glVertex2f(cx1 - 1, chip_top - 1);
				glVertex2f(cx0 + 1, chip_top - 1);
			glEnd();

			// 2px inner shade (top+left dark, bottom+right dark too but slightly
			// lighter) so the chip reads as a recessed 3D button rather than a
			// flat color swatch. Capped at 50% black per spec.
			state->SetState(0,0,0,0,1,0,0);
			glColor4f(0.0f, 0.0f, 0.0f, active ? 0.5f : 0.35f);
			glBegin(GL_QUADS);	// top shade
				glVertex2f(cx0 + 1, chip_top - 3);
				glVertex2f(cx1 - 1, chip_top - 3);
				glVertex2f(cx1 - 1, chip_top - 1);
				glVertex2f(cx0 + 1, chip_top - 1);
			glEnd();
			glBegin(GL_QUADS);	// left shade
				glVertex2f(cx0 + 1, chip_bot + 1);
				glVertex2f(cx0 + 3, chip_bot + 1);
				glVertex2f(cx0 + 3, chip_top - 1);
				glVertex2f(cx0 + 1, chip_top - 1);
			glEnd();
			glColor4f(0.0f, 0.0f, 0.0f, active ? 0.25f : 0.18f);
			glBegin(GL_QUADS);	// bottom shade (lighter, so top/left reads as the "light source" side)
				glVertex2f(cx0 + 1, chip_bot + 1);
				glVertex2f(cx1 - 1, chip_bot + 1);
				glVertex2f(cx1 - 1, chip_bot + 3);
				glVertex2f(cx0 + 1, chip_bot + 3);
			glEnd();
			glBegin(GL_QUADS);	// right shade
				glVertex2f(cx1 - 3, chip_bot + 1);
				glVertex2f(cx1 - 1, chip_bot + 1);
				glVertex2f(cx1 - 1, chip_top - 1);
				glVertex2f(cx1 - 3, chip_top - 1);
			glEnd();
			state->SetState(0,0,0,0,0,0,0);

			if (hovered && !active)
			{
				// extra affordance beyond the color shift: a bright border
				glColor4f(1.0f, 1.0f, 1.0f, 1.0f);
				glBegin(GL_LINE_LOOP);
					glVertex2f(cx0 + 1, chip_bot + 1);
					glVertex2f(cx1 - 1, chip_bot + 1);
					glVertex2f(cx1 - 1, chip_top - 1);
					glVertex2f(cx0 + 1, chip_top - 1);
				glEnd();
			}

			if (active)
			{
				// bright underline so "selected" reads at a glance, not just a color tint
				glColor4f(1.0f, 0.8f, 0.2f, 1.0f);
				glBegin(GL_QUADS);
					glVertex2f(cx0 + 1, chip_bot + 1);
					glVertex2f(cx1 - 1, chip_bot + 1);
					glVertex2f(cx1 - 1, chip_bot + 4);
					glVertex2f(cx0 + 1, chip_bot + 4);
				glEnd();
			}

			float * txt_col = WED_Color_RGBA(wed_Table_Text);
			float tw = GUI_MeasureRange(font_UI_Basic, kFilterLabels[i], kFilterLabels[i] + strlen(kFilterLabels[i]));
			GUI_FontDraw(state, font_UI_Basic, txt_col, cx0 + (chip_w - tw) * 0.5f, (chip_top + chip_bot) * 0.5f - line_h * 0.35f, kFilterLabels[i]);
		}
	}

	// --- size range slider ---
	{
		float handle_r = line_h * 0.5f;
		float slider_top, slider_bot;
		SliderYRange(b, slider_top, slider_bot);
		float track_y  = slider_bot + line_h * 0.5f;
		float track_x0 = b[0] + pad + handle_r;
		float track_x1 = b[2] - pad - handle_r;

		int minIdx = 0, maxIdx = 5;
		if (!mSelectedRamps.empty())
		{
			minIdx = WidthEnumToIndex(mSelectedRamps[0]->GetWidthMin());
			maxIdx = WidthEnumToIndex(mSelectedRamps[0]->GetWidth());
		}

		float * lbl_col    = WED_Color_RGBA(wed_Table_Text);
		float * track_col  = WED_Color_RGBA(wed_Table_Gridlines);
		float * header_col2 = WED_Color_RGBA(wed_Header_Text);

		// zone background + border, so this reads as one distinct control block
		state->SetState(0,0,0,0,0,0,0);
		glColor4f(0.14f, 0.14f, 0.14f, 1.0f);
		glBegin(GL_QUADS);
			glVertex2f((float) b[0] + 1, slider_bot);
			glVertex2f((float) b[2] - 1, slider_bot);
			glVertex2f((float) b[2] - 1, slider_top);
			glVertex2f((float) b[0] + 1, slider_top);
		glEnd();
		glColor4f(0.40f, 0.40f, 0.40f, 1.0f);
		glBegin(GL_LINE_LOOP);
			glVertex2f((float) b[0] + 1, slider_bot);
			glVertex2f((float) b[2] - 1, slider_bot);
			glVertex2f((float) b[2] - 1, slider_top);
			glVertex2f((float) b[0] + 1, slider_top);
		glEnd();

		// title, so it's unmistakable what this control is
		GUI_FontDraw(state, font_UI_Basic, header_col2, b[0] + pad, slider_top - line_h * 0.9f, "Size (ICAO Wingspan Category)");

		for (int i = 0; i < 6; ++i)
		{
			float fx = track_x0 + (track_x1 - track_x0) * i / 5.0f;
			float tw = GUI_MeasureRange(font_UI_Basic, kWidthLabels[i], kWidthLabels[i] + 1);
			GUI_FontDraw(state, font_UI_Basic, lbl_col, fx - tw * 0.5f, slider_top - line_h * 1.9f, kWidthLabels[i]);
		}

		state->SetState(0,0,0,0,0,0,0);
		glDisable(GL_TEXTURE_2D);	// belt-and-suspenders: make sure nothing textured/blended leaks in here
		glDisable(GL_BLEND);

		// faint tick marks at each of the six grid positions
		glColor4f(track_col[0], track_col[1], track_col[2], 1.0f);
		for (int i = 0; i < 6; ++i)
		{
			float fx = track_x0 + (track_x1 - track_x0) * i / 5.0f;
			glBegin(GL_LINES);
				glVertex2f(fx, track_y - 4);
				glVertex2f(fx, track_y + 4);
			glEnd();
		}

		float min_x = track_x0 + (track_x1 - track_x0) * minIdx / 5.0f;
		float max_x = track_x0 + (track_x1 - track_x0) * maxIdx / 5.0f;

		// bold white line filling the range between the two balls - drawn as a
		// filled quad PLUS an outline border, so it stays visible even if fills
		// alone were ever the problem (lines have proven reliable so far)
		glColor4f(1.0f, 1.0f, 1.0f, 1.0f);
		glBegin(GL_QUADS);
			glVertex2f(min_x, track_y - 5);
			glVertex2f(max_x, track_y - 5);
			glVertex2f(max_x, track_y + 5);
			glVertex2f(min_x, track_y + 5);
		glEnd();
		glColor4f(0.05f, 0.05f, 0.05f, 1.0f);
		glBegin(GL_LINE_LOOP);
			glVertex2f(min_x, track_y - 5);
			glVertex2f(max_x, track_y - 5);
			glVertex2f(max_x, track_y + 5);
			glVertex2f(min_x, track_y + 5);
		glEnd();

		// hover ring: whichever ball is currently under the mouse (or being
		// dragged) gets a bright highlight ring drawn around it. When the two
		// balls overlap, hit-testing reports "2" for either - light up both
		// (they're at the same spot, so this draws once in practice) so an
		// overlapped pair doesn't look unresponsive.
		bool ring_min = (mHoverSliderHandle == 0 || mDragHandle == 0 || mHoverSliderHandle == 2 || mDragHandle == 2);
		bool ring_max = (mHoverSliderHandle == 1 || mDragHandle == 1 || mHoverSliderHandle == 2 || mDragHandle == 2);

		if (ring_min)
		{
			glColor4f(1.0f, 0.85f, 0.3f, 1.0f);
			DrawCircleOutline(min_x, track_y, handle_r + 3);
		}
		if (ring_max)
		{
			glColor4f(1.0f, 0.85f, 0.3f, 1.0f);
			DrawCircleOutline(max_x, track_y, handle_r + 3);
		}

		// min ball (orange), max ball (blue), each with a dark outline for contrast -
		// min drawn first so the max ball wins on top when the two happen to overlap
		glColor4f(0.9f, 0.6f, 0.2f, 1.0f);
		DrawFilledCircle(min_x, track_y, handle_r);
		glColor4f(0.05f, 0.05f, 0.05f, 1.0f);
		DrawCircleOutline(min_x, track_y, handle_r);
		DrawGrabberDashes(min_x, track_y, handle_r);

		glColor4f(0.3f, 0.6f, 0.9f, 1.0f);
		DrawFilledCircle(max_x, track_y, handle_r);
		glColor4f(0.05f, 0.05f, 0.05f, 1.0f);
		DrawCircleOutline(max_x, track_y, handle_r);
		DrawGrabberDashes(max_x, track_y, handle_r);
	}

	// --- airline list workspace: toolbar row (Show Recommendation + Sort) boxed
	// together with the checklist below it, so they read as one panel ---
	if (!mSelectedRamps.empty())
	{
		float * row_col = WED_Color_RGBA(wed_Table_Text);

		// Border around the whole workspace - toolbar row plus every row of
		// content below it, down to the bottom of the pane.
		float tb_top, tb_bot;
		ListToolbarYRange(b, tb_top, tb_bot);
		state->SetState(0,0,0,0,0,0,0);
		glColor4f(0.40f, 0.40f, 0.40f, 1.0f);
		glBegin(GL_LINE_LOOP);
			glVertex2f((float) b[0] + 1, (float) b[1] + 1);
			glVertex2f((float) b[2] - 1, (float) b[1] + 1);
			glVertex2f((float) b[2] - 1, tb_top - 1);
			glVertex2f((float) b[0] + 1, tb_top - 1);
		glEnd();
		// Divider between the toolbar row and the content below it.
		glBegin(GL_LINES);
			glVertex2f((float) b[0] + 1, tb_bot);
			glVertex2f((float) b[2] - 1, tb_bot);
		glEnd();

		bool recommend_available = AirportIsCommercial(mAirportDb, mCurrentAirportIcao);

		// "Show Recommendation" toggle - left of the sort button. Greyed out
		// (75% black mask, unclickable - see hit-testing in MouseDown/Up)
		// when this airport has no commercial data to recommend from at all.
		{
			float r[4];
			RecommendButtonRect(b, r);
			bool on = gShowLiveryRecommendation != 0;
			state->SetState(0,0,0,0,0,0,0);
			if (on)							glColor4f(0.22f, 0.50f, 0.85f, 1.0f);
			else if (mTrackRecommendButton)	glColor4f(0.14f, 0.14f, 0.16f, 1.0f);	// pushed: darker/inset
			else if (mHoverRecommendButton)	glColor4f(0.44f, 0.44f, 0.48f, 1.0f);
			else							glColor4f(0.22f, 0.22f, 0.22f, 1.0f);
			glBegin(GL_QUADS);
				glVertex2f(r[0]+1, r[1]+1); glVertex2f(r[2]-1, r[1]+1);
				glVertex2f(r[2]-1, r[3]-1); glVertex2f(r[0]+1, r[3]-1);
			glEnd();
			glColor4f(0.5f, 0.5f, 0.5f, 1.0f);
			glBegin(GL_LINE_LOOP);
				glVertex2f(r[0]+1, r[1]+1); glVertex2f(r[2]-1, r[1]+1);
				glVertex2f(r[2]-1, r[3]-1); glVertex2f(r[0]+1, r[3]-1);
			glEnd();

			const char * label = "Show Recommendation";
			float lw = GUI_MeasureRange(font_UI_Basic, label, label + strlen(label));
			GUI_FontDraw(state, font_UI_Basic, row_col, r[0] + ((r[2]-r[0])-lw)*0.5f, r[1] + ((r[3]-r[1])-line_h)*0.5f + 2, label);

			if (!recommend_available)
			{
				state->SetState(0,0,0,0,1,0,0);
				glColor4f(0.0f, 0.0f, 0.0f, 0.75f);
				glBegin(GL_QUADS);
					glVertex2f(r[0]+1, r[1]+1); glVertex2f(r[2]-1, r[1]+1);
					glVertex2f(r[2]-1, r[3]-1); glVertex2f(r[0]+1, r[3]-1);
				glEnd();
				state->SetState(0,0,0,0,0,0,0);
			}
		}

		// Sort A-Z / Z-A toggle - flush to the right border.
		{
			float r[4];
			SortButtonRect(b, r);
			state->SetState(0,0,0,0,0,0,0);
			if (mTrackSortButton)		glColor4f(0.14f, 0.14f, 0.16f, 1.0f);
			else if (mHoverSortButton)	glColor4f(0.44f, 0.44f, 0.48f, 1.0f);
			else						glColor4f(0.22f, 0.22f, 0.22f, 1.0f);
			glBegin(GL_QUADS);
				glVertex2f(r[0]+1, r[1]+1); glVertex2f(r[2]-1, r[1]+1);
				glVertex2f(r[2]-1, r[3]-1); glVertex2f(r[0]+1, r[3]-1);
			glEnd();
			glColor4f(0.5f, 0.5f, 0.5f, 1.0f);
			glBegin(GL_LINE_LOOP);
				glVertex2f(r[0]+1, r[1]+1); glVertex2f(r[2]-1, r[1]+1);
				glVertex2f(r[2]-1, r[3]-1); glVertex2f(r[0]+1, r[3]-1);
			glEnd();

			const char * label = mSortDescending ? "Z-A" : "A-Z";
			float lw = GUI_MeasureRange(font_UI_Basic, label, label + strlen(label));
			float arrow_w = line_h * 0.6f;
			float total_w = lw + 4 + arrow_w;
			float tx = r[0] + ((r[2]-r[0])-total_w)*0.5f;
			GUI_FontDraw(state, font_UI_Basic, row_col, tx, r[1] + ((r[3]-r[1])-line_h)*0.5f + 2, label);

			// Little vector arrow (no font-glyph-availability risk this way):
			// pointing up for A-Z (ascending), down for Z-A (descending).
			float ax = tx + lw + 4 + arrow_w * 0.5f;
			float ay = (r[1] + r[3]) * 0.5f;
			glColor4f(row_col[0], row_col[1], row_col[2], 1.0f);
			glBegin(GL_TRIANGLES);
				if (!mSortDescending)
				{
					glVertex2f(ax - arrow_w*0.5f, ay - arrow_w*0.35f);
					glVertex2f(ax + arrow_w*0.5f, ay - arrow_w*0.35f);
					glVertex2f(ax,                ay + arrow_w*0.5f);
				}
				else
				{
					glVertex2f(ax - arrow_w*0.5f, ay + arrow_w*0.35f);
					glVertex2f(ax + arrow_w*0.5f, ay + arrow_w*0.35f);
					glVertex2f(ax,                ay - arrow_w*0.5f);
				}
			glEnd();
		}

		// Live-filter search box, filling the toolbar row left of the
		// Recommend button. GUI_TextField always paints its own opaque
		// box+border regardless of content and has no native placeholder, so
		// we hide the real field and hand-draw a "Lookup Operators"
		// placeholder whenever it's both empty and unfocused, and let the
		// real field take over the moment there's text or it's being edited.
		// Repositioning every Draw() (rather than just once) keeps it glued
		// to RecommendButtonRect() if the pane is ever resized.
		{
			float r[4];
			SearchFieldRect(b, r);
			bool has_query = !mSearchQuery.empty();

			// With an entry to clear, the field's own bounds are trimmed to
			// leave the clear button its own non-overlapping strip on the
			// right - the button is drawn AFTER the field only in z-order
			// (this pane's own Draw() body runs before children - see the
			// culling gotcha note at the top of this function for the same
			// "draw order isn't what you'd assume" theme), so it needs its
			// own space rather than trying to paint over the field's opaque
			// background.
			float field_r2 = r[2];
			if (has_query)
			{
				float clear_r[4];
				SearchClearButtonRect(b, clear_r);
				field_r2 = clear_r[0] - 2;
			}
			int ib[4] = { (int) r[0], (int) r[1], (int) field_r2, (int) r[3] };
			mSearchField->SetBounds(ib);

			bool show_real = has_query || mSearchField->IsFocused();
			if (show_real)
			{
				mSearchField->Show();

				if (has_query)
				{
					float cr[4];
					SearchClearButtonRect(b, cr);
					state->SetState(0,0,0,0,0,0,0);

					bool pressed = mTrackClearButton;
					float fill = pressed ? 0.70f : (mHoverClearButton ? 0.94f : 0.82f);
					glColor4f(fill, fill, fill, 1.0f);
					glBegin(GL_QUADS);
						glVertex2f(cr[0]+1, cr[1]+1); glVertex2f(cr[2]-1, cr[1]+1);
						glVertex2f(cr[2]-1, cr[3]-1); glVertex2f(cr[0]+1, cr[3]-1);
					glEnd();

					// Two-tone bevel stroke - light/dark corners swap when
					// pressed, for a simple "raised" vs "pushed in" read
					// without needing an actual icon resource.
					float light = pressed ? 0.35f : 0.95f;
					float dark  = pressed ? 0.95f : 0.35f;
					glBegin(GL_LINES);
						glColor4f(light, light, light, 1.0f);
						glVertex2f(cr[0]+1, cr[3]-1); glVertex2f(cr[0]+1, cr[1]+1);
						glVertex2f(cr[0]+1, cr[1]+1); glVertex2f(cr[2]-1, cr[1]+1);
						glColor4f(dark, dark, dark, 1.0f);
						glVertex2f(cr[2]-1, cr[1]+1); glVertex2f(cr[2]-1, cr[3]-1);
						glVertex2f(cr[2]-1, cr[3]-1); glVertex2f(cr[0]+1, cr[3]-1);
					glEnd();

					float x_r = (cr[3]-cr[1]) * 0.22f;
					glColor4f(0.25f, 0.25f, 0.25f, 1.0f);
					DrawX((cr[0]+cr[2])*0.5f + (pressed ? 0.5f : 0.0f), (cr[1]+cr[3])*0.5f - (pressed ? 0.5f : 0.0f), x_r);
				}
			}
			else
			{
				mSearchField->Hide();

				// Idle appearance is deliberately close to the real GUI_TextField's
				// own white background (see its constructor's mColorBkgnd) rather
				// than matching the dark Sort/Recommend buttons - this box IS a
				// text field, just not focused/typed-in yet.
				state->SetState(0,0,0,0,0,0,0);
				glColor4f(0.82f, 0.82f, 0.82f, 1.0f);
				glBegin(GL_QUADS);
					glVertex2f(r[0]+1, r[1]+1); glVertex2f(r[2]-1, r[1]+1);
					glVertex2f(r[2]-1, r[3]-1); glVertex2f(r[0]+1, r[3]-1);
				glEnd();
				glColor4f(0.45f, 0.45f, 0.45f, 1.0f);
				glBegin(GL_LINE_LOOP);
					glVertex2f(r[0]+1, r[1]+1); glVertex2f(r[2]-1, r[1]+1);
					glVertex2f(r[2]-1, r[3]-1); glVertex2f(r[0]+1, r[3]-1);
				glEnd();

				// Small hand-drawn magnifying glass (circle + diagonal handle,
				// bottom-right) - no icon font/resource anywhere in this
				// codebase to pull a real one from.
				float icon_r = line_h * 0.32f;
				float icon_cx = r[2] - 8 - icon_r;
				float icon_cy = (r[1] + r[3]) * 0.5f;
				glColor4f(0.35f, 0.35f, 0.35f, 1.0f);
				DrawCircleOutline(icon_cx, icon_cy, icon_r);
				glBegin(GL_LINES);
					glVertex2f(icon_cx + icon_r*0.7f, icon_cy - icon_r*0.7f);
					glVertex2f(icon_cx + icon_r*1.5f, icon_cy - icon_r*1.5f);
				glEnd();

				// Truncate (never overflow the field) when a narrow pane leaves less
				// room than the full placeholder needs - a search box that's still
				// wide enough for a couple of letters is more useful than one that
				// silently paints text past its own border.
				string placeholder = "Lookup Operators";
				float avail_w = (icon_cx - icon_r - 4) - (r[0] + 6);
				while (!placeholder.empty() && GUI_MeasureRange(font_UI_Basic, placeholder.c_str(), placeholder.c_str() + placeholder.size()) > avail_w)
					placeholder.pop_back();
				if (!placeholder.empty())
				{
					float ph_col[4] = { 0.35f, 0.35f, 0.35f, 1.0f };
					GUI_FontDraw(state, font_UI_Basic, ph_col, r[0] + 6, r[1] + ((r[3]-r[1])-line_h)*0.5f + 2, placeholder.c_str());
				}
			}
		}

		// --- livery preview card strip (framework/scaffolding only - see this pane's
		// .h comment on this section, and WED_LiveryThumbnailCache.h for the render/
		// cache/evict design). Text and per-card country are still placeholders -
		// there's no airline/aircraft/asset matching to draw real ones from yet. ---
		if (!mPreviewObjVpaths.empty())
		{
			float content_top = ContentTop(b);
			float content_bot = (float) b[1];
			int n_cards = (int) mPreviewObjVpaths.size();

			// Only cards that actually land inside the content viewport get drawn (and
			// therefore rendered) - CardRectForIndex() reads the SHARED mScrollOffset,
			// so this range walks with the checklist below rather than independently.
			// The scroll offset itself is clamped by the checklist block further down,
			// which is the part that knows the total content height.
			int first_visible = -1, last_visible = -1;
			for (int ci = 0; ci < n_cards; ++ci)
			{
				float r[4];
				CardRectForIndex(b, ci, r);
				if (r[1] >= content_top || r[3] <= content_bot) continue;	// fully off one end
				if (first_visible < 0) first_visible = ci;
				last_visible = ci;
			}

			// A WIDER range (+/- one card) is kept CACHED even while off screen.
			// Without this margin, a card evicted the instant it scrolls out gets
			// re-rendered from scratch the instant it scrolls back in - fast
			// back-and-forth scrolling would thrash the cache and stutter every frame
			// instead of scrolling smoothly through already-cached neighbours.
			set<string> keep_alive_vpaths;
			if (first_visible >= 0)
			{
				const int kMargin = 1;
				int first_keep = (std::max)(0, first_visible - kMargin);
				int last_keep  = (std::min)(n_cards - 1, last_visible + kMargin);
				for (int ki = first_keep; ki <= last_keep; ++ki)
					keep_alive_vpaths.insert(mPreviewObjVpaths[ki]);
			}

			WED_ResourceMgr * res_mgr = WED_GetResourceMgr(mResolver);
			ITexMgr * tex_mgr = WED_GetTexMgr(mResolver);

			// Clip to the content viewport - GUI_Pane::InternalDraw() only scissors to
			// this WHOLE PANE's bounds, not this section's, so without this a card
			// scrolled half past the top would paint its full, unclipped image quad
			// straight over the toolbar/slider above it.
			glPushAttrib(GL_SCISSOR_BIT);
			glEnable(GL_SCISSOR_TEST);
			glScissor((int) b[0], (int) content_bot, (int) (b[2] - b[0]), (int) (content_top - content_bot));

			// Caps how many BRAND NEW (not-yet-cached) thumbnails get rendered in this
			// one Draw() call. A big scrollbar jump can reveal several never-before-
			// seen cards at once; rendering all of them in a single frame is exactly
			// the kind of frame-time spike that reads as a stutter/freeze. Already-
			// cached cards (the common case once the strip settles) are unaffected -
			// this only throttles actual off-screen renders.
			const int kMaxRendersPerFrame = 2;
			int renders_this_frame = 0;

			for (int ci = first_visible; ci >= 0 && ci <= last_visible; ++ci)
			{
				float r[4];
				CardRectForIndex(b, ci, r);

				const string & vpath = mPreviewObjVpaths[ci];

				float card_x0   = r[0], card_x1 = r[2];
				float card_bot  = r[1], card_top = r[3];
				float image_h   = (card_x1 - card_x0) / kCardImageAspect;	// must match CardHeight()
				float image_top = card_top;
				float image_bot = image_top - image_h;
				float text_bot  = card_bot;

				// --- drop shadow: a few offset, increasingly transparent slabs down
				// and to the right. Cheap stand-in for a real blur (no shader/FBO
				// pass needed) and enough to lift the card off the panel so the grid
				// reads as separate cards rather than one tiled sheet. ---
				state->SetState(0,0,0,0,1,0,0);		// blend on, no texture
				for (int s = 3; s >= 1; --s)
				{
					glColor4f(0, 0, 0, 0.13f);
					glBegin(GL_QUADS);
						glVertex2f(card_x0 + s, card_bot - s);
						glVertex2f(card_x1 + s, card_bot - s);
						glVertex2f(card_x1 + s, card_top - s);
						glVertex2f(card_x0 + s, card_top - s);
					glEnd();
				}

				// --- card body: the image sits directly on this, and the thumbnail's
				// transparent background lets it show through (see the blend note on
				// the image quad below), so this IS the picture's backdrop. Selected
				// cards sit "pressed in" (darker); hover lifts it slightly. ---
				bool is_selected = mSelectedCards.count(ci) != 0;
				bool is_pressed  = (mTrackCard == ci);
				bool is_hovered  = (mHoverCard == ci) && !is_pressed;

				// A selected card swaps its whole body from neutral grey to the
				// picker's green (0x639875). The sheen drawn later is plain white at
				// low alpha, so it lightens whatever is underneath - over the green
				// that reads as a brighter green band, which is what makes the
				// selected state obvious at a glance rather than subtle.
				float body_r, body_g, body_b;
				if (is_selected)
				{
					body_r = 0.388f; body_g = 0.596f; body_b = 0.459f;		// 0x639875
					float k = is_pressed ? 0.82f : (is_hovered ? 1.12f : 1.0f);
					body_r = (std::min)(1.0f, body_r * k);
					body_g = (std::min)(1.0f, body_g * k);
					body_b = (std::min)(1.0f, body_b * k);
				}
				else
				{
					float g = 0.17f;
					if (is_pressed)			g = 0.13f;
					else if (is_hovered)	g = 0.21f;
					body_r = g; body_g = g; body_b = g + 0.02f;
				}

				state->SetState(0,0,0,0,0,0,0);
				glColor4f(body_r, body_g, body_b, 1.0f);
				glBegin(GL_QUADS);
					glVertex2f(card_x0, card_bot);
					glVertex2f(card_x1, card_bot);
					glVertex2f(card_x1, card_top);
					glVertex2f(card_x0, card_top);
				glEnd();

				// Footer plate behind the caption, a touch darker than the body so the
				// card reads as "picture above, label below" like a real trading card.
				glColor4f(body_r * 0.78f, body_g * 0.78f, body_b * 0.78f, 1.0f);
				glBegin(GL_QUADS);
					glVertex2f(card_x0, card_bot);
					glVertex2f(card_x1, card_bot);
					glVertex2f(card_x1, image_bot);
					glVertex2f(card_x0, image_bot);
				glEnd();

				// --- sheen: the "printed plastic" gloss of a credit/trading card. Two
				// white gradients (a top-down wash plus a diagonal sweep), both fading
				// to fully transparent, drawn with per-vertex alpha so no texture is
				// needed.
				//
				// ORDER MATTERS: this goes ABOVE the body colour but BELOW the
				// thumbnail. The gloss belongs to the card's own surface - letting it
				// wash over the aircraft makes the photo itself look hazy/greasy
				// instead of making the card look laminated. ---
				state->SetState(0,0,0,0,1,0,0);		// blend on, no texture

				// White has very little headroom over the selected card's light green
				// (0x639875), so the same alpha that looks right on the dark grey body
				// is invisible there - the sheen is deliberately stronger when
				// selected so it stays legible on both.
				float sheen_top_a = is_selected ? 0.10f : 0.040f;
				float sheen_bot = card_bot + (card_top - card_bot) * 0.45f;
				glBegin(GL_QUADS);
					glColor4f(1,1,1,0.0f);        glVertex2f(card_x0, sheen_bot);
					glColor4f(1,1,1,0.0f);        glVertex2f(card_x1, sheen_bot);
					glColor4f(1,1,1,sheen_top_a); glVertex2f(card_x1, card_top);
					glColor4f(1,1,1,sheen_top_a); glVertex2f(card_x0, card_top);
				glEnd();

				// The diagonal sweep: a band that peaks along its centre line and fades
				// out on BOTH sides (two gradient quads meeting at that line), leaning
				// across the full card. A single-sided fade is invisible against the
				// body colour; a soft core with wide falloff is what reads as light on
				// a laminated surface. Wide and weak on purpose - a narrow, bright
				// version of exactly this looks greasy rather than glossy.
				float kStreak = is_selected ? 0.20f : 0.065f;	// see the sheen note above re: green
				float band_w  = (card_x1 - card_x0) * 0.30f;
				float band_cx = card_x0 + (card_x1 - card_x0) * 0.42f;
				float skew    = (card_top - card_bot) * 0.75f;
				glBegin(GL_QUADS);
					glColor4f(1,1,1,0.0f);     glVertex2f(band_cx - band_w,        card_bot);
					glColor4f(1,1,1,kStreak);  glVertex2f(band_cx,                 card_bot);
					glColor4f(1,1,1,kStreak);  glVertex2f(band_cx + skew,          card_top);
					glColor4f(1,1,1,0.0f);     glVertex2f(band_cx - band_w + skew, card_top);
				glEnd();
				glBegin(GL_QUADS);
					glColor4f(1,1,1,kStreak);  glVertex2f(band_cx,                 card_bot);
					glColor4f(1,1,1,0.0f);     glVertex2f(band_cx + band_w,        card_bot);
					glColor4f(1,1,1,0.0f);     glVertex2f(band_cx + band_w + skew, card_top);
					glColor4f(1,1,1,kStreak);  glVertex2f(band_cx + skew,          card_top);
				glEnd();
				glColor4f(1,1,1,1);
				state->SetState(0,0,0,0,0,0,0);

				// 3D snapshot - rendered/cached by mThumbCache, off-screen (see that
				// class - never a live 3D view). Texcoords use the plain GL
				// render-to-texture convention (t=0 at the bottom) since this texture
				// came from our own FBO render, not a loaded image file.
				bool already_cached = mThumbCache.IsCached(vpath);
				const WED_LiveryThumbnail * thumb = nullptr;
				if (already_cached || renders_this_frame < kMaxRendersPerFrame)
				{
					thumb = mThumbCache.GetThumbnail(res_mgr, tex_mgr, state, vpath);
					if (!already_cached) ++renders_this_frame;
				}
				if (thumb && thumb->tex != 0)
				{
					// Blend ON so the thumbnail's transparent background (the cache
					// clears its FBO to alpha 0 and only the model itself writes
					// opaque pixels) lets the card body above show through, rather
					// than stamping a black rectangle over it.
					state->SetState(0,1,0,0,1,0,0);
					glColor4f(1,1,1,1);
					state->BindTex((int) thumb->tex, 0);
					glBegin(GL_QUADS);
						glTexCoord2f(0,0); glVertex2f(card_x0, image_bot);
						glTexCoord2f(1,0); glVertex2f(card_x1, image_bot);
						glTexCoord2f(1,1); glVertex2f(card_x1, image_top);
						glTexCoord2f(0,1); glVertex2f(card_x0, image_top);
					glEnd();
					state->SetState(0,0,0,0,0,0,0);
				}

				// Flag icon first - the caption is truncated to whatever room is left
				// beside it, so a narrow pane can never overlap the two.
				float text_room_x1 = card_x1 - 4;
				const WED_LiveryThumbnail * flag = EnsureRawFlagTexture(string("USA"));	// TODO: real per-card country once wired up
				if (flag && flag->tex != 0 && flag->w > 0 && flag->h > 0)
				{
					float icon_h = (image_bot - text_bot) - 6;
					float icon_w = icon_h * ((float) flag->w / (float) flag->h);
					float fx0 = card_x1 - 4 - icon_w;
					float fy_bot = text_bot + 3;
					float fy_top = fy_bot + icon_h;
					text_room_x1 = fx0 - 4;

					// Loaded via WED_LoadPngTopDownARGB (same as the country banner
					// above) - t=0 belongs at the screen-top vertex, same reasoning
					// as that banner's own draw call.
					state->SetState(0,1,0,0,1,0,0);
					glColor4f(1,1,1,1);
					state->BindTex((int) flag->tex, 0);
					glBegin(GL_QUADS);
						glTexCoord2f(0,0); glVertex2f(fx0,          fy_top);
						glTexCoord2f(1,0); glVertex2f(fx0 + icon_w, fy_top);
						glTexCoord2f(1,1); glVertex2f(fx0 + icon_w, fy_bot);
						glTexCoord2f(0,1); glVertex2f(fx0,          fy_bot);
					glEnd();
					state->SetState(0,0,0,0,0,0,0);
				}

				// Caption line: "ICAO - Name" on the left, country code right-aligned
				// just inside the flag. All placeholder text for now - see the comment
				// at the top of this block.
				const char * card_icao    = "ICAO";
				const char * card_name    = "Placeholder Airline";
				const char * card_country = "USA";

				float text_col[4] = { 0.88f, 0.88f, 0.90f, 1.0f };
				float text_y = text_bot + ((image_bot - text_bot) - line_h) * 0.5f + 2;

				// Country code sits immediately left of the flag; the left-hand phrase
				// gets whatever is left over.
				float cc_w = GUI_MeasureRange(font_UI_Basic, card_country, card_country + strlen(card_country));
				float cc_x = text_room_x1 - cc_w;
				GUI_FontDraw(state, font_UI_Basic, text_col, cc_x, text_y, card_country);

				// Ellipsis truncation: trim characters off the END until the string
				// PLUS the "..." fits, so the dots are always the last three glyphs
				// and always land inside the bound (the country code's left edge) -
				// never hanging over it or getting clipped themselves.
				float left_avail = (cc_x - 6) - (card_x0 + 5);
				string caption = string(card_icao) + " - " + card_name;
				if (GUI_MeasureRange(font_UI_Basic, caption.c_str(), caption.c_str() + caption.size()) > left_avail)
				{
					const string ell = "...";
					while (!caption.empty())
					{
						caption.pop_back();
						while (!caption.empty() && caption[caption.size() - 1] == ' ')
							caption.pop_back();			// no "Air ..." - tuck the dots up against the text
						string probe = caption + ell;
						if (GUI_MeasureRange(font_UI_Basic, probe.c_str(), probe.c_str() + probe.size()) <= left_avail)
							break;
					}
					caption = caption.empty() ? string() : caption + ell;
				}
				if (!caption.empty())
					GUI_FontDraw(state, font_UI_Basic, text_col, card_x0 + 5, text_y, caption.c_str());

				// --- selected tick, dead centre, only once the toggle is actually on
				// (i.e. after a completed press-and-release) ---
				if (is_selected)
				{
					int tick_tex = GUI_GetTextureResource("livery_selected.png", tex_Linear | tex_Mipmap, NULL);
					if (tick_tex)
					{
						float tick = (std::min)((card_x1 - card_x0), (image_top - image_bot)) * 0.38f;
						float cx = (card_x0 + card_x1) * 0.5f;
						float cy = (image_bot + image_top) * 0.5f;

						// t=0 at the BOTTOM here - GUI_GetTextureResource() hands back a
						// normal bottom-up GL texture, unlike the flag icons loaded
						// through WED_LoadPngTopDownARGB() a few lines up, which need
						// the opposite mapping. Getting these two mixed up flips the
						// tick upside down.
						state->SetState(0,1,0,0,1,0,0);
						state->BindTex(tick_tex, 0);

						// Shadow first: the same textured quad in black, CONCENTRIC
						// (not offset) and scaled slightly up, twice - a soft dark
						// halo hugging the tick's own outline rather than a shifted
						// copy of it. Default GL_MODULATE means colour * texture, so
						// black plus the tick's own alpha gives its silhouette free.
						for (int k = 1; k >= 0; --k)
						{
							float grow = tick * (k ? 0.077f : 0.035f);	// 30% tighter than the first pass at this
							glColor4f(0, 0, 0, k ? 0.091f : 0.154f);	// ...and 30% weaker
							glBegin(GL_QUADS);
								glTexCoord2f(0,0); glVertex2f(cx - tick - grow, cy - tick - grow);
								glTexCoord2f(1,0); glVertex2f(cx + tick + grow, cy - tick - grow);
								glTexCoord2f(1,1); glVertex2f(cx + tick + grow, cy + tick + grow);
								glTexCoord2f(0,1); glVertex2f(cx - tick - grow, cy + tick + grow);
							glEnd();
						}

						glColor4f(1,1,1,0.95f);
						glBegin(GL_QUADS);
							glTexCoord2f(0,0); glVertex2f(cx - tick, cy - tick);
							glTexCoord2f(1,0); glVertex2f(cx + tick, cy - tick);
							glTexCoord2f(1,1); glVertex2f(cx + tick, cy + tick);
							glTexCoord2f(0,1); glVertex2f(cx - tick, cy + tick);
						glEnd();
						state->SetState(0,0,0,0,0,0,0);
					}
				}

				// --- frame last, so it sits over the picture, footer and sheen ---
				state->SetState(0,0,0,0,0,0,0);
				if (is_selected)		glColor4f(0.60f, 0.82f, 0.69f, 1.0f);	// a lighter tint of the body green, so the edge still reads as an edge
				else if (is_hovered)	glColor4f(0.52f, 0.52f, 0.56f, 1.0f);
				else					glColor4f(0.34f, 0.34f, 0.38f, 1.0f);
				glBegin(GL_LINE_LOOP);
					glVertex2f(card_x0 + 0.5f, card_bot + 0.5f);
					glVertex2f(card_x1 - 0.5f, card_bot + 0.5f);
					glVertex2f(card_x1 - 0.5f, card_top - 0.5f);
					glVertex2f(card_x0 + 0.5f, card_top - 0.5f);
				glEnd();
				glColor4f(0.26f, 0.26f, 0.29f, 1.0f);		// hairline between picture and caption
				glBegin(GL_LINES);
					glVertex2f(card_x0 + 1, image_bot);
					glVertex2f(card_x1 - 1, image_bot);
				glEnd();
			}
			glPopAttrib();		// restores GL_SCISSOR_TEST enable + rect to whatever they were on entry
			mThumbCache.EvictNotVisible(keep_alive_vpaths);
		}

		if (cur_op_enum == ramp_operation_None)
		{
			const char * msg = "No static aircraft will spawn at this spot";
			float msg_w = GUI_MeasureRange(font_UI_Basic, msg, msg + strlen(msg));
			float cx = ((float) b[0] + (float) b[2]) * 0.5f;
			float cy = ((float) b[1] + tb_bot) * 0.5f;
			GUI_FontDraw(state, font_UI_Basic, row_col, cx - msg_w * 0.5f, cy - line_h * 0.4f, msg);
		}
		else
		{
			vector<WED_LiveryDisplayRow> rows = BuildCurrentDisplayRows(mSelectedRamps[0], mSortDescending,
												gShowLiveryRecommendation != 0, mAirportDb, mCurrentAirportIcao, mSearchQuery, mAirlineDirectory, mPopularAirlinesShuffleCache);
			// How many of the selected ramps carry each code, computed ONCE per draw.
			// Asking per row would be O(rows x ramps) every frame - a few hundred
			// ramps against a few hundred rows is tens of thousands of string
			// parses, per frame, for a checkbox.
			map<string,int> code_counts;
			for (size_t i = 0; i < mSelectedRamps.size(); ++i)
			{
				set<string> c = ParseCodes(mSelectedRamps[i]->GetAirlines());
				for (set<string>::const_iterator j = c.begin(); j != c.end(); ++j)
					++code_counts[*j];
			}
			const int n_ramps = (int) mSelectedRamps.size();
			float top = ContentTop(b);

			// Re-clamp every Draw() against the CURRENT row count, not just when the
			// wheel moves it - a filter/search/sort change can shrink the list out
			// from under an existing scroll position (e.g. scrolled deep into "All
			// Airlines" with recommendations on, then narrowing the search query
			// away most of it) just as easily as a resize can. The preview cards are
			// part of this same scrolled content, so their block counts toward the
			// height being scrolled through.
			float cards_h    = mPreviewObjVpaths.empty() ? 0.0f : CardsBlockHeight(b);
			float content_h  = cards_h + rows.size() * row_h;
			float visible_h  = top - (float) b[1];
			float max_scroll = (content_h > visible_h) ? (content_h - visible_h) : 0.0f;
			if (mScrollOffset < 0)          mScrollOffset = 0;
			if (mScrollOffset > max_scroll) mScrollOffset = max_scroll;

			for (size_t vi = 0; vi < rows.size(); ++vi)
			{
				const WED_LiveryDisplayRow & row = rows[vi];
				// Rows start BELOW the card block - see RowForY(), which must stay in
				// sync with this formula.
				float row_top = top + mScrollOffset - cards_h - vi * row_h;
				float row_bot = row_top - row_h;
				if (row_bot < b[1]) break;						// scrolled/clipped past the bottom - nothing lower matters either
				if (row_top > top) continue;						// scrolled past the top - keep going, a later row may still be visible

				if (row.kind == wed_Row_Gap)
					continue;

				if (row.kind == wed_Row_Divider)
				{
					// Near-full-width, centered.
					float dw = (b[2] - b[0]) * 0.85f;
					float dcx = ((float) b[0] + (float) b[2]) * 0.5f;
					float dy = (row_top + row_bot) * 0.5f;
					state->SetState(0,0,0,0,0,0,0);
					glColor4f(0.40f, 0.40f, 0.40f, 1.0f);
					glBegin(GL_LINES);
						glVertex2f(dcx - dw*0.5f, dy);
						glVertex2f(dcx + dw*0.5f, dy);
					glEnd();
					continue;
				}

				if (row.kind == wed_Row_Header)
				{
					GUI_FontDraw(state, font_UI_Basic, row_col, b[0] + pad, row_bot + (row_h - line_h) * 0.5f, row.header_text.c_str());
					// Every section except the last ("All Airlines") is some flavor of
					// recommendation (manual pin, direct hit, same country, or popular fleet) -
					// star all of them, same as the old two-tier layout starred "Recommended".
					if (row.header_text != "All Airlines")
					{
						float hw = GUI_MeasureRange(font_UI_Basic, row.header_text.c_str(), row.header_text.c_str() + row.header_text.size());
						glColor4f(1.0f, 0.85f, 0.2f, 1.0f);
						DrawStar(b[0] + pad + hw + line_h * 0.45f, (row_top + row_bot) * 0.5f, line_h * 0.4f, line_h * 0.17f);
					}
					continue;
				}

				// wed_Row_Airline - indented a bit further right than the section
				// headers/dividers above, so the checkbox rows visually read as
				// nested "under" their header rather than lining up flush with them.
				// Tri-state. A dash rather than a tick or a question mark: it is what
				// macOS, Windows and HTML's own indeterminate checkbox all use, so it
				// reads as "mixed" without needing a legend. With one ramp selected
				// n_ramps is 1, so "mixed" can never occur and this collapses to the
				// ordinary two-state box.
				map<string,int>::const_iterator cc = code_counts.find(row.icao);
				const int  n_with     = (cc == code_counts.end()) ? 0 : cc->second;
				const bool is_checked = (n_with == n_ramps);
				const bool is_mixed   = (n_with > 0 && n_with < n_ramps);

				const float kRowIndent = 14;
				float box  = line_h * 0.7f;
				float bx0  = b[0] + pad + kRowIndent;
				float by0  = row_bot + (row_h - box) * 0.5f;

				state->SetState(0,0,0,0,0,0,0);

				if ((int) vi == mHoverRow)
				{
					glColor4f(1.0f, 1.0f, 1.0f, 1.0f);
					glBegin(GL_LINE_LOOP);
						glVertex2f(b[0] + 1,       row_bot + 1);
						glVertex2f(b[2] - 1,       row_bot + 1);
						glVertex2f(b[2] - 1,       row_top - 1);
						glVertex2f(b[0] + 1,       row_top - 1);
					glEnd();
				}

				glColor4f(row_col[0], row_col[1], row_col[2], 1.0f);
				glBegin(GL_LINE_LOOP);
					glVertex2f(bx0,       by0);
					glVertex2f(bx0 + box, by0);
					glVertex2f(bx0 + box, by0 + box);
					glVertex2f(bx0,       by0 + box);
				glEnd();

				if (is_checked)
				{
					glBegin(GL_QUADS);
						glVertex2f(bx0 + 2,       by0 + 2);
						glVertex2f(bx0 + box - 2, by0 + 2);
						glVertex2f(bx0 + box - 2, by0 + box - 2);
						glVertex2f(bx0 + 2,       by0 + box - 2);
					glEnd();
				}
				else if (is_mixed)
				{
					float mid = by0 + box * 0.5f;
					float th  = (std::max)(1.0f, box * 0.14f);
					glBegin(GL_QUADS);
						glVertex2f(bx0 + 3,       mid - th);
						glVertex2f(bx0 + box - 3, mid - th);
						glVertex2f(bx0 + box - 3, mid + th);
						glVertex2f(bx0 + 3,       mid + th);
					glEnd();
				}

				string disp_code = row.icao;
				for (string::iterator c = disp_code.begin(); c != disp_code.end(); ++c)
					*c = (char) toupper((unsigned char) *c);

				string label = row.name.empty() ? disp_code : (disp_code + " - " + row.name);
				GUI_FontDraw(state, font_UI_Basic, row_col, bx0 + box + pad, row_bot + (row_h - line_h) * 0.5f, label.c_str());
			}

			if (rows.empty())
			{
				GUI_FontDraw(state, font_UI_Basic, row_col, b[0] + pad, top - line_h, "No placeholder liveries tagged for this operation type yet.");
			}

			// Thin scrollbar affordance, drawn only once there's actually more to see
			// than fits - a plain proportional thumb (not draggable; mouse wheel is
			// the only scroll input this pane supports, same as everywhere else in
			// this custom-drawn class).
			if (max_scroll > 0.0f)
			{
				const float track_w = 4;
				float track_x1 = (float) b[2] - 2;
				float track_x0 = track_x1 - track_w;

				float thumb_h = visible_h * (visible_h / content_h);
				if (thumb_h < 20.0f) thumb_h = 20.0f;
				if (thumb_h > visible_h) thumb_h = visible_h;

				float scroll_frac = mScrollOffset / max_scroll;
				float thumb_top = top - scroll_frac * (visible_h - thumb_h);
				float thumb_bot = thumb_top - thumb_h;

				state->SetState(0,0,0,0,0,0,0);
				glColor4f(0.15f, 0.15f, 0.15f, 1.0f);
				glBegin(GL_QUADS);
					glVertex2f(track_x0, (float) b[1]); glVertex2f(track_x1, (float) b[1]);
					glVertex2f(track_x1, top);          glVertex2f(track_x0, top);
				glEnd();

				glColor4f(0.55f, 0.55f, 0.55f, 1.0f);
				glBegin(GL_QUADS);
					glVertex2f(track_x0, thumb_bot); glVertex2f(track_x1, thumb_bot);
					glVertex2f(track_x1, thumb_top); glVertex2f(track_x0, thumb_top);
				glEnd();
			}
		}
	}

	// --- 75%-black mask when this tab is active but nothing eligible is selected ---
	if (mSelectedRamps.empty())
	{
		state->SetState(0,0,0,0,1,0,0);
		glColor4f(0.0f, 0.0f, 0.0f, 0.75f);
		glBegin(GL_QUADS);
			glVertex2f((float) b[0], (float) b[1]);
			glVertex2f((float) b[2], (float) b[1]);
			glVertex2f((float) b[2], (float) b[3]);
			glVertex2f((float) b[0], (float) b[3]);
		glEnd();

		// Warning text ABOVE the mask (drawn after it, so it isn't itself
		// dimmed by it) - the tab is clickable now with nothing selected
		// (see RebuildSelection()), so this is what actually communicates
		// "nothing to edit yet", not a locked/unclickable tab button.
		float warn_col[4] = { 1.0f, 0.85f, 0.2f, 1.0f };
		float cx = (float) (b[0] + b[2]) * 0.5f;
		float line_h = GUI_GetLineHeight(font_UI_Basic);
		const char * msg = "Select a ramp start to edit its liveries";
		float msg_w = GUI_MeasureRange(font_UI_Basic, msg, msg + strlen(msg));
		GUI_FontDraw(state, font_UI_Basic, warn_col, cx - msg_w * 0.5f, (float) (b[1] + b[3]) * 0.5f - line_h * 0.4f, msg);
	}

	// --- country flag banner, drawn last so its enlarged, overlapping
	// footprint paints over the rows below it rather than being painted
	// under them ---
	if (mFlagTexId != 0)
	{
		float strip_top, strip_bot;
		AirportInfoYRange(b, strip_top, strip_bot);
		float bx0, by0, banner_w, banner_h;
		FlagBannerRect(b, strip_top, strip_bot, bx0, by0, banner_w, banner_h);

		state->SetState(0,1,0,0,1,0,0);		// blend on - the composite has a genuinely transparent background now
		glColor4f(1,1,1,1);
		state->BindTex((int) mFlagTexId, 0);
		glBegin(GL_QUADS);
			// Texture row 0 (the composited buffer's top row - see
			// EnsureFlagTexture()) lands at texture-space t=0, so t=0
			// belongs at this pane's screen-top vertex (Y increases
			// upward here, same as every other quad in this file).
			glTexCoord2f(0, 1); glVertex2f(bx0,            by0);
			glTexCoord2f(1, 1); glVertex2f(bx0 + banner_w, by0);
			glTexCoord2f(1, 0); glVertex2f(bx0 + banner_w, by0 + banner_h);
			glTexCoord2f(0, 0); glVertex2f(bx0,            by0 + banner_h);
		glEnd();
		state->SetState(0,0,0,0,0,0,0);
	}

	glPopAttrib();		// restore GL_CULL_FACE (and anything else in GL_ENABLE_BIT) for whatever draws next
}
