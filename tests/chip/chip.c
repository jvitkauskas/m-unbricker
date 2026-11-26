/* SPDX-License-Identifier: MIT */
/* A simulated AC7911B (tests/chip/README.md): the register ranges at their real addresses, mapped with no access.
 * Every access faults: a plain 32-bit load or store is decoded and done here, anything else is single-stepped.
 * Each one is modelled, logged ("R addr value" / "W addr value"), and hashed. Virtual time is the access count.
 *
 * Models: timer 0, the P33 bridge, GPIO with the key matrix of both boards, the USB pads with the other unit's mask
 * ROM (USB_KEY decoder, ACK, calibration, attach), SOF frames, USB0 as a host with the device behind it (the JieLi
 * ROM's download mode, then M-Flasher's loader, over bulk-only mass storage) and its 1 MB flash, SPI0 with this
 * unit's flash, SPI1 with the ST7789 panel. Each only as far as M-Unbricker (bd5ea96) needs. */
#define _GNU_SOURCE
#include <signal.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <ucontext.h>
#include <unistd.h>
#include "chip.h"

typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;

#define LO 0x10000u
#define HI 0x60000u
#define US 48ull /* ticks per microsecond: the model's clock is 48 MHz */
#define MS 48000ull

/* ================================================================ SHA-256 */

struct sha {
	u32 h[8];
	u8 buf[64];
	u64 len;
};

static const u32 K256[64] = {
	0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
	0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
	0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
	0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
	0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
	0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
	0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
	0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2,
};

#define ROR(x, n) (((x) >> (n)) | ((x) << (32 - (n))))

static void sha_init(struct sha *s)
{
	static const u32 h0[8] = { 0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
				   0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19 };
	memcpy(s->h, h0, sizeof h0);
	s->len = 0;
}

static void sha_block(struct sha *s, const u8 *p)
{
	u32 w[64], a, b, c, d, e, f, g, h, i, t1, t2;

	for (i = 0; i < 16; i++)
		w[i] = (u32)p[4 * i] << 24 | (u32)p[4 * i + 1] << 16 | (u32)p[4 * i + 2] << 8 | p[4 * i + 3];
	for (; i < 64; i++)
		w[i] = w[i - 16] + (ROR(w[i - 15], 7) ^ ROR(w[i - 15], 18) ^ (w[i - 15] >> 3)) + w[i - 7] +
		       (ROR(w[i - 2], 17) ^ ROR(w[i - 2], 19) ^ (w[i - 2] >> 10));
	a = s->h[0], b = s->h[1], c = s->h[2], d = s->h[3], e = s->h[4], f = s->h[5], g = s->h[6], h = s->h[7];
	for (i = 0; i < 64; i++) {
		t1 = h + (ROR(e, 6) ^ ROR(e, 11) ^ ROR(e, 25)) + ((e & f) ^ (~e & g)) + K256[i] + w[i];
		t2 = (ROR(a, 2) ^ ROR(a, 13) ^ ROR(a, 22)) + ((a & b) ^ (a & c) ^ (b & c));
		h = g, g = f, f = e, e = d + t1, d = c, c = b, b = a, a = t1 + t2;
	}
	s->h[0] += a, s->h[1] += b, s->h[2] += c, s->h[3] += d, s->h[4] += e, s->h[5] += f, s->h[6] += g, s->h[7] += h;
}

static void sha_add(struct sha *s, const void *d, size_t n)
{
	const u8 *p = d;
	size_t k = s->len % 64;

	s->len += n;
	if (k) {
		size_t m = 64 - k < n ? 64 - k : n;
		memcpy(s->buf + k, p, m);
		p += m, n -= m;
		if (k + m < 64)
			return;
		sha_block(s, s->buf);
	}
	for (; n >= 64; p += 64, n -= 64)
		sha_block(s, p);
	memcpy(s->buf, p, n);
}

static void sha_hex(const struct sha *s0, char out[65])
{
	struct sha s = *s0;
	u64 bits = s.len * 8;
	u8 pad[72] = { 0x80 };
	size_t k = s.len % 64, n = (k < 56 ? 56 : 120) - k, i;

	for (i = 0; i < 8; i++)
		pad[n + i] = (u8)(bits >> (56 - 8 * i));
	sha_add(&s, pad, n + 8);
	for (i = 0; i < 32; i++)
		sprintf(out + 2 * i, "%02x", (s.h[i / 4] >> (24 - 8 * (i % 4))) & 0xFF);
}

static void sha_of(const void *d, size_t n, char out[65])
{
	struct sha s;

	sha_init(&s);
	sha_add(&s, d, n);
	sha_hex(&s, out);
}

static u64 fnv(const u8 *p, u32 n)
{
	u64 h = 0xcbf29ce484222325ull;

	while (n--)
		h = (h ^ *p++) * 0x100000001b3ull;
	return h;
}

/* ================================================================ JieLi helpers (as src/jl.c) */

static u16 crc16(u16 crc, const u8 *d, u32 n)
{
	for (u32 i = 0; i < n; i++) {
		crc ^= (u16)(d[i] << 8);
		for (int k = 0; k < 8; k++)
			crc = (crc & 0x8000) ? (u16)((crc << 1) ^ 0x1021) : (u16)(crc << 1);
	}
	return crc;
}

static u32 crc32z(const u8 *d, u32 n)
{
	static u32 tab[256];
	u32 c = ~0u;

	if (!tab[1])
		for (u32 i = 0; i < 256; i++) {
			u32 v = i;
			for (int k = 0; k < 8; k++)
				v = (v & 1) ? 0xEDB88320u ^ (v >> 1) : v >> 1;
			tab[i] = v;
		}
	while (n--)
		c = tab[(c ^ *d++) & 0xFF] ^ (c >> 8);
	return ~c;
}

static void crc_cipher(const u8 *in, u8 *out, u32 n, u32 key)
{
	static const u8 MAGIC[16] = { 0xC3, 0xCF, 0xC0, 0xE8, 0xCE, 0xD2, 0xB0, 0xAE,
				      0xC4, 0xE3, 0xA3, 0xAC, 0xD3, 0xF1, 0xC1, 0xD6 };
	u8 hi[2] = { (u8)(key >> 16), (u8)(key >> 24) };
	u16 c = crc16((u16)key, hi, 2);

	for (u32 i = 0; i < n; i++) {
		c = crc16(c, &MAGIC[i % 16], 1);
		out[i] = in[i] ^ (u8)c;
	}
}

static u32 ld32(const u8 *p) { return (u32)p[0] | (u32)p[1] << 8 | (u32)p[2] << 16 | (u32)p[3] << 24; }
static u32 bd32(const u8 *p) { return (u32)p[0] << 24 | (u32)p[1] << 16 | (u32)p[2] << 8 | p[3]; }
static void st32(u8 *p, u32 v) { p[0] = (u8)v, p[1] = (u8)(v >> 8), p[2] = (u8)(v >> 16), p[3] = (u8)(v >> 24); }
static void bs32(u8 *p, u32 v) { p[0] = (u8)(v >> 24), p[1] = (u8)(v >> 16), p[2] = (u8)(v >> 8), p[3] = (u8)v; }

/* M-Flasher's flash loader 1.3 (src/mflr_loader.h at bd5ea96, MFLR_A) and the RESTART program (src/jl.c). */
#define MFLR_LEN 6016u
#define MFLR_SHA "ae0cdf4965cc2438a362e933ccf11d41159322d67f2dc630a61ab84cc58e04d7"
static const u8 RESTART_PROG[34] = {
	0xc4, 0xff, 0x80, 0xfd, 0xc7, 0x01, 0x41, 0x20, 0xc1, 0x60, 0xc1, 0x61, 0xc1, 0x62, 0xc1, 0x63,
	0x20, 0x00, 0x60, 0xe0, 0x80, 0x4b, 0x41, 0x60, 0x42, 0x30, 0x21, 0x19, 0xc1, 0x60, 0x20, 0x00,
	0xf7, 0x9f,
};
/* rescue/<board>/boot.bin at bd5ea96 */
static const char *const BOOT_SHA[3] = { 0, "155d5d2e2264379e8a36ded214524cc2d2348cceedaf43863b6fe8d53eaf1a8d",
					 "dbccf4e69e8fb0dbaf27a3ce7642dbc5fb25f051006a1ba644e6803dac7ed8ec" };

/* ================================================================ scenarios */

enum { K_PREV = 1, K_NEXT = 2, K_SEL = 4, K_REC = 8 };
enum { FL_BLANK, FL_OLD_FM1, FL_OLD_SMK, FL_FIXED_FM1, FL_FIXED_SMK };
enum { REF_STALL, REF_EARLY, REF_ODD };
enum { ROM_OFF, ROM_RUNNING, ROM_ATTACHED };
enum { EXP_NONE, EXP_SAME, EXP_FIXED };

/* A key held from `at` for `dur` ms after the start of key session `sess` (0-based: a session starts when the key
 * matrix's clock pin PA3 becomes an output, plat_menu_scan's keys_on, and ends when it is given back). Sessions with
 * no entry get PLAY from +50 ms for 100 ms, unless no_default_press. */
struct press {
	u8 sess, key;
	u16 at, dur;
};

