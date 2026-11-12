/* SPDX-License-Identifier: MIT */
/* Common types and small helpers shared by the bootloader (A), the flash
 * loader (B), the host tool and the host tests. */
#ifndef MBL_H
#define MBL_H

typedef unsigned char u8;
typedef unsigned short u16;
typedef unsigned int u32;
typedef signed int s32;

#define NULL ((void *)0)

/* Retained boot / update / application communication area, SPEC.md 6.1 */
#define RET_BASE 0x01C7FD80u
#define ARRAY_LEN(a) ((u32)(sizeof(a) / sizeof((a)[0])))

/* Little-endian accessors: byte-wise, so alignment never matters. */
static inline u16 ld16(const u8 *p) { return (u16)(p[0] | (p[1] << 8)); }
static inline u32 ld32(const u8 *p)
{
	return (u32)p[0] | ((u32)p[1] << 8) | ((u32)p[2] << 16) | ((u32)p[3] << 24);
}
static inline void st16(u8 *p, u32 v)
{
	p[0] = (u8)v;
	p[1] = (u8)(v >> 8);
}
static inline void st32(u8 *p, u32 v)
{
	p[0] = (u8)v;
	p[1] = (u8)(v >> 8);
	p[2] = (u8)(v >> 16);
	p[3] = (u8)(v >> 24);
}
/* Big-endian, for the loader's command protocol. */
static inline u16 bd16(const u8 *p) { return (u16)((p[0] << 8) | p[1]); }
static inline u32 bd32(const u8 *p)
{
	return ((u32)p[0] << 24) | ((u32)p[1] << 16) | ((u32)p[2] << 8) | (u32)p[3];
}
static inline void bs16(u8 *p, u32 v)
{
	p[0] = (u8)(v >> 8);
	p[1] = (u8)v;
}
static inline void bs32(u8 *p, u32 v)
{
	p[0] = (u8)(v >> 24);
	p[1] = (u8)(v >> 16);
	p[2] = (u8)(v >> 8);
	p[3] = (u8)v;
}

/* Pointer to a CPU RAM address. On the target it is the address itself; the
 * host tests map the few fixed RAM windows onto arrays. */
#ifdef HOST_TEST
u8 *ram_ptr(u32 addr);
#define RAM_PTR(a) ram_ptr(a)
#else
#define RAM_PTR(a) ((u8 *)(a))
#endif

#endif
