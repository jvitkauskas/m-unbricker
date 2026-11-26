# Simulated chip

`chip.c` is an AC7911B, as far as M-Unbricker needs, for host tests. A C program and its Rust port both run on it,
and their register accesses can be compared exactly. The C side is `tests/c/`, and its output is in
`tests/c-reference.txt`.

## How it works

- The register ranges 0x10000-0x5FFFF are mapped `PROT_NONE` at their real addresses, so every access faults.
- The SIGSEGV handler decodes the instruction. A plain 32-bit load or store (`8B`, `89`, `C7 /0`, optional REX) is
  modelled and the handler steps past it. Anything else is single-stepped on the real page: the old technique,
  reported on stderr. An instruction that reads and writes its operand, such as the `or [m32], imm` a Rust
  read-modify-write often compiles to, is a read and then a write, as the C's two instructions are. The decoded path is about 2.5 µs an access, against 9 µs single-stepped.
- Each access is logged as `R aaaaa vvvvvvvv` or `W aaaaa vvvvvvvv`. A write to a DMA address register logs `dma` in
  place of the value, because the buffer's address is the program's own. Data moved by DMA, or into or out of a USB
  endpoint buffer, logs `D <trigger register> <length> <FNV-1a 64>`.
- The summary's `trace <count> <sha256>` is the SHA-256 of these lines. With `CHIP_TRACE=<file>` the lines are also
  written to a file, together with the `-- mark` lines. `grep -v '^--' file | sha256sum` gives the same hash. The
  count is register accesses only.
- **Virtual time.** The timer's count (0x10404) reads `t0 + n * tpa`, where `n` is the number of the access. Both
  `t0` and `tpa` are set per scenario. Every model runs on this clock at 48 MHz (48 ticks = 1 µs), so a run is
  deterministic. Most scenarios use `tpa` 480: one access is 10 µs.
- **DMA addresses** are 32-bit. An address is resolved on the main thread's stack (its upper 32 bits, the range from
  `/proc/self/maps`) when it falls inside the stack. Otherwise it is used as it is: non-PIE statics or the brk heap,
  below 4 GB. A large heap allocation from mmap can't be given to the hardware.

## Models

