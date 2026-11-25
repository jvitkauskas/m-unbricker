// SPDX-License-Identifier: MIT
//! The rescue install, after the Cardputer app's writer.cpp: every writable sector below 0xFF000 ends up exactly as
//! the image says (boot area, firmware, the rest erased). Never touched: 0xFF000-0xFFFFF and, on the SMK-37,
//! 0xC0000-0xC1FFF (Bluetooth pairing, factory calibration).
//!
//! The images come from rescue_img.rs (tools/make_images.py): the boot area as it is, the app area as stored blocks
//! plus regenerated padding (0xFF encrypted with the chip key per 32-byte block).
use crate::jl::Jl;
use crate::rescue_img as img;
use mboot::Aligned;

pub const SECTOR: u32 = 0x1000;
pub const W_END: u32 = 0xff000;
pub const SECTORS: usize = (W_END / SECTOR) as usize;
const BOOT_AREA: u32 = 0x4000;
const FLASH_SIZE: u32 = 0x10_0000;
const NONE: u32 = 0xffff_ffff;

pub struct Image {
    pub boot: &'static [u8; BOOT_AREA as usize],
    pub lit: &'static [u8],
    /// Runs of 32-byte blocks: the first, how many, and where in `lit` they are stored (or NONE: padding).
    pub runs: &'static [[u32; 3]],
    pub app_len: u32,
    pub boot_crc: u32,
    pub app_crc: u32,
    pub keep_lo: u32,
    pub keep_hi: u32,
}

#[derive(Clone, Copy, PartialEq, Eq)]
pub enum Which {
    Fm1,
    Smk37,
}

static IMAGES: [Image; 2] = [
    Image {
        boot: &img::FM1_BOOT,
        lit: &img::FM1_LIT,
        runs: &img::FM1_RUNS,
        app_len: img::FM1_APP_LEN,
        boot_crc: img::FM1_BOOT_CRC,
        app_crc: img::FM1_APP_CRC,
        keep_lo: img::FM1_KEEP_LO,
        keep_hi: img::FM1_KEEP_HI,
    },
    Image {
        boot: &img::SMK37_BOOT,
        lit: &img::SMK37_LIT,
        runs: &img::SMK37_RUNS,
        app_len: img::SMK37_APP_LEN,
        boot_crc: img::SMK37_BOOT_CRC,
        app_crc: img::SMK37_APP_CRC,
        keep_lo: img::SMK37_KEEP_LO,
        keep_hi: img::SMK37_KEEP_HI,
    },
];

pub fn image(which: Which) -> &'static Image {
    &IMAGES[(which == Which::Smk37) as usize]
}

/// Which sectors change, and how many are written, erased, and in how many runs.
pub struct Plan {
    pub change: [bool; SECTORS],
    pub to_write: u32,
    pub to_erase: u32,
    pub runs: u32,
}

impl Plan {
    pub const fn new() -> Plan {
        Plan { change: [false; SECTORS], to_write: 0, to_erase: 0, runs: 0 }
    }
}

/// Where it stopped, for the screen.
#[derive(Clone, Copy)]
pub struct Stop {
    pub what: &'static [u8],
    pub addr: u32,
}

/// The writer's buffers, its CRC-32 table (made the first time it's needed) and the last stop's address.
pub struct Writer {
    tgt: Aligned<{ SECTOR as usize }>,
    rd: Aligned<{ SECTOR as usize }>,
    crc_tab: [u32; 256],
    err_addr: u32,
    pub plan: Plan,
}

/// The 32 bytes of the app area at block `b`.
fn app_block(img: &Image, b: u32, out: &mut [u8]) {
    let run = img.runs.iter().find(|r| b >= r[0] && b < r[0] + r[1]);
    if let Some(r) = run.filter(|r| r[2] != NONE) {
        let at = (r[2] + (b - r[0]) * 32) as usize;
        out[..32].copy_from_slice(&img.lit[at..][..32]);
        return;
    }
    let mut key = (img::IMG_KEY ^ (b * 32) >> 2) & 0xffff;
    for o in &mut out[..32] {
        *o = 0xff ^ key as u8;
        key = (key << 1 ^ if key & 0x8000 != 0 { 0x1021 } else { 0 }) & 0xffff;
    }
}

/// The image's bytes for sector `s`: from the image, or made in `tgt`.
pub fn target<'a>(img: &'static Image, s: u32, tgt: &'a mut [u8; SECTOR as usize]) -> &'a [u8; SECTOR as usize] {
    let a = s * SECTOR;
    if a < BOOT_AREA {
        return img.boot[a as usize..].first_chunk().unwrap();
    }
    if a < BOOT_AREA + img.app_len {
        for (i, block) in tgt.chunks_mut(32).enumerate() {
            app_block(img, (a - BOOT_AREA) / 32 + i as u32, block);
        }
        return tgt;
    }
    *tgt = [0xff; SECTOR as usize];
    tgt
}

/// The zlib CRC-32, carried on from `crc`, with its table made the first time it's needed.
pub fn crc32(tab: &mut [u32; 256], crc: u32, d: &[u8]) -> u32 {
    if tab[1] == 0 {
        for (i, t) in tab.iter_mut().enumerate() {
            let mut c = i as u32;
            for _ in 0..8 {
                c = if c & 1 != 0 { 0xedb8_8320 ^ c >> 1 } else { c >> 1 };
            }
            *t = c;
        }
    }
    !d.iter().fold(!crc, |c, &b| tab[((c ^ b as u32) & 0xff) as usize] ^ c >> 8)
}

fn is_blank(d: &[u8]) -> bool {
    d.iter().all(|&b| b == 0xff)
}

