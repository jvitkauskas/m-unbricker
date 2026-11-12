/* SPDX-License-Identifier: MIT */
/* Port of the Cardputer app's jl.cpp (M-Flasher's jl.rs, MIT, after
 * jl-uboot-tool by kagaimiq, MIT). Commands are 16-byte CDBs: a 16-bit opcode,
 * big-endian arguments, padded with 0xFF. Loader replies are 16 bytes with
 * the opcode echoed. Read-only for now (no erase or write). */
#include "jl.h"
#include "bot.h"
#include "mflr_loader.h"
#include "usbio.h"

#define IDENTIFY 0xFC40
#define CRC32 0xFC41
#define UNIQUE_ID 0xFC42
#define WRITE_MEMORY 0xFB06
#define JUMP 0xFB08
#define READ_FLASH 0xFD05
#define READ_KEY 0xFC09
#define ONLINE_DEVICE 0xFC0A
#define USB_BUFFER_SIZE 0xFC14
#define LOADER_ARG 1
#define RETRIES 4
#define READ_IO 16384u

const char *jl_err = "";
static struct jl_info *cur;

static void wait_ms(u32 ms)
{
	u32 t = tb_now();

	while (tb_ms_since(t) < ms)
		;
}

static void copy(u8 *d, const u8 *s, u32 n)
{
	while (n--)
		*d++ = *s++;
}

u16 crc16_update(u16 crc, const u8 *d, u32 n)
{
	u32 i;
	int k;

	for (i = 0; i < n; i++) {
		crc ^= (u16)(d[i] << 8);
		for (k = 0; k < 8; k++)
			crc = (crc & 0x8000) ? (u16)((crc << 1) ^ 0x1021) : (u16)(crc << 1);
	}
	return crc;
}

static u32 crc_tab[256];

u32 crc32_update(u32 crc, const u8 *d, u32 n)
{
	u32 i, c;
	int k;

	if (!crc_tab[1])
		for (i = 0; i < 256; i++) {
			c = i;
			for (k = 0; k < 8; k++)
				c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
			crc_tab[i] = c;
		}
	crc = ~crc;
	for (i = 0; i < n; i++)
		crc = crc_tab[(crc ^ d[i]) & 0xFF] ^ (crc >> 8);
	return ~crc;
}

static void crc_cipher(const u8 *in, u8 *out, u32 n, u32 key)
{
	static const u8 MAGIC[16] = { 0xC3, 0xCF, 0xC0, 0xE8, 0xCE, 0xD2, 0xB0, 0xAE,
				      0xC4, 0xE3, 0xA3, 0xAC, 0xD3, 0xF1, 0xC1, 0xD6 };
	u8 hi[2] = { (u8)(key >> 16), (u8)(key >> 24) };
	u16 c = crc16_update((u16)key, hi, 2);
	u32 i;

	for (i = 0; i < n; i++) {
		c = crc16_update(c, &MAGIC[i % 16], 1);
		out[i] = in[i] ^ (u8)c;
	}
}

static void cdb(u8 *out, u16 cmd, const u8 *args, u32 n)
{
	u32 i;

	for (i = 0; i < 16; i++)
		out[i] = 0xFF;
	out[0] = (u8)(cmd >> 8);
	out[1] = (u8)cmd;
	copy(out + 2, args, n);
}

static enum bot_res attempts(const u8 *c, u8 *buf, u32 len, int in, u32 *moved, int refusal_answers)
{
	enum bot_res r = B_FAILED;
	int a;

	for (a = 1; a <= RETRIES; a++) {
		r = bot_execute(c, 16, buf, len, in, moved);
		if (r == B_OK || (r == B_REFUSED && refusal_answers))
			return r;
		if (a < RETRIES) {
			if (cur)
				cur->retries++;
			wait_ms(250u * a);
		}
	}
	return r;
}

static enum bot_res call16(u16 cmd, const u8 *args, u32 n, u8 *resp, int probe)
{
	u8 c[16];
	u32 moved, i;
	enum bot_res r;

	cdb(c, cmd, args, n);
	for (i = 0; i < 16; i++)
		resp[i] = 0;
	r = attempts(c, resp, 16, 1, &moved, probe);
	if (r != B_OK)
		return r;
	if (bd16(resp) != cmd) {
		jl_err = "UNEXPECTED ANSWER";
		return probe ? B_REFUSED : B_FAILED;
	}
	return B_OK;
}

