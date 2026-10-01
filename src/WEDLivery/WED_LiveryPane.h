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

/*
	WED_LiveryPane - THEORY OF OPERATION

	Self-contained "Liveries" tab for the property-side GUI_TabPane. Hosts the
	three pillars of ramp-start livery selection, migrated off the generic
	"Selection" property grid (see WED_RampPosition.cpp - properties prefixed
	with "." are hidden from that grid by an existing WED convention):

	1. Ramp Operation Type - a row of filter chips (None/GA/Airline/Cargo/
	   Military) at the top. Same property as before; its value now ALSO
	   filters the airline list below.
	2. Size - a dual-handle range slider over the six ICAO width categories
	   (A-F), backed by the existing "width" property (now the max) plus a
	   new "width_min" property. Not yet exported to apt.dat - see
	   WED_RampPosition.cpp/.h for why.
	3. Airlines - the checklist picker (unchanged from the prior pass),
	   filtered by the Ramp Operation Type chip.

	Selection tracking, tab enable/disable, and the input-blocking mask are
	unchanged from the prior pass.

	Every fact about an operator or a livery comes from the install's
	livery_index.txt (WED_LiveryIndex, WED_AirlineDirectory); which airlines
	serve an airport, from WED_AirportDatabase.
*/

#ifndef WED_LIVERYPANE_H
#define WED_LIVERYPANE_H

#include "GUI_Pane.h"
#include "GUI_Listener.h"
#include "GUI_Commander.h"
#include "WED_AirportDatabase.h"
#include "WED_AirlineDirectory.h"
#include "WED_LiveryIndex.h"
#include "WED_LiveryRules.h"		// WED_SharedLiveryData
#include "WED_LiveryThumbnailCache.h"
#include <vector>
#include <string>
#include <map>
#include <set>
#include <ctime>

// ONE DISPLAYED ROW. At file scope rather than inside the .cpp's anonymous
// namespace so the pane can CACHE the assembled list: rebuilding it - 150-odd
// operators, filtered, sorted and shuffled - was happening on every Draw, and
// Draw runs every frame for as long as a card is animating under the cursor.
enum WED_LiveryRowKind { wed_Row_Airline, wed_Row_Header, wed_Row_Gap, wed_Row_Divider, wed_Row_Note,
						 // the GA / military pool as an odds table
						 wed_Row_PoolBar, wed_Row_PoolClass, wed_Row_PoolItem, wed_Row_PoolLivery };

struct WED_LiveryDisplayRow
{
	WED_LiveryRowKind	kind;
	std::string			icao;			// lowercase, matches WED_RampPosition::CorrectAirlinesString's convention - wed_Row_Airline only
	std::string			name;			// display name if known, else empty (falls back to just the code) - wed_Row_Airline only
	std::string			header_text;	// wed_Row_Header only
	int					hidden_count;	// wed_Row_Header only: rows a collapse is holding back
	WED_LiveryDisplayRow() : kind(wed_Row_Airline), hidden_count(0) {}
};

struct	Point2;
class	IResolver;
class	WED_Archive;
class	WED_RampPosition;
class	GUI_TabPane;
class	GUI_Broadcaster;
class	GUI_GraphState;
class	GUI_TextField;

// GUI_Commander is not decoration. Every pane handed to GUI_TabPane::AddPane()
// ends up as a child of GUI_ChangeView, and GUI_ChangeView::SetSubView()
// dynamic_casts it to GUI_Commander so it can hand the tab keyboard focus
// (GUI_ChangeView.cpp:47). Without that base this pane fired
// DebugAssert(c != NULL) on every switch INTO the Liveries tab, and - past the
// assert - mSearchField could never join the focus chain, because the chain has
// no route into a pane that is not a commander. WED_PropertyPane and WED_TCEPane
// are both commanders for exactly this reason; this pane was the odd one out.
class	WED_LiveryPane : public GUI_Pane, public GUI_Commander, public GUI_Listener {
public:

						 WED_LiveryPane(
								IResolver *		resolver,
								WED_Archive *	archive,
								GUI_TabPane *	host_tabs);
	virtual				~WED_LiveryPane();

	virtual	void		Draw(GUI_GraphState * state);
	virtual	int			MouseMove(int x, int y);
	virtual	int			MouseDown(int x, int y, int button);
	virtual	void		MouseDrag(int x, int y, int button);
	virtual	void		MouseUp  (int x, int y, int button);
	virtual	int			ScrollWheel(int x, int y, int dist, int axis);

	// Discards every cached card thumbnail/flag texture - called both when this
	// pane's tab is switched away from (GUI_TabPane calls Hide() on the outgoing
	// tab) and from the destructor. See WED_LiveryThumbnailCache's own header for
	// why this eviction is the cache's caller's job, not something it does itself.
	virtual	void		Hide(void);

	virtual	void		ReceiveMessage(
								GUI_Broadcaster *	inSrc,
								intptr_t			inMsg,
								intptr_t			inParam);

