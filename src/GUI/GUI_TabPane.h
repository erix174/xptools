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

#ifndef GUI_TabPane_H
#define GUI_TabPane_H

#include "GUI_Packer.h"
#include "GUI_Commander.h"
#include "GUI_Listener.h"
#include "GUI_Broadcaster.h"

class GUI_TabControl;
class GUI_ChangeView;

class GUI_TabPane : public GUI_Packer, public GUI_Commander, public GUI_Listener, public GUI_Broadcaster {
public:

							 GUI_TabPane(GUI_Commander * parent);
	virtual					~GUI_TabPane();

			void			SetTextColor(float color[4]);

			void			SetTab(int n);
			int				GetTab(void) const;

			GUI_Commander *	GetPaneOwner(void);

			void			AddPane(GUI_Pane * who, const char * title);

			// Grey out (and refuse to switch to) the tab for a pane already added via
			// AddPane. Additive: panes default to enabled.
			void			SetPaneEnabled(GUI_Pane * who, bool enabled);

			// Index of an added pane, or of the tab with this exact title; -1 if
			// there is no such tab. Both exist so callers can stop hardcoding the
			// position a pane happened to be added at - a literal index is silently
			// wrong the moment a tab is inserted ahead of it, and nothing catches it.
			int				GetTabForPane(GUI_Pane * who) const;
			int				GetTabForTitle(const char * title) const;

	virtual	void			ReceiveMessage(
									GUI_Broadcaster *		inSrc,
									intptr_t				inMsg,
									intptr_t				inParam);

private:

	GUI_TabControl *	mTabs;
	GUI_ChangeView *	mChangeView;

};

#endif /* GUI_TabPane_H */
