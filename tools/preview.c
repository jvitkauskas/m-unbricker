/* SPDX-License-Identifier: MIT */
/* Host preview: the app's own page renderer (src/ui.c) drawing every page of
 * src/pages.h into a framebuffer, written as raw RGB565 files for
 * tools/preview.py. Build: see tools/preview.py. */
#include <stdio.h>
#include <string.h>
#include "ui.h"
#include "lcd.h"
#include "pages.h"

static unsigned char fb[240 * 240 * 2];
static unsigned wx, wy, ww, wh, pos;

int lcd_init(u32 board) { (void)board; memset(fb, 0, sizeof fb); return 1; }
void lcd_window(u32 x, u32 y, u32 w, u32 h) { wx = x; wy = y; ww = w; wh = h; pos = 0; }
void lcd_pixels(const u8 *p, u32 n)
{
	for (u32 i = 0; i + 1 < n; i += 2, pos++) {
		unsigned x = wx + pos % ww, y = wy + pos / ww;
		if (y < wy + wh) { fb[(y * 240 + x) * 2] = p[i]; fb[(y * 240 + x) * 2 + 1] = p[i + 1]; }
	}
}
void lcd_end(void) {}

static void dump(const char *name)
{
	char path[256];
	snprintf(path, sizeof path, "%s/%s.raw", OUTDIR, name);
	FILE *f = fopen(path, "wb");
	fwrite(fb, 1, sizeof fb, f);
	fclose(f);
}

#define SHOT(pg, extra) do { ui_show(&pg); extra; dump(#pg); } while (0)

int main(void)
{
	ui_init(0);
	SHOT(PG_welcome, ui_status("version 0.4", C_DIM));
	SHOT(PG_unplug, );
	SHOT(PG_connect, );
	SHOT(PG_switch_on, ui_status("Waiting for it… 12 s", C_DIM));
	SHOT(PG_already_on, );
	SHOT(PG_no_answer, );
	SHOT(PG_waking, );
	SHOT(PG_connecting, );
	SHOT(PG_found_fm1, ui_progress(40, 100));
	SHOT(PG_found_smk37, ui_progress(70, 100));
	SHOT(PG_ready_fm1, ui_status("Changes 155 of 255 sectors", C_DIM));
	SHOT(PG_ready_smk37, ui_status("Changes 192 of 255 sectors", C_DIM));
	SHOT(PG_nothing, );
	SHOT(PG_repairing, ui_progress(120, 192));
	SHOT(PG_done_fm1, );
	SHOT(PG_done_smk37, );
	SHOT(PG_left, );
	SHOT(PG_err_connect, ui_status("usb 31 · 30000A0C", C_DIM));
	SHOT(PG_err_other, );
	SHOT(PG_err_unknown, ui_status("flash C84017 key 98", C_DIM));
	SHOT(PG_err_image, );
	SHOT(PG_err_read, );
	SHOT(PG_err_write, ui_status("VERIFY 1F · 00000000", C_DIM));
	SHOT(PG_err_timer, );
	return 0;
}
