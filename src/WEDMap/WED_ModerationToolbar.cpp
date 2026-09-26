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

#include "WED_ModerationToolbar.h"
#include "WED_ModerationLayer.h"
#include "WED_LiveryModeration.h"
#include "GUI_GraphState.h"
#include "GUI_Resources.h"
#include "WED_MapPane.h"

#if APL
	#include <OpenGL/gl.h>
#else
	#include <GL/gl.h>
#endif

static const char * kArt = "moderation_tools.png";
static const int kTools = 2;
static const char * kTips[kTools] = {
	"Moderation View - highlight the ramp starts to check, and show the airport overview",
	"Rotate view (select tool only) - drag along a row to level it; then drag to select (Alt+drag: new line); Shift+right-drag turns in 90-degree steps from the line; click again for north up"
};

WED_ModerationToolbar::WED_ModerationToolbar(WED_MapPane * map) : mMap(map)
{
}

void	WED_ModerationToolbar::CellSize(int & w, int & h)
{
	int m[2] = { 0, 0 };
	GUI_GetImageResourceSize(kArt, m);
	w = m[0] / 2 / Columns();		// normal half, selected half
	h = m[1];
}

bool	WED_ModerationToolbar::IsOn(int tool) const
{
	return tool == 0 ? WED_ModerationViewOn() : (mMap && mMap->IsViewRotated());
}

// Tools fill the pane bottom-up, left to right: tool n is column n % 2 of row n / 2.
int		WED_ModerationToolbar::ToolAt(int x, int y, int cell[4])
{
	int b[4], w, h;
	GetBounds(b);
	CellSize(w, h);
	if (w <= 0 || h <= 0) return -1;
	int col = (x - b[0]) / w, row = (y - b[1]) / h;
	if (x < b[0] || y < b[1] || col >= Columns()) return -1;
	int n = row * Columns() + col;
	if (n < 0 || n >= kTools) return -1;
	cell[0] = b[0] + col * w; cell[1] = b[1] + row * h;
	cell[2] = cell[0] + w;    cell[3] = cell[1] + h;
	return n;
}

void	WED_ModerationToolbar::Draw(GUI_GraphState * state)
{
	if (!WED_ModerationEnabled()) return;
	GUI_TexPosition_t m;
	int tex = GUI_GetTextureResource(kArt, 0, &m);
	int b[4], w, h;
	GetBounds(b);
	CellSize(w, h);
	if (!tex || w <= 0) return;

	state->SetState(0, 1, 0, 1, 1, 0, 0);
	state->BindTex(tex, 0);
	glColor3f(1, 1, 1);
	glBegin(GL_QUADS);
	for (int n = 0; n < kTools; ++n)
	{
		const int col = n % Columns(), row = n / Columns();
		const float sx = (float) (b[0] + col * w), sy = (float) (b[1] + row * h);
		const float tx = (float) (n * w + (IsOn(n) ? m.real_width / 2 : 0));
		const float s0 = tx / m.tex_width, s1 = (tx + w) / m.tex_width;
		const float t0 = 0.0f, t1 = (float) h / m.tex_height;
		glTexCoord2f(s0, t0); glVertex2f(sx, sy);
		glTexCoord2f(s0, t1); glVertex2f(sx, sy + h);
		glTexCoord2f(s1, t1); glVertex2f(sx + w, sy + h);
		glTexCoord2f(s1, t0); glVertex2f(sx + w, sy);
	}
	glEnd();

	// The grid the map tools draw between their cells, over the art: a line
	// above the row and down each side of every cell.
	state->SetState(0, 0, 0, 0, 0, 0, 0);
	glColor3f(0.40f, 0.40f, 0.40f);
	glBegin(GL_LINES);
	const int rows = (kTools + Columns() - 1) / Columns();
	for (int r = 0; r <= rows; ++r)
	{
		const float y = (float) (b[1] + r * h) - (r == rows ? 0.5f : -0.5f);
		glVertex2f((float) b[0], y); glVertex2f((float) (b[0] + Columns() * w), y);
	}
	for (int c = 0; c <= Columns(); ++c)
	{
		const float x = (float) (b[0] + c * w) + (c == Columns() ? -0.5f : 0.5f);
		glVertex2f(x, (float) b[1]); glVertex2f(x, (float) (b[1] + rows * h));
	}
	glEnd();
}

int		WED_ModerationToolbar::MouseDown(int x, int y, int button)
{
	if (!WED_ModerationEnabled()) return 0;
	int cell[4];
	int n = ToolAt(x, y, cell);
	if (n < 0) return 0;
	if (n == 0)		WED_SetModerationView(!WED_ModerationViewOn());
	else if (mMap)	mMap->ToggleViewRotate();
	Refresh();
	if (mMap) mMap->Refresh();
	return 1;
}

int		WED_ModerationToolbar::GetHelpTip(int x, int y, int tip_bounds[4], std::string& tip)
{
	if (!WED_ModerationEnabled()) return 0;
	int n = ToolAt(x, y, tip_bounds);
	if (n < 0) return 0;
	tip = kTips[n];
	return 1;
}
