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

#include "WED_LiveryModeration.h"
#include "WED_LiveryRules.h"
#include "WED_Airport.h"
#include "WED_RampPosition.h"
#include "WED_ToolUtils.h"		// WED_GetCurrentAirport, WED_GetSelect
#include "ISelection.h"
#include "IOperation.h"
#include "WED_EnumSystem.h"
#include "PlatformUtils.h"		// ConfirmMessage
#include "GUI_Help.h"			// GUI_LaunchURL
#if !IBM
	#include <unistd.h>				// access()
#endif

#include <algorithm>
#include <chrono>
#include <thread>
#include <cmath>
#include <map>
#include <set>
#include <sstream>
#include <cctype>
#include <cstdio>

using std::string;
using std::wstring;
using std::set;
using std::vector;

bool	WED_ModerationEnabled(void)
{
	return true;
}

static string Upper(string s)
{
	for (size_t i = 0; i < s.size(); ++i) s[i] = (char) toupper((unsigned char) s[i]);
	return s;
}

static string UrlEncode(const string & s)
{
	static const char * hex = "0123456789ABCDEF";
	string out;
	for (size_t i = 0; i < s.size(); ++i)
	{
		unsigned char c = (unsigned char) s[i];
		if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~')	out += (char) c;
		else if (c == ' ')												out += '+';
		else { out += '%'; out += hex[c >> 4]; out += hex[c & 15]; }
	}
	return out;
}

// The question as a person would type it: the operator's name and the airport's
// ICAO code, plus its city when the airport carries one. NOT the airport's name -
// that is whatever the author typed ("Livery Range Test (shadows Beijing Capital -
// see README)"), and every word of it is noise to a search engine.
string	WED_ModerationSearchURL(const string & operator_name, const string & city, const string & icao)
{
	string q = "Does " + operator_name + " fly to " + icao;
	if (!city.empty()) q += " " + city;
	return "https://www.google.com/search?q=" + UrlEncode(q);
}

// The airport's ICAO (icao_code metadata, else its ID) and city (1302 city).
static void AirportIds(WED_Airport * apt, string & icao, string & city)
{
	string ident;
	apt->GetICAO(ident);
	string meta = apt->ContainsMetaDataKey("icao_code") ? apt->GetMetaDataValue("icao_code") : string();
	icao = Upper(!meta.empty() ? meta : ident);
	city = apt->ContainsMetaDataKey("city") ? apt->GetMetaDataValue("city") : string();
}

void	WED_ModerationNotes(WED_RampPosition * ramp, WED_Airport * apt, vector<WED_ModerationNote> & out)
{
	out.clear();
	if (!ramp || !apt) return;

	if (ramp->IsAutoFilled())
		out.push_back({ WED_ModerationNote::note_AutoFilled, "", "Auto-filled; nobody has edited this stand since.", "" });
	if (ramp->GetRampOperationType() == ramp_operation_None)
		out.push_back({ WED_ModerationNote::note_NoStaticAircraft, "", "Operation type None: no static aircraft will park here.", "" });

	WED_LiveryData * d = WED_GetLiveryData(false);
	if (!d) return;						// no index: nothing to check operators against

	string icao, apt_name;
	AirportIds(apt, icao, apt_name);
	vector<string> served;
	if (!d->airports.GetAirlines(icao, served))
	{
		string ident;							// the metadata code is not in the database: try the ID
		apt->GetICAO(ident);
		d->airports.GetAirlines(Upper(ident), served);
	}
	std::set<string> served_set;
	for (size_t i = 0; i < served.size(); ++i) served_set.insert(Upper(served[i]));
	const bool db_knows_airport = !served.empty();

	std::istringstream ss(ramp->GetAirlines());
	string code;
	while (ss >> code)
	{
		string uc = Upper(code);
		WED_AirlineDirectoryEntry e;
		if (!d->directory.Lookup(uc, e))
		{
			out.push_back({ WED_ModerationNote::note_UnknownOperator, uc,
							uc + " is not an operator in the livery index.",
							WED_ModerationSearchURL(uc + " airline", apt_name, icao) });
			continue;
		}
		// Only for airlines: military, government and GA are not in the
		// airport database's served lists, and the pseudo-codes serve nowhere.
		if (e.op_class != WED_AirlineDirectoryEntry::op_Pax && e.op_class != WED_AirlineDirectoryEntry::op_Cargo) continue;
		if (WED_IsGenericAirlinerCode(uc)) continue;
		if (db_knows_airport && !served_set.count(uc))
			out.push_back({ WED_ModerationNote::note_NotServedHere, uc,
							uc + " " + e.name + " is not listed as serving " + icao + ".",
							WED_ModerationSearchURL(e.name, apt_name, icao) });
	}
}