struct scen {
	const char *name;
	int board; /* this unit (panel pins, keys): 1 FM-1, 2 SMK-37, 0 none */
	u32 tpa; /* timer ticks per access (48 = 1 us) */
	u32 t0; /* timer count at access 0 */
	struct press press[8];
	int no_default_press;
	/* the other unit at the USB pads */
	int rom; /* ROM_OFF: switched on at on_ms (0 = never); ROM_RUNNING: D+ up from the start; ROM_ATTACHED */
	u32 on_ms;
	int no_clamp; /* switched off, it doesn't clamp D- low */
	int need; /* good key packets before the ACK */
	int false_at; /* after this many good packets: D- held low without the D+ pull-up */
	int dp_answer; /* answers by pulling D+ up at once (no D- low) */
	u32 ack_us, false_us, cal_ms, attach_ms;
	int no_attach; /* doesn't attach (after the calibration, or at all) */
	u32 sof; /* ticks per SOF frame from a PC, 0 = none */
	/* the device behind USB0 */
	int dev; /* 1: there is one */
	int dev_board; /* its board: rescue image for FL_FIXED_* and EXP_FIXED */
	int flash; /* FL_* */
	u32 dev_jedec;
	u16 vid, pid;
	int refuse; /* REF_*: how the ROM refuses a command */
	int other_loader;
	u32 io; /* USB_BUFFER_SIZE answer */
	int f_read_stall, f_read_early, f_write_flaky; /* the Nth READ_FLASH / WRITE_FLASH, 1-based */
	int f_write_bad; /* every WRITE_FLASH leaves a byte unprogrammed */
	u32 f_read_fail; /* address + 1: READ_FLASH there always ends in a phase error */
	u32 f_crc_fail; /* address + 1: from the f_crc_from'th CRC32 starting there (1-based), a phase error */
	int f_crc_from;
	int f_crc_short; /* the Nth CRC32 (1-based) answers with only its first 4 bytes */
	/* the Nth bulk OUT packet (1-based) is sent late: the next read of TXCSR1 through USB_CON1 never completes, and the
	 * packet goes (from its buffer as it is then) at the next access to TXCSR1 */
	int f_sie_stuck;
	int no_trimcopy; /* an SMK-37's flash without the TRIMCOPY copy at 0xFF000 */
	int noisy; /* bus errors now and then (bulk OUT 0x04/0x80, bulk IN 0x08, one EP0 error) */
	int expect; /* EXP_* for the device's flash */
	/* this unit's own flash (SPI0) */
	u32 own_jedec;
	int own_marks; /* 1: "trimdat" at 0xC1224, 2: "TRIMCOPY" at 0xFF000 */
};

/* a scenario's later fields override what a group macro set */
#if defined(__clang__)
#pragma clang diagnostic ignored "-Winitializer-overrides"
#elif defined(__GNUC__)
#pragma GCC diagnostic ignored "-Woverride-init"
#endif
#define WAKE .rom = ROM_OFF, .on_ms = 800, .need = 3, .ack_us = 1000, .false_us = 2000, .cal_ms = 1200, .attach_ms = 50
#define DEV(b, fl, id) .rom = ROM_ATTACHED, .dev = 1, .dev_board = (b), .flash = (fl), .dev_jedec = (id), \
	.vid = 0x4C4A, .pid = 0x8057, .io = 4096
#define FM1_OLD DEV(1, FL_OLD_FM1, 0x856014)
#define SMK_OLD DEV(2, FL_OLD_SMK, 0xEB6014)

static const struct scen SCEN[] = {
	/* every page as tools/preview.c draws it */
	{ .name = "panel-fm1", .board = 1, .tpa = 480 },
	{ .name = "panel-smk37", .board = 2, .tpa = 480 },
	/* wait_key(0,0): PLAY; wait_key(1,0): NEXT then the leave key; wait_key(1,0): REC then PLAY */
	{ .name = "keys-fm1", .board = 1, .tpa = 480, .no_default_press = 1,
	  .press = { { 0, K_SEL, 50, 100 }, { 1, K_NEXT, 30, 60 }, { 1, K_PREV, 150, 100 }, { 2, K_REC, 30, 60 },
		     { 2, K_SEL, 150, 100 } } },
	{ .name = "keys-smk37", .board = 2, .tpa = 480, .t0 = 0xFFFE0000u, .no_default_press = 1,
	  .press = { { 0, K_SEL, 50, 100 }, { 1, K_NEXT, 30, 60 }, { 1, K_PREV, 150, 100 }, { 2, K_REC, 30, 60 },
		     { 2, K_SEL, 150, 100 } } },
	/* the key and the calibration pulses */
	{ .name = "wake-ack-fm1", .board = 1, .tpa = 480, WAKE },
	{ .name = "wake-ack-smk37", .board = 2, .tpa = 480, .t0 = 0xFF000000u, WAKE },
	{ .name = "wake-dpanswer-fm1", .board = 1, .tpa = 480, WAKE, .dp_answer = 1 },
	{ .name = "wake-dpstart-fm1", .board = 1, .tpa = 480, .rom = ROM_RUNNING },
	{ .name = "wake-dpstart-smk37", .board = 2, .tpa = 480, .rom = ROM_RUNNING },
	{ .name = "wake-none-fm1", .board = 1, .tpa = 480, WAKE, .on_ms = 0 },
	{ .name = "wake-none-smk37", .board = 2, .tpa = 480, WAKE, .on_ms = 0 },
	{ .name = "wake-false-fm1", .board = 1, .tpa = 480, WAKE, .false_at = 2, .need = 5 },
	{ .name = "wake-false-smk37", .board = 2, .tpa = 480, WAKE, .false_at = 2, .need = 5 },
	/* the timer against the PC's frames */
	{ .name = "sof-present", .tpa = 480, .sof = 48000 },
	{ .name = "sof-absent", .tpa = 480 },
	{ .name = "sof-fast", .tpa = 480, .sof = 50000 }, /* this unit's timer 4 % fast: 50 MHz measured */
	{ .name = "sof-reject", .tpa = 480, .sof = 60000 }, /* 60 MHz: out of range, stays nominal */
	/* which board, from this unit's own flash */
	{ .name = "detect-fm1-856014", .tpa = 48, .own_jedec = 0x856014 },
	{ .name = "detect-fm1-b36014", .tpa = 48, .own_jedec = 0xB36014 },
	{ .name = "detect-smk37-trimdat", .tpa = 48, .own_jedec = 0xEB6014, .own_marks = 1 },
	{ .name = "detect-smk37-trimcopy", .tpa = 48, .own_jedec = 0xEB6014, .own_marks = 2 },
	{ .name = "detect-unknown", .tpa = 48, .own_jedec = 0xC84017 },
	{ .name = "detect-fm1chip-smkmark", .tpa = 48, .own_jedec = 0x856014, .own_marks = 1 },
	{ .name = "detect-none", .tpa = 48, .own_jedec = 0xFFFFFF },
	/* session(): the other unit already in download mode and attached */
	{ .name = "session-repair-fm1", .board = 1, .tpa = 480, FM1_OLD, .expect = EXP_FIXED,
	  .press = { { 0, K_SEL, 1100, 100 } } },
	{ .name = "session-repair-smk37", .board = 2, .tpa = 480, .t0 = 0xFF000000u, SMK_OLD, .expect = EXP_FIXED },
	{ .name = "session-cross-fm1", .board = 1, .tpa = 480, SMK_OLD, .expect = EXP_FIXED },
	{ .name = "session-cross-smk37", .board = 2, .tpa = 480, FM1_OLD, .expect = EXP_FIXED },
	{ .name = "session-nothing-fm1", .board = 1, .tpa = 480, DEV(1, FL_FIXED_FM1, 0xB36014), .expect = EXP_SAME },
	{ .name = "session-leave-fm1", .board = 1, .tpa = 480, FM1_OLD, .expect = EXP_SAME,
	  .press = { { 0, K_PREV, 50, 100 } } },
	{ .name = "session-leave-smk37", .board = 2, .tpa = 480, SMK_OLD, .expect = EXP_SAME,
	  .press = { { 0, K_PREV, 50, 100 } } },
	{ .name = "session-unknown-fm1", .board = 1, .tpa = 480, DEV(1, FL_OLD_FM1, 0xC84017), .expect = EXP_SAME },
	{ .name = "session-otherid-fm1", .board = 1, .tpa = 480, FM1_OLD, .vid = 0x1234, .pid = 0x5678,
	  .expect = EXP_SAME },
	{ .name = "session-noattach-fm1", .board = 1, .tpa = 480, FM1_OLD, .no_attach = 1, .expect = EXP_SAME },
	{ .name = "session-stall-fm1", .board = 1, .tpa = 480, FM1_OLD, .f_read_stall = 5, .expect = EXP_FIXED },
	{ .name = "session-early-fm1", .board = 1, .tpa = 480, FM1_OLD, .refuse = REF_EARLY, .f_read_early = 6,
	  .expect = EXP_FIXED },
	{ .name = "session-odd-fm1", .board = 1, .tpa = 480, FM1_OLD, .refuse = REF_ODD, .expect = EXP_FIXED },
	{ .name = "session-crc-fm1", .board = 1, .tpa = 480, FM1_OLD, .f_write_flaky = 1, .expect = EXP_FIXED },
	{ .name = "session-writefail-fm1", .board = 1, .tpa = 480, FM1_OLD, .f_write_bad = 1 },
	{ .name = "session-readerr-fm1", .board = 1, .tpa = 480, FM1_OLD, .f_read_fail = 0x20000 + 1,
	  .expect = EXP_SAME },
	/* An SMK-37 with a flash chip an FM-1 has, no TRIMCOPY, and its trimdat unreadable: the Rust refuses it (the C took
	 * it for an FM-1 and erased its calibration), so it's checked by its result alone (tests/chip_test.py). */
	{ .name = "session-idfail-fm1", .board = 1, .tpa = 480, DEV(2, FL_OLD_SMK, 0x856014), .no_trimcopy = 1,
	  .f_read_fail = 0xC1224 + 1, .expect = EXP_SAME },
	/* The CRC of the area kept above 0xFF000 can't be read after the write (it could before): the Rust says so,
	 * PROTECTED CRC, where the C said PROTECTED CHANGED; checked by its result alone. */
	{ .name = "session-topcrc-fm1", .board = 1, .tpa = 480, FM1_OLD, .f_crc_fail = 0xFF000 + 1, .f_crc_from = 2,
	  .expect = EXP_FIXED },
	/* The first CRC32 answers with 4 of its 16 bytes: the Rust asks again, where the C took the rest for zeros. */
	{ .name = "session-shortcrc-fm1", .board = 1, .tpa = 480, FM1_OLD, .f_crc_short = 1, .expect = EXP_FIXED },
	/* A bulk OUT packet in a flash write goes late, and the read of TXCSR1 after it gets no answer: the Rust waits for
	 * it, where the C took the read's 0 for an ACK and wrote the next packet over it. */
	{ .name = "session-siestuck-fm1", .board = 1, .tpa = 480, FM1_OLD, .f_sie_stuck = 100, .expect = EXP_FIXED },
	{ .name = "session-otherloader-fm1", .board = 1, .tpa = 480, FM1_OLD, .other_loader = 1, .expect = EXP_SAME },
	{ .name = "session-noisy-fm1", .board = 1, .tpa = 480, FM1_OLD, .noisy = 1, .expect = EXP_FIXED },
	/* rk_main's flow from ui_init to the done page: three pages, the key, the pulses, session() */
	{ .name = "full-fm1", .board = 1, .tpa = 480, FM1_OLD, WAKE, .on_ms = 1500, .expect = EXP_FIXED },
};

