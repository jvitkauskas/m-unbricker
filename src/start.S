/* SPDX-License-Identifier: MIT */
/* Entry: the ROM calls the first byte of the image like a C function
 * (r0 -> its API table, unused). Interrupts off, our own stacks (well above
 * the image, below the retained area at 0x01C7FD80), then C. Never returns. */
	.section .text.start, "ax"
	.global rk_start
rk_start:
	cli
	sp = rk_stack_top
	ssp = rk_sstack_top
	call rk_main
	goto .