	// Re-reads the current selection and syncs the tab's enabled state. Public so
	// WED_DocumentWindow can call it right after AddPane(), once this pane is
	// actually findable in the tab strip - see that call site for why.
	void				RebuildSelection(void);

private:

	// ---- layout ----
	// Single source of truth for section Y-ranges, so hit-testing and drawing
	// can never drift apart. Each section is separated from the next by
	// GapHeight() of empty space.
	float				AirportInfoHeight(int bounds[4]) const;
	float				HeaderHeight(void) const;
	float				FilterRowHeight(void) const;
	float				SliderHeight(void) const;
	// 0 when the selection has no weights at all, which collapses the whole
	// section away - see WeightsYRange(). A stand only gets weights by being
	// asked for them, so an author who never presses the button never gets a
	// 1313 row on anything (R17).
	float				WeightsHeight(void) const;
	float				CoverageHeight(void) const;
	float				ListToolbarHeight(void) const;
	float				GapHeight(void) const;
	void				AirportInfoYRange(int bounds[4], float & top, float & bot) const;
	void				HeaderYRange(int bounds[4], float & top, float & bot) const;
	void				FilterYRange(int bounds[4], float & top, float & bot) const;
	void				SliderYRange(int bounds[4], float & top, float & bot) const;
	void				WeightsYRange(int bounds[4], float & top, float & bot) const;
	void				CoverageYRange(int bounds[4], float & top, float & bot) const;

	// ---- spawn weight bars (apt.dat row 1313) ----
	// Six draggable bars, one per ICAO wingspan class, sharing the size
	// slider's horizontal track so each bar sits directly under its own A-F
	// tick label. Hand-drawn, like everything else in this pane: WED has no
	// multi-value numeric control anywhere to reuse.
	bool				SelectionWeights(int out_w[6]) const;	// false if none, or if the selection disagrees
	bool				SelectionHasWeights(void) const;
	// Every selected stand is a legacy GA or military one: the tab shows it as it
	// will be after the 12.5 export (equal weights from A to the letter), not the
	// step-down an older X-Plane still uses for it (Eric, 2026-09-30).
	bool				SelectionLegacyEqualOnExport(void) const;
	bool				SelectionAllWeights(void) const;	// every selected stand has weights in use
	// The weight bars show only when the selection has weights AND the author has not asked for Simple Mode,
	// which is a view of the same data as a size range - not a way back to the legacy format.
	bool				ShowWeightBars(void) const;
	// The size slider's two balls, as class indices: a legacy stand from A (the step-down reaches it) to its
	// letter, a stand with weights from its smallest to its largest weighted class.
	void				SliderRange(int & lo, int & hi) const;
	int					WeightBarForXY(int bounds[4], int x, int y) const;	// -1 if not on one
	int					WeightValueForY(int bounds[4], int y) const;		// snapped to an integer, clamped
	int					WeightTrackMax(void) const;			// 10, or higher if an imported file needs it
	void				WeightBarRect(int bounds[4], int idx, float r_out[4]) const;
	void				ApplyWeightDrag(void);				// writes mDragWeights to every selected ramp
	void				AbortWeightDrag(void);				// rolls back an unfinished drag; no-op if none

	// The two ways in and out of having weights at all. Seeding is the ONLY
	// path that creates a 1313 row, and clearing returns the stand to "no row",
	// which is NOT the same as six zeros - that is a legal way to say nothing
	// parks here (spec §4.2), and the UI has to offer both.
	// The button toggles a MODE, it does not delete data. Going to simple mode
	// stashes the stand's weights in mWeightCache first, so coming back restores
	// what the author had rather than re-seeding from the size range and losing
	// their distribution - unless the author changed the size letter while in
	// simple mode, in which case it reseeds from the new letter. Keyed by the persistent ID, not by pointer: an undo
	// can destroy and rebuild the object, and a pointer key would then either
	// miss or, worse, hit a recycled address.
	//
	// Session-only on purpose. It is a convenience for toggling back and forth
	// while editing, not a second place where weights live - the entity property
	// remains the single source of truth, and nothing here reaches apt.dat.
	void				SeedWeightsFromSizeRange(void);
	void				WeightButtonRect(int bounds[4], float b_out[4]) const;
	// "Populate This Ramp", right-aligned on the ramp name's row, single selection only.
	void				PopulateButtonRect(int bounds[4], float b_out[4]) const;
	void				PopulateThisRamp(void);
	// "Clear", left of Populate: removes every airline from the selected stands.
	void				ClearButtonRect(int bounds[4], float b_out[4]) const;
	void				ClearAirlines(void);
	std::map<int, std::string>	mWeightCache;
	void				ListToolbarYRange(int bounds[4], float & top, float & bot) const;
	float				ContentTop(int bounds[4]) const;		// top Y of the airline checklist

