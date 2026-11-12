/* SPDX-License-Identifier: MIT */
/* m-unbricker: a working unit unbricks another (FM-1 or SMK-37, either way).
 *
 * A RAM app (m-bootloader docs/SPEC.md 11.8), one build per board it runs on.
 * It is started on the working unit by the m-unbricker web page (through
 * m-bootloader's MIDI mode or the RAM stage) or by `m-flasher run-ram`, and
 * installs nothing there. Guided pages take the user through it:
 *   unplug from the phone / computer, connect the broken unit, switch it on;
 *   then the JieLi USB_KEY and the calibration pulses wake its ROM, this unit
 *   talks to that ROM as a USB host (M-Flasher's loader), identifies the
 *   model, compares the flash with the rescue image (m-bootloader + the
 *   fm1-base template), and after PLAY (SEQ PLAY on the SMK-37) writes,
 *   verifies and restarts it. PLAY at the end restarts this unit into its
 *   own firmware. */
#include "hw.h"
#include "usbio.h"
#include "husb.h"
#include "bot.h"
#include "jl.h"
#include "writer.h"
#include "boot.h"
#include "ui.h"
#include "lcd.h"
#include "pages.h"

#define VERSION "0.4"

#if BOARD_ID == 2
#define THIS_BOARD BRD_SMK
#else
#define THIS_BOARD BRD_FM1
#endif

extern char rk_bss_start[], rk_bss_end[];

static struct dev_info di;
static struct jl_info ji;
static struct plan plan;
static char text[48], detail[48];

/* ------------------------------------------------------------ helpers */

static char *cat(char *d, const char *s)
{
	while (*s)
		*d++ = *s++;
	*d = 0;
	return d;
}

static char *hex(char *d, u32 v, int digits)
{
	while (digits--)
		*d++ = "0123456789ABCDEF"[(v >> (4 * digits)) & 15];
	*d = 0;
	return d;
}

/* Waits for PLAY (1) or, if `no` is set, the leave key (0). With `ping`, the
 * other unit's loader is kept busy meanwhile. */
static int wait_key(int no, int ping)
{
	u32 k, prev = K_SEL | K_PREV, t = tb_now();

	for (;;) {
		k = plat_menu_scan();
		if ((k & K_SEL) && !(prev & K_SEL))
			break;
		if (no && (k & K_PREV) && !(prev & K_PREV)) {
			plat_keys_off();
			return 0;
		}
		prev = k;
		if (ping && tb_ms_since(t) >= 1000u) {
			t = tb_now();
			jl_ping();
		}
	}
	plat_keys_off();
	return 1;
}

/* M-Flasher's RESTART program: clears the download-mode marker at 0x01C7FD80
 * and requests a software reset (pi32v2-isa tools/hwtest/app/reset.S, MIT-0).
 * A RAM app ends by resetting: the unit then starts its own firmware. */
static const u8 restart_prog[34] __attribute__((aligned(4))) = {
	0xc4, 0xff, 0x80, 0xfd, 0xc7, 0x01, 0x41, 0x20, 0xc1, 0x60, 0xc1, 0x61, 0xc1, 0x62, 0xc1, 0x63,
	0x20, 0x00, 0x60, 0xe0, 0x80, 0x4b, 0x41, 0x60, 0x42, 0x30, 0x21, 0x19, 0xc1, 0x60, 0x20, 0x00,
	0xf7, 0x9f,
};

static void the_end(void)
{
	wait_key(0, 0);
	csync();
	((void (*)(void))(unsigned long)restart_prog)();
	for (;;)
		;
}

/* The loader state's clock boost (m-bootloader SPEC.md 10.4): CPU 96 MHz,
 * LSB stays 48 MHz. Only from download mode (run-ram). */
static void boost(void)
{
	u32 c2 = RD(CLK_CON2) & 0xFF, d = RD(CLK_DIV) & 0x30700;

	if (c2 == 0x80 && d == 0) {
		SETB(CLK_DIV, 0x100);
		csync();
		FIELD(CLK_CON2, 0xFF, 0x40);
		csync();
	}
}

/* ------------------------------------------------------------- errors */

static void problem(const struct page *pg, const char *what, u32 code)
{
	char *d = detail;

	ui_show(pg);
	if (what) {
		d = cat(d, what);
		d = cat(d, " ");
		d = hex(d, code, 2);
		d = cat(d, " · ");
		hex(d, husb_dbg[2] | (husb_dbg[3] & 0xFF000000u), 8);
		ui_status(detail, C_DIM);
	}
	wait_key(0, 0);
}

/* ------------------------------------------------------------ the key */

static u32 key_t0, key_shown;

static void key_tick(const struct key_stats *s)
{
	u32 sec = tb_ms_since(key_t0) / 1000u;
	char *d;

	if (sec == key_shown)
		return;
	key_shown = sec;
	d = cat(text, "Waiting for it… ");
	d = ui_fmt_num(d, sec);
	cat(d, " s");
	ui_status(text, C_DIM);
}

/* Until the other unit's ROM answers the key. */
static void wake_it(void)
{
	struct key_stats ks;
	struct cal_result cr;
	enum key_result kr;

	for (;;) {
		ui_show(&PG_switch_on);
		key_t0 = tb_now();
		key_shown = ~0u;
		kr = key_send(&ks, 60000u, key_tick);
		if (kr == KEY_ACK || kr == KEY_DP_ANSWER)
			break;
		usb_take_pins();
		if (kr == KEY_DP_AT_START)
			ui_show(&PG_already_on);
		else
			ui_show(&PG_no_answer);
		wait_key(0, 0);
	}
	ui_show(&PG_waking);
	cal_pulses(&cr);
}

