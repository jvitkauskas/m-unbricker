/* SPDX-License-Identifier: MIT */
/* USB0 host mode, full speed, polled. Register sequences: docs/husb-notes.md
 * (read from the JieLi AC79 SDK's USB library, Apache-2.0, and the
 * fm1-base device driver). EP0 for control, hardware EP1 TX + RX for bulk. */
#include "husb.h"
#include "hw.h"
#include "usbio.h"

#define CON0 0x11800u
#define CON1 0x11804u
#define EP_CNT(n) (0x11808u + 4u * (n))
#define EP0_ADR 0x11818u
#define EP_TADR(n) (0x1181Cu + 8u * ((n) - 1u))
#define EP_RADR(n) (0x11820u + 8u * ((n) - 1u))
#define IO_CON0 0x51000u
#define IO_CON1 0x51004u
#define CLK_CON1 0x10010u

#define CON0_DP_SE (1u << 17)
#define CON0_SOF_PND (1u << 13)
#define CON0_SOF_CLR (1u << 12)

enum { FADDR = 0, POWER = 1, INTRUSB = 6, INTRTX1E = 7, INTRTX2E = 8, INTRRX1E = 9, INTRRX2E = 10,
       INTRUSBE = 11, INDEX = 14, DEVCTL = 15, TXMAXP = 16, CSR0 = 17, TXCSR1 = 17, TXCSR2 = 18,
       RXMAXP = 19, RXCSR1 = 20, RXCSR2 = 21, COUNT0 = 22, RXCOUNT1 = 22, RXCOUNT2 = 23,
       TXTYPE = 24, RXTYPE = 26 };

u32 husb_dbg[4];
static u32 sie_timeouts;
static u8 ep0buf[72] __attribute__((aligned(4)));
static u8 txbuf[64] __attribute__((aligned(4)));
static u8 rxbuf[72] __attribute__((aligned(4)));
static u8 b_addr, b_in, b_out;
static u16 b_mps = 64;

#define ssync() __asm__ volatile("ssync" ::: "memory")

static void wait_ms(u32 ms)
{
	u32 t = tb_now();

	while (tb_ms_since(t) < ms)
		;
}

static void sie_wr(u32 r, u32 v)
{
	u32 n = 20001;

	WR(CON1, ((r << 8) & 0xFFFF00u) | (v & 0xFFu));
	csync();
	while (!(RD(CON1) & 0x8000u))
		if (!--n) {
			sie_timeouts++;
			return;
		}
}

static u32 sie_rd(u32 r)
{
	u32 n = 20001;

	WR(CON1, ((r << 8) & 0xFFBF00u) | 0x4000u);
	csync();
	while (!(RD(CON1) & 0x8000u))
		if (!--n) {
			sie_timeouts++;
			return 0;
		}
	return RD(CON1) & 0xFFu;
}

static void copy(u8 *d, const u8 *s, u32 n)
{
	while (n--)
		*d++ = *s++;
}

static void snapshot(u32 code, u32 csr)
{
	husb_dbg[0] = RD(CON0);
	husb_dbg[1] = sie_rd(DEVCTL) | (sie_rd(POWER) << 8) | (sie_rd(FADDR) << 16) | (sie_rd(INTRUSB) << 24);
	husb_dbg[2] = csr;
	husb_dbg[3] = (code << 24) | (sie_timeouts & 0xFFFFFFu);
}

/* ------------------------------------------------------------ bring-up */

void husb_init(void)
{
	WR(CON0, 0);
	WR(CON1, 0);
	WR(IO_CON0, 0x0C);
	wait_ms(20);
	CLRB(CLK_CON1, 3); /* USB clock: PLL 48 MHz */
	WR(IO_CON0, 0);
	WR(IO_CON1, 0);
	wait_ms(2);
	/* digital inputs, no pull-ups, host pull-downs, pads to the PHY */
	WR(IO_CON0, (1u << 9) | (1u << 10) | (1u << 4) | (1u << 5));
	SETB(CON0, 0x25); /* PHY on, SIE out of reset, VBUS valid; ID 0 = host */
	wait_ms(2);
	WR(EP0_ADR, DMA_ADDR(ep0buf));
	sie_timeouts = 0;
}

