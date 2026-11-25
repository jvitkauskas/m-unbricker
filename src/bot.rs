// SPDX-License-Identifier: MIT
//! The enumeration of the ROM's download-mode disk, and USB mass storage bulk-only transport with JieLi's 16-byte
//! commands, after the Cardputer app's uboot_host.cpp (the same handling of stalls and early status blocks).
use crate::husb::{Husb, Res};
use crate::regs::{ld16, ld32, st16, st32};
use crate::usbio::wait_ms;
use mboot::Aligned;

/// "USBC"
const CBW_SIG: u32 = 0x4342_5355;
/// "USBS"
const CSW_SIG: u32 = 0x5342_5355;
const TIMEOUT_MS: u32 = 5000;
const ADDR: u8 = 1;

#[derive(Clone, Copy, PartialEq, Eq, Debug)]
pub enum BotRes {
    Ok,
    Refused,
    Failed,
}

#[derive(Clone, Copy)]
pub struct DevInfo {
    pub vid: u32,
    pub pid: u32,
    pub mps0: u8,
    pub iface: u8,
    pub ep_in: u8,
    pub ep_out: u8,
    pub mps: u16,
}

impl DevInfo {
    pub const fn new() -> DevInfo {
        DevInfo { vid: 0, pid: 0, mps0: 0, iface: 0, ep_in: 0, ep_out: 0, mps: 0 }
    }
}

/// A command's data stage: none, in from the device, or out to it.
pub enum Data<'a> {
    In(&'a mut [u8]),
    Out(&'a [u8]),
}

impl Data<'_> {
    fn len(&self) -> u32 {
        match self {
            Data::In(b) => b.len() as u32,
            Data::Out(b) => b.len() as u32,
        }
    }
}

/// The disk: the controller, the device found on it, and the transport's state.
pub struct Bot {
    pub usb: Husb,
    pub dev: DevInfo,
    /// The device is known (enumerated far enough to have its EP0 size).
    has_dev: bool,
    tag: u32,
    cfg: Aligned<256>,
    io: Aligned<64>,
    // (The C kept what went wrong last as text, which nothing showed.)
}

impl Bot {
    pub const fn new() -> Bot {
        Bot {
            usb: Husb::new(),
            dev: DevInfo::new(),
            has_dev: false,
            tag: 0,
            cfg: Aligned([0; 256]),
            io: Aligned([0; 64]),
        }
    }

    /// A control request to `addr`, with `len` bytes of data in `cfg` (or none), tried three times unless it stalls.
    #[allow(clippy::too_many_arguments)]
    fn ctl(&mut self, addr: u8, kind: u8, req: u8, val: u16, idx: u16, len: u16, got: Option<&mut u32>) -> Res {
        let mut s = [0u8; 8];
        let mut n = len as u32;
        let mut r = Res::Error;
        s[0] = kind;
        s[1] = req;
        st16(&mut s[2..], val as u32);
        st16(&mut s[4..], idx as u32);
        st16(&mut s[6..], len as u32);
        let mps0 = if self.has_dev { self.dev.mps0 } else { 8 };
        for _ in 0..3 {
            r = self.usb.control(addr, mps0, &s, &mut self.cfg.0[..len as usize], &mut n);
            if r == Res::Ok || r == Res::Stall {
                break;
            }
            wait_ms(20);
        }
        if let Some(got) = got {
            *got = n;
        }
        r
    }

    fn find_bulk(&mut self, total: u32) -> bool {
        let p = &self.cfg.0;
        // (A descriptor that runs off the end reads as zeros.)
        let at = |i: u32| p.get(i as usize).copied().unwrap_or(0);
        let (mut cur, mut found) = (-1, -1);
        let (mut ep_in, mut ep_out, mut mps) = (0, 0, 64u16);
        let mut i = 0;
        while i + 1 < total && at(i) >= 2 {
            if at(i + 1) == 4 {
                if found < 0 {
                    ep_in = 0;
                    ep_out = 0;
                }
                cur = at(i + 2) as i32;
            } else if at(i + 1) == 5 && found < 0 && at(i + 3) & 3 == 2 {
                if at(i + 2) & 0x80 != 0 {
                    ep_in = at(i + 2) & 0x0f;
                    mps = at(i + 4) as u16 | (at(i + 5) as u16) << 8;
                } else {
                    ep_out = at(i + 2) & 0x0f;
                }
                if ep_in != 0 && ep_out != 0 {
                    found = cur;
                }
            }
            i += at(i) as u32;
        }
        if found < 0 {
            return false;
        }
        self.dev.iface = found as u8;
        self.dev.ep_in = ep_in;
        self.dev.ep_out = ep_out;
        self.dev.mps = if mps != 0 { mps } else { 64 };
        true
    }

