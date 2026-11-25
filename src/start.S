/* SPDX-License-Identifier: MIT */
/* Entry: the ROM, the card's launcher or a MIDI-mode route calls the first byte of the image like a C function, with
 * one argument (src/flow.rs). Interrupts off, this program's own stacks (below the image, after .bss: src/ram.ld), then
 * rk_main (src/main.rs). It never returns. */
	.section .text.start, "ax"
	.global rk_start
rk_start:
	cli
	sp = rk_stack_top
	ssp = rk_sstack_top
	call rk_main
	goto .