/* ------------------------------------------------------------ host side */

static void progress(u32 done, u32 total) { ui_progress(done, total); }

/* Which rescue image: the Cardputer app's rules (engine.cpp identify_unit). */
static int which_unit(void)
{
	u8 a[32], b[32];
	int trimdat, trimcopy, i;
	static const u8 td[8] = { 't', 'r', 'i', 'm', 'd', 'a', 't', 0 };
	static const u8 tc[8] = { 'T', 'R', 'I', 'M', 'C', 'O', 'P', 'Y' };
	u32 id = ji.flash_id & 0xFFFFFF;

	if (ji.chip_key != 0x980F)
		return -1;
	trimdat = jl_read(0xC1220, a, 32);
	trimcopy = jl_read(0xFF000, b, 32);
	for (i = 0; i < 8; i++) {
		trimdat = trimdat && a[4 + i] == td[i];
		trimcopy = trimcopy && b[i] == tc[i];
	}
	if (trimdat || trimcopy || id == 0xEB6014)
		return IMG_SMK37;
	if (id == 0x856014 || id == 0xB36014)
		return IMG_FM1;
	return -1;
}

/* 1 = repaired (or nothing to do) and restarted, 0 = try again, -1 = left */
static int session(void)
{
	const struct image *img;
	int step, res, w;
	char *d;

	ui_show(&PG_connecting);
	husb_init();
	if (!husb_wait_attach(5000u)) {
		problem(&PG_err_connect, "attach", 0);
		return 0;
	}
	step = dev_enumerate(&di, &res);
	if (step) {
		problem(&PG_err_connect, "usb", (u32)(step * 16 + res));
		return 0;
	}
	if (di.vid != 0x4C4A || di.pid != 0x8057) {
		problem(&PG_err_other, "id", 0);
		return 0;
	}
	step = jl_start(&ji);
	if (step) {
		problem(&PG_err_connect, "loader", (u32)step);
		return 0;
	}
	w = which_unit();
	if (w < 0) {
		d = cat(text, "flash ");
		d = hex(d, ji.flash_id & 0xFFFFFF, 6);
		d = cat(d, " key");
		problem(&PG_err_unknown, text, ji.chip_key >> 8);
		return -1;
	}
	img = image_get(w);
	ui_show(w == IMG_SMK37 ? &PG_found_smk37 : &PG_found_fm1);
	ui_progress(0, 1);
	if (!image_ok(img)) {
		problem(&PG_err_image, "image", 0);
		return -1;
	}
	if (!plan_make(img, &plan, progress)) {
		problem(&PG_err_read, wr_err, wr_err_addr >> 12);
		return 0;
	}
	if (!plan.to_write && !plan.to_erase) {
		ui_show(&PG_nothing);
	} else {
		ui_show(w == IMG_SMK37 ? &PG_ready_smk37 : &PG_ready_fm1);
		d = cat(text, "Changes ");
		d = ui_fmt_num(d, plan.to_write + plan.to_erase);
		cat(d, " of 255 sectors");
		ui_status(text, C_DIM);
	}
	if (!wait_key(1, 1)) {
		ui_show(&PG_left);
		return -1;
	}
	if (plan.to_write || plan.to_erase) {
		ui_show(&PG_repairing);
		if (!plan_apply(img, &plan, progress)) {
			problem(&PG_err_write, wr_err, wr_err_addr >> 12);
			return 0;
		}
	}
	jl_restart();
	ui_show(w == IMG_SMK37 ? &PG_done_smk37 : &PG_done_fm1);
	return 1;
}

/* ------------------------------------------------------------- the app */

/* arg: the LSB clock in MHz when started over MIDI (m-bootloader's MIDI mode
 * or the RAM stage, which stopped USB first); a pointer to the ROM's words
 * (>= 0x01000000) when started by `m-flasher run-ram` from download mode. */
void rk_main(u32 arg)
{
	int from_rom = arg >= 0x01000000u, r;
	u32 seen = 0, a, b;
	char *p;

	cli();
	for (p = rk_bss_start; p < rk_bss_end; p++)
		*p = 0;
	p33_and(P33_WDOG, 0xEF); /* watchdog off */
	if (from_rom)
		boost();
	else if (arg >= 12 && arg <= 240)
		tb_tps = arg * 1000000u;
	tb_start();
	a = tb_now();
	spin(20000);
	b = tb_now();
	/* from download mode the PC is still connected: its 1 ms frames are the reference */
	if (a != b && from_rom)
		tb_calibrate_sof(1000, &seen);
	usb_take_pins(); /* the host sees the unit go */

	ui_init(THIS_BOARD);
	if (a == b) {
		ui_show(&PG_err_timer);
		for (;;)
			;
	}
	ui_show(&PG_welcome);
	ui_status("version " VERSION, C_DIM);
	wait_key(0, 0);
	ui_show(&PG_unplug);
	wait_key(0, 0);
	ui_show(&PG_connect);
	wait_key(0, 0);
	for (;;) {
		wake_it();
		r = session();
		if (r)
			break;
	}
	the_end();
}
