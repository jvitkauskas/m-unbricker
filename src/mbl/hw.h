/* SPDX-License-Identifier: MIT */
/* Register addresses (SPEC.md 2.2) and MMIO access. The host tests build the
 * same driver code against a register-level simulator (tests/sim.c). */
#ifndef MBL_HW_H
#define MBL_HW_H
#include "mbl.h"

#ifdef HOST_TEST
u32 sim_rd(u32 addr);
void sim_wr(u32 addr, u32 val);
u32 sim_dma(const void *p); /* fake CPU address for a host buffer */
#define RD(a) sim_rd(a)
#define WR(a, v) sim_wr((a), (u32)(v))
#define DMA_ADDR(p) sim_dma(p)
#define csync() ((void)0)
#define cli() ((void)0)
#define spin(n) ((void)(n))
#else
#define RD(a) (*(volatile u32 *)(a))
#define WR(a, v) (*(volatile u32 *)(a) = (u32)(v))
#define DMA_ADDR(p) ((u32)(p))
#define csync() __asm__ volatile("csync" ::: "memory")
#define cli() __asm__ volatile("cli" ::: "memory")
static inline void spin(u32 n)
{
	while (n--)
		__asm__ volatile("");
}
#endif

static inline void SETB(u32 a, u32 m) { WR(a, RD(a) | m); }
static inline void CLRB(u32 a, u32 m) { WR(a, RD(a) & ~m); }
static inline void FIELD(u32 a, u32 mask, u32 val) { WR(a, (RD(a) & ~mask) | (val & mask)); }

/* Power, clocks, timer */
#define PWR_CON 0x10000u
#define CLK_DIV 0x10008u
#define CLK_CON0 0x1000Cu
#define CLK_CON2 0x10014u
#define CHIP_ID 0x10200u
#define TMR0 0x10400u /* +0 control, +4 count, +8 period, +0xC pwm */
#define PLL1_A 0x119A0u
#define PLL1_B 0x119A4u
#define PLL2_A 0x119A8u
#define PLL2_B 0x119ACu

/* SPI blocks: +0 control, +4 baud, +8 byte buffer, +0xC DMA address, +0x10 count */
#define SPI0 0x11C00u
#define SPI1 0x11D00u
#define SPI_CON 0x00u
#define SPI_BAUD 0x04u
#define SPI_BUF 0x08u
#define SPI_ADR 0x0Cu
#define SPI_CNT 0x10u
#define SPI_EN (1u << 0)
#define SPI_FULL (1u << 3) /* separate TX/RX lines */
#define SPI_RX (1u << 12) /* direction: 1 = receive */
#define SPI_CLR (1u << 14) /* write 1: clear completion */
#define SPI_DONE (1u << 15) /* completion */
#define SPI_W(w) ((u32)(w) << 10) /* DMA / byte width: 0 single, 1 dual, 2 quad */

/* Peripheral ENC (on the SPI0 DMA path) */
#define ENC_CON 0x13000u
#define ENC_KEY 0x13004u
#define ENC_ADR 0x13008u

/* P33 serial bridge */
#define P33_PMU 0x13E00u
#define P33_RTC 0x13E04u
#define P33_CON 0x13E08u
#define P33_DAT 0x13E0Cu

/* High-speed USB PHY control, shared with the SMK-37 key LED commons */
#define PHY_CON 0x16A0Cu

/* SFC (flash controller for XIP) and its ENC */
#define SFC_CON 0x40200u
#define SFC_BAUD 0x40204u
#define SFC_CODE 0x40208u
#define SFC_BASE 0x4020Cu
#define XENC_CON 0x40300u
#define XENC_KEY 0x40304u

/* GPIO ports A..H = 0..7 and pin mux */
#define GPIO(p) (0x50000u + 0x40u * (p))
#define GP_OUT 0x00u
#define GP_IN 0x04u
#define GP_DIR 0x08u /* 1 = input */
#define GP_DIE 0x0Cu /* digital input enable */
#define GP_PU 0x10u
#define GP_PD 0x14u
#define GP_HD0 0x18u
#define GP_HD 0x1Cu
#define GP_DIEH 0x20u
#define PA 0
#define PB 1
#define PC 2
#define PD 3
#define PE 4
#define PF 5
#define PG 6
#define PH 7
#define MUX0 0x5101Cu
#define MUX1 0x51020u

/* Cache */
#define CACHE_CON 0x01EEE008u

/* P33 register addresses (not CPU addresses) */
#define P33_WDOG 0x80u
#define P33_VDDIO 0x05u
#define P33_DCDC14 0x06u
#define P33_DVDD 0x09u

/* ---- shared helpers (src/hw/hw.c) ---- */
/* Calibrated delay: provided by the program (timer in the bootloader, a
 * conservative spin loop in the loader, whose CPU clock is the ROM's). */
void delay_us(u32 us);
u8 p33_read(u32 reg);
void p33_write(u32 reg, u32 val);
void p33_or(u32 reg, u32 bits);
void p33_and(u32 reg, u32 mask); /* reg &= mask */
/* One efuse row (3 or 4 bytes, little endian). */
u32 efuse_row(u32 bank, u32 row);

static inline void gp_set(u32 port, u32 pin, int hi)
{
	if (hi)
		SETB(GPIO(port) + GP_OUT, 1u << pin);
	else
		CLRB(GPIO(port) + GP_OUT, 1u << pin);
}
static inline int gp_get(u32 port, u32 pin) { return (RD(GPIO(port) + GP_IN) >> pin) & 1; }
static inline void gp_dir_out(u32 port, u32 pin) { CLRB(GPIO(port) + GP_DIR, 1u << pin); }
static inline void gp_dir_in(u32 port, u32 pin) { SETB(GPIO(port) + GP_DIR, 1u << pin); }

#endif
