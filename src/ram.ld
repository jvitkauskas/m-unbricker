/* SPDX-License-Identifier: MIT */
/* A RAM app (m-bootloader docs/SPEC.md 11.8): the file is linked for
 * 0x01C30000 and ends by 0x01C7FD80, trailer included. .bss and the stacks
 * live in 0x01C16800..0x01C20000, which no route uses; the app never returns
 * (it resets), so nothing of the caller's has to survive. */
OUTPUT_FORMAT("elf32-pi32v2")
ENTRY(rk_start)
MEMORY
{
	APP (rwx) : ORIGIN = 0x01C30000, LENGTH = 0x4FD80 - 8
	LOW (rw) : ORIGIN = 0x01C16800, LENGTH = 0x01C1E000 - 0x01C16800
}
SECTIONS
{
	. = ORIGIN(APP);
	.image : {
		KEEP(*(.text.start))
		*(.text .text.*)
		*(.rodata .rodata.* .srodata .srodata.*)
		*(.data .data.* .sdata .sdata.*)
		. = ALIGN(4);
	} > APP
	rk_image_end = .;
	.bss (NOLOAD) : {
		rk_bss_start = .;
		*(.bss .bss.* .sbss .sbss.* COMMON)
		. = ALIGN(4);
		rk_bss_end = .;
	} > LOW
	rk_stack_top = 0x01C1F800; /* 6 KiB above .bss's limit */
	rk_sstack_top = 0x01C20000; /* 2 KiB */
	/DISCARD/ : { *(.comment) *(.note*) *(.eh_frame*) }
}
