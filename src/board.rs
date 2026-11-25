// SPDX-License-Identifier: MIT
//! Which board this is. One board's build knows; the build for both (m-unbricker-any.bin) reads it from the unit's own
//! flash, never by probing pins (m-bootloader SPEC.md 8.1), with the rule m-apps uses (its kit's `app::board()`): an
//! SMK-37 keeps its calibration ("trimdat" at 0xC1224, or its copy "TRIMCOPY" at 0xFF000); otherwise an FM-1's flash
//! chip (0x856014, 0xB36014) says FM-1; anything else is unknown. The flash is m-bootloader's driver, read a single
//! line wide. Nothing runs from flash here, so XIP being off is fine.
use mboot::Global;
use mboot::platform::{flash_init, flash_jedec, read};

pub const FM1: u32 = 1;
pub const SMK: u32 = 2;

/// The board found at the start, for the build for both.
static FOUND: Global<u32> = Global::new(0);

/// This board: FM1 or SMK (0 before the build for both has found it).
#[inline(always)]
pub fn get() -> u32 {
    known(FOUND.read())
}

/// For the tests on this computer: the board the build for both would have found.
#[cfg(not(target_arch = "pi32v2"))]
pub fn assume(board: u32) {
    FOUND.set(board);
}

/// This board, from what was found at run time when the build doesn't say.
#[inline(always)]
pub fn known(found: u32) -> u32 {
    if crate::APP_BOARD != 0 { crate::APP_BOARD } else { found }
}

/// What the flash says, by the rule above: FM1, SMK, or 0. `trimdat` and `trimcopy` are the 8 bytes at 0xC1224 and
/// 0xFF000, or None if reading them failed, which makes it unknown, as the C's reads did. The copy isn't read when the
/// calibration is there.
pub fn decide(jedec: u32, trimdat: Option<&[u8; 8]>, trimcopy: Option<&[u8; 8]>) -> u32 {
    match (trimdat, trimcopy) {
        (Some(b"trimdat\0"), _) => SMK,
        (Some(_), Some(b"TRIMCOPY")) => SMK,
        (Some(_), Some(_)) if jedec == 0x85_6014 || jedec == 0xb3_6014 => FM1,
        _ => 0,
    }
}

/// This unit's board from its own flash: FM1, SMK, or 0 if it isn't known. `jedec` gets the flash chip's ID.
pub fn detect(jedec: &mut u32) -> u32 {
    let (mut a, mut b) = ([0u8; 8], [0u8; 8]);
    let ok = flash_init(1, 1);
    // (After a failure the chip reads as 0xFF, as the C's did.)
    *jedec = flash_jedec() & 0xff_ffff;
    if !ok {
        return 0;
    }
    let trimdat = read(0xc1224, &mut a).then_some(&a);
    let trimcopy = match trimdat {
        Some(t) if t != b"trimdat\0" => read(0xff000, &mut b).then_some(&b),
        _ => None,
    };
    let board = decide(*jedec, trimdat, trimcopy);
    FOUND.set(board);
    board
}