| Model | Registers | What it does |
|---|---|---|
| Timer 0 | 0x10400 | The count is virtual time (above). Other registers are plain storage. |
| P33 bridge | 0x13E08/0C | Never busy. |
| GPIO | 0x50000 + 0x40·port | Plain storage, with every pin an input at reset. IN reads OUT for outputs and the pull-up for inputs. A sense pin reads low when a pressed key's column is selected. |
| Keys | PA4 data, PA3 clock, PA1 latch; sense PA6/PA8 (FM-1), PH8 (SMK-37) | Decodes the latched word: 16 bits MSB first on the FM-1, 32 bits LSB byte first on the SMK-37. A key is selected when all of its word's low column bits are low (keys.c's `W_*`). A key session starts when PA3 becomes an output (keys_on) and ends when PA3 is given back. Key presses are windows in ms from the start of a session. A session with none gets PLAY from +50 ms for 100 ms. |
| USB pads | 0x51000, 0x51004 | Line levels in strength order: the other unit driving, this unit driving (GPIO mode, output), the other unit's 1.5k pull-up, our pulls. Floating reads low. CON0 bits 17/16 are the levels. |
| Other unit's ROM | | Off until `on_ms`, and when off it clamps D- low. Once on, it latches D- on each D+ rising edge, 16 bits, with a gap over 150 µs starting a new packet. After `need` packets of exactly 0x16EF it holds D- low for `ack_us`, then pulls D+ up. The variants are a false ACK (D- low, no pull-up), "D+ up at once", and "running" (D+ up from the start). In calibration, D+ is high from the pull-up except while we drive it low. Falling edges count as pulses. `cal_ms` after the first pulse D+ drops, and `attach_ms` later the device attaches for USB. |
| SOF | CON0 bit 13 / 12 | Pending once each `sof` ticks (a frame) while a PC is connected. Writing bit 12 clears it until the next frame. |
| USB0 host | CON0, CON1 window, EP_CNT, EP0_ADR, EP1 TADR/RADR | The SIE answers only with CON0 bit 2 set. The window works as husb.c drives it. DEVCTL reports host mode, and a full-speed device once attached. POWER bit 3 falling is a bus reset: address 0, unconfigured. CSR0: SETUP 0x0A, ReqPkt 0x20 (up to 64 bytes, COUNT0, RxPktRdy), OUT 0x02, status 0x42 / 0x60. TXCSR1 0x01 sends EP_CNT(1) bytes. RXCSR1 0x20 requests a packet, giving RXCOUNT and RxPktRdy, or a NAK that leaves ReqPkt set. STALL is 0x04 / 0x20 / 0x40. With no device, or the wrong address, the error bit is set. |
| The device | | Descriptors for VID/PID 4C4A:8057 (configurable), mps0 64, an interface 8/6/0x50 with bulk 0x81 / 0x02 at 64 bytes. It handles SET_ADDRESS (applied at the status stage), SET_CONFIGURATION, CLEAR_FEATURE(HALT), mass storage reset, and STALLs anything else. Bulk-only transport echoes the tag and reports the residue. |
| ROM download mode | | INQUIRY ("JIELI", "UBOOT1.00"), WRITE_MEMORY (CRC16 of the encrypted block checked, decrypted with jl.c's cipher into RAM), JUMP. Anything else is refused. The default is a STALLed data phase with a CSW status of 1. Variants answer with an early CSW (13 bytes in place of the data), or with 16 bytes that don't echo the opcode. JUMP to 0x01C20000 starts the loader only if the RAM there hashes to MFLR_A (bd5ea96). JUMP to 0x01C28000 with the RESTART program restarts the device, and it leaves the bus after the CSW. |
| M-Flasher's loader | | IDENTIFY ("MFLR1.3", 0x01C20000, 96 MHz), READ_KEY (0x980F, encoded as jl.c decodes it), ONLINE_DEVICE (the flash ID, LE at +4), USB_BUFFER_SIZE (4096), UNIQUE_ID, READ_FLASH, CRC32 (zlib), ERASE_BLOCK / ERASE_SECTOR, WRITE_FLASH (CRC16 checked, programmed by AND). |
| Its flash | 1 MB | From the scenario. An old FM-1 has random 0-0x5FFFF. An old SMK-37 has random 0-0x7FFFF, calibration at 0xC0000-0xC1FFF with `trimdat`, and `TRIMCOPY` plus data at 0xFF000. The "repaired" cases have rescue/<board>/boot.bin (read from `CHIP_RESCUE`, default `build/c-ref/rescue`) at 0 and 0xFF up to 0xFF000, with the calibration and the top kept. The summary lists the erased and programmed sectors and each sector's state against the start (`=` same, `F` erased, `X` other). It also checks the expectation: repaired (boot.bin, 0xFF to 0xFF000 except the SMK-37's 0xC0000-0xC1FFF, the rest untouched) or unchanged. |
| SPI0 + own flash | 0x11C00, PD0 CS | Byte transfers through SPI_BUF (RX bit 12, done bit 15, cleared by writing bit 14), and DMA through SPI_ADR + SPI_CNT in either direction. Commands: 0xFF, 0x9F JEDEC, 0x03 read, 0x05 / 0x35 status (0), 0x4B UID. The JEDEC ID and the markers come from the scenario. |
| SPI1 + ST7789 | 0x11D00; FM-1 CS PC7 / DC PC8, SMK-37 CS PC8 / DC PB5 | Done is always set. The DC pin's level picks command or data. CASET, RASET and RAMWR go into a 240x240 RGB565 (big endian) framebuffer, which comes in through SPI_BUF or DMA. |

Fault injection, per scenario, on the device side: the Nth READ_FLASH STALLs, or is answered by an early CSW. The
Nth WRITE_FLASH, or every one, leaves a byte unprogrammed. READ_FLASH at an address always ends in a phase error.
"Noisy" adds a bulk OUT error or NAK timeout every 37th packet, a bulk IN NAK timeout every 41st, and one EP0
error.

## Summary (stdout of `chip_finish`)

