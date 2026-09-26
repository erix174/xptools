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

#ifndef WED_LIVERYPANEINTERNAL_H
#define WED_LIVERYPANEINTERNAL_H

// Shared by the files WED_LiveryPane is split across (WED_LiveryPane.cpp - state,
// selection, cards and coverage; ...Layout.cpp - where everything is; ...Input.cpp
// - mouse and edits; ...Draw.cpp - drawing and animation; ...Rows.cpp - building
// the list of rows). Not for use outside the pane.

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
#include "WED_LiveryRules.h"
#include "WED_LiveryAutoFill.h"
#include "WED_ToolUtils.h"
#include <chrono>
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

namespace livery_pane {


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

	enum WED_IcaoLookupResult
	{
		wed_Icao_Ok,				// found - out_country is the already IOC-normalized country for this ICAO
		wed_Icao_Placeholder,		// "xxxx" (WED's own default) or "zz.." (ICAO's reserved unassigned prefix)
		wed_Icao_NotFound,			// the database is loaded, but doesn't know this ICAO (or its country didn't normalize)
		wed_Icao_IndexUnavailable	// couldn't find/parse WED_AirportDatabase.txt at all (index stays empty)
	};

	extern bool sCoverageExpanded;
	extern const int kFilterEnumTable[5];
	extern const char * kFilterLabels[5];
	extern const char * kFilterLabelsShort[5];
	extern const char * kFilterTips[5];
	extern const int kWidthOrder[6];
	extern const char * kWidthLabels[6];
	extern const char * kSelectionTabTitle;

	string NoIndexSentence(WedDataFileError err);
	int CollectRamps(ISelectable * who, void * ref);
	set<string> ParseCodes(const string & airlines);
	string CodesToString(const set<string> & codes);
	string PopulateCaption(size_t n);
	double PaneClockNow(void);
	void	CardFlags(const vector<WED_LiveryDisplayRow> & rows, vector<bool> & out);
	void	RowIcaos(const vector<WED_LiveryDisplayRow> & rows, vector<string> & out);
	float	TrayFullHeight(size_t n_liveries);
	int WidthEnumToIndex(int enum_val);
	int IndexToWidthEnum(int idx);
	int RampOpToLiveryCategory(int ramp_op_enum);
	bool ContainsCaseInsensitive(const string & haystack, const string & needle_lower);
	string FindPlaceholderName(const string & icao_lower);
	string ResolveAirlineName(const string & icao_lower, const WED_AirlineDirectory & directory);
	bool CompareRowsByIcao(const WED_LiveryDisplayRow & a, const WED_LiveryDisplayRow & b);
	bool NotTheUnpaintedAirliner(const WED_LiveryDisplayRow & r);
	void SortAirlineRows(vector<WED_LiveryDisplayRow> & rows, bool descending);
	vector<string> GetManualRecommendedCodes(const string & icao_raw);
	WED_SectionResult AppendAirlineSection(const string & label, const vector<string> & codes_upper, const WED_AirlineDirectory & directory, bool sort_descending, const string & query_lower, set<string> & seen, bool leading_divider, vector<WED_LiveryDisplayRow> & out, bool preserve_order = false);
	vector<string> GetPopularAirlinesCodes(const WED_AirlineDirectory & directory, const string & airport_country_ioc, const string & airport_icao, const set<string> & checked_lower, map<string, vector<string> > & cache);
	void BuildDisplayRows(int ramp_op_enum, bool sort_descending, bool show_recommendation, const vector<string> & manual_codes_upper, const vector<string> & direct_hit_codes_upper, const string & airport_country_ioc, const string & airport_icao, const set<string> & checked_lower, map<string, vector<string> > & popular_cache, const string & search_query, const WED_AirlineDirectory & directory, const vector<pair<string,string> > & all_operators, vector<WED_LiveryDisplayRow> & out);
	void PinSelected(vector<WED_LiveryDisplayRow> & rows, const set<string> & selected, const set<string> & have_cards);
	void DropCardless(vector<WED_LiveryDisplayRow> & rows, const set<string> & have_cards);
	void PruneEmptySections(vector<WED_LiveryDisplayRow> & rows);
	void ApplyCollapse(vector<WED_LiveryDisplayRow> & rows, const set<string> & collapsed);
	WED_IcaoLookupResult LookupIcaoCountry(WED_AirportDatabase & db, const string & icao_raw, string & out_country);
	bool AirportIsCommercial(WED_AirportDatabase & db, const string & icao_raw);
	vector<string> GetRecommendedAirlineCodes(WED_AirportDatabase & db, const string & icao_raw);
	vector<WED_LiveryDisplayRow> BuildCurrentDisplayRows(WED_RampPosition * primary_ramp, bool sort_descending, bool show_recommendation, WED_AirportDatabase & airport_db, const string & current_icao, const string & search_query, WED_AirlineDirectory & directory, map<string, vector<string> > & popular_cache, const vector<pair<string,string> > & all_operators);
	string ElideToWidth(int font, const string & text, float max_w);
	vector<string> WrapText(int font, const string & text, float max_w);
	void DrawFilledCircle(float cx, float cy, float r);
	void DrawCircleOutline(float cx, float cy, float r);
	void DrawGrabberDashes(float cx, float cy, float r);
	void DrawStar(float cx, float cy, float r_outer, float r_inner);
	void DrawX(float cx, float cy, float r);

}	// namespace livery_pane
using namespace livery_pane;


// The slideshow's crossfade, seconds: the incoming face goes from transparent to solid.
static const float kCycleFadeSec = 0.2f;


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

static const char * kPopulateCaption = "Populate This Ramp";


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

#endif /* WED_LIVERYPANEINTERNAL_H */
