/* SPDX-License-Identifier: MIT */
/* A simulated AC7911B for host tests: the register ranges 0x10000-0x5FFFF are mapped with no access at their real
 * addresses, every access faults and is modelled (tests/chip/README.md). Callable from C and from Rust (extern "C").
 * The program must be built non-PIE, and must access registers as plain volatile 32-bit loads and stores. */
#ifndef CHIP_H
#define CHIP_H

#ifdef __cplusplus
extern "C" {
#endif

/* Map the registers, reset the models and configure them for `scenario` (a name from the table in chip.c). Exits
 * with status 2 for an unknown name. */
void chip_start(const char *scenario);
/* A line "-- text" in the trace, and "-- text @<access count>" in the summary. */
void chip_mark(const char *text);
/* The same, with the panel's framebuffer hash: "-- text fb=<16 hex>". With env CHIP_PANEL_DIR=<dir>, the framebuffer
 * is also written to <dir>/<text>.raw (240x240 RGB565, big endian, as tools/preview.c writes it). */
void chip_mark_panel(const char *text);
/* Stop and print the summary to stdout. */
void chip_finish(void);

#ifdef __cplusplus
}
#endif
#endif
