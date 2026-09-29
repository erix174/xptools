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

#ifndef WED_TiandituKeyDialog_h
#define WED_TiandituKeyDialog_h

/*
	WED_TiandituKeyDialog - THEORY OF OPERATION

	Tianditu (tianditu.gov.cn) only serves tiles to users with their own API key. Unlike the other slippy maps, it
	has to be activated once: the user enters a key, WED fetches one real tile with it, and only a tile that is an
	image counts - a wrong key, a browser-type key or Tianditu's firewall all answer with something else. A verified
	key is kept in the preferences (gTiandituKey, gTiandituVerified); changing it in Preferences needs a new check.

	The dialog outlives nothing: at most one is open, and the slippy map it would switch on tells it when it goes
	away (MapGone), so a document closed meanwhile is never touched.
*/

#include "GUI_FormWindow.h"
#include "GUI_Timer.h"

class	WED_SlippyMap;
class	curl_http_get_file;

class	WED_TiandituKeyDialog : public GUI_FormWindow, public GUI_Timer {
public:

	// Shows the dialog; on success, 'map' is switched to 'mode'.
	static	void	Open(WED_SlippyMap * map, int mode);
	static	void	MapGone(WED_SlippyMap * map);

	virtual	void	Submit();
	virtual	void	Cancel();
	virtual	void	AuxiliaryAction();
	virtual	void	TimerFired();

private:
					 WED_TiandituKeyDialog(WED_SlippyMap * map, int mode);
	virtual			~WED_TiandituKeyDialog();

			void	ShowForm(const string& message, const string& key);

	enum { phase_form, phase_checking, phase_done };

	WED_SlippyMap *			mMap;
	int						mMode;
	int						mPhase;
	string					mKey;
	curl_http_get_file *	mCurl;
	vector<char>			mResponse;

	static	WED_TiandituKeyDialog *	sOpen;
};

#endif /* WED_TiandituKeyDialog_h */
