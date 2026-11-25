// SPDX-License-Identifier: MIT
//! The timebase, the USB pins as GPIO, the JieLi USB_KEY and the calibration pulses, on the rescuing unit's own USB
//! port.
//!
//! Waveforms and thresholds follow the Cardputer app (cardputer-synth-rescue src/usbkey.cpp), which follows
//! FM-1-transporter and the Flipper app, all verified on real units:
//!   key 0x16EF, MSB first, D+ = clock, D- = data, push-pull, one packet per 340 us slot; ROM answer = D- held low,
//!   then D+ pulled up by the ROM;
//!   calibration = 4 us low on D+ every 1 ms, open drain, until D+ drops after >= 1 s or for 6 s.
//!
//! USB0 pins as GPIO (register 0x51000, bits from the AC79 SDK's gpio functions): out D+ 0 / D- 1, direction
//! (1 = input) 2 / 3, pull-down 4 / 5, pull-up 6 / 7, digital input 9 / 10, IO mode 8 + 11. Pin levels: 0x51004 bit 0 /
//! 1. Everything runs with interrupts off.
use crate::regs::{TMR0, TMR0_COUNT, TMR0_PERIOD, USB_CON0, USB_CON1, USB_IO, USB_IO_IN, bit, sync_cpu};
use mboot::Global;

const USB_SOF_PEND: u32 = bit(13);
const USB_SOF_CLR: u32 = bit(12);

const DP_OUT: u32 = bit(0);
const DM_OUT: u32 = bit(1);
const DP_DIR: u32 = bit(2);
const DM_DIR: u32 = bit(3);
const DP_PD: u32 = bit(4);
const DM_PD: u32 = bit(5);
const UIO_MODE: u32 = bit(8) | bit(11);
const UIO_DIE: u32 = bit(9) | bit(10);

// ---- the timebase: Timer 0 free-running at the LSB clock ----

/// Ticks per second: nominal (the loader state's LSB clock, 48 MHz, set at the start) until `calibrate_sof()` measured
/// it.
pub static TPS: Global<u32> = Global::new(0);
pub const NOMINAL_TPS: u32 = 48_000_000;

pub fn start() {
    TMR0.write(0x4000);
    TMR0_COUNT.write(0);
    TMR0_PERIOD.write(0xffff_ffff);
    TMR0.write(1);
    sync_cpu();
}

pub fn now() -> u32 {
    TMR0_COUNT.read()
}

/// Ticks in `us` microseconds (us < 80 s).
pub fn us(us: u32) -> u32 {
    let tpms = TPS.read() / 1000;
    (us / 1000).wrapping_mul(tpms).wrapping_add((us % 1000).wrapping_mul(tpms) / 1000)
}

pub fn wait_until(t: u32) {
    while (now().wrapping_sub(t) as i32) < 0 {}
}

pub fn ms_since(t0: u32) -> u32 {
    now().wrapping_sub(t0) / (TPS.read() / 1000)
}

pub fn wait_ms(ms: u32) {
    let t = now();
    while ms_since(t) < ms {}
}

/// For the panel and the keys.
pub fn delay_us(mut us_left: u32) {
    let mut t = now();
    while us_left != 0 {
        let n = us_left.min(1_000_000);
        t = t.wrapping_add(us(n));
        wait_until(t);
        us_left -= n;
    }
}

fn sof_wait(timeout: u32) -> bool {
    let t0 = now();
    while USB_CON0.read() & USB_SOF_PEND == 0 {
        if now().wrapping_sub(t0) > timeout {
            return false;
        }
    }
    USB_CON0.set(USB_SOF_CLR);
    true
}

/// Measures the timer against the PC's 1 ms USB frames (the ROM's USB is still up). True if measured; false if no
/// frames were seen (the rate stays nominal). `seen` gets the frames counted.
pub fn calibrate_sof(frames: u32, seen: &mut u32) -> bool {
    // 20 ms at the nominal clock
    let to = 48_000 * 20;
    *seen = 0;
    USB_CON0.set(USB_SOF_CLR);
    if !sof_wait(to) {
        return false;
    }
    let t0 = now();
    for n in 0..frames {
        if !sof_wait(to) {
            return false;
        }
        *seen = n + 1;
    }
    let t1 = now();
    if frames != 1000 {
        return false;
    }
    // ticks in 1000 frames = 1 s
    let t1 = t1.wrapping_sub(t0);
    if !(40_000_000..=56_000_000).contains(&t1) {
        return false;
    }
    TPS.set(t1);
    true
}

