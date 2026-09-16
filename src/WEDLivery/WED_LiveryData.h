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

#ifndef WED_LIVERYDATA_H
#define WED_LIVERYDATA_H

// Placeholder livery dataset for the "Liveries" tab. The real data source (format,
// icons, how it ships with X-Plane) is not decided yet - this is the only file
// that needs to change once it is. ICAO codes are stored lowercase to match
// WED_RampPosition::CorrectAirlinesString's normalization.
//
// op_type is this file's OWN small classification (not tied to WED's internal
// RampOperationType enum values) - WED_LiveryPane maps the ramp's actual
// Ramp Operation Type property onto these constants to filter the list.

enum WED_LiveryOpType
{
	wed_LiveryOp_GeneralAviation = 0,
	wed_LiveryOp_Airline         = 1,
	wed_LiveryOp_Cargo           = 2,
	wed_LiveryOp_Military        = 3
};

struct WED_LiveryEntry
{
	const char *	icao;
	const char *	name;
	int				op_type;	// WED_LiveryOpType
};

static const WED_LiveryEntry kWED_PlaceholderAirlines[] =
{
	{ "aal", "American Airlines",       wed_LiveryOp_Airline },
	{ "ual", "United Airlines",         wed_LiveryOp_Airline },
	{ "dal", "Delta Air Lines",         wed_LiveryOp_Airline },
	{ "swa", "Southwest Airlines",      wed_LiveryOp_Airline },
	{ "asa", "Alaska Airlines",         wed_LiveryOp_Airline },
	{ "jbu", "JetBlue Airways",         wed_LiveryOp_Airline },
	{ "baw", "British Airways",         wed_LiveryOp_Airline },
	{ "dlh", "Lufthansa",               wed_LiveryOp_Airline },
	{ "afr", "Air France",              wed_LiveryOp_Airline },
	{ "klm", "KLM Royal Dutch Airlines",wed_LiveryOp_Airline },
	{ "uae", "Emirates",                wed_LiveryOp_Airline },
	{ "qtr", "Qatar Airways",           wed_LiveryOp_Airline },
	{ "aca", "Air Canada",              wed_LiveryOp_Airline },
	{ "qfa", "Qantas",                  wed_LiveryOp_Airline },
	{ "ana", "All Nippon Airways",      wed_LiveryOp_Airline },
	{ "jal", "Japan Airlines",          wed_LiveryOp_Airline },
	{ "csn", "China Southern Airlines", wed_LiveryOp_Airline },
	{ "eza", "Lufthansa CityLine",      wed_LiveryOp_Airline },

	{ "fdx", "FedEx Express",           wed_LiveryOp_Cargo },
	{ "ups", "UPS Airlines",            wed_LiveryOp_Cargo },
	{ "gti", "Atlas Air",               wed_LiveryOp_Cargo },
	{ "clx", "Cargolux",                wed_LiveryOp_Cargo },

	{ "eja", "NetJets",                 wed_LiveryOp_GeneralAviation },
	{ "lxj", "Flexjet",                 wed_LiveryOp_GeneralAviation },

	{ "rch", "US Air Mobility Command (placeholder)", wed_LiveryOp_Military },
	{ "naf", "Royal Netherlands AF (placeholder)",     wed_LiveryOp_Military },
};

static const int kWED_PlaceholderAirlineCount = sizeof(kWED_PlaceholderAirlines) / sizeof(kWED_PlaceholderAirlines[0]);

#endif /* WED_LIVERYDATA_H */
