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

#ifndef WED_LIVERYAUTOFILL_H
#define WED_LIVERYAUTOFILL_H

// ONE-CLICK STATIC AIRCRAFT, the way metadata is updated: for an author who
// will not fine-tune every stand, fill an airport's ramp starts from the
// airport database and the livery index. It EXTENDS and never overwrites - an
// author's airlines, weights and operation types are kept exactly as they are.
//
// Per ramp start (gates and tie-downs; hangars and misc never park static
// aircraft):
//   op type None    - skipped. None means "no static aircraft here".
//   no weights yet  - a legacy stand: its one size letter becomes a weight
//                     spread (WED_LegacyStepDownWeights). Stands that already
//                     carry weights keep them.
//   Passenger/Cargo - adds the airport's RECOMMENDED operators (the airlines
//                     WED_AirportDatabase says serve it) of that operation
//                     class that have a livery the stand can take: a weighted
//                     class, the stand's equipment type, and within range (R26).
//                     No recommendation for the airport - usually a GA field -
//                     adds nothing. XPZZ_* is never added.
//   Military/Gov    - adds the airport country's own military and government
//                     operators that have such a livery. If there are none,
//                     nothing is added and X-Plane draws its generic military
//                     aircraft by size.
//   GA              - weights only. X-Plane draws GA from its library by size,
//                     70% registered in the airport's country and 30% foreign,
//                     with no range rule; no airline list is involved.
//
// Every stand it changes carries the auto-fill watermark (see
// WED_RampPosition::IsAutoFilled) until a person edits anything on it other
// than the weights.
//
// Plan, then apply: planning reads the document and changes nothing, so a
// caller can show what would happen before committing it as one undo step.

#include <string>
#include <vector>

class WED_Airport;
class WED_RampPosition;

struct WED_AutoFillRamp {
	WED_RampPosition *	ramp = nullptr;
	std::string			name;
	std::string			airlines_before;
	std::string			airlines_after;		// == before when nothing is added
	std::vector<std::string>	added;			// codes added, upper case
	bool				set_weights = false;
	int					weights[6] = { 0, 0, 0, 0, 0, 0 };	// meaningful when set_weights
	std::string			skipped;			// why nothing was done, "" when changed
	bool				Changes(void) const { return set_weights || !added.empty(); }
};

struct WED_AutoFillPlan {
	WED_Airport *					airport = nullptr;
	std::string						icao;
	std::string						country;			// IOC, "" if unknown
	std::vector<std::string>		recommended;		// what the airport database has, upper case
	std::vector<WED_AutoFillRamp>	ramps;
	int								changed = 0;
	std::string						error;				// non-empty: nothing could be planned
};

// Reads the airport and the data files; never touches the document. Blocks
// until the livery index's hub positions are placed (R26 needs them) - a
// second or so the first time, nothing after.
//   only            - plan just these ramp starts (NULL: every one at `apt`)
//   convert_legacy  - a stand with no weights gets the legacy spread. false:
//                     leave its weights alone and fill against the size range
//                     it has now - what "Populate This Ramp" wants, since the
//                     author set that scope on purpose.
WED_AutoFillPlan	WED_PlanLiveryAutoFill(WED_Airport * apt,
									   const std::vector<WED_RampPosition *> * only = nullptr,
									   bool convert_legacy = true);

// Applies every changing ramp of `plan` and watermarks each of them - as one
// undoable command of its own, or inside the caller's (own_command false).
// Returns the number of ramp starts changed.
int					WED_ApplyLiveryAutoFill(const WED_AutoFillPlan & plan, bool own_command = true);

// One line per changed ramp and a summary - for the log and a confirmation.
std::string			WED_DescribeAutoFill(const WED_AutoFillPlan & plan);

// THE EXPORT-TIME UPGRADE (Eric's D1-D7, 2026-09-29). Every legacy ramp start
// nobody has set in 2.8, and every one this upgrade marked before (1315 A), is
// brought to the 12.5 format - hands off, for 30,000 Gateway airports:
//   - every code the stand lists is kept, known to the index or not (Validate
//     and Moderation Mode flag unknown ones for a person to check);
//   - a stand that parks nothing gets the airport's operators, as Auto-Populate
//     adds them - but a one-code list is never extended;
//   - the size letter becomes today's step-down as weights, so a stand that
//     parked keeps parking the same aircraft;
//   - and only if the stand then parks something is any of it kept; otherwise
//     the stand is put back exactly as it was (legacy: today's behaviour).
// Upgraded stands carry the auto-fill mark (1315 A). Call inside a command, and
// never for a moderator (they may be overriding the database on purpose).
struct WED_LegacyUpgradeStats {
	int converted = 0;		// legacy stands now in the 12.5 format
	int filled = 0;			// ...of any kind, given operators because nothing parked
	int kept_legacy = 0;	// would have parked nothing: left as they were
};
void				WED_LiveryExportUpgrade(WED_Airport * apt, WED_LegacyUpgradeStats & st);

// Airport > Auto-Populate Static Aircraft (Selected Ramps Only): the selected
// ramp starts, grouped by airport, shown and applied on confirmation as one
// undo step. Enabled only while ramp starts are selected.
class IResolver;
int					WED_CanLiveryAutoFill(IResolver * resolver);
void				WED_DoLiveryAutoFill(IResolver * resolver);

// Airport > Update Legacy Stands to Spawn Weights: every selected stand still in
// the legacy format (no 1313) gets today's step-down written out as weights,
// fall-through folded in (WED_LiveryLegacyUpdateWeights) - it parks what it
// parked, and from then on the new rules apply. One undo step.
int					WED_CanLiveryLegacyUpdate(IResolver * resolver);
void				WED_DoLiveryLegacyUpdate(IResolver * resolver);

#endif /* WED_LIVERYAUTOFILL_H */