static void CollectRamps(WED_Thing * t, vector<WED_RampPosition *> & out)
{
	if (WED_RampPosition * r = dynamic_cast<WED_RampPosition *>(t)) out.push_back(r);
	for (int i = 0; i < t->CountChildren(); ++i) CollectRamps(t->GetNthChild(i), out);
}

static int FindSelected(ISelection * sel, const vector<WED_RampPosition *> & ramps)
{
	for (size_t i = 0; i < ramps.size(); ++i)
		if (sel->IsSelected(ramps[i])) return (int) i;
	return -1;
}

void	WED_ModerationRamps(WED_Airport * apt, vector<WED_RampPosition *> & out)
{
	out.clear();
	if (apt) CollectRamps(apt, out);
}

bool	WED_ModerationHasIssue(const WED_ModerationEntry & e)
{
	return e.n_to_check > 0 || e.verify == WED_ModerationEntry::verify_NoData;
}

WED_RampPosition *	WED_ModerationStep(IResolver * resolver, int dir, bool issues_only)
{
	WED_Airport * apt = WED_GetCurrentAirport(resolver);
	ISelection * sel = WED_GetSelect(resolver);
	if (!apt || !sel) return NULL;

	vector<WED_RampPosition *> ramps;
	CollectRamps(apt, ramps);
	if (ramps.empty()) return NULL;

	int cur = FindSelected(sel, ramps);
	int n = (int) ramps.size();
	int next = -1;
	const int step = dir >= 0 ? 1 : -1;
	int at = cur < 0 ? (dir >= 0 ? -1 : n) : cur;
	for (int tries = 0; tries < n; ++tries)
	{
		at = ((at + step) % n + n) % n;
		if (!issues_only) { next = at; break; }
		WED_ModerationEntry e;
		WED_ModerationDescribe(ramps[at], apt, e);
		if (WED_ModerationHasIssue(e)) { next = at; break; }
	}
	if (next < 0) return NULL;						// nothing needs checking

	IOperation * op = dynamic_cast<IOperation *>(sel);
	if (op) op->StartOperation("Select Ramp Start");
	sel->Clear();
	sel->Select(ramps[next]);
	if (op) op->CommitOperation();
	return ramps[next];
}

void	WED_ModerationPrompt(WED_RampPosition * ramp, WED_Airport * apt)
{
	vector<WED_ModerationNote> notes;
	WED_ModerationNotes(ramp, apt, notes);

	vector<const WED_ModerationNote *> to_check;
	for (size_t i = 0; i < notes.size(); ++i)
		if (!notes[i].search_url.empty()) to_check.push_back(&notes[i]);
	if (to_check.empty()) return;

	string name;
	ramp->GetName(name);
	string msg = "Ramp start '" + name + "' lists operators that need checking:\n";
	for (size_t i = 0; i < to_check.size(); ++i)
		msg += "\n  - " + to_check[i]->text;
	for (size_t i = 0; i < notes.size(); ++i)
		if (notes[i].search_url.empty()) msg += "\n  (" + notes[i].text + ")";

	// Opening a browser tab per operator: capped, so a stand stuffed with thirty
	// codes does not open thirty tabs.
	const size_t kMaxTabs = 5;
	msg += to_check.size() > kMaxTabs ? "\n\nSearch the web for the first five?" : "\n\nSearch the web for them?";
	if (ConfirmMessage(msg.c_str(), "Search", "Skip"))
		for (size_t i = 0; i < to_check.size() && i < kMaxTabs; ++i)
			WED_ModerationOpenSearch(to_check[i]->search_url);
}

// ---- the callout model ----

string	WED_ModerationWeightsText(const int w[6])
{
	int total = 0;
	for (int k = 0; k < 6; ++k) total += w[k];
	if (total <= 0) return string();
	string out;
	for (int k = 0; k < 6; ++k)
	{
		if (w[k] <= 0) continue;
		char buf[16];
		snprintf(buf, sizeof(buf), "%s%c%d", out.empty() ? "" : " ", 'A' + k, (int) (100.0 * w[k] / total + 0.5));
		out += buf;
	}
	return out;
}

