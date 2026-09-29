/*
 * Copyright (c) 2015, Laminar Research.
 *
 * Created by Ben Supnik on 12/18/15.
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
 */

#include "WED_SlippyMap.h"
#include "WED_TiandituKeyDialog.h"

#include <sstream>

#include "WED_MapZoomerNew.h"
#include "WED_Url.h"
#include "WED_Globals.h"
#include "WED_DrawUtils.h"
#include "MathUtils.h"
#include "BitmapUtils.h"
#include "PlatformUtils.h"
#include "TexUtils.h"
#include "GUI_GraphState.h"
#include "GUI_Fonts.h"
#include "curl_http.h"

#include "WED_FileCache.h"
#define _USE_MATH_DEFINES
#include <math.h>
#include <stdlib.h>
#include <chrono>

#if APL
	#include <OpenGL/gl.h>
#else
	#include <GL/gl.h>
#endif

#if DEV
#include <iostream>
#endif

#define SHOW_DEBUG_INFO 0

#define MIN_ZOOM  12        // stop displaying slippys at all below this level
#define MAX_ZOOM  17        // for custom mode maps (predefined maps have their own limits below)

#define TILE_FACTOR 0.8     // save tiles by zooming in a bit later than at 1:1 pixel ratio.
							// Since zoom goes by 1.2x steps - it matters little w.r.t "sharpness"
							// but saves on average 34% of all tile loads

struct slippy_source_t {
	const char * name;         // menu item text, regional maps only
	const char * url;          // tile url template, see SetMode()
	int          max_zoom;
	const char * attribution;
	const char * countries;    // IOC codes of the countries covered, regional maps only
	bool         needs_key;    // the user's own Tianditu key is appended to the url, once verified
};

// Map modes: 0 = off, 1 = OSM, 2 = ESRI, 3 = custom url, 4... = regional maps in the order below.
// The mode is saved in the document prefs, so only ever append to this list.
static const slippy_source_t slippy_sources[] = {
{ NULL, WED_URL_OSM_TILES  "${z}/${x}/${y}.png", 16,  // OSM tiles below this zoom are not cached, but on-demand generated. Openstreetmap foundation asks to limit their use.
  "© OpenStreetMap contributors, ODbL", NULL },
// ToDo: use shorter specific ESRI attribution by downloading https://static.arcgis.com/attribution/World_Imagery
//       and decode it per https://github.com/Esri/esri-leaflet  (which is java code)
{ NULL, WED_URL_ESRI_TILES "${z}/${y}/${x}.jpg", 18,  // ESRI maps are available down to ZL17 in general, but since 2021 below 60 deg also in ZL18
  "Powered by Esri. Source: Esri, Vantor, Earthstar Geographics, and the GIS User Community", NULL },

// Official orthophotos under open licenses. Coverage checked 2026-09: outside it the servers return 404, blank tiles or,
// for swisstopo, heavily upscaled imagery. Only countries with high resolution coverage get listed and flagged.
// Attributions follow each provider's terms as of 2026-09. The map font has no CJK glyphs, hence GSI's English credit.
{ "&Austria (basemap.at)",
  "https://mapsneu.wien.gv.at/basemap/bmaporthofoto30cm/normal/google3857/${z}/${y}/${x}.jpeg", 19,
  "Data source: basemap.at, CC BY 4.0", "AUT" },
{ "Es&tonia (Maa- ja Ruumiamet)",
  "https://tiles.maaamet.ee/tm/tms/1.0.0/foto@GMC/${z}/${x}/${-y}.jpg", 18,
  "Ortofoto: Maa- ja Ruumiamet, open data licence geoportaal.maaruum.ee/opendata-licence", "EST" },
{ "&France, Monaco (IGN)",                           // incl. overseas departments, St Pierre, New Caledonia, Wallis - but not French Polynesia
  "https://data.geopf.fr/wmts?SERVICE=WMTS&REQUEST=GetTile&VERSION=1.0.0&LAYER=ORTHOIMAGERY.ORTHOPHOTOS&STYLE=normal"
  "&TILEMATRIXSET=PM&FORMAT=image/jpeg&TILEMATRIX=${z}&TILEROW=${y}&TILECOL=${x}", 19,
  "© IGN - BD ORTHO, Géoplateforme, Licence Ouverte Etalab 2.0", "FRA MON" },
{ "&Japan (GSI)",
  "https://cyberjapandata.gsi.go.jp/xyz/seamlessphoto/${z}/${x}/${y}.jpg", 18,
  "Source: GSI Tiles (Chiriin Tile), Geospatial Information Authority of Japan", "JPN" },
{ "Net&herlands (PDOK)",                             // European part only
  "https://service.pdok.nl/hwh/luchtfotorgb/wmts/v1_0/Actueel_orthoHR/EPSG:3857/${z}/${x}/${y}.jpeg", 19,
  "Luchtfoto: Beeldmateriaal.nl / PDOK, CC BY 4.0", "NED" },
{ "S&pain, Gibraltar (PNOA)",                        // Gibraltar has no IOC flag
  "https://tms-pnoa-ma.idee.es/1.0.0/pnoa-ma/${z}/${x}/${-y}.jpeg", 19,
  "PNOA, CC BY 4.0 scne.es", "ESP" },
{ "&Switzerland, Liechtenstein (swisstopo)",
  "https://wmts.geo.admin.ch/1.0.0/ch.swisstopo.swissimage/default/current/3857/${z}/${x}/${y}.jpeg", 20,
  "© swisstopo", "SUI LIE" },
// Tianditu: China's national map service, CGCS2000 - no GCJ-02 offset. Needs the user's own server-type key.
{ "Ch&ina (Tianditu)",
  WED_URL_TIANDITU_TILES "?T=img_w&x=${x}&y=${y}&l=${z}&tk=", 18,
  "© Tianditu - National Platform for Common GeoSpatial Information Services", "CHN", true },
};

