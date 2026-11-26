/* SPDX-License-Identifier: MIT */
/* The C side of the comparison: runs one scenario of M-Unbricker's C (bd5ea96) on the simulated chip
 * (tests/chip). `drive <scenario>`; the summary goes to stdout. Built once per board (BOARD_ID 1, 2, and 0 for the
 * build for both) by tests/c/build.sh. A Rust driver calls the Rust functions of the same names in the same order
 * and prints the same marks: each scenario's steps are listed with its function below.
 *
 * Marks are printed with chip_mark(); "%d" etc. below are plain decimal, "%X" upper-case hex. */
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define rk_main c_rk_main
#include "main.c"
#include "chip.h"

/* src/mbl/hw.h's HOST_TEST hooks: plain volatile accesses of the real addresses, which the chip traps. */
u32 sim_rd(u32 a) { return *(volatile u32 *)(uintptr_t)a; }
void sim_wr(u32 a, u32 v) { *(volatile u32 *)(uintptr_t)a = v; }
u32 sim_dma(const void *p) { return (u32)(uintptr_t)p; }
u8 *ram_ptr(u32 a) { return (u8 *)(uintptr_t)a; }
char rk_bss_start[1], rk_bss_end[1];

static void mark(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
static void mark(const char *fmt, ...)
{
	char b[512];
	va_list ap;

	va_start(ap, fmt);
	vsnprintf(b, sizeof b, fmt, ap);
	va_end(ap);
	chip_mark(b);
}

static u32 board; /* from the scenario's name: -fm1 = 1, -smk37 = 2 */

/* ---------------------------------------------------------------- panel-<board>
 * tb_start(); ui_init(board); chip_mark_panel("ui_init");
 * then for each page of tools/preview.c, in its order: ui_show(&PG_x); the same ui_status / ui_progress call
 * as preview.c; chip_mark_panel("PG_x"). */
#define SHOT(pg, extra)                                                                                               \
	do {                                                                                                          \
		ui_show(&pg);                                                                                         \
		extra;                                                                                                \
		chip_mark_panel(#pg);                                                                                 \
	} while (0)

static void sc_panel(void)
{
	tb_start();
	ui_init(board);
	chip_mark_panel("ui_init");
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
}

/* ---------------------------------------------------------------- keys-<board>
 * tb_start(); r = wait_key(0, 0); mark "wait_key(0,0) r";
 * r = wait_key(1, 0); mark "wait_key(1,0) r"; r = wait_key(1, 0); mark "wait_key(1,0) r".
 * No panel: the keys alone. */
static void sc_keys(void)
{
	int r;

	tb_start();
	r = wait_key(0, 0);
	mark("wait_key(0,0) %d", r);
	r = wait_key(1, 0);
	mark("wait_key(1,0) %d", r);
	r = wait_key(1, 0);
	mark("wait_key(1,0) %d", r);
}

/* ---------------------------------------------------------------- wake-ack-<board>, wake-dpanswer-fm1
 * tb_start(); ui_init(board); mark "ui_init"; wake_it(); mark "wake_it". */
static void sc_wake_it(void)
{
	tb_start();
	ui_init(board);
	mark("ui_init");
	wake_it();
	mark("wake_it");
}

static const char *const KR[] = { "ack", "dp-at-start", "dp-answer", "timeout" };

/* The start of wake_it() by hand, with a timeout: ui_show(&PG_switch_on); key_t0 = tb_now(); key_shown = ~0u;
 * r = key_send(&ks, timeout_ms, key_tick); mark "key_send <r> packets P false_acks F alive A". */
static enum key_result send_key(u32 timeout_ms)
{
	struct key_stats ks;
	enum key_result kr;

	ui_show(&PG_switch_on);
	key_t0 = tb_now();
	key_shown = ~0u;
	kr = key_send(&ks, timeout_ms, key_tick);
	mark("key_send %s packets %u false_acks %u alive %u", KR[kr], ks.packets, ks.false_acks, ks.alive);
	return kr;
}

/* wake-dpstart-<board>, wake-none-<board>:
 * tb_start(); ui_init(board); mark "ui_init"; send_key(1000) (above); usb_take_pins();
 * ui_show(r == KEY_DP_AT_START ? &PG_already_on : &PG_no_answer); mark "page". */
static void sc_wake_fail(void)
{
	enum key_result kr;

	tb_start();
	ui_init(board);
	mark("ui_init");
	kr = send_key(1000);
	usb_take_pins();
	ui_show(kr == KEY_DP_AT_START ? &PG_already_on : &PG_no_answer);
	mark("page");
}

/* wake-false-<board>:
 * tb_start(); ui_init(board); mark "ui_init"; send_key(5000); ui_show(&PG_waking); cal_pulses(&cr);
 * mark "cal_pulses pulse_ms X drop_ms Y pulses Z" (drop_ms signed). */
static void sc_wake_false(void)
{
	struct cal_result cr;

	tb_start();
	ui_init(board);
	mark("ui_init");
	send_key(5000);
	ui_show(&PG_waking);
	cal_pulses(&cr);
	mark("cal_pulses pulse_ms %u drop_ms %d pulses %u", cr.pulse_ms, cr.drop_ms, cr.pulses);
}

/* ---------------------------------------------------------------- sof-*
 * tb_start(); r = tb_calibrate_sof(1000, &seen); mark "tb_calibrate_sof r seen N tps T measured M". */
static void sc_sof(void)
{
	u32 seen = 0;
	int r;

	tb_start();
	r = tb_calibrate_sof(1000, &seen);
	mark("tb_calibrate_sof %d seen %u tps %u measured %u", r, seen, tb_tps, tb_measured);
}

/* ---------------------------------------------------------------- detect-*
 * b = board_detect(&jedec); mark "board B jedec %06X". (No tb_start: board_detect needs no time.) */
static void sc_detect(void)
{
	u32 jedec = 0, b = board_detect(&jedec);

	mark("board %u jedec %06X", b, jedec);
}

/* ---------------------------------------------------------------- session-*
 * tb_start(); ui_init(board); mark "ui_init"; r = session(); then the marks of after(). */
static void after(int r)
{
	mark("session %d", r);
	mark("plan write %u erase %u runs %u", plan.to_write, plan.to_erase, plan.runs);
	mark("loader %s key %04X flash %06X io %u retries %u", ji.loader, ji.chip_key, ji.flash_id & 0xFFFFFF, ji.io,
	     ji.retries);
}

static void sc_session(void)
{
	tb_start();
	ui_init(board);
	mark("ui_init");
	after(session());
}

/* ---------------------------------------------------------------- full-fm1
 * rk_main's flow after the timer check, without the_end():
 * tb_start(); usb_take_pins(); ui_init(board); mark "ui_init";
 * ui_show(&PG_welcome); ui_status("version " VERSION, C_DIM); wait_key(0, 0);
 * ui_show(&PG_unplug); wait_key(0, 0); ui_show(&PG_connect); wait_key(0, 0); mark "pages";
 * do { wake_it(); mark "wake_it"; r = session(); } while (!r); then the marks of after(). */
static void sc_full(void)
{
	int r;

	tb_start();
	usb_take_pins();
	ui_init(board);
	mark("ui_init");
	ui_show(&PG_welcome);
	ui_status("version " VERSION, C_DIM);
	wait_key(0, 0);
	ui_show(&PG_unplug);
	wait_key(0, 0);
	ui_show(&PG_connect);
	wait_key(0, 0);
	mark("pages");
	do {
		wake_it();
		mark("wake_it");
		r = session();
	} while (!r);
	after(r);
}

/* ---------------------------------------------------------------- the table */

static const struct {
	const char *name;
	void (*run)(void);
} SC[] = {
	{ "panel-fm1", sc_panel },
	{ "panel-smk37", sc_panel },
	{ "keys-fm1", sc_keys },
	{ "keys-smk37", sc_keys },
	{ "wake-ack-fm1", sc_wake_it },
	{ "wake-ack-smk37", sc_wake_it },
	{ "wake-dpanswer-fm1", sc_wake_it },
	{ "wake-dpstart-fm1", sc_wake_fail },
	{ "wake-dpstart-smk37", sc_wake_fail },
	{ "wake-none-fm1", sc_wake_fail },
	{ "wake-none-smk37", sc_wake_fail },
	{ "wake-false-fm1", sc_wake_false },
	{ "wake-false-smk37", sc_wake_false },
	{ "sof-present", sc_sof },
	{ "sof-absent", sc_sof },
	{ "sof-fast", sc_sof },
	{ "sof-reject", sc_sof },
	{ "detect-fm1-856014", sc_detect },
	{ "detect-fm1-b36014", sc_detect },
	{ "detect-smk37-trimdat", sc_detect },
	{ "detect-smk37-trimcopy", sc_detect },
	{ "detect-unknown", sc_detect },
	{ "detect-fm1chip-smkmark", sc_detect },
	{ "detect-none", sc_detect },
	{ "session-repair-fm1", sc_session },
	{ "session-repair-smk37", sc_session },
	{ "session-cross-fm1", sc_session },
	{ "session-cross-smk37", sc_session },
	{ "session-nothing-fm1", sc_session },
	{ "session-leave-fm1", sc_session },
	{ "session-leave-smk37", sc_session },
	{ "session-unknown-fm1", sc_session },
	{ "session-otherid-fm1", sc_session },
	{ "session-noattach-fm1", sc_session },
	{ "session-stall-fm1", sc_session },
	{ "session-early-fm1", sc_session },
	{ "session-odd-fm1", sc_session },
	{ "session-crc-fm1", sc_session },
	{ "session-writefail-fm1", sc_session },
	{ "session-readerr-fm1", sc_session },
	{ "session-otherloader-fm1", sc_session },
	{ "session-noisy-fm1", sc_session },
	{ "full-fm1", sc_full },
};

static int ends(const char *s, const char *t)
{
	size_t a = strlen(s), b = strlen(t);

	return a >= b && !strcmp(s + a - b, t);
}

int main(int argc, char **argv)
{
	size_t i;

	if (argc != 2) {
		fprintf(stderr, "usage: drive <scenario> | drive --list\n");
		return 2;
	}
	for (i = 0; i < sizeof SC / sizeof SC[0]; i++) {
		if (!strcmp(argv[1], "--list")) {
			printf("%s\n", SC[i].name);
			continue;
		}
		if (strcmp(argv[1], SC[i].name))
			continue;
		board = ends(SC[i].name, "-fm1") ? BRD_FM1 : ends(SC[i].name, "-smk37") ? BRD_SMK : 0;
		if (BOARD_ID != 0 && board && board != BOARD_ID) {
			fprintf(stderr, "drive: %s is for the other board's build\n", SC[i].name);
			return 2;
		}
#if BOARD_ID == 0
		rk_board = board; /* what board_detect() would have found */
#endif
		chip_start(SC[i].name);
		SC[i].run();
		chip_finish();
		return 0;
	}
	if (strcmp(argv[1], "--list")) {
		fprintf(stderr, "drive: no scenario %s\n", argv[1]);
		return 2;
	}
	return 0;
}