	// ---- coverage readout (WED_LiveryFormatSpec.md §4.5) ----
	// The pre-`1313` form of the P(empty) readout. The spec's weighted version
	// needs class weights, which no entity carries yet (roadmap phase 4) - but the
	// defect those weights would expose is already measurable from data WED has
	// today: a stand whose listed operators have no model in ANY class of its own
	// size range parks nothing, every time, with no symptom in the sim. That is
	// the §4.5 case (29.8% of stands), and this is what makes it visible before it is written.
	//
	// When phase 4 lands, the same per-class terms get weighted by `1313` instead
	// of treated as a flat range, and this becomes a probability. The shape of the
	// computation does not change.
	struct Coverage {
		bool	index_ready;		// false => index missing/unreadable. MUST be shown as
									//   such and never as "0%" - see spec §4.5 and §6.4.
		std::string	index_version;	// what the numbers were resolved against (§4.5)
		int		stands;				// stands examined
		int		stands_empty;		// ...of which park nothing, over their own range
		// Set when the single selected stand carries a 1313 row. Then p_occupied
		// is the real §4.5 quantity - 1 - P(empty), weighted by the author's own
		// class distribution - rather than the flat-range approximation.
		bool	weighted;
		float	p_occupied;			// 0..1, only meaningful when `weighted`
		// Why a weighted stand parks nothing. Spec §4.5 point 2 notes the causes
		// are indistinguishable from outside the editor, and §4.5's whole claim
		// is that the readout is where they get told apart - so it has to
		// actually tell them apart. R14's table says the last two must not be
		// treated alike: one is an author error a click repairs, the other is an
		// author correctly working ahead of the art.
		enum EmptyCause {
			empty_None = 0,		// something spawns
			empty_ByChoice,		// all six weights zero - the author said so (§4.2)
			empty_Unfillable,	// the listed operators have nothing at a weighted class
			empty_NoArtYet,		// NOTHING in the library has anything at those classes
			empty_OutOfRange	// they have it, but no hub is within the aircraft's range (R26)
		};
		int		empty_cause;
		// Single-stand detail. Meaningless (and not drawn) when stands != 1.
		int		classes_in_range;
		int		classes_filled;
		int		airlines_listed;
		int		airlines_eligible;	// at least one model somewhere in the range
		// The operator that will park here when it is the ONLY one that can.
		// Empty unless airlines_eligible == 1 and more than one was listed -
		// see the audit: 17.5% of multi-operator stands collapse to exactly one
		// airline forever, which produces aircraft, looks fine, and is invisible
		// to every P(empty) check in the design.
		std::string	sole_operator;
		char	lo_class, hi_class;	// 'A'..'F'
		// Stands that draw from the whole library rather than from their list:
		// general aviation always (R28), and military with no operator listed
		// (spec §4.1). Single-stand only. pool_models counts the liveries that can
		// park here at the stand's sizes; pool_home the GA ones registered in the
		// airport's country (R28's 70%). countries: the military operators' home
		// countries - the pool's, or the eligible listed operators' - with ""
		// standing for the generic XPMI airframes.
		bool	pool_mode;
		int		op_type;			// ramp_operation_*, single stand
		int		pool_models;
		int		pool_home;
		int		pool_refused_equip;		// pool liveries at these sizes the equipment type turns away
		int		pool_refused_home;		// ... that may park only in their own country (R27)
		std::set<std::string>	countries;
	};
	void				RecomputeCoverage(void);
	// Cheap because the index is already resident; still not something to run per
	// frame at CDG (326 stands x 6 classes x n airlines), hence the dirty flag.
	Coverage					mCoverage;
	bool						mCoverageDirty;
	// ONE CARD PER OPERATOR, not per livery. An operator with three aircraft at
	// this stand's classes is one card that cycles through them, not three cards
	// captioned with the same airline - which is what the per-livery model showed
	// and read as a duplication bug.
	//
	// KEYED BY ICAO RATHER THAN HELD IN A PARALLEL VECTOR. The display rows are
	// rebuilt from scratch on every hover, click and draw (BuildCurrentDisplayRows),
	// so anything indexed by row position desyncs the moment a search term or a
	// sort order changes the row list under it. A row carries its icao, the card
	// data is looked up by that, and the two cannot disagree.
	struct AirlineCard {
		std::string					icao;			// UPPERCASE, as the caption shows it
		std::string					name;			// friendly name, or the code if unknown
		std::string					ioc_country;	// for the flag icon
		std::vector<std::string>	abs_paths;		// THE CACHE KEYS, one per livery
		std::vector<std::string>	types;			// parallel: ICAO type designator
		// Parallel too: the type with its livery note when the note distinguishes
		// it - "B738 (Retro)". United ships Legacy AND Modern 737s and 757s, so the
		// designator alone listed the same aircraft twice with nothing to tell the
		// two apart, which reads as a bug in the list rather than two real liveries.
		std::vector<std::string>	labels;
		// Ordered biggest wingspan class first, and within a class reverse
		// alphabetically, so index 0 is the one a card shows at rest.
		// A pool card: one aircraft type from the library X-Plane draws from by
		// size (GA always, military with no operator listed). Shown so the author
		// sees what may park, but it is not an operator - nothing to tick or hold.
		bool						preview = false;
		std::vector<std::string>	codes;			// pool cards: each livery's operator, lowercase
		// How often X-Plane parks this type here, 0..1, when the stand draws from
		// the pool; -1 when it does not (an operator card, or a military stand with
		// an operator listed). Each label carries its own livery's share too.
		float						prob = -1.0f;
		int							cls = -1;		// pool cards: the type's size class, 0 = A
		std::vector<std::string>	ctys;			// pool cards: each livery's registration country
	};
	std::map<std::string, AirlineCard>	mAirlineCards;		// key: LOWERCASE icao, as rows carry it
	// Liveries the range rule removed from a card at this stand, by UPPERCASE
	// operator code - so the readout can name what will not spawn and why.
	std::map<std::string, std::vector<std::string> >	mRangeHidden;
	// True while some cards are pool previews (GA and military stands), which
	// EnsureRows gathers into their own section; see AirlineCard::preview.
	bool								mCardsByType;
	// Liveries of this stand's operation type and sizes that RebuildAirlineCards
	// left out, by reason - so an empty list can say why it is empty instead of
	// suggesting an operator that is not there to tick (Dellanie, EGLF).
	int									mCardsRefusedEquip;
	int									mCardsRefusedHome;
	int									mCardsRefusedRange;
	std::string							EmptyListReason(int ramp_op) const;
	// Liveries the equipment type alone keeps out, by the equipment that would
	// let each in (atc_Heavies ... atc_Fighters) - only ones every other rule
	// already allows. Feeds "Try another Equipment Type?" (Eric, 2026-09-30).
	std::map<int, int>					mEquipGain;
	std::string							EquipmentSuggestion(void) const;
	// "Try a larger size? C: AAL, DAL, UAL" - the first size above the stand's at
	// which listed operators have an aircraft that may park here (Eric, KBTV).
	std::string							SizeSuggestion(void) const;
	void								GatherPoolPreview(void);
	// The pool table. Class odds from RebuildAirlineCards; which
	// classes the user opened or shut (default: open when >= 5%); which types
	// are expanded to their liveries; per-row heights for LayoutRows.
	double								mPoolClassP[6];
	double								mPoolHomeShare = 0;	// military: the home forces' (tickable cards') share of the pool
	std::map<int, bool>					mPoolClassOpen;
	std::set<std::string>				mPoolExpanded;
	std::vector<float>					mRowH;
	bool								PoolClassIsOpen(int k) const;
	// The operator a pool row's lock stands for: a livery row's own, a type row's
	// when all its liveries share one; "" for none (no badge then).
	std::string							PoolLockCode(const WED_LiveryDisplayRow & row) const;
	// Whether an operator may appear on a stand of this operation type.
	bool								OperatorMatchesRampOp(const std::string & code_uc, int ramp_op) const;
	// Whether one livery may appear at this stand, and if not, why - see the .cpp.
	enum Allow { allow_Yes, allow_OutOfRange, allow_ForeignMilitary, allow_Equipment };
	Allow								LiveryAllowedHere(const WED_LiveryIndexEntry & e, const Point2 & here,
														  const std::set<int> & equipment) const;
	// An operator's home country (IOC) from the directory; the code itself when
	// the directory does not know it, so the readout never names a blank.
	std::string							OperatorCountry(const std::string & code_uc) const;
	std::string							mAirportCountry;	// IOC, from the airport strip's lookup; empty when unknown
	// Lines the coverage readout currently needs; see CoverageHeight().
	int									mCoverageLineCount;
	bool								mCoverageHasDetail;		// there is something to expand to
	bool								mHoverCoverageToggle;
	// The headline row of the readout toggles its detail; see Draw.
	bool				CoverageToggleHit(int bounds[4], int x, int y) const;
	void								RebuildAirlineCards(void);
	const AirlineCard *					CardFor(const std::string & icao_lower) const;
	void								CardKeys(std::set<std::string> & out) const;
	// (lowercase icao, display name) for every operator that has a card here. The
	// source for the "All Airlines" tier, which used to read a hand-written list.
	std::vector<std::pair<std::string,std::string> >	AllOperators(void) const;


