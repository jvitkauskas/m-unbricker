# Rescue images: FM-1 and SMK-37

What M-Unbricker writes to the broken unit (`src/writer.c`): a fresh bootloader and nothing else.
Everything else it may write is erased, so m-bootloader finds no firmware at the next start and opens its
recovery menu in **MIDI mode** by itself (m-bootloader SPEC.md §11.6, start step 12). The unit then shows
up as `FM-1 Update` / `SMK-37 Update` and takes a firmware from a browser installer over USB MIDI.
`tools/make_images.py` packs the boot areas into the app (`build/rescue_img.h`); the app picks the one
for the unit it identified. (An `app.bin` next to a `boot.bin` would be written at `0x4000` too; there is none.)

## FM-1 (`fm1/`)

| Range | Contents |
|---|---|
| `0x00000`-`0x03FFF` | `fm1/boot.bin`: [m-bootloader](https://github.com/jvitkauskas/m-bootloader) 2.3-6bc8f45 for the FM-1 (as bundled in [M-Flasher](https://github.com/jvitkauskas/m-flasher) 0.11.0), installed the way M-Flasher installs it |
| `0x04000`-`0xFEFFF` | erased: the old firmware and whatever it left (presets, settings, data) |
| `0xFF000`-`0xFFFFF` | never written (key_mac: factory Bluetooth address, where present) |

## SMK-37 Pro / Elite / MKE-P37 (`smk37/`)

The same hardware under three names: one image for all, the Pro's.

| Range | Contents |
|---|---|
| `0x00000`-`0x03FFF` | `smk37/boot.bin`: m-bootloader 2.3-6bc8f45 for the SMK-37, on the header, file table and chip-key record of the official SMK-37 Pro 016 package |
| `0x04000`-`0xBFFFF`, `0xC2000`-`0xFEFFF` | erased |
| `0xC0000`-`0xC1FFF` | never written: BTIF (Bluetooth address, pairings) and USRTRIM (the factory key, pad and wheel calibration) |
| `0xFF000`-`0xFFFFF` | never written: key_mac, and m-bootloader's copy of the calibration (TRIMCOPY) |

| File | Bytes | CRC32 | SHA-256 |
|---|---|---|---|
| `fm1/boot.bin` | 16384 | `A29F263D` | `e8f8046985c620493f59054a50a3330547ad7103b08bf97f8e42a284d92a694e` |
| `smk37/boot.bin` | 16384 | `029D5A23` | `6a96db9899286868372e4853069439192e7cf766d09b6857574c52b72fefc7a5` |

Each boot area is byte-identical to the one M-Flasher left on a real unit (an FM-1, an SMK-37 Pro) when it
installed m-bootloader 2.3 there; they come from
[cardputer-synth-rescue](https://github.com/jvitkauskas/cardputer-synth-rescue)'s `rescue/`.

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
