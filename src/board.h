/* SPDX-License-Identifier: MIT */
#ifndef RK_BOARD_H
#define RK_BOARD_H
#include "disp.h" /* BRD_FM1, BRD_SMK */

/* This unit's board from its own flash (src/board.c): BRD_FM1, BRD_SMK, or 0
 * if it isn't known. The flash chip's JEDEC ID goes to *jedec if given. */
u32 board_detect(u32 *jedec);

#if BOARD_ID == 0
/* The build that runs on both: the board found at the start, and the keys of
 * src/mbl/keys.c, built once for each board (src/keys_any.c). */
extern u32 rk_board;
#endif
#endif
