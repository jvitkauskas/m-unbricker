// SPDX-License-Identifier: MIT
//! The Rust side of the comparison on the simulated chip (tests/chip): one scenario of the app's Rust, run as
//! tests/c/drive.c runs the C's, with the same steps and marks, so that the summaries (tests/c-reference.txt) can be
//! compared line for line. Built for one board (`--cfg board="fm1"`, `"smk37"` or `"any"`) by tests/chip_test.py.
//!
//!   drive <scenario> | drive --list
#![allow(dead_code)]

#[path = "../src/art.rs"]
mod art;
#[path = "../src/board.rs"]
mod board;
#[path = "../src/bot.rs"]
mod bot;
#[path = "../src/flow.rs"]
mod flow;
#[path = "../src/fonts.rs"]
mod fonts;
#[path = "../src/husb.rs"]
mod husb;
#[path = "../src/jl.rs"]
mod jl;
#[path = "../src/keys.rs"]
mod keys;
#[path = "../src/lcd.rs"]
mod lcd;
#[path = "../src/pages.rs"]
mod pages;
#[path = "../src/regs.rs"]
mod regs;
#[path = "../build/rescue_img.rs"]
mod rescue_img;
#[path = "../src/ui.rs"]
mod ui;
#[path = "../src/usbio.rs"]
mod usbio;
#[path = "../src/writer.rs"]
mod writer;

use flow::{App, Outcome};
use mboot::Global;
use pages::*;
use std::ffi::CString;
use ui::{C_DIM, Page};
use usbio::{CalResult, KeyResult, KeyStats};

pub const APP_BOARD: u32 = if cfg!(board = "fm1") {
    1
} else if cfg!(board = "smk37") {
    2
} else {
    0
};

unsafe extern "C" {
    fn chip_start(scenario: *const std::ffi::c_char);
    fn chip_mark(text: *const std::ffi::c_char);
    fn chip_mark_panel(text: *const std::ffi::c_char);
    fn chip_finish();
}

fn mark(text: &str) {
    let t = CString::new(text).unwrap();
    // Safety: the chip's, which copies the text.
    unsafe { chip_mark(t.as_ptr()) }
}

fn mark_panel(text: &str) {
    let t = CString::new(text).unwrap();
    // Safety: as for chip_mark.
    unsafe { chip_mark_panel(t.as_ptr()) }
}

/// The app's state: a static, as on the unit, so that the DMA's addresses are below 4 GB.
static APP: Global<App> = Global::new(App::new());

struct Run<'a> {
    app: &'a mut App,
    board: u32,
}

// ---- panel-<board> ----

