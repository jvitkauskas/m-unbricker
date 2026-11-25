/* SPDX-License-Identifier: MIT */
/* The C's image packing (src/writer.c at bd5ea96) on this computer, for tests/images_test.py: the same output as
 * tests/images.rs. Built with the C's src/jl.c, against the rescue_img.h of the C's tools/make_images.py. */
#include <stdio.h>
#include "bot.h"
#include "writer.c"

/* writer.c's loader (jl.c, which has the CRC-32 it uses) isn't used here: what jl.c calls, stubbed. */
const char *bot_err = "";
u32 tb_tps = 48000000u;
u32 tb_now(void) { return 0; }
u32 tb_ms_since(u32 t0) { (void)t0; return 1000000u; }
enum bot_res bot_execute(const u8 *cdb, u32 cdb_len, u8 *buf, u32 len, int in, u32 *moved)
{
	(void)cdb, (void)cdb_len, (void)buf, (void)len, (void)in, (void)moved;
	return B_FAILED;
}

int main(void)
{
	static const char *names[2] = { "FM-1", "SMK-37" };
	u32 s;
	int w;

	for (w = 0; w < 2; w++) {
		const struct image *img = image_get(w);

		printf("%s ok %d\n", names[w], image_ok(img));
		for (s = 0; s < SECTORS; s++)
			printf("%u %08x %d\n", s, crc32_update(0, target(img, s), SECTOR), is_protected(img, s));
	}
	return 0;
}