/* ================================================================ state */

static const struct scen *S;
static int running;
static u32 R[(HI - LO) / 4];
#define REG(a) R[((a) - LO) >> 2]
static u64 acc, now;
static FILE *tf;
static struct sha th;
static char *sum;
static size_t sum_len, sum_cap;
static uintptr_t st_lo, st_hi, st_hint;
static u64 fallbacks;

static void die(const char *fmt, ...) __attribute__((format(printf, 1, 2), noreturn));
static void die(const char *fmt, ...)
{
	va_list ap;

	fflush(stdout);
	fprintf(stderr, "chip: ");
	va_start(ap, fmt);
	vfprintf(stderr, fmt, ap);
	va_end(ap);
	fprintf(stderr, "\n");
	_exit(3);
}

static void sum_add(const char *s)
{
	size_t n = strlen(s);

	if (sum_len + n + 1 > sum_cap) {
		sum_cap = (sum_cap + n + 1) * 2;
		sum = realloc(sum, sum_cap);
	}
	memcpy(sum + sum_len, s, n + 1);
	sum_len += n;
}

/* ---- the trace ---- */

static const char HEX[] = "0123456789abcdef";

static void emit(const char *s, size_t n)
{
	sha_add(&th, s, n);
	if (tf)
		fwrite(s, 1, n, tf);
}

static char *hexn(char *d, u64 v, int digits)
{
	while (digits--)
		*d++ = HEX[(v >> (4 * digits)) & 15];
	return d;
}

static int is_dma_reg(u32 a)
{
	return a == 0x11C0C || a == 0x11D0C || (a >= 0x11818 && a <= 0x1183C && a != 0x11834);
}

static void log_rw(char op, u32 a, u32 v)
{
	char b[24], *d = b;

	*d++ = op, *d++ = ' ';
	d = hexn(d, a, 5);
	*d++ = ' ';
	if (op == 'W' && is_dma_reg(a))
		memcpy(d, "dma", 3), d += 3; /* the buffer's address is the program's own */
	else
		d = hexn(d, v, 8);
	*d++ = '\n';
	emit(b, (size_t)(d - b));
}

/* Data moved by DMA (or into an endpoint buffer): its length and FNV-1a 64. */
static void log_dma(u32 reg, const u8 *p, u32 n)
{
	char b[48], *d = b;

	*d++ = 'D', *d++ = ' ';
	d = hexn(d, reg, 5);
	*d++ = ' ';
	d += sprintf(d, "%u ", n);
	d = hexn(d, fnv(p, n), 16);
	*d++ = '\n';
	emit(b, (size_t)(d - b));
}

/* A buffer address the program gave the hardware: below 4 GB as it is (non-PIE statics, brk heap), else on the main
 * thread's stack. */
static u8 *resolve(u32 a, u32 n)
{
	uintptr_t s = (st_hint & ~(uintptr_t)0xFFFFFFFFu) | a;

	if (s >= st_lo && s + n <= st_hi)
		return (u8 *)s;
	s = (st_lo & ~(uintptr_t)0xFFFFFFFFu) | a;
	if (s >= st_lo && s + n <= st_hi)
		return (u8 *)s;
	if (a < 0x100000u)
		die("DMA from address %08x", a);
	return (u8 *)(uintptr_t)a;
}

/* ================================================================ GPIO, keys */

#define GP(p, r) REG(0x50000u + 0x40u * (p) + (r))
static u32 sr, latched;
static int ksess, kactive;
static u64 ks_t0, n_latch, key_reads;

struct keydef {
	u32 mask;
	int port, pin;
};
/* index i is key bit 1 << i: PREV, NEXT, SEL, REC. A key's column is selected when its mask's bits are low in the
 * latched word (src/mbl/keys.c's words). */
static const struct keydef KD_FM1[4] = { { 0x0801, 0, 8 }, { 0x1002, 0, 8 }, { 0x0400, 0, 6 }, { 0x0080, 0, 6 } };
static const struct keydef KD_SMK[4] = { { 0x00010000, 7, 8 }, { 0x10000000, 7, 8 }, { 0x01000000, 7, 8 },
					 { 0x08000000, 7, 8 } };

static int key_down(int k)
{
	int s = ksess - 1, listed = 0;
	u64 t = now - ks_t0;

	if (!kactive)
		return 0;
	for (int i = 0; i < 8 && S->press[i].key; i++) {
		const struct press *p = &S->press[i];
		if (p->sess != s)
			continue;
		listed = 1;
		if (p->key == k && t >= p->at * MS && t < (u64)(p->at + p->dur) * MS)
			return 1;
	}
	if (!listed && !S->no_default_press && k == K_SEL)
		return t >= 50 * MS && t < 150 * MS;
	return 0;
}

static u32 keys_low(int port)
{
	const struct keydef *kd = S->board == 1 ? KD_FM1 : S->board == 2 ? KD_SMK : 0;
	u32 low = 0;

	if (!kd)
		return 0;
	for (int i = 0; i < 4; i++)
		if (kd[i].port == port && (latched & kd[i].mask) == 0 && key_down(1 << i))
			low |= 1u << kd[i].pin;
	return low;
}

static void latch(void)
{
	n_latch++;
	if (S->board == 1) {
		latched = 0xFFFF0000u | (sr & 0xFFFF); /* 16 bits, MSB first */
	} else {
		u32 w = 0; /* 32 bits: least significant byte first, MSB first in each */
		for (int p = 0; p < 32; p++)
			if ((sr >> (31 - p)) & 1)
				w |= 1u << (8 * (p / 8) + 7 - p % 8);
		latched = w;
	}
}

static u32 gpio_in(int p)
{
	u32 out = GP(p, 0x00), dir = GP(p, 0x08), pu = GP(p, 0x10), v;
	u32 low = keys_low(p);

	v = (out & ~dir) | (dir & pu);
	if (low)
		key_reads++;
	return v & ~low;
}

/* this unit's flash's chip select: PD0, an output and low */
static int o_cs, o_op, o_n;
static u32 o_addr;
static u64 o_bytes;

static void o_cs_update(void)
{
	int a = !(GP(3, 0x00) & 1) && !(GP(3, 0x08) & 1);

	if (a && !o_cs)
		o_op = -1, o_n = 0, o_addr = 0;
	o_cs = a;
}

static void gpio_w(u32 a, u32 v)
{
	int p = (int)((a - 0x50000u) / 0x40u);
	u32 r = a & 0x3F, old = REG(a);

	REG(a) = v;
	if (p == 0 && r == 0x00 && S->board) {
		if (!(old & 8) && (v & 8))
			sr = sr << 1 | ((v >> 4) & 1); /* clock PA3 rising: data PA4 */
		if (!(old & 2) && (v & 2))
			latch(); /* latch PA1 rising */
	}
	if (p == 0 && r == 0x08) {
		if ((old & 8) && !(v & 8))
			kactive = 1, ksess++, ks_t0 = now;
		else if (!(old & 8) && (v & 8))
			kactive = 0;
	}
	if (p == 3 && (r == 0x00 || r == 0x08))
		o_cs_update();
}

