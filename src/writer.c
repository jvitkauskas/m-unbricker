/* SPDX-License-Identifier: MIT */
/* Rescue writer, after the Cardputer app's writer.cpp. The images come from
 * rescue_img.h: boot area as is, app area as stored blocks plus regenerated
 * padding (0xFF encrypted with the chip key per 32-byte block). */
#include "writer.h"
#include "jl.h"
#include "rescue_img.h"

#define BOOT_AREA 0x4000u
#define FLASH_SIZE 0x100000u
#define NONE 0xFFFFFFFFu

const char *wr_err = "";
u32 wr_err_addr;

static const struct image images[2] = {
	{ "FM-1", FM1_BOOT, FM1_LIT, FM1_RUNS, sizeof FM1_RUNS / sizeof FM1_RUNS[0], FM1_APP_LEN,
	  FM1_BOOT_CRC, FM1_APP_CRC, FM1_KEEP_LO, FM1_KEEP_HI },
	{ "SMK-37", SMK37_BOOT, SMK37_LIT, SMK37_RUNS, sizeof SMK37_RUNS / sizeof SMK37_RUNS[0], SMK37_APP_LEN,
	  SMK37_BOOT_CRC, SMK37_APP_CRC, SMK37_KEEP_LO, SMK37_KEEP_HI },
};

const struct image *image_get(int which) { return &images[which == IMG_SMK37]; }

static u8 tgt[SECTOR] __attribute__((aligned(4)));
static u8 rd[SECTOR] __attribute__((aligned(4)));

/* 32 bytes of the app area at block b */
static void app_block(const struct image *img, u32 b, u8 *out)
{
	u32 i, r, key;

	for (r = 0; r < img->nruns; r++)
		if (b >= img->runs[r][0] && b < img->runs[r][0] + img->runs[r][1])
			break;
	if (r < img->nruns && img->runs[r][2] != NONE) {
		const u8 *s = img->lit + img->runs[r][2] + (b - img->runs[r][0]) * 32u;

		for (i = 0; i < 32; i++)
			out[i] = s[i];
		return;
	}
	key = (IMG_KEY ^ ((b * 32u) >> 2)) & 0xFFFF;
	for (i = 0; i < 32; i++) {
		out[i] = (u8)(0xFF ^ key);
		key = ((key << 1) ^ ((key & 0x8000) ? 0x1021 : 0)) & 0xFFFF;
	}
}

/* The image's bytes for sector s */
static const u8 *target(const struct image *img, u32 s)
{
	u32 a = s * SECTOR, i;

	if (a < BOOT_AREA)
		return img->boot + a;
	if (a < BOOT_AREA + img->app_len) {
		for (i = 0; i < SECTOR / 32u; i++)
			app_block(img, (a - BOOT_AREA) / 32u + i, tgt + 32u * i);
		return tgt;
	}
	for (i = 0; i < SECTOR; i++)
		tgt[i] = 0xFF;
	return tgt;
}

static int is_blank(const u8 *d)
{
	u32 i;

	for (i = 0; i < SECTOR; i++)
		if (d[i] != 0xFF)
			return 0;
	return 1;
}

static int is_protected(const struct image *img, u32 s)
{
	u32 a = s * SECTOR;

	return a >= W_END || (a >= img->keep_lo && a < img->keep_hi);
}

static u32 target_crc(const struct image *img, u32 first, u32 count)
{
	u32 crc = 0, s;

	for (s = first; s < first + count; s++)
		crc = crc32_update(crc, target(img, s), SECTOR);
	return crc;
}

int image_ok(const struct image *img)
{
	u32 crc = 0, s;

	if (crc32_update(0, img->boot, BOOT_AREA) != img->boot_crc)
		return 0;
	for (s = BOOT_AREA / SECTOR; s < (BOOT_AREA + img->app_len) / SECTOR; s++)
		crc = crc32_update(crc, target(img, s), SECTOR);
	return crc == img->app_crc;
}