	// ---- livery preview cards ----
	// Cards ARE the airline rows of the scrolled content, not a separate viewport
	// with its own scrollbar - one mScrollOffset moves the whole list, so a section
	// header can never get stranded off the bottom of the tab.
	//
	// CardHeight() takes bounds because the image's height derives from the pane's
	// ACTUAL width (fixed 16:9) - a card is exactly as tall as its full-width image
	// plus one text row, so it must never be computed from a nominal/guessed width
	// (that mismatch once rendered the whole block as one oversized black slab - see
	// DrawAirlineCard()'s image_h, which must use this SAME bounds-derived width).
	float				CardWidth(int bounds[4]) const;			// one column's width, gaps already taken out
	float				CardHeight(int bounds[4]) const;

	// WHERE EVERY DISPLAY ROW SITS. Airline rows are cards laid out kCardCols to a
	// line; headers, dividers and notes stay full-width single lines between them.
	// Draw() and every hit test run this same pass, because the previous design had
	// the formula written out in both and they drifted - a click in the bottom of
	// the card strip used to toggle the first airline's checkbox.
	//
	// A run of cards that does not fill its last line leaves the remaining columns
	// empty rather than centring what is left, so a lone final card sits under the
	// first column with whitespace to its right.
	struct RowSlot {
		float	top, bot;		// the CARD's own extent (or the text row's), already scrolled
		float	slot_bot;		// bottom including any open tray; == bot when none
		float	x0, x1;			// horizontal extent - the card's own, or the full row
		bool	is_card;
	};
	bool								PoolLockRect(int b[4], const RowSlot & slot, float r_out[4]) const;
	void								DrawPoolRow(GUI_GraphState * state, int b[4], const RowSlot & slot,
													const WED_LiveryDisplayRow & row, int & renders_this_frame,
													std::set<std::string> & keep_alive);
	// Fills one slot per row, and returns the total content height so the caller can
	// clamp mScrollOffset. Takes only "is this row a card", not the rows themselves:
	// WED_LiveryDisplayRow is private to the .cpp, and the layout genuinely needs
	// nothing else, so this stays a pure function of its arguments.
	// `tray_h` is per row and usually all zeros: the extra height an open tray adds
	// under a card. It is applied to the whole GRID LINE, not to one card, because a
	// tray that pushed only its own column down would slide out from under its
	// neighbour and overlap the line below.
	float				LayoutRows(int bounds[4], const std::vector<bool> & is_card,
								   const std::vector<float> & tray_h,
								   std::vector<RowSlot> & out) const;
	// The row under the cursor, card or not; -1 for none. Replaces RowForY, which
	// could not tell which column of a card line was hit.
	int					RowForXY(int bounds[4], const std::vector<bool> & is_card,
								 const std::vector<float> & tray_h, int x, int y) const;

