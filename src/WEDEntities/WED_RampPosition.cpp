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

#include "WED_RampPosition.h"
#include "AptDefs.h"
#include "WED_EnumSystem.h"
#include "GISUtils.h"

DEFINE_PERSISTENT(WED_RampPosition)
TRIVIAL_COPY(WED_RampPosition, WED_GISPoint_Heading)

WED_RampPosition::WED_RampPosition(WED_Archive * a, int i) : WED_GISPoint_Heading(a,i),
	ramp_type	(this,PROP_Name("Ramp Start Type",     XML_Name("ramp_start","type"   )), ATCRampType, atc_Ramp_Misc),
	equip_type	(this,PROP_Name("Equipment Type",      XML_Name("ramp_start","traffic")), ATCTrafficType, 0),
	// Size is edited on the "Liveries" tab (WED_LiveryPane) - it is a range with
	// optional class weights now, which one letter in the grid cannot show. The
	// leading "." hides it from the generic property grid (see
	// WED_PropertyTable::RecalculateColumns). Ramp Operation Type and Airlines stay
	// in the grid as well: 1301's airline list also drives ATC and AI parking, so
	// it must be typeable without an index, and for an operator with no livery.
	// XML tag names are unchanged, so old documents still round-trip.
	width		(this,PROP_Name(".Size",                XML_Name("ramp_start","width")), ATCIcaoWidth, width_C),
	width_min	(this,PROP_Name(".Size Min",             XML_Name("ramp_start","width_min")), ATCIcaoWidth, width_A),
	ramp_op_type(this,PROP_Name("Ramp Operation Type",  XML_Name("ramp_start","ramp_op_type")), RampOperationType, ramp_operation_None),
	airlines	(this,PROP_Name("Airlines",             XML_Name("ramp_start","airlines")),""),
	class_weights(this,PROP_Name(".Class Weights",      XML_Name("ramp_start","weights")),""),
	weights_mode (this,PROP_Name(".Weights Mode",       XML_Name("ramp_start","weights_mode")), 0),
	auto_filled  (this,PROP_Name(".Auto Filled",        XML_Name("ramp_start","auto_filled")), 0),
	mLegacyWidthOnly(false)
{
}

WED_RampPosition::~WED_RampPosition()
{
}

void	WED_RampPosition::Import(const AptGate_t& x, void (* print_func)(void *, const char *, ...), void * ref)
{
	SetLocation(gis_Geo,x.location);
	SetHeading(x.heading);
	SetName(x.name);
	ramp_op_type = ENUM_Import(RampOperationType, x.ramp_op_type);
	if(ramp_op_type == -1)
	{
		print_func(ref,"Illegal oerations tye: %d\n", x.ramp_op_type);
		ramp_op_type = ramp_operation_None;
	}
	SetAirlines(x.airlines);
	
	ramp_type			= ENUM_Import(ATCRampType,		x.type	);
	if(ramp_type == -1)
	{
		print_func(ref,"Illegal ramp type: %d\n",x.type);
		ramp_type = atc_Ramp_Misc;
	}
	width = ENUM_Import(ATCIcaoWidth, x.width);
	if(width == -1)
	{
		print_func(ref,"Illegal ramp size: %d\n",x.type);
		width = width_E;			// was "ramp_type = width_E" - a size assigned to the TYPE
	}

	// Weights arrive already validated by AptIO - it enforces exactly six values
	// in range and drops the row whole otherwise (R5), so an empty vector here
	// means either "no 1313 row" or "one that did not parse", which R5 makes the
	// same thing on purpose.
	if (x.class_weights.size() == 6)
	{
		int w[6];
		for (int i = 0; i < 6; ++i) w[i] = x.class_weights[i];
		SetClassWeights(w);
	}
	else
		ClearClassWeights();

	// apt.dat carries ONE size letter per ramp start (row 1301's first field), so
	// an imported stand is a single class, not a range - min and max are equal.
	// Without this, width_min keeps its property default of width_A and every
	// imported ramp reads as [A .. whatever], a range the author never wrote and
	// which the Liveries tab then presents as a wide-open slider.
	width_min = width.value;

	ENUM_ImportSet(equip_type.domain,x.equipment,equip_type.value);
}