static int session(void)
{
	u32 t0, d = 0;

	sie_wr(POWER, 0x60);
	sie_wr(INTRUSBE, 0);
	sie_wr(INTRTX1E, 0);
	sie_wr(INTRTX2E, 0);
	sie_wr(INTRRX1E, 0);
	sie_wr(INTRRX2E, 0);
	sie_wr(DEVCTL, 0x03); /* session, host request */
	t0 = tb_now();
	while (tb_ms_since(t0) < 500u) {
		d = sie_rd(DEVCTL);
		if ((d & 0x04) && (d & 0x40))
			return 1; /* host mode, full-speed device */
		if ((d & 0x04) && (d & 0x20)) {
			SETB(CON0, 2); /* low speed (not expected) */
			return 1;
		}
		wait_ms(10);
	}
	snapshot(1, d);
	return 0;
}

int husb_wait_attach(u32 timeout_ms)
{
	u32 t0 = tb_now(), high = 0;

	while (tb_ms_since(t0) < timeout_ms) {
		if (RD(CON0) & CON0_DP_SE) {
			if (++high >= 100) /* 100 ms debounce */
				return session();
		} else {
			high = 0;
		}
		wait_ms(1);
	}
	snapshot(2, 0);
	return 0;
}

int husb_attached(void)
{
	int i;

	for (i = 0; i < 16; i++)
		if (RD(CON0) & ((1u << 17) | (1u << 16)))
			return 1;
	return 0;
}

void husb_bus_reset(void)
{
	u32 p, n = 0;

	do {
		p = sie_rd(POWER);
		sie_wr(POWER, (p & 0xF7u) | 0x08u);
	} while (!(p & 0x08u) && ++n < 10);
	wait_ms(50);
	sie_wr(POWER, sie_rd(POWER) & 0xF7u);
	wait_ms(40);
}

/* ------------------------------------------------------------- control */

static u32 csr0(void)
{
	sie_wr(INDEX, 0);
	return sie_rd(CSR0);
}

static void csr0_w(u32 v)
{
	sie_wr(INDEX, 0);
	sie_wr(CSR0, v);
}

/* Waits until (csr & mask) == want; STALL / error / timeout end it. */
static enum husb_res ep0_wait(u32 mask, u32 want, u32 stage)
{
	u32 t0 = tb_now(), c;

	for (;;) {
		c = csr0();
		if (c & 0x04) {
			snapshot(0x10 + stage, c);
			csr0_w(0);
			return HU_STALL;
		}
		if (c & 0x10) {
			snapshot(0x20 + stage, c);
			csr0_w(0);
			return HU_ERROR;
		}
		if ((c & mask) == want)
			return HU_OK;
		if (tb_ms_since(t0) > 1000u) {
			snapshot(0x30 + stage, c);
			csr0_w(0);
			return HU_TIMEOUT;
		}
	}
}

enum husb_res husb_control(u8 addr, u8 mps0, const u8 setup[8], u8 *data, u32 *len)
{
	u32 want = ld16(setup + 6), left, n, got = 0;
	int in = setup[0] & 0x80;
	enum husb_res r;

	*len = 0;
	if (!mps0 || mps0 > 64)
		mps0 = 8;
	sie_wr(FADDR, addr);
	copy(ep0buf, setup, 8);
	csync();
	WR(EP_CNT(0), 8);
	csr0_w(0x0A); /* SETUP */
	r = ep0_wait(0x02, 0, 1);
	if (r != HU_OK)
		return r;
	left = want;
	if (want && in) {
		csr0_w(0x20);
		while (left) {
			r = ep0_wait(0x01, 0x01, 2);
			if (r != HU_OK)
				return r;
			sie_wr(INDEX, 0);
			n = sie_rd(COUNT0);
			if (n > left)
				n = left;
			ssync();
			copy(data + got, ep0buf, n);
			got += n;
			left -= n;
			if (n < mps0)
				left = 0;
			csr0_w(left ? 0x20 : 0x00);
		}
	} else if (want) {
		while (left) {
			n = left > mps0 ? mps0 : left;
			copy(ep0buf, data + got, n);
			csync();
			WR(EP_CNT(0), n);
			csr0_w(0x02);
			r = ep0_wait(0x02, 0, 3);
			if (r != HU_OK)
				return r;
			got += n;
			left -= n;
		}
	}
	*len = got;
	if (want && in) { /* status OUT: zero-length */
		WR(EP_CNT(0), 0);
		csr0_w(0x42);
		return ep0_wait(0x02, 0, 4);
	}
	csr0_w(0x60); /* status IN */
	r = ep0_wait(0x01, 0x01, 5);
	csr0_w(0);
	return r;
}

