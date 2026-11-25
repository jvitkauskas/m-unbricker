// SPDX-License-Identifier: MIT
//! M-Unbricker: a working unit unbricks another (FM-1 or SMK-37, either way).
//!
//! A RAM app (m-bootloader docs/SPEC.md 11.8), one build per board it runs on (`--cfg board="fm1"`, `"smk37"`), and
//! one for both (`board="any"`), which reads which board it's on from its flash. It is started on the working unit by
//! the M-Unbricker web page (through m-bootloader's MIDI mode or the RAM stage), by the card's launcher (m-apps), or by
//! `m-flasher run-ram`, and installs nothing there. src/flow.rs has what it does.
//!
//! The chip's common drivers are m-bootloader's Rust crate (the m-bootloader submodule), `mboot`.
#![no_std]
#![cfg_attr(target_arch = "pi32v2", feature(asm_experimental_arch))]

mod art;
mod board;
mod bot;
mod flow;
mod fonts;
mod husb;
mod jl;
mod keys;
mod lcd;
mod pages;
mod regs;
#[path = "../build/rescue_img.rs"]
mod rescue_img;
mod ui;
mod usbio;
mod writer;

use mboot::Global;

/// The board this was built for: 1 FM-1, 2 SMK-37, as the file's trailer counts them, or 0 for the build for both,
/// which finds out at the start (src/board.rs).
pub const APP_BOARD: u32 = if cfg!(board = "fm1") {
    1
} else if cfg!(board = "smk37") {
    2
} else {
    0
};

static APP: Global<flow::App> = Global::new(flow::App::new());

/// From the entry (src/start.S), on this program's own stacks, with interrupts off. It never returns: it ends by
/// restarting the unit.
#[unsafe(no_mangle)]
pub extern "C" fn rk_main(arg: u32) -> ! {
    unsafe extern "C" {
        static mut rk_bss_start: u8;
        static rk_bss_end: u8;
    }
    mboot::hw::irq_off();
    // Safety: this program's own data, which nothing has used yet.
    unsafe {
        core::ptr::write_bytes(
            &raw mut rk_bss_start,
            0,
            mboot::hw::distance(&raw const rk_bss_start, &raw const rk_bss_end),
        )
    };
    // Safety: the one use of the app's state, which everything is given from here.
    flow::run(unsafe { APP.get() }, arg)
}
