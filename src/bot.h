/* SPDX-License-Identifier: MIT */
/* Enumeration of the ROM's download-mode disk and USB mass storage
 * bulk-only transport with JieLi's 16-byte commands. */
#ifndef RK_BOT_H
#define RK_BOT_H
#include "mbl.h"

enum bot_res { B_OK, B_REFUSED, B_FAILED };

struct dev_info {
	u16 vid, pid;
	u8 mps0, iface, ep_in, ep_out;
	u16 mps;
};

/* Reset, address 1, descriptors, configuration, bulk endpoints. 0 = OK,
 * else the step that failed (1..6) and *res the host result. */
int dev_enumerate(struct dev_info *d, int *res);

/* One command: CBW, data (IN or OUT, may be 0), CSW. *moved = data bytes. */
enum bot_res bot_execute(const u8 *cdb, u32 cdb_len, u8 *buf, u32 len, int in, u32 *moved);
extern const char *bot_err;

#endif