```
scenario <name>
-- <mark> @<access count>         one per chip_mark / chip_mark_panel ("<text> fb=<16 hex>")
trace <accesses> <sha256>
panel fb <sha256> on <0|1> commands <n> pixels <n>        (if the panel was used)
keys sessions <n> latches <n> pressed-reads <n>          (if the keys were scanned)
usbkey packets <0x16EF> other <n> false-acks <n> state <s> [ack-at-us <t>] cal-pulses <n> [drop-at-us <t>]
sof clears <n>
own-flash bytes <n> dma <n>
usb setups <n> address <a> config <c> commands <n> mode <rom|loader|other-loader|restarted|crashed> loader <-|ok|BAD> bad-cbw <n>
jl reads <n> writes <n> bad-crc <n> faults <n>
flash sha256 / erased / programmed / state / expect
```

`CHIP_PANEL=<file>` writes the framebuffer at the end. `CHIP_PANEL_DIR=<dir>` writes one per `chip_mark_panel`.

## Scenarios and their steps

The driver calls `chip_start(name)`, then the steps, then `chip_finish()`. Marks (`mark`) are `chip_mark` with the
text shown. The steps and the mark formats are also in `tests/c/drive.c`, by the function that runs them. `board` is
1 for `-fm1` and 2 for `-smk37`.

| Scenarios | Steps |
|---|---|
| `panel-fm1`, `panel-smk37` | `tb_start(); ui_init(board); chip_mark_panel("ui_init")`. Then, for each page in tools/preview.c's order, `ui_show(&PG_x)`, the same `ui_status` / `ui_progress` call as preview.c, and `chip_mark_panel("PG_x")`. |
| `keys-fm1`, `keys-smk37` (t0 near the wrap) | `tb_start()`, then `wait_key(0,0)` with PLAY, marked `wait_key(0,0) 1`. Then `wait_key(1,0)` with NEXT, then the leave key, marked `wait_key(1,0) 0`. Then `wait_key(1,0)` with REC, then PLAY, marked `wait_key(1,0) 1`. |
| `wake-ack-fm1`, `wake-ack-smk37` (t0 near the wrap), `wake-dpanswer-fm1` | `tb_start(); ui_init(board); mark "ui_init"; wake_it(); mark "wake_it"`. The unit is switched on at 800 ms and ACKs after 3 packets, with `dp-answer` pulling D+ up instead. The calibration drops at 1200 ms. |
| `wake-dpstart-*` (unit running), `wake-none-*` (never on) | `tb_start(); ui_init(board); mark "ui_init"; ui_show(&PG_switch_on); key_t0 = tb_now(); key_shown = ~0u; r = key_send(&ks, 1000, key_tick)`, marked `key_send <ack\|dp-at-start\|dp-answer\|timeout> packets P false_acks F alive A`. Then `usb_take_pins(); ui_show(r == KEY_DP_AT_START ? &PG_already_on : &PG_no_answer); mark "page"`. |
| `wake-false-*` | The same, with a 5000 ms timeout: a false ACK after 2 packets, the real one after 5. Then `ui_show(&PG_waking); cal_pulses(&cr)`, marked `cal_pulses pulse_ms X drop_ms Y pulses Z`. |
| `sof-present`, `sof-absent`, `sof-fast` (50 MHz), `sof-reject` (60 MHz) | `tb_start(); r = tb_calibrate_sof(1000, &seen)`, marked `tb_calibrate_sof r seen N tps T measured M`. |
| `detect-fm1-856014`, `-fm1-b36014`, `-smk37-trimdat`, `-smk37-trimcopy`, `-unknown` (C84017), `-fm1chip-smkmark`, `-none` (FFFFFF) | `b = board_detect(&jedec)`, marked `board B jedec %06X`. No `tb_start`. |
| `session-*` | `tb_start(); ui_init(board); mark "ui_init"; r = session()`, then the marks `session r`, `plan write W erase E runs R` and `loader <ji.loader> key %04X flash %06X io N retries N`. The other unit is in download mode and attached from the start. |
| `full-fm1` | `tb_start(); usb_take_pins(); ui_init(board); mark "ui_init"`. Then `PG_welcome` with `ui_status("version 0.5")`, `wait_key(0,0)`, `PG_unplug`, `wait_key(0,0)`, `PG_connect`, `wait_key(0,0)`, and `mark "pages"`. Then `do { wake_it(); mark "wake_it"; r = session(); } while (!r)` and the session marks. |

