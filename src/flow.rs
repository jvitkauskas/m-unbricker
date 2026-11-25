// SPDX-License-Identifier: MIT
//! The flow: the pages the user is taken through, and the repair (main.c in the C).
//!
//! Unplug from the phone or computer, connect the broken unit, switch it on; then the JieLi USB_KEY and the
//! calibration pulses wake its ROM, this unit talks to that ROM as a USB host (M-Flasher's loader), identifies the
//! model, compares the flash with the rescue image (m-bootloader, the rest erased), and after PLAY (SEQ PLAY on the
//! SMK-37) writes, verifies and restarts it: it then waits in m-bootloader's MIDI mode for a firmware. PLAY at the end
//! restarts this unit into its own firmware.
use crate::board;
use crate::husb::Res;
use crate::jl::{Jl, RESTART_PROG};
use crate::keys::{K_PREV, K_SEL, Keys};
use crate::lcd::Lcd;
use crate::pages::*;
use crate::regs::sync_cpu;
use crate::ui::{C_DIM, Line, Page, Ui};
use crate::usbio::{self, CalResult, KeyResult, KeyStats, ms_since, now};
use crate::writer::{self, Stop, Which, Writer};

/// The version on the first page.
macro_rules! version {
    () => {
        "0.5"
    };
}

/// Everything the app keeps: the screen, the other unit's session, the writer and the keys.
pub struct App {
    pub ui: Ui<Lcd>,
    pub jl: Jl,
    pub writer: Writer,
    pub keys: Keys,
}

impl App {
    pub const fn new() -> App {
        App { ui: Ui::new(Lcd::new()), jl: Jl::new(), writer: Writer::new(), keys: Keys::new() }
    }
}

/// Waits for PLAY (true) or, if `no` is set, the leave key (false). With `ping`, the other unit's loader is kept busy
/// meanwhile.
pub fn wait_key(keys: &mut Keys, no: bool, mut ping: Option<&mut Jl>) -> bool {
    let mut prev = K_SEL | K_PREV;
    let mut t = now();
    loop {
        let k = keys.scan();
        if k & K_SEL != 0 && prev & K_SEL == 0 {
            break;
        }
        if no && k & K_PREV != 0 && prev & K_PREV == 0 {
            keys.off();
            return false;
        }
        prev = k;
        if let Some(jl) = ping.as_deref_mut() {
            if ms_since(t) >= 1000 {
                t = now();
                jl.ping();
            }
        }
    }
    keys.off();
    true
}

/// M-Flasher's RESTART program (src/jl.rs): a RAM app ends by resetting, and the unit then starts its own firmware.
pub fn restart() -> ! {
    sync_cpu();
    // Safety: the program is pi32v2 code, aligned, which clears the download-mode marker and resets the chip: it
    // doesn't return, and needs nothing of this program.
    let run: extern "C" fn() -> ! = unsafe { core::mem::transmute(RESTART_PROG.0.as_ptr()) };
    run()
}

fn the_end(app: &mut App) -> ! {
    wait_key(&mut app.keys, false, None);
    restart()
}

/// The loader state's clock boost (m-bootloader SPEC.md 10.4): CPU 96 MHz, the LSB stays 48 MHz. Only from download
/// mode (run-ram). It is m-bootloader's `hal_clock_b`, which does that only from the ROM's clocks.
fn boost() {
    mboot::platform::hal_clock_b();
}

// ---- errors ----

fn res_code(r: Res) -> u32 {
    match r {
        Res::Ok => 0,
        Res::Stall => 1,
        Res::Timeout => 2,
        Res::Error => 3,
    }
}

/// A page about what went wrong, with what and the controller's last snapshot on its status line, until PLAY.
pub fn problem(app: &mut App, pg: &'static Page, what: &[u8], code: u32) {
    app.ui.show(pg);
    let dbg = &app.jl.bot.usb.dbg;
    let mut d = Line::new();
    d.push(what).push(b" ").hex(code, 2).push(" · ".as_bytes()).hex(dbg[2] | dbg[3] & 0xff00_0000, 8);
    app.ui.status(d.as_bytes(), C_DIM);
    wait_key(&mut app.keys, false, None);
}

fn stopped(app: &mut App, pg: &'static Page, stop: Stop) {
    problem(app, pg, stop.what, stop.addr >> 12)
}

// ---- the key ----

/// Until the other unit's ROM answers the key.
pub fn wake_it(app: &mut App) {
    let mut ks = KeyStats::default();
    loop {
        app.ui.show(&SWITCH_ON);
        let key_t0 = now();
        let mut key_shown = u32::MAX;
        let ui = &mut app.ui;
        let kr = usbio::key_send(&mut ks, 60_000, &mut |_: &KeyStats| {
            let sec = ms_since(key_t0) / 1000;
            if sec == key_shown {
                return;
            }
            key_shown = sec;
            let mut text = Line::new();
            text.push("Waiting for it… ".as_bytes()).num(sec).push(b" s");
            ui.status(text.as_bytes(), C_DIM);
        });
        if kr == KeyResult::Ack || kr == KeyResult::DpAnswer {
            break;
        }
        usbio::take_pins();
        app.ui.show(if kr == KeyResult::DpAtStart { &ALREADY_ON } else { &NO_ANSWER });
        wait_key(&mut app.keys, false, None);
    }
    app.ui.show(&WAKING);
    usbio::cal_pulses(&mut CalResult::default());
}

// ---- the host side ----

