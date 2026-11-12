/* SPDX-License-Identifier: MIT */
/* The panel (src/lcd.c): 240x240, RGB565 big endian. */
#ifndef RK_LCD_H
#define RK_LCD_H
#include "disp.h" /* BRD_FM1, BRD_SMK, RGB() */

int lcd_init(u32 board);
void lcd_window(u32 x, u32 y, u32 w, u32 h); /* then pixels, then lcd_end */
void lcd_pixels(const u8 *p, u32 n);
void lcd_end(void);

#endif