	// Sub-targets inside one card. Both are carved off the card's own rect so they
	// cannot drift from what DrawAirlineCard paints.
	void				LockIconRect(const RowSlot & slot, float r_out[4]) const;
	void				TrayTabRect (const RowSlot & slot, float r_out[4]) const;

	void				DrawAirlineCard(GUI_GraphState * state, const RowSlot & slot,
										const AirlineCard & card, int show,
										bool is_selected, bool is_hover, bool is_pressed,
										bool is_locked, bool is_dimmed,
										float tray_open,
										int & renders_this_frame,
										int fade_from = -1, float fade = 1.0f);
	void				DrawHoverTip(GUI_GraphState * state, int bounds[4]);
	void				DrawCardTray(GUI_GraphState * state, const RowSlot & slot,
									 const AirlineCard & card, float open_frac, int lit_row);
	// Which tray row (if any) the point is over. -1 when no tray is open, when the
	// point is elsewhere, or while the tray is still sliding - a target that moves
	// under the cursor is not a target.
	int					TrayRowForXY(const std::vector<std::string> & row_icaos,
									 const std::vector<RowSlot> & slots, int x, int y);
	// Takes the rows' icao codes, not the rows: WED_LiveryDisplayRow lives in the
	// .cpp's anonymous namespace and cannot be named here. An empty string means
	// "not an airline row".
	void				TrayHeights(const std::vector<std::string> & row_icaos,
									std::vector<float> & out) const;
	const WED_LiveryThumbnail *	EnsureRawFlagTexture(const std::string & ioc_country_code);	// no masking - see .cpp

	// ---- ramp operation filter (row of chips) ----
	int					FilterChipForXY(int bounds[4], int x, int y) const;	// -1 if none
	void				SetRampOpFilter(int wed_ramp_op_enum);

	// ---- size range slider ----
	// Handle IDs used for hit-testing/hover only: -1 none, 0 = nearer the min ball,
	// 1 = nearer the max ball, 2 = the two balls currently overlap. Dragging itself
	// doesn't care which ball you grabbed - see the anchor/moving design below.
	int					SliderHandleForXY(int bounds[4], int x, int y) const;
	float				SliderContinuousIndexForX(int bounds[4], int x) const;	// float in [0,5], unsnapped

	// A drag tracks the OTHER (unmoving) ball as an anchor and the grabbed ball's
	// live position as "current". min/max are re-derived every update as
	// min(anchor,current)/max(anchor,current), so dragging straight through the
	// other ball and out the far side "just works" - no special-casing needed.
	void				ApplyDragRange(void);