Session cases (the rescuing unit's board is the suffix):

| Scenario | The other unit | Ends |
|---|---|---|
| `session-repair-fm1` | FM-1, old firmware. PLAY at +1.1 s, so one `jl_ping` | repaired, PG_done_fm1 |
| `session-repair-smk37`, `session-cross-fm1` | SMK-37 with calibration and 0xFF000 data | repaired, both kept, PG_done_smk37 |
| `session-cross-smk37` | FM-1, old firmware | repaired |
| `session-nothing-fm1` | FM-1 (B36014), already repaired | "Nothing to repair", PLAY, restarted, unchanged |
| `session-leave-fm1`, `session-leave-smk37` | old firmware, the leave key at the ready page | PG_left, unchanged |
| `session-unknown-fm1` | flash C84017 | PG_err_unknown, -1 |
| `session-otherid-fm1` | VID/PID 1234:5678 | PG_err_other, 0 |
| `session-noattach-fm1` | never attaches | PG_err_connect ("attach"), 0 |
| `session-stall-fm1` | READ_FLASH #5 STALLs | retried, repaired |
| `session-early-fm1` | the ROM refuses with an early CSW, and READ_FLASH #6 too | repaired |
| `session-odd-fm1` | the ROM answers unknown commands without the opcode | repaired |
| `session-crc-fm1` | WRITE_FLASH #1 drops a byte: VERIFY, then do_run again | repaired |
| `session-writefail-fm1` | every WRITE_FLASH drops a byte | PG_err_write, 0 |
| `session-readerr-fm1` | READ_FLASH at 0x20000 always ends in a phase error (reset recovery) | PG_err_read, 0 |
| `session-otherloader-fm1` | another loader: no IDENTIFY, ONLINE_DEVICE answers | PG_err_connect ("loader 02"), 0 |
| `session-noisy-fm1` | bus errors and NAK timeouts | repaired |

## Rebuilding the C reference

```sh
tests/c/build.sh --reference
```

This extracts `git archive bd5ea96 src rescue tools` into `build/c-ref/`, generates `rescue_img.h` and patches
husb.c's `ssync`. It builds `build/chip/libchip.a` and the drivers `build/c-drive/drive-{fm1,smk37,any}` with host
clang (`-DHOST_TEST -no-pie`), and tools/preview.c's raw pages into `build/preview-ref/`.
`tests/c/reference.sh` then runs every scenario and writes `tests/c-reference.txt`. It fails if:

- `drive-any` (BOARD_ID 0, with the keys built twice) prints anything different;
- the panel pages differ from preview.c's;
- a scenario doesn't end on its page in `tests/c/pages.txt`;
- a flash expectation fails.

A full trace for diffing: `CHIP_TRACE=/tmp/c.txt build/c-drive/drive-fm1 session-repair-fm1`.

## Linking a Rust driver

The Rust driver is `tests/drive.rs`, built by `tests/host.py --chip` and run by `tests/chip_test.py`. By hand, after
`make build/host/libmboot.rlib build/rescue_img.rs` (m-bootloader's crate for this computer, and the rescue images):

```sh
python3 tests/host.py tests/drive.rs build/test/chip/drive-fm1 --board fm1 --chip
CHIP_RESCUE=rescue build/test/chip/drive-fm1 session-repair-fm1
```

(host.py builds `tests/chip/chip.c` into `build/host/libchip.a` and links it, with the edition and the crate.)

`extern "C" { fn chip_start(s: *const c_char); fn chip_mark(s: *const c_char); fn chip_mark_panel(s: *const c_char); fn chip_finish(); }`

The code under test must:

- access registers as `read_volatile` / `write_volatile` of `u32` at their real addresses;
- give DMA `ptr as u32` of a static or of a buffer on the main thread's stack.

Rust's own SIGSEGV handler (the stack guard) is replaced by `chip_start`.
