// SPDX-License-Identifier: MIT
//! USB0 in host mode, full speed, polled: one device, control and bulk only. Register sequences: docs/husb-notes.md
//! (read from the JieLi AC79 SDK's USB library, Apache-2.0, and the fm1-base device driver). EP0 for control, hardware
//! EP1 TX + RX for bulk.
use core::cell::UnsafeCell;
use crate::regs::{Reg, USB_CON0 as CON0, USB_CON1 as CON1, bit, sync_cpu, sync_data};
use crate::usbio::{ms_since, now, wait_ms};

const EP0_CNT: Reg = Reg::at::<0x11808>();
const EP1_CNT: Reg = Reg::at::<0x1180c>();
const EP0_ADR: Reg = Reg::at::<0x11818>();
const EP1_TADR: Reg = Reg::at::<0x1181c>();
const EP1_RADR: Reg = Reg::at::<0x11820>();
const IO_CON0: Reg = Reg::at::<0x51000>();
const IO_CON1: Reg = Reg::at::<0x51004>();
const CLK_CON1: Reg = Reg::at::<0x10010>();

const CON0_DP_SE: u32 = bit(17);
const CON0_DM_SE: u32 = bit(16);

// The SIE's registers, through CON1.
const FADDR: u32 = 0;
const POWER: u32 = 1;
const INTRUSB: u32 = 6;
const INTRTX1E: u32 = 7;
const INTRTX2E: u32 = 8;
const INTRRX1E: u32 = 9;
const INTRRX2E: u32 = 10;
const INTRUSBE: u32 = 11;
const INDEX: u32 = 14;
const DEVCTL: u32 = 15;
const TXMAXP: u32 = 16;
const CSR0: u32 = 17;
const TXCSR1: u32 = 17;
const TXCSR2: u32 = 18;
const RXMAXP: u32 = 19;
const RXCSR1: u32 = 20;
const RXCSR2: u32 = 21;
const COUNT0: u32 = 22;
const RXCOUNT1: u32 = 22;
const RXCOUNT2: u32 = 23;
const TXTYPE: u32 = 24;
const RXTYPE: u32 = 26;

#[derive(Clone, Copy, PartialEq, Eq, Debug)]
pub enum Res {
    Ok,
    Stall,
    Timeout,
    Error,
    // (The C had a fifth, "gone", which nothing returned.)
}

/// A buffer the controller's DMA reads or writes, which no reference ever covers: it's apart from the controller's
/// state, which the code borrows, and its bytes are reached only through pointers, volatile, when the DMA has finished
/// with them.
#[repr(C, align(4))]
struct DmaBuf<const N: usize>(UnsafeCell<[u8; N]>);

// Safety: one thread; the controller is the only other party, and only pointers reach the bytes.
unsafe impl<const N: usize> Sync for DmaBuf<N> {}

impl<const N: usize> DmaBuf<N> {
    const fn new() -> Self {
        DmaBuf(UnsafeCell::new([0; N]))
    }

    fn bytes(&self) -> *mut u8 {
        self.0.get().cast()
    }

    fn address(&self) -> u32 {
        mboot::hw::dma_address(self.bytes())
    }

    /// Bytes for the DMA to send.
    fn put(&self, data: &[u8]) {
        assert!(data.len() <= N);
        for (i, &b) in data.iter().enumerate() {
            // Safety: byte i, inside the buffer as checked.
            unsafe { self.bytes().add(i).write_volatile(b) };
        }
    }

    /// The first `out.len()` bytes the DMA wrote.
    fn take(&self, out: &mut [u8]) {
        assert!(out.len() <= N);
        for (i, o) in out.iter_mut().enumerate() {
            // Safety: byte i, inside the buffer as checked.
            *o = unsafe { self.bytes().add(i).read_volatile() };
        }
    }
}

// The controller's DMA writes past a full packet when receiving, so the receive buffers have room to spare.
static EP0BUF: DmaBuf<72> = DmaBuf::new();
static TXBUF: DmaBuf<64> = DmaBuf::new();
static RXBUF: DmaBuf<72> = DmaBuf::new();

/// The controller's state. Its DMA buffers are EP0BUF, TXBUF and RXBUF.
pub struct Husb {
    b_addr: u8,
    b_in: u8,
    b_out: u8,
    b_mps: u32,
    sie_timeouts: u32,
    /// The last error's detail for the screen: a register snapshot.
    pub dbg: [u32; 4],
}

