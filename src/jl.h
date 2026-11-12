/* SPDX-License-Identifier: MIT */
/* JieLi UBOOT1.00 (mask ROM download mode) and M-Flasher's flash loader:
 * a C port of the Cardputer app's jl.cpp (itself after M-Flasher's jl.rs). */
#ifndef RK_JL_H
#define RK_JL_H
#include "mbl.h"

struct jl_info {
	char vendor[9], product[17];
	char loader[4];
	u32 loader_addr;
	u8 mhz;
	u16 chip_key;
	u32 flash_id;
	u32 io;
	u8 uid[8];
	u32 retries;
};

u16 crc16_update(u16 crc, const u8 *d, u32 n);
u32 crc32_update(u32 crc, const u8 *d, u32 n);

/* INQUIRY, upload and start the loader, identity. 0 = OK, else the step. */
int jl_start(struct jl_info *info);
int jl_read(u32 addr, u8 *buf, u32 n);
int jl_crc32(u32 addr, u32 n, u32 *crc);
int jl_erase(u32 addr, u32 n);
int jl_program(u32 addr, const u8 *data, u32 n);
int jl_ping(void);
int jl_restart(void);
extern const char *jl_err;

#endif
