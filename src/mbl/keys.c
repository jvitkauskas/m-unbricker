/* SPDX-License-Identifier: MIT */
/* Recovery / menu keys and their LEDs on the two boards (SPEC.md 8.2, 8.3). */
#include "boot.h"
#include "hw.h"

#if BOARD_ID == BRD_FM1
#define SNAP_PORTS 2 /* PA, PH */
#else
#define SNAP_PORTS 2
#endif

/* Restore order: direction first (pins that were inputs stop driving the
 * shift registers), the output levels last. */
static const u8 snap_off[8] = { GP_DIR, GP_DIE, GP_PU, GP_PD, GP_HD0, GP_HD, GP_DIEH, GP_OUT };
static u32 snap[SNAP_PORTS][8];
static u32 snap_phy;
static u8 on;

static void save(void)
{
	u32 i, p;

	for (p = 0; p < SNAP_PORTS; p++)
		for (i = 0; i < 8; i++)
			snap[p][i] = RD(GPIO(p ? PH : PA) + snap_off[i]);
	snap_phy = RD(PHY_CON);
}

static void restore(void)
{
	u32 i, p;

	for (p = 0; p < SNAP_PORTS; p++)
		for (i = 0; i < 8; i++)
			WR(GPIO(p ? PH : PA) + snap_off[i], snap[p][i]);
	WR(PHY_CON, snap_phy);
}

static void pulse(u32 port, u32 pin)
{
	gp_set(port, pin, 1);
	gp_set(port, pin, 0);
}

static void sense_pin(u32 port, u32 pin)
{
	gp_dir_in(port, pin);
	SETB(GPIO(port) + GP_DIE, 1u << pin);
	SETB(GPIO(port) + GP_DIEH, 1u << pin); /* second digital-input enable (H12) */
	SETB(GPIO(port) + GP_PU, 1u << pin);
	CLRB(GPIO(port) + GP_PD, 1u << pin);
}

#if BOARD_ID == BRD_FM1

/* Two cascaded shift registers: data PA4, clock PA3, latch PA1; 16 bits MSB
 * first; a selected column is a low bit. */
static void shift(u32 w)
{
	int i;

	for (i = 15; i >= 0; i--) {
		gp_set(PA, 4, (w >> i) & 1);
		pulse(PA, 3);
	}
	pulse(PA, 1);
}

#define IDLE 0xFFFFu
#define W_REC 0xFF7Fu
#define W_SEL 0xFBFFu
#define W_PREV 0xF7FEu
#define W_NEXT 0xEFFDu

/* LED lines PA9, PA10, PH6, PH9 belong to sense rows PA5..PA8; the menu keys
 * use PH9 (octave keys, row PA8) and PA10 (PLAY, row PA6). */
static void led(u32 which, int hi)
{
	if (which)
		gp_set(PA, 10, hi);
	else
		gp_set(PH, 9, hi);
}

static void keys_on(void)
{
	if (on)
		return;
	on = 1;
	save();
	gp_set(PA, 4, 0);
	gp_set(PA, 3, 0);
	gp_set(PA, 1, 0);
	gp_dir_out(PA, 4);
	gp_dir_out(PA, 3);
	gp_dir_out(PA, 1);
	sense_pin(PA, 6);
	sense_pin(PA, 8);
	gp_set(PA, 10, 0);
	gp_set(PH, 9, 0);
	gp_dir_out(PA, 10);
	gp_dir_out(PH, 9);
	shift(IDLE);
}

static u32 sample(u32 word, u32 port, u32 pin)
{
	shift(word);
	delay_us(10);
	return !gp_get(port, pin); /* closed key reads low */
}

u32 plat_boot_keys(void)
{
	u32 k = 0;

	keys_on();
	k |= sample(W_REC, PA, 6) ? K_REC : 0;
	k |= sample(W_SEL, PA, 6) ? K_SEL : 0;
	shift(IDLE);
	return k;
}

u32 plat_menu_scan(void)
{
	static const struct {
		u16 w;
		u8 port, pin, led, key;
	} k[3] = { { W_PREV, PA, 8, 0, K_PREV }, { W_NEXT, PA, 8, 0, K_NEXT },
		   { W_SEL, PA, 6, 1, K_SEL } };
	u32 i, r = 0;

	keys_on();
	for (i = 0; i < 3; i++) {
		if (sample(k[i].w, k[i].port, k[i].pin))
			r |= k[i].key;
		led(k[i].led, 1);
		delay_us(1000);
		led(k[i].led, 0);
	}
	shift(IDLE);
	return r;
}

#else /* SMK-37 */

/* Four shift registers: data PA4, clock PA3, latch PA1, output enable PA0
 * (active low). The 32-bit word goes out least significant byte first, MSB
 * first within each byte. */
static void shift(u32 w)
{
	int b, i;

	for (b = 0; b < 4; b++)
		for (i = 7; i >= 0; i--) {
			gp_set(PA, 4, (w >> (8 * b + i)) & 1);
			pulse(PA, 3);
		}
	pulse(PA, 1);
}

#define IDLE 0xFFFFFFFFu
#define W_REC 0xF7FFFFFFu
#define W_SEL 0xFEFFFFFFu
#define W_PREV 0xFFFEFFFFu
#define W_NEXT 0xEFFFFFFFu
#define PHY_ON 0x1000Fu /* drive enables; bit 17 / 18 high = group 0 / 1 off */
#define PHY_OFF (PHY_ON | (3u << 17))

static void keys_on(void)
{
	if (on)
		return;
	on = 1;
	save();
	gp_set(PA, 0, 0);
	gp_set(PA, 1, 0);
	gp_set(PA, 3, 0);
	gp_set(PA, 4, 0);
	gp_dir_out(PA, 0);
	gp_dir_out(PA, 1);
	gp_dir_out(PA, 3);
	gp_dir_out(PA, 4);
	sense_pin(PH, 8);
	shift(IDLE);
}

static u32 sample(u32 word)
{
	shift(word);
	delay_us(10);
	return !gp_get(PH, 8);
}

u32 plat_boot_keys(void)
{
	u32 k = 0;

	keys_on();
	k |= sample(W_REC) ? K_REC : 0;
	k |= sample(W_SEL) ? K_SEL : 0;
	shift(IDLE);
	return k;
}

/* LED lines are the low 16 bits of the word (active low): `<` is line 0 of
 * group 0; `>` is line 0 and SEQ PLAY line 12 of group 1. Bits 5:3 address an
 * analog multiplexer and stay high. */
u32 plat_menu_scan(void)
{
	static const u32 col[3] = { W_PREV, W_NEXT, W_SEL };
	static const u8 key[3] = { K_PREV, K_NEXT, K_SEL };
	u32 t, seen = 7, pressed;

	keys_on();
	for (t = 0; t < 6; t++) {
		u32 g = t & 1, i = t % 3;

		WR(PHY_CON, PHY_OFF); /* both groups off while the lines change */
		pressed = sample(col[i] & (g ? 0xFFFFEFFEu : 0xFFFFFFFEu));
		if (!pressed)
			seen &= ~(1u << i);
		WR(PHY_CON, PHY_OFF & ~(1u << (g ? 18 : 17)));
		delay_us(1000);
	}
	WR(PHY_CON, PHY_OFF);
	shift(IDLE);
	return (seen & 1 ? key[0] : 0) | (seen & 2 ? key[1] : 0) | (seen & 4 ? key[2] : 0);
}

#endif

void plat_keys_off(void)
{
	if (!on)
		return;
	shift(IDLE);
	restore();
	on = 0;
}

#ifdef HOST_TEST
void keys_test_reset(void) { on = 0; }
#endif