/* ================================================================ SPI0: this unit's flash */

static u8 oflash[1 << 20];
static int spi0_done;
static u64 o_dma;

static u8 o_xfer(u8 b, int rx)
{
	if (!o_cs)
		return 0xFF;
	o_bytes++;
	if (o_op < 0) {
		if (!rx)
			o_op = b;
		return 0xFF;
	}
	switch (o_op) {
	case 0x03:
		if (o_n < 3) {
			o_addr = o_addr << 8 | b;
			o_n++;
			return 0xFF;
		}
		return rx ? oflash[o_addr++ & 0xFFFFF] : 0xFF;
	case 0x9F:
		if (!rx || o_n >= 3)
			return 0xFF;
		o_n++;
		return (u8)(S->own_jedec >> (24 - 8 * o_n));
	case 0x05:
	case 0x35:
		return S->own_jedec == 0xFFFFFF ? 0xFF : 0;
	case 0x4B:
		if (o_n++ < 4)
			return 0xFF;
		return rx ? (u8)(0xA0 + o_n) : 0xFF;
	default:
		return 0xFF;
	}
}

/* ================================================================ SPI1: the panel */

static u8 fb[240 * 240 * 2];
static int p_cmd = -1, p_n, p_on, half;
static u8 p_par[4];
static u32 xs, xe = 239, ys, ye = 239, px, py;
static u64 p_cmds, p_bytes;

static void panel_byte(u8 b)
{
	int cs, dc;

	/* chip select: an output, low */
	if (S->board == 1)
		cs = ((GP(2, 0) | GP(2, 8)) >> 7) & 1, dc = (GP(2, 0) >> 8) & 1; /* CS PC7, DC PC8 */
	else if (S->board == 2)
		cs = ((GP(2, 0) | GP(2, 8)) >> 8) & 1, dc = (GP(1, 0) >> 5) & 1; /* CS PC8, DC PB5 */
	else
		return;
	if (cs)
		return;
	if (!dc) {
		p_cmd = b, p_n = 0, p_cmds++;
		if (b == 0x2C)
			px = xs, py = ys, half = 0;
		if (b == 0x29)
			p_on = 1;
		if (b == 0x28)
			p_on = 0;
		return;
	}
	if (p_cmd == 0x2A || p_cmd == 0x2B) {
		if (p_n < 4)
			p_par[p_n++] = b;
		if (p_n == 4) {
			u32 s = (u32)p_par[0] << 8 | p_par[1], e = (u32)p_par[2] << 8 | p_par[3];
			if (p_cmd == 0x2A)
				xs = s, xe = e;
			else
				ys = s, ye = e;
			p_n++;
		}
	} else if (p_cmd == 0x2C) {
		if (px < 240 && py < 240)
			fb[(py * 240 + px) * 2 + half] = b;
		p_bytes++;
		if (half && ++px > xe) {
			px = xs;
			if (++py > ye)
				py = ys;
		}
		half ^= 1;
	}
}

/* ================================================================ the other unit at the USB pads */

enum { RS_OFF, RS_KEY, RS_ACKLOW, RS_FALSE, RS_CAL, RS_DROP, RS_ATTACHED, RS_RUNNING, RS_GONE };
static const char *const RS_NAME[] = { "off", "key", "ack", "false-ack", "cal", "dropped", "attached", "running",
				       "gone" };
static int rs, false_done, pads_used, have_pulse;
static u64 on_t, until, first_pulse, drop_t, attach_t, ack_t, klast;
static u32 kword;
static int kbits;
static u64 k_good, k_bad, k_false, k_pulses;

static void rom_update(void)
{
	for (;;)
		switch (rs) {
		case RS_OFF:
			if (on_t && now >= on_t) {
				rs = RS_KEY;
				continue;
			}
			return;
		case RS_ACKLOW:
			if (now >= until) {
				rs = RS_CAL;
				continue;
			}
			return;
		case RS_FALSE:
			if (now >= until) {
				rs = RS_KEY;
				continue;
			}
			return;
		case RS_CAL:
			if (have_pulse && now >= first_pulse + S->cal_ms * MS) {
				drop_t = first_pulse + S->cal_ms * MS;
				attach_t = drop_t + S->attach_ms * MS;
				rs = RS_DROP;
				continue;
			}
			return;
		case RS_DROP:
			if (!S->no_attach && now >= attach_t) {
				rs = RS_ATTACHED;
				continue;
			}
			return;
		default:
			return;
		}
}

/* strongest first: the other unit driving, this unit driving, the other unit's 1.5k pull-up, our pulls */
static int line(int strong, int drive, int val, int devpu, int pu)
{
	if (strong >= 0)
		return strong;
	if (drive)
		return val;
	return devpu || pu;
}

static void levels(int *dp, int *dm)
{
	u32 u = REG(0x51000);
	int gpio = (u & 0x900) != 0, sdm = -1, pdp = 0;

	rom_update();
	switch (rs) {
	case RS_OFF:
		if (!S->no_clamp)
			sdm = 0; /* unpowered: clamps D- */
		break;
	case RS_ACKLOW:
	case RS_FALSE:
		sdm = 0;
		break;
	case RS_CAL:
	case RS_ATTACHED:
	case RS_RUNNING:
		pdp = 1;
		break;
	default:
		break;
	}
	*dp = line(-1, gpio && !(u & 4), u & 1, pdp, (u >> 6) & 1);
	*dm = line(sdm, gpio && !(u & 8), (u >> 1) & 1, 0, (u >> 7) & 1);
}

static void key_bit(int bit)
{
	if (kbits && now - klast > 150 * US)
		kbits = 0; /* a gap: a new packet */
	klast = now;
	kword = (kword << 1 | (u32)bit) & 0xFFFF;
	if (++kbits < 16)
		return;
	kbits = 0;
	if (kword != 0x16EF) {
		k_bad++;
		return;
	}
	k_good++;
	if (S->false_at && (int)k_good == S->false_at && !false_done) {
		false_done = 1, k_false++;
		rs = RS_FALSE, until = now + S->false_us * US;
	} else if ((int)k_good >= S->need) {
		ack_t = now;
		if (S->dp_answer)
			rs = RS_CAL;
		else
			rs = RS_ACKLOW, until = now + S->ack_us * US;
	}
}

static void pads_w(u32 v)
{
	int dp0, dm0, dp1, dm1;

	levels(&dp0, &dm0);
	REG(0x51000) = v;
	levels(&dp1, &dm1);
	pads_used = 1;
	if (!dp0 && dp1 && rs == RS_KEY)
		key_bit(dm1); /* D+ rising: latch D- */
	if (dp0 && !dp1 && rs == RS_CAL) {
		k_pulses++;
		if (!have_pulse)
			have_pulse = 1, first_pulse = now;
	}
}

static u32 pads_in(void)
{
	u32 u = REG(0x51000);
	int dp, dm;

	levels(&dp, &dm);
	return (u32)(dp && (u & 0x200)) | (u32)(dm && (u & 0x400)) << 1;
}

/* ================================================================ USB0 and the device */

static u8 sie[16], epr[4][16]; /* SIE 0x00-0x0F; 0x10-0x1F per INDEX */
#define CSR0 epr[0][1]
#define COUNT0 epr[0][6]
#define TXCSR epr[1][1]
#define RXCSR epr[1][4]
#define RXC1 epr[1][6]
#define RXC2 epr[1][7]
#define TXTYPE epr[1][8]
#define RXTYPE epr[1][10]
static u64 sof_clr, sof_clears;
static int dev_addr, dev_cfg, in_halt, out_halt;
static u64 n_setup, n_out, n_in, n_cmd, n_read, n_write, n_crc_bad, n_bad_cbw, n_fault, n_crc_at, n_crc, n_sie_rd;

enum { M_ROM, M_LOADER, M_OTHER, M_RESTARTED, M_CRASHED };
static const char *const M_NAME[] = { "rom", "loader", "other-loader", "restarted", "crashed" };
static int mode, loader_ok = -1, gone_after;

static u8 dflash[1 << 20], dflash0[1 << 20], ram[0x80000];
static u32 erased_b[256], erased_s[256], programmed[256];
static u8 bootbin[0x4000];

static int dev_on(void)
{
	rom_update();
	return S->dev && rs == RS_ATTACHED;
}

static int dev_here(void) { return dev_on() && sie[0] == dev_addr && !(sie[1] & 8); }

/* ---- control ---- */

static u8 c_buf[64];
static u32 c_len, c_pos, c_val, c_idx;
static int c_stall, c_act, ep0_err;
enum { A_NONE, A_ADDR, A_CFG, A_CLEAR, A_RESET };

static void bot_reset(void);