#define PREDEFINED_MAPS  ((int) (sizeof(slippy_sources) / sizeof(slippy_sources[0])))
#define FIRST_REGIONAL   2      // index of the first regional map in slippy_sources
#define MODE_CUSTOM      3

// index into slippy_sources, -1 for the custom map and invalid modes
static int predefined_idx(int mode)
{
	if(mode == 1 || mode == 2)                              return mode - 1;
	if(mode > MODE_CUSTOM && mode - 2 < PREDEFINED_MAPS)    return mode - 2;
	return -1;
}

// Debug aids, both off unless the environment variable is set:
// WED_SLIPPY_DEBUG     logs every mode change, tile request and result
// WED_SLIPPY_SELFTEST  after startup, shows each map at an airport inside and one outside its coverage
//                      and logs how many tiles loaded. Open a package first, e.g. with --package.
static bool slippy_self_test(void)
{
	static bool on = getenv("WED_SLIPPY_SELFTEST") != NULL;
	return on;
}

static bool slippy_debug(void)
{
	static bool on = getenv("WED_SLIPPY_DEBUG") != NULL || slippy_self_test();
	return on;
}

static double seconds_now(void)
{
	return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

struct slippy_test_t {
	int          mode;
	const char * icao;
	double       lat, lon;
	bool         inside;       // inside the map's coverage
};

static const slippy_test_t slippy_tests[] = {
	{  1, "LOWW",  48.1103,   16.5697, true  },
	{  2, "LOWW",  48.1103,   16.5697, true  },
	{  4, "LOWW",  48.1103,   16.5697, true  },    // Austria
	{  4, "LZIB",  48.1702,   17.2127, false },
	{  5, "EETN",  59.4133,   24.8328, true  },    // Estonia
	{  5, "EVRA",  56.9236,   23.9711, false },
	{  6, "LFPG",  49.0097,    2.5479, true  },    // France, Monaco
	{  6, "FMEE", -20.8871,   55.5103, true  },
	{  6, "NTAA", -17.5537, -149.6070, false },
	{  7, "RJTT",  35.5494,  139.7798, true  },    // Japan
	{  7, "RKPK",  35.1795,  128.9382, false },
	{  8, "EHAM",  52.3086,    4.7639, true  },    // Netherlands
	{  8, "TNCB",  12.1310,  -68.2685, false },
	{  9, "LEMD",  40.4719,   -3.5626, true  },    // Spain, Gibraltar
	{  9, "LXGB",  36.1512,   -5.3497, true  },
	{  9, "LPPT",  38.7813,   -9.1359, false },
	{ 10, "LSZH",  47.4647,    8.5492, true  },    // Switzerland, Liechtenstein
	{ 10, "LSXB",  47.0664,    9.5372, true  },
	// Eric's repro 2026-09-28: Japan, then ESRI, then back to Japan loaded nothing. Each step at a new place, so
	// every step has to fetch new tiles instead of showing cached ones.
	{  7, "RJTT",  35.5494,  139.7798, true  },
	{  2, "RJAA",  35.7647,  140.3864, true  },
	{  7, "RJAA",  35.7647,  140.3864, true  },
	{  2, "RJBB",  34.4347,  135.2440, true  },
	{  7, "RJBB",  34.4347,  135.2440, true  },
	{  1, "RJOO",  34.7855,  135.4382, true  },
	{  7, "RJOO",  34.7855,  135.4382, true  },
};
#define SELF_TESTS ((int) (sizeof(slippy_tests) / sizeof(slippy_tests[0])))
#define SELF_TEST_TIMEOUT 20.0
#define SELF_TEST_ENOUGH  6     // tiles resolved one way or the other before moving on

int WED_SlippyMap::CountRegionalMaps(void)
{
	return PREDEFINED_MAPS - FIRST_REGIONAL;
}

int WED_SlippyMap::RegionalMapMode(int n)
{
	return MODE_CUSTOM + 1 + n;
}

const char * WED_SlippyMap::RegionalMapName(int n)
{
	return slippy_sources[FIRST_REGIONAL + n].name;
}

bool WED_SlippyMap::RegionalMapNeedsKey(int n)
{
	return slippy_sources[FIRST_REGIONAL + n].needs_key;
}

// The url with any API key blanked out, for the log
static string redact_key(const string& url)
{
	string r(url);
	size_t p = r.find("tk=");
	if(p != string::npos)
	{
		size_t e = r.find('&', p);
		r.replace(p + 3, (e == string::npos ? r.size() : e) - p - 3, "<key>");
	}
	return r;
}

const char * WED_SlippyMap::RegionalMapCountries(int n)
{
	return slippy_sources[FIRST_REGIONAL + n].countries;
}

struct attrib_t {
	char zoomMax;
	char zoomMin;
	char score;
	float bounds[4];
};

vector<pair<string, vector<attrib_t> > > slippyAttrib;

static string ESRI_attributions(float lon, float lat, int z)
{
	// get_JSON_string (once per session)
	// get all relevant data (zl 17-13) Vector<attribution,Vector<{zl_min, zl_max, score, BBox2}> >

	string attrib;
	int score = 0;
	for(auto& s : slippyAttrib)
	{
		for(auto& a : s.second)
		{
			if(z >= a.zoomMin && z <= a.zoomMax)
			{
				if(lon >= a.bounds[0] && lon <= a.bounds[2] &&
				   lat >= a.bounds[1] && lat <= a.bounds[3])
					{
						score += a.score;
						if(!attrib.empty()) attrib += ",";
						attrib += s.first;
						if(score >= 100) return attrib;
					}
			}
		}
	}
	return attrib.empty() ? slippy_sources[1].attribution : attrib;
}


static inline int long2tilex(double lon, int z)
{
	return (int)(floor((lon + 180.0) / 360.0 * pow(2.0, z)));
}

static inline int lat2tiley(double lat, int z)
{
	return (int)(floor((1.0 - log( tan(lat * M_PI/180.0) + 1.0 / cos(lat * M_PI/180.0)) / M_PI) / 2.0 * pow(2.0, z)));
}

static inline double tilex2long(int x, int z)
{
	return x / pow(2.0, z) * 360.0 - 180;
}

static inline double tiley2lat(int y, int z)
{
	double n = M_PI - 2.0 * M_PI * y / pow(2.0, z);
	return 180.0 / M_PI * atan(0.5 * (exp(n) - exp(-n)));
}

int WED_SlippyMap::get_zl_for_map(double in_ppm, double lattitude)
{
	double mpp = 1.0 / in_ppm;
	double zl_mpp = 156543.03 * TILE_FACTOR / 1.4 * cos(lattitude * 3.14/180.0);
	int zl = 0;
	int idx = predefined_idx(mMapMode);
	int max_zl = idx >= 0 ? slippy_sources[idx].max_zoom : MAX_ZOOM;
	if (lattitude > 60.0 || lattitude < -60.0) max_zl--;
	if (lattitude > 75.0 || lattitude < -75.0) max_zl--;

	while(zl < max_zl && zl_mpp > mpp)
	{
		zl_mpp *= 0.5;
		++zl;
	}
	return zl;
}

static void get_ll_box_for_tile(int z, int x, int y, double bounds[4])
{
	bounds[0] = tilex2long(x,z  );
	bounds[2] = tilex2long(x+1,z);

	bounds[1] = tiley2lat (y  ,z);
	bounds[3] = tiley2lat (y+1,z);
}

// This returns an INCLUSIVE range.
static void get_tile_range_for_box(const double bounds[4], int z, int tiles[4])
{
	int max_tile = (1 << z) - 1;
	tiles[0] = intlim(long2tilex(bounds[0], z), 0, max_tile);
	tiles[2] = intlim(long2tilex(bounds[2], z), 0, max_tile);

	tiles[1] = intlim(lat2tiley(bounds[1], z), 0, max_tile);
	tiles[3] = intlim(lat2tiley(bounds[3], z), 0, max_tile);
}


WED_SlippyMap::WED_SlippyMap(GUI_Pane * h, WED_MapZoomerNew * zoomer, IResolver * resolver)
	: WED_MapLayer(h, zoomer, resolver),
	m_cache_request(NULL),
	mMapMode(0),
	mSelfTest(slippy_self_test() ? 0 : -1),
	mSelfTestStarted(false),
	mSelfTestStart(0),
	mWant(0), mGot(0), mBad(0), mZoom(0),
	mRequestStart(0), mStallReported(false), mDrawnMode(0)
{
	if(mSelfTest >= 0)
	{
		LOG_MSG("I/Sli SELFTEST starting, %d tests\n", SELF_TESTS);
		Start(0.25);
	}
}

WED_SlippyMap::~WED_SlippyMap()
{
	WED_TiandituKeyDialog::MapGone(this);
	delete m_cache_request;
	m_cache_request = NULL;
}

void	WED_SlippyMap::DrawVisualization(bool inCurrent, GUI_GraphState * g)
{
	if (mMapMode ==0) { mDrawnMode = 0; return; }
	if (slippy_debug() && mMapMode != mDrawnMode)
	{
		LOG_MSG("I/Sli drawing mode %d\n", mMapMode);
		LOG_FLUSH();
	}
	mDrawnMode = mMapMode;
	finish_loading_tile();

	double map_bounds[4];

	WED_MapZoomerNew * zoomer = GetZoomer();
	zoomer->GetMapVisibleBounds(map_bounds[0], map_bounds[1], map_bounds[2], map_bounds[3]);
	map_bounds[0] = doblim(map_bounds[0],-180.0,180.0);
	map_bounds[2] = doblim(map_bounds[2],-180.0,180.0);
	map_bounds[1] = doblim(map_bounds[1],-85.0,85.0);
	map_bounds[3] = doblim(map_bounds[3],-85.0,85.0);

	double ppm = zoomer->GetPPM();
	int z_max = get_zl_for_map(ppm, map_bounds[1]);
	int min_zoom = flt_abs(map_bounds[1]) > 60.0 ? MIN_ZOOM-1 : MIN_ZOOM; // get those ant/artic designers a bit more visibility
	if(z_max < min_zoom) return;

	int want = 0, got = 0, bad = 0;
	for(int z = max(min_zoom,z_max-1); z <= z_max; ++z)      // Display only the next lower zoom level
	{                                                        // avoids having to load up to 4x14 extra tiles at ZL16
		int tiles[4];

		get_tile_range_for_box(map_bounds,z,tiles);

		for(int y = tiles[3]; y <= tiles[1]; ++y)
		for (int x = tiles[0]; x <= tiles[2]; ++x)
		{
			++want;
			double tbounds[4];
			Point2 pbounds[4];
			get_ll_box_for_tile(z, x, y, tbounds);

			pbounds[0] = zoomer->LLToPixel(Point2(tbounds[0], tbounds[3]));
			pbounds[1] = zoomer->LLToPixel(Point2(tbounds[0], tbounds[1]));
			pbounds[2] = zoomer->LLToPixel(Point2(tbounds[2], tbounds[3]));
			pbounds[3] = zoomer->LLToPixel(Point2(tbounds[2], tbounds[1]));

#if DEV && SHOW_DEBUG_INFO
			//Draw border around tile
			g->SetState(0, 0, 0, 0, 0, 0, 0);
			GLfloat black[4] = { 0, 0, 0, 1 };
			glColor4fv(black);

			GLfloat prev_line_width = 0;
			glGetFloatv(GL_LINE_WIDTH, &prev_line_width);
			glLineWidth(3.0f);
			glBegin(GL_LINE_LOOP);
			glVertex2p(pbounds, 4);
			glEnd();
			glLineWidth(prev_line_width);
			{
				char msg[100];
				snprintf(msg, 100, "%d/%d/%d", z, x, y);
				GUI_FontDraw(g, font_UI_Basic, black, (pbounds[0] + pbounds[2]) / 2, (pbounds[1] + pbounds[3]) / 2, msg);
			}
#endif
			int yTransformed;
			switch(y_coordinate_math)
			{
				case yYahoo: yTransformed = (1 << (z-1)) - 1 - y; break;
				case yOSGeo: yTransformed = (1 << z) - 1 - y; break;
				default: yTransformed = y;
			}
#if IBM
			char url[1024]; _sprintf_p(url, sizeof(url), url_printf_fmt.c_str(), x, yTransformed, z);
			char dir[1024]; _sprintf_p(dir, sizeof(dir), dir_printf_fmt.c_str(), x, yTransformed, z);
#else
			char url[1024]; snprintf(url, sizeof(url), url_printf_fmt.c_str(), x, yTransformed, z);
			char dir[1024]; snprintf(dir, sizeof(dir), dir_printf_fmt.c_str(), x, yTransformed, z);  // make sure ALL args are referenced in the format string
#endif
			string folder_prefix(dir); folder_prefix.erase(folder_prefix.find_last_of(DIR_STR));

			//The potential place the tile could appear on disk, were it to be downloaded or have been downloaded
			string potential_path = gFileCache.url_to_cache_path(WED_file_cache_request(cache_domain_osm_tile, folder_prefix , url));

			if (m_cache.count(potential_path))
			{
				++got;

				int id = m_cache[potential_path];
				if(id != 0)
				{
					g->SetState(0, 1, 0, 0, 0, 0, 0);
					glColor4f(1,1,1,1);
					g->BindTex(id, 0);
					glBegin(GL_TRIANGLE_STRIP);
						glTexCoord2f(0, 0); glVertex2(pbounds[0]);
						glTexCoord2f(0, 1); glVertex2(pbounds[1]);
						glTexCoord2f(1, 0); glVertex2(pbounds[2]);
						glTexCoord2f(1, 1); glVertex2(pbounds[3]);
					glEnd();

#if DEV && SHOW_DEBUG_INFO
						stringstream ss;
						ss << potential_path.substr(28) << " Id: " << id;
						GUI_FontDraw(g, font_UI_Basic, black, pbounds[1].x() + 5, pbounds[1].y() - 15, ss.str().c_str()+20);
#endif
				}
				else
				{
					++bad;
				}
			}
			else if(m_cache_request == NULL)
			{
				m_cache_request = new WED_file_cache_request(cache_domain_osm_tile, folder_prefix, url);
				mRequestStart = seconds_now();
				mStallReported = false;
				if(slippy_debug())
					LOG_MSG("I/Sli get %s\n         -> %s\n", redact_key(url).c_str(), potential_path.c_str());
			}
		}
	}

	mWant = want; mGot = got; mBad = bad; mZoom = z_max;

	if (m_cache_request)
	{
		this->Start(0.05);
	}
	else if (mSelfTest >= 0)
	{
		this->Start(0.25);
	}
	else
	{
		this->Stop();
	}

	char str[30];
	snprintf(str, sizeof(str), "%d/%d Tiles %d errs" , got, want, bad);

	int bnds[4];
	GetHost()->GetBounds(bnds);
	GLfloat white[4] = { 1, 1, 1, 1 };

	// The status line and the attribution belong to the screen, not the map:
	// while the view is turned (WED_Map), undo the turn for them.
	const double rot = GetZoomer()->GetViewRotation();
	if (rot != 0)
	{
		const double cx = (bnds[0] + bnds[2]) * 0.5, cy = (bnds[1] + bnds[3]) * 0.5;
		glMatrixMode(GL_MODELVIEW);
		glPushMatrix();
		glTranslated(cx, cy, 0);
		glRotated(-rot, 0, 0, 1);
		glTranslated(-cx, -cy, 0);
	}
	GUI_FontDraw(g, font_UI_Basic, white, bnds[0] + 10, bnds[1] + 40, str);

	int idx = predefined_idx(mMapMode);
	if(idx >= 0)
	{
		const char * attrib = slippy_sources[idx].attribution;
		int txtWidth = GUI_MeasureRange(font_UI_Small, attrib, attrib + strlen(attrib));

		g->SetState(0, 0, 0, 0, 1, 0, 0);
		glColor4f(0,0,0,0.65);
		glBegin(GL_QUADS);
			glVertex2f(bnds[2] - 10 - txtWidth, bnds[1] + 12 );
			glVertex2f(bnds[2],                 bnds[1] + 12 );
			glVertex2f(bnds[2],                 bnds[1]      );
			glVertex2f(bnds[2] - 10 - txtWidth, bnds[1]      );
		glEnd();
		GUI_FontDraw(g, font_UI_Small, white, bnds[2] - 5, bnds[1] + 2, attrib, align_Right);
	}

	// A regional map outside its country is just empty - say so, or it looks broken.
	// Failed tiles come back slowly, so don't wait for all of them.
	if(idx >= FIRST_REGIONAL && got >= min(want, 4) && got > 0 && bad == got)
	{
		string name(slippy_sources[idx].name);                  // "S&pain, Gibraltar (PNOA)"
		name.erase(remove(name.begin(), name.end(), '&'), name.end());
		size_t paren = name.find(" (");
		string msg = "No imagery here - this map covers " + name.substr(0, paren) + " only";

		int txtWidth = GUI_MeasureRange(font_UI_Basic, msg.c_str(), msg.c_str() + msg.size());
		float cx = (bnds[0] + bnds[2]) * 0.5f, cy = (bnds[1] + bnds[3]) * 0.5f;
		g->SetState(0, 0, 0, 0, 1, 0, 0);
		glColor4f(0,0,0,0.65);
		glBegin(GL_QUADS);
			glVertex2f(cx - txtWidth / 2 - 10, cy + 20);
			glVertex2f(cx + txtWidth / 2 + 10, cy + 20);
			glVertex2f(cx + txtWidth / 2 + 10, cy - 10);
			glVertex2f(cx - txtWidth / 2 - 10, cy - 10);
		glEnd();
		GUI_FontDraw(g, font_UI_Basic, white, cx, cy, msg.c_str(), align_Center);
	}
	if (rot != 0)
	{
		glMatrixMode(GL_MODELVIEW);
		glPopMatrix();
	}
}

void	WED_SlippyMap::GetCaps(bool& draw_ent_v, bool& draw_ent_s, bool& cares_about_sel, bool& wants_clicks)
{
	draw_ent_v = draw_ent_s = cares_about_sel = wants_clicks = 0;
}

static bool is_ESRI_blank(const string& path, const ImageInfo& info)
{
	bool same_grey = false;           // ESRI sends a mostly solid grey image with embedded error text if image isn't available

	if(path.find("arcgisonline.com") != string::npos)
		if (info.channels == 3 && info.width > 6 && info.height > 6)
		{
			int line_stride = info.channels * (info.width + info.pad);

			auto pixel = info.data + 2 * line_stride + 6;  // 3rd row, 3rd pixel
			auto color = *pixel;
			same_grey = true;

			for (int x = info.width - 4; x > 0; x--)
				if (color != *(pixel++) || color != *(pixel++) || color != *(pixel++))
				{
					same_grey = false;
					break;
				}

			pixel = info.data + (info.height - 3) * line_stride + 6; // 3rd last row, 3rd pixel
			for (int x = info.width - 4; x > 0; x--)
				if (color != *(pixel++) || color != *(pixel++) || color != *(pixel++))
				{
					same_grey = false;
					break;
				}
		}

	return same_grey;
}

// Several regional servers answer outside their coverage with a single colored tile instead of a 404.
// Drawing those would cover the map with white or black squares, so treat them as missing.
static bool is_uniform(const ImageInfo& info)
{
	int line_stride = info.channels * (info.width + info.pad);
	for (int y = 0; y < info.height; ++y)
	{
		const unsigned char * row = info.data + y * line_stride;
		for (int x = 0; x < info.width * info.channels; ++x)
			if (row[x] != info.data[x % info.channels])
				return false;
	}
	return true;
}

void	WED_SlippyMap::finish_loading_tile()
{
	if (m_cache_request != NULL)
	{
		WED_file_cache_response res = gFileCache.request_file(*m_cache_request);
		if (res.out_status == cache_status_available)
		{
			struct ImageInfo info;
			int r = CreateBitmapFromPNG(res.out_path.c_str(), &info, false, 0);
			if(r != 0)
				r = CreateBitmapFromJPEG(res.out_path.c_str(), &info);
			if (r == 0)
			{
				if (info.channels == 3)                                                        // apply to color changes
					for (int x = 0; x < info.height * (info.width + info.pad) * info.channels; x += info.channels)
					{
						double BRIGHTNESS = -20;
						double SATURATION = 1.0;
						if (mMapMode == 1) { BRIGHTNESS = -140.0; SATURATION = 0.4; }

						int val = 0.3 * info.data[x] + 0.6 * info.data[x + 1] + 0.1 * info.data[x + 2];  // deliberately not HSV weighing - want red's brighter
						for (int c = 0; c < info.channels; ++c)
							info.data[x + c] = intlim((1.0 - SATURATION) * val + SATURATION * info.data[x + c] + BRIGHTNESS, 0, 255);
					}
				if (is_ESRI_blank(res.out_path, info) || is_uniform(info))
				{
					if(slippy_debug())
						LOG_MSG("I/Sli blank tile %s\n", res.out_path.c_str());
					m_cache[res.out_path] = 0;
				}
				else
				{
					GLuint tex_id;
					glGenTextures(1, &tex_id);
					if (LoadTextureFromImage(info, tex_id, tex_Linear, NULL, NULL, NULL, NULL))
					{
						m_cache[res.out_path] = tex_id;
						if(slippy_debug())
							LOG_MSG("I/Sli ok %dx%d %s\n", info.width, info.height, res.out_path.c_str());
					}
					else
					{
						LOG_MSG("E/Sli bad png or JPG in %s\n", res.out_path.c_str());
						m_cache[res.out_path] = 0;
					}
				}
			}
			else
			{
				LOG_MSG("E/Sli bad png or JPG in %s\n", res.out_path.c_str());
				m_cache[res.out_path] = 0;
			}
			DestroyBitmap(&info);
			delete m_cache_request;
			m_cache_request = NULL;
		}
		else if (res.out_status == cache_status_error || res.out_status == cache_status_cooling)
		{
			// res.out_path is empty on errors. Marking that instead of the tile left the tile unmarked, so it was requested
			// again on the next draw - and with one request at a time, one missing tile stopped all others from loading.
			// A tile that failed recently is 'cooling' for a minute in the file cache - waiting that out here stalled
			// every other tile, of every map, for that minute. So it counts as failed as well.
			string tile_path = gFileCache.url_to_cache_path(*m_cache_request);
			LOG_MSG("E/Sli %s: %s\n", redact_key(m_cache_request->in_url).c_str(), res.out_error_human.c_str());

			m_cache[tile_path] = 0;

			delete m_cache_request;
			m_cache_request = NULL;
		}
		else if (slippy_debug() && !mStallReported && seconds_now() - mRequestStart > 10.0)
		{
			LOG_MSG("W/Sli request pending for %.0fs, cache status %d, progress %.0f: %s\n", seconds_now() - mRequestStart,
				(int) res.out_status, res.out_download_progress, redact_key(m_cache_request->in_url).c_str());
			mStallReported = true;
		}
	}
	if (slippy_debug())
		LOG_FLUSH();
}

void	WED_SlippyMap::self_test_step()
{
	const slippy_test_t& t = slippy_tests[mSelfTest];
	if(!mSelfTestStarted)
	{
		SetMode(t.mode);
		double dlon = 0.004, dlat = 0.004 * cos(t.lat * M_PI / 180.0);    // about ZL17 in a full screen map
		GetZoomer()->ZoomShowArea(t.lon - dlon, t.lat - dlat, t.lon + dlon, t.lat + dlat);
		mWant = mGot = mBad = mZoom = 0;
		mSelfTestStart = seconds_now();
		mSelfTestStarted = true;
		return;
	}

	int textures = mGot - mBad;
	double elapsed = seconds_now() - mSelfTestStart;
	bool done = (mWant > 0 && mGot >= mWant) || textures >= SELF_TEST_ENOUGH || mBad >= SELF_TEST_ENOUGH || elapsed > SELF_TEST_TIMEOUT;
	if(!done) return;

	const char * verdict = t.inside ? (textures > 0 ? "PASS" : "FAIL") : (textures == 0 ? "PASS" : "CHECK - outside coverage, but tiles came back");
	int idx = predefined_idx(t.mode);
	LOG_MSG("I/Sli SELFTEST %-4s mode %2d %-40s %s z%d want %d got %d textures %d errors %d in %.1fs\n", verdict, t.mode,
		idx >= 2 ? slippy_sources[idx].name : (t.mode == 1 ? "OpenStreetMap" : "ESRI"), t.icao, mZoom, mWant, mGot, textures, mBad, elapsed);

	mSelfTestStarted = false;
	if(++mSelfTest >= SELF_TESTS)
	{
		LOG_MSG("I/Sli SELFTEST done\n");
		mSelfTest = -1;
	}
}

void	WED_SlippyMap::TimerFired()
{
	if(mSelfTest >= 0)
		self_test_step();
	GetHost()->Refresh();
}

static bool replace_token(string& str, const string& from, const string& to)
{
    size_t start_pos = str.find(from);
    if(start_pos == string::npos)
        return false;
    str = str.substr(0,start_pos) + to + str.substr(start_pos+from.length());
    return true;
}

void	WED_SlippyMap::SetMode(int mode)
{
	if(mode == 0)
	{
		mMapMode = 0;
		mDrawnMode = 0;
		SetVisible(0);
		if(GetHost())
			GetHost()->Refresh();
		return;
	}

	int idx = predefined_idx(mode);
	if(idx >= 0)
	{
		url_printf_fmt = slippy_sources[idx].url;
		if(slippy_sources[idx].needs_key)
		{
			if(gTiandituVerified && !gTiandituKey.empty())
				url_printf_fmt += gTiandituKey;
			else
			{
				LOG_MSG("I/Sli map %d needs a verified Tianditu key, turned off\n", mode);
				url_printf_fmt.clear();
			}
		}
	}
	else if(mode == MODE_CUSTOM)
		url_printf_fmt = gCustomSlippyMap;
	else
		url_printf_fmt.clear();     // unknown mode, e.g. saved by a newer WED - fails below and turns the map off

	y_coordinate_math = yNone;
	if     (replace_token(url_printf_fmt, "${y}",  "%2$d")) 	y_coordinate_math = yNormal;
	else if(replace_token(url_printf_fmt, "${!y}", "%2$d")) 	y_coordinate_math = yYahoo;
	else if(replace_token(url_printf_fmt, "${-y}", "%2$d")) 	y_coordinate_math = yOSGeo;


	if(replace_token(url_printf_fmt, "${x}", "%1$d") &&
	   y_coordinate_math != yNone &&
	   replace_token(url_printf_fmt, "${z}", "%3$d"))
	{
		// The cache file name comes from the url. With a query string all tiles would share the same folder,
		// so put the tile coordinates into the folder names instead.
		size_t query_pos = url_printf_fmt.find('?');
		if(query_pos == string::npos)
			dir_printf_fmt = url_printf_fmt;
		else
			dir_printf_fmt = url_printf_fmt.substr(0, query_pos) + "/%3$d/%1$d/%2$d";
		dir_printf_fmt = dir_printf_fmt.substr(dir_printf_fmt.find("//")+2);
		replace(dir_printf_fmt.begin(), dir_printf_fmt.end(), '/', DIR_CHAR);
		for(auto& c : dir_printf_fmt)                        // e.g. PDOK's ".../EPSG:3857/..." - ':' is illegal in Windows folder names
			if(strchr(":*?\"<>|", c))
				c = '_';

		mMapMode = mode;
		SetVisible(1);
		if(slippy_debug())
			LOG_MSG("I/Sli mode %d url %s\n                dir %s\n", mode, redact_key(url_printf_fmt).c_str(), dir_printf_fmt.c_str());
	}
	else
	{
		mMapMode = 0;
		SetVisible(0);
		LOG_MSG("E/Sli Illegal URL string %s for SlippyMap\n", url_printf_fmt.c_str());
	}
	// SetVisible() does not redraw. Switching between two maps changes no visibility, so without this the old map
	// stayed on screen until something else redrew the map - e.g. from ESRI, once loaded, back to Japan.
	if(GetHost())
		GetHost()->Refresh();
}

void	WED_SlippyMap::AskForKey(int mode)
{
	WED_TiandituKeyDialog::Open(this, mode);
}

int		WED_SlippyMap::GetMode(void)
{
	return mMapMode;
}