void	WED_RampPosition::Export(		 AptGate_t& x) const
{
	GetLocation(gis_Geo,x.location);
	x.heading = GetHeading();
	GetName(x.name);
	x.type = ENUM_Export(ramp_type.value);
	x.equipment = ENUM_ExportSet(equip_type.value);
	x.width = ENUM_Export(width.value);
	x.ramp_op_type = ENUM_Export(ramp_op_type.value);
	x.airlines = WED_RampPosition::CorrectAirlinesString(airlines.value);

	x.class_weights.clear();
	int w[6];
	if (GetClassWeights(w))
	{
		x.class_weights.assign(w, w + 6);

		// R23: once weights exist, 1301's size letter is a DERIVED field. It is
		// set to the highest class carrying a non-zero weight, and nothing reads
		// it for selection any more - it survives for the two consumers that
		// cannot see 1313, an old sim (which step-downs from it, so it should
		// start at the largest class the author allows) and WED's own map view,
		// which sizes the stand icon from it.
		//
		// Written here rather than back onto the entity on purpose: the export
		// struct is a snapshot, and silently rewriting the author's property
		// during a save is a different and worse thing than emitting a corrected
		// value.
		for (int i = 5; i >= 0; --i)
			if (w[i] > 0) { x.width = i; break; }
	}
}

// Each setter below is a human edit of something auto-fill decided or relied
// on, so a real change drops the watermark. The weight setters do not: tuning
// the distribution is what an author is expected to do after a fill.
void	WED_RampPosition::SetType(int	rt)
{
	if (rt != ramp_type.value) auto_filled = false;
	ramp_type = rt;
}

void	WED_RampPosition::SetEquipment(const set<int>&	et)
{
	if (et != equip_type.value) auto_filled = false;
	equip_type = et;
}

void	WED_RampPosition::SetWidth(int		w)
{
	if (w != width.value) auto_filled = false;
	width = w;
}

void	WED_RampPosition::SetWidthMin(int		w)
{
	if (w != width_min.value) auto_filled = false;
	width_min = w;
}

bool	WED_RampPosition::IsAutoFilled(void) const	{ return auto_filled.value != 0; }
void	WED_RampPosition::SetAutoFilled(bool on)	{ auto_filled = on; }

void	WED_RampPosition::StartElement(WED_XMLReader * reader, const XML_Char * name, const XML_Char ** atts)
{
	if (strcmp(name, "ramp_start") == 0)
		mLegacyWidthOnly = (get_att("width_min", atts) == NULL);

	WED_GISPoint_Heading::StartElement(reader, name, atts);
}

void	WED_RampPosition::EndElement(void)
{
	if (mLegacyWidthOnly)
	{
		// Pre-migration files only ever had a single "width" letter - no notion
		// of a range. Team-agreed mapping onto [min,max]: the two extreme
		// classes stay single-width on purpose ("they're there for a reason"),
		// everything else widens by exactly one class below:
		//   A->A   B->[A,B]   C->[B,C]   D->[C,D]   E->[D,E]   F->F
		static const int kOrder[6]     = { width_A, width_B, width_C, width_D, width_E, width_F };
		static const int kLegacyMin[6] = { width_A, width_A, width_B, width_C, width_D, width_F };

		int idx = 2;	// unknown/corrupt width falls back to C, same as elsewhere in this file
		for (int i = 0; i < 6; ++i)
			if (kOrder[i] == width.value) { idx = i; break; }

		width_min = kLegacyMin[idx];
		mLegacyWidthOnly = false;
	}

	WED_GISPoint_Heading::EndElement();
}

void	WED_RampPosition::SetRampOperationType(int ait)
{
	if (ait != ramp_op_type.value) auto_filled = false;
	ramp_op_type = ait;
}

int 	WED_RampPosition::GetRampOperationType() const
{
	return ramp_op_type;
}


static bool two_adjacent_spaces(char lhs, char rhs)
{
	return (lhs == rhs) && (lhs == ' ');
}

// Longest airline list observed in the global apt.dat is 27 codes, about 110
// characters. This is a defensive ceiling, not a design limit - it exists so a
// hand-authored document cannot put an unbounded string into a row that other
// tools have to read.
static const size_t kMaxAirlinesChars = 1024;

string	WED_RampPosition::CorrectAirlinesString(const string &a)
{
	string cleaned_airlines_str;
	std::transform(a.begin(), a.end(), back_inserter(cleaned_airlines_str), [](unsigned char c) {return tolower(c);} );

	// Fold EVERY kind of whitespace to a plain space before anything else.
	//
	// This string is written into apt.dat with a bare fprintf("%s") followed by
	// the row terminator (AptIO.cpp:1475). Only ' ' was ever collapsed below, so
	// an embedded newline survived the whole pipeline and appeared in the
	// exported file as a row of its own - a content injection that reaches
	// anything consuming that apt.dat, Gateway included. A tab or a carriage
	// return would equally have corrupted the row's field structure.
	for (size_t i = 0; i < cleaned_airlines_str.size(); ++i)
		if (isspace((unsigned char) cleaned_airlines_str[i]))
			cleaned_airlines_str[i] = ' ';

	//Thanks Plamen for this concise trim http://stackoverflow.com/a/22711818
	//Ben says: except - the stack overflow answer is WRONG - missing a check for
	//the empty string case.
	while(!cleaned_airlines_str.empty() && isspace(*cleaned_airlines_str.begin()))
	{
		cleaned_airlines_str.erase(cleaned_airlines_str.begin());
	}

	while(!cleaned_airlines_str.empty() && isspace(*cleaned_airlines_str.rbegin()))
	{
		cleaned_airlines_str.erase(cleaned_airlines_str.length()-1);
	}
	
	cleaned_airlines_str.erase(std::unique(cleaned_airlines_str.begin(), cleaned_airlines_str.end(), two_adjacent_spaces), cleaned_airlines_str.end());

	// Bound the result, cutting at a token boundary so truncation can never
	// invent a code that was never written.
	if (cleaned_airlines_str.size() > kMaxAirlinesChars)
	{
		cleaned_airlines_str.resize(kMaxAirlinesChars);
		size_t last = cleaned_airlines_str.find_last_of(' ');
		cleaned_airlines_str.erase(last == string::npos ? 0 : last);
	}

	return cleaned_airlines_str;
}