    /// Reset, address 1, descriptors, configuration, bulk endpoints. Ok(()), or the step that failed (1..6) with the
    /// controller's result.
    pub fn enumerate(&mut self) -> Result<(), (u32, Res)> {
        let mut got = 0;
        self.has_dev = false;
        self.dev.mps0 = 8;
        self.usb.bus_reset();
        wait_ms(20);
        // the first 8 bytes of the device descriptor at address 0: the EP0 size
        let r = self.ctl(0, 0x80, 6, 0x0100, 0, 8, Some(&mut got));
        if r != Res::Ok || got < 8 {
            return Err((1, r));
        }
        self.dev.mps0 = if self.cfg.0[7] != 0 { self.cfg.0[7] } else { 8 };
        self.has_dev = true;
        self.usb.bus_reset();
        wait_ms(20);
        // SET_ADDRESS
        let r = self.ctl(0, 0x00, 5, ADDR as u16, 0, 0, None);
        if r != Res::Ok {
            return Err((2, r));
        }
        wait_ms(10);
        let r = self.ctl(ADDR, 0x80, 6, 0x0100, 0, 18, Some(&mut got));
        if r != Res::Ok || got < 18 {
            return Err((3, r));
        }
        self.dev.vid = ld16(&self.cfg.0[8..]);
        self.dev.pid = ld16(&self.cfg.0[10..]);
        let r = self.ctl(ADDR, 0x80, 6, 0x0200, 0, 9, Some(&mut got));
        if r != Res::Ok || got < 9 {
            return Err((4, r));
        }
        let total = ld16(&self.cfg.0[2..]).min(self.cfg.0.len() as u32);
        let r = self.ctl(ADDR, 0x80, 6, 0x0200, 0, total as u16, Some(&mut got));
        if r != Res::Ok || !self.find_bulk(got) {
            return Err((5, r));
        }
        // SET_CONFIGURATION
        let r = self.ctl(ADDR, 0x00, 9, self.cfg.0[5] as u16, 0, 0, None);
        if r != Res::Ok {
            return Err((6, r));
        }
        let d = self.dev;
        self.usb.bulk_setup(ADDR, d.ep_in, d.ep_out, d.mps);
        Ok(())
    }

    // ---- BOT ----

    fn clear_halt(&mut self, input: bool) {
        let ep = if input { 0x80 | self.dev.ep_in as u16 } else { self.dev.ep_out as u16 };
        self.ctl(ADDR, 0x02, 1, 0, ep, 0, None);
        self.usb.reset_toggle(input);
    }

    fn reset_recovery(&mut self) {
        self.ctl(ADDR, 0x21, 0xff, 0, self.dev.iface as u16, 0, None);
        self.clear_halt(true);
        self.clear_halt(false);
    }

    fn transfer(&mut self, cdb: &[u8], data: Data, moved: &mut u32) -> BotRes {
        let (mut cbw, mut csw) = ([0u8; 31], [0u8; 16]);
        let len = data.len();
        let mut n = 0;
        let mut early = false;
        *moved = 0;
        self.tag = self.tag.wrapping_add(1);
        st32(&mut cbw, CBW_SIG);
        st32(&mut cbw[4..], self.tag);
        st32(&mut cbw[8..], len);
        cbw[12] = if matches!(data, Data::In(_)) { 0x80 } else { 0 };
        cbw[14] = cdb.len() as u8;
        cbw[15..][..cdb.len()].copy_from_slice(cdb);
        if self.usb.bulk_out(&cbw, TIMEOUT_MS) != Res::Ok {
            return BotRes::Failed;
        }
        match data {
            Data::In(buf) if len != 0 => {
                let r = self.usb.bulk_in(buf, &mut n, TIMEOUT_MS);
                if r == Res::Stall {
                    // a stalled data phase: the status follows
                    self.clear_halt(true);
                } else if r != Res::Ok {
                    return BotRes::Failed;
                } else if n == 13 && len > 13 && ld32(buf) == CSW_SIG && ld32(&buf[4..]) == self.tag {
                    // a status block in place of the data
                    csw[..13].copy_from_slice(&buf[..13]);
                    early = true;
                } else {
                    *moved = n;
                }
            }
            Data::Out(buf) if len != 0 => {
                let r = self.usb.bulk_out(buf, TIMEOUT_MS);
                if r == Res::Stall {
                    self.clear_halt(false);
                } else if r != Res::Ok {
                    return BotRes::Failed;
                } else {
                    *moved = len;
                }
            }
            _ => {}
        }
        if !early {
            let mut r = self.usb.bulk_in(&mut self.io.0, &mut n, TIMEOUT_MS);
            if r == Res::Stall {
                self.clear_halt(true);
                r = self.usb.bulk_in(&mut self.io.0, &mut n, TIMEOUT_MS);
            }
            if r != Res::Ok || n != 13 {
                return BotRes::Failed;
            }
            csw[..13].copy_from_slice(&self.io.0[..13]);
        }
        if ld32(&csw) != CSW_SIG || ld32(&csw[4..]) != self.tag {
            return BotRes::Failed;
        }
        match csw[12] {
            0 => BotRes::Ok,
            1 => BotRes::Refused,
            _ => BotRes::Failed,
        }
    }

    /// One command: the CBW, the data stage, the CSW. `moved` gets the data bytes moved.
    pub fn execute(&mut self, cdb: &[u8], data: Data, moved: &mut u32) -> BotRes {
        let r = self.transfer(cdb, data, moved);
        if r == BotRes::Failed && self.usb.attached() {
            self.reset_recovery();
        }
        r
    }
}
