// SPDX-License-Identifier: MIT
//! The registers M-Unbricker uses (m-bootloader's docs/SPEC.md §2.2), and the pin helpers in the order the C had them:
//! a level, a direction and a reading are separate writes, so that each driver keeps its exact sequence.
//! Registers are m-bootloader's [`Reg`].
pub use mboot::hw::{Reg, bit, gpio, sync_cpu};

/// Timer 0: +0 control, +4 count, +8 period.
pub const TMR0: Reg = Reg::at::<0x10400>();
pub const TMR0_COUNT: Reg = TMR0.add(4);
pub const TMR0_PERIOD: Reg = TMR0.add(8);
/// USB0's control and its SIE window (src/husb.rs).
pub const USB_CON0: Reg = Reg::at::<0x11800>();
pub const USB_CON1: Reg = Reg::at::<0x11804>();
/// USB0's pins as GPIO: their outputs, directions and pulls, and their levels (src/usbio.rs).
pub const USB_IO: Reg = Reg::at::<0x51000>();
pub const USB_IO_IN: Reg = Reg::at::<0x51004>();
/// The high-speed USB PHY's control, shared with the SMK-37's key LED commons.
pub const PHY_CON: Reg = Reg::at::<0x16a0c>();
pub const IO_MUX1: Reg = Reg::at::<0x51020>();

/// A port's registers, from its base: output, input, direction (1 = input), digital input, pull-up, pull-down, the
/// two drive strengths and the second digital-input enable.
pub const GP_OUT: u32 = 0x00;
pub const GP_IN: u32 = 0x04;
pub const GP_DIR: u32 = 0x08;
pub const GP_DIE: u32 = 0x0c;
pub const GP_PU: u32 = 0x10;
pub const GP_PD: u32 = 0x14;
pub const GP_HD0: u32 = 0x18;
pub const GP_HD: u32 = 0x1c;
pub const GP_DIEH: u32 = 0x20;
pub const PA: u32 = 0;
pub const PB: u32 = 1;
pub const PC: u32 = 2;
pub const PH: u32 = 7;

/// A pin's level, as m-bootloader's `gpio_level`.
#[inline(always)]
pub fn gp_set(port: u32, pin: u32, high: bool) {
    mboot::platform::gpio_level(port, pin, high)
}

/// A pin's level read back: true if high.
#[inline(always)]
pub fn gp_get(port: u32, pin: u32) -> bool {
    gpio(port, GP_IN).read() >> pin & 1 != 0
}

#[inline(always)]
pub fn gp_dir_out(port: u32, pin: u32) {
    gpio(port, GP_DIR).clear(bit(pin))
}

#[inline(always)]
pub fn gp_dir_in(port: u32, pin: u32) {
    gpio(port, GP_DIR).set(bit(pin))
}

/// Waits for the DMA's writes to reach memory before reading what it wrote.
#[inline(always)]
pub fn sync_data() {
    #[cfg(target_arch = "pi32v2")]
    // Safety: an ordering instruction, which touches no memory itself.
    unsafe {
        core::arch::asm!("ssync", options(nostack, preserves_flags))
    }
}

/// The address the DMA is given for a buffer.
#[inline(always)]
pub fn dma(p: &[u8]) -> u32 {
    mboot::hw::dma_address(p.as_ptr().cast_mut())
}

/// Bytes put together and taken apart: little endian for USB, big endian for the loader's commands.
pub fn ld16(p: &[u8]) -> u32 {
    p[0] as u32 | (p[1] as u32) << 8
}

pub fn ld32(p: &[u8]) -> u32 {
    u32::from_le_bytes([p[0], p[1], p[2], p[3]])
}

pub fn st16(p: &mut [u8], v: u32) {
    p[..2].copy_from_slice(&(v as u16).to_le_bytes())
}

pub fn st32(p: &mut [u8], v: u32) {
    p[..4].copy_from_slice(&v.to_le_bytes())
}

pub fn bd16(p: &[u8]) -> u32 {
    (p[0] as u32) << 8 | p[1] as u32
}

pub fn bd32(p: &[u8]) -> u32 {
    u32::from_be_bytes([p[0], p[1], p[2], p[3]])
}

pub fn bs32(p: &mut [u8], v: u32) {
    p[..4].copy_from_slice(&v.to_be_bytes())
}