bool	WED_RampPosition::IsValidAirlineCode(const string &code)
{
	size_t us = code.find('_');
	size_t base = (us == string::npos) ? code.size() : us;
	if (base < 3 || base > 4) return false;
	if (us != string::npos && (code.size() - us - 1 < 1 || code.size() - us - 1 > 6)) return false;
	for (size_t i = 0; i < code.size(); ++i)
	{
		if (i == us) continue;
		char c = code[i];
		if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9'))) return false;
	}
	return true;
}

// The grid writes the property directly; normalise it the same way SetAirlines
// does, or a code typed as "DAL" reads as unchecked on the Liveries tab.
static bool	SamePropVal(const PropertyVal_t & a, const PropertyVal_t & b)
{
	// Only the field the kind uses: the others are not initialised.
	if (a.prop_kind != b.prop_kind) return false;
	switch (a.prop_kind) {
	case prop_EnumSet:	return a.set_val == b.set_val;
	case prop_Double:	return a.double_val == b.double_val;
	case prop_Int:
	case prop_Enum:
	case prop_Bool:		return a.int_val == b.int_val;
	default:			return a.string_val == b.string_val;
	}
}

void	WED_RampPosition::SetNthProperty(int n, const PropertyVal_t& val)
{
	PropertyVal_t v(val);
	if (n == PropertyItemNumber(&airlines) && val.prop_kind == prop_String)
		v.string_val = CorrectAirlinesString(val.string_val);

	// A grid edit of a field auto-fill relies on drops the watermark, as the
	// setters do - but only when the value really changes.
	bool human_field = n == PropertyItemNumber(&airlines)  || n == PropertyItemNumber(&ramp_op_type) ||
					   n == PropertyItemNumber(&ramp_type) || n == PropertyItemNumber(&equip_type)   ||
					   n == PropertyItemNumber(&width)     || n == PropertyItemNumber(&width_min);
	if (human_field)
	{
		PropertyVal_t before;
		GetNthProperty(n, before);
		if (!SamePropVal(before, v)) auto_filled = false;
	}
	WED_GISPoint_Heading::SetNthProperty(n, v);
}

void	WED_RampPosition::SetAirlines(const string &a)
{
	// Normalize HERE, not only on export. apt.dat is case-insensitive about
	// airline codes and files in the wild carry "AAL DAL", but everything that
	// compares against this string - the Liveries tab's checkboxes, the
	// recommendation rows, WED_AirlineDirectory lookups - works in lower case.
	// Storing what we were handed meant an uppercase import rendered every
	// checkbox unchecked for a ramp that demonstrably had those airlines, and
	// the first click then wrote the code a second time in lower case
	// ("aal dal aal"), which round-tripped straight back out to apt.dat.
	string cleaned = CorrectAirlinesString(a);
	if (cleaned != airlines.value) auto_filled = false;
	airlines = cleaned;
}

// ---------------------------------------------------------------------------
// per-class spawn weights (apt.dat row 1313)
// ---------------------------------------------------------------------------

