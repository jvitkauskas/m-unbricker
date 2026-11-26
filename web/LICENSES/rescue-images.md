# Rescue images: FM-1 and SMK-37

What M-Unbricker writes to the broken unit (`src/writer.rs`): a fresh bootloader and nothing else.
Everything else it may write is erased, so m-bootloader finds no firmware at the next start and opens its
recovery menu in **MIDI mode** by itself (m-bootloader SPEC.md §11.6, start step 12). The unit then shows
up as `FM-1 Update` / `SMK-37 Update` and takes a firmware from a browser installer over USB MIDI.
`tools/make_images.py` packs the boot areas into the app (`build/rescue_img.rs`); the app picks the one
for the unit it identified. (An `app.bin` next to a `boot.bin` would be written at `0x4000` too; there is none.)

## FM-1 (`fm1/`)

| Range | Contents |
|---|---|
| `0x00000`-`0x03FFF` | `fm1/boot.bin`: [m-bootloader](https://github.com/jvitkauskas/m-bootloader) 3.0-cd3eecf for the FM-1, installed the way [M-Flasher](https://github.com/jvitkauskas/m-flasher) installs it |
| `0x04000`-`0xFEFFF` | erased: the old firmware and whatever it left (presets, settings, data) |
| `0xFF000`-`0xFFFFF` | never written (key_mac: factory Bluetooth address, where present) |

## SMK-37 Pro / Elite / MKE-P37 (`smk37/`)

The same hardware under three names: one image for all, the Pro's.

| Range | Contents |
|---|---|
| `0x00000`-`0x03FFF` | `smk37/boot.bin`: m-bootloader 3.0-cd3eecf for the SMK-37, on the header, file table and chip-key record of the official SMK-37 Pro 016 package |
| `0x04000`-`0xBFFFF`, `0xC2000`-`0xFEFFF` | erased |
| `0xC0000`-`0xC1FFF` | never written: BTIF (Bluetooth address, pairings) and USRTRIM (the factory key, pad and wheel calibration) |
| `0xFF000`-`0xFFFFF` | never written: key_mac, and m-bootloader's copy of the calibration (TRIMCOPY) |

| File | Bytes | CRC32 | SHA-256 |
|---|---|---|---|
| `fm1/boot.bin` | 16384 | `D62C61B6` | `82a0a2bee64702b4483e748e004ee9c44550ae9530d92f047385a73b0c64bd13` |
| `smk37/boot.bin` | 16384 | `402B4F83` | `997ed13841e35aa653405de262b73bf0e2dc9c6092e09b45c9306c9c7266b93e` |

Each boot area is the 2.3 one M-Flasher left on a real unit (an FM-1, an SMK-37 Pro; from
[cardputer-synth-rescue](https://github.com/jvitkauskas/cardputer-synth-rescue)'s `rescue/`), with its bootloader
replaced by the `fm1-uboot.boot` / `smk37-uboot.boot` of the
[m-bootloader v3.0 release](https://github.com/jvitkauskas/m-bootloader/releases/tag/v3.0) (checked against its
SHA256SUMS) through M-Flasher 0.12.1's own `build_short_boot_area()` (`src/formats.rs`), which also passes
`check_boot_area()`. The same step with the v2.3 release's files gives back the 2.3 boot areas byte for byte.

## What installs a firmware afterwards

m-bootloader's MIDI mode was tested with SLOOP's, Felucca's, Melodee's and ChoralRoot's installers, and on
the SMK-37 Pro with ChoralRoot's and Felucca's SMK-37 installers. After an M-Unbricker repair, an FM-1
took stock V15 and V16 from M-VAVE's M-UPGRADE and SLOOP from its installer, from MIDI mode. Baud
Girl's installer refuses MIDI mode: it needs a firmware running on the unit. An SMK-37 Elite installed from MIDI mode reports
itself as a Pro.

## Licences

- m-bootloader: MIT (`LICENSES/MIT-m-bootloader.txt`).
- The boot area's 160-byte flash header and file table and the 34-byte chip-key record come from M-VAVE's
  FM-1 015 / SMK-37 Pro 016 packages, as every installer that puts m-bootloader on a unit keeps them.
