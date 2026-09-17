/*
 * Copyright (c) 2007, Laminar Research.
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

#ifndef WED_RAMPPOSITION_H
#define WED_RAMPPOSITION_H

#include "WED_GISPoint_Heading.h"
#include "WED_XMLReader.h"

struct	AptGate_t;

class	WED_RampPosition : public WED_GISPoint_Heading {

DECLARE_PERSISTENT(WED_RampPosition)

public:

	void	SetType(int		ramp_type);
	void	SetEquipment(const set<int>&	et);
	void	SetWidth(int		width);
	void	SetWidthMin(int	width_min);
	void	SetRampOperationType(int ait);
	void	SetAirlines(const string& airlines);

	string  GetAirlines() const;
	int		GetType() const;
	int		GetWidth() const;
	int		GetWidthMin() const;
	void	GetTips(Point2 c[4]) const;              // nose, tail, both wing tips. Takes heading, siize and offset(type=misc) into account
	void	GetEquipment(set<int>& out_eq) const;
	int		GetRampOperationType() const;
	
	// ---- per-class spawn weights (apt.dat row 1313) ----
	// Six relative integers, A..F. Stored as one space-separated string for the
	// same reason `airlines` is: it IS the apt.dat payload, and the field next
	// door already works this way.
	//
	// The encoding also carries a distinction the format requires and six
	// separate integer properties could not express. An EMPTY string means "this
	// stand has no 1313 row", which keeps today's step-down behaviour (R17);
	// "0 0 0 0 0 0" means the author deliberately said nothing parks here (§4.2).
	// Six int properties defaulting to zero would make those the same value.
	//
	// GetClassWeights returns false for absent AND for malformed, which is R5's
	// drop-the-row-whole rule expressed in one place.
	bool	GetClassWeights(int out_w[6]) const;
	void	SetClassWeights(const int w[6]);
	void	ClearClassWeights(void);

	static string CorrectAirlinesString(const string &a);
	// Normalises to six integers separated by single spaces, or returns "" if the
	// input is not exactly six values in 0..1000 (R11). Never throws, never
	// partially accepts.
	static string CorrectWeightsString(const string &w);

	void	Import(const AptGate_t& x, void (* print_func)(void *, const char *, ...), void * ref);
	void	Export(		 AptGate_t& x) const;

	virtual const char *	HumanReadableType(void) const { return "Ramp Start"; }

	// Pre-migration documents only ever wrote a single "width" letter - no
	// "width_min" attribute existed. StartElement() notices this per-object as
	// each <ramp_start> is parsed; EndElement() then backfills width_min from
	// the legacy value once it's known. See WED_RampPosition.cpp for the
	// team-agreed mapping.
	virtual void	StartElement(WED_XMLReader * reader, const XML_Char * name, const XML_Char ** atts);
	virtual void	EndElement(void);

private:

	WED_PropIntEnum			ramp_type;
	WED_PropIntEnumBitfield	equip_type;
	WED_PropIntEnum			width;
	WED_PropIntEnum			width_min;
	WED_PropIntEnum			ramp_op_type;
	WED_PropStringText		airlines;
	WED_PropStringText		class_weights;	// "" = no 1313 row; see GetClassWeights()

	bool					mLegacyWidthOnly;	// true while parsing an XML element that had no width_min attribute

};

#endif /* WED_RAMPPOSITION_H */
