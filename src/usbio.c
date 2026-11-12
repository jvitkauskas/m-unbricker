/* SPDX-License-Identifier: MIT */
/* USB_KEY and calibration pulses from the rescuing unit's USB port.
 *
 * Waveforms and thresholds follow the Cardputer app (cardputer-synth-rescue
 * src/usbkey.cpp), which follows FM-1-transporter and the Flipper app, all
 * verified on real units:
 *   key 0x16EF, MSB first, D+ = clock, D- = data, push-pull, one packet per
 *   340 us slot; ROM answer = D- held low, then D+ pulled up by the ROM;
 *   calibration = 4 us low on D+ every 1 ms, open drain, until D+ drops after
 *   >= 1 s or for 6 s.
 *
 * USB0 pins as GPIO (register 0x51000, bits from the AC79 SDK's gpio
 * functions): out D+ 0 / D- 1, direction (1 = input) 2 / 3, pull-down 4 / 5,
 * pull-up 6 / 7, digital input 9 / 10, IO mode 8 + 11. Pin levels: 0x51004
 * bit 0 / 1. Everything runs with interrupts off. */
#include "usbio.h"
#include "hw.h"

#define USB_CON0 0x11800u
#define USB_CON1 0x11804u
#define USB_SOF_PEND (1u << 13)
#define USB_SOF_CLR (1u << 12)

#define UIO 0x51000u
#define UIO_IN 0x51004u
#define DP_OUT (1u << 0)
#define DM_OUT (1u << 1)
#define DP_DIR (1u << 2)
#define DM_DIR (1u << 3)
#define DP_PD (1u << 4)
#define DM_PD (1u << 5)
#define DP_PU (1u << 6)
#define DM_PU (1u << 7)
#define UIO_MODE ((1u << 8) | (1u << 11))
#define UIO_DIE ((1u << 9) | (1u << 10))

/* ------------------------------------------------------------- timebase */

u32 tb_tps = 48000000u; /* the loader state's LSB clock */
u8 tb_measured;

void tb_start(void)
{
	WR(TMR0, 0x4000);
	WR(TMR0 + 4, 0);
	WR(TMR0 + 8, 0xFFFFFFFFu);
	WR(TMR0, 1);
	csync();
}

u32 tb_now(void) { return RD(TMR0 + 4); }

u32 tb_us(u32 us)
{
	u32 tpms = tb_tps / 1000u;

	return (us / 1000u) * tpms + (us % 1000u) * tpms / 1000u;
}

void tb_wait_until(u32 t)
{
	while ((s32)(tb_now() - t) < 0)
		;
}

u32 tb_ms_since(u32 t0) { return (tb_now() - t0) / (tb_tps / 1000u); }

void delay_us(u32 us) /* for the display driver */
{
	u32 t = tb_now();

	while (us) {
		u32 n = us > 1000000u ? 1000000u : us;

		t += tb_us(n);
		tb_wait_until(t);
		us -= n;
	}
}

static int sof_wait(u32 timeout)
{
	u32 t0 = tb_now();

	while (!(RD(USB_CON0) & USB_SOF_PEND))
		if (tb_now() - t0 > timeout)
			return 0;
	SETB(USB_CON0, USB_SOF_CLR);
	return 1;
}

int tb_calibrate_sof(u32 frames, u32 *seen)
{
	u32 t0, t1, n, to = 48000u * 20u; /* 20 ms at the nominal clock */

	*seen = 0;
	SETB(USB_CON0, USB_SOF_CLR);
	if (!sof_wait(to))
		return 0;
	t0 = tb_now();
	for (n = 0; n < frames; n++) {
		if (!sof_wait(to))
			return 0;
		*seen = n + 1;
	}
	t1 = tb_now();
	if (frames != 1000u)
		return 0;
	t1 -= t0; /* ticks in 1000 frames = 1 s */
	if (t1 < 40000000u || t1 > 56000000u)
		return 0;
	tb_tps = t1;
	tb_measured = 1;
	return 1;
}

/* ------------------------------------------------------------- USB pins */

static u32 uio;

static inline void uio_put(void) { WR(UIO, uio); }

void usb_take_pins(void)
{
	WR(USB_CON0, 0);
	WR(USB_CON1, 0);
	uio = UIO_MODE | UIO_DIE | DP_DIR | DM_DIR | DP_PD | DM_PD;
	uio_put();
}

void usb_float_pins(void)
{
	uio = UIO_MODE | UIO_DIE | DP_DIR | DM_DIR;
	uio_put();
}

int pin_dp(void) { return RD(UIO_IN) & 1; }
int pin_dm(void) { return (RD(UIO_IN) >> 1) & 1; }

/* ------------------------------------------------------------------ key */

#define USB_KEY 0x16EFu
#define KEY_SLOT_US 340u
#define GAP_PROBES 4
#define GAP_PROBE_SPACING_US 35u
#define ACK_PROBE_LOWS 2
#define DP_HIGH_TIMEOUT_MS 500u

static u32 t_us2, t_us4, t_us8, t_us10, t_us12;

/* ~323 us: both lines low; per bit clock low + data for 8 us, clock high for
 * 12 us (10 us on the last bit); then both released. */
