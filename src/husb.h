/* SPDX-License-Identifier: MIT */
/* USB0 in host mode, full speed, polled. One device, control + bulk only. */
#ifndef RK_HUSB_H
#define RK_HUSB_H
#include "mbl.h"

enum husb_res { HU_OK, HU_STALL, HU_TIMEOUT, HU_ERROR, HU_GONE };

/* Controller into host mode (the pins stay with the controller afterwards). */
void husb_init(void);
/* 1 once a full-speed device is attached (D+ pulled up), 0 on timeout. */
int husb_wait_attach(u32 timeout_ms);
int husb_attached(void);
/* Bus reset (~50 ms SE0), then the bus runs (SOFs). */
void husb_bus_reset(void);
/* Control transfer to `addr` (endpoint 0, max packet `mps0`). The direction
 * and wLength come from `setup`; *len gets the bytes moved in the data stage. */
enum husb_res husb_control(u8 addr, u8 mps0, const u8 setup[8], u8 *data, u32 *len);
/* Configures the bulk pipes: device address, IN and OUT endpoint numbers
 * (without the direction bit), max packet size. Resets both toggles. */
void husb_bulk_setup(u8 addr, u8 ep_in, u8 ep_out, u16 mps);
/* Bulk OUT of len bytes; bulk IN of up to len bytes (stops at a short
 * packet), *got = bytes received. */
enum husb_res husb_bulk_out(const u8 *buf, u32 len, u32 timeout_ms);
enum husb_res husb_bulk_in(u8 *buf, u32 len, u32 *got, u32 timeout_ms);
/* Data toggle back to DATA0 (after CLEAR_FEATURE(ENDPOINT_HALT)). */
void husb_reset_toggle(int in);
/* Last error detail for the screen (register snapshot). */
extern u32 husb_dbg[4];

#endif