impl Husb {
    pub const fn new() -> Husb {
        Husb {
            b_addr: 0,
            b_in: 0,
            b_out: 0,
            b_mps: 0,
            sie_timeouts: 0,
            dbg: [0; 4],
        }
    }

    fn sie_wr(&mut self, r: u32, v: u32) {
        let mut n = 20001;
        CON1.write(r << 8 & 0xff_ff00 | v & 0xff);
        sync_cpu();
        while CON1.read() & 0x8000 == 0 {
            n -= 1;
            if n == 0 {
                self.sie_timeouts += 1;
                return;
            }
        }
    }

    fn sie_rd(&mut self, r: u32) -> u32 {
        self.sie_try_rd(r).unwrap_or(0)
    }

    /// A register's value, or None if the read timed out. Where a clear bit means done (a packet sent, a SETUP
    /// taken), a timed-out read must not count as one: the C's 0 for it did.
    fn sie_try_rd(&mut self, r: u32) -> Option<u32> {
        let mut n = 20001;
        CON1.write(r << 8 & 0xff_bf00 | 0x4000);
        sync_cpu();
        while CON1.read() & 0x8000 == 0 {
            n -= 1;
            if n == 0 {
                self.sie_timeouts += 1;
                return None;
            }
        }
        Some(CON1.read() & 0xff)
    }

    fn snapshot(&mut self, code: u32, csr: u32) {
        self.dbg[0] = CON0.read();
        self.dbg[1] =
            self.sie_rd(DEVCTL) | self.sie_rd(POWER) << 8 | self.sie_rd(FADDR) << 16 | self.sie_rd(INTRUSB) << 24;
        self.dbg[2] = csr;
        self.dbg[3] = code << 24 | self.sie_timeouts & 0xff_ffff;
    }

    // ---- bring-up ----

    /// The controller into host mode (the pins stay with the controller afterwards).
    pub fn init(&mut self) {
        CON0.write(0);
        CON1.write(0);
        IO_CON0.write(0x0c);
        wait_ms(20);
        // USB clock: PLL 48 MHz
        CLK_CON1.clear(3);
        IO_CON0.write(0);
        IO_CON1.write(0);
        wait_ms(2);
        // digital inputs, no pull-ups, host pull-downs, pads to the PHY
        IO_CON0.write(bit(9) | bit(10) | bit(4) | bit(5));
        // PHY on, SIE out of reset, VBUS valid; ID 0 = host
        CON0.set(0x25);
        wait_ms(2);
        EP0_ADR.write(EP0BUF.address());
        self.sie_timeouts = 0;
    }

    fn session(&mut self) -> bool {
        let mut d = 0;
        self.sie_wr(POWER, 0x60);
        self.sie_wr(INTRUSBE, 0);
        self.sie_wr(INTRTX1E, 0);
        self.sie_wr(INTRTX2E, 0);
        self.sie_wr(INTRRX1E, 0);
        self.sie_wr(INTRRX2E, 0);
        // session, host request
        self.sie_wr(DEVCTL, 0x03);
        let t0 = now();
        while ms_since(t0) < 500 {
            d = self.sie_rd(DEVCTL);
            if d & 0x04 != 0 && d & 0x40 != 0 {
                // host mode, full-speed device
                return true;
            }
            if d & 0x04 != 0 && d & 0x20 != 0 {
                // low speed (not expected)
                CON0.set(2);
                return true;
            }
            wait_ms(10);
        }
        self.snapshot(1, d);
        false
    }

    /// True once a full-speed device is attached (D+ pulled up), false on timeout.
    pub fn wait_attach(&mut self, timeout_ms: u32) -> bool {
        let t0 = now();
        let mut high = 0;
        while ms_since(t0) < timeout_ms {
            if CON0.read() & CON0_DP_SE != 0 {
                high += 1;
                // 100 ms debounce
                if high >= 100 {
                    return self.session();
                }
            } else {
                high = 0;
            }
            wait_ms(1);
        }
        self.snapshot(2, 0);
        false
    }

    pub fn attached(&self) -> bool {
        (0..16).any(|_| CON0.read() & (CON0_DP_SE | CON0_DM_SE) != 0)
    }

    /// A bus reset (~50 ms SE0), then the bus runs (SOFs).
    pub fn bus_reset(&mut self) {
        let mut n = 0;
        loop {
            let p = self.sie_rd(POWER);
            self.sie_wr(POWER, p & 0xf7 | 0x08);
            n += 1;
            if p & 0x08 != 0 || n >= 10 {
                break;
            }
        }
        wait_ms(50);
        let p = self.sie_rd(POWER);
        self.sie_wr(POWER, p & 0xf7);
        wait_ms(40);
    }

