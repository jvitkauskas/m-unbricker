// SPDX-License-Identifier: MIT
//! The menu keys and their LEDs on the two boards: m-bootloader 1.4's src/boot/keys.c (MIT, Julius Vitkauskas; its
//! docs/SPEC.md 8.2, 8.3), both boards in one, as its scan lights the keys' LEDs while it waits, which m-bootloader's
//! own keys today don't. One board's build has its board as a constant.
use crate::board;
use crate::regs::{GP_DIE, GP_DIEH, GP_DIR, GP_HD, GP_HD0, GP_OUT, GP_PD, GP_PU, PA, PH, PHY_CON, bit};
use crate::regs::{gp_dir_in, gp_dir_out, gp_get, gp_set, gpio};
use crate::usbio::delay_us;

pub const K_PREV: u32 = 1;
pub const K_NEXT: u32 = 2;
pub const K_SEL: u32 = 4;

/// The restore order: direction first (pins that were inputs stop driving the shift registers), the output levels
/// last.
const SNAP_OFF: [u32; 8] = [GP_DIR, GP_DIE, GP_PU, GP_PD, GP_HD0, GP_HD, GP_DIEH, GP_OUT];

// The FM-1: two cascaded shift registers, data PA4, clock PA3, latch PA1; 16 bits MSB first; a selected column is a
// low bit.
const FM_IDLE: u32 = 0xffff;
const FM_SEL: u32 = 0xfbff;
const FM_PREV: u32 = 0xf7fe;
const FM_NEXT: u32 = 0xeffd;

// The SMK-37: four shift registers, data PA4, clock PA3, latch PA1, output enable PA0 (active low). The 32-bit word
// goes out least significant byte first, MSB first within each byte.
const SMK_IDLE: u32 = 0xffff_ffff;
const SMK_SEL: u32 = 0xfeff_ffff;
const SMK_PREV: u32 = 0xfffe_ffff;
const SMK_NEXT: u32 = 0xefff_ffff;
/// The drive enables; bit 17 / 18 high = LED group 0 / 1 off.
const PHY_ON: u32 = 0x1000f;
const PHY_OFF: u32 = PHY_ON | 3 << 17;

/// The keys' pins, saved when the keys came on, to put back when they go off.
pub struct Keys {
    snap: [[u32; 8]; 2],
    snap_phy: u32,
    on: bool,
}

fn smk() -> bool {
    board::get() == board::SMK
}

fn pulse(port: u32, pin: u32) {
    gp_set(port, pin, true);
    gp_set(port, pin, false);
}

fn sense_pin(port: u32, pin: u32) {
    gp_dir_in(port, pin);
    gpio(port, GP_DIE).set(bit(pin));
    // the second digital-input enable (H12)
    gpio(port, GP_DIEH).set(bit(pin));
    gpio(port, GP_PU).set(bit(pin));
    gpio(port, GP_PD).clear(bit(pin));
}

fn shift(w: u32) {
    if smk() {
        for b in 0..4 {
            for i in (0..8).rev() {
                gp_set(PA, 4, w >> (8 * b + i) & 1 != 0);
                pulse(PA, 3);
            }
        }
    } else {
        for i in (0..16).rev() {
            gp_set(PA, 4, w >> i & 1 != 0);
            pulse(PA, 3);
        }
    }
    pulse(PA, 1);
}

fn idle() -> u32 {
    if smk() { SMK_IDLE } else { FM_IDLE }
}

/// A key's column selected: true if its row reads it closed (low).
fn sample(word: u32, port: u32, pin: u32) -> bool {
    shift(word);
    delay_us(10);
    !gp_get(port, pin)
}

/// The FM-1's LED lines PA9, PA10, PH6, PH9 belong to sense rows PA5..PA8; the menu keys use PH9 (the octave keys,
/// row PA8) and PA10 (PLAY, row PA6).
fn led(which: u32, high: bool) {
    if which != 0 {
        gp_set(PA, 10, high);
    } else {
        gp_set(PH, 9, high);
    }
}

impl Keys {
    pub const fn new() -> Keys {
        Keys { snap: [[0; 8]; 2], snap_phy: 0, on: false }
    }

    fn save(&mut self) {
        for (p, port) in [PA, PH].into_iter().enumerate() {
            for (i, &off) in SNAP_OFF.iter().enumerate() {
                self.snap[p][i] = gpio(port, off).read();
            }
        }
        self.snap_phy = PHY_CON.read();
    }

    fn restore(&self) {
        for (p, port) in [PA, PH].into_iter().enumerate() {
            for (i, &off) in SNAP_OFF.iter().enumerate() {
                gpio(port, off).write(self.snap[p][i]);
            }
        }
        PHY_CON.write(self.snap_phy);
    }

    fn on(&mut self) {
        if self.on {
            return;
        }
        self.on = true;
        self.save();
        if smk() {
            for pin in [0, 1, 3, 4] {
                gp_set(PA, pin, false);
            }
            for pin in [0, 1, 3, 4] {
                gp_dir_out(PA, pin);
            }
            sense_pin(PH, 8);
        } else {
            for pin in [4, 3, 1] {
                gp_set(PA, pin, false);
            }
            for pin in [4, 3, 1] {
                gp_dir_out(PA, pin);
            }
            sense_pin(PA, 6);
            sense_pin(PA, 8);
            gp_set(PA, 10, false);
            gp_set(PH, 9, false);
            gp_dir_out(PA, 10);
            gp_dir_out(PH, 9);
        }
        shift(idle());
    }

    /// The menu keys held now: K_PREV (OCT− or <), K_NEXT (OCT+ or >), K_SEL (PLAY or SEQ PLAY), lighting each key's
    /// LED for a millisecond.
    pub fn scan(&mut self) -> u32 {
        self.on();
        let mut r = 0;
        if smk() {
            // The LED lines are the low 16 bits of the word (active low): `<` is line 0 of group 0; `>` is line 0 and
            // SEQ PLAY line 12 of group 1. Bits 5:3 address an analog multiplexer and stay high.
            const COL: [u32; 3] = [SMK_PREV, SMK_NEXT, SMK_SEL];
            const KEY: [u32; 3] = [K_PREV, K_NEXT, K_SEL];
            let mut seen = 7;
            for t in 0..6 {
                let (g, i) = (t & 1, t % 3);
                // both groups off while the lines change
                PHY_CON.write(PHY_OFF);
                let pressed = sample(COL[i] & if g != 0 { 0xffff_effe } else { 0xffff_fffe }, PH, 8);
                if !pressed {
                    seen &= !(1 << i);
                }
                PHY_CON.write(PHY_OFF & !bit(if g != 0 { 18 } else { 17 }));
                delay_us(1000);
            }
            PHY_CON.write(PHY_OFF);
            for (i, &k) in KEY.iter().enumerate() {
                if seen & 1 << i != 0 {
                    r |= k;
                }
            }
        } else {
            // (column word, sense row pin, LED, key)
            const K: [(u32, u32, u32, u32); 3] =
                [(FM_PREV, 8, 0, K_PREV), (FM_NEXT, 8, 0, K_NEXT), (FM_SEL, 6, 1, K_SEL)];
            for (w, pin, which, key) in K {
                if sample(w, PA, pin) {
                    r |= key;
                }
                led(which, true);
                delay_us(1000);
                led(which, false);
            }
        }
        shift(idle());
        r
    }

    pub fn off(&mut self) {
        if !self.on {
            return;
        }
        shift(idle());
        self.restore();
        self.on = false;
    }
}
