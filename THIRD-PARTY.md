# Third-party parts

M-Unbricker is MIT ([LICENSE](LICENSE)). It includes or builds in:

| Where | What | Licence |
|---|---|---|
| `m-bootloader/` (submodule) | [m-bootloader](https://github.com/jvitkauskas/m-bootloader)'s Rust crate: register access, pins, the P33 bridge, the clock boost, the flash driver, CRC16, the runtime | MIT |
| `src/keys.rs`, `src/lcd.rs` | the menu keys and the panel driver, ported from m-bootloader 1.4's C | MIT |
| `src/mflr-loader-a.bin` | M-Flasher's flash loader 1.3 (its `data/mflr-loader-a.bin`, m-bootloader's program B), uploaded to the broken unit | MIT |
| `rescue/` | the rescue images: m-bootloader 2.4's boot areas, see [rescue/README.md](rescue/README.md) | MIT |
| `src/fonts.rs` | glyphs rendered from DejaVu Sans | Bitstream Vera licence / public domain ([web/LICENSES/DejaVu-fonts.txt](web/LICENSES/DejaVu-fonts.txt)) |
| `web/lib/protocol.js`, `web/lib/formats.js` | from [M-Flasher](https://github.com/jvitkauskas/m-flasher)'s web page (`runApp` added) | MIT |
| `stage/stage.rs`, `web/assets/stage-*.fwsc` | the helper: m-bootloader 3.0's RAM stage (built from its v3.0 source, `tools/build_stage.sh`), its waiting screen changed to M-Unbricker's logo | MIT |
| `art/` | pictures made for this project | MIT |

The USB host driver (`src/husb.rs`, ported from M-Unbricker's own C) was written from register facts read in the JieLi AC79 SDK's
USB library (Apache-2.0), recorded in [docs/husb-notes.md](docs/husb-notes.md); no SDK code is
included. The USB_KEY and calibration timings follow [FM-1-transporter](https://github.com/kurogedelic/FM-1-transporter) and
[flipper-fm1-recovery](https://github.com/merthsoft/flipper-fm1-recovery), as measured by
[cardputer-synth-rescue](https://github.com/jvitkauskas/cardputer-synth-rescue).