    // ---- control ----

    fn csr0_w(&mut self, v: u32) {
        self.sie_wr(INDEX, 0);
        self.sie_wr(CSR0, v);
    }

    /// Waits until (csr & mask) == want; a STALL, an error or a timeout ends it.
    fn ep0_wait(&mut self, mask: u32, want: u32, stage: u32) -> Res {
        let t0 = now();
        let mut c = 0;
        loop {
            self.sie_wr(INDEX, 0);
            // (a read that timed out: only the time limit ends the wait)
            if let Some(v) = self.sie_try_rd(CSR0) {
                c = v;
                if c & 0x04 != 0 {
                    self.snapshot(0x10 + stage, c);
                    self.csr0_w(0);
                    return Res::Stall;
                }
                if c & 0x10 != 0 {
                    self.snapshot(0x20 + stage, c);
                    self.csr0_w(0);
                    return Res::Error;
                }
                if c & mask == want {
                    return Res::Ok;
                }
            }
            if ms_since(t0) > 1000 {
                self.snapshot(0x30 + stage, c);
                self.csr0_w(0);
                return Res::Timeout;
            }
        }
    }

    /// A control transfer to `addr` (endpoint 0, max packet `mps0`). The direction and wLength come from `setup`; the
    /// data stage moves bytes to or from `data`, which has room for wLength bytes; `len` gets how many were moved.
    pub fn control(&mut self, addr: u8, mut mps0: u8, setup: &[u8; 8], data: &mut [u8], len: &mut u32) -> Res {
        let want = crate::regs::ld16(&setup[6..]);
        let input = setup[0] & 0x80 != 0;
        let mut got = 0;
        *len = 0;
        if mps0 == 0 || mps0 > 64 {
            mps0 = 8;
        }
        self.sie_wr(FADDR, addr as u32);
        EP0BUF.put(setup);
        sync_cpu();
        EP0_CNT.write(8);
        // SETUP
        self.csr0_w(0x0a);
        let r = self.ep0_wait(0x02, 0, 1);
        if r != Res::Ok {
            return r;
        }
        let mut left = want;
        if want != 0 && input {
            self.csr0_w(0x20);
            while left != 0 {
                let r = self.ep0_wait(0x01, 0x01, 2);
                if r != Res::Ok {
                    return r;
                }
                self.sie_wr(INDEX, 0);
                let n = self.sie_rd(COUNT0).min(left);
                sync_data();
                // (The buffer holds 72 bytes: a longer packet than that can't come at full speed.)
                let k = (n as usize).min(72);
                EP0BUF.take(&mut data[got as usize..][..k]);
                got += n;
                left -= n;
                if n < mps0 as u32 {
                    left = 0;
                }
                self.csr0_w(if left != 0 { 0x20 } else { 0x00 });
            }
        } else if want != 0 {
            while left != 0 {
                let n = left.min(mps0 as u32);
                EP0BUF.put(&data[got as usize..][..n as usize]);
                sync_cpu();
                EP0_CNT.write(n);
                self.csr0_w(0x02);
                let r = self.ep0_wait(0x02, 0, 3);
                if r != Res::Ok {
                    return r;
                }
                got += n;
                left -= n;
            }
        }
        *len = got;
        if want != 0 && input {
            // status OUT: zero-length
            EP0_CNT.write(0);
            self.csr0_w(0x42);
            return self.ep0_wait(0x02, 0, 4);
        }
        // status IN
        self.csr0_w(0x60);
        let r = self.ep0_wait(0x01, 0x01, 5);
        self.csr0_w(0);
        r
    }

    // ---- bulk ----

    /// Configures the bulk pipes: the device's address, IN and OUT endpoint numbers (without the direction bit), max
    /// packet size. Resets both toggles.
    pub fn bulk_setup(&mut self, addr: u8, ep_in: u8, ep_out: u8, mps: u16) {
        self.b_addr = addr;
        self.b_in = ep_in;
        self.b_out = ep_out;
        self.b_mps = if mps != 0 && mps <= 64 { mps as u32 } else { 64 };
        EP1_RADR.write(RXBUF.address());
        EP1_TADR.write(TXBUF.address());
        self.sie_wr(INDEX, 1);
        self.sie_wr(RXMAXP, 0xff);
        // toggle DATA0, flush
        self.sie_wr(RXCSR1, 0x90);
        self.sie_wr(RXCSR2, 0);
        self.sie_wr(TXMAXP, 0xff);
        // toggle DATA0, flush
        self.sie_wr(TXCSR1, 0x48);
        self.sie_wr(TXCSR2, 0);
    }

