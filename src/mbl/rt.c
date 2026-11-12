/* SPDX-License-Identifier: MIT */
/* The few library routines freestanding code generation may call. Written as
 * plain loops (and built with -fno-builtin) so they cannot call themselves. */
#include "mbl.h"

void *memcpy(void *d, const void *s, unsigned long n)
{
	u8 *dp = d;
	const u8 *sp = s;

	while (n--)
		*dp++ = *sp++;
	return d;
}

void *memmove(void *d, const void *s, unsigned long n)
{
	u8 *dp = d;
	const u8 *sp = s;

	if (dp < sp) {
		while (n--)
			*dp++ = *sp++;
	} else {
		dp += n;
		sp += n;
		while (n--)
			*--dp = *--sp;
	}
	return d;
}

void *memset(void *d, int c, unsigned long n)
{
	u8 *dp = d;

	while (n--)
		*dp++ = (u8)c;
	return d;
}

int memcmp(const void *a, const void *b, unsigned long n)
{
	const u8 *ap = a, *bp = b;

	while (n--) {
		if (*ap != *bp)
			return *ap - *bp;
		ap++;
		bp++;
	}
	return 0;
}
