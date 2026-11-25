// SPDX-License-Identifier: MIT
//! JieLi UBOOT1.00 (the mask ROM's download mode) and M-Flasher's flash loader: a port of the Cardputer app's jl.cpp
//! (M-Flasher's jl.rs, MIT, after jl-uboot-tool by kagaimiq, MIT). Commands are 16-byte CDBs: a 16-bit opcode,
//! big-endian arguments, padded with 0xFF. The loader's replies are 16 bytes with the opcode echoed.
use crate::bot::{Bot, BotRes, Data};
use crate::regs::{bd16, bd32, bs32, ld32};
use crate::usbio::wait_ms;
use mboot::formats::crc16;

const IDENTIFY: u16 = 0xfc40;
const CRC32: u16 = 0xfc41;
const UNIQUE_ID: u16 = 0xfc42;
const WRITE_MEMORY: u16 = 0xfb06;
const JUMP: u16 = 0xfb08;
const READ_FLASH: u16 = 0xfd05;
const READ_KEY: u16 = 0xfc09;
const ONLINE_DEVICE: u16 = 0xfc0a;
const USB_BUFFER_SIZE: u16 = 0xfc14;
const ERASE_BLOCK: u16 = 0xfb00;
const ERASE_SECTOR: u16 = 0xfb01;
const WRITE_FLASH: u16 = 0xfb04;
const LOADER_ARG: u8 = 1;
const RETRIES: u32 = 4;
const READ_IO: u32 = 16384;

/// M-Flasher's flash loader (its data/mflr-loader-a.bin: m-bootloader's flash loader 1.3, MIT, Julius Vitkauskas),
/// linked for 0x01C20000.
const MFLR_ADDR_A: u32 = 0x01c2_0000;
static MFLR_A: [u8; 6016] = *include_bytes!("mflr-loader-a.bin");
/// The other slot, where the restart program goes.
const MFLR_ADDR_B: u32 = 0x01c2_8000;

/// M-Flasher's RESTART program: it clears the download-mode marker at 0x01C7FD80 and requests a software reset
/// (pi32v2-isa tools/hwtest/app/reset.S, MIT-0). Uploaded to the slot the loader is not in (`restart`), and run on
/// this unit at the end (src/flow.rs).
#[repr(align(4))]
pub struct Program(pub [u8; 34]);
pub static RESTART_PROG: Program = Program([
    0xc4, 0xff, 0x80, 0xfd, 0xc7, 0x01, 0x41, 0x20, 0xc1, 0x60, 0xc1, 0x61, 0xc1, 0x62, 0xc1, 0x63, 0x20, 0x00, 0x60,
    0xe0, 0x80, 0x4b, 0x41, 0x60, 0x42, 0x30, 0x21, 0x19, 0xc1, 0x60, 0x20, 0x00, 0xf7, 0x9f,
]);

/// What the loader said about the unit.
#[derive(Clone, Copy)]
pub struct Info {
    pub chip_key: u16,
    pub flash_id: u32,
    /// The loader's buffer: what a write sends at a time.
    pub io: u32,
    /// The loader's version, such as "1.3", NUL-terminated.
    pub loader: [u8; 4],
    /// Commands sent again, over all sessions.
    pub retries: u32,
    // (The C also kept the disk's vendor and product, the loader's address and clock and the unit's ID, which nothing
    // read.)
}

/// The session with the other unit: its disk, and what its loader said.
pub struct Jl {
    pub bot: Bot,
    pub info: Info,
}

/// The CRC cipher of the ROM's memory writes.
fn crc_cipher(data: &[u8], out: &mut [u8], key: u32) {
    const MAGIC: [u8; 16] =
        [0xc3, 0xcf, 0xc0, 0xe8, 0xce, 0xd2, 0xb0, 0xae, 0xc4, 0xe3, 0xa3, 0xac, 0xd3, 0xf1, 0xc1, 0xd6];
    let mut c = crc16(key as u16, &[(key >> 16) as u8, (key >> 24) as u8]);
    for (i, (o, &d)) in out.iter_mut().zip(data).enumerate() {
        c = crc16(c, &MAGIC[i % 16..][..1]);
        *o = d ^ c as u8;
    }
}

fn cdb(cmd: u16, args: &[u8]) -> [u8; 16] {
    let mut out = [0xff; 16];
    out[..2].copy_from_slice(&cmd.to_be_bytes());
    out[2..][..args.len()].copy_from_slice(args);
    out
}

