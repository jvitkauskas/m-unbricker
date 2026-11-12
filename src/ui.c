/* SPDX-License-Identifier: MIT */
/* Guided pages, drawn in strips of BAND pixel rows (no framebuffer).
 *
 *   0..27    title (font_title), centred, in the page's accent colour
 *   28..147  picture, 120x120, centred
 *   150..206 body (font_body), up to 3 centred lines
 *   207..221 status line (font_small) or progress bar
 *   223..239 key hint (font_small), yellow */
#include "ui.h"
#include "lcd.h"
#include "fonts.h"

#define W 240
#define BAND 8
#define PIC_Y 28
#define BODY_Y 148
#define STATUS_Y 207
#define STATUS_H 15
#define HINT_Y 223
#define MAX_LINES 8
#define TEXT_W 228

struct tline {
	const struct font *f;
	const char *s;
	u16 len;
	short x, y;
	u16 col[4];
};

static u8 band[W * BAND * 2] __attribute__((aligned(4)));
static struct tline lines[MAX_LINES];
static u32 nlines;
static const struct pic *pic;
static const u8 *pic_p;
static u32 pic_run, pic_ci;
static s32 bar = -1; /* progress in 1/1000, or -1 */
static char status_buf[40];
static u16 status_col;

/* ------------------------------------------------------------ text */

static u32 next_cp(const char **ps)
{
	const u8 *s = (const u8 *)*ps;
	u32 c = *s++;

	if (c >= 0xE0 && s[0] && s[1]) {
		c = ((c & 0x0F) << 12) | ((s[0] & 0x3F) << 6) | (s[1] & 0x3F);
		s += 2;
	} else if (c >= 0xC0 && s[0]) {
		c = ((c & 0x1F) << 6) | (s[0] & 0x3F);
		s += 1;
	}
	*ps = (const char *)s;
	return c;
}

static const struct glyph *find(const struct font *f, u32 cp)
{
	u32 i;

	for (i = 0; i < f->n; i++)
		if (f->g[i].cp == cp)
			return &f->g[i];
	return &f->g[0]; /* space */
}

static u32 text_width(const struct font *f, const char *s, u32 len)
{
	const char *e = s + len;
	u32 w = 0;

	while (s < e && *s)
		w += find(f, next_cp(&s))->adv;
	return w;
}

static void shades(u16 *col, u16 fg)
{
	u32 r = fg >> 11, g = (fg >> 5) & 63, b = fg & 31, i;

	for (i = 0; i < 4; i++)
		col[i] = (u16)(((r * i / 3) << 11) | ((g * i / 3) << 5) | (b * i / 3));
}

static void add_line(const struct font *f, const char *s, u32 len, s32 y, u16 fg)
{
	struct tline *t;

	if (nlines >= MAX_LINES)
		return;
	t = &lines[nlines++];
	t->f = f;
	t->s = s;
	t->len = (u16)len;
	t->x = (short)((W - (s32)text_width(f, s, len)) / 2);
	t->y = (short)y;
	shades(t->col, fg);
}

/* Greedy word wrap into at most max_lines centred lines from y. */
static void add_wrapped(const struct font *f, const char *s, s32 y, u32 max_lines, u16 fg)
{
	u32 n = 0;

	while (*s && n < max_lines) {
		const char *start = s, *brk = 0, *p = s;
		u32 len;

		while (*p && *p != '\n') {
			const char *q = p;

			next_cp(&q);
			if (text_width(f, start, (u32)(q - start)) > TEXT_W)
				break;
			if (*p == ' ')
				brk = p;
			p = q;
		}
		if (*p && *p != '\n' && brk)
			p = brk;
		len = (u32)(p - start);
		add_line(f, start, len, y, fg);
		y += f->height;
		n++;
		s = p;
		while (*s == ' ' || *s == '\n')
			s++;
	}
}

static void draw_text(const struct tline *t, s32 by)
{
	const char *s = t->s, *e = t->s + t->len;
	s32 x = t->x;

	if (by + BAND <= t->y || by >= t->y + t->f->height)
		return;
	while (s < e && *s) {
		const struct glyph *g = find(t->f, next_cp(&s));
		s32 gx0 = x + g->x0, gy0 = t->y + g->y0, row, col;

		for (row = 0; row < g->h; row++) {
			s32 yy = gy0 + row - by;

			if (yy < 0 || yy >= BAND)
				continue;
			for (col = 0; col < g->w; col++) {
				u32 i = (u32)(row * g->w + col);
				u32 lv = (t->f->bits[g->off + i / 4] >> (2 * (i % 4))) & 3;
				s32 xx = gx0 + col;

				if (lv && xx >= 0 && xx < W) {
					u8 *px = band + (yy * W + xx) * 2;

					px[0] = (u8)(t->col[lv] >> 8);
					px[1] = (u8)t->col[lv];
				}
			}
		}
		x += g->adv;
	}
}

