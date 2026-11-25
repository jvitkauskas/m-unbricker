// SPDX-License-Identifier: MIT
//! Guided pages on the 240x240 panel: a title, a 4-colour picture, a few lines of text, a status line or a progress
//! bar, and the key hint. Drawn in strips of BAND pixel rows, with no framebuffer. Text is UTF-8.
//!
//!   0..27    title (fonts::TITLE), centred, in the page's accent colour
//!   28..147  picture, 120x120, centred
//!   150..206 body (fonts::BODY), up to 3 centred lines
//!   207..221 status line (fonts::SMALL) or progress bar
//!   223..239 key hint (fonts::SMALL), yellow
use crate::fonts;
use mboot::Aligned;

pub struct Glyph {
    pub cp: u16,
    pub off: u16,
    pub w: u8,
    pub h: u8,
    pub x0: i8,
    pub y0: i8,
    pub adv: u8,
}

pub struct Font {
    pub g: &'static [Glyph],
    /// 2 bits a pixel, four to a byte, row after row.
    pub bits: &'static [u8],
    pub height: u8,
}

/// A picture: 2-bit colour indices, run-length coded: each byte is (run - 1) << 2 | index, runs of 1..64 pixels, row
/// after row.
pub struct Pic {
    pub w: u16,
    pub h: u16,
    pub pal: [u16; 4],
    pub rle: &'static [u8],
}

pub const C_BG: u16 = 0x0000;
pub const C_TEXT: u16 = 0xffff;
/// grey
pub const C_DIM: u16 = 0x9cf3;
/// yellow
pub const C_ACCENT: u16 = 0xfea0;
/// green
pub const C_GOOD: u16 = 0x5feb;
/// red
pub const C_BAD: u16 = 0xfa8a;
/// blue
pub const C_INFO: u16 = 0x5d9f;

pub struct Page {
    pub pic: Option<&'static Pic>,
    /// The title's colour.
    pub accent: u16,
    pub title: &'static [u8],
    /// Wrapped: at most 3 lines.
    pub body: &'static [u8],
    /// The bottom line, such as "PLAY: next".
    pub hint: Option<&'static [u8]>,
}

/// In a hint, where this unit's key names go (the build for both boards): PLAY or SEQ PLAY, and OCT− or <.
pub const KEY_GO: &str = "\x01";
pub const KEY_NO: &str = "\x02";

/// What the pages are drawn on: the panel (src/lcd.rs), or a picture of it on a computer (tools/preview.rs).
pub trait Panel {
    fn init(&mut self, board: u32) -> bool;
    /// Then pixels, then `end`.
    fn window(&mut self, x: u32, y: u32, w: u32, h: u32);
    /// RGB565, big endian.
    fn pixels(&mut self, p: &[u8]);
    fn end(&mut self);
}

const W: i32 = 240;
const BAND: i32 = 8;
const PIC_Y: i32 = 28;
const BODY_Y: i32 = 148;
const STATUS_Y: i32 = 207;
const STATUS_H: i32 = 15;
const HINT_Y: i32 = 223;
const MAX_LINES: usize = 8;
const TEXT_W: u32 = 228;
const STATUS_LEN: usize = 40;
/// (And the most a line copied for drawing holds: the status line's is shorter.)
const HINT_LEN: usize = 48;

/// Where a line's text is.
#[derive(Clone, Copy, PartialEq, Eq)]
enum Source {
    Title,
    Body,
    Hint,
    Status,
}

/// Which font: an index, so that a state of zeros is a valid one (and in .bss).
#[derive(Clone, Copy, PartialEq, Eq)]
enum FontId {
    Title,
    Body,
    Small,
}

fn font(id: FontId) -> &'static Font {
    match id {
        FontId::Title => &fonts::TITLE,
        FontId::Body => &fonts::BODY,
        FontId::Small => &fonts::SMALL,
    }
}

#[derive(Clone, Copy)]
struct TLine {
    font: FontId,
    source: Source,
    start: u16,
    len: u16,
    x: i16,
    y: i16,
    col: [u16; 4],
}

/// A byte of a string, or 0 past its end, as C's strings end.
fn at(s: &[u8], i: usize) -> u8 {
    s.get(i).copied().unwrap_or(0)
}

/// The code point at `*i`, moving `*i` past it.
fn next_cp(s: &[u8], i: &mut usize) -> u32 {
    let mut c = at(s, *i) as u32;
    let (a, b) = (at(s, *i + 1) as u32, at(s, *i + 2) as u32);
    *i += 1;
    if c >= 0xe0 && a != 0 && b != 0 {
        c = (c & 0x0f) << 12 | (a & 0x3f) << 6 | b & 0x3f;
        *i += 2;
    } else if c >= 0xc0 && a != 0 {
        c = (c & 0x1f) << 6 | a & 0x3f;
        *i += 1;
    }
    c
}