/// A write command's arguments: the address, the length, and the data's CRC16.
fn write_args(addr: u32, k: u32, crc: u16) -> [u8; 9] {
    let mut args = [0u8; 9];
    bs32(&mut args, addr);
    args[4] = (k >> 8) as u8;
    args[5] = k as u8;
    args[7] = crc as u8;
    args[8] = (crc >> 8) as u8;
    args
}

/// Something went wrong: the step that failed.
pub type Step = u32;

impl Jl {
    pub const fn new() -> Jl {
        Jl { bot: Bot::new(), info: Info { chip_key: 0, flash_id: 0, io: 0, loader: [0; 4], retries: 0 } }
    }

    fn attempts(&mut self, c: &[u8; 16], data: &mut Data, moved: &mut u32, refusal_answers: bool) -> BotRes {
        let mut r = BotRes::Failed;
        for a in 1..=RETRIES {
            let d = match data {
                Data::In(b) => Data::In(b),
                Data::Out(b) => Data::Out(b),
            };
            r = self.bot.execute(c, d, moved);
            if r == BotRes::Ok || (r == BotRes::Refused && refusal_answers) {
                return r;
            }
            if a < RETRIES {
                self.info.retries += 1;
                wait_ms(250 * a);
            }
        }
        r
    }

    fn call16(&mut self, cmd: u16, args: &[u8], resp: &mut [u8; 16], probe: bool) -> BotRes {
        let c = cdb(cmd, args);
        let mut moved = 0;
        *resp = [0; 16];
        let r = self.attempts(&c, &mut Data::In(resp), &mut moved, probe);
        if r != BotRes::Ok {
            return r;
        }
        if bd16(resp) != cmd as u32 {
            // an unexpected answer
            return if probe { BotRes::Refused } else { BotRes::Failed };
        }
        BotRes::Ok
    }

    /// The loader's answer to IDENTIFY, if it runs: its version and its address.
    fn identify(&mut self) -> Option<([u8; 3], u32)> {
        let mut r = [0u8; 16];
        if self.call16(IDENTIFY, &[], &mut r, true) != BotRes::Ok || &r[2..6] != b"MFLR" {
            return None;
        }
        Some(([r[6], r[7], r[8]], bd32(&r[10..])))
    }

    fn upload(&mut self, addr: u32, img: &[u8]) -> bool {
        let mut block = [0u8; 512];
        let mut moved = 0;
        for (i, part) in img.chunks(512).enumerate() {
            let k = part.len();
            crc_cipher(part, &mut block[..k], 0xffff_ffff);
            let crc = crc16(0, &block[..k]);
            let c = cdb(WRITE_MEMORY, &write_args(addr + 512 * i as u32, k as u32, crc));
            if self.attempts(&c, &mut Data::Out(&block[..k]), &mut moved, false) != BotRes::Ok {
                return false;
            }
        }
        true
    }

    fn jump(&mut self, addr: u32) -> bool {
        let mut args = [0u8; 6];
        bs32(&mut args, addr);
        args[5] = LOADER_ARG;
        let c = cdb(JUMP, &args);
        let mut r = [0u8; 16];
        let mut moved = 0;
        for a in 1..=3 {
            if self.bot.execute(&c, Data::In(&mut r), &mut moved) == BotRes::Ok && bd16(&r) == JUMP as u32 {
                wait_ms(100);
                return true;
            }
            wait_ms(250 * a);
            if self.identify().is_some_and(|(_, at)| at == addr) {
                return true;
            }
        }
        false
    }