pub fn is_protected(img: &Image, s: u32) -> bool {
    let a = s * SECTOR;
    a >= W_END || (a >= img.keep_lo && a < img.keep_hi)
}

impl Writer {
    pub const fn new() -> Writer {
        Writer {
            tgt: Aligned([0; SECTOR as usize]),
            rd: Aligned([0; SECTOR as usize]),
            crc_tab: [0; 256],
            err_addr: 0,
            plan: Plan::new(),
        }
    }

    fn target_crc(&mut self, img: &'static Image, first: u32, count: u32) -> u32 {
        let mut crc = 0;
        for s in first..first + count {
            crc = crc32(&mut self.crc_tab, crc, target(img, s, &mut self.tgt.0));
        }
        crc
    }

    /// The image regenerates to the build's CRCs.
    pub fn image_ok(&mut self, img: &'static Image) -> bool {
        if crc32(&mut self.crc_tab, 0, img.boot) != img.boot_crc {
            return false;
        }
        self.target_crc(img, BOOT_AREA / SECTOR, img.app_len / SECTOR) == img.app_crc
    }

    fn stop(&mut self, what: &'static [u8], addr: u32) -> Stop {
        self.err_addr = addr;
        Stop { what, addr }
    }

    /// Reads the unit's flash and compares it with the image, into `self.plan`.
    pub fn plan_make(
        &mut self,
        img: &'static Image,
        jl: &mut Jl,
        progress: &mut impl FnMut(u32, u32),
    ) -> Result<(), Stop> {
        let mut prev = false;
        let p = &mut self.plan;
        p.to_write = 0;
        p.to_erase = 0;
        p.runs = 0;
        for s in 0..SECTORS as u32 {
            let mut change = false;
            if !is_protected(img, s) {
                if !jl.read(s * SECTOR, &mut self.rd.0) {
                    self.err_addr = s * SECTOR;
                    return Err(Stop { what: b"READ", addr: s * SECTOR });
                }
                change = self.rd.0 != *target(img, s, &mut self.tgt.0);
            }
            let p = &mut self.plan;
            p.change[s as usize] = change;
            if change {
                if is_blank(target(img, s, &mut self.tgt.0)) {
                    p.to_erase += 1;
                } else {
                    p.to_write += 1;
                }
                if !prev {
                    p.runs += 1;
                }
            }
            prev = change;
            if s % 16 == 15 {
                progress(s + 1, SECTORS as u32);
            }
        }
        Ok(())
    }

    fn do_run(&mut self, img: &'static Image, jl: &mut Jl, first: u32, count: u32) -> Result<(), Stop> {
        self.err_addr = first * SECTOR;
        if !jl.erase(first * SECTOR, count * SECTOR) {
            return Err(self.stop(b"ERASE", first * SECTOR));
        }
        for s in first..first + count {
            let t = target(img, s, &mut self.tgt.0);
            if !is_blank(t) && !jl.program(s * SECTOR, t) {
                return Err(self.stop(b"WRITE", s * SECTOR));
            }
        }
        let addr = self.err_addr;
        let Some(chip) = jl.crc32(first * SECTOR, count * SECTOR) else {
            return Err(self.stop(b"CRC", addr));
        };
        if chip != self.target_crc(img, first, count) {
            return Err(self.stop(b"VERIFY", addr));
        }
        Ok(())
    }

    fn check_written(&mut self, img: &'static Image, jl: &mut Jl, lo: u32, hi: u32) -> Result<(), Stop> {
        self.err_addr = lo;
        let Some(chip) = jl.crc32(lo, hi - lo) else {
            return Err(self.stop(b"FINAL CRC", lo));
        };
        if chip != self.target_crc(img, lo / SECTOR, (hi - lo) / SECTOR) {
            return Err(self.stop(b"FINAL CHECK", lo));
        }
        Ok(())
    }

    /// Erases, programs and verifies run by run, then the final checks.
    pub fn plan_apply(
        &mut self,
        img: &'static Image,
        jl: &mut Jl,
        progress: &mut impl FnMut(u32, u32),
    ) -> Result<(), Stop> {
        let keep = img.keep_hi > img.keep_lo;
        let total = self.plan.to_write + self.plan.to_erase;
        let mut kept = 0;
        if keep {
            match jl.crc32(img.keep_lo, img.keep_hi - img.keep_lo) {
                Some(c) => kept = c,
                None => return Err(self.stop(b"CRC BEFORE", self.err_addr)),
            }
        }
        let Some(top) = jl.crc32(W_END, FLASH_SIZE - W_END) else {
            return Err(self.stop(b"CRC BEFORE", self.err_addr));
        };
        progress(0, total);
        let mut done = 0;
        let mut s = 0;
        while s < SECTORS {
            if !self.plan.change[s] {
                s += 1;
                continue;
            }
            let first = s;
            while s < SECTORS && self.plan.change[s] {
                s += 1;
            }
            let count = (s - first) as u32;
            // not verified twice
            if self.do_run(img, jl, first as u32, count).is_err() {
                self.do_run(img, jl, first as u32, count)?;
            }
            done += count;
            progress(done, total);
        }
        if keep {
            self.check_written(img, jl, 0, img.keep_lo)?;
            self.check_written(img, jl, img.keep_hi, W_END)?;
            if jl.crc32(img.keep_lo, img.keep_hi - img.keep_lo) != Some(kept) {
                return Err(self.stop(b"PROTECTED CHANGED", img.keep_lo));
            }
        } else {
            self.check_written(img, jl, 0, W_END)?;
        }
        if jl.crc32(W_END, FLASH_SIZE - W_END) != Some(top) {
            return Err(self.stop(b"PROTECTED CHANGED", W_END));
        }
        Ok(())
    }
}