static void ctl_setup(const u8 *p)
{
	u32 type = p[0], req = p[1], val = p[2] | p[3] << 8, idx = p[4] | p[5] << 8, len = p[6] | p[7] << 8;
	u8 dd[18] = { 18, 1, 0x10, 0x01, 0, 0, 0, 64, (u8)S->vid, (u8)(S->vid >> 8), (u8)S->pid, (u8)(S->pid >> 8),
		      0x00, 0x01, 1, 2, 3, 1 };
	static const u8 cd[32] = { 9, 2, 32, 0, 1, 1, 0, 0x80, 50, 9, 4, 0, 0, 2, 8, 6, 0x50, 0,
				   7, 5, 0x81, 2, 64, 0, 0, 7, 5, 0x02, 2, 64, 0, 0 };

	n_setup++;
	c_stall = 0, c_len = c_pos = 0, c_act = A_NONE, c_val = val, c_idx = idx;
	if (type == 0x80 && req == 6 && (val >> 8) == 1) {
		c_len = len < 18 ? len : 18;
		memcpy(c_buf, dd, c_len);
	} else if (type == 0x80 && req == 6 && (val >> 8) == 2) {
		c_len = len < 32 ? len : 32;
		memcpy(c_buf, cd, c_len);
	} else if (type == 0x00 && req == 5) {
		c_act = A_ADDR;
	} else if (type == 0x00 && req == 9) {
		c_act = A_CFG;
	} else if (type == 0x02 && req == 1 && val == 0) {
		c_act = A_CLEAR;
	} else if (type == 0x21 && req == 0xFF) {
		c_act = A_RESET;
	} else if (type == 0xA1 && req == 0xFE) {
		c_buf[0] = 0, c_len = len ? 1 : 0;
	} else {
		c_stall = 1;
	}
}

static int ctl_status_in(void)
{
	if (c_stall)
		return -1;
	if (c_act == A_ADDR)
		dev_addr = c_val & 0x7F;
	else if (c_act == A_CFG)
		dev_cfg = c_val & 0xFF;
	else if (c_act == A_CLEAR && c_idx == 0x81)
		in_halt = 0;
	else if (c_act == A_CLEAR && c_idx == 0x02)
		out_halt = 0;
	else if (c_act == A_RESET)
		bot_reset();
	c_act = A_NONE;
	return 0;
}

static void csr0_w(u32 v)
{
	u8 *e = resolve(REG(0x11818), 64);

	if (v & 0x02) {
		if (!dev_here() || (S->noisy && n_setup == 2 && (v & 8) && !ep0_err++ && ++n_fault)) {
			CSR0 = 0x10; /* no handshake: error after three tries */
			return;
		}
		if (v & 0x08) {
			u32 n = REG(0x11808);
			log_dma(0x11818, e, n);
			ctl_setup(e);
			CSR0 = 0;
		} else if (v & 0x40) {
			CSR0 = 0; /* status OUT */
		} else {
			log_dma(0x11818, e, REG(0x11808));
			CSR0 = c_stall ? 0x04 : 0;
		}
		return;
	}
	if (v & 0x20) {
		if (!dev_here()) {
			CSR0 = 0x10;
			return;
		}
		if (v & 0x40) {
			if (ctl_status_in() < 0) {
				CSR0 = 0x04;
			} else {
				COUNT0 = 0;
				CSR0 = 0x01;
			}
		} else if (c_stall) {
			CSR0 = 0x04;
		} else {
			u32 n = c_len - c_pos < 64 ? c_len - c_pos : 64;
			memcpy(e, c_buf + c_pos, n);
			c_pos += n;
			COUNT0 = (u8)n;
			CSR0 = 0x01;
			log_dma(0x11818, e, n);
		}
		return;
	}
	CSR0 = (u8)(v & ~0x15u);
}

/* ---- bulk-only mass storage ---- */

static u8 inq[20000], outb[70000], cbw[31];
static u32 in_len, in_pos, in_dend, out_need, out_got, tag, dlen;
static int stage_out;

static void bot_reset(void)
{
	in_len = in_pos = in_dend = 0;
	stage_out = 0;
	in_halt = out_halt = 0;
}

static void q_csw(u32 residue, u8 status)
{
	u8 *c = inq + in_len;

	st32(c, 0x53425355u);
	st32(c + 4, tag);
	st32(c + 8, residue);
	c[12] = status;
	in_len += 13;
}

static void reply(const u8 *d, u32 n, u8 status)
{
	if (n > dlen)
		n = dlen;
	if (n)
		memcpy(inq, d, n); /* (d is null for a reply with no data) */
	in_pos = 0, in_dend = in_len = n;
	q_csw(dlen - n, status);
}

static void reply_csw_only(u8 status, int stall)
{
	in_len = in_pos = in_dend = 0; /* with no stall: the status block comes in place of the data */
	in_halt = stall;
	q_csw(dlen, status);
}

static void refuse(void)
{
	static const u8 z[16];

	if (dlen && !(cbw[12] & 0x80)) {
		stage_out = 2, out_need = dlen, out_got = 0; /* take the data, then fail */
		return;
	}
	if (!dlen) {
		reply(0, 0, 1);
		return;
	}
	switch (S->refuse) {
	case REF_EARLY:
		reply_csw_only(1, 0);
		break;
	case REF_ODD:
		reply(z, 16, 0); /* an answer that isn't the opcode */
		break;
	default:
		reply_csw_only(1, 1);
		break;
	}
}

static void ans16(const u8 *cb, u8 *r)
{
	memset(r, 0, 16);
	r[0] = cb[0], r[1] = cb[1];
}

static int sha_is(const u8 *d, u32 n, const char *want)
{
	char h[65];

	sha_of(d, n, h);
	return !strcmp(h, want);
}

static void data_out_done(void)
{
	const u8 *cb = cbw + 15;
	u32 a = bd32(cb + 2), n = (u32)cb[6] << 8 | cb[7];
	u16 crc = (u16)(cb[9] | cb[10] << 8);
	u8 st = 0;

	if (stage_out == 2) {
		stage_out = 0;
		reply(0, 0, 1);
		return;
	}
	stage_out = 0;
	if (n != out_got || crc16(0, outb, n) != crc) {
		n_crc_bad++;
		st = 1;
	} else if (cb[0] == 0xFB && cb[1] == 0x06) {
		if (a < 0x01C00000u || a + n > 0x01C80000u)
			st = 1;
		else
			crc_cipher(outb, ram + (a - 0x01C00000u), n, 0xFFFFFFFFu);
	} else {
		u32 skip = ~0u;
		n_write++;
		if ((S->f_write_flaky && (int)n_write == S->f_write_flaky) || S->f_write_bad)
			for (u32 i = 0; i < n && skip == ~0u; i++)
				if (outb[i] != 0xFF && dflash[(a + i) & 0xFFFFF] != outb[i])
					skip = i, n_fault++; /* one byte stays unprogrammed */
		for (u32 i = 0; i < n; i++)
			if (i != skip)
				dflash[(a + i) & 0xFFFFF] &= outb[i];
		for (u32 s = a >> 12; s <= (a + n - 1) >> 12 && s < 256; s++)
			programmed[s]++;
	}
	reply(0, 0, st);
}

static void command(void)
{
	const u8 *cb = cbw + 15;
	u32 op = (u32)cb[0] << 8 | cb[1], a = bd32(cb + 2);
	u8 r[36];

	n_cmd++;
	if (cbw[14] == 6 && cb[0] == 0x12) {
		static const u8 inquiry[36] = { 0, 0x80, 2, 2, 31, 0, 0, 0, 'J', 'I', 'E', 'L', 'I', ' ', ' ', ' ',
						'U', 'B', 'O', 'O', 'T', '1', '.', '0', '0', ' ', ' ', ' ', ' ',
						' ', ' ', ' ', '1', '.', '0', '0' };
		reply(inquiry, 36, 0);
		return;
	}
	if ((op == 0xFB06 || op == 0xFB04) && !(cbw[12] & 0x80) && dlen &&
	    (op == 0xFB06 ? mode != M_OTHER : mode == M_LOADER)) {
		stage_out = 1, out_need = dlen, out_got = 0;
		return;
	}
	if (op == 0xFB08 && mode != M_OTHER) {
		ans16(cb, r);
		reply(r, 16, 0);
		if (a == 0x01C20000u && sha_is(ram + 0x20000, MFLR_LEN, MFLR_SHA)) {
			mode = M_LOADER, loader_ok = 1;
		} else if (a == 0x01C28000u && !memcmp(ram + 0x28000, RESTART_PROG, sizeof RESTART_PROG)) {
			mode = M_RESTARTED, gone_after = 1;
		} else {
			if (a == 0x01C20000u)
				loader_ok = 0;
			mode = M_CRASHED, gone_after = 1;
		}
		return;
	}
	if (mode == M_OTHER && op == 0xFC0A) {
		ans16(cb, r);
		st32(r + 4, S->dev_jedec);
		reply(r, 16, 0);
		return;
	}
	if (mode != M_LOADER) {
		refuse();
		return;
	}
	ans16(cb, r);
	switch (op) {
	case 0xFC40: /* IDENTIFY */
		memcpy(r + 2, "MFLR1.3", 7);
		bs32(r + 10, 0x01C20000u);
		r[15] = 96;
		break;
	case 0xFC41: { /* CRC32 */
		u32 n = bd32(cb + 6);
		if (S->f_crc_short && ++n_crc == (u64)S->f_crc_short) {
			n_fault++;
			bs32(r + 2, crc32z(dflash + a, n));
			reply(r, 4, 0); /* half the CRC: the C took the rest for zeros */
			return;
		}
		if (S->f_crc_fail && a == S->f_crc_fail - 1 && ++n_crc_at >= (u64)S->f_crc_from) {
			n_fault++;
			reply(r, 16, 2); /* phase error */
			return;
		}
		bs32(r + 2, a < (1u << 20) && n <= (1u << 20) - a ? crc32z(dflash + a, n) : 0);
		break;
	}
	case 0xFC42: /* UNIQUE_ID */
		for (int i = 0; i < 8; i++)
			r[2 + i] = (u8)(0x11 * (i + 1));
		break;
	case 0xFC09: { /* READ_KEY: 0x980F, as jl.c decodes it */
		u8 plain[2] = { 0x0F, 0x98 }, enc[2];
		crc_cipher(plain, enc, 2, 0xFFFFFFFFu);
		r[7] = enc[0], r[6] = enc[1];
		break;
	}
	case 0xFC0A: /* ONLINE_DEVICE: the flash ID */
		st32(r + 4, S->dev_jedec);
		break;
	case 0xFC14: /* USB_BUFFER_SIZE */
		bs32(r + 2, S->io);
		break;
	case 0xFD05: { /* READ_FLASH */
		u32 n = (u32)cb[6] << 8 | cb[7];
		n_read++;
		if (n > sizeof inq - 13 || a >= (1u << 20) || n > (1u << 20) - a) {
			refuse();
			return;
		}
		if (S->f_read_stall && (int)n_read == S->f_read_stall) {
			n_fault++;
			reply_csw_only(1, 1);
			return;
		}
		if (S->f_read_early && (int)n_read == S->f_read_early) {
			n_fault++;
			reply_csw_only(1, 0);
			return;
		}
		if (S->f_read_fail && a <= S->f_read_fail - 1 && S->f_read_fail - 1 < a + n) {
			n_fault++;
			reply(dflash + a, n, 2); /* phase error */
			return;
		}
		reply(dflash + a, n, 0);
		return;
	}
	case 0xFB00: /* ERASE_BLOCK */
	case 0xFB01: { /* ERASE_SECTOR */
		u32 sz = op == 0xFB00 ? 0x10000u : 0x1000u;
		a &= ~(sz - 1) & 0xFFFFF;
		memset(dflash + a, 0xFF, sz);
		for (u32 s = a >> 12; s < (a + sz) >> 12; s++)
			op == 0xFB00 ? erased_b[s]++ : erased_s[s]++;
		break;
	}
	default:
		refuse();
		return;
	}
	reply(r, 16, 0);
}

