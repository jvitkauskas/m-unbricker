/* SPDX-License-Identifier: MIT */
/* Enumeration and bulk-only transport, after the Cardputer app's
 * uboot_host.cpp (same handling of stalls and early status blocks). */
#include "bot.h"
#include "husb.h"
#include "usbio.h"

#define CBW_SIG 0x43425355u /* "USBC" */
#define CSW_SIG 0x53425355u /* "USBS" */
#define TIMEOUT_MS 5000u
#define ADDR 1

const char *bot_err = "";
static struct dev_info *dev;
static u32 tag;
static u8 cfg[256] __attribute__((aligned(4)));
static u8 io[64] __attribute__((aligned(4)));

static void wait_ms(u32 ms)
{
	u32 t = tb_now();

	while (tb_ms_since(t) < ms)
		;
}

static void setup_pkt(u8 *s, u8 type, u8 req, u16 val, u16 idx, u16 len)
{
	s[0] = type;
	s[1] = req;
	st16(s + 2, val);
	st16(s + 4, idx);
	st16(s + 6, len);
}

static enum husb_res ctl(u8 addr, u8 type, u8 req, u16 val, u16 idx, u8 *data, u16 len, u32 *got)
{
	u8 s[8];
	u32 n = len;
	enum husb_res r;
	int i;

	setup_pkt(s, type, req, val, idx, len);
	for (i = 0; i < 3; i++) {
		r = husb_control(addr, dev ? dev->mps0 : 8, s, data, &n);
		if (r == HU_OK || r == HU_STALL || r == HU_GONE)
			break;
		wait_ms(20);
	}
	if (got)
		*got = n;
	return r;
}

static int find_bulk(struct dev_info *d, const u8 *p, u32 total)
{
	u32 i;
	int cur = -1, found = -1;
	u8 in = 0, out = 0;
	u16 mps = 64;

	for (i = 0; i + 1 < total && p[i] >= 2; i += p[i]) {
		if (p[i + 1] == 4) {
			if (found < 0)
				in = out = 0;
			cur = p[i + 2];
		} else if (p[i + 1] == 5 && found < 0 && (p[i + 3] & 3) == 2) {
			if (p[i + 2] & 0x80) {
				in = p[i + 2] & 0x0F;
				mps = (u16)(p[i + 4] | (p[i + 5] << 8));
			} else {
				out = p[i + 2] & 0x0F;
			}
			if (in && out)
				found = cur;
		}
	}
	if (found < 0)
		return 0;
	d->iface = (u8)found;
	d->ep_in = in;
	d->ep_out = out;
	d->mps = mps ? mps : 64;
	return 1;
}

int dev_enumerate(struct dev_info *d, int *res)
{
	u32 got;
	enum husb_res r;

	dev = 0;
	d->mps0 = 8;
	husb_bus_reset();
	wait_ms(20);
	/* first 8 bytes of the device descriptor at address 0: the EP0 size */
	r = ctl(0, 0x80, 6, 0x0100, 0, cfg, 8, &got);
	*res = r;
	if (r != HU_OK || got < 8)
		return 1;
	d->mps0 = cfg[7] ? cfg[7] : 8;
	dev = d;
	husb_bus_reset();
	wait_ms(20);
	r = ctl(0, 0x00, 5, ADDR, 0, 0, 0, 0); /* SET_ADDRESS */
	*res = r;
	if (r != HU_OK)
		return 2;
	wait_ms(10);
	r = ctl(ADDR, 0x80, 6, 0x0100, 0, cfg, 18, &got);
	*res = r;
	if (r != HU_OK || got < 18)
		return 3;
	d->vid = ld16(cfg + 8);
	d->pid = ld16(cfg + 10);
	r = ctl(ADDR, 0x80, 6, 0x0200, 0, cfg, 9, &got);
	*res = r;
	if (r != HU_OK || got < 9)
		return 4;
	got = ld16(cfg + 2);
	if (got > sizeof cfg)
		got = sizeof cfg;
	r = ctl(ADDR, 0x80, 6, 0x0200, 0, cfg, (u16)got, &got);
	*res = r;
	if (r != HU_OK || !find_bulk(d, cfg, got))
		return 5;
	r = ctl(ADDR, 0x00, 9, cfg[5], 0, 0, 0, 0); /* SET_CONFIGURATION */
	*res = r;
	if (r != HU_OK)
		return 6;
	husb_bulk_setup(ADDR, d->ep_in, d->ep_out, d->mps);
	return 0;
}