static int same(const u8 *a, const u8 *b)
{
	u32 i;

	for (i = 0; i < SECTOR; i++)
		if (a[i] != b[i])
			return 0;
	return 1;
}

int plan_make(const struct image *img, struct plan *p, progress_fn progress)
{
	u32 s;
	int prev = 0, change;

	p->to_write = p->to_erase = p->runs = 0;
	for (s = 0; s < SECTORS; s++) {
		change = 0;
		if (!is_protected(img, s)) {
			if (!jl_read(s * SECTOR, rd, SECTOR)) {
				wr_err = "READ";
				wr_err_addr = s * SECTOR;
				return 0;
			}
			change = !same(rd, target(img, s));
		}
		p->change[s] = (u8)change;
		if (change) {
			if (is_blank(target(img, s)))
				p->to_erase++;
			else
				p->to_write++;
			if (!prev)
				p->runs++;
		}
		prev = change;
		if (s % 16 == 15)
			progress(s + 1, SECTORS);
	}
	return 1;
}

static int do_run(const struct image *img, u32 first, u32 count)
{
	u32 s, chip;
	const u8 *t;

	wr_err_addr = first * SECTOR;
	if (!jl_erase(first * SECTOR, count * SECTOR)) {
		wr_err = "ERASE";
		return 0;
	}
	for (s = first; s < first + count; s++) {
		t = target(img, s);
		if (!is_blank(t) && !jl_program(s * SECTOR, t, SECTOR)) {
			wr_err = "WRITE";
			wr_err_addr = s * SECTOR;
			return 0;
		}
	}
	if (!jl_crc32(first * SECTOR, count * SECTOR, &chip)) {
		wr_err = "CRC";
		return 0;
	}
	if (chip != target_crc(img, first, count)) {
		wr_err = "VERIFY";
		return 0;
	}
	return 1;
}

static int check_written(const struct image *img, u32 lo, u32 hi)
{
	u32 chip;

	wr_err_addr = lo;
	if (!jl_crc32(lo, hi - lo, &chip)) {
		wr_err = "FINAL CRC";
		return 0;
	}
	if (chip != target_crc(img, lo / SECTOR, (hi - lo) / SECTOR)) {
		wr_err = "FINAL CHECK";
		return 0;
	}
	return 1;
}

int plan_apply(const struct image *img, const struct plan *p, progress_fn progress)
{
	int keep = img->keep_hi > img->keep_lo;
	u32 kept = 0, top = 0, crc, total = p->to_write + p->to_erase, done = 0, s, first, count;

	if ((keep && !jl_crc32(img->keep_lo, img->keep_hi - img->keep_lo, &kept)) ||
	    !jl_crc32(W_END, FLASH_SIZE - W_END, &top)) {
		wr_err = "CRC BEFORE";
		return 0;
	}
	progress(0, total);
	for (s = 0; s < SECTORS;) {
		if (!p->change[s]) {
			s++;
			continue;
		}
		first = s;
		while (s < SECTORS && p->change[s])
			s++;
		count = s - first;
		if (!do_run(img, first, count) && !do_run(img, first, count))
			return 0; /* not verified twice */
		done += count;
		progress(done, total);
	}
	if (keep) {
		if (!check_written(img, 0, img->keep_lo) || !check_written(img, img->keep_hi, W_END))
			return 0;
		if (!jl_crc32(img->keep_lo, img->keep_hi - img->keep_lo, &crc) || crc != kept) {
			wr_err = "PROTECTED CHANGED";
			wr_err_addr = img->keep_lo;
			return 0;
		}
	} else if (!check_written(img, 0, W_END)) {
		return 0;
	}
	if (!jl_crc32(W_END, FLASH_SIZE - W_END, &crc) || crc != top) {
		wr_err = "PROTECTED CHANGED";
		wr_err_addr = W_END;
		return 0;
	}
	return 1;
}