    /// INQUIRY, the loader uploaded and started, the unit's identity. Ok, or the step that failed.
    pub fn start(&mut self) -> Result<(), Step> {
        let c6 = [0x12, 0, 0, 0, 36, 0];
        let mut inq = [0u8; 36];
        let mut resp = [0u8; 16];
        let mut got = 0;
        let mut r = BotRes::Failed;
        for i in 1..=3 {
            r = self.bot.execute(&c6, Data::In(&mut inq), &mut got);
            if r == BotRes::Ok && got >= 32 {
                break;
            }
            r = BotRes::Failed;
            wait_ms(250 * i);
        }
        if r != BotRes::Ok {
            return Err(1);
        }
        // (The C kept the disk's vendor and product from the answer.)
        // Another loader that answers isn't ours to replace.
        if self.identify().is_none() && self.call16(ONLINE_DEVICE, &[], &mut resp, true) == BotRes::Ok {
            return Err(2);
        }
        if !self.upload(MFLR_ADDR_A, &MFLR_A) {
            return Err(3);
        }
        let Some((version, _)) = (if self.jump(MFLR_ADDR_A) { self.identify() } else { None }) else {
            return Err(4);
        };
        self.info.loader = [version[0], version[1], version[2], 0];
        if self.call16(READ_KEY, &[0x00, 0xac, 0x69, 0x00], &mut resp, false) != BotRes::Ok {
            return Err(5);
        }
        let mut plain = [0u8; 2];
        crc_cipher(&[resp[7], resp[6]], &mut plain, 0xffff_ffff);
        self.info.chip_key = u16::from_le_bytes(plain);
        if self.call16(ONLINE_DEVICE, &[], &mut resp, false) != BotRes::Ok {
            return Err(6);
        }
        self.info.flash_id = ld32(&resp[4..]);
        self.info.io = 512;
        if self.call16(USB_BUFFER_SIZE, &[], &mut resp, false) == BotRes::Ok {
            self.info.io = bd32(&resp[2..]).clamp(256, 4096);
        }
        self.call16(UNIQUE_ID, &[], &mut resp, false);
        Ok(())
    }

    pub fn read(&mut self, addr: u32, buf: &mut [u8]) -> bool {
        let n = buf.len() as u32;
        let mut done = 0;
        while done < n {
            let k = (n - done).min(READ_IO);
            let mut args = [0u8; 6];
            bs32(&mut args, addr + done);
            args[4] = (k >> 8) as u8;
            args[5] = k as u8;
            let c = cdb(READ_FLASH, &args);
            let mut ok = false;
            for a in 1..=RETRIES {
                let mut moved = 0;
                if self.bot.execute(&c, Data::In(&mut buf[done as usize..][..k as usize]), &mut moved) == BotRes::Ok
                    && moved == k
                {
                    ok = true;
                    break;
                } else if a < RETRIES {
                    self.info.retries += 1;
                    wait_ms(250 * a);
                }
            }
            if !ok {
                return false;
            }
            done += k;
        }
        true
    }

    pub fn crc32(&mut self, addr: u32, n: u32) -> Option<u32> {
        let mut args = [0u8; 8];
        let mut resp = [0u8; 16];
        bs32(&mut args, addr);
        bs32(&mut args[4..], n);
        if self.call16(CRC32, &args, &mut resp, false) != BotRes::Ok {
            return None;
        }
        Some(bd32(&resp[2..]))
    }

    // ---- writing ----

    /// Erases [addr, addr + n): 64 KiB blocks where aligned, else 4 KiB sectors.
    pub fn erase(&mut self, addr: u32, n: u32) -> bool {
        let (mut a, end) = (addr, addr + n);
        let mut resp = [0u8; 16];
        while a < end {
            let block = a % 0x10000 == 0 && end - a >= 0x10000;
            if self.call16(if block { ERASE_BLOCK } else { ERASE_SECTOR }, &a.to_be_bytes(), &mut resp, false)
                != BotRes::Ok
            {
                return false;
            }
            a += if block { 0x10000 } else { 0x1000 };
        }
        true
    }

    /// Programs erased flash in chunks of the loader's buffer size; all-0xFF chunks are skipped. The loader checks the
    /// CRC16 of the plain data.
    pub fn program(&mut self, addr: u32, data: &[u8]) -> bool {
        let io = if self.info.io != 0 { self.info.io } else { 512 };
        let mut moved = 0;
        for (i, part) in data.chunks(io as usize).enumerate() {
            if part.iter().all(|&b| b == 0xff) {
                continue;
            }
            let c = cdb(WRITE_FLASH, &write_args(addr + io * i as u32, part.len() as u32, crc16(0, part)));
            if self.attempts(&c, &mut Data::Out(part), &mut moved, false) != BotRes::Ok {
                return false;
            }
        }
        true
    }

    /// A harmless command that keeps the session busy.
    pub fn ping(&mut self) -> bool {
        self.identify().is_some()
    }

    /// The restart program, uploaded to the slot the loader is not in, and started; its answer may be lost.
    pub fn restart(&mut self) -> bool {
        if !self.upload(MFLR_ADDR_B, &RESTART_PROG.0) {
            return false;
        }
        let mut args = [0u8; 6];
        bs32(&mut args, MFLR_ADDR_B);
        args[5] = LOADER_ARG;
        let mut r = [0u8; 16];
        let mut moved = 0;
        self.bot.execute(&cdb(JUMP, &args), Data::In(&mut r), &mut moved);
        true
    }
}