fn sc_panel(r: &mut Run) {
    usbio::start();
    r.app.ui.init(r.board);
    mark_panel("ui_init");
    let shots: [(&str, &'static Page, &dyn Fn(&mut App)); 24] = [
        ("PG_welcome", &WELCOME, &|a| a.ui.status(b"version 0.4", C_DIM)),
        ("PG_unplug", &UNPLUG, &|_| ()),
        ("PG_connect", &CONNECT, &|_| ()),
        ("PG_switch_on", &SWITCH_ON, &|a| a.ui.status("Waiting for it… 12 s".as_bytes(), C_DIM)),
        ("PG_already_on", &ALREADY_ON, &|_| ()),
        ("PG_no_answer", &NO_ANSWER, &|_| ()),
        ("PG_waking", &WAKING, &|_| ()),
        ("PG_connecting", &CONNECTING, &|_| ()),
        ("PG_found_fm1", &FOUND_FM1, &|a| a.ui.progress(40, 100)),
        ("PG_found_smk37", &FOUND_SMK37, &|a| a.ui.progress(70, 100)),
        ("PG_ready_fm1", &READY_FM1, &|a| a.ui.status(b"Changes 155 of 255 sectors", C_DIM)),
        ("PG_ready_smk37", &READY_SMK37, &|a| a.ui.status(b"Changes 192 of 255 sectors", C_DIM)),
        ("PG_nothing", &NOTHING, &|_| ()),
        ("PG_repairing", &REPAIRING, &|a| a.ui.progress(120, 192)),
        ("PG_done_fm1", &DONE_FM1, &|_| ()),
        ("PG_done_smk37", &DONE_SMK37, &|_| ()),
        ("PG_left", &LEFT, &|_| ()),
        ("PG_err_connect", &ERR_CONNECT, &|a| a.ui.status("usb 31 · 30000A0C".as_bytes(), C_DIM)),
        ("PG_err_other", &ERR_OTHER, &|_| ()),
        ("PG_err_unknown", &ERR_UNKNOWN, &|a| a.ui.status(b"flash C84017 key 98", C_DIM)),
        ("PG_err_image", &ERR_IMAGE, &|_| ()),
        ("PG_err_read", &ERR_READ, &|_| ()),
        ("PG_err_write", &ERR_WRITE, &|a| a.ui.status("VERIFY 1F · 00000000".as_bytes(), C_DIM)),
        ("PG_err_timer", &ERR_TIMER, &|_| ()),
    ];
    for (name, page, extra) in shots {
        r.app.ui.show(page);
        extra(r.app);
        mark_panel(name);
    }
}

// ---- keys-<board> ----

fn sc_keys(r: &mut Run) {
    usbio::start();
    let k = flow::wait_key(&mut r.app.keys, false, None);
    mark(&format!("wait_key(0,0) {}", k as u32));
    let k = flow::wait_key(&mut r.app.keys, true, None);
    mark(&format!("wait_key(1,0) {}", k as u32));
    let k = flow::wait_key(&mut r.app.keys, true, None);
    mark(&format!("wait_key(1,0) {}", k as u32));
}

// ---- wake-* ----

fn sc_wake_it(r: &mut Run) {
    usbio::start();
    r.app.ui.init(r.board);
    mark("ui_init");
    flow::wake_it(r.app);
    mark("wake_it");
}

fn send_key(r: &mut Run, timeout_ms: u32) -> KeyResult {
    let mut ks = KeyStats::default();
    r.app.ui.show(&SWITCH_ON);
    let kr = flow::send_key(&mut r.app.ui, &mut ks, timeout_ms);
    let name = match kr {
        KeyResult::Ack => "ack",
        KeyResult::DpAtStart => "dp-at-start",
        KeyResult::DpAnswer => "dp-answer",
        KeyResult::Timeout => "timeout",
    };
    mark(&format!("key_send {name} packets {} false_acks {} alive {}", ks.packets, ks.false_acks, ks.alive as u32));
    kr
}

fn sc_wake_fail(r: &mut Run) {
    usbio::start();
    r.app.ui.init(r.board);
    mark("ui_init");
    let kr = send_key(r, 1000);
    usbio::take_pins();
    r.app.ui.show(if kr == KeyResult::DpAtStart { &ALREADY_ON } else { &NO_ANSWER });
    mark("page");
}

fn sc_wake_false(r: &mut Run) {
    usbio::start();
    r.app.ui.init(r.board);
    mark("ui_init");
    send_key(r, 5000);
    r.app.ui.show(&WAKING);
    let mut cr = CalResult::default();
    usbio::cal_pulses(&mut cr);
    mark(&format!("cal_pulses pulse_ms {} drop_ms {} pulses {}", cr.pulse_ms, cr.drop_ms, cr.pulses));
}

// ---- sof-* ----

fn sc_sof(_: &mut Run) {
    usbio::start();
    let mut seen = 0;
    let ok = usbio::calibrate_sof(1000, &mut seen);
    mark(&format!(
        "tb_calibrate_sof {} seen {seen} tps {} measured {}",
        ok as u32,
        usbio::TPS.read(),
        usbio::MEASURED.read() as u32
    ));
}

// ---- detect-* ----

fn sc_detect(_: &mut Run) {
    let mut jedec = 0;
    let b = board::detect(&mut jedec);
    mark(&format!("board {b} jedec {jedec:06X}"));
}

// ---- session-*, full-fm1 ----

fn after(r: &mut Run, o: Outcome) {
    mark(&format!(
        "session {}",
        match o {
            Outcome::Done => 1,
            Outcome::Again => 0,
            Outcome::Left => -1,
        }
    ));
    let p = &r.app.writer.plan;
    mark(&format!("plan write {} erase {} runs {}", p.to_write, p.to_erase, p.runs));
    let i = &r.app.jl.info;
    let loader: String = i.loader.iter().take_while(|&&c| c != 0).map(|&c| c as char).collect();
    mark(&format!(
        "loader {loader} key {:04X} flash {:06X} io {} retries {}",
        i.chip_key,
        i.flash_id & 0xff_ffff,
        i.io,
        i.retries
    ));
}

fn sc_session(r: &mut Run) {
    usbio::start();
    r.app.ui.init(r.board);
    mark("ui_init");
    let o = flow::session(r.app);
    after(r, o);
}

fn sc_full(r: &mut Run) {
    usbio::start();
    usbio::take_pins();
    r.app.ui.init(r.board);
    mark("ui_init");
    r.app.ui.show(&WELCOME);
    r.app.ui.status(b"version 0.5", C_DIM);
    flow::wait_key(&mut r.app.keys, false, None);
    r.app.ui.show(&UNPLUG);
    flow::wait_key(&mut r.app.keys, false, None);
    r.app.ui.show(&CONNECT);
    flow::wait_key(&mut r.app.keys, false, None);
    mark("pages");
    let o = loop {
        flow::wake_it(r.app);
        mark("wake_it");
        let o = flow::session(r.app);
        if o != Outcome::Again {
            break o;
        }
    };
    after(r, o);
}

type Scenario = (&'static str, fn(&mut Run));

const SC: [Scenario; 44] = [
    ("panel-fm1", sc_panel),
    ("panel-smk37", sc_panel),
    ("keys-fm1", sc_keys),
    ("keys-smk37", sc_keys),
    ("wake-ack-fm1", sc_wake_it),
    ("wake-ack-smk37", sc_wake_it),
    ("wake-dpanswer-fm1", sc_wake_it),
    ("wake-dpstart-fm1", sc_wake_fail),
    ("wake-dpstart-smk37", sc_wake_fail),
    ("wake-none-fm1", sc_wake_fail),
    ("wake-none-smk37", sc_wake_fail),
    ("wake-false-fm1", sc_wake_false),
    ("wake-false-smk37", sc_wake_false),
    ("sof-present", sc_sof),
    ("sof-absent", sc_sof),
    ("sof-fast", sc_sof),
    ("sof-reject", sc_sof),
    ("detect-fm1-856014", sc_detect),
    ("detect-fm1-b36014", sc_detect),
    ("detect-smk37-trimdat", sc_detect),
    ("detect-smk37-trimcopy", sc_detect),
    ("detect-unknown", sc_detect),
    ("detect-fm1chip-smkmark", sc_detect),
    ("detect-none", sc_detect),
    ("session-repair-fm1", sc_session),
    ("session-repair-smk37", sc_session),
    ("session-cross-fm1", sc_session),
    ("session-cross-smk37", sc_session),
    ("session-nothing-fm1", sc_session),
    ("session-leave-fm1", sc_session),
    ("session-leave-smk37", sc_session),
    ("session-unknown-fm1", sc_session),
    ("session-otherid-fm1", sc_session),
    ("session-noattach-fm1", sc_session),
    ("session-stall-fm1", sc_session),
    ("session-early-fm1", sc_session),
    ("session-odd-fm1", sc_session),
    ("session-crc-fm1", sc_session),
    ("session-writefail-fm1", sc_session),
    ("session-readerr-fm1", sc_session),
    ("session-idfail-fm1", sc_session),
    ("session-otherloader-fm1", sc_session),
    ("session-noisy-fm1", sc_session),
    ("full-fm1", sc_full),
];

fn main() {
    let arg = std::env::args().nth(1).unwrap_or_default();
    if arg == "--list" {
        for (name, _) in SC {
            println!("{name}");
        }
        return;
    }
    let Some(&(name, run)) = SC.iter().find(|(n, _)| *n == arg) else {
        eprintln!("usage: drive <scenario> | drive --list");
        std::process::exit(2);
    };
    let board = if name.ends_with("-fm1") {
        board::FM1
    } else if name.ends_with("-smk37") {
        board::SMK
    } else {
        0
    };
    if APP_BOARD != 0 && board != 0 && board != APP_BOARD {
        eprintln!("drive: {name} is for the other board's build");
        std::process::exit(2);
    }
    // What the start does first, with no register: the timer's nominal rate; and for the build for both, the board
    // its detection would have found.
    usbio::TPS.set(usbio::NOMINAL_TPS);
    board::assume(board);
    let n = CString::new(name).unwrap();
    // Safety: the chip maps the registers and stands in for them from here to chip_finish().
    unsafe { chip_start(n.as_ptr()) };
    // Safety: the one use of the app's state.
    run(&mut Run { app: unsafe { APP.get() }, board });
    // Safety: as for chip_start.
    unsafe { chip_finish() };
}
