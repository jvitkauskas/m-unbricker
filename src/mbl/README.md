# From m-bootloader

These files are from [m-bootloader](https://github.com/jvitkauskas/m-bootloader) (MIT, © 2026 Julius
Vitkauskas), from its first implementation (version 1.4, before the 2.x rewrite), unchanged:

| File | What M-Unbricker uses it for |
|---|---|
| `keys.c`, `boot.h`, `cfg.h`, `fs.h` | the menu keys of both boards (PLAY / OCT−, SEQ PLAY / <) |
| `hw.c`, `hw.h` | register access, the P33 bridge (watchdog), GPIO |
| `rt.c` | memcpy and friends for freestanding code |
| `mbl.h`, `disp.h` | common types, board numbers, colours |

`src/lcd.c` is m-bootloader's panel driver (`src/hw/disp.c`) with its 5x7 text replaced.