/// Which rescue image: the Cardputer app's rules (engine.cpp identify_unit).
pub fn which_unit(jl: &mut Jl) -> Option<Which> {
    let (mut a, mut b) = ([0u8; 32], [0u8; 32]);
    let id = jl.info.flash_id & 0xff_ffff;
    if jl.info.chip_key != 0x980f {
        return None;
    }
    let trimdat = jl.read(0xc1220, &mut a) && &a[4..12] == b"trimdat\0";
    let trimcopy = jl.read(0xff000, &mut b) && &b[..8] == b"TRIMCOPY";
    if trimdat || trimcopy || id == 0xeb_6014 {
        Some(Which::Smk37)
    } else if id == 0x85_6014 || id == 0xb3_6014 {
        Some(Which::Fm1)
    } else {
        None
    }
}

/// What a session ended in.
#[derive(Clone, Copy, PartialEq, Eq, Debug)]
pub enum Outcome {
    /// Repaired (or nothing to do) and restarted.
    Done,
    /// Try again.
    Again,
    /// Left.
    Left,
}

pub fn session(app: &mut App) -> Outcome {
    app.ui.show(&CONNECTING);
    app.jl.bot.usb.init();
    if !app.jl.bot.usb.wait_attach(5000) {
        problem(app, &ERR_CONNECT, b"attach", 0);
        return Outcome::Again;
    }
    if let Err((step, res)) = app.jl.bot.enumerate() {
        problem(app, &ERR_CONNECT, b"usb", step * 16 + res_code(res));
        return Outcome::Again;
    }
    let dev = app.jl.bot.dev;
    if dev.vid != 0x4c4a || dev.pid != 0x8057 {
        problem(app, &ERR_OTHER, b"id", 0);
        return Outcome::Again;
    }
    if let Err(step) = app.jl.start() {
        problem(app, &ERR_CONNECT, b"loader", step);
        return Outcome::Again;
    }
    let Some(w) = which_unit(&mut app.jl) else {
        let mut text = Line::new();
        text.push(b"flash ").hex(app.jl.info.flash_id & 0xff_ffff, 6).push(b" key");
        let key = app.jl.info.chip_key as u32 >> 8;
        problem(app, &ERR_UNKNOWN, text.as_bytes(), key);
        return Outcome::Left;
    };
    let smk37 = w == Which::Smk37;
    let img = writer::image(w);
    app.ui.show(if smk37 { &FOUND_SMK37 } else { &FOUND_FM1 });
    app.ui.progress(0, 1);
    if !app.writer.image_ok(img) {
        problem(app, &ERR_IMAGE, b"image", 0);
        return Outcome::Left;
    }
    let ui = &mut app.ui;
    if let Err(stop) = app.writer.plan_make(img, &mut app.jl, &mut |done, total| ui.progress(done, total)) {
        stopped(app, &ERR_READ, stop);
        return Outcome::Again;
    }
    let plan = &app.writer.plan;
    let changes = plan.to_write + plan.to_erase;
    if changes == 0 {
        app.ui.show(&NOTHING);
    } else {
        app.ui.show(if smk37 { &READY_SMK37 } else { &READY_FM1 });
        let mut text = Line::new();
        text.push(b"Changes ").num(changes).push(b" of 255 sectors");
        app.ui.status(text.as_bytes(), C_DIM);
    }
    if !wait_key(&mut app.keys, true, Some(&mut app.jl)) {
        app.ui.show(&LEFT);
        return Outcome::Left;
    }
    if changes != 0 {
        app.ui.show(&REPAIRING);
        let ui = &mut app.ui;
        if let Err(stop) = app.writer.plan_apply(img, &mut app.jl, &mut |done, total| ui.progress(done, total)) {
            stopped(app, &ERR_WRITE, stop);
            return Outcome::Again;
        }
    }
    app.jl.restart();
    app.ui.show(if smk37 { &DONE_SMK37 } else { &DONE_FM1 });
    Outcome::Done
}

// ---- the app ----

/// `arg`: the LSB clock in MHz when started over MIDI (m-bootloader's MIDI mode or the RAM stage, which stopped USB
/// first); a pointer to the ROM's words (>= 0x01000000) when started by `m-flasher run-ram` from download mode.
/// Called with .bss cleared.
pub fn run(app: &mut App, arg: u32) -> ! {
    let from_rom = arg >= 0x0100_0000;
    usbio::TPS.set(usbio::NOMINAL_TPS);
    // watchdog off
    mboot::platform::p33_and(0x80, 0xef);
    if from_rom {
        boost();
    } else if (12..=240).contains(&arg) {
        usbio::TPS.set(arg * 1_000_000);
    }
    usbio::start();
    let a = now();
    mboot::platform::settle(20_000);
    let b = now();
    // from download mode the PC is still connected: its 1 ms frames are the reference
    if a != b && from_rom {
        usbio::calibrate_sof(1000, &mut 0);
    }
    // the host sees the unit go
    usbio::take_pins();
    if crate::APP_BOARD == 0 && board::detect(&mut 0) == 0 {
        // Its screen and keys aren't known, so none of their pins are touched: the unit restarts into its own
        // firmware.
        restart();
    }
    app.ui.init(board::get());
    if a == b {
        app.ui.show(&ERR_TIMER);
        loop {}
    }
    app.ui.show(&WELCOME);
    app.ui.status(concat!("version ", version!()).as_bytes(), C_DIM);
    wait_key(&mut app.keys, false, None);
    app.ui.show(&UNPLUG);
    wait_key(&mut app.keys, false, None);
    app.ui.show(&CONNECT);
    wait_key(&mut app.keys, false, None);
    loop {
        wake_it(app);
        if session(app) != Outcome::Again {
            break;
        }
    }
    the_end(app)
}
