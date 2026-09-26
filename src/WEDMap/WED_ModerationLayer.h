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

#ifndef WED_MODERATIONLAYER_H
#define WED_MODERATIONLAYER_H

/*
	WED_ModerationLayer - THEORY OF OPERATION

	A callout per selected ramp start, drawn on the map for a Gateway moderator
	reading a submission stand by stand. A leader runs from the stand to a card:

		[flag] ZBAA  02-PIN-C                    (pin)
		Airlines   DAL UAL
		Updated · Manual   C60 D30 E10           (or: Legacy · size C)
		Passenger · Heavy Jets, Jets · Gate
		v 1 to check                             <- hover: the operator tray

	The tray lists the operators three to a row with their flags and a verdict -
	see WED_ModerationDescribe() for what is checked against what. A "?" opens a
	web search for that operator at that airport.

	COLOUR IS SIMILARITY. The leader and the card's stroke take a colour from the
	stand's signature (operation type, airline set, size or weights): stands that
	would park the same thing share a colour, different ones get colours spread
	round the hue circle (golden-ratio steps), so a moderator sees at a glance
	which stands were copied from which. The first hue is random per session.

	COMPARE. The pin on a card makes it the base: every other card then shows,
	GitHub-style, what it has that the base does not (+, green), what it lacks
	(-, red) and what is different (~). The pinned stand keeps its card while
	other stands are selected.

	Selecting an airport or a group counts as selecting its ramp starts, as for
	auto-fill. Shown while WED_ModerationEnabled() - today always; Moderation
	Mode only once that exists.
*/

#include "WED_MapLayer.h"
#include "WED_LiveryModeration.h"
#include <map>
#include <string>
#include <vector>

class WED_RampPosition;

// The colour of a ramp start's callout, for the layers that draw its aircraft
// silhouette: while a stand has a card, its outline wears the card's colour
// instead of the default green, so a stand and its card read as one even where
// cards cannot sit next to their stands. False when the stand has no card.
// (From the last frame drawn - the silhouettes are drawn before the callouts.)
bool	WED_ModerationTintFor(const WED_RampPosition * ramp, float out_rgb[3]);

class	WED_ModerationLayer : public WED_MapLayer {
public:

						 WED_ModerationLayer(GUI_Pane * host, WED_MapZoomerNew * zoomer, IResolver * resolver);
	virtual				~WED_ModerationLayer();

	virtual	int			HandleClickDown(int inX, int inY, int inButton, GUI_KeyFlags modifiers);
	virtual	void		HandleClickUp  (int inX, int inY, int inButton, GUI_KeyFlags modifiers);
	virtual	void		DrawSelected(bool inCurrent, GUI_GraphState * g);
	virtual	void		GetCaps(bool& draw_ent_v, bool& draw_ent_s, bool& cares_about_sel, bool& wants_clicks);

private:

	struct Flag { unsigned int tex; int w, h; };

	struct Hit {
		enum Kind { hit_Card, hit_Pin, hit_Tray, hit_Search };
		int				kind;
		float			x0, y0, x1, y1;
		int				ramp_id;
		std::string		url;			// hit_Search
	};

	struct Callout {
		WED_RampPosition *		ramp;
		int						id;
		WED_ModerationEntry		e;
		float					ax, ay;		// the stand, in pixels
		float					x0, y0, x1, y1;	// the card
		float					tray_y;		// top of the tray row
		std::vector<std::string>	diff;	// "+ CSN CES", "- UAL", "~ ..." against the pinned stand
		std::vector<int>			diff_kind;	// 1 added, -1 removed, 0 changed, 2 same
	};

	void				Collect(std::vector<Callout> & out);
	void				Diff(const WED_ModerationEntry & base, Callout & c) const;
	void				ColourFor(const std::string & signature, float rgba[4]);
	const Flag *		FlagFor(const std::string & ioc);
	void				DrawCard(GUI_GraphState * g, Callout & c, bool pinned);
	void				DrawTray(GUI_GraphState * g, Callout & c);

	std::vector<Hit>				mHits;			// from the last frame, for clicks
	int								mPinnedID;		// WED_Persistent id, -1 = none
	int								mTrayID;		// the card whose tray is open, -1 = none
	std::string						mPendingURL;	// a "?" pressed: opened on the mouse-up
	std::map<int, std::vector<float> >	mLastTints;	// the tints the silhouettes were drawn with
	std::map<std::string, int>		mSigIndex;		// signature -> colour slot, stable for the session
	float							mHueSeed;
	std::map<std::string, Flag>		mFlags;
};

#endif /* WED_MODERATIONLAYER_H */
