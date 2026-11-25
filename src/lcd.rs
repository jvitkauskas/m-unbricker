// SPDX-License-Identifier: MIT
//! The 240x240 ST7789 panel on SPI1: m-bootloader's panel driver (MIT, Julius Vitkauskas, its 1.4 src/hw/disp.c)
//! without its 5x7 text, with a window and pixel interface for the strip renderer (src/ui.rs). Pixels are RGB565, big
//! endian.
use crate::board::{self, FM1, SMK};
use crate::regs::{IO_MUX1, PA, PB, PC, Reg, bit, dma, gp_dir_out, gp_set, sync_cpu};
use crate::ui::Panel;
use crate::usbio::delay_us;
use mboot::Aligned;

const S1CON: Reg = Reg::at::<0x11d00>();
const S1BAUD: Reg = S1CON.add(4);
const S1BUF: Reg = S1CON.add(8);
const S1ADR: Reg = S1CON.add(12);
const S1CNT: Reg = S1CON.add(16);
const SPI_CLR: u32 = bit(14);
const SPI_DONE: u32 = bit(15);

/// (port, pin) of each line.
struct Pins {
    clk: (u32, u32),
    mosi: (u32, u32),
    cs: (u32, u32),
    dc: (u32, u32),
}

const PIN_FM1: Pins = Pins { clk: (PC, 9), mosi: (PC, 10), cs: (PC, 7), dc: (PC, 8) };
const PIN_SMK: Pins = Pins { clk: (PB, 6), mosi: (PB, 7), cs: (PC, 8), dc: (PB, 5) };

const GAMMA_SMK: [u8; 14] = [0xd0, 0x08, 0x0e, 0x09, 0x09, 0x05, 0x31, 0x33, 0x48, 0x17, 0x14, 0x15, 0x31, 0x34];

pub struct Lcd {
    /// The board, for the build for both; one board's build has it as a constant.
    board: u32,
    zeros: Aligned<480>,
}

impl Lcd {
    pub const fn new() -> Lcd {
        Lcd { board: 0, zeros: Aligned([0; 480]) }
    }

    fn fm1(&self) -> bool {
        board::known(self.board) == FM1
    }

    fn pins(&self) -> &'static Pins {
        if self.fm1() { &PIN_FM1 } else { &PIN_SMK }
    }

    fn cs(&self, high: bool) {
        let p = self.pins().cs;
        gp_set(p.0, p.1, high);
    }

    fn dc(&self, high: bool) {
        let p = self.pins().dc;
        gp_set(p.0, p.1, high);
    }

    fn wait_done() {
        let mut t = 2_000_000;
        sync_cpu();
        while S1CON.read() & SPI_DONE == 0 {
            t -= 1;
            if t == 0 {
                break;
            }
        }
        S1CON.write(S1CON.read() & !SPI_DONE | SPI_CLR);
    }

    fn put(b: u8) {
        S1BUF.write(b as u32);
        Self::wait_done();
    }

    /// A command with its parameters, in one chip-select burst.
    fn cmd(&self, c: u8, d: &[u8]) {
        self.cs(false);
        self.dc(false);
        Self::put(c);
        self.dc(true);
        for &b in d {
            Self::put(b);
        }
        self.cs(true);
    }

    fn send(p: &[u8]) {
        if p.is_empty() {
            // a zero count would never complete
            return;
        }
        S1ADR.write(dma(p));
        S1CNT.write(p.len() as u32);
        Self::wait_done();
    }

    fn clear(&mut self) {
        self.zeros.0 = [0; 480];
        self.window(0, 0, 240, 240);
        for _ in 0..240 {
            Self::send(&self.zeros.0);
        }
        self.cs(true);
    }
}

impl Panel for Lcd {
    fn init(&mut self, board: u32) -> bool {
        if crate::APP_BOARD == 0 {
            if board != FM1 && board != SMK {
                return false;
            }
            self.board = board;
        }
        let fm1 = self.fm1();
        if fm1 {
            IO_MUX1.set(0x10);
        } else {
            IO_MUX1.clear(0x10);
        }
        // Chip select high; command/data, clock and data low; all outputs.
        let p = self.pins();
        gp_set(p.cs.0, p.cs.1, true);
        gp_set(p.dc.0, p.dc.1, false);
        gp_set(p.clk.0, p.clk.1, false);
        gp_set(p.mosi.0, p.mosi.1, false);
        gp_dir_out(p.cs.0, p.cs.1);
        gp_dir_out(p.dc.0, p.dc.1);
        gp_dir_out(p.clk.0, p.clk.1);
        gp_dir_out(p.mosi.0, p.mosi.1);
        if fm1 {
            // the backlight, active low: off until the panel is cleared
            gp_set(PA, 2, true);
            gp_dir_out(PA, 2);
        }
        S1CON.write(0x4021);
        S1BAUD.write(4);
        if fm1 {
            self.cmd(0x01, &[]);
            delay_us(150_000);
            self.cmd(0x11, &[]);
            delay_us(120_000);
            self.cmd(0x3a, &[0x55]);
            self.cmd(0x36, &[0x00]);
            self.cmd(0x21, &[]);
            self.cmd(0x13, &[]);
        } else {
            delay_us(100_000);
            self.cmd(0x11, &[]);
            delay_us(120_000);
            self.cmd(0x36, &[0x00]);
            self.cmd(0x3a, &[0x05]);
            self.cmd(0xb2, &[0x0c, 0x0c, 0x00, 0x33, 0x33]);
            self.cmd(0xb7, &[0x35]);
            self.cmd(0xbb, &[0x32]);
            self.cmd(0xc2, &[0x01]);
            self.cmd(0xc3, &[0x15]);
            self.cmd(0xc4, &[0x20]);
            self.cmd(0xc6, &[0x0f]);
            self.cmd(0xd0, &[0xa4, 0xa1]);
            self.cmd(0xe0, &GAMMA_SMK);
            self.cmd(0xe1, &GAMMA_SMK);
            self.cmd(0x21, &[]);
        }
        self.clear();
        self.cmd(0x29, &[]);
        if fm1 {
            gp_set(PA, 2, false);
        }
        true
    }

    /// Starts a pixel burst: the window, memory write, and chip select stays low.
    fn window(&mut self, x: u32, y: u32, w: u32, h: u32) {
        let span = |from: u32, size: u32| {
            let to = from + size - 1;
            [(from >> 8) as u8, from as u8, (to >> 8) as u8, to as u8]
        };
        self.cmd(0x2a, &span(x, w));
        self.cmd(0x2b, &span(y, h));
        self.cs(false);
        self.dc(false);
        Self::put(0x2c);
        self.dc(true);
    }

    fn pixels(&mut self, p: &[u8]) {
        Self::send(p)
    }

    fn end(&mut self) {
        self.cs(true)
    }
}
