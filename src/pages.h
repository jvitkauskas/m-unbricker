/* SPDX-License-Identifier: MIT */
/* Every page's picture, title, text and key hint, in one place: the app
 * (main.c) and the preview (tools/preview.c) both use this table. Titles
 * must fit one line (about 18 characters), bodies three. */
#ifndef RK_PAGES_H
#define RK_PAGES_H
#include "ui.h"
#include "art.h"

#if BOARD_ID == 2 /* this unit is an SMK-37 */
#define SELF(name) PIC_##name##_smk37
#define K_GO_NAME "SEQ PLAY"
#define K_NO_NAME "<"
#else
#define SELF(name) PIC_##name##_fm1
#define K_GO_NAME "PLAY"
#define K_NO_NAME "OCT−"
#endif
#define HINT_NEXT K_GO_NAME ": next"
#define HINT_AGAIN K_GO_NAME ": try again"
#define HINT_RESTART K_GO_NAME ": restart me"
#define HINT_REPAIR K_GO_NAME ": repair  ·  " K_NO_NAME ": leave it"

#define PAGE(name, pic, accent, title, body, hint) static const struct page name = { pic, accent, title, body, hint };

/* the way in */
PAGE(PG_welcome, SELF(welcome), C_ACCENT, "M-Unbricker", "I'll bring a broken FM-1 or SMK-37 back to life.", HINT_NEXT)
PAGE(PG_unplug, SELF(unplug_pc), C_ACCENT, "Unplug me", "Pull the cable out of the phone or computer. Leave it in me.", HINT_NEXT)
PAGE(PG_connect, SELF(connect), C_ACCENT, "Connect it", "Plug the cable into the broken unit. Keep it switched off for now.", HINT_NEXT)
PAGE(PG_switch_on, PIC_switch_on, C_ACCENT, "Switch it on", "Switch the broken unit on now. I'll wake it up.", 0)
PAGE(PG_already_on, PIC_retry, C_ACCENT, "Switch it off first", "It's already running. Switch it off, then press the key.", HINT_AGAIN)
PAGE(PG_no_answer, PIC_retry, C_ACCENT, "No answer", "Check the cable in both units. Switch it off, then press the key.", HINT_AGAIN)
PAGE(PG_waking, PIC_waking, C_ACCENT, "Waking it up", "This takes a few seconds.", 0)
PAGE(PG_connecting, PIC_waking, C_ACCENT, "Connecting", "Talking to it…", 0)

/* found, the repair */
PAGE(PG_found_fm1, PIC_found_fm1, C_INFO, "Found an FM-1", "Checking what needs repairing…", 0)
PAGE(PG_found_smk37, PIC_found_smk37, C_INFO, "Found an SMK-37", "Checking what needs repairing…", 0)
PAGE(PG_ready_fm1, PIC_press_play, C_ACCENT, "Ready to repair", "A fresh bootloader. Its old firmware and settings are erased.", HINT_REPAIR)
PAGE(PG_ready_smk37, PIC_press_play, C_ACCENT, "Ready to repair", "A fresh bootloader. Old firmware is erased; calibration stays.", HINT_REPAIR)
PAGE(PG_nothing, PIC_press_play, C_GOOD, "Nothing to repair", "It's already waiting for a firmware. Restart it?", HINT_REPAIR)
PAGE(PG_repairing, PIC_repairing, C_ACCENT, "Repairing", "Keep both units switched on and connected.", 0)
PAGE(PG_done_fm1, PIC_success_fm1, C_GOOD, "Done!", "It's in MIDI mode now, ready for a firmware from your phone or computer.", HINT_RESTART)
PAGE(PG_done_smk37, PIC_success_smk37, C_GOOD, "Done!", "It's in MIDI mode now, ready for a firmware from your phone or computer.", HINT_RESTART)
PAGE(PG_left, PIC_leave, C_INFO, "Left as it was", "Nothing was written. You can switch it off.", HINT_RESTART)

/* trouble */
PAGE(PG_err_connect, PIC_error, C_BAD, "It didn't connect", "Switch it off, then press the key to start again.", HINT_AGAIN)
PAGE(PG_err_other, PIC_error, C_BAD, "That's not it", "Something else answered. Use a plain USB-C cable, no hub.", HINT_AGAIN)
PAGE(PG_err_unknown, PIC_error, C_BAD, "Unknown unit", "Not an FM-1 or SMK-37 I know. Nothing was written.", HINT_RESTART)
PAGE(PG_err_image, PIC_error, C_BAD, "Damaged program", "My copy of the repair is damaged. Load m-unbricker again.", HINT_RESTART)
PAGE(PG_err_read, PIC_error, C_BAD, "Reading stopped", "Switch it off, then press the key to start again.", HINT_AGAIN)
PAGE(PG_err_write, PIC_error, C_BAD, "Repair stopped", "Run it again: switch it off, then press the key.", HINT_AGAIN)
PAGE(PG_err_timer, PIC_error, C_BAD, "Can't run here", "This unit's timer doesn't run. Restart it.", 0)

#endif