// The Liveries tab's names for the operation types, so the callout and the tab
// read the same. The file keeps the original enum names (see WED_Enums.h).
static const char * OpLabel(int op)
{
	switch (op) {
	case ramp_operation_GeneralAviation:	return "Private";
	case ramp_operation_Airline:			return "Passenger";
	case ramp_operation_Cargo:				return "Cargo";
	case ramp_operation_Military:			return "Military/Gov";
	default:								return "None";
	}
}

void	WED_ModerationDescribe(WED_RampPosition * ramp, WED_Airport * apt, WED_ModerationEntry & out)
{
	out = WED_ModerationEntry();
	out.verify = WED_ModerationEntry::verify_None;
	out.n_to_check = 0;
	for (int k = 0; k < 6; ++k) out.weights[k] = 0;
	if (!ramp) return;

	ramp->GetName(out.ramp_name);
	out.op_type     = ramp->GetRampOperationType();
	out.op_label    = OpLabel(out.op_type);
	out.ramp_type   = ENUM_Desc(ramp->GetType());
	out.auto_filled = ramp->IsAutoFilled();
	out.updated     = ramp->GetClassWeights(out.weights);
	const char * letter = ENUM_Desc(ramp->GetWidth());
	out.size_letter = (letter && *letter) ? letter[0] : '?';

	set<int> eq;
	ramp->GetEquipment(eq);
	for (set<int>::const_iterator e = eq.begin(); e != eq.end(); ++e)
		out.equipment += string(out.equipment.empty() ? "" : ", ") + ENUM_Desc(*e);

	string apt_name;
	if (apt) AirportIds(apt, out.icao, apt_name);

	WED_LiveryData * d = WED_GetLiveryData(false);
	vector<string> served;
	if (d && apt)
	{
		string ident;
		apt->GetICAO(ident);
		if (!d->airports.GetCountry(out.icao, out.country)) d->airports.GetCountry(Upper(ident), out.country);
		if (!d->airports.GetAirlines(out.icao, served))     d->airports.GetAirlines(Upper(ident), served);
	}
	std::set<string> served_set;
	for (size_t i = 0; i < served.size(); ++i) served_set.insert(Upper(served[i]));

	const bool pax_cargo = out.op_type == ramp_operation_Airline || out.op_type == ramp_operation_Cargo;
	if (pax_cargo)
		out.verify = out.auto_filled   ? WED_ModerationEntry::verify_Assumed
				   : !served.empty()   ? WED_ModerationEntry::verify_Database
									   : WED_ModerationEntry::verify_NoData;
	else if (out.op_type == ramp_operation_Military)
		out.verify = WED_ModerationEntry::verify_Country;

	std::istringstream ss(ramp->GetAirlines());
	string code;
	while (ss >> code)
	{
		WED_ModerationCode c;
		c.code    = Upper(code);
		c.verdict = WED_ModerationCode::v_Plain;

		WED_AirlineDirectoryEntry e;
		const bool known = d && d->directory.Lookup(c.code, e);
		if (known) c.country = e.country;
		const string name = known ? e.name : c.code + " airline";
		const bool pseudo = WED_IsGenericAirlinerCode(c.code) || c.code == "XPGA" || c.code == "XPMI";

		switch (out.verify) {
		case WED_ModerationEntry::verify_Assumed:
			c.verdict = WED_ModerationCode::v_Assumed;
			break;
		case WED_ModerationEntry::verify_Database:
			if (pseudo)							c.verdict = WED_ModerationCode::v_Plain;
			else if (known && served_set.count(c.code))	c.verdict = WED_ModerationCode::v_Ok;
			else
			{
				c.verdict    = WED_ModerationCode::v_Check;
				c.search_url = WED_ModerationSearchURL(name, apt_name, out.icao);
			}
			break;
		case WED_ModerationEntry::verify_Country:
			if (pseudo || c.country.empty() || out.country.empty())	c.verdict = WED_ModerationCode::v_Plain;
			else c.verdict = c.country == out.country ? WED_ModerationCode::v_Ok : WED_ModerationCode::v_Foreign;
			break;
		default:
			break;
		}
		if (c.verdict == WED_ModerationCode::v_Check || c.verdict == WED_ModerationCode::v_Foreign) ++out.n_to_check;
		out.codes.push_back(c);
	}

	out.signature = WED_ModerationSignature(ramp);
}