/* ------------------------------------------------------------------ BOT */

static void clear_halt(int in)
{
	ctl(ADDR, 0x02, 1, 0, in ? (u16)(0x80 | dev->ep_in) : dev->ep_out, 0, 0, 0);
	husb_reset_toggle(in);
}

static void reset_recovery(void)
{
	ctl(ADDR, 0x21, 0xFF, 0, dev->iface, 0, 0, 0);
	clear_halt(1);
	clear_halt(0);
}

static enum bot_res transfer(const u8 *cdb, u32 cdb_len, u8 *buf, u32 len, int in, u32 *moved)
{
	u8 cbw[31], csw[16];
	u32 i, n, early = 0;
	enum husb_res r;

	*moved = 0;
	tag++;
	for (i = 0; i < 31; i++)
		cbw[i] = 0;
	st32(cbw, CBW_SIG);
	st32(cbw + 4, tag);
	st32(cbw + 8, len);
	cbw[12] = in ? 0x80 : 0;
	cbw[14] = (u8)cdb_len;
	for (i = 0; i < cdb_len; i++)
		cbw[15 + i] = cdb[i];
	if (husb_bulk_out(cbw, 31, TIMEOUT_MS) != HU_OK) {
		bot_err = "SENDING CMD";
		return B_FAILED;
	}
	if (len && in) {
		r = husb_bulk_in(buf, len, &n, TIMEOUT_MS);
		if (r == HU_STALL) {
			clear_halt(1); /* stalled data phase: the status follows */
		} else if (r != HU_OK) {
			bot_err = "READING DATA";
			return B_FAILED;
		} else if (n == 13 && len > 13 && ld32(buf) == CSW_SIG && ld32(buf + 4) == tag) {
			for (i = 0; i < 13; i++) /* a status block in place of the data */
				csw[i] = buf[i];
			early = 1;
		} else {
			*moved = n;
		}
	} else if (len) {
		r = husb_bulk_out(buf, len, TIMEOUT_MS);
		if (r == HU_STALL) {
			clear_halt(0);
		} else if (r != HU_OK) {
			bot_err = "SENDING DATA";
			return B_FAILED;
		} else {
			*moved = len;
		}
	}
	if (!early) {
		r = husb_bulk_in(io, 64, &n, TIMEOUT_MS);
		if (r == HU_STALL) {
			clear_halt(1);
			r = husb_bulk_in(io, 64, &n, TIMEOUT_MS);
		}
		if (r != HU_OK || n != 13) {
			bot_err = "READING STATUS";
			return B_FAILED;
		}
		for (i = 0; i < 13; i++)
			csw[i] = io[i];
	}
	if (ld32(csw) != CSW_SIG || ld32(csw + 4) != tag) {
		bot_err = "BAD STATUS BLOCK";
		return B_FAILED;
	}
	if (csw[12] == 1) {
		bot_err = "REFUSED";
		return B_REFUSED;
	}
	if (csw[12] != 0) {
		bot_err = "PHASE ERROR";
		return B_FAILED;
	}
	return B_OK;
}

enum bot_res bot_execute(const u8 *cdb, u32 cdb_len, u8 *buf, u32 len, int in, u32 *moved)
{
	enum bot_res r = transfer(cdb, cdb_len, buf, len, in, moved);

	if (r == B_FAILED && husb_attached())
		reset_recovery();
	return r;
}