    /// The data toggle back to DATA0 (after CLEAR_FEATURE(ENDPOINT_HALT)).
    pub fn reset_toggle(&mut self, input: bool) {
        self.sie_wr(INDEX, 1);
        if input {
            self.sie_wr(RXCSR1, 0x90);
        } else {
            self.sie_wr(TXCSR1, 0x48);
        }
    }

    /// A bulk OUT of all of `buf`.
    pub fn bulk_out(&mut self, mut buf: &[u8], timeout_ms: u32) -> Res {
        let t0 = now();
        let mut errs = 0;
        self.sie_wr(FADDR, self.b_addr as u32);
        while !buf.is_empty() {
            let n = buf.len().min(self.b_mps as usize);
            TXBUF.put(&buf[..n]);
            sync_cpu();
            'resend: loop {
                self.sie_wr(INDEX, 1);
                self.sie_wr(TXTYPE, 0x20 | self.b_out as u32);
                EP1_CNT.write(n as u32);
                sync_cpu();
                self.sie_wr(TXCSR1, 0x01);
                let mut c = 0;
                loop {
                    // (a read that timed out: only the time limit ends the wait)
                    if let Some(v) = self.sie_try_rd(TXCSR1) {
                        c = v;
                        if c & 0x20 != 0 {
                            self.snapshot(0x40, c);
                            self.sie_wr(TXCSR1, c & !0x21);
                            return Res::Stall;
                        }
                        if c & 0x84 != 0 {
                            // NAK timeout or 3 errors: send it again
                            self.sie_wr(TXCSR1, c & !0x85);
                            errs += 1;
                            if errs > 5 {
                                self.snapshot(0x41, c);
                                return Res::Error;
                            }
                            continue 'resend;
                        }
                        if c & 0x01 == 0 {
                            // ACKed
                            break 'resend;
                        }
                    }
                    if ms_since(t0) > timeout_ms {
                        self.snapshot(0x42, c);
                        // flush
                        self.sie_wr(TXCSR1, 0x08);
                        return Res::Timeout;
                    }
                }
            }
            buf = &buf[n..];
        }
        Res::Ok
    }

    /// A bulk IN of up to `buf.len()` bytes, which stops at a short packet; `got` gets the bytes received.
    pub fn bulk_in(&mut self, buf: &mut [u8], got: &mut u32, timeout_ms: u32) -> Res {
        let mut t0 = now();
        let mut errs = 0;
        let mut left = buf.len() as u32;
        *got = 0;
        self.sie_wr(FADDR, self.b_addr as u32);
        self.sie_wr(INDEX, 1);
        self.sie_wr(RXTYPE, 0x20 | self.b_in as u32);
        // request a packet
        self.sie_wr(RXCSR1, 0x20);
        loop {
            let c = self.sie_rd(RXCSR1);
            if c & 0x01 != 0 {
                let n = self.sie_rd(RXCOUNT1) | self.sie_rd(RXCOUNT2) << 8;
                sync_data();
                let m = n.min(left);
                // (As for EP0: no full-speed packet is longer than the buffer.)
                let k = (m as usize).min(72);
                RXBUF.take(&mut buf[*got as usize..][..k]);
                // release the buffer
                self.sie_wr(RXCSR1, 0x10);
                *got += m;
                left -= m;
                if left == 0 || n < self.b_mps {
                    return Res::Ok;
                }
                self.sie_wr(RXTYPE, 0x20 | self.b_in as u32);
                self.sie_wr(RXCSR1, 0x20);
                t0 = now();
                continue;
            }
            if c & 0x40 != 0 {
                self.snapshot(0x50, c);
                self.sie_wr(RXCSR1, 0);
                return Res::Stall;
            }
            if c & 0x0c != 0 {
                // an error or a NAK timeout: ask again
                errs += 1;
                if errs > 5 {
                    self.snapshot(0x51, c);
                    self.sie_wr(RXCSR1, 0);
                    return Res::Error;
                }
                self.sie_wr(RXCSR1, 0x20);
                continue;
            }
            if ms_since(t0) > timeout_ms {
                self.snapshot(0x52, c);
                self.sie_wr(RXCSR1, 0);
                self.sie_wr(RXCSR1, 0x10);
                return Res::Timeout;
            }
        }
    }
}
