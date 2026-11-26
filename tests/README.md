# Tests: the Rust against the C

`make test` runs these on the computer. Each compares the Rust with M-Unbricker's last C (`bd5ea96`): what the C gave
is kept in `tests/c-reference/` and `tests/c-reference.txt`, and `make c-reference` makes it again from the C in this
repository's history, built with the computer's clang. Needs a recent `rustc`, `clang` and Python 3.

| Test | What it compares |
|---|---|
| `images_test.py` (`images.rs`, `c/images.c`) | The image packing: `tools/make_images.py` and `src/writer.rs` against the C's `make_images.py` and `writer.c`, sector by sector (each sector's CRC-32, which are protected, the image's own check), for `rescue/` and for made-up images with an app area, which exercise the stored blocks and the regenerated padding. |
| `preview_test.py` | The pages: every page `tools/preview.rs` draws, for each board's build and for the build for both with each board's key names, against the C preview's pixels. |
| `chip_test.py` (`drive.rs`, `c/drive.c`) | The rest, on a simulated chip ([chip/README.md](chip/README.md)): 47 scenarios, each on its board's build and on the build for both. A scenario's summary is compared line for line with the C's: the count and SHA-256 of every register access in order, the data moved by DMA and through the USB endpoints, the panel's framebuffer, what the other unit's flash holds at the end, and the marks. Four scenarios are faults the C handled wrongly (`OUTCOME_ONLY` in chip_test.py): only their outcome is checked. A scenario in neither fails the test. |

The scenarios on the chip:

- **panel:** the panel driver's start and every page, for each board;
- **keys:** both boards' key scans, with presses;
- **wake:** USB_KEY and the calibration pulses against a model of the other unit's ROM: it answers, it is already
  running, it never answers, a false ACK, a D+ answer;
- **sof:** measuring the timer against the PC's USB frames, with frames, without, and too fast or slow;
- **detect:** the board detection on seven flash chips and markers. The result is compared, not the accesses: the
  Rust reads the flash with m-bootloader's driver, by DMA, where the C read byte by byte;
- **session:** whole sessions against a JieLi ROM and M-Flasher's loader on the other unit: each board repairing each
  board, nothing to repair, leaving, an unknown chip, another device, no device, stalls, early status blocks, odd
  answers, a failed verify that is written again, a write that keeps failing, a read error, another loader running,
  and a noisy bus;
- **full:** the flow from the first page to a repaired unit.

`CHIP_TRACE=<file> build/test/chip/drive-fm1 <scenario>` writes a scenario's full trace, to diff with the C's
(`build/c-drive/drive-fm1`, from `tests/c/build.sh`).
