// SPDX-License-Identifier: MIT
//! The preview on a computer: the app's own page renderer (src/ui.rs) drawing every page of src/pages.rs into a
//! framebuffer, each written as a PNG, with the status lines and progress bars a page shows on the unit. Built for one
//! board (`--cfg board="fm1"` or `"smk37"`) by `make preview`:
//!
//!   preview OUT_DIR [BOARD]
//!
//! writes OUT_DIR/<page>.png, and OUT_DIR/pages.txt with the pages in order, for tools/preview.py's contact sheet.
//! BOARD is what the renderer is started with (Ui::init), 0 unless given: the build for both (`board="any"`) shows
//! the FM-1's key names with 1 and the SMK-37's with 2 (tests/preview_test.py).
#![allow(dead_code)]

#[path = "../src/art.rs"]
mod art;
#[path = "../src/board.rs"]
mod board;
#[path = "../src/fonts.rs"]
mod fonts;
#[path = "../src/pages.rs"]
mod pages;
#[path = "../src/ui.rs"]
mod ui;

use pages::*;
use std::io::Write;
use ui::{C_DIM, Page, Panel, Ui};

pub const APP_BOARD: u32 = if cfg!(board = "fm1") {
    1
} else if cfg!(board = "smk37") {
    2
} else {
    0
};

/// The panel as a picture: RGB565, big endian, as the unit's has it.
struct Picture {
    fb: Vec<u8>,
    window: (u32, u32, u32, u32),
    pos: u32,
}

impl Panel for Picture {
    fn init(&mut self, _board: u32) -> bool {
        self.fb.fill(0);
        true
    }

    fn window(&mut self, x: u32, y: u32, w: u32, h: u32) {
        self.window = (x, y, w, h);
        self.pos = 0;
    }

    fn pixels(&mut self, p: &[u8]) {
        let (wx, wy, ww, wh) = self.window;
        for px in p.chunks_exact(2) {
            let (x, y) = (wx + self.pos % ww, wy + self.pos / ww);
            if y < wy + wh {
                let at = ((y * 240 + x) * 2) as usize;
                self.fb[at..at + 2].copy_from_slice(px);
            }
            self.pos += 1;
        }
    }

    fn end(&mut self) {}
}

// ---- PNG, uncompressed ----

fn crc32(data: &[u8]) -> u32 {
    !data.iter().fold(!0u32, |mut c, &b| {
        c ^= b as u32;
        for _ in 0..8 {
            c = if c & 1 != 0 { 0xedb8_8320 ^ c >> 1 } else { c >> 1 };
        }
        c
    })
}

fn chunk(out: &mut Vec<u8>, kind: &[u8; 4], data: &[u8]) {
    out.extend((data.len() as u32).to_be_bytes());
    let start = out.len();
    out.extend(kind);
    out.extend(data);
    let crc = crc32(&out[start..]);
    out.extend(crc.to_be_bytes());
}

/// An RGB PNG of the framebuffer, each 565 colour widened as tools/preview.py widened it.
fn png(fb: &[u8]) -> Vec<u8> {
    let mut raw = Vec::new();
    for row in fb.chunks(480) {
        raw.push(0);
        for px in row.chunks(2) {
            let v = (px[0] as u32) << 8 | px[1] as u32;
            raw.extend([
                ((v >> 11 & 31) * 255 / 31) as u8,
                ((v >> 5 & 63) * 255 / 63) as u8,
                ((v & 31) * 255 / 31) as u8,
            ]);
        }
    }
    // zlib with stored blocks
    let mut z = vec![0x78, 0x01];
    for (i, block) in raw.chunks(65535).enumerate() {
        let last = (i + 1) * 65535 >= raw.len();
        z.push(last as u8);
        z.extend((block.len() as u16).to_le_bytes());
        z.extend((!(block.len() as u16)).to_le_bytes());
        z.extend(block);
    }
    let (mut a, mut b) = (1u32, 0u32);
    for &x in &raw {
        a = (a + x as u32) % 65521;
        b = (b + a) % 65521;
    }
    z.extend((b << 16 | a).to_be_bytes());
    let mut out = b"\x89PNG\r\n\x1a\n".to_vec();
    let mut ihdr = Vec::new();
    ihdr.extend(240u32.to_be_bytes());
    ihdr.extend(240u32.to_be_bytes());
    ihdr.extend([8, 2, 0, 0, 0]);
    chunk(&mut out, b"IHDR", &ihdr);
    chunk(&mut out, b"IDAT", &z);
    chunk(&mut out, b"IEND", &[]);
    out
}

