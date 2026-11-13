<img src="art/logo/logo.png" alt="" width="120" align="right">

# M-Unbricker

Bring a broken M-VAVE **FM-1** or **SMK-37** back to life with a second, working one and a USB-C
cable. No dongle, no extra hardware, and nothing installed on the working unit.

**Start here: <https://jvitkauskas.github.io/m-unbricker/>**: a step-by-step page for Chrome or
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
an FM-1 bricked with firmware that hangs at start (stock bootloader, no USB at all).

The page's helper has started on an FM-1 running M-VAVE stock V15 and V16, Baud Girl FM-1+VA 102,
SLOOP 2.8.5, JIANT FM 10.7.17, Skiff 2.5.1, Felucca 1.8.2 beta and Melodee 1.6.1. On an SMK-37: M-VAVE stock V16,
Baud Girl SMK-37+VA 102, Erik37 1.2, Snoop37 2.7.2 and Melodee 1.4.5.

## Build

You need GNU Make, Python 3 and the [pi32v2 LLVM toolchain](https://github.com/jvitkauskas/pi32v2-llvm)
release `pi32v2-23.1.5-2`. On Linux x86-64:

```sh
curl -fLo toolchain.tar.xz https://github.com/jvitkauskas/pi32v2-llvm/releases/download/pi32v2-23.1.5-2/pi32v2-llvm-23.1.5-2-linux-x64.tar.xz
echo 'cb896562418a2be93edb9bac26af4bc81a106a32974002a78e6b9fc203c93990  toolchain.tar.xz' | sha256sum -c
mkdir toolchain && tar -xJf toolchain.tar.xz -C toolchain --strip-components=1
make
```

`make TOOLCHAIN=<directory>` uses a toolchain unpacked elsewhere. Outputs:
`build/m-unbricker-fm1.bin` and `build/m-unbricker-smk37.bin`, one per board it runs on.

Other targets: `make preview` renders every on-screen page with the app's own renderer
(`build/preview/`, needs gcc and Pillow); `make fonts` and `make art` regenerate `src/fonts.h`
(DejaVu Sans, Pillow) and `src/art.h` (from `art/device/`).

## Running it without the page

From a computer with [M-Flasher](https://github.com/jvitkauskas/m-flasher): put the working unit in
download mode (`m-flasher download-mode`), then

```sh
m-flasher run-ram build/m-unbricker-fm1.bin --addr 0x01C30000
```

## Source

| Path | What |
|---|---|
| `src/main.c`, `src/pages.h` | the flow and every page's picture and text |
| `src/usbio.c` | timebase, the USB pins as GPIO, USB_KEY, calibration pulses |
| `src/husb.c` | USB0 as a full-speed host, polled ([docs/husb-notes.md](docs/husb-notes.md)) |
| `src/bot.c`, `src/jl.c` | mass storage (bulk-only) and the JieLi / M-Flasher loader protocol |
| `src/writer.c` | the plan, erase, write and verify of the rescue image |
| `src/ui.c`, `src/lcd.c` | pages drawn in strips: 4-colour pictures, antialiased text |
| `web/` | the page; `tools/deploy_web.sh` copies it to GitHub Pages |
| `stage/` | the helper's screen: m-bootloader 2.3's RAM stage with M-Unbricker's logo; `tools/build_stage.sh` builds the packages in `web/assets/` from m-bootloader v2.3 |
| `art/` | the pictures ([art/README.md](art/README.md)) |
| `tools/` | image packing, fonts, pictures, the RAM app trailer, the page preview |

## Licence

MIT ([LICENSE](LICENSE)). Third-party parts: [THIRD-PARTY.md](THIRD-PARTY.md).
Not affiliated with M-VAVE or JieLi.

## Support

M-Unbricker is free, like M-Flasher and m-bootloader. A donation helps pay for code signing
certificates for the M-Flasher apps:
[Sponsor on GitHub](https://github.com/sponsors/jvitkauskas) ·
[Donate on Ko-fi](https://ko-fi.com/jvitkauskas)