/* ---------------------------------------------------------------- bulk */

void husb_bulk_setup(u8 addr, u8 ep_in, u8 ep_out, u16 mps)
{
	b_addr = addr;
	b_in = ep_in;
	b_out = ep_out;
	b_mps = mps && mps <= 64 ? mps : 64;
	WR(EP_RADR(1), DMA_ADDR(rxbuf));
	WR(EP_TADR(1), DMA_ADDR(txbuf));
	sie_wr(INDEX, 1);
	sie_wr(RXMAXP, 0xFF);
	sie_wr(RXCSR1, 0x90); /* toggle DATA0, flush */
	sie_wr(RXCSR2, 0);
	sie_wr(TXMAXP, 0xFF);
	sie_wr(TXCSR1, 0x48); /* toggle DATA0, flush */
	sie_wr(TXCSR2, 0);
}

void husb_reset_toggle(int in)
{
	sie_wr(INDEX, 1);
	if (in)
		sie_wr(RXCSR1, 0x90);
	else
		sie_wr(TXCSR1, 0x48);
}

enum husb_res husb_bulk_out(const u8 *buf, u32 len, u32 timeout_ms)
{
	u32 n, c, t0 = tb_now(), errs = 0;

	sie_wr(FADDR, b_addr);
	while (len) {
		n = len > b_mps ? b_mps : len;
		copy(txbuf, buf, n);
		csync();
	resend:
		sie_wr(INDEX, 1);
		sie_wr(TXTYPE, 0x20u | b_out);
		WR(EP_CNT(1), n);
		csync();
		sie_wr(TXCSR1, 0x01);
		for (;;) {
			c = sie_rd(TXCSR1);
			if (c & 0x20) {
				snapshot(0x40, c);
				sie_wr(TXCSR1, c & ~0x21u);
				return HU_STALL;
			}
			if (c & 0x84) { /* NAK timeout or 3 errors: send again */
				sie_wr(TXCSR1, c & ~0x85u);
				if (++errs > 5) {
					snapshot(0x41, c);
					return HU_ERROR;
				}
				goto resend;
			}
			if (!(c & 0x01))
				break; /* ACKed */
			if (tb_ms_since(t0) > timeout_ms) {
				snapshot(0x42, c);
				sie_wr(TXCSR1, 0x08); /* flush */
				return HU_TIMEOUT;
			}
		}
		buf += n;
		len -= n;
	}
	return HU_OK;
}

enum husb_res husb_bulk_in(u8 *buf, u32 len, u32 *got, u32 timeout_ms)
{
	u32 n, c, t0 = tb_now(), errs = 0, left = len;

	*got = 0;
	sie_wr(FADDR, b_addr);
	sie_wr(INDEX, 1);
	sie_wr(RXTYPE, 0x20u | b_in);
	sie_wr(RXCSR1, 0x20); /* request a packet */
	for (;;) {
		c = sie_rd(RXCSR1);
		if (c & 0x01) {
			n = sie_rd(RXCOUNT1) | (sie_rd(RXCOUNT2) << 8);
			ssync();
			copy(buf + *got, rxbuf, n < left ? n : left);
			sie_wr(RXCSR1, 0x10); /* release the buffer */
			*got += n < left ? n : left;
			left -= n < left ? n : left;
			if (!left || n < b_mps)
				return HU_OK;
			sie_wr(RXTYPE, 0x20u | b_in);
			sie_wr(RXCSR1, 0x20);
			t0 = tb_now();
			continue;
		}
		if (c & 0x40) {
			snapshot(0x50, c);
			sie_wr(RXCSR1, 0);
			return HU_STALL;
		}
		if (c & 0x0C) { /* error or NAK timeout: ask again */
			if (++errs > 5) {
				snapshot(0x51, c);
				sie_wr(RXCSR1, 0);
				return HU_ERROR;
			}
			sie_wr(RXCSR1, 0x20);
			continue;
		}
		if (tb_ms_since(t0) > timeout_ms) {
			snapshot(0x52, c);
			sie_wr(RXCSR1, 0);
			sie_wr(RXCSR1, 0x10);
			return HU_TIMEOUT;
		}
	}
}
