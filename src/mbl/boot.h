/* SPDX-License-Identifier: MIT */
/* Program A (the bootloader): shared state and the platform interface that
 * separates the boot decisions (src/boot/boot.c, menu.c) from the hardware
 * (src/boot/plat.c, keys.c). */
#ifndef MBL_BOOT_H
#define MBL_BOOT_H
#include "cfg.h"
#include "disp.h"
#include "fs.h"
#include "mbl.h"

#ifndef BOARD_ID
#error "define BOARD_ID (BRD_FM1 or BRD_SMK), BOARD_NAME and BOARD_TAG"
#endif

#ifndef BOOT_VERSION
#define BOOT_VERSION "M-BOOT 0.1"
#endif

/* Keys (SPEC.md 8.2): the same logical keys on both boards. */
#define K_PREV 1u /* OCT-  |  < */
#define K_NEXT 2u /* OCT+  |  > */
#define K_SEL 4u /* PLAY  |  SEQ PLAY */
#define K_REC 8u /* REC   |  SEQ REC */

/* Why the menu opened. */
#define RS_KEY 0 /* the user asked for it */
#define RS_APP 1 /* application not bootable (see app.state) */
#define RS_FLASH 2 /* flash not recognised */
#define RS_RETURNED 3 /* the application returned */
#define RS_UPDATER 4 /* the staged updater was refused or returned */
#define RS_GUARD 5 /* calibration guard failed */

struct boot {
	struct cfg cfg;
	struct fsx fs;
	struct app app;
	u32 jedec;
	u32 trim; /* efuse trim result (low 16 bits go to the application) */
	u8 mac[8];
	u8 hdr[32]; /* decoded flash header */
	u8 xip_w; /* flash data width verified for XIP */
	u8 flash_ok; /* chip identified, capacity known */
	u8 hdr_ok;
	u8 xip_used; /* an XIP boot has been attempted since power-on / reset */
	u8 bypass; /* one-shot boot-anyway for this boot */
	u8 want_menu;
	u8 reason; /* RS_* */
	u8 stg_err; /* STG_* of the refused updater */
	u8 gstat; /* GUARD_* */
	u32 nreq; /* saved flash update records */
	u8 ram_req; /* a valid retained request exists */
	u8 freq[80]; /* a request found in flash */
};
extern struct boot B;
/* Shared staging: configuration blob at start-up, staged-updater blocks later. */
extern u8 iobuf[4096];
/* Contexts handed to the staged updater (kept alive below its load address). */
extern u8 upd_cfgobj[52], upd_memobj[60], upd_ctx[24];

/* The decision flow; never returns. `desc` is the ROM boot-device descriptor. */
void boot_main(void *desc);
/* Run the menu. Returns when the user chose to continue booting. */
void menu_run(void);
/* Bring the panel up (once) and show that a slow check is running. */
void menu_busy(void);

/* ---- platform (hardware) ---- */
#define SCAN_MS (BOARD_ID == BRD_FM1 ? 3 : 6) /* one plat_menu_scan() call */

/* Power, config from the ROM descriptor, pins, clocks, timer. */
void plat_early(void *desc, struct cfg *c);
/* Instantaneous scan of REC and SEL (no LEDs); K_REC | K_SEL bits. */
u32 plat_boot_keys(void);
/* One multiplexed pass over the menu keys with their LEDs lit; returns
 * K_PREV | K_NEXT | K_SEL and takes SCAN_MS milliseconds. */
u32 plat_menu_scan(void);
/* Deselect the key matrix and give the pins back as found. */
void plat_keys_off(void);
void plat_delay_ms(u32 ms);
/* Bring SPI0 up, identify the flash, read key / MAC / trim, choose the width.
 * Returns 0, or -1 when the chip is not recognised. */
int plat_flash_init(struct boot *b);
/* Hand SPI0 to the flash controller and enable decrypted XIP; and back. */
void plat_xip_on(struct boot *b);
void plat_xip_off(void);
void plat_call_app(u32 entry, void *obj);
void plat_call_updater(u32 entry, void *ctx, u32 end);
void plat_reset(void) __attribute__((noreturn));
void plat_usb_download(void) __attribute__((noreturn));

#endif
