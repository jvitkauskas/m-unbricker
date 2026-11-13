# What this page carries, and under which licences

The page and M-Unbricker itself (`assets/m-unbricker-*.bin`, `app.js`, `lib/`, the pictures in `img/`):
MIT, see [../LICENSE](../LICENSE). `lib/protocol.js` and `lib/formats.js` come from
[M-Flasher](https://github.com/jvitkauskas/m-flasher)'s web page (MIT).

Inside `assets/m-unbricker-*.bin`:

| Part | Licence | Source |
|---|---|---|
| M-Unbricker's own code; display, key and runtime code from m-bootloader | MIT | m-unbricker; [m-bootloader](https://github.com/jvitkauskas/m-bootloader) |
| M-Flasher's flash loader 1.3 (uploaded to the broken unit) | MIT ([MIT-m-bootloader.txt](MIT-m-bootloader.txt)) | m-bootloader |
| The rescue images: m-bootloader 2.3's boot areas, see [rescue-images.md](rescue-images.md) | MIT ([MIT-m-bootloader.txt](MIT-m-bootloader.txt)) | m-bootloader |
| Screen fonts, rendered from DejaVu Sans | Bitstream Vera / public domain ([DejaVu-fonts.txt](DejaVu-fonts.txt)) | DejaVu fonts |

`assets/stage-*.fwsc`: m-bootloader 2.3's RAM stage (MIT), built from its source with M-Unbricker's logo on its waiting screen.

The USB host driver was written from the register facts in the JieLi AC79 SDK (Apache-2.0); no SDK code
is included.
