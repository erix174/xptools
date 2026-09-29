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

#include "WED_TiandituKeyDialog.h"
#include "WED_SlippyMap.h"
#include "WED_Globals.h"
#include "WED_Url.h"
#include "GUI_Application.h"
#include "GUI_Help.h"
#include "GUI_Resources.h"
#include "curl_http.h"
#include "curl/curl.h"

enum { field_key = 1 };

WED_TiandituKeyDialog * WED_TiandituKeyDialog::sOpen = NULL;

void	WED_TiandituKeyDialog::Open(WED_SlippyMap * map, int mode)
{
	if (sOpen)
	{
		sOpen->mMap = map;          // the latest request wins
		sOpen->mMode = mode;
		sOpen->Show();
		return;
	}
	sOpen = new WED_TiandituKeyDialog(map, mode);
}

void	WED_TiandituKeyDialog::MapGone(WED_SlippyMap * map)
{
	if (sOpen && sOpen->mMap == map)
		sOpen->mMap = NULL;
}

WED_TiandituKeyDialog::WED_TiandituKeyDialog(WED_SlippyMap * map, int mode) :
	GUI_FormWindow(gApplication, "Tianditu Imagery", 520, 360),
	mMap(map), mMode(mode), mPhase(phase_form), mCurl(NULL)
{
	ShowForm("", gTiandituKey);
}

WED_TiandituKeyDialog::~WED_TiandituKeyDialog()
{
	Stop();
	delete mCurl;
	if (sOpen == this) sOpen = NULL;
}

void	WED_TiandituKeyDialog::ShowForm(const string& message, const string& key)
{
	mPhase = phase_form;
	Reset("Get a Key", "Verify", "Cancel", true);
	AddLabel("Tianditu, China's national map service, needs your own API key.");
	AddLabel("Use a key of the 'server' type - a browser key is tied to web sites.");
	if (!message.empty())
		AddLabel(WordWrap(message, 64));
	AddField(field_key, "API Key", key);
}

void	WED_TiandituKeyDialog::AuxiliaryAction()
{
	GUI_LaunchURL(WED_URL_TIANDITU_CONSOLE);
}

void	WED_TiandituKeyDialog::Cancel()
{
	AsyncDestroy();
}

void	WED_TiandituKeyDialog::Submit()
{
	if (mPhase == phase_done)
	{
		AsyncDestroy();
		return;
	}
	if (mPhase != phase_form)
		return;

	string key = GetField(field_key);
	key.erase(0, key.find_first_not_of(" \t"));
	key.erase(key.find_last_not_of(" \t") + 1);

	bool well_formed = key.size() == 32;
	for (char c : key)
		if (!isxdigit((unsigned char) c))
			well_formed = false;
	if (!well_formed)
	{
		ShowForm("A Tianditu key is 32 characters, 0-9 and a-f. Please check it.", key);
		return;
	}

	// One real imagery tile over Beijing. Only an image proves the key works: a wrong key, a browser key or
	// Tianditu's firewall answer with an error page instead.
	mKey = key;
	mResponse.clear();
	mCurl = new curl_http_get_file(string(WED_URL_TIANDITU_TILES) + "?T=img_w&x=843&y=388&l=10&tk=" + key, &mResponse);
	mPhase = phase_checking;
	Reset("", "", "Cancel", false);
	AddLabel("Checking the key with Tianditu ...");
	Start(0.2);
}

void	WED_TiandituKeyDialog::TimerFired()
{
	if (mPhase != phase_checking || !mCurl || !mCurl->is_done())
		return;
	Stop();

	bool is_image = mResponse.size() > 4 &&
		(((unsigned char) mResponse[0] == 0xFF && (unsigned char) mResponse[1] == 0xD8) ||      // JPEG
		 ((unsigned char) mResponse[0] == 0x89 && mResponse[1] == 'P' && mResponse[2] == 'N'));  // PNG

	string problem;
	if (mCurl->is_ok() && is_image)
	{
		gTiandituKey = mKey;
		gTiandituVerified = 1;
	}
	else if (mCurl->is_ok())
		problem = "Tianditu answered, but not with imagery - the key was not accepted.";
	else
	{
		int err = mCurl->get_error();          // a curl error, or above CURL_LAST the HTTP status (as WED_GatewayExport)
		if (err <= CURL_LAST)
			problem = string("Could not reach Tianditu: ") + curl_easy_strerror((CURLcode) err) + ".";
		else if (err == 418 || err == 403)
			problem = "Tianditu refused the request (HTTP " + to_string(err) + "). Either the key is not a server "
			          "key, or Tianditu does not serve this network - it blocks many connections from outside China.";
		else if (err == 401)
			problem = "Tianditu does not know this key (HTTP 401).";
		else
			problem = "Tianditu answered with HTTP error " + to_string(err) + ".";
	}
	delete mCurl;
	mCurl = NULL;

	if (!problem.empty())
	{
		ShowForm(problem, mKey);
		return;
	}

	if (mMap)
		mMap->SetMode(mMode);
	mPhase = phase_done;
	Reset("", "OK", "", true);
	AddLabel(WordWrap("The key works. Tianditu imagery is now available in View > Slippy Map.", 64));
}
