/* SPDX-License-Identifier: MIT */
/* Which board this is, for the build that runs on both (m-unbricker-any.bin).
 * Read from the unit's own flash, never by probing pins (m-bootloader SPEC.md
 * 8.1), with the rule m-apps uses (kit/app.c): an SMK-37 keeps its calibration
 * ("trimdat" at 0xC1224, or its copy "TRIMCOPY" at 0xFF000); otherwise an
 * FM-1's flash chip (0x856014, 0xB36014) says FM-1; anything else is unknown.
 * The SPI0 set-up is m-bootloader's flash_init() (src/platform.c), reading a
 * byte at a time. Nothing runs from flash here, so XIP being off is fine. */
#include "board.h"
#include "hw.h"

static int failed;

static void wait_done(void)
{
	u32 n;

	for (n = 3000000; n; n--)
		if (RD(SPI0 + SPI_CON) & SPI_DONE)
			return;
	failed = 1;
}

static void cs(int high)
{
	gp_set(PD, 0, high);
	spin(40);
}

static u32 byte(u32 v, int rx)
{
	if (failed)
		return 0xFF;
	if (rx)
		SETB(SPI0 + SPI_CON, SPI_RX);
	else
		CLRB(SPI0 + SPI_CON, SPI_RX);
	WR(SPI0 + SPI_BUF, v);
	csync();
	wait_done();
	v = RD(SPI0 + SPI_BUF) & 0xFF;
	SETB(SPI0 + SPI_CON, SPI_CLR);
	return v;
}

static void command(u32 op)
{
	WR(SPI0 + SPI_CON, (RD(SPI0 + SPI_CON) & ~(SPI_W(3) | SPI_RX)) | SPI_FULL);
	cs(0);
	byte(op, 0);
}

static void read(u32 a, u8 *out, u32 n)
{
	command(3);
	byte(a >> 16, 0);
	byte(a >> 8, 0);
	byte(a, 0);
	while (n--)
		*out++ = (u8)byte(0xFF, 1);
	cs(1);
}

static int same(const u8 *a, const char *b, u32 n)
{
	while (n--)
		if (*a++ != (u8)*b++)
			return 0;
	return 1;
}

u32 board_detect(u32 *jedec)
{
	u8 mark[8];
	u32 id = 0, i;

	WR(SFC_CON, 0);
	WR(ENC_CON, 0);
	CLRB(MUX1, 1u << 1);
	CLRB(MUX0, 1u << 5);
	gp_set(PD, 0, 1);
	gp_dir_out(PD, 0);
	gp_set(PD, 4, 0);
	gp_dir_out(PD, 4);
	gp_set(PD, 5, 0);
	gp_dir_out(PD, 5);
	SETB(GPIO(PD) + GP_HD, 1u << 4);
	gp_dir_in(PD, 1);
	SETB(GPIO(PD) + GP_DIE, 1u << 1);
	CLRB(GPIO(PD) + GP_PU, 1u << 1);
	CLRB(GPIO(PD) + GP_PD, 1u << 1);
	gp_set(PD, 2, 1);
	gp_dir_out(PD, 2);
	gp_set(PD, 3, 1);
	gp_dir_out(PD, 3);
	WR(SPI0 + SPI_CON, 0x28);
	WR(SPI0 + SPI_BAUD, 1);
	SETB(SPI0 + SPI_CON, SPI_CLR | SPI_EN);
	cs(1);
	/* out of any continuous-read state the firmware left */
	command(0xFF);
	byte(0xFF, 0);
	cs(1);
	command(0x9F);
	for (i = 0; i < 3; i++)
		id = (id << 8) | byte(0xFF, 1);
	cs(1);
	if (jedec)
		*jedec = id;
	if (failed)
		return 0;
	read(0xC1224, mark, 8);
	if (!failed && same(mark, "trimdat", 8))
		return BRD_SMK;
	read(0xFF000, mark, 8);
	if (!failed && same(mark, "TRIMCOPY", 8))
		return BRD_SMK;
	if (!failed && (id == 0x856014u || id == 0xB36014u))
		return BRD_FM1;
	return 0;
}
