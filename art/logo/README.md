# M-Unbricker logo mark

An orange, model-neutral synth with three broad pads, a USB-C cable loop, and a green heartbeat on its black screen. The pulse represents a bricked synth coming back to life. There are no words, letters, or numbers in the mark. It was made specifically for M-Unbricker and supports both the FM-1 and SMK-37 without depicting either model in detail.

## Files

| File | Use |
| --- | --- |
| `logo_device.png` | 480 × 480 opaque indexed PNG; pure black background; exactly four colours. Designed to be displayed around 160 × 160 on a 240 × 240 screen. |
| `logo_device_palette.json` | Black, orange, grey, and green, in palette order. |
| `logo.png` | 1024 × 1024 RGBA web/README mark with a transparent background. |
| `icon-256.png` | 256 × 256 app icon on a rounded dark `#161615` tile; transparent corners. |
| `icon-32.png` | 32 × 32 browser icon from the same master. |
| `logo_preview.png` | Review sheet: device at 160 px on black, web mark on `#f5f5f2` and `#161615`, icons at actual 256, 64, and 32 px. |
| `generated_source.png` | Original built-in imagegen output, retained for provenance. |
| `generation_prompt.txt` | Complete image-generation prompt. |
| `build_logo.py` | Reproducible local finishing, resizing, quantisation, preview, and validation. |
| `validation.json` | Mechanical checks of the delivered files. |

## How it was made

The brief was a friendly, flat, thick-outline pictogram for a free tool that lets one working M-VAVE FM-1 pocket synth or SMK-37 keyboard revive a bricked one over USB-C. The requested palette was orange `#ff7800`, greys, and alive/OK green `#27d68a`, with no text and strong small-size readability. Existing project artwork, `../web/hero.png` and `../device/welcome_fm1.png`, provided visual references; `../contact_sheet.png` was also reviewed.

The original raster was generated with OpenAI's built-in imagegen tool using those references and the prompt in `generation_prompt.txt`. The delivered design uses that generated geometry, with deterministic Python finishing: flatten colours, strengthen the exterior outline, and widen the orange pad dividers. These are bitmap derivatives of the generated mark, not a replacement drawn from scratch.

The device derivative simplifies the USB connector to a solid grey silhouette to remove its fine inset. Visible colour is fitted within 432 × 432 px and centred, giving a minimum 24 px pure-black border. It is resized and then quantised by nearest Euclidean RGB distance, with no dithering, to exactly:

```json
["#000000", "#ff7800", "#a8adb0", "#27d68a"]
```

The web master retains the connector inset and transparency. Icons are downsampled from the master on a rounded dark tile. Antialiasing is used for web/icon resizing; the device PNG contains only its four palette colours and has no gradients or shading.

The delivered sheet was visually inspected at the device's actual 160 px display size and the icons' actual 256, 64, and 32 px sizes. The orange device, green pulse, three pads, and cable remain distinguishable at 32 px. Mechanical checks verify dimensions, transparency, four exact device colours, black margins, and nominal feature widths of at least 16 px across the main straight sections.

To rebuild locally with Pillow, NumPy, and SciPy installed:

```sh
python3 art/logo/build_logo.py
```

All generated deliverables and provenance files are written only into this directory. No application or firmware files are changed.

## License

Made for this project and licensed with it under the MIT License. Copyright (c) 2026 m-unbricker contributors. See the project's [artwork MIT license](../LICENSE) for the full terms.
