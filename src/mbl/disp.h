/* SPDX-License-Identifier: MIT */
/* 240x240 ST7789 panel on SPI1, 20x15 text cells of 12x16 pixels
 * (SPEC.md 8.4). Shared by the bootloader (one board per build) and the
 * loader (board chosen at run time). */
#ifndef MBL_DISP_H
#define MBL_DISP_H
#include "mbl.h"

#define BRD_FM1 1
#define BRD_SMK 2

#define COLS 20u
#define ROWS 15u

#define RGB(r, g, b) ((u16)((((r) >> 3) << 11) | (((g) >> 2) << 5) | ((b) >> 3)))
#define C_BLACK 0x0000
#define C_WHITE 0xFFFF
#define C_YELLOW RGB(255, 220, 0)
#define C_GREEN RGB(0, 220, 0)
#define C_RED RGB(255, 40, 40)
#define C_GREY RGB(150, 150, 150)

/* Initialise the panel for `board` (BRD_*). Returns 1 on success, 0 for an
 * unknown board (no pin is touched in that case). */
int disp_init(u32 board);
/* Draw `n` (<= 20) characters on a row from column 0 and pad the rest of the
 * row with the background colour. Lower case is drawn as upper case, unknown
 * characters as blanks. */
void disp_text(u32 row, u32 fg, u32 bg, const u8 *s, u32 n);
void disp_fill(u32 x, u32 y, u32 w, u32 h, u32 color);
/* The five column bytes of a character's glyph (a blank for unknown ones). */
const u8 *disp_glyph(u32 c);

#endif
