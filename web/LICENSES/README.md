# What this page carries, and under which licences

The page and M-Unbricker itself (`assets/m-unbricker-*.bin`, `app.js`, `lib/`, the pictures in `img/`):
MIT, see [../LICENSE](../LICENSE). `lib/protocol.js` and `lib/formats.js` come from
[M-Flasher](https://github.com/jvitkauskas/m-flasher)'s web page (MIT).

Inside `assets/m-unbricker-*.bin`:

| Part | Licence | Source |
|---|---|---|
| M-Unbricker's own code; display, key and runtime code from m-bootloader | MIT | m-unbricker; [m-bootloader](https://github.com/jvitkauskas/m-bootloader) |
| M-Flasher's flash loader 1.3 (uploaded to the broken unit) | MIT ([MIT-m-bootloader.txt](MIT-m-bootloader.txt)) | m-bootloader |
| The rescue images: m-bootloader 2.3 and fm1-base's starter firmware (template), with the parts listed in [rescue-images.md](rescue-images.md) | MIT, MPL-2.0 ([MPL-2.0.txt](MPL-2.0.txt)), Apache-2.0 ([Apache-2.0.txt](Apache-2.0.txt)), OFL-1.1 fonts ([Barlow](OFL-1.1-Barlow.txt), [Terminus](OFL-1.1-Terminus.txt)) | [fm1-base](https://github.com/charlesvestal/fm1-base) (MPL-2.0 source: the commits named in rescue-images.md) |
| Screen fonts, rendered from DejaVu Sans | Bitstream Vera / public domain ([DejaVu-fonts.txt](DejaVu-fonts.txt)) | DejaVu fonts |

`assets/stage-*.fwsc`: m-bootloader 2.3's RAM stage packages (MIT), as on M-Flasher's web page.

The USB host driver was written from the register facts in the JieLi AC79 SDK (Apache-2.0); no SDK code
is included.
