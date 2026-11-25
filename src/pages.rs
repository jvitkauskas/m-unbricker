// SPDX-License-Identifier: MIT
//! Every page's picture, title, text and key hint, in one place: the app (src/flow.rs) and the preview
//! (tools/preview.rs) both use this table. Titles must fit one line (about 18 characters), bodies three.
use crate::art;
use crate::ui::{C_ACCENT, C_BAD, C_GOOD, C_INFO, Page};

// This unit's key names: one board's, or for both boards the places ui.rs puts this unit's in (Ui::init).
#[cfg(board = "smk37")]
macro_rules! go {
    () => {
        "SEQ PLAY"
    };
}
#[cfg(board = "smk37")]
macro_rules! no {
    () => {
        "<"
    };
}
#[cfg(board = "fm1")]
macro_rules! go {
    () => {
        "PLAY"
    };
}
#[cfg(board = "fm1")]
macro_rules! no {
    () => {
        "OCT−"
    };
}
#[cfg(board = "any")]
macro_rules! go {
    () => {
        "\x01"
    };
}
#[cfg(board = "any")]
macro_rules! no {
    () => {
        "\x02"
    };
}
// (ui.rs's KEY_GO and KEY_NO are the same as the build for both's.)
const _: () = assert!(crate::ui::KEY_GO.as_bytes()[0] == 1 && crate::ui::KEY_NO.as_bytes()[0] == 2);

const fn t(s: &'static str) -> &'static [u8] {
    s.as_bytes()
}

const HINT_NEXT: Option<&[u8]> = Some(t(concat!(go!(), ": next")));
const HINT_AGAIN: Option<&[u8]> = Some(t(concat!(go!(), ": try again")));
const HINT_RESTART: Option<&[u8]> = Some(t(concat!(go!(), ": restart me")));
const HINT_REPAIR: Option<&[u8]> = Some(t(concat!(go!(), ": repair  ·  ", no!(), ": leave it")));

macro_rules! page {
    ($name:ident, $pic:expr, $accent:expr, $title:expr, $body:expr, $hint:expr) => {
        pub static $name: Page = Page { pic: $pic, accent: $accent, title: t($title), body: t($body), hint: $hint };
    };
}

// the way in
page! { WELCOME, art::WELCOME, C_ACCENT, "M-Unbricker", "I'll bring a broken FM-1 or SMK-37 back to life.", HINT_NEXT }
page! { UNPLUG, art::UNPLUG_PC, C_ACCENT, "Unplug me", "Pull the cable out of the phone or computer. Leave it in me.", HINT_NEXT }
page! { CONNECT, art::CONNECT, C_ACCENT, "Connect it", "Plug the cable into the broken unit. Keep it switched off for now.", HINT_NEXT }
page! { SWITCH_ON, art::SWITCH_ON, C_ACCENT, "Switch it on", "Switch the broken unit on now. I'll wake it up.", None }
page! { ALREADY_ON, art::RETRY, C_ACCENT, "Switch it off first", "It's already running. Switch it off, then press the key.", HINT_AGAIN }
page! { NO_ANSWER, art::RETRY, C_ACCENT, "No answer", "Check the cable in both units. Switch it off, then press the key.", HINT_AGAIN }
page! { WAKING, art::WAKING, C_ACCENT, "Waking it up", "This takes a few seconds.", None }
page! { CONNECTING, art::WAKING, C_ACCENT, "Connecting", "Talking to it…", None }

// found, the repair
page! { FOUND_FM1, art::FOUND, C_INFO, "Found an FM-1", "Checking what needs repairing…", None }
page! { FOUND_SMK37, art::FOUND, C_INFO, "Found an SMK-37", "Checking what needs repairing…", None }
page! { READY_FM1, art::PRESS_PLAY, C_ACCENT, "Ready to repair", "A fresh bootloader. Its old firmware and settings are erased.", HINT_REPAIR }
page! { READY_SMK37, art::PRESS_PLAY, C_ACCENT, "Ready to repair", "A fresh bootloader. Old firmware is erased; calibration stays.", HINT_REPAIR }
page! { NOTHING, art::PRESS_PLAY, C_GOOD, "Nothing to repair", "It's already waiting for a firmware. Restart it?", HINT_REPAIR }
page! { REPAIRING, art::REPAIRING, C_ACCENT, "Repairing", "Keep both units switched on and connected.", None }
page! { DONE_FM1, art::SUCCESS, C_GOOD, "Done!", "It's in MIDI mode now, ready for a firmware from your phone or computer.", HINT_RESTART }
page! { DONE_SMK37, art::SUCCESS, C_GOOD, "Done!", "It's in MIDI mode now, ready for a firmware from your phone or computer.", HINT_RESTART }
page! { LEFT, art::LEAVE, C_INFO, "Left as it was", "Nothing was written. You can switch it off.", HINT_RESTART }

// trouble
page! { ERR_CONNECT, art::ERROR, C_BAD, "It didn't connect", "Switch it off, then press the key to start again.", HINT_AGAIN }
page! { ERR_OTHER, art::ERROR, C_BAD, "That's not it", "Something else answered. Use a plain USB-C cable, no hub.", HINT_AGAIN }
page! { ERR_UNKNOWN, art::ERROR, C_BAD, "Unknown unit", "Not an FM-1 or SMK-37 I know. Nothing was written.", HINT_RESTART }
page! { ERR_IMAGE, art::ERROR, C_BAD, "Damaged program", "My copy of the repair is damaged. Load m-unbricker again.", HINT_RESTART }
page! { ERR_READ, art::ERROR, C_BAD, "Reading stopped", "Switch it off, then press the key to start again.", HINT_AGAIN }
page! { ERR_WRITE, art::ERROR, C_BAD, "Repair stopped", "Run it again: switch it off, then press the key.", HINT_AGAIN }
page! { ERR_TIMER, art::ERROR, C_BAD, "Can't run here", "This unit's timer doesn't run. Restart it.", None }