// ---- the USB pins ----

/// What 0x51000 holds: written whole each time.
static UIO: Global<u32> = Global::new(0);

fn uio_put(v: u32) {
    UIO.set(v);
    USB_IO.write(v);
}

/// The controller off, D+ and D- GPIO inputs with pull-downs: the host sees the unit go.
pub fn take_pins() {
    USB_CON0.write(0);
    USB_CON1.write(0);
    uio_put(UIO_MODE | UIO_DIE | DP_DIR | DM_DIR | DP_PD | DM_PD);
}

fn pin_dp() -> bool {
    USB_IO_IN.read() & 1 != 0
}

fn pin_dm() -> bool {
    USB_IO_IN.read() >> 1 & 1 != 0
}

// ---- the key ----

const USB_KEY: u32 = 0x16ef;
const KEY_SLOT_US: u32 = 340;
const GAP_PROBES: u32 = 4;
const GAP_PROBE_SPACING_US: u32 = 35;
const ACK_PROBE_LOWS: u32 = 2;
const DP_HIGH_TIMEOUT_MS: u32 = 500;

#[derive(Clone, Copy, PartialEq, Eq)]
pub enum KeyResult {
    Ack,
    DpAtStart,
    DpAnswer,
    Timeout,
}

#[derive(Clone, Copy, Default)]
pub struct KeyStats {
    pub packets: u32,
    pub false_acks: u32,
    pub alive: bool,
}

/// The key's short waits, in ticks: 2, 4, 8, 10 and 12 us.
#[derive(Clone, Copy)]
struct Waits {
    us2: u32,
    us4: u32,
    us8: u32,
    us10: u32,
    us12: u32,
}

/// ~323 us: both lines low; per bit clock low + data for 8 us, clock high for 12 us (10 us on the last bit); then both
/// released.
fn key_packet(w: &Waits) {
    let mut t = now();
    let mut uio = UIO.read() & !(DP_OUT | DM_OUT | DP_DIR | DM_DIR);
    uio_put(uio);
    t = t.wrapping_add(w.us4);
    wait_until(t);
    for bit in (0..16).rev() {
        if USB_KEY >> bit & 1 != 0 {
            uio = uio & !DP_OUT | DM_OUT;
        } else {
            uio &= !(DP_OUT | DM_OUT);
        }
        uio_put(uio);
        t = t.wrapping_add(w.us8);
        wait_until(t);
        // rising edge: the ROM latches D-
        uio |= DP_OUT;
        uio_put(uio);
        t = t.wrapping_add(if bit != 0 { w.us12 } else { w.us10 });
        wait_until(t);
    }
    uio &= !(DP_OUT | DM_OUT);
    uio |= DP_DIR | DM_DIR;
    uio_put(uio);
}

/// Drives D- high for 2 us and reads it: low only if something holds it down hard (the ROM's ACK; an unpowered unit
/// clamps it too).
fn probe_dm_high(w: &Waits) -> bool {
    uio_put((UIO.read() | DM_OUT) & !DM_DIR);
    wait_until(now().wrapping_add(w.us2));
    let v = pin_dm();
    uio_put((UIO.read() | DM_DIR) & !DM_OUT);
    v
}

/// What the gap after a packet showed.
#[derive(PartialEq, Eq)]
enum Gap {
    Nothing,
    /// D- held low.
    Ack,
    /// D+ already pulled up.
    DpHigh,
}

fn gap_sense(w: &Waits, alive: &mut bool) -> Gap {
    let (mut lows, mut dp_highs) = (0, 0);
    let mut t = now();
    for _ in 0..GAP_PROBES {
        t = t.wrapping_add(us(GAP_PROBE_SPACING_US));
        wait_until(t);
        if pin_dp() {
            dp_highs += 1;
            if dp_highs >= 2 {
                return Gap::DpHigh;
            }
        } else {
            dp_highs = 0;
        }
        if probe_dm_high(w) {
            *alive = true;
            lows = 0;
        } else if *alive {
            lows += 1;
            if lows >= ACK_PROBE_LOWS {
                return Gap::Ack;
            }
        }
    }
    Gap::Nothing
}

