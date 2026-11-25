// SPDX-License-Identifier: MIT
//! The image packing on this computer: what src/writer.rs makes of a build/rescue_img.rs (tools/make_images.py), sector
//! by sector, with its check. tests/images_test.py compares it with the C's (tests/c/images.c).
//!
//!   images        (built with RESCUE_IMG=<rescue_img.rs> in its environment)
//!
//! prints, for each image: whether it checks, then each sector's CRC-32 and whether it is protected.
#![allow(dead_code)]
#[path = "../src/bot.rs"]
mod bot;
#[path = "../src/husb.rs"]
mod husb;
#[path = "../src/jl.rs"]
mod jl;
#[path = "../src/regs.rs"]
mod regs;
mod rescue_img {
    include!(env!("RESCUE_IMG"));
}
#[path = "../src/usbio.rs"]
mod usbio;
#[path = "../src/writer.rs"]
mod writer;

use writer::{SECTOR, SECTORS, Which, Writer};

fn main() {
    let mut w = Box::new(Writer::new());
    let mut tab = [0u32; 256];
    let mut tgt = [0u8; SECTOR as usize];
    for (name, which) in [("FM-1", Which::Fm1), ("SMK-37", Which::Smk37)] {
        let img = writer::image(which);
        println!("{name} ok {}", w.image_ok(img) as u32);
        for s in 0..SECTORS as u32 {
            let crc = writer::crc32(&mut tab, 0, writer::target(img, s, &mut tgt));
            println!("{s} {crc:08x} {}", writer::is_protected(img, s) as u32);
        }
    }
}
