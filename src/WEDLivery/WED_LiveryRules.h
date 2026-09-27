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

#ifndef WED_LIVERYRULES_H
#define WED_LIVERYRULES_H

// The rules every livery consumer in WED applies the same way - the Liveries
// tab's cards and coverage readout, the auto-fill, the validator's R14 check and
// the moderation overview and report. One copy, from one set of data: a preview,
// an auto-filled stand and a warning can never disagree about what may park
// where.

#include <set>
#include <string>
#include "WED_LiveryIndex.h"
#include "WED_AirlineDirectory.h"
#include "WED_AirportDatabase.h"

// THE DATA, three databases in two files, read raw - nothing processed or
// cached to disk in between:
//   livery_index.txt (the X-Plane install's) - the liveries, and the OPERATOR
//                     records (WED_AirlineDirectory reads those)
//   WED_AirportDatabase.txt (shipped beside WED) - who serves which airport
// Every consumer, the Liveries tab included, reads this one instance. It is
// rebuilt whole when the X-Plane folder changes. WED_GetLiveryData returns NULL
// when there is no livery index (an X-Plane before 12.5); need_hubs blocks
// until R26's hub positions are placed. WED_SharedLiveryData hands out the
// instance without loading anything, for callers that load lazily and report
// load errors themselves (the tab). Never keep a reference across frames: a
// folder change replaces the instance.
// ONE livery index for the process: the Liveries tab and the whole-airport
// commands read the same instance, so its 380 MB hub scan runs once. A second
// copy made the first "Populate This Ramp" block the UI for a scan the tab had
// already finished.
WED_LiveryIndex &	WED_SharedLiveryIndex(void);

struct WED_LiveryData {
	WED_LiveryIndex &		index;
	WED_AirlineDirectory	directory;
	WED_AirportDatabase		airports;
	WED_LiveryData() : index(WED_SharedLiveryIndex()) {}
};
WED_LiveryData *	WED_GetLiveryData(bool need_hubs);
WED_LiveryData &	WED_SharedLiveryData(void);

// MAY THIS LIVERY APPEAR AT THIS STAND. By operation class:
//   GA              - anywhere, no range rule.
//   Military / Gov  - anywhere, unless the row is HOME (R27): then only where
//                     the operator's country (else the registration's) is the
//                     airport's. Either country unknown -> allowed.
//   everything else - the range rule, R26.
enum WED_LiveryAllow { livery_allow_Yes, livery_allow_OutOfRange, livery_allow_ForeignMilitary, livery_allow_Equipment };

WED_LiveryAllow	WED_LiveryAllowedAt(const WED_LiveryIndexEntry & e,
									const WED_AirlineDirectory & directory,
									const std::string & airport_country,	// IOC, "" if unknown
									double stand_lat, double stand_lon);

// THE STAND RULE: WED_LiveryAllowedAt plus the stand's Equipment Type. What
// every consumer asks of one livery at one stand. equipment empty = any.
WED_LiveryAllow	WED_LiveryFitsStand(const WED_LiveryIndexEntry & e,
									const WED_AirlineDirectory & directory,
									const std::string & airport_country,
									double stand_lat, double stand_lon,
									const std::set<int> & equipment);

// MAY THIS OPERATOR BE LISTED ON A STAND OF THIS OPERATION TYPE: its OPERATOR
// record's class against the stand's type, the pseudo-codes by name (XPGA
// general aviation, XPMI military, XPZZ_ generic airliners on airline and cargo
// stands). Unknown to the directory: an airline, fail open. Spec R18's
// op_class_matches. ramp_operation_None matches nothing - None parks nothing.
bool			WED_LiveryOperatorFitsRampOp(const std::string & code_uc, int ramp_op,
											 const WED_AirlineDirectory & directory);

// The ramp Equipment Type a livery needs, from the folder X-Plane ships it in
// (apt_aircraft/<jet|heavy|turboprop|prop|helo|fighter>/...): one of the
// atc_Heavies / atc_Jets / ... enum values, or -1 when the folder says nothing.
int				WED_LiveryEquipment(const WED_LiveryIndexEntry & e);

// Class weights for a stand that only ever had one size letter (0 = A .. 5 = F).
// The old letter meant "up to this size", so the weight spreads downward:
//   A: A100   B: A30 B70   C: B30 C70   D: B10 C40 D50   E: C10 D30 E60   F: D10 E50 F40
// A class the draw lands on with no livery leaves the stand empty for that
// load (R18) - the weights are never renormalised onto the classes that exist.
void			WED_LegacyClassWeights(int size_class, int out_w[6]);

// TODAY'S STEP-DOWN, as weights. A stand with no 1313 row is the legacy format:
// the sim takes its 1301 letter 75% of the time and hands 75% of what is left to
// each smaller class in turn, class A taking the remainder - share
// 0.75 x 0.25^k, k classes below the top. Integers summing to 1000
// (D: A15 B47 C188 D750). One difference that this cannot carry: a legacy draw
// on a class with nothing to park steps on down, a weight does not (R18) - so
// judging a legacy stand, "parks nothing" means nothing fits at the letter or
// ANY class below it.
void			WED_LegacyStepDownWeights(int top_class, int out_w[6]);

#endif /* WED_LIVERYRULES_H */
