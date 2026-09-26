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

#include "WED_LiveryPaneInternal.h"

// Building the list of rows, and the small helpers every part of the pane uses.

namespace livery_pane {

	const int kFilterEnumTable[5]  = { ramp_operation_None, ramp_operation_GeneralAviation, ramp_operation_Airline, ramp_operation_Cargo, ramp_operation_Military };
	// These name the OPERATION at the stand, not the aircraft - that is what the
	// Equipment Type list is for. A PC-12 flying a scheduled service is Passenger;
	// a 737 BBJ is Private/BizJet. UI ONLY: apt.dat still writes none /
	// general_aviation / airline / cargo / military, and always will.
	//   Private/BizJet <- general_aviation      Passenger <- airline
	// The short forms are drawn when a chip is too narrow for the full label,
	// which at a fifth of the pane width "Private/BizJet" often is.
	const char * kFilterLabels[5]      = { "None", "Private/BizJet", "Passenger", "Cargo", "Military/Gov" };
	const char * kFilterLabelsShort[5] = { "None", "Private",        "Passenger", "Cargo", "Military"     };

	// The label and the file no longer say the same word, so each chip names the
	// apt.dat token it writes. A divergence nobody can see is the kind that gets
	// rediscovered at 2am by someone diffing a .dat.
	const char * kFilterTips[5] = {
		"No static aircraft park here   |   apt.dat: none",
		"Private and business aviation   |   apt.dat: general_aviation",
		"Scheduled and charter passenger service   |   apt.dat: airline",
		"Freight   |   apt.dat: cargo",
		"Air forces, and state flights that are not military   |   apt.dat: military",
	};
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

	// XPZZ_<TYPE>, the unpainted airliners, sink to the bottom of every list whichever
	// way the sort arrow points. It is a fallback, not a choice - offering a
	// white 757 above a real operator is the picker answering the wrong
	// question first, and at a busy airport it would be the first thing seen.
	// The rows carry the code lowercased (see r.icao above).
	bool NotTheUnpaintedAirliner(const WED_LiveryDisplayRow & r)
	{
		return !WED_IsGenericAirlinerCode(r.icao);
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
								vector<WED_LiveryDisplayRow> & out, bool preserve_order)
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
	//
	// A listed code with NO card here - out of range, no livery at this size, or
	// not in the index at all (a code typed in the grid, ryr_1) - is named on a
	// note line in the same section. Dropping it silently made a stand that lists
	// three operators look like it lists none.
	void PinSelected(vector<WED_LiveryDisplayRow> & rows, const set<string> & selected,
					 const set<string> & have_cards)
	{
		if (selected.empty()) return;

		string cardless;
		for (set<string>::const_iterator c = selected.begin(); c != selected.end(); ++c)
			if (!have_cards.count(*c))
			{
				string uc = *c;
				for (size_t k = 0; k < uc.size(); ++k) uc[k] = (char) toupper((unsigned char) uc[k]);
				cardless += (cardless.empty() ? "" : " ") + uc;
			}

		vector<WED_LiveryDisplayRow> picked;
		for (size_t i = 0; i < rows.size(); ++i)
		{
			if (rows[i].kind != wed_Row_Airline || !selected.count(rows[i].icao)) continue;
			bool dup = false;
			for (size_t j = 0; j < picked.size(); ++j)
				if (picked[j].icao == rows[i].icao) { dup = true; break; }
			if (!dup) picked.push_back(rows[i]);
		}
		if (picked.empty() && cardless.empty()) return;

		vector<WED_LiveryDisplayRow> rest = rows;

		vector<WED_LiveryDisplayRow> out;
		WED_LiveryDisplayRow h; h.kind = wed_Row_Header; h.header_text = "Selected";
		out.push_back(h);
		WED_LiveryDisplayRow g; g.kind = wed_Row_Gap;
		if (!picked.empty())
		{
			out.push_back(g);
			out.insert(out.end(), picked.begin(), picked.end());
		}
		if (!cardless.empty())
		{
			WED_LiveryDisplayRow n; n.kind = wed_Row_Note;
			n.header_text = "Also listed, nothing to show at this stand: " + cardless;
			out.push_back(n);
		}
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
				// Logged, not alerted. This runs on the Draw path, and a missing
				// index is not a fault: an X-Plane older than 12.5 simply does not
				// ship one. The pane says so in place (NoIndexSentence) and the
				// Airlines field on the Selection tab still takes codes by hand.
				static bool s_logged = false;
				if (!s_logged)
				{
					s_logged = true;
					LOG_MSG("I/Livery no operator records - %s: %s\n",
							WedDataFileErrorText(directory.LoadError()), dir_path.c_str());
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


// What the pane says when livery_index.txt did not load. A missing file is the
// normal state of an X-Plane older than 12.5 and is worded as such, not as an
// error; a file that is there but unreadable is a fault and says so. Either
// way the author is told where airline codes can still be typed.
string NoIndexSentence(WedDataFileError err)
{
	if (err == wed_data_no_file || err == wed_data_ok)
		return "Livery previews need X-Plane 12.5 or later. Airlines can still be typed in the Selection tab.";
	return string("livery_index.txt could not be used (") + WedDataFileErrorText(err) +
		   "). Airlines can still be typed in the Selection tab.";
}

int CollectRamps(ISelectable * who, void * ref)
{
	vector<WED_RampPosition *> * out = (vector<WED_RampPosition *> *) ref;
	WED_RampPosition * r = SAFE_CAST(WED_RampPosition, who);
	if (r) out->push_back(r);
	return 0;		// keep iterating - we want the whole selection, not just the first hit
}

set<string> ParseCodes(const string & airlines)
{
	set<string> out;
	std::istringstream iss(airlines);
	string tok;
	while (iss >> tok) out.insert(tok);
	return out;
}

string CodesToString(const set<string> & codes)
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

// One stand or many: the same button, named for what it will do.
string PopulateCaption(size_t n)
{
	if (n <= 1) return kPopulateCaption;
	char buf[48];
	snprintf(buf, sizeof(buf), "Populate %d Ramps", (int) n);
	return buf;
}

double PaneClockNow(void)
{
	return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

// The readout's detail is collapsed by default - the headline is the statistic
// an author needs, and five lines of it under every click was too much - and
// the choice holds across stands for the session.
bool sCoverageExpanded = false;

// Cards and text rows share one hit test, so every caller needs the same "which
// rows are cards" vector. Kept here rather than repeated at each call site,
// because a caller that got it wrong would hit-test against a layout the draw
// never used - the exact class of drift LayoutRows exists to end.
void	CardFlags(const vector<WED_LiveryDisplayRow> & rows, vector<bool> & out)
{
	out.resize(rows.size());
	for (size_t i = 0; i < rows.size(); ++i)
		out[i] = (rows[i].kind == wed_Row_Airline);
}

// The icao of each airline row, empty for every other kind. Feeds TrayHeights,
// which cannot take the rows themselves - see its declaration.
void	RowIcaos(const vector<WED_LiveryDisplayRow> & rows, vector<string> & out)
{
	out.assign(rows.size(), string());
	for (size_t i = 0; i < rows.size(); ++i)
		if (rows[i].kind == wed_Row_Airline) out[i] = rows[i].icao;
}

// How tall this operator's tray is when fully open.
float	TrayFullHeight(size_t n_liveries)
{
	// One extra row's worth for the "Preview ONLY" band across the top.
	return kTrayPad * 2.0f + (float) (n_liveries + 1) * kTrayRowH;
}

}	// namespace livery_pane