static void bot_out(const u8 *p, u32 n)
{
	if (stage_out) {
		u32 k = out_got + n <= sizeof outb ? n : 0;
		memcpy(outb + out_got, p, k);
		out_got += n;
		if (out_got >= out_need)
			data_out_done();
		return;
	}
	if (n != 31 || ld32(p) != 0x43425355u) {
		n_bad_cbw++;
		in_halt = out_halt = 1;
		return;
	}
	memcpy(cbw, p, 31);
	tag = ld32(p + 4);
	dlen = ld32(p + 8);
	command();
}

static int bot_in(u8 *p)
{
	u32 n;

	if (in_pos >= in_len)
		return -1;
	n = in_pos < in_dend ? in_dend - in_pos : in_len - in_pos;
	if (n > 64)
		n = 64;
	memcpy(p, inq + in_pos, n);
	in_pos += n;
	if (in_pos == in_len) {
		in_len = in_pos = in_dend = 0;
		if (gone_after)
			rs = RS_GONE; /* restarted, or crashed: off the bus */
	}
	return (int)n;
}

static int tx_late; /* 1: a packet is still to go, and the next TXCSR1 read times out; 2: it goes at the next one */

static void tx_send(void)
{
	u32 n = REG(0x1180C) & 0x3FF;
	u8 *p = resolve(REG(0x1181C), n);
	log_dma(0x1181C, p, n);
	bot_out(p, n);
	TXCSR = 0;
	tx_late = 0;
}

static void tx_w(u32 v)
{
	if (!(v & 0x01)) {
		TXCSR = (u8)(v & ~0x49u); /* flush and toggle bits clear themselves */
		return;
	}
	if (!dev_here() || (TXTYPE & 0x0F) != 2 || !dev_cfg) {
		TXCSR = 0x04;
		return;
	}
	if (out_halt) {
		TXCSR = 0x20;
		return;
	}
	if (S->noisy && ++n_out % 37 == 0) {
		n_fault++;
		TXCSR = (n_out / 37) & 1 ? 0x84 : 0x04; /* not delivered: sent again */
		return;
	}
	if (tx_late)
		tx_send(); /* written over while it was still going: it goes as the buffer is now */
	if (S->f_sie_stuck && ++n_sie_rd == (u64)S->f_sie_stuck) {
		n_fault++;
		tx_late = 1;
		TXCSR = 0x01; /* still going */
		return;
	}
	tx_send();
}

static void rx_poll(void)
{
	u8 buf[64];
	int n;

	if (!(RXCSR & 0x20))
		return;
	if (!dev_here() || (RXTYPE & 0x0F) != 1 || !dev_cfg) {
		RXCSR = 0x04;
		return;
	}
	if (in_halt) {
		RXCSR = 0x40;
		return;
	}
	if (in_pos >= in_len)
		return; /* NAK: the request stays */
	if (S->noisy && ++n_in % 41 == 0) {
		n_fault++;
		RXCSR = 0x08;
		return;
	}
	n = bot_in(buf);
	memcpy(resolve(REG(0x11820), 64), buf, (size_t)n);
	log_dma(0x11820, buf, (u32)n);
	RXC1 = (u8)n, RXC2 = (u8)(n >> 8);
	RXCSR = 0x01;
}

static void rx_w(u32 v)
{
	if (v & 0x20) {
		RXCSR = 0x20;
		rx_poll();
		return;
	}
	RXCSR = (u8)(v & ~0xB1u);
}

static u32 sie_read(u32 r)
{
	if (r == 15) {
		u32 d = sie[15] & 3;
		if (d & 1) {
			d |= 0x04 | 0x18; /* host mode, VBUS valid */
			if (dev_on())
				d |= 0x40; /* a full-speed device */
		}
		return d;
	}
	if (r >= 2 && r <= 6)
		return 0; /* interrupt flags: polled code doesn't use them */
	if (r < 16)
		return sie[r];
	if (r < 32) {
		int i = sie[14] & 3;
		if (i == 1 && r == 0x14)
			rx_poll();
		return epr[i][r - 16];
	}
	return 0;
}

static void sie_write(u32 r, u32 v)
{
	if (r < 16) {
		if (r == 1 && (sie[1] & 8) && !(v & 8)) { /* bus reset ends */
			dev_addr = 0, dev_cfg = 0;
			bot_reset();
		}
		sie[r] = (u8)(r == 14 ? v & 15 : v);
		return;
	}
	if (r < 32) {
		int i = sie[14] & 3;
		if (r == 0x11 && i == 0)
			csr0_w(v);
		else if (r == 0x11 && i == 1)
			tx_w(v);
		else if (r == 0x14 && i == 1)
			rx_w(v);
		else
			epr[i][r - 16] = (u8)v;
	}
}

static u32 con0_r(void)
{
	int dp, dm, pend = 0;

	levels(&dp, &dm);
	if (S->sof)
		pend = now / S->sof > sof_clr;
	return (REG(0x11800) & ~0x33000u) | (u32)pend << 13 | (u32)dm << 16 | (u32)dp << 17;
}

static void con0_w(u32 v)
{
	if ((v & 0x1000) && S->sof)
		sof_clr = now / S->sof, sof_clears++;
	REG(0x11800) = v & ~0x33000u;
}

static void con1_w(u32 v)
{
	if (!(REG(0x11800) & 4)) {
		REG(0x11804) = v & ~0x8000u; /* SIE in reset: no answer */
		return;
	}
	if (v & 0x4000) {
		if (((v >> 8) & 0x3F) == 17 && sie[14] == 1 && tx_late) {
			if (tx_late == 1) {
				tx_late = 2;
				REG(0x11804) = v & ~0x8000u; /* no answer: the read times out */
				return;
			}
			tx_send();
		}
		REG(0x11804) = (v & 0xFFFF00u) | sie_read((v >> 8) & 0x3F) | 0x8000u;
	} else {
		sie_write((v >> 8) & 0xFF, v & 0xFF);
		REG(0x11804) = (v & 0xFFFFFFu) | 0x8000u;
	}
}

/* ================================================================ register dispatch */

static u32 rd(u32 a)
{
	switch (a) {
	case 0x10404:
		return (u32)now;
	case 0x13E08:
		return REG(a) & ~2u; /* P33 never busy */
	case 0x11800:
		return con0_r();
	case 0x11C00:
		return (REG(a) & ~0x8000u) | (u32)spi0_done << 15;
	case 0x11D00:
		return REG(a) | 0x8000u;
	case 0x51004:
		return pads_in();
	}
	if (a >= 0x50000u && a < 0x50200u && (a & 0x3F) == 0x04)
		return gpio_in((int)((a - 0x50000u) / 0x40u));
	return REG(a);
}

