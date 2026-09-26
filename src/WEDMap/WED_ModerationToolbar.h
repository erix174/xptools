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

#ifndef WED_MODERATIONTOOLBAR_H
#define WED_MODERATIONTOOLBAR_H

/*
	WED_ModerationToolbar - THEORY OF OPERATION

	The moderation tools, in the empty foot of the map's tool column: same
	cells, same art (moderation_tools.png: normal half, then selected half, as
	map_tools.png), but aligned to the BOTTOM of the pane and growing upward, so
	the two toolbars never meet. Unlike the map tools these are independent
	toggles, not one-of-many, so this is its own pane rather than a GUI_ToolBar.

	  0  Moderation View   - see WED_ModerationViewOn()
	  1  Rotate canvas     - placeholder: the map cannot rotate yet

	Shown only while WED_ModerationEnabled().
*/

#include "GUI_Pane.h"
#include <string>
#include <vector>

class	WED_ModerationToolbar : public GUI_Pane {
public:

					 WED_ModerationToolbar(GUI_Pane * map);

	// Width and height of the art, for placing the pane.
	static	void	CellSize(int & w, int & h);
	static	int		Columns(void) { return 2; }

	virtual	void	Draw(GUI_GraphState * state);
	virtual	int		MouseDown(int x, int y, int button);
	virtual	int		GetHelpTip(int x, int y, int tip_bounds[4], std::string& tip);

private:

	int				ToolAt(int x, int y, int cell[4]);
	bool			IsOn(int tool) const;

	GUI_Pane *		mMap;			// refreshed when a toggle changes what it draws
	bool			mRotate;
};

#endif /* WED_MODERATIONTOOLBAR_H */
