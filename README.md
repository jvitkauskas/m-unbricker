<img src="art/logo/logo.png" alt="" width="120" align="right">

# M-Unbricker

Bring a broken M-VAVE **FM-1** or **SMK-37** back to life with a second, working one and a USB-C
cable. No dongle, no extra hardware, and nothing installed on the working unit.

**Start here: <https://jvitkauskas.github.io/m-apps/unbricker/>**: a step-by-step page for Chrome or
Edge on a computer, Chrome on Android, or the MIDIWeb app on an iPhone or iPad.

![A working FM-1 repairing a broken unit over a USB-C cable](art/web/hero.png)

## How it works

1. The page talks to the working unit over USB MIDI. If it runs firmware, the page first starts
   m-bootloader's small RAM stage on it through the firmware's own update route (the firmware and
   its settings stay); a unit with m-bootloader can use its MIDI mode instead. Then it sends
   M-Unbricker, a RAM app (m-bootloader's
   [RAM app format](https://github.com/jvitkauskas/m-bootloader/blob/main/docs/SPEC.md)), which the
   unit loads into memory and runs.
2. M-Unbricker takes over the working unit's USB-C port and its screen, and guides the user.
3. With the cable moved to the broken unit and that unit switched on, it sends the JieLi
   **USB_KEY** (0x16EF on D+/D-) and the calibration pulses, which put the broken unit's mask ROM
   into its USB download mode, whatever is in its flash.
4. It then works as a **USB host**: it talks to that ROM over mass storage, starts M-Flasher's
   flash loader on it, identifies the model, and compares its flash with the rescue image.
5. After PLAY (SEQ PLAY on the SMK-37) it writes a fresh **m-bootloader**, erases everything else
   up to `0xFEFFF`, verifies it with the chip's CRC32 and restarts the unit. `0xFF000`+ (and on the
   SMK-37 its Bluetooth pairing and calibration at `0xC0000`-`0xC1FFF`) are never written. With no
   firmware, m-bootloader opens its MIDI mode by itself, and the unit takes a firmware from that
   firmware's own browser installer. See [rescue/README.md](rescue/README.md).
6. PLAY at the end restarts the working unit into its own firmware.

Either model can repair either model. Tested on an FM-1 and an SMK-37 Pro, both ways, including
an FM-1 bricked with firmware that hangs at start (stock bootloader, no USB at all). After a repair
(0.5), the FM-1 waited in MIDI mode and took stock V15 and V16 from M-VAVE's M-UPGRADE and SLOOP
from its installer.

The page's helper has started on an FM-1 running M-VAVE stock V15 and V16, Baud Girl FM-1+VA 102,
SLOOP 2.8.5, JIANT FM 10.7.17, Skiff 2.5.1, Felucca 1.8.2 beta and Melodee 1.6.1. On an SMK-37: M-VAVE stock V16,
Baud Girl SMK-37+VA 102, Erik37 1.2, Snoop37 2.7.2 and Melodee 1.4.5.

## Build

M-Unbricker is Rust, on [m-bootloader](https://github.com/jvitkauskas/m-bootloader)'s Rust crate (`mboot`) for the
chip's common drivers, which comes in as the `m-bootloader` submodule. You need:
- the [pi32v2 LLVM toolchain](https://github.com/jvitkauskas/pi32v2-llvm), release `pi32v2-23.1.6-2`;
- the [pi32v2 Rust compiler](https://github.com/jvitkauskas/pi32v2-rust), release `pi32v2-1.100.0-1`, built on that
  LLVM, as the linker makes the code from its bitcode;
- `cargo` and a recent `rustc` for the computer: the build makes Rust's `core` once with cargo, and the preview and
  the tests run on the computer;
- GNU Make and Python 3.

On Linux x86-64:

```sh
git clone --recurse-submodules https://github.com/jvitkauskas/m-unbricker
cd m-unbricker
curl -fLo toolchain.tar.xz https://github.com/jvitkauskas/pi32v2-llvm/releases/download/pi32v2-23.1.6-2/pi32v2-llvm-23.1.6-2-linux-x64.tar.xz
echo 'b9aa6878a3e9863d54f7dea96ec5dd2e4f438b02a1503f7b042fa601bb5e4c22  toolchain.tar.xz' | sha256sum -c
mkdir toolchain && tar -xJf toolchain.tar.xz -C toolchain --strip-components=1
curl -fLo pi32v2-rust.tar.xz https://github.com/jvitkauskas/pi32v2-rust/releases/download/pi32v2-1.100.0-1/pi32v2-rust-1.100.0-1-linux-x64.tar.xz
echo 'efc89e3cd13b38a22d6698b3d965c0a754bda31c0299afc9b4d73f0a73595690  pi32v2-rust.tar.xz' | sha256sum -c
mkdir pi32v2-rust && tar -xJf pi32v2-rust.tar.xz -C pi32v2-rust --strip-components=1
make
```

`make TOOLCHAIN=<directory> PI32V2_RUST=<directory>` uses a toolchain and a compiler unpacked elsewhere. Outputs:
`build/m-unbricker-fm1.bin` and `build/m-unbricker-smk37.bin`, one per board it runs on, and
`build/m-unbricker-any.bin`, one file for both (board 0 in its trailer). That one reads which board it's
on from the unit's own flash, as m-apps' universal apps do (`src/board.rs`): an SMK-37 by its
calibration (`trimdat` at `0xC1224`, or its copy `TRIMCOPY` at `0xFF000`), an FM-1 by its flash chip.
On anything else it restarts the unit without starting the screen or the keys and without writing
anything: only the flash's pins, which it read by, and USB have been set up by then. The page uses the per-board files; the card's
launcher (m-apps) uses the one for both.

- **How it's built:** `src/main.rs` is one crate, built for a board with `--cfg board="fm1"` (`"smk37"`, `"any"`).
  rustc writes LLVM bitcode, and the linker optimises the app as one program with m-bootloader's crate and `core`, at
  `opt-level=z`.
- **No `core::fmt`:** formatting would cost kilobytes; nothing here formats with it.

Other targets:
- `make preview` renders every on-screen page with the app's own renderer (`tools/preview.rs`, a program for the
  computer) into `build/preview/<board>/`, and contact sheets of them (those need Pillow).
- `make test` compares the Rust with M-Unbricker's last C, on the computer: the pages' pixels, the image packing, and
  on a simulated chip the panel's, the keys', the USB host's and the pins' register accesses, the board detection, and
  whole repairs with what the mass storage and the loader protocol send ([tests/README.md](tests/README.md)).
- `make fonts` and `make art` regenerate `src/fonts.rs` (DejaVu Sans, Pillow) and `src/art.rs` (from `art/device/`).

## Running it without the page

From a computer with [M-Flasher](https://github.com/jvitkauskas/m-flasher): put the working unit in
download mode (`m-flasher download-mode`), then

```sh
m-flasher run-ram build/m-unbricker-fm1.bin --addr 0x01C30000
```

## Source

| Path | What |
|---|---|
| `src/flow.rs`, `src/pages.rs` | the flow and every page's picture and text |
| `src/usbio.rs` | timebase, the USB pins as GPIO, USB_KEY, calibration pulses |
| `src/husb.rs` | USB0 as a full-speed host, polled ([docs/husb-notes.md](docs/husb-notes.md)) |
| `src/bot.rs`, `src/jl.rs` | mass storage (bulk-only) and the JieLi / M-Flasher loader protocol |
| `src/writer.rs` | the plan, erase, write and verify of the rescue image |
| `src/ui.rs`, `src/lcd.rs` | pages drawn in strips: 4-colour pictures, antialiased text |
| `src/board.rs`, `src/keys.rs` | which board, from the flash, for the build for both; both boards' keys |
| `m-bootloader/` | m-bootloader, a submodule: its Rust crate's registers, pins, P33 bridge, clock and flash drivers |
| `web/` | the page; `tools/deploy_web.sh` copies it to GitHub Pages |
| `stage/` | the helper: m-bootloader 3.0's RAM stage (`stage/stage.rs`) with M-Unbricker's logo; `tools/build_stage.sh` builds the packages in `web/assets/` from m-bootloader v3.0 |
| `art/` | the pictures ([art/README.md](art/README.md)) |
| `tools/` | image packing, fonts, pictures, the RAM app trailer, the page preview |
| `tests/` | the Rust against the C, on the computer ([tests/README.md](tests/README.md)) |

**Not on units yet:** this Rust build and the 3.0 helper in `web/assets/stage-*.fwsc` haven't been tried on a unit.
Don't deploy the page with them (`tools/deploy_web.sh`) until both boards have repaired each other with them, from
the page and from the card. The page deployed now has the 2.4 helper and the C builds of the app.

## Licence

MIT ([LICENSE](LICENSE)). Third-party parts: [THIRD-PARTY.md](THIRD-PARTY.md).
Not affiliated with M-VAVE or JieLi.

## Support

M-Unbricker is free, like M-Flasher and m-bootloader. A donation helps pay for code signing
certificates for the M-Flasher apps:
[Sponsor on GitHub](https://github.com/sponsors/jvitkauskas) ·
[Donate on Ko-fi](https://ko-fi.com/jvitkauskas)
