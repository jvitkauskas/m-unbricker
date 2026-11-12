/* SPDX-License-Identifier: MIT */
/* Rescue install (port of the Cardputer app's writer.cpp): every writable
 * sector below 0xFF000 ends up exactly as the image says (boot area,
 * firmware, the rest erased). Never touched: 0xFF000-0xFFFFF and, on the
 * SMK-37, 0xC0000-0xC1FFF (Bluetooth pairing, factory calibration). */
#ifndef RK_WRITER_H
#define RK_WRITER_H
#include "mbl.h"

#define SECTOR 0x1000u
#define W_END 0xFF000u
#define SECTORS (W_END / SECTOR)

struct image {
	const char *name;
	const u8 *boot;
	const u8 *lit;
	const u32 (*runs)[3];
	u32 nruns;
	u32 app_len;
	u32 boot_crc, app_crc;
	u32 keep_lo, keep_hi;
};

enum { IMG_FM1, IMG_SMK37 };
const struct image *image_get(int which);
/* The image regenerates to the build's CRCs. */
int image_ok(const struct image *img);

struct plan {
	u8 change[SECTORS];
	u32 to_write, to_erase, runs;
};

typedef void (*progress_fn)(u32 done, u32 total);
/* Reads the unit's flash and compares it with the image. 1 = OK. */
int plan_make(const struct image *img, struct plan *p, progress_fn progress);
/* Erases, programs, verifies run by run; final checks. 1 = OK. */
int plan_apply(const struct image *img, const struct plan *p, progress_fn progress);
extern const char *wr_err;
extern u32 wr_err_addr;

#endif