static int identify(char *ver, u32 *at, u8 *mhz)
{
	u8 r[16];

	if (call16(IDENTIFY, 0, 0, r, 1) != B_OK)
		return 0;
	if (r[2] != 'M' || r[3] != 'F' || r[4] != 'L' || r[5] != 'R')
		return 0;
	ver[0] = (char)r[6];
	ver[1] = (char)r[7];
	ver[2] = (char)r[8];
	ver[3] = 0;
	*at = bd32(r + 10);
	if (mhz)
		*mhz = r[15];
	return 1;
}

static int upload(u32 addr, const u8 *img, u32 n)
{
	u8 block[512], c[16], args[9];
	u32 off, k, moved;
	u16 crc;

	for (off = 0; off < n; off += 512) {
		k = n - off < 512 ? n - off : 512;
		crc_cipher(img + off, block, k, 0xFFFFFFFFu);
		crc = crc16_update(0, block, k);
		bs32(args, addr + off);
		args[4] = (u8)(k >> 8);
		args[5] = (u8)k;
		args[6] = 0;
		args[7] = (u8)crc;
		args[8] = (u8)(crc >> 8);
		cdb(c, WRITE_MEMORY, args, 9);
		if (attempts(c, block, k, 0, &moved, 0) != B_OK)
			return 0;
	}
	return 1;
}

static int jump(u32 addr)
{
	u8 args[6], c[16], r[16];
	u32 moved, at;
	char v[4];
	int a;

	bs32(args, addr);
	args[4] = 0;
	args[5] = LOADER_ARG;
	cdb(c, JUMP, args, 6);
	for (a = 1; a <= 3; a++) {
		if (bot_execute(c, 16, r, 16, 1, &moved) == B_OK && bd16(r) == JUMP) {
			wait_ms(100);
			return 1;
		}
		wait_ms(250u * a);
		if (identify(v, &at, 0) && at == addr)
			return 1;
	}
	return 0;
}

int jl_start(struct jl_info *info)
{
	u8 inq[36], c6[6] = { 0x12, 0, 0, 0, 36, 0 }, resp[16];
	u8 key_args[4] = { 0x00, 0xAC, 0x69, 0x00 }, enc[2], plain[2];
	u32 got = 0, i, at, io;
	enum bot_res r = B_FAILED;
	char ver[4];

	cur = info;
	for (i = 1; i <= 3; i++) {
		r = bot_execute(c6, 6, inq, 36, 1, &got);
		if (r == B_OK && got >= 32)
			break;
		r = B_FAILED;
		wait_ms(250u * i);
	}
	if (r != B_OK) {
		jl_err = bot_err;
		return 1;
	}
	copy((u8 *)info->vendor, inq + 8, 8);
	info->vendor[8] = 0;
	copy((u8 *)info->product, inq + 16, 16);
	info->product[16] = 0;
	for (i = 8; i-- && info->vendor[i] == ' ';)
		info->vendor[i] = 0;
	for (i = 16; i-- && info->product[i] == ' ';)
		info->product[i] = 0;
	if (!identify(ver, &at, 0)) {
		if (call16(ONLINE_DEVICE, 0, 0, resp, 1) == B_OK) {
			jl_err = "ANOTHER LOADER RUNS";
			return 2;
		}
	}
	if (!upload(MFLR_ADDR_A, MFLR_A, sizeof MFLR_A)) {
		jl_err = "LOADER UPLOAD";
		return 3;
	}
	if (!jump(MFLR_ADDR_A) || !identify(info->loader, &info->loader_addr, &info->mhz)) {
		jl_err = "LOADER DID NOT START";
		return 4;
	}
	if (call16(READ_KEY, key_args, 4, resp, 0) != B_OK) {
		jl_err = "CHIP KEY";
		return 5;
	}
	enc[0] = resp[7];
	enc[1] = resp[6];
	crc_cipher(enc, plain, 2, 0xFFFFFFFFu);
	info->chip_key = (u16)(plain[0] | (plain[1] << 8));
	if (call16(ONLINE_DEVICE, 0, 0, resp, 0) != B_OK) {
		jl_err = "FLASH ID";
		return 6;
	}
	info->flash_id = ld32(resp + 4);
	info->io = 512;
	if (call16(USB_BUFFER_SIZE, 0, 0, resp, 0) == B_OK) {
		io = bd32(resp + 2);
		info->io = io < 256 ? 256 : io > 4096 ? 4096 : io;
	}
	if (call16(UNIQUE_ID, 0, 0, resp, 0) == B_OK)
		copy(info->uid, resp + 2, 8);
	return 0;
}

