// SPDX-License-Identifier: MIT
// What this page carries: m-bootloader's RAM stage packages (from M-Flasher's web page, m-bootloader
// 2.3, MIT) and m-unbricker for each board.
import { crc16, enc, parsePackage } from "./formats.js?v=dev";

/** Which stage build a firmware's model takes. */
const VARIANTS = {
  "FM-1": "fm1", MiniFM: "fm1", "SMK-37 Pro": "smk37-pro", "SMK-37+VA": "smk37-pro", "SMK-37 Elite": "smk37-elite",
};
const STAGE = { fm1: "stage-fm1", "smk37-pro": "stage-smk37-pro", "smk37-elite": "stage-smk37-elite" };

export const variantOf = (model) => VARIANTS[model] ?? null;

/** ChanceTheMaker's SMK firmware: an update request starts its own updater, which only takes its own
 * kind of package. Tested on SMK-37_930 (M-Flasher web). */
export const hasOwnUpdater = (identity) => /^(ota-)?SMK-37_9\d\d$/.test(identity ?? "");

async function fetchBytes(file) {
  const response = await fetch(new URL(`../assets/${file}`, import.meta.url), { cache: "no-cache" });
  if (!response.ok) throw new Error(`this page is missing ${file}`);
  return new Uint8Array(await response.arrayBuffer());
}

/** flash.bin's size in this board's own packages: what newer Felucca-family firmware (Skiff 2.0.2 and
 * later, its ota.c OTA_FL_LEN) requires of a package whose updater isn't M-VAVE's. */
const FLASH_LEN = { fm1: 0x93000, "smk37-pro": 0x9d000, "smk37-elite": 0x9d000 };

const u16 = (b, at) => b[at] | (b[at + 1] << 8);
const u32 = (b, at) => (b[at] | (b[at + 1] << 8) | (b[at + 2] << 16) | (b[at + 3] << 24)) >>> 0;
const put16 = (b, at, v) => { b[at] = v & 0xff; b[at + 1] = (v >>> 8) & 0xff; };
const put32 = (b, at, v) => { for (let i = 0; i < 4; i++) b[at + i] = (v >>> (8 * i)) & 0xff; };

/**
 * The listed package's image with its blank flash.bin listed at the board's full size (0xFF), so
 * firmware that checks that size takes the stage too. Nothing reads flash.bin: Felucca-family
 * firmware checks only its size for an updater with the FELUCCA-LOADER-1 mark, and the stage
 * ignores it.
 */
export function sizedImage(listed, variant) {
  const image = listed.image;
  const head = enc(image.subarray(0, 0x40), 0xffff);
  const count = u16(head, 8);
  const list = image.slice(0x40, 0x40 + 0x50 * count);
  let at = -1;
  for (let i = 0; i < count; i++) if (u16(enc(list.subarray(i * 0x50, i * 0x50 + 0x50), 0xffff), 0) === 0) at = i * 0x50;
  if (at < 0) throw new Error("the listed stage package lists no flash.bin");
  const entry = enc(list.subarray(at, at + 0x50), 0xffff);
  const offset = u32(entry, 8);
  const len = FLASH_LEN[variant];
  const out = new Uint8Array(offset + len).fill(0xff);
  out.set(image.subarray(0, offset));
  put16(entry, 4, crc16(out.subarray(offset, offset + len)));
  put32(entry, 12, len);
  put32(entry, 16, len);
  list.set(enc(entry, 0xffff), at);
  out.set(list, 0x40);
  put16(head, 2, crc16(list));
  put32(head, 4, out.length);
  put16(head, 0, crc16(head.subarray(2)));
  out.set(enc(head, 0xffff), 0);
  return out;
}

/** The stage package for a variant, as the image to serve: "plain" (no flash.bin), "listed" (a blank
 * 32-byte flash.bin) or "sized" (listed at the board's size). */
export async function loadStage(variant, kind) {
  const pkg = parsePackage(await fetchBytes(`${STAGE[variant]}${kind === "plain" ? "" : "-listed"}.fwsc`));
  const ota = pkg.files["ota.bin"];
  const end = ota ? ota.data.byteOffset - pkg.image.byteOffset + ota.data.length : pkg.image.length;
  return { image: kind === "sized" ? sizedImage(pkg, variant) : pkg.image, end };
}

/** m-unbricker for the board it runs on: "fm1" or "smk37". */
export async function loadUnbricker(board) {
  const app = await fetchBytes(`m-unbricker-${board}.bin`);
  const t = app.length - 8;
  if (app.length < 16 || String.fromCharCode(...app.subarray(t, t + 4)) !== "APP1" || app[t + 6] !== (board === "smk37" ? 2 : 1)) {
    throw new Error(`m-unbricker-${board}.bin on this page is damaged`);
  }
  return app;
}
