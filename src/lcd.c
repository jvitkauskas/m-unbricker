/* SPDX-License-Identifier: MIT */
/* The 240x240 ST7789 panel on SPI1: m-bootloader's src/hw/disp.c (MIT,
 * Julius Vitkauskas) without its 5x7 text, plus a window/pixel interface for
 * the strip renderer (ui.c). Pixels are RGB565, big endian. */
#include "lcd.h"
#include "hw.h"

#ifdef DISP_BOARD /* single-board build: constant-folds the other board away */
#define BOARD DISP_BOARD
#else
static u32 g_board;
#define BOARD g_board
#endif

#define S1CON (SPI1 + SPI_CON)

/* port, pin pairs */
struct pins {
	u8 clk_p, clk_n, mosi_p, mosi_n, cs_p, cs_n, dc_p, dc_n;
};
static const struct pins pin_fm1 = { PC, 9, PC, 10, PC, 7, PC, 8 };
static const struct pins pin_smk = { PB, 6, PB, 7, PC, 8, PB, 5 };

#define PINS (BOARD == BRD_FM1 ? &pin_fm1 : &pin_smk)


static void cs(int hi) { gp_set(PINS->cs_p, PINS->cs_n, hi); }
static void dc(int hi) { gp_set(PINS->dc_p, PINS->dc_n, hi); }

static void wait_done(void)
{
	u32 t = 2000000;

	csync();
	while (!(RD(S1CON) & SPI_DONE) && --t)
		;
	WR(S1CON, (RD(S1CON) & ~SPI_DONE) | SPI_CLR);
}

static void put(u32 b)
{
	WR(SPI1 + SPI_BUF, b & 0xFF);
	wait_done();
}

/* Command with parameters, one chip-select burst. */
static void cmd(u32 c, const u8 *d, u32 n)
{
	cs(0);
	dc(0);
	put(c);
	dc(1);
	while (n--)
		put(*d++);
	cs(1);
}

static void cmd1(u32 c, u32 v)
{
	u8 b = (u8)v;

	cmd(c, &b, 1);
}

/* Start a pixel burst: window, memory write, chip select stays low. */
static void window(u32 x, u32 y, u32 w, u32 h)
{
	u8 b[4];

	bs16(b, x);
	bs16(b + 2, x + w - 1);
	cmd(0x2A, b, 4);
	bs16(b, y);
	bs16(b + 2, y + h - 1);
	cmd(0x2B, b, 4);
	cs(0);
	dc(0);
	put(0x2C);
	dc(1);
}

static void send(const u8 *p, u32 n)
{
	if (!n)
		return; /* a zero count would never complete */
	WR(SPI1 + SPI_ADR, DMA_ADDR(p));
	WR(SPI1 + SPI_CNT, n);
	wait_done();
}

void lcd_window(u32 x, u32 y, u32 w, u32 h) { window(x, y, w, h); }
void lcd_pixels(const u8 *p, u32 n) { send(p, n); }
void lcd_end(void) { cs(1); }

static void lcd_clear(void)
{
	static u8 z[480] __attribute__((aligned(4)));
	u32 i;

	for (i = 0; i < sizeof z; i++)
		z[i] = 0;
	window(0, 0, 240, 240);
	for (i = 0; i < 240; i++)
		send(z, sizeof z);
	cs(1);
}

/* ---- initialisation ---- */

static const u8 gamma_smk[14] = { 0xD0, 0x08, 0x0E, 0x09, 0x09, 0x05, 0x31,
				  0x33, 0x48, 0x17, 0x14, 0x15, 0x31, 0x34 };

int lcd_init(u32 board)
{
#ifndef DISP_BOARD
	if (board != BRD_FM1 && board != BRD_SMK)
		return 0;
	g_board = board;
#else
	(void)board;
#endif
	if (BOARD == BRD_FM1)
		SETB(MUX1, 0x10);
	else
		CLRB(MUX1, 0x10);
	/* Chip select high; command/data, clock and data low; all outputs. */
	gp_set(PINS->cs_p, PINS->cs_n, 1);
	gp_set(PINS->dc_p, PINS->dc_n, 0);
	gp_set(PINS->clk_p, PINS->clk_n, 0);
	gp_set(PINS->mosi_p, PINS->mosi_n, 0);
	gp_dir_out(PINS->cs_p, PINS->cs_n);
	gp_dir_out(PINS->dc_p, PINS->dc_n);
	gp_dir_out(PINS->clk_p, PINS->clk_n);
	gp_dir_out(PINS->mosi_p, PINS->mosi_n);
	if (BOARD == BRD_FM1) { /* backlight, active low: off until the panel is cleared */
		gp_set(PA, 2, 1);
		gp_dir_out(PA, 2);
	}
	WR(S1CON, 0x4021);
	WR(SPI1 + SPI_BAUD, 4);
	if (BOARD == BRD_FM1) {
		cmd(0x01, 0, 0);
		delay_us(150000);
		cmd(0x11, 0, 0);
		delay_us(120000);
		cmd1(0x3A, 0x55);
		cmd1(0x36, 0x00);
		cmd(0x21, 0, 0);
		cmd(0x13, 0, 0);
	} else {
		delay_us(100000);
		cmd(0x11, 0, 0);
		delay_us(120000);
		cmd1(0x36, 0x00);
		cmd1(0x3A, 0x05);
		cmd(0xB2, (const u8 *)"\x0C\x0C\x00\x33\x33", 5);
		cmd1(0xB7, 0x35);
		cmd1(0xBB, 0x32);
		cmd1(0xC2, 0x01);
		cmd1(0xC3, 0x15);
		cmd1(0xC4, 0x20);
		cmd1(0xC6, 0x0F);
		cmd(0xD0, (const u8 *)"\xA4\xA1", 2);
		cmd(0xE0, gamma_smk, 14);
		cmd(0xE1, gamma_smk, 14);
		cmd(0x21, 0, 0);
	}
	lcd_clear();
	cmd(0x29, 0, 0);
	if (BOARD == BRD_FM1)
		gp_set(PA, 2, 0);
	return 1;
}
