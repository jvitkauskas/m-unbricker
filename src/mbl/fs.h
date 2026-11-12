/* SPDX-License-Identifier: MIT */
/* Flash filesystem records and the application area (SPEC.md 5.2, 5.4). */
#ifndef MBL_FS_H
#define MBL_FS_H
#include "mbl.h"

#define REC_SZ 32u
#define HDR_KEY 0xFFFFu /* fixed key of the top-level metadata */

/* Application state classification (SPEC.md 9.1). */
#define APP_OK 0
#define APP_NOTFOUND 1
#define APP_BADCRC 2
#define APP_INCOMPLETE 3

#define XIP_BASE 0x02000000u /* decrypted flash window; app logical 0 */
#define XIP_LIMIT 0x04000000u /* external RAM window starts here */

struct rec {
	u16 dcrc; /* data CRC16 (or FFFF) */
	u32 addr;
	u32 len;
	u8 attr;
	u8 name[16]; /* NUL-terminated only when shorter than 16 */
};

/* Parse a decrypted 32-byte record. Returns 1 when its CRC is valid. */
int rec_parse(const u8 *d, struct rec *r);
/* Bounded name comparison against a C string of at most 16 characters. */
int name_eq(const u8 *name16, const char *s);

/* Per-flash facts the logic needs. */
struct fsx {
	u32 cap; /* physical capacity in bytes */
	u16 key; /* unit encryption key */
};

/* Read and decode one top-level record (key FFFF) at an absolute address.
 * Returns 1 when read and CRC are fine. */
int rec_top(u32 addr, struct rec *r);

/* The decoded 32-byte flash header at address 0. Returns 1 if its CRC holds. */
int fs_header(u8 hdr[32]);

/* Flash key fallback page (last 256 bytes): returns 1 and sets *key if valid. */
int key_from_page(const u8 *page, u16 *key);

/* ---- application area ---- */

#define MAXF 8
#define MAXR 4
#define RK_VM 1
#define RK_BTIF 2

struct afile {
	u32 rel, len; /* area-relative */
	u16 crc;
};
struct reg {
	u32 start, len; /* absolute physical */
	u8 kind;
};
struct app {
	u8 state; /* APP_* */
	u8 nf, nr;
	u8 safe; /* metadata is sound enough to run the code even if a CRC fails */
	u8 rover; /* more VM / BTIF records than fit: nothing is eligible for a wipe */
	u32 base; /* physical base of the selected area */
	u32 entry; /* CPU entry address */
	u32 applen; /* app.bin length */
	struct afile f[MAXF];
	struct reg r[MAXR];
};

/* Select the bank and read the area tables. Sets state to APP_NOTFOUND when
 * nothing usable exists, else APP_OK (not yet CRC-checked). */
void app_mount(const struct fsx *fs, struct app *a);
/* Check every declared file CRC over decrypted data. Updates state and safe. */
void app_check(const struct fsx *fs, struct app *a);
/* CRC16 of `len` decrypted bytes at area-relative offset `rel`. Returns 0 and
 * stores the CRC, or -1 on a read error / out-of-range request. */
int area_crc(const struct fsx *fs, u32 base, u32 rel, u32 len, u16 *out);
/* Decrypted read of area bytes (any alignment, len <= 512). */
int area_read(const struct fsx *fs, u32 base, u32 rel, void *dst, u32 len);

#endif
