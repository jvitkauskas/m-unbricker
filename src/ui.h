/* SPDX-License-Identifier: MIT */
/* Guided pages on the 240x240 panel: a title, a 4-colour picture, a few
 * lines of text, a status line or a progress bar, and the key hint. Drawn in
 * strips of a few pixel rows, with no framebuffer. Text is UTF-8. */
#ifndef RK_UI_H
#define RK_UI_H
#include "mbl.h"

struct glyph {
	u16 cp, off;
	u8 w, h;
	signed char x0, y0;
	u8 adv;
};
struct font {
	const struct glyph *g;
	const u8 *bits;
	u16 n;
	u8 height, ascent;
};
/* A picture: 2-bit colour indices, run-length coded: each byte is
 * (run - 1) << 2 | index, runs of 1..64 pixels, row after row. */
struct pic {
	u16 w, h;
	u16 pal[4];
	const u8 *rle;
};

#define C_BG 0x0000
#define C_TEXT 0xFFFF
#define C_DIM 0x9CF3 /* grey */
#define C_ACCENT 0xFEA0 /* yellow */
#define C_GOOD 0x5FEB /* green */
#define C_BAD 0xFA8A /* red */
#define C_INFO 0x5D9F /* blue */

struct page {
	const struct pic *pic;
	u16 accent;      /* the title's colour */
	const char *title;
	const char *body;  /* wrapped; at most 3 lines */
	const char *hint;  /* bottom line, e.g. "PLAY: next" */
};

void ui_init(u32 board);
void ui_show(const struct page *p);
/* The line above the hint: a short status text, or a progress bar
 * (done of total). Redraws only that line. */
void ui_status(const char *text, u16 color);
void ui_progress(u32 done, u32 total);
/* Small text helpers for status lines. */
char *ui_fmt_num(char *out, u32 v);

#endif