fn wait_dp_high(timeout_ms: u32) -> bool {
    let t0 = now();
    let mut t = t0;
    let mut highs = 0;
    while ms_since(t0) < timeout_ms {
        highs = if pin_dp() { highs + 1 } else { 0 };
        if highs >= 5 {
            return true;
        }
        t = t.wrapping_add(us(50));
        wait_until(t);
    }
    false
}

/// Sends 0x16EF until the ROM answers. `tick` is called about every 250 ms (it may draw).
pub fn key_send(s: &mut KeyStats, timeout_ms: u32, tick: &mut impl FnMut(&KeyStats)) -> KeyResult {
    let w = Waits { us2: us(2), us4: us(4), us8: us(8), us10: us(10), us12: us(12) };
    *s = KeyStats::default();
    take_pins();
    delay_us(5000);
    if pin_dp() {
        return KeyResult::DpAtStart;
    }
    let start = now();
    let mut last_tick = start;
    loop {
        let slot = now();
        key_packet(&w);
        s.packets += 1;
        wait_until(slot.wrapping_add(us(KEY_SLOT_US)));
        match gap_sense(&w, &mut s.alive) {
            Gap::DpHigh => return KeyResult::DpAnswer,
            Gap::Ack => {
                if wait_dp_high(DP_HIGH_TIMEOUT_MS) {
                    return KeyResult::Ack;
                }
                // D- low without the D+ pull-up: not an ACK
                s.false_acks += 1;
                s.alive = false;
            }
            Gap::Nothing => {}
        }
        if ms_since(start) > timeout_ms {
            take_pins();
            return KeyResult::Timeout;
        }
        if ms_since(last_tick) >= 250 {
            last_tick = now();
            tick(s);
        }
    }
}

// ---- the calibration pulses ----

const PULSE_MAX_MS: u32 = 6000;
const CAL_DONE_MIN_MS: u32 = 1000;
/// 300 us low: not one of our 4 us pulses.
const PULSE_LOW_SAMPLES: u32 = 3;

#[derive(Clone, Copy, Default)]
pub struct CalResult {
    /// How long pulses were sent.
    pub pulse_ms: u32,
    /// When D+ dropped (calibration done), or -1 if it didn't.
    pub drop_ms: i32,
    pub pulses: u32,
}

pub fn cal_pulses(r: &mut CalResult) {
    let tps = TPS.read();
    let (tpms, frac) = (tps / 1000, tps % 1000);
    let us4 = us(4);
    let (mut acc, mut ms, mut lows) = (0, 0, 0);
    r.drop_ms = -1;
    r.pulses = 0;
    // D+ released
    let mut uio = UIO_MODE | UIO_DIE | DP_DIR | DM_DIR | DP_PD | DM_PD;
    uio_put(uio);
    let t0 = now().wrapping_add(us(100));
    let mut next = t0;
    'pulses: loop {
        wait_until(next);
        // D+ low
        uio &= !(DP_OUT | DP_DIR);
        uio_put(uio);
        wait_until(next.wrapping_add(us4));
        // released: the ROM's pull-up takes it high
        uio |= DP_DIR;
        uio_put(uio);
        r.pulses += 1;
        let mut nxt = next.wrapping_add(tpms);
        acc += frac;
        if acc >= 1000 {
            nxt = nxt.wrapping_add(1);
            acc -= 1000;
        }
        for i in 1..=9 {
            wait_until(next.wrapping_add(i * (tpms / 10)));
            if !pin_dp() {
                lows += 1;
                if lows >= PULSE_LOW_SAMPLES && ms >= CAL_DONE_MIN_MS {
                    r.drop_ms = ms as i32;
                    break 'pulses;
                }
            } else {
                lows = 0;
            }
        }
        next = nxt;
        ms = next.wrapping_sub(t0) / tpms;
        if ms >= PULSE_MAX_MS {
            break;
        }
    }
    r.pulse_ms = now().wrapping_sub(t0) / tpms;
    uio_put(UIO_MODE | UIO_DIE | DP_DIR | DM_DIR | DP_PD | DM_PD);
}