static void key_packet(void)
{
	u32 t = tb_now();
	int bit;

	uio &= ~(DP_OUT | DM_OUT | DP_DIR | DM_DIR);
	uio_put();
	t += t_us4;
	tb_wait_until(t);
	for (bit = 15; bit >= 0; bit--) {
		if ((USB_KEY >> bit) & 1)
			uio = (uio & ~DP_OUT) | DM_OUT;
		else
			uio &= ~(DP_OUT | DM_OUT);
		uio_put();
		t += t_us8;
		tb_wait_until(t);
		uio |= DP_OUT; /* rising edge: the ROM latches D- */
		uio_put();
		t += bit ? t_us12 : t_us10;
		tb_wait_until(t);
	}
	uio &= ~(DP_OUT | DM_OUT);
	uio |= DP_DIR | DM_DIR;
	uio_put();
}

/* Drive D- high for 2 us and read it: low only if something holds it down
 * hard (the ROM's ACK; an unpowered unit clamps it too). */
static int probe_dm_high(void)
{
	int v;

	uio = (uio | DM_OUT) & ~DM_DIR;
	uio_put();
	tb_wait_until(tb_now() + t_us2);
	v = pin_dm();
	uio = (uio | DM_DIR) & ~DM_OUT;
	uio_put();
	return v;
}

/* 0: nothing, 1: ACK (D- held low), 2: D+ already pulled up */
static int gap_sense(u8 *alive)
{
	int i, lows = 0, dp_highs = 0;
	u32 t = tb_now();

	for (i = 0; i < GAP_PROBES; i++) {
		t += tb_us(GAP_PROBE_SPACING_US);
		tb_wait_until(t);
		if (pin_dp()) {
			if (++dp_highs >= 2)
				return 2;
		} else {
			dp_highs = 0;
		}
		if (probe_dm_high()) {
			*alive = 1;
			lows = 0;
		} else if (*alive && ++lows >= ACK_PROBE_LOWS) {
			return 1;
		}
	}
	return 0;
}

static int wait_dp_high(u32 timeout_ms)
{
	u32 t0 = tb_now(), t = t0;
	int highs = 0;

	while (tb_ms_since(t0) < timeout_ms) {
		highs = pin_dp() ? highs + 1 : 0;
		if (highs >= 5)
			return 1;
		t += tb_us(50);
		tb_wait_until(t);
	}
	return 0;
}

enum key_result key_send(struct key_stats *s, u32 timeout_ms, void (*tick)(const struct key_stats *))
{
	u32 start, last_tick, slot;
	int g;

	t_us2 = tb_us(2);
	t_us4 = tb_us(4);
	t_us8 = tb_us(8);
	t_us10 = tb_us(10);
	t_us12 = tb_us(12);
	s->packets = s->false_acks = 0;
	s->alive = 0;
	usb_take_pins();
	delay_us(5000);
	if (pin_dp())
		return KEY_DP_AT_START;
	start = last_tick = tb_now();
	for (;;) {
		slot = tb_now();
		key_packet();
		s->packets++;
		tb_wait_until(slot + tb_us(KEY_SLOT_US));
		g = gap_sense(&s->alive);
		if (g == 2)
			return KEY_DP_ANSWER;
		if (g == 1) {
			if (wait_dp_high(DP_HIGH_TIMEOUT_MS))
				return KEY_ACK;
			s->false_acks++; /* D- low without the D+ pull-up: not an ACK */
			s->alive = 0;
		}
		if (tb_ms_since(start) > timeout_ms) {
			usb_take_pins();
			return KEY_TIMEOUT;
		}
		if (tb_ms_since(last_tick) >= 250u) {
			last_tick = tb_now();
			tick(s);
		}
	}
}

/* ------------------------------------------------------ calibration pulses */

#define PULSE_MAX_MS 6000u
#define CAL_DONE_MIN_MS 1000u
#define PULSE_LOW_SAMPLES 3 /* 300 us low: not one of our 4 us pulses */

void cal_pulses(struct cal_result *r)
{
	u32 tpms = tb_tps / 1000u, frac = tb_tps % 1000u, acc = 0;
	u32 t0, next, nxt, ms = 0;
	int lows = 0, i;

	t_us4 = tb_us(4);
	r->drop_ms = -1;
	r->pulses = 0;
	uio = UIO_MODE | UIO_DIE | DP_DIR | DM_DIR | DP_PD | DM_PD; /* D+ released */
	uio_put();
	t0 = next = tb_now() + tb_us(100);
	for (;;) {
		tb_wait_until(next);
		uio &= ~(DP_OUT | DP_DIR); /* D+ low */
		uio_put();
		tb_wait_until(next + t_us4);
		uio |= DP_DIR; /* released: the ROM's pull-up takes it high */
		uio_put();
		r->pulses++;
		nxt = next + tpms;
		acc += frac;
		if (acc >= 1000u) {
			nxt++;
			acc -= 1000u;
		}
		for (i = 1; i <= 9; i++) {
			tb_wait_until(next + i * (tpms / 10u));
			if (!pin_dp()) {
				if (++lows >= PULSE_LOW_SAMPLES && ms >= CAL_DONE_MIN_MS) {
					r->drop_ms = (s32)ms;
					goto done;
				}
			} else {
				lows = 0;
			}
		}
		next = nxt;
		ms = (next - t0) / tpms;
		if (ms >= PULSE_MAX_MS)
			break;
	}
done:
	r->pulse_ms = (tb_now() - t0) / tpms;
	uio = UIO_MODE | UIO_DIE | DP_DIR | DM_DIR | DP_PD | DM_PD;
	uio_put();
}