int jl_read(u32 addr, u8 *buf, u32 n)
{
	u32 done = 0, k, moved;
	u8 args[6], c[16];
	int a, ok;

	while (done < n) {
		k = n - done < READ_IO ? n - done : READ_IO;
		bs32(args, addr + done);
		args[4] = (u8)(k >> 8);
		args[5] = (u8)k;
		cdb(c, READ_FLASH, args, 6);
		ok = 0;
		for (a = 1; a <= RETRIES && !ok; a++) {
			moved = 0;
			if (bot_execute(c, 16, buf + done, k, 1, &moved) == B_OK && moved == k) {
				ok = 1;
			} else if (a < RETRIES) {
				if (cur)
					cur->retries++;
				wait_ms(250u * a);
			}
		}
		if (!ok) {
			jl_err = "READ";
			return 0;
		}
		done += k;
	}
	return 1;
}

int jl_crc32(u32 addr, u32 n, u32 *crc)
{
	u8 args[8], resp[16];

	bs32(args, addr);
	bs32(args + 4, n);
	if (call16(CRC32, args, 8, resp, 0) != B_OK) {
		jl_err = "CRC32";
		return 0;
	}
	*crc = bd32(resp + 2);
	return 1;
}

/* ------------------------------------------------------------ writing */

#define ERASE_BLOCK 0xFB00
#define ERASE_SECTOR 0xFB01
#define WRITE_FLASH 0xFB04
#define MFLR_ADDR_B 0x01C28000u

/* Erases [addr, addr + n): 64 KiB blocks where aligned, else 4 KiB sectors.
 * A failed erase is sent again (erasing twice is harmless). */
int jl_erase(u32 addr, u32 n)
{
	u32 a = addr, end = addr + n;
	u8 args[4], resp[16];
	int block;

	while (a < end) {
		block = a % 0x10000u == 0 && end - a >= 0x10000u;
		bs32(args, a);
		if (call16(block ? ERASE_BLOCK : ERASE_SECTOR, args, 4, resp, 0) != B_OK)
			return 0;
		a += block ? 0x10000u : 0x1000u;
	}
	return 1;
}

/* Programs erased flash in chunks of the loader's buffer size; all-0xFF
 * chunks are skipped. The loader checks the CRC16 of the plain data. */
int jl_program(u32 addr, const u8 *data, u32 n)
{
	u32 io = cur && cur->io ? cur->io : 512, off, k, i, moved;
	u8 args[9], c[16];
	u16 crc;
	int blank;

	for (off = 0; off < n; off += io) {
		k = n - off < io ? n - off : io;
		blank = 1;
		for (i = 0; i < k && blank; i++)
			blank = data[off + i] == 0xFF;
		if (blank)
			continue;
		crc = crc16_update(0, data + off, k);
		bs32(args, addr + off);
		args[4] = (u8)(k >> 8);
		args[5] = (u8)k;
		args[6] = 0;
		args[7] = (u8)crc;
		args[8] = (u8)(crc >> 8);
		cdb(c, WRITE_FLASH, args, 9);
		if (attempts(c, (u8 *)data + off, k, 0, &moved, 0) != B_OK)
			return 0;
	}
	return 1;
}

/* A harmless command that keeps the session busy. */
int jl_ping(void)
{
	char v[4];
	u32 at;

	return identify(v, &at, 0);
}

/* M-Flasher's RESTART program: clears the download-mode marker at 0x01C7FD80
 * and requests a software reset (pi32v2-isa tools/hwtest/app/reset.S, MIT-0).
 * Uploaded to the slot our loader is not in; the answer may be lost. */
static const u8 RESTART_PROG[34] = {
	0xc4, 0xff, 0x80, 0xfd, 0xc7, 0x01, 0x41, 0x20, 0xc1, 0x60, 0xc1, 0x61, 0xc1, 0x62, 0xc1, 0x63,
	0x20, 0x00, 0x60, 0xe0, 0x80, 0x4b, 0x41, 0x60, 0x42, 0x30, 0x21, 0x19, 0xc1, 0x60, 0x20, 0x00,
	0xf7, 0x9f,
};

int jl_restart(void)
{
	u8 args[6], c[16], r[16];
	u32 moved;

	if (!upload(MFLR_ADDR_B, RESTART_PROG, sizeof RESTART_PROG))
		return 0;
	bs32(args, MFLR_ADDR_B);
	args[4] = 0;
	args[5] = LOADER_ARG;
	cdb(c, JUMP, args, 6);
	bot_execute(c, 16, r, 16, 1, &moved);
	return 1;
}