	// ---- list toolbar (sort + show-recommendation buttons) ----
	// Both buttons live in one row, right-aligned as a pair against the tab's
	// right border: [Show Recommendation][Sort A-Z/Z-A]|. Rects computed from
	// bounds alone (no stored state) so hit-testing and drawing never drift.
	void				SortButtonRect(int bounds[4], float b_out[4]) const;
	void				RecommendButtonRect(int bounds[4], float b_out[4]) const;
	// Everything left of RecommendButtonRect(), down to the box's own left
	// edge - the search field auto-fills whatever room that leaves.
	void				SearchFieldRect(int bounds[4], float b_out[4]) const;
	// Small square carved out of SearchFieldRect()'s own right edge - only
	// meaningful (and only drawn/hit-tested) while the field has text in it;
	// see Draw()'s search-box block for why this doesn't overlap the actual
	// GUI_TextField's bounds.
	void				SearchClearButtonRect(int bounds[4], float b_out[4]) const;

	// ---- airline checklist ----
	int					RowForY(int bounds[4], int y) const;
	void				ToggleCode(const std::string& icao);
	void				AbortSizeDrag(void);	// rolls back an unfinished slider drag; no-op if none
	int					CountRampsWithCode(const std::string & icao) const;	// for the tri-state checkbox

	IResolver *					mResolver;
	WED_Archive *				mArchive;
	GUI_TabPane *				mHostTabs;

	std::vector<WED_RampPosition *>	mSelectedRamps;


	// Opt-in mirror image of the above (see gPromptLiveriesOnRampSelect in
	// WED_Globals.h): the last ramp-only selection we auto-switched INTO this
	// tab for, so a repeat of the exact same selection doesn't re-trigger but
	// any different one (or a fresh reselection after going empty) does.
	std::vector<WED_RampPosition *>	mLastAutoSwitchedInRamps;

	int							mTrackRow;			// airline row being clicked, -1 if none
	int							mTrackFilterChip;	// filter chip being clicked, -1 if none
	int							mHoverFilterChip;	// filter chip currently under the mouse, -1 if none
	int							mHoverRow;			// airline row currently under the mouse, -1 if none
	int							mHoverSliderHandle;	// slider handle currently under the mouse (0/1), -1 if none

	// Sort direction for the airline list - in-memory only (not persisted;
	// resets to the default A-Z each time WED starts). Applies to both
	// halves of the list when "Show Recommendation" is on.
	bool						mSortDescending;
	bool						mHoverSortButton, mTrackSortButton;
	bool						mHoverRecommendButton, mTrackRecommendButton;

	// Live-filter search box, left of the Recommend button. GUI_TextField
	// always paints its own opaque box, so there's no native placeholder -
	// we hide it and hand-draw a "Lookup Operators" placeholder instead
	// whenever it's both empty AND unfocused (see Draw()/MouseDown()).
	// mSearchQuery mirrors the field's live text (kept in sync via
	// ReceiveMessage(GUI_TEXT_FIELD_TEXT_CHANGED)) so BuildDisplayRows() can
	// filter without asking the widget directly.
	GUI_TextField *				mSearchField;
	std::string					mSearchQuery;
	bool						mHoverClearButton, mTrackClearButton;

	// Vertical scroll position of the airline checklist, in pixels (0 = top,
	// larger = scrolled further down). ScrollWheel() adjusts it; Draw() re-
	// clamps it to [0, max] every frame against the CURRENT row count (so a
	// filter/search change that shrinks the list can never leave it scrolled
	// past the new end), and RowForY() reads it to keep hit-testing in sync
	// with whatever Draw() last actually painted.
	float						mScrollOffset;
	// How far the whole top of the tab (flag, stand name, operation types, size,
	// readout) has rolled up out of view, 0 = not at all. The wheel over the list
	// rolls this first and then the list, and back the other way - so on a short
	// pane (1080p) the list can take the whole tab. Every section's Y range is
	// chained off AirportInfoYRange, which is the only place that reads it.
	float						mPageScroll;
	float						PageScrollMax(int bounds[4]) const;
	float						FlagBannerWidth(int bounds[4]) const;

	// The airline code actually feeding the country/flag lookup this Draw()
	// call (already the metadata-preferred, fallback-resolved code - see the
	// top of Draw()) - cached so the "Show Recommendation" button's enabled
	// state can be checked from MouseDown() too, not just at Draw() time.
	// Empty when nothing resolved (no flag/no recommendation data either).
	std::string					mCurrentAirportIcao;

	// "Popular Airlines" tier's per-airport, session-stable random draw - see
	// GetPopularAirlinesCodes()'s doc comment in the .cpp for the full algorithm.
	// Computed once per airport ICAO the first time it's needed, then reused for
	// the rest of this WED run (not re-shuffled on every Draw(), and not reset
	// when switching away and back to the same airport) - keyed by airport ICAO
	// so different airports naturally get independent draws.
	std::map<std::string, std::vector<std::string>>	mPopularAirlinesShuffleCache;

