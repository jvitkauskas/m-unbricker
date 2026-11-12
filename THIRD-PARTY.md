# Third-party parts

M-Unbricker is MIT ([LICENSE](LICENSE)). It includes or builds in:

| Where | What | Licence |
|---|---|---|
| `src/mbl/`, `src/lcd.c` | keys, hardware access, runtime and the panel driver from [m-bootloader](https://github.com/jvitkauskas/m-bootloader) | MIT |
| `src/mflr_loader.h` | M-Flasher's flash loader 1.3 (m-bootloader's program B), uploaded to the broken unit | MIT |
| `rescue/` | the rescue images: m-bootloader 2.3 and [fm1-base](https://github.com/charlesvestal/fm1-base)'s template, with the parts listed in [rescue/README.md](rescue/README.md) | MIT, MPL-2.0, Apache-2.0, OFL-1.1: texts in [web/LICENSES](web/LICENSES) |
| `src/fonts.h` | glyphs rendered from DejaVu Sans | Bitstream Vera licence / public domain ([web/LICENSES/DejaVu-fonts.txt](web/LICENSES/DejaVu-fonts.txt)) |
| `web/lib/protocol.js`, `web/lib/formats.js` | from [M-Flasher](https://github.com/jvitkauskas/m-flasher)'s web page (`runApp` added) | MIT |
| `stage/stage.c`, `web/assets/stage-*.fwsc` | the helper: m-bootloader 2.3's RAM stage (built from its v2.3 source, `tools/build_stage.sh`), its waiting screen changed to M-Unbricker's logo | MIT |
| `art/` | pictures made for this project | MIT |

The USB host driver (`src/husb.c`) was written from register facts read in the JieLi AC79 SDK's
USB library (Apache-2.0), recorded in [docs/husb-notes.md](docs/husb-notes.md); no SDK code is
included. The USB_KEY and calibration timings follow [FM-1-transporter](https://github.com/kurogedelic/FM-1-transporter) and
[flipper-fm1-recovery](https://github.com/merthsoft/flipper-fm1-recovery), as measured by
[cardputer-synth-rescue](https://github.com/jvitkauskas/cardputer-synth-rescue).
