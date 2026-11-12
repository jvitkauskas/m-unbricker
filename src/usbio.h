/* SPDX-License-Identifier: MIT */
/* Timebase, the USB pins as GPIO, the JieLi USB_KEY and the calibration
 * pulses, on the rescuing unit's own USB port. */
#ifndef RK_USBIO_H
#define RK_USBIO_H
#include "mbl.h"

/* ---- timebase: Timer 0 free-running at the LSB clock ---- */
void tb_start(void);
u32 tb_now(void);
/* Ticks per second: nominal until tb_calibrate_sof() measured it. */
extern u32 tb_tps;
extern u8 tb_measured;
u32 tb_us(u32 us);  /* ticks in `us` microseconds (us < 80 s) */
void tb_wait_until(u32 t);
u32 tb_ms_since(u32 t0);
/* Measure the timer against the PC's 1 ms USB frames (the ROM's USB is still
 * up). 1 = measured; 0 = no frames seen (tb_tps stays nominal). */
int tb_calibrate_sof(u32 frames, u32 *seen);

/* ---- USB pins ---- */
void usb_take_pins(void);   /* controller off, D+/D- GPIO inputs with pull-downs */
void usb_float_pins(void);  /* inputs, no pulls */
int pin_dp(void);
int pin_dm(void);

/* ---- key ---- */
enum key_result { KEY_ACK, KEY_DP_AT_START, KEY_DP_ANSWER, KEY_TIMEOUT };
struct key_stats {
	u32 packets, false_acks;
	u8 alive;
};
/* Sends 0x16EF until the ROM answers. `tick` is called about every 250 ms
 * (it may draw). */
enum key_result key_send(struct key_stats *s, u32 timeout_ms, void (*tick)(const struct key_stats *));

/* ---- calibration pulses ---- */
struct cal_result {
	u32 pulse_ms;  /* how long pulses were sent */
	s32 drop_ms;   /* when D+ dropped (calibration done), -1 if it didn't */
	u32 pulses;
};
void cal_pulses(struct cal_result *r);

#endif
