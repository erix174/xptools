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
#include "WED_LibraryMgr.h"

#include "WED_RampPosition.h"
#include "WED_Airport.h"
#include "WED_Archive.h"
#include "WED_Persistent.h"		// pulls in the StartCommand(x) convenience macro
#include "WED_Messages.h"
#include "WED_ToolUtils.h"		// WED_GetSelect, WED_GetParentAirport
#include "WED_EnumSystem.h"		// ramp_operation_*, width_A..width_F
#include "WED_LiveryIndex.h"		// WED_LiveryIndexDefaultPath(), WED_LiveryInRange()
#include "GISUtils.h"
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
	// These name the OPERATION at the stand, not the aircraft - that is what the
	// Equipment Type list is for. A PC-12 flying a scheduled service is Passenger;
	// a 737 BBJ is Private/BizJet. UI ONLY: apt.dat still writes none /
	// general_aviation / airline / cargo / military, and always will.
	//   Private/BizJet <- general_aviation      Passenger <- airline
	// The short forms are drawn when a chip is too narrow for the full label,
	// which at a fifth of the pane width "Private/BizJet" often is.
	const char * kFilterLabels[5]      = { "None", "Private/BizJet", "Passenger", "Cargo", "Military" };
	const char * kFilterLabelsShort[5] = { "None", "Private",        "Passenger", "Cargo", "Military" };
	const int kWidthOrder[6]       = { width_A, width_B, width_C, width_D, width_E, width_F };
	const char * kWidthLabels[6]   = { "A", "B", "C", "D", "E", "F" };

	// Tab titles, not tab indices. The positions these panes were added at used to
	// be hardcoded here (0 and 5), which is silently wrong the moment anyone
	// inserts a tab ahead of them - nothing catches it, the auto-switch just lands
	// on the wrong pane. GUI_TabPane::GetTabForPane()/GetTabForTitle() resolve them
	// at the point of use instead. See WED_DocumentWindow.cpp's AddPane() calls.
	const char * kSelectionTabTitle = "Selection";

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
	// wed_Row_Note is an explanatory line under a section header that came out
	// empty - "no operator matches ...". It exists so an empty section can still
	// SAY something. Previously a section with no rows dropped its header too, so
	// "not researched", "filtered out by the search box" and "the airline
	// directory failed to load" were all indistinguishable from each other and
	// from the section simply not existing.
	// What AppendAirlineSection() did, so the caller can tell an empty tier apart
	// from a missing one.
	enum WED_SectionResult {
		sect_HasRows,		// rows were appended
		sect_NoData,		// nothing to show in this tier at all - emitted nothing
		sect_AllShownAbove,	// every code here already appeared in a higher tier - emitted nothing
		sect_FilteredOut	// had rows, the search box removed them all - emitted a header + note
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

	// Same idea as FindPlaceholderName(), but checked first against the OPERATOR
	// records of livery_index.txt (which is what actually knows most
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

	// XPZZ, the unpainted airliner, sinks to the bottom of every list whichever
	// way the sort arrow points. It is a fallback, not a choice - offering a
	// white 757 above a real operator is the picker answering the wrong
	// question first, and at a busy airport it would be the first thing seen.
	// The rows carry the code lowercased (see r.icao above).
	bool NotTheUnpaintedAirliner(const WED_LiveryDisplayRow & r)
	{
		return r.icao != "xpzz";
	}

	void SortAirlineRows(vector<WED_LiveryDisplayRow> & rows, bool descending)
	{
		std::sort(rows.begin(), rows.end(), CompareRowsByIcao);
		if (descending) std::reverse(rows.begin(), rows.end());
		// stable_partition, so both groups keep the order the sort just gave them
		std::stable_partition(rows.begin(), rows.end(), NotTheUnpaintedAirliner);
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
	WED_SectionResult AppendAirlineSection(const string & label, const vector<string> & codes_upper,
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

		// Snapshot BEFORE the search filter runs. The difference between "this tier
		// had nothing" and "this tier had things and you filtered them away" is the
		// whole point of the return value, and it is only visible here.
		const bool had_rows_before_search = !section.empty();

		if (!query_lower.empty())
		{
			vector<WED_LiveryDisplayRow> filtered;
			for (size_t i = 0; i < section.size(); ++i)
				if (ContainsCaseInsensitive(section[i].icao, query_lower) || ContainsCaseInsensitive(section[i].name, query_lower))
					filtered.push_back(section[i]);
			section.swap(filtered);
		}

		if (section.empty())
		{
			// Deliberately silent when the tier was empty to begin with, or when
			// everything in it already appeared higher up: an empty "Popular
			// Airlines" header on every airport whose airlines were all already
			// listed under "Recommended" would be pure noise. Only a section the
			// SEARCH emptied gets to explain itself, because there the user did
			// something and deserves to know it had an effect here.
			if (!had_rows_before_search)
				return codes_upper.empty() ? sect_NoData : sect_AllShownAbove;

			if (leading_divider)
			{
				WED_LiveryDisplayRow gap;	gap.kind = wed_Row_Gap;		out.push_back(gap);
				WED_LiveryDisplayRow div;	div.kind = wed_Row_Divider;	out.push_back(div);
			}
			WED_LiveryDisplayRow header;
			header.kind = wed_Row_Header;
			header.header_text = label;
			out.push_back(header);

			WED_LiveryDisplayRow note;
			note.kind = wed_Row_Note;
			note.header_text = "nothing here matches \"" + query_lower + "\"";
			out.push_back(note);
			return sect_FilteredOut;
		}

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
		return sect_HasRows;
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

		// A CHECKED OPERATOR IS KEPT, NOT PROMOTED. It used to be hoisted to the
		// front of this tier, so ticking a card made it jump to the top of its own
		// section - the list rearranging itself under the cursor on every click,
		// which is the same complaint the "Selected" section already answers by
		// mirroring rather than moving.
		//
		// The reason the special case existed is still real: this tier shows ten of
		// thirty and a reshuffle could drop a checked operator out of view
		// entirely. So a checked one is admitted past the slot limit, but only ever
		// at its own place in the shuffle - the order never changes, it only gets
		// longer.
		vector<string> result;
		set<string>    result_set;
		for (size_t i = 0; i < shuffle_order.size(); ++i)
		{
			string lower = shuffle_order[i];
			for (string::iterator c = lower.begin(); c != lower.end(); ++c)
				*c = (char) tolower((unsigned char) *c);

			bool keep = checked_lower.count(lower) != 0 || (int) result.size() < kDisplaySlots;
			if (keep && !result_set.count(shuffle_order[i]))
			{
				result.push_back(shuffle_order[i]);
				result_set.insert(shuffle_order[i]);
			}
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
							const WED_AirlineDirectory & directory,
							const vector<pair<string,string> > & all_operators,
							vector<WED_LiveryDisplayRow> & out)
	{
		out.clear();
		if (ramp_op_enum == ramp_operation_None)
			return;		// "No static aircraft will spawn at this spot" - see Draw()

		string query_lower = search_query;
		for (string::iterator c = query_lower.begin(); c != query_lower.end(); ++c)
			*c = (char) tolower((unsigned char) *c);

		int category = RampOpToLiveryCategory(ramp_op_enum);

		// EVERY OPERATOR THE LIVERY INDEX HAS, not the hand-written placeholder list
		// in WED_LiveryData.h. That list is about 25 codes chosen years ago; the
		// shipped index carries 152, and none of the two sets' overlap survives
		// DropCardless - which is why this section came out empty and could not be
		// opened. The caller passes what it found, already reduced to operators with
		// a livery that fits this stand.
		//
		// No op_type filter here any more either: the placeholder list carried one
		// per row and the index does not, and inventing one from the code would be
		// guessing. The tiers above have never been filtered by it - see this
		// function's own doc comment - so this now matches them.
		vector<WED_LiveryDisplayRow> all_rows;
		for (size_t i = 0; i < all_operators.size(); ++i)
		{
			WED_LiveryDisplayRow r;
			r.kind = wed_Row_Airline;
			r.icao = all_operators[i].first;			// lowercase, as rows carry it
			r.name = all_operators[i].second;
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

		any |= sect_HasRows == AppendAirlineSection("Manual Recommendations", manual_codes_upper, directory,
										sort_descending, query_lower, seen, any, out);

		any |= sect_HasRows == AppendAirlineSection("Recommended", direct_hit_codes_upper, directory,
										sort_descending, query_lower, seen, any, out);

		vector<string> popular_codes = GetPopularAirlinesCodes(directory, airport_country_ioc, airport_icao,
																	checked_lower, popular_cache);
		any |= sect_HasRows == AppendAirlineSection("Popular Airlines", popular_codes, directory,
										sort_descending, query_lower, seen, any, out, /*preserve_order=*/true);

		vector<string> same_country_codes;
		if (!airport_country_ioc.empty())
		{
			vector<const WED_AirlineDirectoryEntry *> matches;
			directory.GetByCountry(airport_country_ioc, matches);
			for (size_t i = 0; i < matches.size(); ++i)
				same_country_codes.push_back(matches[i]->code);
		}
		any |= sect_HasRows == AppendAirlineSection("Same Country", same_country_codes, directory,
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


	// ---- two post-passes over the finished row list ----
	// Both run AFTER the tiers are assembled rather than inside them, so the tier
	// logic (dedup by `seen`, search filtering, the per-airport shuffle) stays one
	// thing with one reason to change.

	// MIRRORS every ticked operator into a section at the very top, and LEAVES THE
	// ORIGINAL WHERE IT WAS. Moving it was the obvious implementation and a bad
	// interaction: clicking a card made it vanish from under the cursor and
	// reappear at the top of the list, so the row you were working through
	// reshuffled itself on every tick and you lost your place. The copy is a
	// shortcut to what is already chosen, not a new home for it - untick, and the
	// copy disappears while the card you actually clicked is still where you left
	// it.
	//
	// Both copies are the same operator and both read their state from the same
	// icao key, so ticking, locking or opening the tray on one shows on the other.
	// That is honest rather than confusing: there is one operator, shown twice.
	void PinSelected(vector<WED_LiveryDisplayRow> & rows, const set<string> & selected)
	{
		if (selected.empty()) return;

		vector<WED_LiveryDisplayRow> picked;
		for (size_t i = 0; i < rows.size(); ++i)
		{
			if (rows[i].kind != wed_Row_Airline || !selected.count(rows[i].icao)) continue;
			bool dup = false;
			for (size_t j = 0; j < picked.size(); ++j)
				if (picked[j].icao == rows[i].icao) { dup = true; break; }
			if (!dup) picked.push_back(rows[i]);
		}
		if (picked.empty()) return;

		vector<WED_LiveryDisplayRow> rest = rows;

		vector<WED_LiveryDisplayRow> out;
		WED_LiveryDisplayRow h; h.kind = wed_Row_Header; h.header_text = "Selected";
		out.push_back(h);
		WED_LiveryDisplayRow g; g.kind = wed_Row_Gap;
		out.push_back(g);
		out.insert(out.end(), picked.begin(), picked.end());
		out.push_back(g);
		WED_LiveryDisplayRow d; d.kind = wed_Row_Divider;
		out.push_back(d);
		out.insert(out.end(), rest.begin(), rest.end());
		rows.swap(out);
	}

	// Drops every airline row that has no card behind it. WITHOUT THIS THE GRID GETS
	// HOLES: the layout allocates a slot for every airline row, and Draw() then
	// skipped the ones with nothing modelled, leaving the slot empty and pushing the
	// rest of the line sideways. Layout and content have to walk the same list.
	void DropCardless(vector<WED_LiveryDisplayRow> & rows, const set<string> & have_cards)
	{
		vector<WED_LiveryDisplayRow> out;
		for (size_t i = 0; i < rows.size(); ++i)
			if (rows[i].kind != wed_Row_Airline || have_cards.count(rows[i].icao))
				out.push_back(rows[i]);
		rows.swap(out);
	}

	// A header whose rows were all dropped is worse than no section: it reads as
	// "this tier is broken" when the truth is "everything here was already shown
	// above, or none of it is modelled". AppendAirlineSection decides whether to
	// emit a header BEFORE DropCardless has run, so it cannot know - this does.
	//
	// A section that deliberately kept a note ("nothing here matches ...") is left
	// alone: that one is saying something.
	void PruneEmptySections(vector<WED_LiveryDisplayRow> & rows)
	{
		vector<bool> drop(rows.size(), false);
		for (size_t i = 0; i < rows.size(); ++i)
		{
			if (rows[i].kind != wed_Row_Header) continue;
			bool has_content = false;
			size_t j = i + 1;
			for (; j < rows.size() && rows[j].kind != wed_Row_Header; ++j)
				if (rows[j].kind == wed_Row_Airline || rows[j].kind == wed_Row_Note)
					{ has_content = true; break; }
			if (has_content) continue;
			// the header plus the structural padding that came with it
			for (size_t k = i; k < rows.size() && (k == i || rows[k].kind == wed_Row_Gap ||
												   rows[k].kind == wed_Row_Divider); ++k)
				drop[k] = true;
		}
		vector<WED_LiveryDisplayRow> out;
		for (size_t i = 0; i < rows.size(); ++i)
			if (!drop[i]) out.push_back(rows[i]);
		rows.swap(out);
	}

	// Drops the airline rows of any collapsed section, keeping its header. Only the
	// header is left behind, so the section can be reopened - and "All Airlines"
	// starts collapsed, because it is the tier with no filter behind it and
	// rendering its hundreds of cards unasked is the one way this pane can stall.
	void ApplyCollapse(vector<WED_LiveryDisplayRow> & rows, const set<string> & collapsed)
	{
		if (collapsed.empty()) return;

		vector<WED_LiveryDisplayRow> out;
		bool skipping = false;
		for (size_t i = 0; i < rows.size(); ++i)
		{
			if (rows[i].kind == wed_Row_Header)
				skipping = collapsed.count(rows[i].header_text) != 0;
			if (skipping && rows[i].kind != wed_Row_Header) continue;
			// A collapsed header carries the count it is holding back, so the reader
			// can tell "closed, 37 operators inside" from "this tier is empty". With
			// no number those two look identical, which is exactly the confusion the
			// empty-section pruning above exists to prevent.
			if (skipping && rows[i].kind == wed_Row_Header)
			{
				int n = 0;
				for (size_t j = i + 1; j < rows.size() && rows[j].kind != wed_Row_Header; ++j)
					if (rows[j].kind == wed_Row_Airline) ++n;
				out.push_back(rows[i]);
				out.back().hidden_count = n;
				continue;
			}
			out.push_back(rows[i]);
		}
		rows.swap(out);
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
							map<string, vector<string> > & popular_cache,
							const vector<pair<string,string> > & all_operators)
	{
		vector<WED_LiveryDisplayRow> rows;
		if (!primary_ramp) return rows;

		if (!directory.IsLoaded() && !directory.LoadFailed())
		{
			// ONE FILE, NO FALLBACK. The operators come out of livery_index.txt, the
			// same file the liveries and the sim's rule come from, so the two
			// readers cannot disagree about who an operator is. There is no
			// WED-side directory any more: a second source that only ever kicks in
			// when the first is missing is exactly the kind of path that rots
			// between releases, and nobody notices which file they are looking at.
			const string dir_path = WED_LiveryIndexDefaultPath();
			if (dir_path.empty() || !directory.EnsureLoaded(dir_path) || directory.Count() == 0)
			{
				// Say so, once. Without the directory the tab still works, but
				// airlines render as bare ICAO codes and the "Popular Airlines" and
				// "Same Country" sections vanish outright - a degraded result that
				// looks exactly like a normal, short list. Silently handing that to
				// an author is worse than one alert they dismiss.
				//
				// Once per SESSION, not per failure: this sits on the path Draw()
				// takes, and the alert is modal, so it would otherwise reopen the
				// instant it was dismissed.
				static bool s_warned = false;
				if (!s_warned)
				{
					s_warned = true;
					string msg = "WED could not load its airline database - ";
					msg += WedDataFileErrorText(directory.LoadError());
					msg += ":\n\n  ";
					msg += dir_path;
					msg += "\n\nThe Liveries tab still works, but airlines will show as "
						   "codes without names, and the region-based recommendations "
						   "will be missing.\n\nReinstalling WED restores the file.";
					DoUserAlert(msg.c_str());
				}
			}
		}

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
							search_query, directory, all_operators, rows);
		return rows;
	}

	// Truncates `text` with a trailing ellipsis so it fits within max_w pixels.
	// Single line, unlike WrapText below - for one-line explanatory notes, where
	// the property panel can be dragged narrow enough to push text outside the
	// border. Backs off a character at a time; the ellipsis is always the last
	// thing inside the limit.
	string ElideToWidth(int font, const string & text, float max_w)
	{
		if (max_w <= 0) return string();

		float w = GUI_MeasureRange(font, text.c_str(), text.c_str() + text.size());
		if (w <= max_w) return text;

		string out(text);
		while (!out.empty())
		{
			out.erase(out.size() - 1);
			string candidate = out + "...";
			if (GUI_MeasureRange(font, candidate.c_str(), candidate.c_str() + candidate.size()) <= max_w)
				return candidate;
		}
		return string();
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


// Aspect of a card's image area. MUST match kThumbW/kThumbH in
// WED_LiveryThumbnailCache.cpp - that's the shape the cached texture is rendered at,
// and the card is just a quad displaying it, so a mismatch would letterbox or stretch
// it. Used by both CardHeight() and Draw()'s image quad, which must also agree with
// each other (they once didn't - see CardHeight()'s comment).
static const float kCardImageAspect = 32.0f / 9.0f;

// The grab strip along a card's bottom edge: the tray's handle, and the only
// place the disclosure arrow can live without sitting on the operator's name.
// Declared here because CardHeight() has to reserve it.
static const float kTrayTabH    = 18.0f;	// height of the caption line's clickable gutter
static const float kTrayGutterW = 22.0f;	// and its width

// Cards are laid out as a grid of "trading cards": kCardCols per row, with a fixed
// gap on every side and between them. The gap is what actually makes each card read
// as its own object rather than one continuous strip, so it stays a fixed pixel
// value rather than scaling with the pane.
static const int   kCardCols = 2;
static const float kCardGap  = 6.0f;

WED_LiveryPane::WED_LiveryPane(
						IResolver *		resolver,
						WED_Archive *	archive,
						GUI_TabPane *	host_tabs) :
	// The host tab pane is itself a commander (GUI_TabPane.h:35), so it is the
	// natural parent: focus flows window -> tab pane -> this pane -> mSearchField.
	GUI_Commander(host_tabs),
	mResolver(resolver),
	mArchive(archive),
	mHostTabs(host_tabs),
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
	mContentDragStartY(-1),
	mContentDragStartX(-1),		// declared later in the header (after mCachedStatusLines) -
	mContentDragStartOffset(0),	// listed here anyway so all the "simple scalar" inits stay together
	mCycleShow(0),
	mRowsDirty(true),
	mCardsByType(false),
	mCoverageLineCount(3),
	mLastAnimClock(0),
	mTrayHoverIdx(-1),
	mHoverX(0),
	mHoverY(0),
	mCycleAccum(0.0f),
	mTrayOpen(0.0f),
	mTrayClosingOpen(0.0f),
	mDragWeightBar(-1),
	mHoverWeightBar(-1),
	mHoverWeightButton(false),
	mTrackWeightButton(false),
	mCoverageDirty(true)
{
	// Zeroed rather than left indeterminate: Draw() reads mCoverage before the
	// first RecomputeCoverage() can run if a frame lands before any selection
	// change, and index_ready == false makes that frame say "index not loaded"
	// instead of printing garbage counts.
	// Field by field, NOT memset. Coverage carries std::strings now
	// (index_version, sole_operator), and zeroing the bytes of a std::string is
	// undefined behaviour - it overwrites the object's own bookkeeping, so the
	// first assignment or destruction afterwards is working from a state the
	// implementation never produced. It happened to survive on MSVC's small
	// string; that is luck, not a guarantee.
	mCoverage.index_ready      = false;
	mCoverage.weighted         = false;
	mCoverage.p_occupied       = 0.0f;
	mCoverage.empty_cause      = Coverage::empty_None;
	mCoverage.stands           = 0;
	mCoverage.stands_empty     = 0;
	mCoverage.classes_in_range = 0;
	// "All Airlines" is the only tier with no filter behind it, so it can be
	// hundreds of cards. It starts closed: rendering those unasked is the one way
	// this pane can stall, and a user who scrolls to the bottom should not be the
	// one who discovers that.
	mCollapsedSections.insert("All Airlines");

	mCoverage.classes_filled   = 0;
	mCoverage.airlines_listed  = 0;
	mCoverage.airlines_eligible= 0;
	mCoverage.lo_class         = 'A';
	mCoverage.hi_class         = 'F';

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

	// Cards are built from the livery index on the first Draw and rebuilt
	// whenever the selection, the operators or the weights change - see
	// RebuildAirlineCards(). There is nothing to seed here.
}

WED_LiveryPane::~WED_LiveryPane()
{
	// Release through Hide(), which is the context-safe path: it is called on tab
	// switch, while the window's GL context is still current and guaranteed valid.
	// Deleting textures directly from a destructor runs at document-window
	// teardown, where the context may already be gone - a silent no-op on every
	// driver we ship against, but undefined by the spec, and free to avoid.
	//
	// Hide() clears both containers, so anything it released cannot be released
	// twice by the sweep below; that sweep only exists for a texture created
	// after the last Hide().
	Hide();

	if (mFlagTexId != 0)
	{
		glDeleteTextures(1, &mFlagTexId);
		mFlagTexId = 0;
	}
	for (map<string, WED_LiveryThumbnail>::iterator i = mRawFlagTex.begin(); i != mRawFlagTex.end(); ++i)
		if (i->second.tex != 0)
			glDeleteTextures(1, &i->second.tex);
	mRawFlagTex.clear();
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
	AbortWeightDrag();		// same reason - see AbortSizeDrag()'s caller comment above

	// Nothing may keep animating behind a hidden tab: the timer would go on
	// rebuilding the row list and re-laying it out every frame, forever, for a
	// pane nobody is looking at.
	mCycleAirline.clear();  mCycleShow = 0;  mCycleAccum = 0.0f;
	mTrayClosing.clear();   mTrayClosingOpen = 0.0f;
	mLastAnimClock = 0;

	// Everything else the mouse was in the middle of, too. Only the two drags
	// above own an archive command, so only they can strand it - but the rest of
	// the gesture state is just as live, and it is what made the pane follow the
	// cursor with no button held after switching away mid-drag and back: the
	// tab that gets hidden never receives the MouseUp that would have cleared
	// it, so the pane came back still believing a gesture was in progress.
	mContentDragStartY    = -1;
	mContentDragStartX    = -1;
	mTrackRow             = -1;
	mTrackFilterChip      = -1;
	mTrackSortButton      = false;
	mTrackRecommendButton = false;
	mTrackClearButton     = false;
	mTrackWeightButton    = false;

	// Hover highlights too, or the pane repaints with a lit-up control under a
	// cursor that is somewhere else entirely.
	mHoverRow             = -1;
	mHoverFilterChip      = -1;
	mHoverSliderHandle    = -1;
	mHoverWeightBar       = -1;
	mHoverWeightButton    = false;
	mHoverSortButton      = false;
	mHoverRecommendButton = false;
	mHoverClearButton     = false;

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
	// &v[0] on an empty vector is undefined - and the decoder sizes its output
	// from the PNG's own dimensions, which come from the file.
	if (composited.empty()) return;

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
	if (WED_LoadPngTopDownARGB(path, source_argb, src_w, src_h) &&
		!source_argb.empty() && src_w > 0 && src_h > 0)
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
		SetRowsDirty();			// the query filters the list
		Refresh();
	}
}

void	WED_LiveryPane::RebuildSelection(void)
{
	vector<WED_RampPosition *> old_selection = mSelectedRamps;

	mSelectedRamps.clear();
	ISelection * sel = WED_GetSelect(mResolver);
	if (sel) sel->IterateSelectionOr(CollectRamps, &mSelectedRamps);

	// Unconditional, not gated on (mSelectedRamps != old_selection): the same set
	// of ramps can come back with different airlines or a different size range
	// after an undo, a property-grid edit, or a change made on another tab.
	mCoverageDirty = true;

	// A genuinely different ramp selection (different ramp, or a different airport
	// entirely) means the checklist content just changed out from under whatever
	// scroll position was left over from before - snap back to the top rather than
	// risk leaving the user scrolled past a short "Recommended"/"Manual" section (or
	// the whole list) for the new selection. An unrelated Draw() call caused by
	// something else entirely (e.g. just moving the mouse) leaves this alone.
	SetRowsDirty();		// a new selection is a new list, always

	if (mSelectedRamps != old_selection)
	{
		mScrollOffset = 0;

		// The lock, the open tray and the running slideshow are all statements
		// about the stand being edited, not about the document. Carrying them to a
		// different stand would dim a list the user has not touched yet, and the
		// lock in particular would arrive with no indication of where it came from.
		mLockedAirline.clear();
		mTrayAirline.clear();   mTrayOpen = 0.0f;
		mTrayClosing.clear();   mTrayClosingOpen = 0.0f;
		mCycleAirline.clear();  mCycleShow = 0;  mCycleAccum = 0.0f;
		mTrayHoverIdx = -1;
	}

	// No more SetPaneEnabled() lock - the tab stays clickable even with
	// nothing selected (the greyed-out mask + warning text in Draw() carries
	// that state instead). Only auto-navigate the user OFF this tab the
	// first time it goes empty while they're actually looking at it; once
	// they've manually clicked back in with nothing selected, leave them be
	// until selection is non-empty again.
	if (mSelectedRamps.empty())
	{
		// Deliberately NO auto-navigate away. Clicking a different ramp start
		// clears the old selection before setting the new one, so the selection
		// passes through empty on the way - and bouncing to the Selection tab at
		// that instant threw the author off this one every single time they
		// picked another stand, whether or not they had asked to be brought here.
		//
		// The empty state is already carried by Draw()'s greyed mask and its
		// warning text, which is what that mask is FOR. Leaving the tab up and
		// masked for a moment is the correct behaviour; navigating away from the
		// thing the author is working in is not.
		mLastAutoSwitchedInRamps.clear();		// selection's gone - a later re-selection counts as "new" again
	}
	else
	{

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
			int my_tab = mHostTabs->GetTabForPane(this);
			if (my_tab >= 0) mHostTabs->SetTab(my_tab);
			mLastAutoSwitchedInRamps = mSelectedRamps;
		}
	}
}

// ---------------------------------------------------------------------------------------------
// preview cards
// ---------------------------------------------------------------------------------------------

// One card per livery that can actually spawn on the selected stand.
//
// The question asked here is deliberately the same one RecomputeCoverage() asks
// - which classes can spawn, and which of the listed operators has a livery at
// each - so the cards and the sentence above them are answers to one query
// rather than two that can disagree. They used to disagree spectacularly: a
// stand reading "this stand parks nothing" had four aircraft pictured directly
// underneath it, because the cards were the first four objects in the library
// and had nothing to do with the ramp at all.
//
// Obsolete liveries need no filtering here. R25 keeps them out of the index's
// lookup tables entirely, so a query cannot return one.

static void	CardFlags(const vector<WED_LiveryDisplayRow> & rows, vector<bool> & out);
static void	RowIcaos (const vector<WED_LiveryDisplayRow> & rows, vector<string> & out);

void	WED_LiveryPane::EnsureRows(void)
{
	if (!mRowsDirty) return;
	mRowsDirty = false;

	mRows.clear();
	mRowIsCard.clear();
	mRowIcaos.clear();
	if (mSelectedRamps.empty()) return;

	mRows = BuildCurrentDisplayRows(mSelectedRamps[0], mSortDescending,
				gShowLiveryRecommendation != 0, mAirportDb, mCurrentAirportIcao,
				mSearchQuery, mAirlineDirectory, mPopularAirlinesShuffleCache, AllOperators());
	{ set<string> have; CardKeys(have); DropCardless(mRows, have); }
	PruneEmptySections(mRows);
	PinSelected(mRows, ParseCodes(mSelectedRamps[0]->GetAirlines()));
	ApplyCollapse(mRows, mCollapsedSections);

	CardFlags(mRows, mRowIsCard);
	RowIcaos(mRows, mRowIcaos);
}



// MAY THIS LIVERY APPEAR AT THIS STAND. Three answers, by operation class:
//
//   General aviation  - anywhere. A private turboprop's range is short and its
//                       "hub" is wherever its owner lives; measuring either would
//                       filter out exactly the aircraft that turn up at every
//                       small field on earth. No range rule.
//   Military and Gov  - ONLY on home soil. An F-15 does not park at Beijing and a
//                       PLAAF 737 does not park at Denver, however far either can
//                       fly; the constraint is sovereignty, not fuel. Operator
//                       country (directory) must equal the airport's. Either
//                       unknown -> allowed, fail open.
//   Everything else   - the range rule, R26.
//
// The reason a livery was refused comes back so the readout can name it: only
// range refusals go into mRangeHidden, because that is the line's subject.
WED_LiveryPane::Allow	WED_LiveryPane::LiveryAllowedHere(const WED_LiveryIndexEntry & e, const Point2 & here) const
{
	const string & code = e.airline;

	WED_AirlineDirectoryEntry d;
	bool known = mAirlineDirectory.Lookup(code, d);

	bool is_ga  = code == "XPGA" || (known && d.op_class == WED_AirlineDirectoryEntry::op_GA);
	bool is_mil = code == "XPMI" || (known && (d.op_class == WED_AirlineDirectoryEntry::op_Military ||
											   d.op_class == WED_AirlineDirectoryEntry::op_Gov));

	if (is_ga) return allow_Yes;

	if (is_mil)
	{
		// The operator's country from the directory; failing that, the paint's -
		// an olive C172 registered ET- is Ethiopian whoever "XPMI" is. And this
		// one FAILS CLOSED: the rule is "home soil only", and an aircraft whose
		// home nobody can name has no home soil to be on. Fail-open here would put
		// four unmarked military 757s on every apron in the world, which is the
		// screenshot that prompted this. The airport's country unknown is the
		// other way round - then nothing about the stand is known and the
		// readout is already saying so, so do not also empty the list.
		// ANYWHERE BY DEFAULT; HOME SOIL ONLY WHEN MARKED. Most military equipment
		// is operated by many countries - an F-15 or a Seahawk at a foreign base is
		// unremarkable - so the default is global. The exception is equipment that
		// identifies one operator so specifically it has no business abroad (a
		// head-of-state 757, an air force's own-marked airliner), and those rows
		// carry HOME in their HUBS cell. Only then does the
		// country matter: the directory's, else the registration's.
		if (!e.home_only) return allow_Yes;
		string home = (known && !d.country.empty()) ? d.country : e.reg_country;
		if (home.empty() || mAirportCountry.empty()) return allow_Yes;
		return home == mAirportCountry ? allow_Yes : allow_ForeignMilitary;
	}

	return WED_LiveryInRange(e, here.y(), here.x()) ? allow_Yes : allow_OutOfRange;
}

// Which operation classes a ramp's operation type admits. Pseudo-codes are the
// index's own (see WED_LiveryIndex.h): XPGA general aviation - light aircraft
// and business jets alike, since the ramp draws no distinction between them -
// XPMI military, and XPZZ a generic unpainted airliner any commercial stand may
// use. A code this function does not know still gets an answer from its
// operator record's OP column, so retiring a pseudo-code costs nothing.
bool	WED_LiveryPane::OperatorMatchesRampOp(const string & code_uc, int ramp_op) const
{
	if (ramp_op == ramp_operation_None) return true;			// not stated - offer everything

	if (code_uc == "XPGA")                      return ramp_op == ramp_operation_GeneralAviation;
	if (code_uc == "XPMI")                      return ramp_op == ramp_operation_Military;
	if (code_uc == "XPZZ")                      return ramp_op == ramp_operation_Airline || ramp_op == ramp_operation_Cargo;

	WED_AirlineDirectoryEntry e;
	if (!mAirlineDirectory.Lookup(code_uc, e))
		return ramp_op == ramp_operation_Airline;			// unknown to the directory: assume airline, fail open

	switch (e.op_class)
	{
	case WED_AirlineDirectoryEntry::op_Pax:      return ramp_op == ramp_operation_Airline;
	case WED_AirlineDirectoryEntry::op_Cargo:    return ramp_op == ramp_operation_Cargo;
	case WED_AirlineDirectoryEntry::op_GA:       return ramp_op == ramp_operation_GeneralAviation;
	case WED_AirlineDirectoryEntry::op_Military:
	case WED_AirlineDirectoryEntry::op_Gov:      return ramp_op == ramp_operation_Military;
	}
	return true;
}

void	WED_LiveryPane::RebuildAirlineCards(void)
{
	mAirlineCards.clear();

	if (mSelectedRamps.size() != 1) return;		// a mixed selection has no single answer to preview
	if (!mLiveryIndex.IsLoaded())   return;

	WED_RampPosition * ramp = mSelectedRamps[0];

	// Which classes this stand can draw. Weights when it has them, otherwise the
	// size range - R17's two states, and the same branch RecomputeCoverage takes.
	bool use_class[6] = { false, false, false, false, false, false };
	int  w[6];
	if (ramp->GetClassWeights(w))
	{
		for (int k = 0; k < 6; ++k) use_class[k] = (w[k] > 0);
	}
	else
	{
		int lo = WidthEnumToIndex(ramp->GetWidthMin());
		int hi = WidthEnumToIndex(ramp->GetWidth());
		if (lo > hi) std::swap(lo, hi);
		for (int k = lo; k <= hi; ++k) use_class[k] = true;
	}

	// EVERY operator the directory knows, not just the ticked ones: a card has to
	// exist before it can be clicked, and clicking a card is now how an operator
	// gets ticked. Operators with nothing at this stand's classes get no card at
	// all, which is what keeps a section honest rather than showing an aircraft
	// that cannot park here.
	vector<string> codes;
	mLiveryIndex.GetAirlineCodes(codes);

	// Where this stand IS, for the range rule below. Same number the sim reads
	// off the 1300 row, so the two evaluate the identical predicate.
	Point2 here;
	ramp->GetLocation(gis_Geo, here);
	mRangeHidden.clear();

	// THE RAMP'S OPERATION TYPE IS A FILTER, not a label. A cargo stand offers
	// cargo operators; a GA stand the generic GA pseudo-code and the private and
	// corporate operators; a military stand the forces and the government fleets.
	// The class is the directory's fourth column. An operator the directory does
	// not know is treated as an airline - fail open, like the range rule - and
	// "None" leaves the list unfiltered, since it means the author has not said.
	const int ramp_op = ramp->GetRampOperationType();

	// GENERAL AVIATION HAS NO OPERATORS. "BTQ", "URF", "WML" are private owners
	// the generator had to give a code to; grouping by them puts one PC-12 on a
	// card of its own and eleven more, plus every Challenger and Cirrus, on the
	// generic XPGA card - a shape that says nothing about what parks here. A GA
	// stand groups by AIRCRAFT TYPE instead: one card per type, its registrations
	// and paints behind it. And nothing on a GA card is a picker - the sim draws
	// GA from the library by size, not from a 1301 list - so ticking and the lock
	// are switched off for them (see MouseUp), and the card is a preview only.
	const bool by_type = (ramp_op == ramp_operation_GeneralAviation || ramp_op == ramp_operation_Military);
	mCardsByType = by_type;

	for (size_t i = 0; i < codes.size(); ++i)
	{
		string code_uc = codes[i];
		for (size_t ci = 0; ci < code_uc.size(); ++ci)
			code_uc[ci] = (char) toupper((unsigned char) code_uc[ci]);

		if (!OperatorMatchesRampOp(code_uc, ramp_op)) continue;

		// BIGGEST CLASS FIRST, and reverse-alphabetically inside a class. Index 0 is
		// what the card shows at rest, so at rest a card shows the largest aircraft
		// that operator can park here - the most informative single frame, and a
		// stable one, since it does not move when an unrelated class is weighted out.
		// Per-operator grouping builds one card here; per-type grouping (GA) files
		// each livery under its type's card instead, so the card is looked up per
		// entry below rather than made once per operator.
		AirlineCard card;
		card.icao = code_uc;
		for (int k = 5; k >= 0; --k)
		{
			if (!use_class[k]) continue;

			vector<const WED_LiveryIndexEntry *> hits;
			mLiveryIndex.GetForAirlineAndClass(code_uc, (char) ('A' + k), hits);

			vector<pair<string, const WED_LiveryIndexEntry *> > sorted;
			for (size_t h = 0; h < hits.size(); ++h)
				sorted.push_back(make_pair(hits[h]->type, hits[h]));
			std::sort(sorted.begin(), sorted.end());
			std::reverse(sorted.begin(), sorted.end());

			for (size_t h = 0; h < sorted.size(); ++h)
			{
				const WED_LiveryIndexEntry * e = sorted[h].second;

				// No path, no entry. An index row whose object cannot be located -
				// no X-Plane root selected yet, for instance - would otherwise be a
				// frame in the cycle that can never draw anything.
				string abs_path = WED_LiveryObjectPath(e->obj_path);
				if (abs_path.empty()) continue;

				// THE RANGE RULE. A livery whose operator has no hub within the
				// aircraft's reach of this stand will not be spawned by the sim, so
				// it is not offered here either - not greyed, not annotated, simply
				// absent, exactly as it will be absent on the apron. What was
				// removed is remembered so the readout can say so; otherwise the
				// author sees United's card shrink to a 777 with no explanation.
				Allow a = LiveryAllowedHere(*e, here);
				if (a != allow_Yes)
				{
					if (a == allow_OutOfRange) mRangeHidden[code_uc].push_back(e->type);
					continue;
				}

				if (by_type)
				{
					string tkey = e->type;
					for (size_t c = 0; c < tkey.size(); ++c) tkey[c] = (char) tolower((unsigned char) tkey[c]);
					AirlineCard & tc = mAirlineCards[tkey];
					tc.icao = e->type;
					tc.name = e->type;
					if (tc.ioc_country.empty()) tc.ioc_country = e->reg_country;
					tc.abs_paths.push_back(abs_path);
					tc.types.push_back(e->type);
					// What distinguishes two PC-12s is the paint, so that is the label:
					// the registration when there is one, the note otherwise.
					string lab = !e->reg.empty() ? e->reg
							   : (e->note != "Default" ? e->note : string("Unmarked"));
					if (!e->reg.empty() && !e->note.empty() && e->note != "Default")
						lab += " (" + e->note + ")";
					// A GA/military card is one type; the operator still tells two
					// air forces' F-15s apart, so it goes on the label, not the face.
					if (code_uc != "XPGA" && code_uc != "XPMI")
						lab += "  " + code_uc;
					tc.labels.push_back(lab);
					continue;
				}

				card.abs_paths.push_back(abs_path);
				card.types.push_back(e->type);

				// "Default" means "no annotation" (see WED_LiveryIndex.h), so it
				// adds nothing; anything else is what separates two liveries of the
				// same type and has to be shown.
				string label = e->type;
				if (!e->note.empty() && e->note != "Default")
				{
					string n = e->note;
					for (size_t c = 0; c < n.size(); ++c)
						if (n[c] == '_') n[c] = ' ';		// stored underscored - see WED_MakeLiveryKey
					label += " (" + n + ")";
				}
				card.labels.push_back(label);
				if (card.ioc_country.empty()) card.ioc_country = e->reg_country;
			}
		}

		if (by_type) continue;						// filed per type above
		if (card.abs_paths.empty()) continue;		// nothing that fits - no card

		// The friendly name if the directory knows the code, the code itself if it
		// does not - a livery the index has is worth showing even when the operator
		// is missing from the name table.
		card.name = mAirlineDirectory.GetName(code_uc);
		if (card.name.empty()) card.name = code_uc;

		string key = code_uc;
		for (size_t ci = 0; ci < key.size(); ++ci)
			key[ci] = (char) tolower((unsigned char) key[ci]);
		mAirlineCards[key] = card;
	}
}

// Rows carry a lowercase icao; cards are keyed by the same string, so this cannot
// return a card belonging to a different row - see the .h on why the two are not
// a pair of parallel vectors.
vector<pair<string,string> >	WED_LiveryPane::AllOperators(void) const
{
	vector<pair<string,string> > out;
	for (map<string, AirlineCard>::const_iterator i = mAirlineCards.begin(); i != mAirlineCards.end(); ++i)
		out.push_back(make_pair(i->first, i->second.name));
	return out;
}

void	WED_LiveryPane::CardKeys(set<string> & out) const
{
	out.clear();
	for (map<string, AirlineCard>::const_iterator i = mAirlineCards.begin(); i != mAirlineCards.end(); ++i)
		out.insert(i->first);
}

const WED_LiveryPane::AirlineCard *	WED_LiveryPane::CardFor(const string & icao_lower) const
{
	map<string, AirlineCard>::const_iterator i = mAirlineCards.find(icao_lower);
	return (i == mAirlineCards.end()) ? NULL : &i->second;
}

// ---------------------------------------------------------------------------------------------
// spawn weight bars  (apt.dat row 1313)
// ---------------------------------------------------------------------------------------------

bool	WED_LiveryPane::SelectionHasWeights(void) const
{
	int w[6];
	for (size_t i = 0; i < mSelectedRamps.size(); ++i)
		if (mSelectedRamps[i]->GetClassWeights(w)) return true;
	return false;
}

// The weights to draw, or false when the selection disagrees about them. A
// mixed selection still SHOWS the section - the author needs to see that the
// stands differ - it just draws indeterminate, the same answer the tri-state
// operator checkbox gives to the same question.
bool	WED_LiveryPane::SelectionWeights(int out_w[6]) const
{
	bool have = false;
	int  w[6];
	for (size_t i = 0; i < mSelectedRamps.size(); ++i)
	{
		int cur[6];
		if (!mSelectedRamps[i]->GetClassWeights(cur)) return false;	// one stand has none -> mixed
		if (!have) { memcpy(w, cur, sizeof(w)); have = true; }
		else if (memcmp(w, cur, sizeof(w)) != 0) return false;		// they disagree
	}
	if (!have) return false;
	memcpy(out_w, w, sizeof(w));
	return true;
}

// Bars are relative, so the track has no natural ceiling. Ten is the idiom the
// format itself uses - §4.2 notes "3 and 7" means the same as "30 and 70" - but
// a file that arrived carrying 700/300 has to stay both visible and draggable,
// so the track grows to fit whatever is already there.
int		WED_LiveryPane::WeightTrackMax(void) const
{
	int w[6], hi = 10;
	if (SelectionWeights(w))
		for (int i = 0; i < 6; ++i) if (w[i] > hi) hi = w[i];
	return hi;
}

void	WED_LiveryPane::WeightBarRect(int bounds[4], int idx, float r_out[4]) const
{
	float line_h = GUI_GetLineHeight(font_UI_Basic);

	// The bars get their OWN inset track rather than the slider's. Sharing the
	// slider's extents put bar A's centre on the leftmost tick, which meant its
	// left half hung 12px outside the pane - and bar F's right half likewise.
	// Six bars of real width simply do not fit between two endpoints that were
	// laid out for two circular handles.
	const float pad  = 4;
	float span       = (float) (bounds[2] - bounds[0]) - pad * 2;
	float step       = span / 6.0f;					// six slots, not five gaps
	float half       = (std::min)(step * 0.40f, 22.0f);
	float track_x0   = bounds[0] + pad + step * 0.5f;	// centre of the first slot

	float top, bot;
	WeightsYRange(bounds, top, bot);

	float cx = track_x0 + step * idx;
	r_out[0] = cx - half;
	r_out[2] = cx + half;
	r_out[1] = bot + line_h * 2 + 4;			// leaves the label and percentage rows below
	r_out[3] = top - line_h - 6;				// leaves the section title above
}

int		WED_LiveryPane::WeightBarForXY(int bounds[4], int x, int y) const
{
	if (WeightsHeight() <= 0) return -1;
	for (int i = 0; i < 6; ++i)
	{
		float r[4];
		WeightBarRect(bounds, i, r);
		// Generous vertically: the whole column is the target, not just the
		// filled part, or dragging a zero-height bar back up would be
		// impossible.
		if (x >= r[0] && x <= r[2] && y >= r[1] - 4 && y <= r[3] + 4) return i;
	}
	return -1;
}

int		WED_LiveryPane::WeightValueForY(int bounds[4], int y) const
{
	float r[4];
	WeightBarRect(bounds, 0, r);
	float h = r[3] - r[1];
	if (h <= 0) return 0;

	float frac = ((float) y - r[1]) / h;
	if (frac < 0.0f) frac = 0.0f;
	if (frac > 1.0f) frac = 1.0f;

	int v = (int) (frac * (float) WeightTrackMax() + 0.5f);	// snap to an integer
	if (v < 0)    v = 0;
	if (v > 1000) v = 1000;									// R11 ceiling
	return v;
}

void	WED_LiveryPane::ApplyWeightDrag(void)
{
	for (size_t i = 0; i < mSelectedRamps.size(); ++i)
		mSelectedRamps[i]->SetClassWeights(mDragWeights);
	mCoverageDirty = true;
}

void	WED_LiveryPane::AbortWeightDrag(void)
{
	if (mDragWeightBar < 0) return;
	mArchive->AbortCommand();
	mDragWeightBar = -1;
}

// The ONLY path that gives a stand a 1313 row. Restores what the author had if
// they have been here before this session, otherwise seeds one unit per class
// inside the size range they already set - which reads as "any of these,
// equally", and is exactly what that range meant before weights existed.
void	WED_LiveryPane::SeedWeightsFromSizeRange(void)
{
	if (mSelectedRamps.empty()) return;

	mArchive->StartCommand("Add Spawn Weights");
	for (size_t i = 0; i < mSelectedRamps.size(); ++i)
	{
		WED_RampPosition * r = mSelectedRamps[i];

		// The stand may still hold a distribution from before it was switched to
		// the size range - in this session or in a saved document. Bring it back
		// rather than reseeding over it.
		int stored[6];
		if (r->HasStoredWeights(stored))
		{
			r->SetWeightsInUse(true);
			continue;
		}

		int lo = WidthEnumToIndex(r->GetWidthMin());
		int hi = WidthEnumToIndex(r->GetWidth());
		if (lo > hi) std::swap(lo, hi);

		int w[6];
		for (int k = 0; k < 6; ++k) w[k] = (k >= lo && k <= hi) ? 1 : 0;
		r->SetClassWeights(w);
	}
	mArchive->CommitCommand();

	mCoverageDirty = true;
	Refresh();
}

// Back to the plain size range, and back to "no 1313 row on this stand" - which
// is NOT six zeros. Six zeros is the author saying nothing parks here (§4.2); no
// row at all is the author not having said anything, which keeps today's
// step-down (R17). Conflating the two would put one of them out of reach.
//
// This is a MODE SWITCH, not a delete, and the mode lives on the stand: the
// weights stay in the document, flagged out of use, so they survive a save and
// a reload and come straight back when the author returns. Export follows the
// mode - a stand left in simple mode writes no 1313 row, whatever it holds.
// (A pane-side cache did this before, and lost the distribution on every save.)
void	WED_LiveryPane::SwitchToSimpleMode(void)
{
	if (mSelectedRamps.empty()) return;

	mArchive->StartCommand("Use Simple Size Range");
	for (size_t i = 0; i < mSelectedRamps.size(); ++i)
		mSelectedRamps[i]->SetWeightsInUse(false);
	mArchive->CommitCommand();

	mCoverageDirty = true;
	Refresh();
}

void	WED_LiveryPane::WeightButtonRect(int bounds[4], float b_out[4]) const
{
	// Lives on the size slider's row, right-aligned, because that is where the
	// author is when they decide this stand needs a distribution rather than a
	// range. Same derive-from-the-section idiom as SortButtonRect().
	float top, bot;
	if (SelectionHasWeights()) WeightsYRange(bounds, top, bot);	// the slider has collapsed - see SliderHeight()
	else                       SliderYRange(bounds, top, bot);
	const float pad = 4;
	const float w   = 124;

	b_out[2] = (float) bounds[2] - pad;
	b_out[0] = b_out[2] - w;
	b_out[3] = top - 3;
	b_out[1] = b_out[3] - (GUI_GetLineHeight(font_UI_Basic) + 6);
}

// ---------------------------------------------------------------------------------------------
// coverage readout  (WED_LiveryFormatSpec.md §4.5)
// ---------------------------------------------------------------------------------------------

// "Can the operators listed on this stand actually fill it?"
//
// A stand is counted EMPTY when no listed operator has a model in any class of
// the stand's own size range. In the sim that stand parks nothing, every time,
// and produces no log line and no error - an empty gate is indistinguishable
// from a gate that did not happen to get an aircraft this time. Spec §4.5
// measures 7,604 of 44,242 stands (17.2%) in that state across the real global
// apt.dat, at 42% of airports, and notes the cause is almost never "this
// operator has no models" but "none in THIS class".
//
// The index is consulted per (airline, class) rather than per airline: an
// operator having SOME model is not the same as having one that fits here, and
// conflating the two is precisely the mistake that makes the 17.2% invisible.
void	WED_LiveryPane::RecomputeCoverage(void)
{
	mCoverageDirty = false;

	Coverage c;
	c.index_ready       = false;
	c.weighted          = false;
	c.p_occupied        = 0.0f;
	c.empty_cause       = Coverage::empty_None;
	c.stands            = (int) mSelectedRamps.size();
	c.stands_empty      = 0;
	c.classes_in_range  = 0;
	c.classes_filled    = 0;
	c.airlines_listed   = 0;
	c.airlines_eligible = 0;
	c.lo_class          = 'A';
	c.hi_class          = 'F';

	// EnsureLoaded() is a no-op for a path it has already tried, success or
	// failure, so this is safe to call as often as the readout is refreshed - and
	// it re-loads by itself if the user has since pointed WED at a different
	// X-Plane folder, because the path is derived from the root.
	const string index_path = WED_LiveryIndexDefaultPath();
	if (!index_path.empty())
		mLiveryIndex.EnsureLoaded(index_path);

	// A missing or unreadable index MUST NOT read as "nothing fits" - see spec
	// §6.4, where an index/install mismatch is called out as failing silently.
	// Leaving index_ready false makes Draw() say so instead of printing a zero.
	if (!mLiveryIndex.IsLoaded())
	{
		c.index_version = mLiveryIndex.DescribeVersion();
		mCoverage = c;
		return;
	}
	c.index_ready   = true;
	c.index_version = mLiveryIndex.DescribeVersion();

	for (size_t i = 0; i < mSelectedRamps.size(); ++i)
	{
		WED_RampPosition * ramp = mSelectedRamps[i];

		int lo = WidthEnumToIndex(ramp->GetWidthMin());
		int hi = WidthEnumToIndex(ramp->GetWidth());
		if (lo > hi) std::swap(lo, hi);		// defensive; the slider cannot produce it

		// With a 1313 row the size letter is DERIVED - R23 writes only the largest
		// weighted class into 1301 - so width_min..width collapses to one class
		// and the eligibility scan below missed every operator whose aircraft sit
		// lower. A C+E stand listing Air China (737) and United (747) reported
		// "Only UAL will ever park here". Scan what the weights actually open.
		{
			int w[6];
			if (ramp->GetClassWeights(w))
			{
				int wlo = -1, whi = -1;
				for (int k = 0; k < 6; ++k) if (w[k] > 0) { if (wlo < 0) wlo = k; whi = k; }
				if (wlo >= 0) { lo = wlo; hi = whi; }
			}
		}

		set<string> codes = ParseCodes(ramp->GetAirlines());

		// The readout must count what the sim will actually draw from, so the
		// range rule applies here exactly as it does to the cards: an operator
		// whose only class-C aircraft cannot reach this stand does not "fill"
		// class C, and must not be reported as eligible. Same predicate, same
		// stand position, so the percentage and the cards agree.
		Point2 here;
		ramp->GetLocation(gis_Geo, here);

		int  filled_classes  = 0;
		set<string> eligible;
		for (int k = lo; k <= hi; ++k)
		{
			char size_class = (char) ('A' + k);
			bool any_here = false;
			for (set<string>::const_iterator it = codes.begin(); it != codes.end(); ++it)
			{
				vector<const WED_LiveryIndexEntry *> hits;
				mLiveryIndex.GetForAirlineAndClass(*it, size_class, hits);
				bool reachable = false;
				for (size_t h = 0; h < hits.size() && !reachable; ++h)
					reachable = LiveryAllowedHere(*hits[h], here) == allow_Yes;
				if (reachable)
				{
					any_here = true;
					eligible.insert(*it);
				}
			}
			if (any_here) ++filled_classes;
		}

		if (filled_classes == 0) ++c.stands_empty;

		// With a 1313 row the flat range stops being the question. The author
		// has said how often each class is drawn, so the quantity that matters
		// is the one §4.5 specifies: how much of that distribution lands on a
		// class no listed operator can fill. Everything above stays as the
		// fallback for a stand with no weights, which R17 keeps on today's
		// behaviour.
		int wts[6];
		if (mSelectedRamps.size() == 1 && ramp->GetClassWeights(wts))
		{
			int total = 0;
			for (int k = 0; k < 6; ++k) total += wts[k];

			if (total > 0)
			{
				int  fillable        = 0;
				bool any_art_at_all  = false;	// does the LIBRARY have anything at a weighted class?
				bool listed_have_art = false;	// do the LISTED operators, range aside?
				for (int k = 0; k < 6; ++k)
				{
					if (wts[k] == 0) continue;
					char size_class = (char) ('A' + k);

					if (mLiveryIndex.CountAtClass(size_class) > 0) any_art_at_all = true;

					for (set<string>::const_iterator it = codes.begin(); it != codes.end(); ++it)
					{
						vector<const WED_LiveryIndexEntry *> hits;
						mLiveryIndex.GetForAirlineAndClass(*it, size_class, hits);
						if (!hits.empty()) listed_have_art = true;
						// Same range rule as the cards and the flat loop above: a
						// class is only "filled" by an aircraft that can reach the
						// stand. Without this the weighted readout said "100% of the
						// time, from 0 of 2 listed operators" - both halves computed
						// honestly, from different definitions of eligible.
						bool reach = false;
						for (size_t h = 0; h < hits.size() && !reach; ++h)
							reach = LiveryAllowedHere(*hits[h], here) == allow_Yes;
						if (reach) { fillable += wts[k]; break; }
					}
				}
				c.weighted   = true;
				c.p_occupied = (float) fillable / (float) total;

				if (fillable == 0)
					// R14's distinction, and it decides whether the author is
					// being told they made a mistake or told they are early.
					// The third case is the range rule's: the aircraft exist and the
					// operators fly them, they just cannot get here. Saying "no
					// aircraft at size C" for that sends the author to the size
					// slider, the one control that cannot fix it.
					c.empty_cause = !any_art_at_all  ? Coverage::empty_NoArtYet
									: listed_have_art ? Coverage::empty_OutOfRange
													  : Coverage::empty_Unfillable;
			}
			else
			{
				// All six zero is legal and deliberate: the author said nothing
				// parks here. That is NOT the same as an unfillable stand, and
				// the readout must not accuse them of a mistake for it.
				c.weighted    = true;
				c.p_occupied  = 0.0f;
				c.empty_cause = Coverage::empty_ByChoice;
			}
		}

		if (mSelectedRamps.size() == 1)
		{
			c.classes_in_range  = hi - lo + 1;
			c.classes_filled    = filled_classes;
			c.airlines_listed   = (int) codes.size();
			c.airlines_eligible = (int) eligible.size();
			c.lo_class          = (char) ('A' + lo);
			c.hi_class          = (char) ('A' + hi);

			// Variety collapse. The author listed several operators and exactly
			// one of them can ever appear, so this stand parks the same airline
			// every single time. It is NOT the empty case - aircraft do spawn,
			// nothing looks broken - which is why nothing else in this readout
			// would ever mention it.
			if (eligible.size() == 1 && codes.size() > 1)
				c.sole_operator = *eligible.begin();

			// Once a stand is weighted, the size SLIDER is no longer what the
			// sentence should name - the author's weights are. They usually
			// agree (R23 derives the 1301 letter from the weights on export),
			// but a stand weighted for D alone inside a C-E range would
			// otherwise be described as C-E, which is not what will spawn.
			if (c.weighted)
			{
				int w_lo = -1, w_hi = -1;
				for (int k = 0; k < 6; ++k)
					if (wts[k] > 0) { if (w_lo < 0) w_lo = k; w_hi = k; }
				if (w_lo >= 0)
				{
					c.lo_class = (char) ('A' + w_lo);
					c.hi_class = (char) ('A' + w_hi);
				}
			}
		}
	}

	mCoverage = c;
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
	// Gone entirely once the stand has weights: the six bars ARE the size control
	// then, and a greyed slider saying "derived from the weights below" was a row
	// of dead space explaining its own absence. The Simple Mode button moves onto
	// the weights section's title row - see WeightButtonRect().
	if (SelectionHasWeights()) return 0;
	// title row + A-F label row + track/ball row, plus padding
	return GUI_GetLineHeight(font_UI_Basic) * 3 + 16;
}

float	WED_LiveryPane::WeightsHeight(void) const
{
	// Collapses to nothing when there is nothing to show. A stand with no 1313
	// row keeps today's behaviour (R17) and should not be carrying an empty
	// control that implies otherwise.
	if (!SelectionHasWeights()) return 0;

	// title row + bar track + the A-F label row + the percentage row
	return GUI_GetLineHeight(font_UI_Basic) * 3 + 44;
}

float	WED_LiveryPane::CoverageHeight(void) const
{
	// Two lines: the headline, and one line of detail. Fixed rather than
	// content-derived so the sections below it never shift as the numbers change -
	// a readout that moves the airline list every time you tick a checkbox is
	// worse than no readout.
	// Content-derived after all: the detail and the range clause wrap to the
	// pane's width, so a narrow panel needs more lines than a wide one and a
	// fixed count either clipped or wasted. mCoverageLineCount is set by Draw()
	// from the wrapped text BEFORE it lays the section out, so there is no frame
	// of lag. The list below does move when the count changes; that is the price
	// of legible text, and it changes only when the stand's situation does.
	return GUI_GetLineHeight(font_UI_Basic) * mCoverageLineCount + 10;
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

void	WED_LiveryPane::WeightsYRange(int bounds[4], float & top, float & bot) const
{
	float stop, sbot;
	SliderYRange(bounds, stop, sbot);
	const float h = WeightsHeight();
	// A hidden section takes no gap either, or every stand without weights
	// would carry a stripe of dead space where the bars would have been.
	top = sbot - (h > 0 ? GapHeight() : 0);
	bot = top - h;
}

void	WED_LiveryPane::CoverageYRange(int bounds[4], float & top, float & bot) const
{
	float wtop, wbot;
	WeightsYRange(bounds, wtop, wbot);
	top = wbot - GapHeight();
	bot = top - CoverageHeight();
}

void	WED_LiveryPane::ListToolbarYRange(int bounds[4], float & top, float & bot) const
{
	float ctop, cbot;
	CoverageYRange(bounds, ctop, cbot);
	top = cbot - GapHeight();
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

// THE ONE PLACE ROW GEOMETRY IS COMPUTED. Cards are no longer a block above the
// checklist - they ARE the airline rows, laid out kCardCols to a line inside
// whichever section emitted them, with that section's header and divider still
// drawn as ordinary full-width lines in between.
//
// Every caller runs this rather than re-deriving y from a row index. The previous
// design wrote the formula out in Draw() and again in RowForY(), and they drifted:
// a click in the bottom row-height of the card strip toggled the first airline's
// checkbox, because one of the two forgot the card block was there.
float	WED_LiveryPane::LayoutRows(int bounds[4], const vector<bool> & is_card,
								   const vector<float> & tray_h,
								   vector<RowSlot> & out) const
{
	const float row_h  = GUI_GetLineHeight(font_UI_Basic) + 4;
	const float card_w = CardWidth(bounds);
	const float card_h = CardHeight(bounds);
	const float y_top  = ContentTop(bounds) + mScrollOffset;

	out.clear();
	out.resize(is_card.size());

	float y          = y_top;
	int   col        = 0;
	float line_extra = 0.0f;		// tray height added to the current line of cards

	for (size_t i = 0; i < is_card.size(); ++i)
	{
		RowSlot & s = out[i];
		s.is_card = is_card[i];

		if (is_card[i])
		{
			if (col == 0)
			{
				y -= kCardGap;						// the gap sits above each line of cards

				// The tallest tray anywhere on THIS grid line sets the line's extra
				// height. Applying it per card instead would slide one column's tray
				// out from under its neighbour and over the line below.
				line_extra = 0.0f;
				int scan_col = 0;
				for (size_t j = i; j < is_card.size() && is_card[j] && scan_col < kCardCols; ++j, ++scan_col)
					if (j < tray_h.size() && tray_h[j] > line_extra) line_extra = tray_h[j];
			}
			s.top      = y;
			s.bot      = y - card_h;				// the card itself - the tray hangs below it
			s.slot_bot = y - card_h - line_extra;
			s.x0  = (float) bounds[0] + kCardGap + col * (card_w + kCardGap);
			s.x1  = s.x0 + card_w;
			if (++col == kCardCols) { y -= card_h + line_extra; col = 0; }
		}
		else
		{
			// A header cannot share a line with the cards above it, so close any
			// partly-filled card line first. That is also what leaves a lone last
			// card sitting under column 0 with whitespace beside it, rather than
			// centred or stretched.
			if (col != 0) { y -= card_h + line_extra; col = 0; line_extra = 0.0f; }
			s.top      = y;
			s.bot      = y - row_h;
			s.slot_bot = s.bot;
			s.x0  = (float) bounds[0];
			s.x1  = (float) bounds[2];
			y -= row_h;
		}
	}
	if (col != 0) y -= card_h + line_extra;			// trailing partial line still takes its height

	// A bottom margin, counted into the content height so it can actually be
	// scrolled to. Without it the last section header - usually the collapsed "All
	// Airlines" - sits flush on the pane's edge with its box touching the frame.
	y -= kCardGap * 2.0f;

	return y_top - y;								// total content height
}

int		WED_LiveryPane::RowForXY(int bounds[4], const vector<bool> & is_card,
								 const vector<float> & tray_h, int x, int y) const
{
	if ((float) y > ContentTop(bounds)) return -1;	// above the content area entirely

	vector<RowSlot> slots;
	LayoutRows(bounds, mRowIsCard, tray_h, slots);

	for (size_t i = 0; i < slots.size(); ++i)
	{
		const RowSlot & s = slots[i];
		if ((float) y > s.top || (float) y <= s.slot_bot) continue;
		// Cards only answer for their own column - the gaps between and after them
		// are deliberately "not a card", so a click in the whitespace beside a lone
		// final card does nothing instead of toggling it.
		if (s.is_card && ((float) x < s.x0 || (float) x > s.x1)) continue;
		return (int) i;
	}
	return -1;
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

// Cards and text rows share one hit test, so every caller needs the same "which
// rows are cards" vector. Kept here rather than repeated at each call site,
// because a caller that got it wrong would hit-test against a layout the draw
// never used - the exact class of drift LayoutRows exists to end.
static void	CardFlags(const vector<WED_LiveryDisplayRow> & rows, vector<bool> & out)
{
	out.resize(rows.size());
	for (size_t i = 0; i < rows.size(); ++i)
		out[i] = (rows[i].kind == wed_Row_Airline);
}

// The icao of each airline row, empty for every other kind. Feeds TrayHeights,
// which cannot take the rows themselves - see its declaration.
static void	RowIcaos(const vector<WED_LiveryDisplayRow> & rows, vector<string> & out)
{
	out.assign(rows.size(), string());
	for (size_t i = 0; i < rows.size(); ++i)
		if (rows[i].kind == wed_Row_Airline) out[i] = rows[i].icao;
}


// ---------------------------------------------------------------------------------------------
// card sub-targets, the animation timer, and the tray
// ---------------------------------------------------------------------------------------------

// ONE per frame, not a batch. Each render parses an OBJ that can be 150k lines
// and uploads a 2048-square texture, so two of them in a frame is a visible
// hitch; one, with Draw() asking for another frame while any remain, fills a cold
// screenful over a few frames and never blocks.
static const int   kMaxRendersPerFrame = 1;

static const float kTrayRowH   = 18.0f;		// one aircraft line inside an open tray
static const float kTrayPad    =  6.0f;
static const float kLockSize   = 14.0f;

// Top-right of the card. Carved off the slot rather than re-derived, so it cannot
// drift from what DrawAirlineCard paints.
void	WED_LiveryPane::LockIconRect(const RowSlot & slot, float r_out[4]) const
{
	r_out[2] = slot.x1 - 5.0f;
	r_out[0] = r_out[2] - kLockSize;
	r_out[3] = slot.top - 5.0f;
	r_out[1] = r_out[3] - kLockSize;
}

// The strip along the card's bottom edge that opens the tray. Deliberately the
// full card width: it is a "pull this open" affordance, and a narrow tab would be
// a worse target for no gain.
void	WED_LiveryPane::TrayTabRect(const RowSlot & slot, float r_out[4]) const
{
	// THE WHOLE CAPTION ROW, not a gutter under the arrow. A 22px target was the
	// reason this needed three attempts to hit; the row is the full card width and
	// still cannot be confused with the picture above it, which is what selects.
	r_out[0] = slot.x0;
	r_out[2] = slot.x1;
	r_out[1] = slot.bot;
	r_out[3] = slot.bot + kTrayTabH;
}

// How tall this operator's tray is when fully open.
static float	TrayFullHeight(size_t n_liveries)
{
	// One extra row's worth for the "Preview ONLY" band across the top.
	return kTrayPad * 2.0f + (float) (n_liveries + 1) * kTrayRowH;
}

// ANIMATION IS DRIVEN FROM Draw(), NOT FROM A TIMER. GUI_Timer's Windows path is
// SetTimer(NULL, 0, ...) - a thread timer whose WM_TIMER only arrives if the
// message loop dispatches messages with a NULL hwnd. It does not here: the log
// showed Start() being called on every hover (busy=1) and TimerFired running
// exactly zero times, which is why neither the hover cycle nor the tray ever
// moved while both looked correct in every other respect.
//
// Advancing on wall-clock time inside Draw() and asking for another frame while
// anything is still moving needs no platform support and is the same pattern the
// progressive thumbnail fill in this pane already uses successfully. It also
// self-limits: when nothing is animating no extra frame is requested.
//
// Returns true when something is still in motion, so Draw() knows to come back.
bool	WED_LiveryPane::StepAnimation(void)
{
	clock_t now = clock();
	if (mLastAnimClock == 0) { mLastAnimClock = now; return false; }

	float dt = (float)(now - mLastAnimClock) / (float) CLOCKS_PER_SEC;
	mLastAnimClock = now;

	// A frame that took a long time - the window was hidden, or a thumbnail parse
	// ran long - must not teleport the animation to its end.
	if (dt < 0.0f)  dt = 0.0f;
	if (dt > 0.1f)  dt = 0.1f;

	bool moving = false;

	const float kTraySpeed = 4.0f;				// full travel in a quarter second
	if (!mTrayAirline.empty() && mTrayOpen < 1.0f)
	{
		mTrayOpen = (std::min)(1.0f, mTrayOpen + dt * kTraySpeed);
		moving = true;
	}
	if (!mTrayClosing.empty())
	{
		mTrayClosingOpen -= dt * kTraySpeed;
		if (mTrayClosingOpen <= 0.0f) { mTrayClosingOpen = 0.0f; mTrayClosing.clear(); }
		moving = true;
	}

	// One aircraft per second on the hovered card. mCycleShow is advanced blind and
	// wrapped by the drawer against the card's actual length, because the card can
	// change under the cursor - a weight drag can remove a whole class - and
	// clamping here would need the card, which this does not have.
	if (!mCycleAirline.empty() && mTrayHoverIdx < 0)
	{
		mCycleAccum += dt;
		if (mCycleAccum >= 1.0f) { mCycleAccum -= 1.0f; ++mCycleShow; }
		moving = true;
	}

	return moving;
}

// The list under an open card: every aircraft that operator can park at this
// stand, biggest first, boxed one per line. Clipped to the animated height, so it
// is revealed rather than scaled - text that scaled would shimmer.
void	WED_LiveryPane::DrawCardTray(GUI_GraphState * state, const RowSlot & slot,
									 const AirlineCard & card, float open_frac, int lit_row)
{
	if (open_frac <= 0.0f || card.abs_paths.empty()) return;

	float full = TrayFullHeight(card.abs_paths.size());
	float h    = full * open_frac;
	float top  = slot.bot;
	float bot  = top - h;

	state->SetState(0,0,0,0,1,0,0);
	glColor4f(0.16f, 0.16f, 0.18f, 0.97f);
	glBegin(GL_QUADS);
		glVertex2f(slot.x0, bot);  glVertex2f(slot.x1, bot);
		glVertex2f(slot.x1, top);  glVertex2f(slot.x0, top);
	glEnd();

	glColor4f(0.40f, 0.40f, 0.44f, 1.0f);
	glBegin(GL_LINE_LOOP);
		glVertex2f(slot.x0 + 0.5f, bot + 0.5f);  glVertex2f(slot.x1 - 0.5f, bot + 0.5f);
		glVertex2f(slot.x1 - 0.5f, top - 0.5f);  glVertex2f(slot.x0 + 0.5f, top - 0.5f);
	glEnd();

	float line_h = GUI_GetLineHeight(font_UI_Basic);
	float txt[4] = { 0.86f, 0.86f, 0.88f, 1.0f };
	for (size_t i = 0; i < card.labels.size(); ++i)
	{
		float ry = top - kTrayPad - (float) (i + 2) * kTrayRowH;	// +1 for the band
		if (ry < bot) break;					// still sliding open - the rest is not revealed yet

		// The lit row is whatever is on the card's face right now, so the tray and
		// the picture above it always agree about which aircraft is being shown.
		if ((int) i == lit_row) glColor4f(0.30f, 0.46f, 0.36f, 1.0f);
		else                    glColor4f(0.24f, 0.24f, 0.27f, 1.0f);
		glBegin(GL_QUADS);
			glVertex2f(slot.x0 + kTrayPad,          ry + 1);
			glVertex2f(slot.x1 - kTrayPad,          ry + 1);
			glVertex2f(slot.x1 - kTrayPad,          ry + kTrayRowH - 2);
			glVertex2f(slot.x0 + kTrayPad,          ry + kTrayRowH - 2);
		glEnd();

		GUI_FontDraw(state, font_UI_Basic, txt, slot.x0 + kTrayPad + 5,
					 ry + (kTrayRowH - line_h) * 0.5f + 1, card.labels[i].c_str());
	}

	// The tray shows what this operator HAS; it is not a second place to choose
	// from. Saying so is cheaper than letting someone discover it by clicking - the
	// per-stand aircraft whitelist that would have made these selectable is exactly
	// what draft 7 removed (spec 8.6).
	if (open_frac > 0.6f)
	{
		float pc[4] = { 0.62f, 0.62f, 0.66f, 1.0f };
		const char * only = "Preview ONLY";
		float ow = GUI_MeasureRange(font_UI_Basic, only, only + strlen(only));
		GUI_FontDraw(state, font_UI_Basic, pc, slot.x1 - kTrayPad - ow,
					 top - kTrayPad - kTrayRowH + (kTrayRowH - line_h) * 0.5f + 1.0f, only);
	}
}


// The extra height each row's tray is currently claiming. Both the opening tray
// and the retracting one contribute, which is what keeps the total continuous
// while they cross over - if only the opening one counted, every row below would
// jump up by a tray's height the moment the other was dropped.
void	WED_LiveryPane::TrayHeights(const vector<string> & row_icaos, vector<float> & out) const
{
	out.assign(row_icaos.size(), 0.0f);
	for (size_t i = 0; i < row_icaos.size(); ++i)
	{
		if (row_icaos[i].empty()) continue;
		const AirlineCard * ac = CardFor(row_icaos[i]);
		if (!ac) continue;

		if (row_icaos[i] == mTrayAirline)
			out[i] = TrayFullHeight(ac->abs_paths.size()) * mTrayOpen;
		else if (row_icaos[i] == mTrayClosing)
			out[i] = TrayFullHeight(ac->abs_paths.size()) * mTrayClosingOpen;
	}
}


// The operator's full aircraft list, at the cursor. The caption can only fit
// "(and 3 more)" - this is what says WHICH three, without making the reader open
// the tray to find out that none of them was what they wanted.
//
// Drawn LAST in Draw(), after the scissor is popped, for the usual reason a
// tooltip is: it has to be allowed outside the box that spawned it, and a card
// near the bottom of the list has nowhere else to put it.
void	WED_LiveryPane::DrawHoverTip(GUI_GraphState * state, int b[4])
{
	if (mHoverTipText.empty()) return;
	const string & text = mHoverTipText;

	float line_h = GUI_GetLineHeight(font_UI_Basic);
	float tw     = GUI_MeasureRange(font_UI_Basic, text.c_str(), text.c_str() + text.size());
	float pad    = 6.0f;
	float w      = tw + pad * 2.0f;
	float h      = line_h + pad * 2.0f - 2.0f;

	// Flip to the other side of the cursor rather than being clipped - a tip that
	// runs off the pane is worse than no tip, because the part that falls off is
	// the end of the list.
	float x0 = (float) mHoverX + 14.0f;
	if (x0 + w > (float) b[2] - 2.0f) x0 = (float) mHoverX - 14.0f - w;
	if (x0 < (float) b[0] + 2.0f)     x0 = (float) b[0] + 2.0f;

	float y1 = (float) mHoverY + 6.0f + h;
	if (y1 > (float) b[3] - 2.0f) y1 = (float) mHoverY - 6.0f;
	float y0 = y1 - h;

	state->SetState(0,0,0,0,1,0,0);
	glColor4f(0.0f, 0.0f, 0.0f, 0.30f);
	glBegin(GL_QUADS);
		glVertex2f(x0+2, y0-2);  glVertex2f(x0+w+2, y0-2);
		glVertex2f(x0+w+2, y1-2); glVertex2f(x0+2, y1-2);
	glEnd();
	glColor4f(0.13f, 0.13f, 0.16f, 0.98f);
	glBegin(GL_QUADS);
		glVertex2f(x0, y0);  glVertex2f(x0+w, y0);
		glVertex2f(x0+w, y1); glVertex2f(x0, y1);
	glEnd();
	glColor4f(0.45f, 0.45f, 0.50f, 1.0f);
	glBegin(GL_LINE_LOOP);
		glVertex2f(x0+0.5f, y0+0.5f);   glVertex2f(x0+w-0.5f, y0+0.5f);
		glVertex2f(x0+w-0.5f, y1-0.5f); glVertex2f(x0+0.5f, y1-0.5f);
	glEnd();

	float tc[4] = { 0.90f, 0.90f, 0.93f, 1.0f };
	GUI_FontDraw(state, font_UI_Basic, tc, x0 + pad, y0 + pad - 1.0f, text.c_str());
}


// -1 when no tray is open, when the point is elsewhere, or while the tray is
// still moving: a target sliding under the cursor is not a target, and treating
// it as one makes the preview flicker between aircraft as the drawer extends.
// Takes the caller's layout rather than building its own. MouseMove had already
// assembled the row list - 150-odd operators, sorted and filtered - and this
// rebuilt the identical thing a second time on every single mouse move.
int		WED_LiveryPane::TrayRowForXY(const vector<string> & row_icaos,
									 const vector<RowSlot> & slots, int x, int y)
{
	if (mTrayAirline.empty() || mTrayOpen < 1.0f) return -1;
	const AirlineCard * ac = CardFor(mTrayAirline);
	if (!ac) return -1;

	for (size_t vi = 0; vi < row_icaos.size() && vi < slots.size(); ++vi)
	{
		if (row_icaos[vi] != mTrayAirline) continue;
		const RowSlot & sl = slots[vi];
		if ((float) x < sl.x0 || (float) x > sl.x1) continue;

		float top = sl.bot;
		for (size_t i = 0; i < ac->labels.size(); ++i)
		{
			float ry = top - kTrayPad - (float) (i + 2) * kTrayRowH;
			if ((float) y >= ry && (float) y <= ry + kTrayRowH) return (int) i;
		}
	}
	return -1;
}

// ONE CARD. Everything it needs is passed in: it is called from the row loop now,
// once per airline row, rather than from a block of its own above the checklist.
// `show` picks which of the operator's liveries is on the face - see AirlineCard
// in the .h for the ordering, and why index 0 is what a card shows at rest.
void	WED_LiveryPane::DrawAirlineCard(GUI_GraphState * state, const RowSlot & slot,
										const AirlineCard & card, int show,
										bool is_selected, bool is_hover, bool is_pressed,
										bool is_locked, bool is_dimmed,
										float tray_open,
										int & renders_this_frame)
{
	if (card.abs_paths.empty()) return;
	if (show < 0 || show >= (int) card.abs_paths.size()) show = 0;
	const string & abs_path = card.abs_paths[show];
	const string & type_str = card.labels[show];

	float line_h = GUI_GetLineHeight(font_UI_Basic);

	WED_ResourceMgr * res_mgr = WED_GetResourceMgr(mResolver);
	ITexMgr *         tex_mgr = WED_GetTexMgr(mResolver);

	float card_x0   = slot.x0, card_x1 = slot.x1;
	float card_bot  = slot.bot, card_top = slot.top;
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
	// Cards are previews, not a picker - there is no selected state left
	// to draw. Hover survives because pointing at a card and having it
	// respond is how the strip reads as a list of distinct things.
	// NO PRESSED STATE. A press that is not also a release means nothing here - one
	// click already does the whole job - so darkening the card mid-gesture only
	// made it flicker on the way to the thing the user wanted.
	(void) is_pressed;
	bool is_hovered  = is_hover;

	// A selected card swaps its whole body from neutral grey to the
	// picker's green (0x639875). The sheen drawn later is plain white at
	// low alpha, so it lightens whatever is underneath - over the green
	// that reads as a brighter green band, which is what makes the
	// selected state obvious at a glance rather than subtle.
	float body_r, body_g, body_b;
	if (is_selected)
	{
		body_r = 0.388f; body_g = 0.596f; body_b = 0.459f;		// 0x639875
		float k = is_hovered ? 1.12f : 1.0f;
		body_r = (std::min)(1.0f, body_r * k);
		body_g = (std::min)(1.0f, body_g * k);
		body_b = (std::min)(1.0f, body_b * k);
	}
	else
	{
		float g = is_hovered ? 0.21f : 0.17f;
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
	bool already_cached = mThumbCache.IsCached(abs_path);
	const WED_LiveryThumbnail * thumb = nullptr;
	if (already_cached || renders_this_frame < kMaxRendersPerFrame)
	{
		thumb = mThumbCache.GetThumbnail(res_mgr, tex_mgr, state, abs_path);
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
	// The registration country of THIS aircraft, not the airport's -
	// an operator's fleet can be registered anywhere, and the index
	// carries the IOC code per livery for exactly this.
	const WED_LiveryThumbnail * flag = card.ioc_country.empty()
										? NULL
										: EnsureRawFlagTexture(card.ioc_country);
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

	// Caption line: "AAL - B772 (and 3 more)" on the left, registration country
	// right-aligned just inside the flag.
	//
	// OPERATOR FIRST, THEN THE AIRCRAFT. Both are needed - a card is one operator
	// but the picture is one aircraft, and neither alone explains what is on
	// screen. Leading with the operator is what makes the tail safe to elide: the
	// code is fixed-width, so "(and N more)" always lands in the same place
	// regardless of how long the operator's name would have been. Leading with the
	// full name instead put the interesting part behind an unpredictable amount of
	// text, which is how "B772 - American ... " ate its own suffix.
	const char * card_country = card.ioc_country.empty() ? "" : card.ioc_country.c_str();

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
	// The text starts after the arrow's gutter when there is an arrow, and at the
	// card's edge when there is not, so a single-aircraft card does not carry an
	// indent for a control it does not have.
	float text_x0    = card_x0 + (card.abs_paths.size() > 1 ? kTrayGutterW : 5.0f);
	float left_avail = (cc_x - 6) - text_x0;

	// A per-type card (GA, military) is one type by construction, so its face is
	// the type and the count - "PC12 (+11)". What differs between its entries is
	// the paint, and that is what the tray is for.
	string head = mCardsByType ? card.icao : card.icao + " - " + type_str;
	string tail, tail_short;
	if (card.abs_paths.size() > 1)
	{
		char m[40];
		snprintf(m, sizeof(m), "  (and %d more)", (int) card.abs_paths.size() - 1);
		tail = m;
		snprintf(m, sizeof(m), "  (+%d)", (int) card.abs_paths.size() - 1);
		tail_short = m;
	}

	// THE SUFFIX ALWAYS SURVIVES. It is the only thing on the face that says the
	// card opens; a user who cannot see "(+8)" has no way to know eight more
	// aircraft are behind it. So it is reserved FIRST - long form if it fits with
	// the whole head, compact "(+N)" otherwise - and the head is cut to whatever
	// is left, with an ellipsis. The earlier fitting order (head first, suffix if
	// room) produced exactly the card this comment is written against: a full
	// operator name and no hint at all that it was a stack.
	float head_w = GUI_MeasureRange(font_UI_Basic, head.c_str(), head.c_str() + head.size());
	float tail_w = tail.empty() ? 0.0f
				 : GUI_MeasureRange(font_UI_Basic, tail.c_str(), tail.c_str() + tail.size());
	if (!tail.empty() && head_w + tail_w > left_avail)
	{
		tail   = tail_short;
		tail_w = GUI_MeasureRange(font_UI_Basic, tail.c_str(), tail.c_str() + tail.size());
	}

	float head_avail = left_avail - tail_w;
	if (head_w > head_avail)
	{
		const string ell = "...";
		while (!head.empty())
		{
			head.pop_back();
			while (!head.empty() && head[head.size() - 1] == ' ')
				head.pop_back();			// no "Air ..." - tuck the dots up against the text
			string probe = head + ell;
			if (GUI_MeasureRange(font_UI_Basic, probe.c_str(), probe.c_str() + probe.size()) <= head_avail)
				break;
		}
		head = head.empty() ? string() : head + ell;
	}
	string caption = head + tail;
	if (!caption.empty())
		GUI_FontDraw(state, font_UI_Basic, text_col, text_x0, text_y, caption.c_str());

	// --- disclosure triangle, left end of the bottom bar. ONLY on cards that have
	// more than one livery: on a single-aircraft card there is nothing to page
	// through and nothing for a tray to list, so an arrow there would promise a
	// slideshow that never comes. Its presence is therefore the answer to "is this
	// card supposed to be cycling" - which is unanswerable without it, since a
	// still card and a card with one aircraft look identical.
	//
	// Points right when shut and rotates to point down as the tray extends, driven
	// by the same 0..1 the tray height uses, so the arrow and the drawer are never
	// out of step. Long and narrow rather than equilateral - a stubby triangle at
	// this size reads as a blob.
	if (card.abs_paths.size() > 1)
	{
		const float cx  = card_x0 + kTrayGutterW * 0.5f;
		const float cy  = card_bot + (image_bot - card_bot) * 0.5f;
		const float lon = 5.5f, lat = 3.2f;		// along the pointing axis, and across it

		// Defined pointing RIGHT, then rotated to wherever the tray has got to: a
		// real rotation rather than a lerp between two shapes, so the triangle keeps
		// its proportions all the way round instead of flattening in the middle.
		float t = (tray_open < 0.0f) ? 0.0f : (tray_open > 1.0f ? 1.0f : tray_open);
		float a = -1.57079633f * t;				// 0 = right, -90 deg = down
		float ca = cosf(a), sa = sinf(a);

		const float px[3] = {  lon, -lon * 0.55f, -lon * 0.55f };
		const float py[3] = { 0.0f, -lat,          lat         };

		state->SetState(0,0,0,0,1,0,0);
		glColor4f(0.82f, 0.82f, 0.86f, is_dimmed ? 0.35f : 0.90f);
		glBegin(GL_TRIANGLES);
			for (int i = 0; i < 3; ++i)
				glVertex2f(cx + px[i] * ca - py[i] * sa,
						   cy + px[i] * sa + py[i] * ca);
		glEnd();
	}

	// --- another card holds the lock, so this one is out of the running. A flat
	// wash over the finished card rather than a different set of colours for every
	// element: it reads as "disabled" without needing a second palette, and it
	// cannot get out of step with the card art underneath. ---
	if (is_dimmed)
	{
		state->SetState(0,0,0,0,1,0,0);
		glColor4f(0.10f, 0.10f, 0.12f, 0.66f);
		glBegin(GL_QUADS);
			glVertex2f(card_x0, card_bot);  glVertex2f(card_x1, card_bot);
			glVertex2f(card_x1, card_top);  glVertex2f(card_x0, card_top);
		glEnd();
	}

	// --- lock badge, top right. PLACEHOLDER ART: a plain square with the same
	// offset-slab shadow the card itself uses, standing in until there is an icon.
	// Deliberately drawn even when unlocked, at low contrast, because a control
	// that only appears once you have used it cannot be discovered. ---
	{
		float lr[4];
		lr[2] = card_x1 - 5.0f;  lr[0] = lr[2] - kLockSize;
		lr[3] = card_top - 5.0f; lr[1] = lr[3] - kLockSize;

		state->SetState(0,0,0,0,1,0,0);
		for (int sh = 2; sh >= 1; --sh)
		{
			glColor4f(0, 0, 0, 0.22f);
			glBegin(GL_QUADS);
				glVertex2f(lr[0]+sh, lr[1]-sh);  glVertex2f(lr[2]+sh, lr[1]-sh);
				glVertex2f(lr[2]+sh, lr[3]-sh);  glVertex2f(lr[0]+sh, lr[3]-sh);
			glEnd();
		}
		if (is_locked) glColor4f(0.98f, 0.80f, 0.25f, 1.00f);	// held - amber, unmistakable
		else           glColor4f(0.72f, 0.72f, 0.76f, 0.55f);	// available
		glBegin(GL_QUADS);
			glVertex2f(lr[0], lr[1]);  glVertex2f(lr[2], lr[1]);
			glVertex2f(lr[2], lr[3]);  glVertex2f(lr[0], lr[3]);
		glEnd();
		glColor4f(0.08f, 0.08f, 0.10f, 0.85f);
		glBegin(GL_LINE_LOOP);
			glVertex2f(lr[0]+0.5f, lr[1]+0.5f);  glVertex2f(lr[2]-0.5f, lr[1]+0.5f);
			glVertex2f(lr[2]-0.5f, lr[3]-0.5f);  glVertex2f(lr[0]+0.5f, lr[3]-0.5f);
		glEnd();
	}

	// --- selected tick, dead centre, only once the toggle is actually on
	// (i.e. after a completed press-and-release) ---

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

	mCoverageDirty = true;
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

	// Live during the drag, not just on mouse-up: watching the covered-class
	// count fall as you narrow the range is the whole point of the readout.
	mCoverageDirty = true;
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

	// ONLY THE CARD LIST SCROLLS, and only when the cursor is actually over it.
	// This pane answered the wheel anywhere in the tab, so rolling over the size
	// slider or the weight bars - controls with nothing scrollable about them -
	// moved the list underneath instead of doing nothing, and the whole tab felt
	// like one scrolling surface when only its bottom section is one.
	int b[4];  GetBounds(b);
	if ((float) y > ContentTop(b) || y < b[1]) return 0;

	float line_h = GUI_GetLineHeight(font_UI_Basic);
	float row_h  = line_h + 4;

	// CLAMPED HERE, not only in Draw(). Draw() owning the clamp alone meant every
	// wheel notch past the end still wrote an out-of-range offset and asked for a
	// repaint, which Draw then undid - the log showed a hundred "clamp 54 -> 0" in
	// a list whose content (602px) is shorter than its viewport (636px), i.e. one
	// that cannot scroll at all. That is the jerk: a repaint per notch, changing
	// nothing.
	float  max_scroll = 0.0f;
	if (!mSelectedRamps.empty())
	{
		EnsureRows();

		vector<float> tray_h;  TrayHeights(mRowIcaos, tray_h);
		vector<RowSlot> slots;
		float content_h = LayoutRows(b, mRowIsCard, tray_h, slots);
		float visible_h = ContentTop(b) - (float) b[1];
		if (content_h > visible_h) max_scroll = content_h - visible_h;
	}

	float want = mScrollOffset - dist * row_h * 3;
	if (want < 0)          want = 0;
	if (want > max_scroll) want = max_scroll;

	if (want == mScrollOffset) return 0;	// nowhere to go - let whoever is behind us have it

	mScrollOffset = want;
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

	// This is the edit the readout exists for: ticking an operator off is the
	// cheapest way to empty a stand without noticing.
	mCoverageDirty = true;
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

	int hover_wbar = mSelectedRamps.empty() ? -1 : WeightBarForXY(b, x, y);
	if (hover_wbar != mHoverWeightBar)	{ mHoverWeightBar = hover_wbar;	changed = true; }

	bool over_wbtn = false;
	if (!mSelectedRamps.empty())
	{
		float wb[4];
		WeightButtonRect(b, wb);
		over_wbtn = (x >= wb[0] && x <= wb[2] && y >= wb[1] && y <= wb[3]);
	}
	if (over_wbtn != mHoverWeightButton)	{ mHoverWeightButton = over_wbtn;	changed = true; }

	int row = -1;
	if (!mSelectedRamps.empty() && !over_sort && !over_recommend && !over_clear)
	{
		EnsureRows();
		vector<float> tray_h;  TrayHeights(mRowIcaos, tray_h);
		int r = RowForXY(b, mRowIsCard, tray_h, x, y);
		row = (r >= 0 && r < (int) mRows.size() && mRows[r].kind == wed_Row_Airline) ? r : -1;

		// Hovering a card starts it cycling; leaving stops it and drops the card
		// back to index 0. Keyed by icao so the cycle survives the row list being
		// rebuilt underneath it, which happens on this very call.
		mHoverX = x; mHoverY = y;

		vector<RowSlot> hover_slots;
		LayoutRows(b, mRowIsCard, tray_h, hover_slots);
		int tray_row = TrayRowForXY(mRowIcaos, hover_slots, x, y);
		if (tray_row != mTrayHoverIdx)
		{
			// Leaving a tray row resumes the sequence FROM that aircraft, so
			// stopping to look at one does not cost you your place.
			if (tray_row < 0 && mTrayHoverIdx >= 0) { mCycleShow = mTrayHoverIdx; mCycleAccum = 0.0f; }
			mTrayHoverIdx = tray_row;
			changed = true;
		}
		string want = (row >= 0) ? mRows[row].icao : string();
		if (want != mCycleAirline)
		{
			mCycleAirline = want;
			mCycleShow    = 0;
			mCycleAccum   = 0.0f;
			changed       = true;
			Refresh();
		}
	}
	else if (!mCycleAirline.empty())
	{
		mCycleAirline.clear(); mCycleShow = 0; mCycleAccum = 0.0f;
		changed = true;
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

	// The add/clear button, and the bars, both before the slider - the button
	// overlaps the slider's row, and a press on a bar must not be read as a
	// press on anything underneath it.
	{
		float wb[4];
		WeightButtonRect(b, wb);
		if (x >= wb[0] && x <= wb[2] && y >= wb[1] && y <= wb[3])
		{
			mTrackWeightButton = true;
			Refresh();
			return 1;
		}
	}

	int wbar = WeightBarForXY(b, x, y);
	if (wbar >= 0)
	{
		int w[6];
		if (!SelectionWeights(w))
		{
			// Mixed selection: the first drag unifies it, which is the rule the
			// tri-state operator checkbox already uses. Start from the first
			// ramp's own weights so the gesture has somewhere to stand.
			if (!mSelectedRamps.empty() && !mSelectedRamps[0]->GetClassWeights(w))
				for (int k = 0; k < 6; ++k) w[k] = 0;
		}
		memcpy(mDragWeights,  w, sizeof(w));
		memcpy(mDragWeights0, w, sizeof(w));	// what MouseUp compares against

		mDragWeightBar = wbar;
		mArchive->StartCommand("Set Spawn Weights");

		mDragWeights[wbar] = WeightValueForY(b, y);
		ApplyWeightDrag();
		Refresh();
		return 1;
	}

	// A stand carrying weights has its size derived from them (R23), so the
	// slider is a readout, not a control. Refusing the hit here is the other
	// half of drawing it greyed - a visual-only disable that still responds to
	// clicks is exactly the bug that two-part idiom exists to prevent.
	int handle = SelectionHasWeights() ? -1 : SliderHandleForXY(b, x, y);
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

	// A press anywhere in the content area arms BOTH possible outcomes: a card
	// toggle (if the cursor barely moves before release) and a drag-scroll (if it
	// does). MouseUp decides which actually happened - see its slop check.
	//
	// The whole content scrolls this way now, not just a card block at the top:
	// cards ARE the rows, so restricting the gesture to "the cards" would have
	// meant restricting it to most of the list and then stopping arbitrarily at a
	// section header.
	EnsureRows();
	vector<float> tray_h;  TrayHeights(mRowIcaos, tray_h);

	if (y <= ContentTop(b))
	{
		mContentDragStartY = y;
		mContentDragStartX = x;
		mContentDragStartOffset = mScrollOffset;
	}

	int row = RowForXY(b, mRowIsCard, tray_h, x, y);
	mTrackRow = (row >= 0 && row < (int) mRows.size() &&
				(mRows[row].kind == wed_Row_Airline || mRows[row].kind == wed_Row_Header)) ? row : -1;
	Refresh();
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
		// NOTHING MOVES UNTIL THE GESTURE CLEARS THE SLOP. A click almost always
		// drifts a pixel or two, and scrolling by that much slides the content out
		// from under the cursor between press and release - which is why the tray
		// could not be opened: its bar is 14px tall, so a 2px shift was enough for
		// the release to land on the card body instead and be read as a tick. The
		// card body is 90px tall, so ticking kept working and hid the cause.
		const int kDragSlop = 3;
		if (abs(y - mContentDragStartY) <= kDragSlop &&
			(mContentDragStartX < 0 || abs(x - mContentDragStartX) <= kDragSlop))
			return;

		mScrollOffset = mContentDragStartOffset + (float) (y - mContentDragStartY);
		if (mScrollOffset < 0) mScrollOffset = 0;
		Refresh();
		return;
	}

	// The gesture is locked to the bar it started on. Sliding sideways does NOT
	// paint across the neighbours: an author correcting one class should not
	// discover they have flattened the other five.
	if (mDragWeightBar >= 0)
	{
		int bw[4];
		GetBounds(bw);
		int v = WeightValueForY(bw, y);
		if (v != mDragWeights[mDragWeightBar])
		{
			mDragWeights[mDragWeightBar] = v;
			ApplyWeightDrag();
			Refresh();
		}
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

	if (mTrackWeightButton)
	{
		mTrackWeightButton = false;
		float wb[4];
		WeightButtonRect(b, wb);
		if (x >= wb[0] && x <= wb[2] && y >= wb[1] && y <= wb[3])
		{
			if (SelectionHasWeights())	SwitchToSimpleMode();
			else						SeedWeightsFromSizeRange();
		}
		Refresh();
		return;
	}

	if (mDragWeightBar >= 0)
	{
		// Commit only if something moved. A click that lands on a bar's
		// existing height is a no-op, and a no-op has no business on the undo
		// stack - the same call the map's handle tool makes when a drag turns
		// out to have moved zero pixels.
		if (memcmp(mDragWeights, mDragWeights0, sizeof(mDragWeights)) == 0)
			mArchive->AbortCommand();
		else
			mArchive->CommitCommand();

		mDragWeightBar = -1;
		mCoverageDirty = true;
		Refresh();
		return;
	}

	// A press in the content area arms a drag-scroll, and this used to swallow the
	// release unconditionally - which quietly disabled EVERY click in the list once
	// the cards became the list: section headers would not expand, the lock badge
	// and tray tab did nothing, and operators could not be ticked. Only consume the
	// release when the cursor actually travelled; otherwise it was a click, and the
	// handling below is what it was for.
	if (mContentDragStartY >= 0)
	{
		const int kDragSlop = 3;
		bool moved = (abs(y - mContentDragStartY) > kDragSlop) ||
					 (mContentDragStartX >= 0 && abs(x - mContentDragStartX) > kDragSlop);
		mContentDragStartY = -1;	// scrolling is view state, not document state - nothing to commit
		mContentDragStartX = -1;
		if (moved)
		{
			Refresh();
			return;
		}
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
			SetRowsDirty();
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
			SetRowsDirty();
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

	EnsureRows();
	vector<float> tray_h;  TrayHeights(mRowIcaos, tray_h);

	if (RowForXY(b, mRowIsCard, tray_h, x, y) == mTrackRow &&
		mTrackRow >= 0 && mTrackRow < (int) mRows.size() && mRows[mTrackRow].kind == wed_Row_Header)
	{
		const string & h = mRows[mTrackRow].header_text;
		if (!h.empty() && h != "Selected")		// see the draw side - not a collapsible tier
		{
			if (mCollapsedSections.count(h)) mCollapsedSections.erase(h);
			else                             mCollapsedSections.insert(h);
			SetRowsDirty();
		}
		mTrackRow = -1;
		Refresh();
		return;
	}

	// The lock badge and the tray tab sit ON the card, so they are tested first -
	// otherwise either one would also toggle the operator underneath it.
	if (RowForXY(b, mRowIsCard, tray_h, x, y) == mTrackRow &&
		mTrackRow >= 0 && mTrackRow < (int) mRows.size() && mRows[mTrackRow].kind == wed_Row_Airline)
	{
		vector<RowSlot> slots;
		LayoutRows(b, mRowIsCard, tray_h, slots);
		const string & icao = mRows[mTrackRow].icao;
		const AirlineCard * tc = CardFor(icao);
		size_t ac_livery_count = tc ? tc->abs_paths.size() : 0;
		float lr[4], tr[4];
		LockIconRect(slots[mTrackRow], lr);
		TrayTabRect (slots[mTrackRow], tr);

		LOG_MSG("I/LiveryClick %s at (%d,%d)  tray=[%.0f..%.0f x %.0f..%.0f] hit=%d  lock=%d\n",
				icao.c_str(), x, y, tr[0], tr[2], tr[1], tr[3],
				(int)(x >= tr[0] && x <= tr[2] && y >= tr[1] && y <= tr[3]),
				(int)(x >= lr[0] && x <= lr[2] && y >= lr[1] && y <= lr[3]));

		if (x >= lr[0] && x <= lr[2] && y >= lr[1] && y <= lr[3])
		{
			if (mCardsByType) { mTrackRow = -1; return; }		// GA cards are previews - no lock
			// Exclusive by construction: holding the lock is a single string, so
			// taking it necessarily releases whoever had it.
			mLockedAirline = (mLockedAirline == icao) ? string() : icao;
			mTrackRow = -1;
			Refresh();
			return;
		}
		// THE WHOLE CAPTION ROW IS THE TRAY'S, not just the arrow's gutter, and the
		// picture above it is the only place that selects. Two zones that touch but
		// never overlap: a click either changes what parks here or asks what else
		// this operator has, and there is no pixel where it might do either.
		if (x >= tr[0] && x <= tr[2] && y >= tr[1] && y <= tr[3])
		{
			// A single-livery card has no tray, but its caption row still is not a
			// select target - otherwise the same strip would mean one thing on one
			// card and something else on its neighbour.
			if (ac_livery_count < 2) { mTrackRow = -1; Refresh(); return; }

			if (mTrayAirline == icao)
			{
				mTrayClosing = mTrayAirline;  mTrayClosingOpen = mTrayOpen;
				mTrayAirline.clear();         mTrayOpen = 0.0f;
			}
			else
			{
				// The one already open starts retracting from wherever it is, so
				// the two animations cross over instead of one snapping shut.
				if (!mTrayAirline.empty()) { mTrayClosing = mTrayAirline; mTrayClosingOpen = mTrayOpen; }
				mTrayAirline = icao;  mTrayOpen = 0.0f;
			}
			mTrackRow = -1;
			Refresh();
			return;
		}
	}

	if (RowForXY(b, mRowIsCard, tray_h, x, y) == mTrackRow && mTrackRow < (int) mRows.size() && mRows[mTrackRow].kind == wed_Row_Airline
		&& !mCardsByType)		// GA cards are previews, not a picker - see RebuildAirlineCards
	{
		// SCROLL ANCHORING. Ticking a card can create or grow the "Selected"
		// section ABOVE the viewport, and every row below it then slides down by
		// that much - so the card you just clicked walks out from under the cursor
		// and the whole page appears to jump. The height change is real and wanted;
		// what is not wanted is the viewport staying still while the content moves
		// past it.
		//
		// So the clicked card is the anchor: remember where it sits on screen, let
		// the list change, then shift the scroll offset by however far that same
		// card moved. It ends up exactly where it was, whatever happened above it.
		vector<RowSlot> before;
		LayoutRows(b, mRowIsCard, tray_h, before);
		const string anchor_icao = mRows[mTrackRow].icao;
		const float  anchor_y    = before[mTrackRow].top;

		ToggleCode(anchor_icao);
		SetRowsDirty();

		EnsureRows();

		vector<float> tray2;  TrayHeights(mRowIcaos, tray2);
		vector<RowSlot> after;
		LayoutRows(b, mRowIsCard, tray2, after);

		// The LAST match, not the first: ticking mirrors a copy into "Selected" at
		// the top, and the card the user actually clicked is the original further
		// down. Anchoring on the copy would jump the list to the top instead.
		int found = -1;
		for (size_t i = 0; i < mRows.size(); ++i)
			if (mRows[i].kind == wed_Row_Airline && mRows[i].icao == anchor_icao) found = (int) i;

		if (found >= 0)
		{
			mScrollOffset += anchor_y - after[found].top;
			if (mScrollOffset < 0) mScrollOffset = 0;	// Draw() owns the upper clamp
		}
	}

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
		mAirportCountry.clear();

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
				//
				// A PLACEHOLDER COUNTS AS A MISS. It used to retry only on
				// NotFound, so an airport whose metadata still held a reserved
				// code - "ZZLI" - kept reporting "placeholder, country unknown"
				// after its Airport ID had been corrected to a real one, and the
				// pane looked like it was failing to refresh when it was in fact
				// faithfully reporting stale metadata. A reserved code carries no
				// information by definition, so anything real must outrank it.
				if ((r == wed_Icao_NotFound || r == wed_Icao_Placeholder) &&
					!icao_meta.empty() && icao_meta != icao_primary)
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
					mAirportCountry     = country;	// for the military rule - see LiveryAllowedHere()

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
					// Two different situations reach wed_Icao_Placeholder and they
					// need different sentences. Saying "not set" about a code the
					// user can see filled in reads as a bug in WED rather than as
					// a fact about the code - which is how it read for ZZLI, a
					// deliberate choice from ICAO's reserved range.
					if (icao.empty())
						info_text = "Airport ICAO not set - country unknown, can't weight liveries by region.";
					else
						info_text = "\"" + icao + "\" is a placeholder or reserved ICAO code - country unknown, can't weight liveries by region.";
					warn = true;
				}
				else if (r == wed_Icao_IndexUnavailable)
				{
					// Say WHICH way it failed. A file that is present but whose
					// header was edited used to report as "not found", sending
					// people to look for a file sitting in front of them.
					info_text = string("WED_AirportDatabase.txt: ") +
								WedDataFileErrorText(mAirportDb.LoadError()) +
								" - can't look up country.";
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
			const char * lbl = kFilterLabels[i];
			float tw = GUI_MeasureRange(font_UI_Basic, lbl, lbl + strlen(lbl));
			if (tw > chip_w - 6.0f)			// too tight - fall back to the short form
			{
				lbl = kFilterLabelsShort[i];
				tw  = GUI_MeasureRange(font_UI_Basic, lbl, lbl + strlen(lbl));
			}
			GUI_FontDraw(state, font_UI_Basic, txt_col, cx0 + (chip_w - tw) * 0.5f, (chip_top + chip_bot) * 0.5f - line_h * 0.35f, lbl);
		}
	}

	// --- size range slider ---
	if (SliderHeight() > 0)		// collapsed to nothing while the stand has weights
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
			// Clamped: the outer two labels are centred on the track's own
			// endpoints, which sit close enough to the pane edge that half a
			// glyph fell outside it.
			float lx = fx - tw * 0.5f;
			if (lx < (float) b[0] + 2)          lx = (float) b[0] + 2;
			if (lx + tw > (float) b[2] - 2)     lx = (float) b[2] - 2 - tw;
			GUI_FontDraw(state, font_UI_Basic, lbl_col, lx, slider_top - line_h * 1.9f, kWidthLabels[i]);
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

		// Once weights exist the size letter is DERIVED from them (R23), so
		// this control is a readout. Mask it the way the disabled Recommend
		// button is masked - the hit tests already refuse it, and this is the
		// half that says so.
		if (SelectionHasWeights())
		{
			state->SetState(0,0,0,0,1,0,0);
			glColor4f(0.0f, 0.0f, 0.0f, 0.55f);
			glBegin(GL_QUADS);
				glVertex2f((float) b[0] + 1, slider_bot);
				glVertex2f((float) b[2] - 1, slider_bot);
				glVertex2f((float) b[2] - 1, slider_top);
				glVertex2f((float) b[0] + 1, slider_top);
			glEnd();
			// Centred in the zone rather than jammed against its bottom edge,
			// where it landed on the track and the A-F labels and was
			// unreadable through the mask.
			state->SetState(0,0,0,0,0,0,0);
			const char * drv = "Size is derived from the weights below";
			float dw = GUI_MeasureRange(font_UI_Basic, drv, drv + strlen(drv));
			float dcol[4] = { 0.92f, 0.92f, 0.94f, 1.0f };
			GUI_FontDraw(state, font_UI_Basic, dcol,
						 ((float) b[0] + (float) b[2]) * 0.5f - dw * 0.5f,
						 (slider_bot + slider_top) * 0.5f - line_h * 0.35f, drv);
		}
	}

	// --- spawn weight bars (apt.dat row 1313) ---
	// Only drawn when the selection actually has weights; WeightsHeight()
	// collapses the section to nothing otherwise, and the button below is how
	// an author gets here from there.
	if (!mSelectedRamps.empty() && WeightsHeight() > 0)
	{
		float wtop, wbot;
		WeightsYRange(b, wtop, wbot);

		int w[6];
		const bool uniform = SelectionWeights(w);
		if (!uniform) for (int i = 0; i < 6; ++i) w[i] = 0;
		const int track_max = WeightTrackMax();

		int total = 0;
		for (int i = 0; i < 6; ++i) total += w[i];

		// zone background, matching the slider and the readout above and below
		state->SetState(0,0,0,0,0,0,0);
		glColor4f(0.14f, 0.14f, 0.14f, 1.0f);
		glBegin(GL_QUADS);
			glVertex2f((float) b[0] + 1, wbot);
			glVertex2f((float) b[2] - 1, wbot);
			glVertex2f((float) b[2] - 1, wtop);
			glVertex2f((float) b[0] + 1, wtop);
		glEnd();
		glColor4f(0.40f, 0.40f, 0.40f, 1.0f);
		glBegin(GL_LINE_LOOP);
			glVertex2f((float) b[0] + 1, wbot);
			glVertex2f((float) b[2] - 1, wbot);
			glVertex2f((float) b[2] - 1, wtop);
			glVertex2f((float) b[0] + 1, wtop);
		glEnd();

		float * hdr_col = WED_Color_RGBA(wed_Header_Text);
		float * lbl_col = WED_Color_RGBA(wed_Table_Text);
		float   dim[4]  = { 0.62f, 0.62f, 0.62f, 1.0f };

		GUI_FontDraw(state, font_UI_Basic, hdr_col, b[0] + pad, wtop - line_h * 0.9f,
					 uniform ? "Spawn Distribution (relative)" : "Spawn Distribution - selection differs");

		for (int i = 0; i < 6; ++i)
		{
			float r[4];
			WeightBarRect(b, i, r);
			float full_h = r[3] - r[1];
			float frac   = (track_max > 0) ? (float) w[i] / (float) track_max : 0.0f;
			float fill_t = r[1] + full_h * frac;

			const bool hot = (i == mHoverWeightBar) || (i == mDragWeightBar);

			// the empty column, so a zero bar is still a visible drop target
			state->SetState(0,0,0,0,0,0,0);
			glColor4f(0.20f, 0.20f, 0.20f, 1.0f);
			glBegin(GL_QUADS);
				glVertex2f(r[0], r[1]); glVertex2f(r[0], r[3]);
				glVertex2f(r[2], r[3]); glVertex2f(r[2], r[1]);
			glEnd();

			if (w[i] > 0)
			{
				if (hot) glColor4f(0.42f, 0.72f, 1.00f, 1.0f);
				else     glColor4f(0.30f, 0.60f, 0.90f, 1.0f);
				glBegin(GL_QUADS);
					glVertex2f(r[0], r[1]); glVertex2f(r[0], fill_t);
					glVertex2f(r[2], fill_t); glVertex2f(r[2], r[1]);
				glEnd();
			}

			glColor4f(hot ? 0.85f : 0.45f, hot ? 0.85f : 0.45f, hot ? 0.85f : 0.45f, 1.0f);
			glBegin(GL_LINE_LOOP);
				glVertex2f(r[0], r[1]); glVertex2f(r[0], r[3]);
				glVertex2f(r[2], r[3]); glVertex2f(r[2], r[1]);
			glEnd();

			// class letter, then the share this bar represents - the share is
			// what the author is actually reasoning about, the integer is just
			// how it gets stored.
			float cx = (r[0] + r[2]) * 0.5f;
			float tw = GUI_MeasureRange(font_UI_Basic, kWidthLabels[i], kWidthLabels[i] + 1);
			GUI_FontDraw(state, font_UI_Basic, lbl_col, cx - tw * 0.5f, r[1] - line_h - 2, kWidthLabels[i]);

			char pct[16];
			if (!uniform)        snprintf(pct, sizeof(pct), "--");
			else if (total <= 0) snprintf(pct, sizeof(pct), "0%%");
			else                 snprintf(pct, sizeof(pct), "%d%%", (int) (100.0f * w[i] / total + 0.5f));
			float pw = GUI_MeasureRange(font_UI_Basic, pct, pct + strlen(pct));
			GUI_FontDraw(state, font_UI_Basic, (w[i] > 0) ? lbl_col : dim,
						 cx - pw * 0.5f, r[1] - line_h * 2 - 3, pct);
		}

		if (uniform && total == 0)
			GUI_FontDraw(state, font_UI_Basic, dim, b[0] + pad + 200, wtop - line_h * 0.9f,
						 "- all zero: nothing parks here, deliberately");
	}

	// --- the add/clear button, on the slider's row ---
	if (!mSelectedRamps.empty())
	{
		float wb[4];
		WeightButtonRect(b, wb);
		const bool has = SelectionHasWeights();

		state->SetState(0,0,0,0,0,0,0);
		float k = mTrackWeightButton ? 0.82f : (mHoverWeightButton ? 1.15f : 1.0f);
		glColor4f(0.26f * k, 0.30f * k, 0.36f * k, 1.0f);
		glBegin(GL_QUADS);
			glVertex2f(wb[0], wb[1]); glVertex2f(wb[0], wb[3]);
			glVertex2f(wb[2], wb[3]); glVertex2f(wb[2], wb[1]);
		glEnd();
		glColor4f(0.55f, 0.55f, 0.58f, 1.0f);
		glBegin(GL_LINE_LOOP);
			glVertex2f(wb[0], wb[1]); glVertex2f(wb[0], wb[3]);
			glVertex2f(wb[2], wb[3]); glVertex2f(wb[2], wb[1]);
		glEnd();

		// "Simple Mode", not "Clear": the weights are stashed, not destroyed,
		// and the button's job is to say which of the two controls is in charge.
		const char * cap = has ? "Simple Mode" : "Set Spawn Weights";
		float cw = GUI_MeasureRange(font_UI_Basic, cap, cap + strlen(cap));
		GUI_FontDraw(state, font_UI_Basic, WED_Color_RGBA(wed_Table_Text),
					 (wb[0] + wb[2]) * 0.5f - cw * 0.5f, wb[1] + 4, cap);
	}

	// --- coverage readout (WED_LiveryFormatSpec.md §4.5) ---
	// Sits directly under the size slider because those are its two inputs: the
	// operators ticked below, measured against the size range set above. An edit
	// to either updates this line on the same frame.
	{
		float cov_top, cov_bot;
		CoverageYRange(b, cov_top, cov_bot);

		// One flag drives both. The cards and the readout are answers to the
		// same query, so recomputing one without the other is exactly how they
		// would drift back into contradicting each other.
		if (mCoverageDirty) { RecomputeCoverage(); RebuildAirlineCards(); SetRowsDirty(); }

		float col_warn[4]  = { 1.00f, 0.45f, 0.35f, 1.0f };	// a stand that parks nothing
		float col_good[4]  = { 0.55f, 0.85f, 0.55f, 1.0f };
		float col_muted[4] = { 0.62f, 0.62f, 0.62f, 1.0f };

		char head[256]; char detail[256];
		float * head_col = col_muted;

		head[0] = 0; detail[0] = 0;

		if (mSelectedRamps.empty())
		{
			snprintf(head,   sizeof(head),   "Coverage");
			snprintf(detail, sizeof(detail), "Select a ramp start to see what can park on it.");
		}
		else if (!mCoverage.index_ready)
		{
			// Explicitly NOT "0 operators" - spec §6.4 calls out a missing or
			// mismatched index as the failure that costs a day precisely because
			// its only symptom is things quietly not appearing. A readout that
			// printed a confident zero here would be worse than none at all.
			snprintf(head,   sizeof(head),   "Coverage unavailable - livery index not loaded");
			snprintf(detail, sizeof(detail), "Looked for livery_index.txt under the selected X-Plane folder.");
		}
		else if (mCoverage.stands == 1)
		{
			char range[8];
			if (mCoverage.lo_class == mCoverage.hi_class)
				snprintf(range, sizeof(range), "%c", mCoverage.lo_class);
			else
				snprintf(range, sizeof(range), "%c-%c", mCoverage.lo_class, mCoverage.hi_class);

			if (mCoverage.airlines_listed == 0)
			{
				snprintf(head,   sizeof(head),   "This stand parks nothing - no operators listed");
				head_col = col_warn;
				snprintf(detail, sizeof(detail), "Tick an operator below. Size range is %s.", range);
			}
			else if (mCoverage.weighted)
			{
				// The §4.5 sentence. The percentage is OCCUPANCY - how often the
				// stand has an aircraft at all - not "the chance of this
				// operator", which on a single-operator stand is always 100% and
				// carries no information.
				const int pct = (int) (mCoverage.p_occupied * 100.0f + 0.5f);
				if (pct == 0)
				{
					// Three ways to park nothing, and they are NOT the same
					// news. Saying "all zero, or nothing fits" would name both
					// and choose neither, which is the §4.5 complaint restated
					// rather than answered.
					switch (mCoverage.empty_cause)
					{
					case Coverage::empty_ByChoice:
						snprintf(head, sizeof(head), "This stand will not spawn any static aircraft");
						head_col = col_muted;			// deliberate - not a warning
						snprintf(detail, sizeof(detail),
							"Every class weight is zero. That is a valid way to say a stand stays empty.");
						break;

					case Coverage::empty_NoArtYet:
						snprintf(head, sizeof(head), "Nothing can park here yet - no aircraft exists at size %s", range);
						head_col = col_muted;			// ahead of the art, not wrong (R14)
						snprintf(detail, sizeof(detail),
							"X-Plane ships no aircraft at all at this size. The stand starts working the day one does, with no edit here.");
						break;

					case Coverage::empty_OutOfRange:
						snprintf(head, sizeof(head), "This stand parks nothing - nothing listed can reach it");
						head_col = col_warn;
						snprintf(detail, sizeof(detail),
							"The %d listed operators fly size %s, but none can reach here from a hub. List one based nearer.",
							mCoverage.airlines_listed, range);
						break;

					default:
						snprintf(head, sizeof(head), "This stand parks nothing - and that looks unintended");
						head_col = col_warn;
						snprintf(detail, sizeof(detail),
							"None of the %d listed operators has an aircraft at size %s, though other operators do. Widen the size range, or list one that flies it.",
							mCoverage.airlines_listed, range);
						break;
					}
				}
				else
				{
					snprintf(head, sizeof(head),
						"This stand will spawn aircraft %d%% of the time", pct);
					head_col = (pct >= 95) ? col_good : col_warn;
					snprintf(detail, sizeof(detail),
						"Weighted across %s, from %d of %d listed operators. Empty the other %d%%.",
						range, mCoverage.airlines_eligible, mCoverage.airlines_listed, 100 - pct);
				}
			}
			else if (mCoverage.stands_empty > 0)
			{
				snprintf(head,   sizeof(head),   "This stand parks nothing");
				head_col = col_warn;
				snprintf(detail, sizeof(detail),
					"None of the %d listed operators has a model at size %s. Widen the range, or list an operator that flies one.",
					mCoverage.airlines_listed, range);
			}
			else
			{
				snprintf(head,   sizeof(head),   "Parks aircraft from %d of %d listed operators",
					mCoverage.airlines_eligible, mCoverage.airlines_listed);
				head_col = col_good;
				snprintf(detail, sizeof(detail), "%d of %d sizes in %s can be filled.",
					mCoverage.classes_filled, mCoverage.classes_in_range, range);
			}
		}
		else if (mCoverage.stands_empty > 0)
		{
			// The bulk case, which is the one spec §4.5 says has no eyes on it.
			snprintf(head,   sizeof(head),   "%d of %d selected stands park nothing",
				mCoverage.stands_empty, mCoverage.stands);
			head_col = col_warn;
			snprintf(detail, sizeof(detail), "Each stand measured against its own size range and operator list.");
		}
		else
		{
			snprintf(head,   sizeof(head),   "All %d selected stands can be filled", mCoverage.stands);
			head_col = col_good;
			snprintf(detail, sizeof(detail), "Each stand measured against its own size range and operator list.");
		}

		// Variety collapse outranks the ordinary "it works" line, because from
		// the author's side nothing looks wrong: the stand spawns aircraft, the
		// occupancy reads high, and every one of them is the same airline. It
		// does NOT outrank an empty stand - that is still the worse news - so
		// only a healthy-looking readout gets replaced.
		if (!mCoverage.sole_operator.empty() && head_col != col_warn)
		{
			string nice = mCoverage.sole_operator;
			for (size_t i = 0; i < nice.size(); ++i) nice[i] = (char) toupper((unsigned char) nice[i]);

			char rng[8];
			if (mCoverage.lo_class == mCoverage.hi_class)
				snprintf(rng, sizeof(rng), "%c", mCoverage.lo_class);
			else
				snprintf(rng, sizeof(rng), "%c-%c", mCoverage.lo_class, mCoverage.hi_class);

			snprintf(head, sizeof(head), "Only %s will ever park here", nice.c_str());
			head_col = col_warn;
			snprintf(detail, sizeof(detail),
				"The other %d listed operators have no aircraft at size %s, so every aircraft on this stand is the same airline.",
				mCoverage.airlines_listed - 1, rng);
		}

		// What the range rule removed at THIS stand, named. Without this the author
		// sees United's card shrink to a 777 and has no way to know whether that is
		// the index, the weights, or a bug. One clause per operator, types joined.
		// Only the operators LISTED ON THIS STAND. mRangeHidden is filled for every
		// card the pane builds - the whole index - so unfiltered it opened with
		// American and Cargojet on a stand that lists Southwest and easyJet. The
		// line exists to explain why a listed operator's card shrank or vanished;
		// what happened to operators the author never chose is not its business.
		string range_line;
		if (!mRangeHidden.empty() && mSelectedRamps.size() == 1)
		{
			set<string> listed_lc = ParseCodes(mSelectedRamps[0]->GetAirlines());
			string s;
			int n = 0;
			for (map<string, vector<string> >::const_iterator i = mRangeHidden.begin(); i != mRangeHidden.end(); ++i)
			{
				string lc = i->first;
				for (size_t c = 0; c < lc.size(); ++c) lc[c] = (char) tolower((unsigned char) lc[c]);
				if (!listed_lc.count(lc)) continue;
				if (n == 4) { s += " ..."; break; }
				s += (n ? "; " : "") + i->first + " ";
				for (size_t k = 0; k < i->second.size(); ++k)
					s += (k ? "/" : "") + i->second[k];
				++n;
			}
			if (n) range_line = "Out of range from their hubs, not offered: " + s + ".";
		}

		// §4.5: the readout MUST name what it resolved against. Appended rather
		// than given its own line, because on a correctly generated index it is
		// reassurance, and on one carrying no stamps at all it is the only
		// warning §6.4's silent mismatch will ever produce.
		// The index version stamp used to be appended here. It is diagnostic, not
		// guidance, and it doubled the length of every readout; it still goes to
		// the log, which is where a build mismatch gets investigated anyway.

		// Wrap to the width we actually have, then size the section to the result
		// BEFORE laying it out - so the background, the lines and everything below
		// agree on this frame. (One long line ran off the right edge on every
		// stand that had anything to say.)
		float avail_w = (float) (b[2] - b[0]) - pad * 2;
		vector<string> body = WrapText(font_UI_Basic, detail, avail_w);
		if (!range_line.empty())
		{
			vector<string> more = WrapText(font_UI_Basic, range_line, avail_w);
			body.insert(body.end(), more.begin(), more.end());
		}
		mCoverageLineCount = 1 + (int) body.size();
		CoverageYRange(b, cov_top, cov_bot);

		// zone background, matching the slider's so the two read as one stack
		state->SetState(0,0,0,0,0,0,0);
		glColor4f(0.14f, 0.14f, 0.14f, 1.0f);
		glBegin(GL_QUADS);
			glVertex2f((float) b[0] + 1, cov_bot);
			glVertex2f((float) b[2] - 1, cov_bot);
			glVertex2f((float) b[2] - 1, cov_top);
			glVertex2f((float) b[0] + 1, cov_top);
		glEnd();
		glColor4f(0.40f, 0.40f, 0.40f, 1.0f);
		glBegin(GL_LINE_LOOP);
			glVertex2f((float) b[0] + 1, cov_bot);
			glVertex2f((float) b[2] - 1, cov_bot);
			glVertex2f((float) b[2] - 1, cov_top);
			glVertex2f((float) b[0] + 1, cov_top);
		glEnd();

		GUI_FontDraw(state, font_UI_Basic, head_col,  b[0] + pad, cov_top - line_h * 0.9f, head);
		for (size_t li = 0; li < body.size(); ++li)
			GUI_FontDraw(state, font_UI_Basic, col_muted, b[0] + pad,
						 cov_top - line_h * (1.9f + (float) li), body[li].c_str());
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
		// cache/evict design). ---
		//
		// An empty strip is an ANSWER, not a blank. A stand whose operators have
		// nothing at its classes gets no cards, and saying so here is what stops
		// the strip contradicting the readout directly above it - which is
		// exactly what it used to do, showing four aircraft under the words
		// "this stand parks nothing".

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
			EnsureRows();
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
			// away most of it) just as easily as a resize can. Airline rows are
			// cards and are several lines tall, so the total comes from the layout
			// pass rather than from a row count times a row height.
			vector<float> tray_h;  TrayHeights(mRowIcaos, tray_h);

			vector<RowSlot> slots;
			float content_h  = LayoutRows(b, mRowIsCard, tray_h, slots);
			float visible_h  = top - (float) b[1];
			float max_scroll = (content_h > visible_h) ? (content_h - visible_h) : 0.0f;
			if (mScrollOffset < 0)          mScrollOffset = 0;
			if (mScrollOffset > max_scroll)
			{
				LOG_MSG("I/LiveryScroll clamp %.0f -> %.0f  (content=%.0f visible=%.0f rows=%d)\n",
						mScrollOffset, max_scroll, content_h, visible_h, (int) mRows.size());
				mScrollOffset = max_scroll;
			}
			if (mScrollOffset != 0.0f)		// the clamp may have moved it - relay out
				content_h = LayoutRows(b, mRowIsCard, tray_h, slots);

			// Thumbnails are rendered at most kMaxRendersPerFrame per frame and
			// everything NOT on screen is evicted at the end, so scrolling a long
			// section does not render a hundred aircraft. The keep-alive set has to
			// hold the SAME strings GetThumbnail() is called with or every entry
			// evicts and re-renders every frame - which looks like a performance
			// mystery rather than a mismatch.
			// A LOCK MUST NOT OUTLIVE ITS CARD. The badge is the only way to
			// release it, so if its operator drops out of the list - a search term,
			// a weight drag that removes its class, a different X-Plane folder -
			// every other card stays dimmed forever with nothing left to click. The
			// same goes for a tray left open on a card that is no longer drawn.
			if (!mLockedAirline.empty() && !CardFor(mLockedAirline))  mLockedAirline.clear();
			if (!mTrayAirline.empty()   && !CardFor(mTrayAirline))    { mTrayAirline.clear();  mTrayOpen = 0.0f; }
			if (!mTrayClosing.empty()   && !CardFor(mTrayClosing))    { mTrayClosing.clear();  mTrayClosingOpen = 0.0f; }
			if (!mCycleAirline.empty()  && !CardFor(mCycleAirline))   mCycleAirline.clear();

			int         renders_this_frame = 0;
			set<string> keep_alive_paths;
			mHoverTipText.clear();		// re-decided below, per frame, by whatever is under the cursor

			// Clip to the content viewport. GUI_Pane::InternalDraw() only scissors to
			// the WHOLE PANE's bounds, not this section's, so without this a card
			// scrolled half past the top paints its full image quad straight over the
			// toolbar and slider above it - which is why the draw loop used to skip
			// anything not entirely inside, and why cards vanished at the border.
			// Clamp both extents to >= 0: ContentTop() subtracts a chain of fixed
			// section heights from the pane's top edge, so dragging the property panel
			// short can put it BELOW the pane bottom, and glScissor turns a negative
			// extent into GL_INVALID_VALUE and then an assert in a debug build.
			glPushAttrib(GL_SCISSOR_BIT);
			glEnable(GL_SCISSOR_TEST);
			{
				int sc_w = (int) (b[2] - b[0]);
				int sc_h = (int) (top - (float) b[1]);
				if (sc_w < 0) sc_w = 0;
				if (sc_h < 0) sc_h = 0;
				glScissor((int) b[0], (int) b[1], sc_w, sc_h);
			}

			// KEEP-ALIVE IS A WIDER WINDOW THAN WHAT IS DRAWN, and it is collected in
			// its own pass because the draw loop below breaks out the moment it goes
			// off the bottom. Built from the visible rows alone - which is what
			// shipped - a card evicted the instant it scrolled out was re-rendered
			// the instant it scrolled back in, so dragging the pane taller re-rendered
			// everything it revealed AND everything it had just pushed past. That is
			// the 0.5-0.8s stall: not one slow thumbnail, but the same thumbnails
			// being thrown away and rebuilt.
			//
			// A card and a half beyond the border in each direction - far enough that
			// a card begins rendering well before it is needed and is not dropped the
			// moment it leaves, which is the window the eviction below uses too.
			const float kOffscreenMargin = CardHeight(b) * 1.5f;
			{
				float keep_hi = top + kOffscreenMargin;
				float keep_lo = (float) b[1] - kOffscreenMargin;
				for (size_t vi = 0; vi < mRows.size(); ++vi)
				{
					if (mRows[vi].kind != wed_Row_Airline)  continue;
					if (slots[vi].bot > keep_hi)           continue;
					if (slots[vi].top < keep_lo)           break;		// everything below is further away
					const AirlineCard * ac = CardFor(mRows[vi].icao);
					if (!ac || ac->abs_paths.empty()) continue;

					// EVERY livery of the card being cycled, not just the one on its
					// face. Keeping only the visible one meant each tick of the hover
					// cycle evicted the aircraft it had just finished showing, and
					// wrapping round re-rendered it - 44 renders for 12 objects in one
					// short session. The parse is cached by then, but the texture
					// allocation and the offscreen pass are not.
					if (mRows[vi].icao == mCycleAirline)
						for (size_t k = 0; k < ac->abs_paths.size(); ++k)
							keep_alive_paths.insert(ac->abs_paths[k]);
					else
						keep_alive_paths.insert(ac->abs_paths[0]);
				}
			}

			for (size_t vi = 0; vi < mRows.size(); ++vi)
			{
				const WED_LiveryDisplayRow & row = mRows[vi];
				float row_top = slots[vi].top;
				float row_bot = slots[vi].bot;
				// OVERLAP, not containment. Testing "is it entirely inside" made a
				// card vanish the instant its edge crossed the border instead of
				// being clipped by the scissor below, which is the whole reason that
				// scissor exists. The margin means it also starts rendering a card
				// and a half early, so the work is done before it is looked at.
				if (slots[vi].top < (float) b[1] - kOffscreenMargin) break;	// this and everything below are far off
				if (slots[vi].slot_bot > top + kOffscreenMargin) continue;	// far above; a later row may still be near

				if (row.kind == wed_Row_Gap)
					continue;

				if (row.kind == wed_Row_Airline)
				{
					const AirlineCard * ac = CardFor(row.icao);
					if (!ac) continue;							// nothing modelled - no card to draw

					map<string,int>::const_iterator cc = code_counts.find(row.icao);
					const int  n_with = (cc == code_counts.end()) ? 0 : cc->second;

					// At rest a card shows index 0 - the operator's largest aircraft
					// here. While hovered it steps through the rest, wrapping against
					// THIS card's length rather than whatever length it had when the
					// cursor arrived: a weight drag can shorten it mid-cycle.
					int show = 0;
					if (!ac->abs_paths.empty())
					{
						// Pointing at a tray row HOLDS the face on that aircraft;
						// otherwise the hover cycle drives it.
						if (row.icao == mTrayAirline && mTrayHoverIdx >= 0 &&
							mTrayHoverIdx < (int) ac->abs_paths.size())
							show = mTrayHoverIdx;
						else if (row.icao == mCycleAirline)
							show = mCycleShow % (int) ac->abs_paths.size();
					}

					const bool locked = (!mLockedAirline.empty() && row.icao == mLockedAirline);
					const bool dimmed = (!mLockedAirline.empty() && row.icao != mLockedAirline);

					// WHAT THE TIP SAYS, decided here because this is where the
					// card's sub-rectangles are known. Most specific target wins:
					// the lock and the tray tab both sit on the card, so a generic
					// "here is what they fly" would otherwise shadow the two
					// controls the cursor is actually on.
					if ((int) vi == mHoverRow)
					{
						float lr[4], tr[4];
						LockIconRect(slots[vi], lr);
						TrayTabRect (slots[vi], tr);

						// A lock badge is small, so its target is grown by a few
						// percent of the card - enough to forgive a near miss
						// without reaching the tray tab below it.
						float grow = (std::max)(3.0f, (slots[vi].x1 - slots[vi].x0) * 0.03f);

						if (mHoverX >= lr[0] - grow && mHoverX <= lr[2] + grow &&
							mHoverY >= lr[1] - grow && mHoverY <= lr[3] + grow)
						{
							mHoverTipText = locked ? "Spawn every listed operator again"
												   : "Spawn this operator only";
						}
						else if (mHoverX >= tr[0] && mHoverX <= tr[2] &&
								 mHoverY >= tr[1] && mHoverY <= tr[3] &&
								 ac->labels.size() > 1)
						{
							mHoverTipText = "Show all of this operator's aircraft";
						}
						else if (ac->labels.size() > 1)
						{
							string t = ac->name + ": ";
							for (size_t k = 0; k < ac->labels.size(); ++k)
							{
								if (k) t += ", ";
								t += ac->labels[k];
							}
							mHoverTipText = t;
						}
					}

					keep_alive_paths.insert(ac->abs_paths[show]);
					float tray_frac = (mRows[vi].icao == mTrayAirline)    ? mTrayOpen
									: (mRows[vi].icao == mTrayClosing)    ? mTrayClosingOpen
									: 0.0f;

					DrawAirlineCard(state, slots[vi], *ac, show,
									n_with == n_ramps, (int) vi == mHoverRow,
									(int) vi == mTrackRow, locked, dimmed,
									tray_frac, renders_this_frame);

					if (row.icao == mTrayAirline && mTrayOpen > 0.0f)
						DrawCardTray(state, slots[vi], *ac, mTrayOpen, show);
					else if (row.icao == mTrayClosing && mTrayClosingOpen > 0.0f)
						DrawCardTray(state, slots[vi], *ac, mTrayClosingOpen, -1);
					continue;
				}

				if (row.kind == wed_Row_Note)
				{
					// Indented past the section header and dimmed - it is an
					// explanation, not a selectable entry. Elided against the row's
					// own width so a narrow property panel cannot push it outside
					// the border (same GUI_MeasureRange approach the card captions
					// use, no new machinery).
					float note_col[4] = { 0.62f, 0.62f, 0.64f, 1.0f };
					float note_x = b[0] + pad + 14;
					string note = ElideToWidth(font_UI_Basic, row.header_text, (float) b[2] - pad - note_x);
					GUI_FontDraw(state, font_UI_Basic, note_col, note_x,
									row_bot + (row_h - line_h) * 0.5f, note.c_str());
					continue;
				}

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
					// A COLLAPSIBLE SECTION IS DRAWN AS A CONTROL, not as a line of
					// text that happens to be clickable. It gets a band, a chevron
					// and its count, because the only thing that previously told the
					// reader it could be opened was a parenthetical - which reads as
					// a caption, not a button, and left the section looking like one
					// more label in a list of labels.
					// EVERY section is collapsible and every one looks it. Only "All
					// Airlines" drew the box and chevron before, yet MouseUp has
					// always toggled ANY header - so collapsing "Popular Airlines"
					// left a bare label with its cards gone and no hint that it was
					// shut, which reads as a section that lost its contents.
					bool collapsible = !row.header_text.empty() && row.header_text != "Selected";
					bool collapsed   = mCollapsedSections.count(row.header_text) != 0;
					float tx = (float) b[0] + pad;

					if (collapsible)
					{
						// A filled box with its own outline, full width. The faint
						// wash it had before was invisible against the panel, so the
						// section still read as a line of text - which is the thing
						// being fixed.
						state->SetState(0,0,0,0,1,0,0);
						glColor4f(1.0f, 1.0f, 1.0f, (int) vi == mHoverRow ? 0.16f : 0.09f);
						glBegin(GL_QUADS);
							glVertex2f((float) b[0] + kCardGap, row_bot);
							glVertex2f((float) b[2] - kCardGap, row_bot);
							glVertex2f((float) b[2] - kCardGap, row_top);
							glVertex2f((float) b[0] + kCardGap, row_top);
						glEnd();
						glColor4f(1.0f, 1.0f, 1.0f, 0.22f);
						glBegin(GL_LINE_LOOP);
							glVertex2f((float) b[0] + kCardGap + 0.5f, row_bot + 0.5f);
							glVertex2f((float) b[2] - kCardGap - 0.5f, row_bot + 0.5f);
							glVertex2f((float) b[2] - kCardGap - 0.5f, row_top - 0.5f);
							glVertex2f((float) b[0] + kCardGap + 0.5f, row_top - 0.5f);
						glEnd();

						// Same convention as the card's tray arrow: right when shut,
						// down when open. One gesture, one shape, two places.
						float acx = tx + 5.0f, acy = (row_top + row_bot) * 0.5f;
						float lon = 4.5f, lat = 2.8f;
						float ang = collapsed ? 0.0f : -1.57079633f;
						float ca = cosf(ang), sa = sinf(ang);
						const float px[3] = {  lon, -lon * 0.55f, -lon * 0.55f };
						const float py[3] = { 0.0f, -lat,          lat         };
						glColor4f(0.85f, 0.85f, 0.88f, 1.0f);
						glBegin(GL_TRIANGLES);
							for (int i = 0; i < 3; ++i)
								glVertex2f(acx + px[i] * ca - py[i] * sa,
										   acy + px[i] * sa + py[i] * ca);
						glEnd();
						tx += 16.0f;
					}

					string htxt = row.header_text;
					if (collapsible && collapsed)
					{
						char n[48];
						snprintf(n, sizeof(n), "   %d", row.hidden_count);
						htxt += n;
					}
					GUI_FontDraw(state, font_UI_Basic, row_col, tx, row_bot + (row_h - line_h) * 0.5f, htxt.c_str());
					// Every section except the last ("All Airlines") is some flavor of
					// recommendation (manual pin, direct hit, same country, or popular fleet) -
					// star all of them, same as the old two-tier layout starred "Recommended".
					if (row.header_text != "All Airlines" && row.header_text != "Selected")
					{
						float hw = GUI_MeasureRange(font_UI_Basic, row.header_text.c_str(), row.header_text.c_str() + row.header_text.size());
						glColor4f(1.0f, 0.85f, 0.2f, 1.0f);
						DrawStar(b[0] + pad + hw + line_h * 0.45f, (row_top + row_bot) * 0.5f, line_h * 0.4f, line_h * 0.17f);
					}
					continue;
				}

			}

			glPopAttrib();		// restores GL_SCISSOR_TEST enable + rect to whatever they were on entry

			// After the clip, deliberately: a tip has to be allowed outside the box
			// that spawned it, and a card near the bottom has nowhere else to put it.
			DrawHoverTip(state, b);

			mThumbCache.EvictNotVisible(keep_alive_paths);

			// Rendering is capped per frame, so a screenful that is entirely cold
			// fills in over the next few frames instead of blocking one of them for
			// all of it. Asking for the next frame here is what keeps that going;
			// it stops on its own once everything visible is cached, because then
			// the cap is never reached.
			if (renders_this_frame >= kMaxRendersPerFrame) Refresh();

			// Same mechanism, different reason: while a tray is sliding or a card is
			// cycling, ask for the next frame. Nothing is scheduled when nothing
			// moves, so an idle pane goes quiet.
			if (StepAnimation()) Refresh();

			if (mRows.empty())
			{
				// Name the actual reason. One fixed sentence about operation types
				// used to be shown for every empty list, including one the user had
				// just emptied by typing in the search box - which reads as a bug in
				// the tool rather than as an answer.
				string why;
				if (!mAirlineDirectory.IsLoaded())
					why = "Airline database unavailable - see the warning shown at startup.";
				else if (!mSearchQuery.empty())
					why = "No operator matches \"" + mSearchQuery + "\".";
				else if (cur_op_enum == ramp_operation_None)
					why = "Set a ramp operation type above to see operators.";
				else
					why = "No operators are tagged for this operation type yet.";

				// Drawn BELOW the card strip, not at ContentTop - that is where the
				// cards themselves start, so this text used to be painted on top of
				// card one.
				float msg_y = top + mScrollOffset - line_h;
				string msg = ElideToWidth(font_UI_Basic, why, (float) b[2] - pad - (b[0] + pad));
				GUI_FontDraw(state, font_UI_Basic, row_col, b[0] + pad, msg_y, msg.c_str());
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
