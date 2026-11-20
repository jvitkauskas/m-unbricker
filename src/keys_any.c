/* SPDX-License-Identifier: MIT */
/* The keys for the build that runs on both boards: src/mbl/keys.c is built
 * twice, as fm1_plat_* and smk_plat_* (Makefile), and this picks one. */
#include "boot.h"
#include "board.h"

u32 rk_board;

u32 fm1_plat_boot_keys(void);
u32 fm1_plat_menu_scan(void);
void fm1_plat_keys_off(void);
u32 smk_plat_boot_keys(void);
u32 smk_plat_menu_scan(void);
void smk_plat_keys_off(void);

u32 plat_boot_keys(void) { return rk_board == BRD_SMK ? smk_plat_boot_keys() : fm1_plat_boot_keys(); }
u32 plat_menu_scan(void) { return rk_board == BRD_SMK ? smk_plat_menu_scan() : fm1_plat_menu_scan(); }
void plat_keys_off(void)
{
	if (rk_board == BRD_SMK)
		smk_plat_keys_off();
	else
		fm1_plat_keys_off();
}