// R5 and R11 in one function: exactly six values, each 0..1000, or nothing.
//
// There is deliberately no partial acceptance. A weight vector means "here is
// the whole distribution", so keeping five of six numbers changes what the
// survivors mean - and because all-zero is legal and means "nothing parks here",
// a truncated row read as zeros would silently empty the stand instead of
// falling back to today's behaviour. Returning "" puts it back to R17's floor,
// which is the one place soft-fail has to be explicit about what it falls back
// TO rather than merely that it fell back.
string	WED_RampPosition::CorrectWeightsString(const string &w)
{
	int    v[6];
	size_t n = 0;
	size_t i = 0;

	while (i < w.size())
	{
		while (i < w.size() && isspace((unsigned char) w[i])) ++i;
		if (i >= w.size()) break;

		if (n >= 6) return string();				// a seventh value - drop whole

		size_t start = i;
		int    acc   = 0;
		while (i < w.size() && isdigit((unsigned char) w[i]))
		{
			acc = acc * 10 + (w[i] - '0');
			if (acc > 1000) return string();		// R11 ceiling, and it also caps overflow
			++i;
		}

		// Anything that is not a run of digits - a sign, a decimal point, a
		// stray letter - makes the row malformed. Note this is what rejects
		// "-5" and "1.5" without either needing a special case: the '-' and the
		// '.' simply are not digits, so the token does not end at whitespace.
		if (i == start) return string();
		if (i < w.size() && !isspace((unsigned char) w[i])) return string();

		v[n++] = acc;
	}

	if (n != 6) return string();					// five, or none, or seven

	char buf[64];
	snprintf(buf, sizeof(buf), "%d %d %d %d %d %d", v[0], v[1], v[2], v[3], v[4], v[5]);
	return string(buf);
}

bool	WED_RampPosition::GetClassWeights(int out_w[6]) const
{
	if (!weights_mode.value) return false;		// parked - the size range rules, see the .h
	return HasStoredWeights(out_w);
}

bool	WED_RampPosition::HasStoredWeights(int out_w[6]) const
{
	const string s = CorrectWeightsString(class_weights.value);
	if (s.empty()) return false;

	// Re-scanning the normalised form rather than the raw one: it is known to be
	// six plain integers, so this cannot fail in a way the caller has to handle.
	int n = sscanf(s.c_str(), "%d %d %d %d %d %d",
				   &out_w[0], &out_w[1], &out_w[2], &out_w[3], &out_w[4], &out_w[5]);
	return n == 6;
}

void	WED_RampPosition::SetClassWeights(const int w[6])
{
	char buf[64];
	snprintf(buf, sizeof(buf), "%d %d %d %d %d %d", w[0], w[1], w[2], w[3], w[4], w[5]);
	class_weights = CorrectWeightsString(buf);
	weights_mode  = true;						// setting weights is choosing to use them
}

bool	WED_RampPosition::WeightsInUse(void) const		{ return weights_mode.value != 0; }
void	WED_RampPosition::SetWeightsInUse(bool in_use)	{ weights_mode = in_use; }

void	WED_RampPosition::ClearClassWeights(void)
{
	// Back to "no 1313 row on this stand", which is NOT the same as all-zero.
	class_weights = string();
	weights_mode  = false;
}

string  WED_RampPosition::GetAirlines() const
{
	return airlines.value;
}

int	WED_RampPosition::GetWidth() const
{
	return width.value;
}

int	WED_RampPosition::GetWidthMin() const
{
	return width_min.value;
}

void WED_RampPosition::GetTips(Point2 c[4]) const
{
	double fuse_len, nose_offset, wingspan, wing_offset;
	switch (width.value)
	{
			case width_A: fuse_len = 11.0; wingspan = 14.0; nose_offset = 1.0; wing_offset =  -4.5; break;
			case width_B: fuse_len = 28.0; wingspan = 27.0; nose_offset = 2.7; wing_offset = -15.0; break;
			case width_C: fuse_len = 43.0; wingspan = 41.0; nose_offset = 4.7; wing_offset = -23.0; break;
			case width_D: fuse_len = 56.0; wingspan = 56.0; nose_offset = 9.5; wing_offset = -38.0; break;
			case width_E: fuse_len = 72.0; wingspan = 72.0; nose_offset = 8.2; wing_offset = -45.0; break;
			case width_F: fuse_len = 80.0; wingspan = 80.0; nose_offset = 8.8; wing_offset = -50.0; break;
	}

	Point2 nosewheel_loc;
	GetLocation(gis_Geo, nosewheel_loc);
	
	Vector2 nose_dir(0,1);
	nose_dir.rotate_by_degrees(-GetHeading());
	Vector2 right_dir(nose_dir.perpendicular_cw());

	if(ramp_type.value == atc_Ramp_Misc)
		c[0] = Point2(0,0) + nose_dir * fuse_len / 2.0;     // tip of nose
	else
		c[0] = Point2(0,0) + nose_dir * nose_offset;
		
	c[1] = c[0] + right_dir * wingspan / 2.0 + nose_dir * wing_offset;
	c[2] = c[0] - nose_dir * fuse_len;
	c[3] = c[0] - right_dir * wingspan / 2.0 + nose_dir * wing_offset;

	MetersToLLE(nosewheel_loc, 4, c);
}

int		WED_RampPosition::GetType() const
{
	return ramp_type.value;
}

void		WED_RampPosition::GetEquipment(set<int>& out_eq) const
{
	out_eq = equip_type.value;
}