	// Drag state. mDragAnchorIndex is the OTHER ball's fixed position for the
	// whole gesture; mDragCurrentIndex is the grabbed ball's live, detent-snapped
	// position. mDragHandle just tracks which side "current" is presently on
	// (0 = acting as min, 1 = acting as max, 2 = coincides with the anchor) so the
	// hover/highlight ring follows the right ball - it has no effect on the
	// min/max values actually written, which always come from
	// min(anchor,current)/max(anchor,current).
	int							mDragHandle;		// -1 when not dragging
	int							mDragAnchorIndex;
	int							mDragCurrentIndex;
	int							mDragStartIndex;	// the grabbed ball's class when the press began; unchanged -> no undo entry
	bool						mSimpleView;		// Simple Mode: the size range, not the bars, for a stand with weights

	// Weight-bar drag. mDragWeightBar is the grabbed bar, -1 when idle; the
	// gesture is locked to it, so sliding sideways never paints across its
	// neighbours. mDragWeights is the live vector being written; mDragWeights0
	// is what it was when the mouse went down, so MouseUp can abort instead of
	// commit when nothing actually moved - a click that lands on a bar's
	// existing height must not leave an entry on the undo stack.
	int							mDragWeightBar;
	int							mDragWeights[6];
	int							mDragWeights0[6];
	// The track's scale, frozen for the length of a drag. WeightTrackMax() follows
	// the tallest bar, so while that bar was the one being dragged every mouse
	// move rescaled the track under the cursor: the bar stayed full height and
	// the value halved itself away on each event.
	int							mDragTrackMax;
	int							mHoverWeightBar;	// -1 when the cursor is off the bars
	bool						mHoverWeightButton, mTrackWeightButton;
	bool						mHoverPopulate, mTrackPopulate;
	bool						mHoverClearAirlines, mTrackClearAirlines;
	std::string					mClearFlash;			// the Clear button's caption for a moment after a click
	double						mClearFlashUntil;
	// What the last Populate did, shown on the button for a few seconds instead of
	// a dialog (a modal inside a mouse handler is what once left the click count stuck).
	std::string					mPopulateFlash, mPopulateDetail;
	double						mPopulateFlashUntil;

	// The airport database (WED_AirportDatabase.txt beside WED: country and
	// who serves each airport) and the operator directory (the OPERATOR records
	// of the install's livery_index.txt) are NOT the tab's own: it reads the
	// one shared instance the validator, auto-fill and moderation read
	// (WED_SharedLiveryData), so the tab can never judge a stand from different
	// data than the warning does. Fetched per use - a folder change replaces it.
	WED_AirportDatabase &		AirportDb(void) const		{ return WED_SharedLiveryData().airports; }
	WED_AirlineDirectory &		Directory(void) const		{ return WED_SharedLiveryData().directory; }

	// The shipped static-aircraft index (livery_index.txt, inside the selected
	// X-Plane folder - NOT next to WED.exe like the two above). This is the first
	// consumer the class has ever had; see the appendix of WED_LiveryFormatSpec.md.
	// Loaded lazily and re-loaded whenever the resolved path changes, which is how
	// it survives the user switching X-Plane folders mid-session.
	WED_LiveryIndex &			mLiveryIndex;		// WED_SharedLiveryIndex() - one per process

	// ---- country flag banner (WED_FlagProjector / WED_FlagAssets) ----
	// Re-projected only when the displayed country actually changes (this is
	// a ~2048x768-pixel warp+composite, not something to redo every Draw()).
	// mFlagTexCountry == "" means "no texture uploaded yet / nothing to show".
	void			EnsureFlagTexture(const std::string & ioc_country_code);
	std::string		mFlagTexCountry;
	unsigned int	mFlagTexId;
	int				mFlagTexW, mFlagTexH;

	// Always exactly half the tab's width, flush to the pane's left border
	// and to the strip's own top edge. Single source of truth so the
	// info-text x-offset (computed early) and the actual draw (issued last,
	// so the banner paints over whatever it overlaps) always agree.
	void			FlagBannerRect(int bounds[4], float strip_top, float strip_bot,
						float & out_x, float & out_y, float & out_w, float & out_h) const;

	// Word-wrapped info_text/status_text lines, recomputed once near the top
	// of every Draw() call (before AirportInfoYRange() is first used) so
	// AirportInfoHeight() can size the tray to fit them on the SAME frame
	// they change, rather than a frame late - same reasoning as the flag
	// texture ordering requirement documented at the top of Draw().
	std::vector<std::string>	mCachedInfoLines;
	std::vector<std::string>	mCachedStatusLines;

	// ---- livery preview cards (framework/scaffolding only) ----
	WED_LiveryThumbnailCache	mThumbCache;
	std::string					mLiveryIndexPath;	// last resolved path; a change means a new X-Plane root