string	WED_ModerationSignature(WED_RampPosition * ramp)
{
	if (!ramp) return string();
	vector<string> codes;
	std::istringstream ss(ramp->GetAirlines());
	string code;
	while (ss >> code) codes.push_back(Upper(code));
	std::sort(codes.begin(), codes.end());
	codes.erase(std::unique(codes.begin(), codes.end()), codes.end());

	string sig = string(OpLabel(ramp->GetRampOperationType())) + "|";
	for (size_t i = 0; i < codes.size(); ++i) sig += codes[i] + " ";
	sig += "|";
	int w[6];
	if (ramp->GetClassWeights(w)) sig += WED_ModerationWeightsText(w);
	else { const char * l = ENUM_Desc(ramp->GetWidth()); sig += (l && *l) ? l : "?"; }
	return sig;
}

// Colour slots are shared by every map layer that asks, and stable for the
// session: the first signature seen gets slot 0, the next slot 1, and so on.
// Golden-ratio steps round the hue circle put each new slot as far as it can be
// from the ones before it, so neighbouring slots never look alike.
static void	HsvToRgb(float h, float s, float v, float out[4])
{
	h = h - floorf(h);
	float r, g, b;
	int i = (int) (h * 6.0f);
	float f = h * 6.0f - (float) i;
	float p = v * (1 - s), q = v * (1 - f * s), t = v * (1 - (1 - f) * s);
	switch (i % 6) {
	case 0: r = v; g = t; b = p; break;
	case 1: r = q; g = v; b = p; break;
	case 2: r = p; g = v; b = t; break;
	case 3: r = p; g = q; b = v; break;
	case 4: r = t; g = p; b = v; break;
	default: r = v; g = p; b = q; break;
	}
	out[0] = r; out[1] = g; out[2] = b; out[3] = 1.0f;
}

void	WED_ModerationColour(const string & signature, float out_rgba[4])
{
	static std::map<string, int> slots;
	static float seed = -1.0f;
	if (seed < 0.0f)
	{
		long long t = std::chrono::steady_clock::now().time_since_epoch().count();
		seed = (float) ((t / 1000) % 1000) / 1000.0f;
	}
	std::map<string, int>::iterator i = slots.find(signature);
	int slot = i != slots.end() ? i->second : (slots[signature] = (int) slots.size());
	HsvToRgb(seed + (float) slot * 0.6180339f, 0.62f, 0.97f, out_rgba);
}

// ---- the search window ----
//
// A small, chromeless browser window beside the cursor, not a tab in whatever
// browser happens to be the default: Edge or Chrome in --app mode. Windows ships
// Edge, so this almost always works there; elsewhere it tries Chrome/Chromium.
// When none is found, the system default browser, as before.
//
// It runs in the moderator's own browser profile, and WED places the window
// itself once it appears. An --app window handed to a browser that is already
// running goes to that process, which ignores the size and position flags (the
// first try opened full-size in the corner). A separate profile would honour
// them, but a profile with no cookies meets Google's "unusual traffic" check
// instead of the results - so: the real profile, and SetWindowPos afterwards.
#if IBM
// The search's query, decoded, for finding its window by title.
static wstring	QueryOf(const string & url)
{
	size_t q = url.find("q=");
	if (q == string::npos) return wstring();
	string enc = url.substr(q + 2, url.find('&', q) == string::npos ? string::npos : url.find('&', q) - q - 2), dec;
	for (size_t i = 0; i < enc.size(); ++i)
	{
		if (enc[i] == '+') dec += ' ';
		else if (enc[i] == '%' && i + 2 < enc.size()) { dec += (char) strtol(enc.substr(i + 1, 2).c_str(), NULL, 16); i += 2; }
		else dec += enc[i];
	}
	return wstring(dec.begin(), dec.end());
}

struct PlaceWindow { wstring query; int x, y, w, h; HWND found; std::set<HWND> before; };

static BOOL CALLBACK	FindSearchWindow(HWND h, LPARAM ref)
{
	PlaceWindow * pw = (PlaceWindow *) ref;
	if (!IsWindowVisible(h)) return TRUE;
	wchar_t title[512];
	if (GetWindowTextW(h, title, 512) <= 0) return TRUE;
	wstring t(title);
	// the page title once loaded, or the address while it loads
	if (t.find(pw->query) == wstring::npos && t.find(L"google.com/search?q=") == wstring::npos) return TRUE;
	if (pw->before.count(h)) return TRUE;			// was there before this search: not ours
	pw->found = h;
	return FALSE;
}

