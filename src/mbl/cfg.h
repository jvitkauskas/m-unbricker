/* SPDX-License-Identifier: MIT */
/* Boot configuration blob (SPEC.md 5.3). */
#ifndef MBL_CFG_H
#define MBL_CFG_H
#include "mbl.h"

#define CFG_MAX 4096u

/* The only clock plan this bootloader programs: 24 MHz oscillator, main PLL
 * 480 MHz, system 360 MHz (SPEC.md 3.2). */
#define CFG_OSC_HZ 24000000u
#define CFG_SYS_HZ 360000000u

#define CFG_OK 0 /* parsed, supported */
#define CFG_DEFAULT 1 /* unreadable / malformed / unsupported: defaults used */

struct cfg {
	u8 state; /* CFG_* */
	u8 spi_w; /* flash data width: 1, 2 or 4 */
	u8 spi_div; /* SPI baud divider register value */
	u8 spi_mode; /* 0 output, 1 I/O, 2 continuous I/O */
	u8 hsb_div; /* divisor - 1 */
	u8 lsb_div; /* divisor - 1 */
	u8 hc, onepin; /* oscillator flags, kept for updater metadata */
	u8 rst_pin; /* port*16 + pin, 0 when absent */
	u8 rst_lvl;
	u8 rst_hold;
};

/* The recovery profile: validated clock plan, dual-output flash reads with
 * divider 3 (the SPEC.md 5.3 fallback for a missing SPI record). */
void cfg_defaults(struct cfg *c);
/* Parse `n` plain blob bytes. On any problem `c` is left at the defaults and
 * the result is nonzero. */
int cfg_parse(struct cfg *c, const u8 *blob, u32 n);

#endif