static void wr(u32 a, u32 v)
{
	switch (a) {
	case 0x11800:
		con0_w(v);
		return;
	case 0x11804:
		con1_w(v);
		return;
	case 0x11C00:
		if (v & 0x4000)
			spi0_done = 0;
		REG(a) = v & ~0xC000u;
		return;
	case 0x11C08:
		REG(a) = o_xfer((u8)v, (REG(0x11C00) >> 12) & 1);
		spi0_done = 1;
		return;
	case 0x11C10: {
		u32 n = v & 0xFFFF, rx = (REG(0x11C00) >> 12) & 1;
		u8 *p = resolve(REG(0x11C0C), n);
		for (u32 i = 0; i < n; i++)
			if (rx)
				p[i] = o_xfer(0xFF, 1);
			else
				o_xfer(p[i], 0);
		o_dma += n;
		log_dma(0x11C10, p, n);
		REG(a) = v;
		spi0_done = 1;
		return;
	}
	case 0x11D08:
		REG(a) = v;
		panel_byte((u8)v);
		return;
	case 0x11D10: {
		u32 n = v & 0xFFFF;
		u8 *p = resolve(REG(0x11D0C), n);
		for (u32 i = 0; i < n; i++)
			panel_byte(p[i]);
		log_dma(0x11D10, p, n);
		REG(a) = v;
		return;
	}
	case 0x51000:
		pads_w(v);
		return;
	}
	if (a >= 0x50000u && a < 0x50200u && (a & 0x3F) < 0x28) {
		gpio_w(a, v);
		return;
	}
	REG(a) = v;
}

static u32 reg_access(u32 a, int w, u32 v)
{
	if (!running)
		die("access to %05x after chip_finish", a);
	acc++;
	now = (u64)S->t0 + acc * S->tpa;
	if (!w) {
		v = rd(a);
		log_rw('R', a, v);
		return v;
	}
	log_rw('W', a, v);
	wr(a, v);
	return 0;
}

/* ================================================================ faults */

static const int GR[16] = { REG_RAX, REG_RCX, REG_RDX, REG_RBX, REG_RSP, REG_RBP, REG_RSI, REG_RDI,
			    REG_R8,  REG_R9,  REG_R10, REG_R11, REG_R12, REG_R13, REG_R14, REG_R15 };
static uintptr_t step_page;
static u32 step_addr;
static int step_write;

/* mov r32, m32 (8B) / mov m32, r32 (89) / mov m32, imm32 (C7 /0): its length, or 0 */
static int decode(const u8 *ip, int *load, int *reg, int *imm, u32 *val)
{
	const u8 *p = ip;
	int rex = 0, op, m, mod, rm, disp = 0;

	if ((*p & 0xF0) == 0x40)
		rex = *p++;
	if (rex & 8)
		return 0;
	op = *p++;
	if (op != 0x8B && op != 0x89 && op != 0xC7)
		return 0;
	m = *p++;
	mod = m >> 6, rm = m & 7;
	if (mod == 3 || (op == 0xC7 && ((m >> 3) & 7)))
		return 0;
	if (rm == 4) {
		int sib = *p++;
		if ((sib & 7) == 5 && mod == 0)
			disp = 4;
	} else if (rm == 5 && mod == 0) {
		disp = 4;
	}
	if (mod == 1)
		disp = 1;
	else if (mod == 2)
		disp = 4;
	p += disp;
	*load = op == 0x8B;
	*reg = ((m >> 3) & 7) | (rex & 4 ? 8 : 0);
	*imm = op == 0xC7;
	if (*imm) {
		memcpy(val, p, 4);
		p += 4;
	}
	return (int)(p - ip);
}

/* An instruction that reads its memory operand as well as writing it, such as `or [m32], imm`: a Rust
 * read-modify-write (`Reg::set`) often compiles to one. Its write fault is an access of each kind, read first. */
static int read_modify_write(const u8 *p)
{
	int op, r;

	while (*p == 0x66 || *p == 0xF0)
		p++;
	if ((*p & 0xF0) == 0x40)
		p++;
	op = *p++;
	if (op == 0x0F)
		return *p == 0xAB || *p == 0xB3 || *p == 0xBB || *p == 0xBA; /* bts, btr, btc, bt* imm */
	r = (*p >> 3) & 7;
	switch (op) {
	case 0x01: case 0x09: case 0x11: case 0x19: case 0x21: case 0x29: case 0x31: /* add or adc sbb and sub xor */
	case 0x81: case 0x83: /* the same, with an immediate */
	case 0xC1: case 0xD1: case 0xD3: /* shifts */
		return 1;
	case 0xF7:
		return r == 2 || r == 3; /* not, neg */
	case 0xFF:
		return r == 0 || r == 1; /* inc, dec */
	}
	return 0;
}

static void on_segv(int sig, siginfo_t *si, void *ctx)
{
	ucontext_t *uc = ctx;
	greg_t *g = uc->uc_mcontext.gregs;
	uintptr_t a = (uintptr_t)si->si_addr;
	int load, reg, imm, len;
	u32 val = 0;

	(void)sig;
	if (a < LO || a >= HI || !running) {
		fprintf(stderr, "chip: real fault at %lx\n", (unsigned long)a);
		signal(SIGSEGV, SIG_DFL); /* a real fault: let it happen */
		return;
	}
	len = decode((const u8 *)g[REG_RIP], &load, &reg, &imm, &val);
	if (len && !(a & 3)) {
		if (load)
			g[GR[reg]] = (greg_t)reg_access((u32)a, 0, 0);
		else
			reg_access((u32)a, 1, imm ? val : (u32)g[GR[reg]]);
		g[REG_RIP] += len;
		return;
	}
	/* anything else: single-stepped on the real page */
	fallbacks++;
	step_addr = (u32)a & ~3u;
	step_write = (g[REG_ERR] & 2) != 0;
	step_page = a & ~(uintptr_t)0xFFF;
	mprotect((void *)step_page, 4096, PROT_READ | PROT_WRITE);
	*(volatile u32 *)(uintptr_t)step_addr =
		step_write && !read_modify_write((const u8 *)g[REG_RIP]) ? REG(step_addr) : reg_access(step_addr, 0, 0);
	g[REG_EFL] |= 0x100;
}

static void on_trap(int sig, siginfo_t *si, void *ctx)
{
	ucontext_t *uc = ctx;

	(void)sig, (void)si;
	if (!step_page)
		return;
	uc->uc_mcontext.gregs[REG_EFL] &= ~0x100;
	if (step_write)
		reg_access(step_addr, 1, *(volatile u32 *)(uintptr_t)step_addr);
	mprotect((void *)step_page, 4096, PROT_NONE);
	step_page = 0;
}

/* ================================================================ set-up */

static u64 rng;

static u8 rnd(void)
{
	rng ^= rng << 13, rng ^= rng >> 7, rng ^= rng << 17;
	return (u8)(rng >> 24);
}

static void random_fill(u8 *d, u32 lo, u32 hi)
{
	for (u32 i = lo; i < hi; i++)
		d[i] = rnd();
}

static const char *rescue_dir(char *buf, size_t n)
{
	const char *e = getenv("CHIP_RESCUE"), *f = __FILE__, *s = strrchr(f, '/');

	if (e)
		return e;
	snprintf(buf, n, "%.*s/../../build/c-ref/rescue", s ? (int)(s - f) : 1, s ? f : ".");
	return buf;
}

static void load_boot(int board)
{
	char dir[4096], path[4200], h[65];
	FILE *f;

	snprintf(path, sizeof path, "%s/%s/boot.bin", rescue_dir(dir, sizeof dir), board == 2 ? "smk37" : "fm1");
	f = fopen(path, "rb");
	if (!f || fread(bootbin, 1, sizeof bootbin, f) != sizeof bootbin)
		die("can't read %s (env CHIP_RESCUE: the rescue/ directory)", path);
	fclose(f);
	sha_of(bootbin, sizeof bootbin, h);
	if (strcmp(h, BOOT_SHA[board]))
		fprintf(stderr, "chip: warning: %s is not bd5ea96's\n", path);
}

static void dev_flash_init(void)
{
	int fl = S->flash;

	rng = 0x9E3779B97F4A7C15ull ^ (u64)fl * 0x1234567;
	memset(dflash, 0xFF, sizeof dflash);
	if (fl == FL_OLD_FM1)
		random_fill(dflash, 0, 0x60000);
	if (fl == FL_OLD_SMK)
		random_fill(dflash, 0, 0x80000);
	if (fl == FL_OLD_SMK || fl == FL_FIXED_SMK) {
		random_fill(dflash, 0xC0000, 0xC2000); /* pairing, calibration */
		memcpy(dflash + 0xC1224, "trimdat", 8);
		if (!S->no_trimcopy)
			memcpy(dflash + 0xFF000, "TRIMCOPY", 8);
		random_fill(dflash, 0xFF008, 0xFF200);
	}
	if (fl == FL_OLD_FM1 || fl == FL_FIXED_FM1)
		random_fill(dflash, 0xFF000, 0xFF200); /* data above 0xFF000 */
	if (fl == FL_FIXED_FM1 || fl == FL_FIXED_SMK) {
		load_boot(fl == FL_FIXED_SMK ? 2 : 1);
		memcpy(dflash, bootbin, sizeof bootbin);
	}
	memcpy(dflash0, dflash, sizeof dflash);
}

static void find_stack(void)
{
	FILE *f = fopen("/proc/self/maps", "r");
	char line[512];
	unsigned long lo, hi;

	while (f && fgets(line, sizeof line, f))
		if (strstr(line, "[stack]") && sscanf(line, "%lx-%lx", &lo, &hi) == 2)
			st_lo = lo, st_hi = hi;
	if (f)
		fclose(f);
}