// Waits up to eight seconds for the window, off the main thread, then moves it.
static void	PlaceSearchWindow(PlaceWindow pw)
{
	std::thread([pw]() mutable {
		for (int i = 0; i < 80 && !pw.found; ++i)
		{
			Sleep(100);
			EnumWindows(FindSearchWindow, (LPARAM) &pw);
		}
		if (pw.found) SetWindowPos(pw.found, NULL, pw.x, pw.y, pw.w, pw.h, SWP_NOZORDER);
	}).detach();
}

static bool	FindBrowserExe(wstring & out)
{
	const wchar_t * keys[] = {
		L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\App Paths\\msedge.exe",
		L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\App Paths\\chrome.exe" };
	for (int k = 0; k < 2; ++k)
		for (int hive = 0; hive < 2; ++hive)
		{
			wchar_t buf[MAX_PATH];
			DWORD len = sizeof(buf);
			if (RegGetValueW(hive ? HKEY_CURRENT_USER : HKEY_LOCAL_MACHINE, keys[k], NULL, RRF_RT_REG_SZ, NULL, buf, &len) == ERROR_SUCCESS &&
				GetFileAttributesW(buf) != INVALID_FILE_ATTRIBUTES)
			{
				out = buf;
				return true;
			}
		}
	return false;
}
#endif

void	WED_ModerationOpenSearch(const string & url)
{
	const int w = 620, h = 760;
#if IBM
	wstring exe;
	if (FindBrowserExe(exe))
	{
		POINT p = { 100, 100 };
		GetCursorPos(&p);
		HMONITOR mon = MonitorFromPoint(p, MONITOR_DEFAULTTONEAREST);
		MONITORINFO mi;
		mi.cbSize = sizeof(mi);
		int x = p.x + 24, y = p.y - h / 3;
		if (GetMonitorInfo(mon, &mi))
		{
			if (x + w > mi.rcWork.right)  x = p.x - 24 - w;
			if (x < mi.rcWork.left)       x = mi.rcWork.left;
			if (y + h > mi.rcWork.bottom) y = mi.rcWork.bottom - h;
			if (y < mi.rcWork.top)        y = mi.rcWork.top;
		}
		wchar_t tail[128];
		swprintf(tail, 128, L" --window-size=%d,%d --window-position=%d,%d", w, h, x, y);
		wstring cmd = L"\"" + exe + L"\" --app=\"" + wstring(url.begin(), url.end()) + L"\"" + tail;
		STARTUPINFOW si;
		PROCESS_INFORMATION pi;
		ZeroMemory(&si, sizeof(si));
		si.cb = sizeof(si);
		ZeroMemory(&pi, sizeof(pi));
		vector<wchar_t> line(cmd.begin(), cmd.end());
		line.push_back(0);
		// Any window already showing this search belongs to the moderator, not
		// to this click: note them so only the new one is moved.
		PlaceWindow pw = { QueryOf(url), x, y, w, h, NULL };
		for (;;)
		{
			pw.found = NULL;
			EnumWindows(FindSearchWindow, (LPARAM) &pw);
			if (!pw.found) break;
			pw.before.insert(pw.found);
		}
		if (CreateProcessW(NULL, &line[0], NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi))
		{
			CloseHandle(pi.hThread);
			CloseHandle(pi.hProcess);
			PlaceSearchWindow(pw);
			return;
		}
	}
#elif APL
	const char * apps[] = { "/Applications/Google Chrome.app", "/Applications/Microsoft Edge.app", "/Applications/Chromium.app" };
	for (int i = 0; i < 3; ++i)
		if (access(apps[i], F_OK) == 0)
		{
			char size[64];
			snprintf(size, sizeof(size), "--window-size=%d,%d", w, h);
			string cmd = string("open -na \"") + apps[i] + "\" --args --app='" + url + "' " + size + " &";
			if (system(cmd.c_str()) == 0) return;
		}
#else
	const char * bins[] = { "google-chrome", "chromium", "chromium-browser", "microsoft-edge" };
	for (int i = 0; i < 4; ++i)
	{
		string probe = string("command -v ") + bins[i] + " >/dev/null 2>&1";
		if (system(probe.c_str()) == 0)
		{
			char size[64];
			snprintf(size, sizeof(size), "--window-size=%d,%d", w, h);
			string cmd = string(bins[i]) + " --app='" + url + "' " + size + " >/dev/null 2>&1 &";
			if (system(cmd.c_str()) == 0) return;
		}
	}
#endif
	GUI_LaunchURL(url.c_str());
}
