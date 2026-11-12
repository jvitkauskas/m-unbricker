/* SPDX-License-Identifier: MIT */
#include "hw.h"

/* One byte of the P33 serial bridge. */
static u32 p33_byte(u32 b)
{
	u32 t = 100000;

	FIELD(P33_CON, 0xC, 0xC);
	WR(P33_DAT, b);
	SETB(P33_CON, 0x10);
	csync();
	while ((RD(P33_CON) & 2) && --t)
		;
	return RD(P33_DAT) & 0xFF;
}

static u32 p33_xfer(u32 op, u32 reg, u32 data)
{
	u32 r;

	SETB(P33_CON, 1);
	p33_byte(op | ((reg >> 8) & 3));
	p33_byte(reg & 0xFF);
	r = p33_byte(data);
	CLRB(P33_CON, 1);
	return r;
}

u8 p33_read(u32 reg) { return (u8)p33_xfer(0x80, reg, 0x5E); }
void p33_write(u32 reg, u32 val) { p33_xfer(0x00, reg, val & 0xFF); }
void p33_or(u32 reg, u32 bits) { p33_xfer(0x20, reg, bits & 0xFF); }
void p33_and(u32 reg, u32 mask) { p33_xfer(0x40, reg, mask & 0xFF); }

u32 efuse_row(u32 bank, u32 row)
{
	u32 w = (RD(CHIP_ID) & 0xFF) ? 4 : 3, j, v = 0;

	p33_write(0xB0, bank << 6);
	for (j = 0; j < w; j++) {
		p33_write(0xB1, (row << 6) | (j << 2) | 2);
		delay_us(60);
		v |= (u32)p33_read(0xB2) << (8 * j);
		p33_write(0xB1, 0);
	}
	return v;
}
