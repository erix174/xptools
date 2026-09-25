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
// tab's cards and coverage readout, and the auto-fill. One copy, so a preview
// and an auto-filled stand can never disagree about what may park where.

#include <string>

struct WED_LiveryIndexEntry;
class  WED_AirlineDirectory;

// MAY THIS LIVERY APPEAR AT THIS STAND. By operation class:
//   GA              - anywhere, no range rule.
//   Military / Gov  - anywhere, unless the row is HOME (R27): then only where
//                     the operator's country (else the registration's) is the
//                     airport's. Either country unknown -> allowed.
//   everything else - the range rule, R26.
enum WED_LiveryAllow { livery_allow_Yes, livery_allow_OutOfRange, livery_allow_ForeignMilitary };

WED_LiveryAllow	WED_LiveryAllowedAt(const WED_LiveryIndexEntry & e,
									const WED_AirlineDirectory & directory,
									const std::string & airport_country,	// IOC, "" if unknown
									double stand_lat, double stand_lon);

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

#endif /* WED_LIVERYRULES_H */
