# m-unbricker artwork

Made for **m-unbricker**, which lets one working synthesizer repair another over a USB-C to USB-C cable. This original project artwork is licensed with the project under the **MIT license**; see [LICENSE](LICENSE).

## Making the pictures

All 28 illustrations were drawn with the built-in OpenAI image generation tool, one generation per requested picture, with targeted image-generation corrections after review. No API/CLI fallback was used. The orange FM-1 was based on the supplied photograph. The SMK-37 shape was checked against [product reference pictures](https://vietmusic.vn/id/products/midi-keyboard-controller-m-vave-smk-37-pro), then rendered dark grey as requested. Third-party reference photographs are not included or relicensed here.

The brief was a clean, friendly, flat pictogram system: an orange pocket FM-1 with a square screen, side knobs and lower rounded pads; a wider dark SMK-37 keyboard with upper controls; a consistent grey unknown-synth silhouette; and rounded USB-C connectors. Lit screens indicate working/repaired devices, blank dark screens indicate bricked devices. Pictures have no words, numbers, branding or labels. Question marks, play triangles, arrows, checks and warning symbols carry the action. Names appear only outside the illustrations on the contact sheet.

The generated hero and keyboard variant served as visual references for later generations. Generic switch, chip and plug symbols were generated independently to avoid unwanted product bodies. The unplug arrow was corrected to point away from the laptop; the identified FM-1 screen was corrected to stay black behind the magnifier.

## Production files

- `device/*.png`: 18 opaque **480 × 480** indexed PNGs, black plus at most three additional colours, encoded with a 2-bit palette. Each has at least a 24 px empty black margin. `device/palette.json` is the exact per-picture palette.
- `web/*.png`: 10 opaque **960 × 720** PNGs on **#f5f5f2**, flattened to a small consistent full-colour palette.
- `contact_sheet.png`: all production pictures with their filenames; every device picture appears both enlarged for review and at its actual **120 × 120** display size. Those device previews use nearest-neighbour reduction to preserve the shipped colours.
- `sources/*.png`: the 28 selected, unquantised image-generation masters (about 1 MB each). They are not in the repository; the production files above are what the device and the web wizard use.
- `sources/generation-manifest.json`: the complete generation prompt set, selected source paths and any correction prompts.
- `build_assets.py`: reproducible export and contact-sheet script; needs Pillow and NumPy. Run `python3 art/build_assets.py` from the project root. It writes only inside this art directory and does not generate new images.
- `validation.json`: per-file dimensions, actual colour counts and opacity from the export checks.

Device export crops the occupied source bounds, fits them within 432 × 432 on black, resamples, then maps every pixel to the nearest specified RGB palette entry with **no dithering**. No antialias colours or transparency survive in the shipped device PNGs. Green was chosen sufficiently saturated that grey housings do not accidentally map to green. Web export fits rather than clips the source, flattens colours, keeps neutral pixels neutral, and sets the border-connected background to exactly #f5f5f2.

## Review limits

The main actions and device silhouettes remain readable at 120 × 120. The paired SMK-37 welcome/connect scenes are the most crowded. **Some secondary key gaps, small controls and connector details remain thinner than the requested 12 px minimum at 480 px.** Dimensions, colour limits, black margins and opacity are checked by the script; minimum feature width is not a passed check.

These are separate generated pictograms using common references, so tiny control counts, spacing and contours vary slightly between scenes; they are not pixel-identical reusable product drawings. The SMK-37 keys and control bank are deliberately abbreviated for legibility, rather than an exact drawing of all 37 keys. The locked chip in `leave` conveys leaving memory unchanged, but its accompanying wizard text should supply the precise meaning.

## File inventory

Device: `welcome_fm1.png`, `welcome_smk37.png`, `unplug_pc_fm1.png`, `unplug_pc_smk37.png`, `connect_fm1.png`, `connect_smk37.png`, `switch_on.png`, `waking.png`, `found_fm1.png`, `found_smk37.png`, `checking.png`, `press_play.png`, `repairing.png`, `success_fm1.png`, `success_smk37.png`, `error.png`, `retry.png`, `leave.png`.

Web: `hero.png`, `plug_good.png`, `sending.png`, `move_cable.png`, `switch_on.png`, `press_play.png`, `working.png`, `success.png`, `trouble.png`, `smk37_variant.png`.

The exact palettes are in [device/palette.json](device/palette.json).