	// One card per livery that can actually spawn on the selected stand. Built
	// from the index by RebuildPreviewCards(), which asks it the SAME question
	// RecomputeCoverage() asks - so the cards and the readout above them cannot
	// end up describing different aircraft.
	//
	// Static previews, not a picker. The selection state these used to carry
	// served the per-stand aircraft-type whitelist that draft 7 deleted (spec
	// §8.6); a tick on a card has nothing left to mean, and since the list is
	// rebuilt whenever the operators or the weights change, a mark kept by index
	// would drift onto a different aircraft anyway.

	// Click-and-drag scrolling of the content area ("grab and pull", same feel as a
	// touch scroll) - moves mScrollOffset, so cards and checklist move together.
	// mContentDragStartY < 0 means no drag in progress; mContentDragStartOffset is
	// the scroll position the gesture started from, so the live offset always derives
	// from the gesture's TOTAL cursor movement rather than accumulated per-move
	// deltas (which would drift over a long drag).
	int							mContentDragStartY;
	int							mContentDragStartX;		// so MouseUp can tell a click from a scroll
	float						mContentDragStartOffset;

	// Advances whatever is in motion by the wall-clock time since the last frame
	// and reports whether anything still is, so Draw() can ask for another frame.
	// NOT a GUI_Timer: see the .cpp on why that never fired here.
	bool						StepAnimation(void);
	double						mLastAnimClock;		// PaneClockNow() seconds; 0 = not running

	// ---- hover cycling ----
	// A hovered card steps through its operator's other aircraft once a second, so
	// one card can answer "what else do they park here" without being opened. The
	// cycle is identified by ICAO, not row index, for the same reason the cards are
	// - rows are rebuilt constantly and an index would land on a stranger.
	std::string					mCycleAirline;		// empty when nothing is cycling
	int							mHoverX, mHoverY;	// last cursor position, for the hover tip
	// Which row of the open tray the cursor is on, -1 for none. While it is set the
	// slideshow HOLDS on that aircraft instead of advancing, and when the cursor
	// leaves the cycle resumes from there rather than from wherever it would have
	// got to - so pointing at one aircraft to look at it does not cost you your
	// place in the sequence.
	int							mTrayHoverIdx;
	// What the tip should say this frame, decided while the cards are drawn (that
	// is where the sub-rects are known) and rendered after the clip is popped.
	// Empty means no tip.
	std::string					mHoverTipText;

	// ---- the assembled row list, cached ----
	// Everything that reads the list goes through EnsureRows(). Rebuilding it costs
	// a directory pass over ~150 operators plus a sort, a search filter and the
	// per-airport shuffle, and it was being paid on every Draw - which, while a
	// card animates under the cursor, is every frame.
	//
	// INVALIDATED EXPLICITLY, by SetRowsDirty(), from each of the handful of places
	// that can change what the list contains. A cache that guesses when to refresh
	// is worse than none: the failure is a list that silently disagrees with the
	// document, and the layout and every hit test are derived from it.
	std::vector<WED_LiveryDisplayRow>	mRows;
	std::vector<bool>					mRowIsCard;
	std::vector<std::string>			mRowIcaos;
	bool								mRowsDirty;
	void								EnsureRows(void);
	void								SetRowsDirty(void) { mRowsDirty = true; }
	int							mCycleShow;			// which livery is on the face
	float						mCycleAccum;		// seconds since the last step
	int							mCyclePrevShow;		// the face being faded out
	bool						mCycleNextReady;	// the next face is loaded - the cycle waits for it
	float						mCycleFade;			// seconds into the crossfade; >= kCycleFadeSec when done

	// ---- the tray ----
	// Opening one tray closes whichever was open, and the two animate TOGETHER -
	// one extending while the other retracts - so the list never jumps.
	std::string					mTrayAirline;		// the tray opening/open; empty for none
	float						mTrayOpen;			// 0..1
	std::string					mTrayClosing;		// the tray retracting; empty for none
	float						mTrayClosingOpen;	// 1..0

	// ---- the exclusive lock ----
	// "Only this operator parks here." At most one card can hold it, and while one
	// does every other card is drawn dimmed, because the lock has taken them out of
	// the running - see the format spec on why this is UI-only and never reaches
	// apt.dat as a row of its own.
	std::string					mLockedAirline;		// the stand's only listed code, lowercase; "" if it lists 0 or 2+
public:
	// THE LOCK IS DATA (Eric, 2026-09-30): a stand whose 1301 lists one code is
	// locked to it. Locking cuts the list to that code (the old list kept on the
	// stand, see GetAirlinesBeforeLock); unlocking puts the old list back.
	void						ToggleLock(const std::string & code_lc);
	std::string					LockedCode(void) const;
private:

	// Section headers that are collapsed. Only "All Airlines" starts collapsed: it
	// is the tier with no filter behind it, so expanding it can mean hundreds of
	// cards, and rendering those unasked is the one way this pane can stall.
	std::set<std::string>		mCollapsedSections;

	std::map<std::string, WED_LiveryThumbnail>	mRawFlagTex;	// unmasked flag icon textures (w/h = source PNG's own), keyed by IOC country code

};

#endif /* WED_LIVERYPANE_H */