void chip_start(const char *name)
{
	volatile int here = 0;
	struct sigaction sa;
	const char *t;

	S = 0;
	for (size_t i = 0; i < sizeof SCEN / sizeof SCEN[0]; i++)
		if (!strcmp(SCEN[i].name, name))
			S = &SCEN[i];
	if (!S) {
		fprintf(stderr, "chip: no scenario %s\n", name);
		exit(2);
	}
	st_hint = (uintptr_t)&here;
	find_stack();
	if (!st_lo)
		st_lo = st_hint - (8u << 20), st_hi = st_hint + 4096;

	memset(R, 0, sizeof R);
	for (int p = 0; p < 8; p++)
		GP(p, 0x08) = 0xFFFF; /* every pin an input */
	acc = now = 0;
	sum_len = 0;
	sum_add("");
	sha_init(&th);
	sr = 0, latched = ~0u, ksess = kactive = 0, n_latch = key_reads = 0;
	o_cs = 0, o_op = -1, o_bytes = o_dma = 0, spi0_done = 0;
	memset(oflash, 0xFF, sizeof oflash);
	if (S->own_marks & 1)
		memcpy(oflash + 0xC1224, "trimdat", 8);
	if (S->own_marks & 2)
		memcpy(oflash + 0xFF000, "TRIMCOPY", 8);
	memset(fb, 0, sizeof fb);
	p_cmd = -1, p_on = 0, p_cmds = p_bytes = 0, xs = ys = 0, xe = ye = 239;
	rs = S->rom == ROM_RUNNING ? RS_RUNNING : S->rom == ROM_ATTACHED ? (S->no_attach ? RS_DROP : RS_ATTACHED) : RS_OFF;
	if (S->rom == ROM_ATTACHED && S->on_ms)
		rs = RS_OFF; /* the whole flow: woken first */
	on_t = S->on_ms ? S->t0 + S->on_ms * MS : 0, false_done = 0, pads_used = 0, have_pulse = 0, kbits = 0;
	attach_t = ~0ull;
	k_good = k_bad = k_false = k_pulses = 0, ack_t = 0, drop_t = 0;
	sof_clr = 0, sof_clears = 0;
	memset(sie, 0, sizeof sie);
	memset(epr, 0, sizeof epr);
	dev_addr = dev_cfg = in_halt = out_halt = ep0_err = 0;
	n_setup = n_out = n_in = n_cmd = n_read = n_write = n_crc_bad = n_bad_cbw = n_fault = n_crc_at = n_crc = n_sie_rd = 0;
	tx_late = 0;
	mode = S->other_loader ? M_OTHER : M_ROM, loader_ok = -1, gone_after = 0;
	bot_reset();
	memset(ram, 0, sizeof ram);
	memset(erased_b, 0, sizeof erased_b), memset(erased_s, 0, sizeof erased_s);
	memset(programmed, 0, sizeof programmed);
	if (S->dev)
		dev_flash_init();
	if (S->expect == EXP_FIXED)
		load_boot(S->dev_board);
	fallbacks = 0;

	t = getenv("CHIP_TRACE");
	tf = t ? fopen(t, "w") : 0;
	if (tf)
		setvbuf(tf, 0, _IOFBF, 1 << 22);
	if (mmap((void *)(uintptr_t)LO, HI - LO, PROT_NONE, MAP_FIXED | MAP_PRIVATE | MAP_ANONYMOUS, -1, 0) !=
	    (void *)(uintptr_t)LO) {
		perror("chip: mmap");
		exit(2);
	}
	memset(&sa, 0, sizeof sa);
	sa.sa_flags = SA_SIGINFO;
	sa.sa_sigaction = on_segv;
	sigaction(SIGSEGV, &sa, 0);
	sa.sa_sigaction = on_trap;
	sigaction(SIGTRAP, &sa, 0);
	running = 1;
}

void chip_mark(const char *text)
{
	char b[600];

	if (tf)
		fprintf(tf, "-- %s\n", text);
	snprintf(b, sizeof b, "-- %s @%llu\n", text, (unsigned long long)acc);
	sum_add(b);
}

void chip_mark_panel(const char *text)
{
	char h[65], b[512];
	const char *dir = getenv("CHIP_PANEL_DIR");

	sha_of(fb, sizeof fb, h);
	snprintf(b, sizeof b, "%s fb=%.16s", text, h);
	chip_mark(b);
	if (dir) {
		char path[4200];
		FILE *f;
		snprintf(path, sizeof path, "%s/%s.raw", dir, text);
		f = fopen(path, "wb");
		if (f)
			fwrite(fb, 1, sizeof fb, f), fclose(f);
	}
}

/* ---- the summary ---- */

/* runs of sectors where f(s) gives the same nonzero class */
static void ranges(const char *what, char (*f)(u32))
{
	u32 s = 0;
	int any = 0;

	printf("flash %s", what);
	while (s < 256) {
		char c = f(s);
		u32 e = s;
		while (e + 1 < 256 && f(e + 1) == c)
			e++;
		if (c) {
			printf(" %05X-%05X", s << 12, (e << 12) | 0xFFF);
			if (c != '*')
				printf(":%c", c);
			any = 1;
		}
		s = e + 1;
	}
	printf("%s\n", any ? "" : " none");
}

static char c_erased(u32 s) { return erased_b[s] || erased_s[s] ? '*' : 0; }
static char c_programmed(u32 s) { return programmed[s] ? '*' : 0; }

static char c_state(u32 s)
{
	const u8 *a = dflash + (s << 12), *b = dflash0 + (s << 12);
	u32 i;

	if (!memcmp(a, b, 4096))
		return '=';
	for (i = 0; i < 4096 && a[i] == 0xFF; i++)
		;
	return i == 4096 ? 'F' : 'X';
}

static void expect(void)
{
	static u8 want[1 << 20];
	u32 i, klo = S->dev_board == 2 ? 0xC0000 : 0, khi = S->dev_board == 2 ? 0xC2000 : 0;

	memcpy(want, dflash0, sizeof want);
	if (S->expect == EXP_FIXED) {
		memcpy(want, bootbin, sizeof bootbin);
		for (i = 0x4000; i < 0xFF000; i++)
			if (i < klo || i >= khi)
				want[i] = 0xFF;
	}
	for (i = 0; i < sizeof want && want[i] == dflash[i]; i++)
		;
	if (i == sizeof want)
		printf("flash expect %s ok\n", S->expect == EXP_FIXED ? "repaired" : "unchanged");
	else
		printf("flash expect %s FAIL at %05X\n", S->expect == EXP_FIXED ? "repaired" : "unchanged", i);
}

void chip_finish(void)
{
	char h[65];

	running = 0;
	if (tf)
		fclose(tf), tf = 0;
	printf("scenario %s\n%s", S->name, sum);
	sha_hex(&th, h);
	printf("trace %llu %s\n", (unsigned long long)acc, h);
	if (p_cmds) {
		sha_of(fb, sizeof fb, h);
		printf("panel fb %s on %d commands %llu pixels %llu\n", h, p_on, (unsigned long long)p_cmds,
		       (unsigned long long)(p_bytes / 2));
		if (getenv("CHIP_PANEL")) {
			FILE *f = fopen(getenv("CHIP_PANEL"), "wb");
			if (f)
				fwrite(fb, 1, sizeof fb, f), fclose(f);
		}
	}
	if (n_latch)
		printf("keys sessions %d latches %llu pressed-reads %llu\n", ksess, (unsigned long long)n_latch,
		       (unsigned long long)key_reads);
	if (pads_used && (S->rom != ROM_ATTACHED || S->on_ms)) {
		printf("usbkey packets %llu other %llu false-acks %llu state %s", (unsigned long long)k_good,
		       (unsigned long long)k_bad, (unsigned long long)k_false, RS_NAME[rs]);
		if (ack_t)
			printf(" ack-at-us %llu", (unsigned long long)((ack_t - S->t0) / US));
		printf(" cal-pulses %llu", (unsigned long long)k_pulses);
		if (drop_t)
			printf(" drop-at-us %llu", (unsigned long long)((drop_t - S->t0) / US));
		printf("\n");
	}
	if (S->sof)
		printf("sof clears %llu\n", (unsigned long long)sof_clears);
	if (o_bytes)
		printf("own-flash bytes %llu dma %llu\n", (unsigned long long)o_bytes, (unsigned long long)o_dma);
	if (S->dev) {
		printf("usb setups %llu address %d config %d commands %llu mode %s loader %s bad-cbw %llu\n",
		       (unsigned long long)n_setup, dev_addr, dev_cfg, (unsigned long long)n_cmd, M_NAME[mode],
		       loader_ok < 0 ? "-" : loader_ok ? "ok" : "BAD", (unsigned long long)n_bad_cbw);
		printf("jl reads %llu writes %llu bad-crc %llu faults %llu\n", (unsigned long long)n_read,
		       (unsigned long long)n_write, (unsigned long long)n_crc_bad, (unsigned long long)n_fault);
		sha_of(dflash, sizeof dflash, h);
		printf("flash sha256 %s\n", h);
		ranges("erased", c_erased);
		ranges("programmed", c_programmed);
		ranges("state", c_state);
		if (S->expect)
			expect();
	}
	fflush(stdout);
	if (fallbacks)
		fprintf(stderr, "chip: %llu accesses single-stepped\n", (unsigned long long)fallbacks);
}