fn main() {
    let dir = std::path::PathBuf::from(std::env::args().nth(1).expect("usage: preview OUT_DIR"));
    let mut ui = Ui::new(Picture { fb: vec![0; 240 * 240 * 2], window: (0, 0, 1, 1), pos: 0 });
    let mut list = Vec::new();
    // The page, and what's on its status line on the unit.
    let shots: [(&str, &'static Page, &dyn Fn(&mut Ui<Picture>)); 24] = [
        ("PG_welcome", &WELCOME, &|u| u.status(b"version 0.4", C_DIM)),
        ("PG_unplug", &UNPLUG, &|_| ()),
        ("PG_connect", &CONNECT, &|_| ()),
        ("PG_switch_on", &SWITCH_ON, &|u| u.status("Waiting for it… 12 s".as_bytes(), C_DIM)),
        ("PG_already_on", &ALREADY_ON, &|_| ()),
        ("PG_no_answer", &NO_ANSWER, &|_| ()),
        ("PG_waking", &WAKING, &|_| ()),
        ("PG_connecting", &CONNECTING, &|_| ()),
        ("PG_found_fm1", &FOUND_FM1, &|u| u.progress(40, 100)),
        ("PG_found_smk37", &FOUND_SMK37, &|u| u.progress(70, 100)),
        ("PG_ready_fm1", &READY_FM1, &|u| u.status(b"Changes 155 of 255 sectors", C_DIM)),
        ("PG_ready_smk37", &READY_SMK37, &|u| u.status(b"Changes 192 of 255 sectors", C_DIM)),
        ("PG_nothing", &NOTHING, &|_| ()),
        ("PG_repairing", &REPAIRING, &|u| u.progress(120, 192)),
        ("PG_done_fm1", &DONE_FM1, &|_| ()),
        ("PG_done_smk37", &DONE_SMK37, &|_| ()),
        ("PG_left", &LEFT, &|_| ()),
        ("PG_err_connect", &ERR_CONNECT, &|u| u.status("usb 31 · 30000A0C".as_bytes(), C_DIM)),
        ("PG_err_other", &ERR_OTHER, &|_| ()),
        ("PG_err_unknown", &ERR_UNKNOWN, &|u| u.status(b"flash C84017 key 98", C_DIM)),
        ("PG_err_image", &ERR_IMAGE, &|_| ()),
        ("PG_err_read", &ERR_READ, &|_| ()),
        ("PG_err_write", &ERR_WRITE, &|u| u.status("VERIFY 1F · 00000000".as_bytes(), C_DIM)),
        ("PG_err_timer", &ERR_TIMER, &|_| ()),
    ];
    // (Board 0, as the C's preview had it: one board's build has its key names in the pages already.)
    ui.init(std::env::args().nth(2).map_or(0, |b| b.parse().expect("BOARD is a number")));
    for (name, page, extra) in shots {
        ui.show(page);
        extra(&mut ui);
        std::fs::write(dir.join(format!("{name}.png")), png(&ui.panel.fb)).expect("can't write the picture");
        writeln!(list, "{name}").unwrap();
    }
    std::fs::write(dir.join("pages.txt"), list).expect("can't write the list");
    println!("{}: {} pages", dir.display(), shots.len());
}