/* ------------------------------------------------------------- picture */

static void pic_start(void)
{
	pic_p = pic ? pic->rle : 0;
	pic_run = 0;
}

static void draw_pic_row(s32 yy)
{
	u32 x0 = (W - pic->w) / 2, x;
	u8 *px = band + (yy * W + x0) * 2;

	for (x = 0; x < pic->w; x++) {
		u16 c;

		if (!pic_run) {
			pic_ci = *pic_p & 3;
			pic_run = (u32)(*pic_p++ >> 2) + 1;
		}
		pic_run--;
		c = pic->pal[pic_ci];
		*px++ = (u8)(c >> 8);
		*px++ = (u8)c;
	}
}

/* ---------------------------------------------------------- the strips */

static void draw_bar(s32 by)
{
	s32 y, x, x0 = 30, x1 = 210, y0 = STATUS_Y + 4, y1 = STATUS_Y + 12;
	s32 fill = x0 + 2 + (x1 - x0 - 4) * bar / 1000;

	for (y = by; y < by + BAND; y++) {
		if (y < y0 || y >= y1)
			continue;
		for (x = x0; x < x1; x++) {
			int edge = y == y0 || y == y1 - 1 || x == x0 || x == x1 - 1;
			u16 c = edge ? C_DIM : (x < fill ? C_GOOD : C_BG);
			u8 *px = band + ((y - by) * W + x) * 2;

			px[0] = (u8)(c >> 8);
			px[1] = (u8)c;
		}
	}
}

static void draw_rows(s32 y0, s32 y1, u32 first_status_line)
{
	s32 by, yy;
	u32 i;

	lcd_window(0, (u32)y0, W, (u32)(y1 - y0));
	for (by = y0; by < y1; by += BAND) {
		for (i = 0; i < sizeof band; i++)
			band[i] = 0;
		if (pic)
			for (yy = 0; yy < BAND; yy++)
				if (by + yy >= PIC_Y && by + yy < PIC_Y + pic->h)
					draw_pic_row(yy);
		for (i = 0; i < nlines; i++)
			if (i < first_status_line || bar < 0)
				draw_text(&lines[i], by);
		if (bar >= 0)
			draw_bar(by);
		lcd_pixels(band, (u32)(y1 - by < BAND ? y1 - by : BAND) * W * 2);
	}
	lcd_end();
}

static u32 status_line = MAX_LINES; /* index of the status text line */

static void layout(const struct page *p)
{
	nlines = 0;
	if (p->title)
		add_line(&font_title, p->title, 0xFFFF, 3, p->accent);
	if (p->body)
		add_wrapped(&font_body, p->body, BODY_Y, 3, C_TEXT);
	if (p->hint)
		add_line(&font_small, p->hint, 0xFFFF, HINT_Y, C_ACCENT);
	status_line = nlines;
	if (status_buf[0] && bar < 0)
		add_line(&font_small, status_buf, 0xFFFF, STATUS_Y, status_col);
}

static const struct page *cur;

void ui_init(u32 board) { lcd_init(board); }

void ui_show(const struct page *p)
{
	cur = p;
	pic = p->pic;
	status_buf[0] = 0;
	bar = -1;
	layout(p);
	pic_start();
	draw_rows(0, 240, MAX_LINES);
}

void ui_status(const char *text, u16 color)
{
	u32 i;

	for (i = 0; text[i] && i < sizeof status_buf - 1; i++)
		status_buf[i] = text[i];
	status_buf[i] = 0;
	status_col = color;
	bar = -1;
	if (!cur)
		return;
	layout(cur);
	pic = 0; /* the picture ends above the status line */
	draw_rows(STATUS_Y, STATUS_Y + STATUS_H + 1, MAX_LINES);
	pic = cur->pic;
}

void ui_progress(u32 done, u32 total)
{
	s32 v = total ? (s32)(done * 1000u / total) : 0;

	if (v == bar)
		return;
	bar = v > 1000 ? 1000 : v;
	if (!cur)
		return;
	pic = 0;
	draw_rows(STATUS_Y, STATUS_Y + STATUS_H + 1, status_line);
	pic = cur->pic;
}

char *ui_fmt_num(char *out, u32 v)
{
	char b[10];
	int n = 0;

	do {
		b[n++] = (char)('0' + v % 10u);
		v /= 10u;
	} while (v);
	while (n)
		*out++ = b[--n];
	*out = 0;
	return out;
}