fn find(f: &'static Font, cp: u32) -> &'static Glyph {
    // (A missing glyph is a space.)
    f.g.iter().find(|g| g.cp as u32 == cp).unwrap_or(&f.g[0])
}

/// The width of up to `len` bytes of `s`.
fn text_width(f: &'static Font, s: &[u8], len: usize) -> u32 {
    let mut i = 0;
    let mut w = 0;
    while i < len && at(s, i) != 0 {
        w += find(f, next_cp(s, &mut i)).adv as u32;
    }
    w
}

fn shades(fg: u16) -> [u16; 4] {
    let (r, g, b) = (fg as u32 >> 11, fg as u32 >> 5 & 63, fg as u32 & 31);
    core::array::from_fn(|i| {
        let i = i as u32;
        (r * i / 3) << 11 | (g * i / 3) << 5 | b * i / 3
    })
    .map(|c| c as u16)
}

/// The renderer's state, with the band it draws each strip in.
pub struct Ui<P: Panel> {
    pub panel: P,
    band: Aligned<{ (W * BAND * 2) as usize }>,
    lines: [TLine; MAX_LINES],
    nlines: usize,
    page: Option<&'static Page>,
    /// The picture drawn: none for a redraw below it.
    pic: Option<&'static Pic>,
    pic_p: usize,
    pic_run: u32,
    pic_ci: u32,
    /// The progress in 1/1000, or -1.
    bar: i32,
    status_buf: [u8; STATUS_LEN],
    status_col: u16,
    /// The index of the status text's line.
    status_line: usize,
    smk: bool,
    hint_buf: [u8; HINT_LEN],
    hint_len: usize,
}

impl<P: Panel> Ui<P> {
    pub const fn new(panel: P) -> Ui<P> {
        Ui {
            panel,
            band: Aligned([0; (W * BAND * 2) as usize]),
            lines: [TLine { font: FontId::Title, source: Source::Title, start: 0, len: 0, x: 0, y: 0, col: [0; 4] };
                MAX_LINES],
            nlines: 0,
            page: None,
            pic: None,
            pic_p: 0,
            pic_run: 0,
            pic_ci: 0,
            bar: 0,
            status_buf: [0; STATUS_LEN],
            status_col: 0,
            status_line: 0,
            smk: false,
            hint_buf: [0; HINT_LEN],
            hint_len: 0,
        }
    }

    // ---- text ----

    fn text(&self, source: Source) -> &[u8] {
        match source {
            Source::Title => self.page.map_or(b"", |p| p.title),
            Source::Body => self.page.map_or(b"", |p| p.body),
            Source::Hint => &self.hint_buf[..self.hint_len],
            Source::Status => &self.status_buf,
        }
    }

    fn add_line(&mut self, id: FontId, source: Source, start: usize, len: usize, y: i32, fg: u16) {
        if self.nlines >= MAX_LINES {
            return;
        }
        let s = &self.text(source)[start..];
        let x = (W - text_width(font(id), s, len) as i32) / 2;
        let len = len.min(s.len()) as u16;
        self.lines[self.nlines] =
            TLine { font: id, source, start: start as u16, len, x: x as i16, y: y as i16, col: shades(fg) };
        self.nlines += 1;
    }

    /// A greedy word wrap into at most `max_lines` centred lines from `y`.
    fn add_wrapped(&mut self, id: FontId, source: Source, mut y: i32, max_lines: u32, fg: u16) {
        let s = self.text(source);
        let font = font(id);
        let mut i = 0;
        let mut n = 0;
        // (The lines are added as they're found; at most MAX_LINES lines are, so this holds them.)
        let mut found = [(0usize, 0usize); MAX_LINES];
        while at(s, i) != 0 && n < max_lines {
            let start = i;
            let mut brk = None;
            let mut p = i;
            while at(s, p) != 0 && at(s, p) != b'\n' {
                let mut q = p;
                next_cp(s, &mut q);
                if text_width(font, &s[start..], q - start) > TEXT_W {
                    break;
                }
                if at(s, p) == b' ' {
                    brk = Some(p);
                }
                p = q;
            }
            if at(s, p) != 0 && at(s, p) != b'\n' {
                if let Some(b) = brk {
                    p = b;
                }
            }
            if (n as usize) < MAX_LINES {
                found[n as usize] = (start, p - start);
            }
            n += 1;
            i = p;
            while at(s, i) == b' ' || at(s, i) == b'\n' {
                i += 1;
            }
        }
        for &(start, len) in &found[..(n as usize).min(MAX_LINES)] {
            self.add_line(id, source, start, len, y, fg);
            y += font.height as i32;
        }
    }

    /// A line's text: the page's own, or a copy of the hint's or the status's, as the band is drawn into meanwhile.
    fn line_text<'a>(&self, source: Source, copy: &'a mut [u8; HINT_LEN]) -> &'a [u8] {
        let own: &[u8] = match (source, self.page) {
            (Source::Title, Some(p)) => return p.title,
            (Source::Body, Some(p)) => return p.body,
            (Source::Hint, _) => &self.hint_buf[..self.hint_len],
            (Source::Status, _) => &self.status_buf,
            _ => &[],
        };
        copy[..own.len()].copy_from_slice(own);
        &copy[..own.len()]
    }

    fn draw_text(&mut self, t: &TLine, by: i32) {
        let f = font(t.font);
        if by + BAND <= t.y as i32 || by >= t.y as i32 + f.height as i32 {
            return;
        }
        let mut copy = [0; HINT_LEN];
        let text = self.line_text(t.source, &mut copy);
        let (mut i, e) = (t.start as usize, t.start as usize + t.len as usize);
        let mut x = t.x as i32;
        while i < e && at(text, i) != 0 {
            let g = find(f, next_cp(text, &mut i));
            let (gx0, gy0) = (x + g.x0 as i32, t.y as i32 + g.y0 as i32);
            for row in 0..g.h as i32 {
                let yy = gy0 + row - by;
                if !(0..BAND).contains(&yy) {
                    continue;
                }
                for col in 0..g.w as i32 {
                    let k = (row * g.w as i32 + col) as u32;
                    let lv = f.bits[g.off as usize + k as usize / 4] >> (2 * (k % 4)) & 3;
                    let xx = gx0 + col;
                    if lv != 0 && (0..W).contains(&xx) {
                        let px = ((yy * W + xx) * 2) as usize;
                        self.band.0[px..][..2].copy_from_slice(&t.col[lv as usize].to_be_bytes());
                    }
                }
            }
            x += g.adv as i32;
        }
    }

    // ---- the picture ----

    fn pic_start(&mut self) {
        self.pic_p = 0;
        self.pic_run = 0;
    }

    fn draw_pic_row(&mut self, pic: &'static Pic, yy: i32) {
        let x0 = (W - pic.w as i32) / 2;
        let row = &mut self.band.0[((yy * W + x0) * 2) as usize..][..pic.w as usize * 2];
        for px in row.as_chunks_mut::<2>().0 {
            if self.pic_run == 0 {
                let b = pic.rle[self.pic_p];
                self.pic_p += 1;
                self.pic_ci = b as u32 & 3;
                self.pic_run = (b >> 2) as u32 + 1;
            }
            self.pic_run -= 1;
            *px = pic.pal[self.pic_ci as usize].to_be_bytes();
        }
    }

    // ---- the strips ----

    fn draw_bar(&mut self, by: i32) {
        let (x0, x1, y0, y1) = (30, 210, STATUS_Y + 4, STATUS_Y + 12);
        let fill = x0 + 2 + (x1 - x0 - 4) * self.bar / 1000;
        for y in by..by + BAND {
            if y < y0 || y >= y1 {
                continue;
            }
            for x in x0..x1 {
                let edge = y == y0 || y == y1 - 1 || x == x0 || x == x1 - 1;
                let c = if edge {
                    C_DIM
                } else if x < fill {
                    C_GOOD
                } else {
                    C_BG
                };
                let px = (((y - by) * W + x) * 2) as usize;
                self.band.0[px..][..2].copy_from_slice(&c.to_be_bytes());
            }
        }
    }

    fn draw_rows(&mut self, y0: i32, y1: i32, first_status_line: usize) {
        self.panel.window(0, y0 as u32, W as u32, (y1 - y0) as u32);
        let mut by = y0;
        while by < y1 {
            self.band.0.fill(0);
            if let Some(pic) = self.pic {
                for yy in 0..BAND {
                    if by + yy >= PIC_Y && by + yy < PIC_Y + pic.h as i32 {
                        self.draw_pic_row(pic, yy);
                    }
                }
            }
            for i in 0..self.nlines {
                if i < first_status_line || self.bar < 0 {
                    let t = self.lines[i];
                    self.draw_text(&t, by);
                }
            }
            if self.bar >= 0 {
                self.draw_bar(by);
            }
            let n = ((y1 - by).min(BAND) * W * 2) as usize;
            self.panel.pixels(&self.band.0[..n]);
            by += BAND;
        }
        self.panel.end();
    }

    /// A hint with KEY_GO / KEY_NO in it, with this unit's key names, into hint_buf.
    fn key_names(&mut self, s: &[u8]) {
        let (go, no): (&[u8], &[u8]) = if self.smk { (b"SEQ PLAY", b"<") } else { (b"PLAY", "OCT−".as_bytes()) };
        let mut d = 0;
        for &c in s {
            let k: &[u8] = if c == KEY_GO.as_bytes()[0] {
                go
            } else if c == KEY_NO.as_bytes()[0] {
                no
            } else {
                core::slice::from_ref(&c)
            };
            for &b in k {
                if d < HINT_LEN - 1 {
                    self.hint_buf[d] = b;
                    d += 1;
                }
            }
        }
        self.hint_len = d;
    }

    fn layout(&mut self, p: &'static Page) {
        self.nlines = 0;
        self.add_line(FontId::Title, Source::Title, 0, 0xffff, 3, p.accent);
        self.add_wrapped(FontId::Body, Source::Body, BODY_Y, 3, C_TEXT);
        if let Some(hint) = p.hint {
            self.key_names(hint);
            self.add_line(FontId::Small, Source::Hint, 0, 0xffff, HINT_Y, C_ACCENT);
        }
        self.status_line = self.nlines;
        if self.status_buf[0] != 0 && self.bar < 0 {
            self.add_line(FontId::Small, Source::Status, 0, 0xffff, STATUS_Y, self.status_col);
        }
    }

    pub fn init(&mut self, board: u32) {
        self.bar = -1;
        self.status_line = MAX_LINES;
        self.smk = board == crate::board::SMK;
        self.panel.init(board);
    }

    pub fn show(&mut self, p: &'static Page) {
        self.page = Some(p);
        self.pic = p.pic;
        self.status_buf[0] = 0;
        self.bar = -1;
        self.layout(p);
        self.pic_start();
        self.draw_rows(0, 240, MAX_LINES);
    }

    /// The line above the hint: a short status text. Redraws only that line.
    pub fn status(&mut self, text: &[u8], color: u16) {
        let n = text.iter().take(STATUS_LEN - 1).take_while(|&&c| c != 0).count();
        self.status_buf[..n].copy_from_slice(&text[..n]);
        self.status_buf[n] = 0;
        self.status_col = color;
        self.bar = -1;
        let Some(page) = self.page else {
            return;
        };
        self.layout(page);
        // the picture ends above the status line
        self.pic = None;
        self.draw_rows(STATUS_Y, STATUS_Y + STATUS_H + 1, MAX_LINES);
        self.pic = page.pic;
    }

    /// The line above the hint as a progress bar, `done` of `total`. Redraws only that line.
    pub fn progress(&mut self, done: u32, total: u32) {
        let v = if total != 0 { (done.wrapping_mul(1000) / total) as i32 } else { 0 };
        if v == self.bar {
            return;
        }
        self.bar = v.min(1000);
        let Some(page) = self.page else {
            return;
        };
        self.pic = None;
        self.draw_rows(STATUS_Y, STATUS_Y + STATUS_H + 1, self.status_line);
        self.pic = page.pic;
    }
}

/// Text built for a status line: up to 48 bytes.
pub struct Line {
    buf: [u8; 48],
    len: usize,
}

impl Line {
    pub const fn new() -> Line {
        Line { buf: [0; 48], len: 0 }
    }

    pub fn as_bytes(&self) -> &[u8] {
        &self.buf[..self.len]
    }

    pub fn push(&mut self, s: &[u8]) -> &mut Line {
        for &c in s {
            if self.len < self.buf.len() {
                self.buf[self.len] = c;
                self.len += 1;
            }
        }
        self
    }

    /// `v` as exactly `digits` hex digits, in capitals.
    pub fn hex(&mut self, v: u32, digits: u32) -> &mut Line {
        for d in (0..digits).rev() {
            self.push(&[b"0123456789ABCDEF"[(v >> (4 * d) & 15) as usize]]);
        }
        self
    }

    pub fn num(&mut self, mut v: u32) -> &mut Line {
        let mut b = [0u8; 10];
        let mut n = 0;
        loop {
            b[n] = b'0' + (v % 10) as u8;
            n += 1;
            v /= 10;
            if v == 0 {
                break;
            }
        }
        while n > 0 {
            n -= 1;
            self.push(&[b[n]]);
        }
        self
    }
}
