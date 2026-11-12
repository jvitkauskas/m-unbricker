// SPDX-License-Identifier: MIT
// The stage packages against the checks newer Felucca-family firmware makes before it stores an
// updater (Skiff 2.5.1, firmware/src/ota.c: ota_ufw and ota_stage steps 1-2), ported here.
import { test } from "node:test";
import assert from "node:assert/strict";
import { readFileSync } from "node:fs";
import { crc16, enc, parsePackage } from "../lib/formats.js";
import { sizedImage } from "../lib/assets.js";

const rd16 = (b, at) => b[at] | (b[at + 1] << 8);
const rd32 = (b, at) => (b[at] | (b[at + 1] << 8) | (b[at + 2] << 16) | (b[at + 3] << 24)) >>> 0;
const crc = (b) => crc16(b);

/** Skiff's verdict: 0 = it would store the updater, else its error code. */
function skiff(image, flLen) {
  const hdr = enc(image.subarray(0, 0x40), 0xffff);
  const nent = rd16(hdr, 8);
  const list = image.subarray(0x40, 0x40 + nent * 0x50);
  if (crc(hdr.subarray(2, 0x40)) !== rd16(hdr, 0) || nent === 0 || nent > 11 || crc(list) !== rd16(hdr, 2)) return -2;
  let flOff = 0, flLenGot = 0, otaOff = 0, otaLen = 0;
  for (let i = 0; i < nent; i++) {
    const e = enc(list.subarray(i * 0x50, i * 0x50 + 0x50), 0xffff);
    if (rd16(e, 0) === 0) { flOff = rd32(e, 8); flLenGot = rd32(e, 12); }
    if (rd16(e, 0) === 100) { otaOff = rd32(e, 8); otaLen = rd32(e, 12); }
  }
  if (!flOff || !otaOff) return -3;
  if (flLenGot !== flLen) return -15;
  const b = image.subarray(otaOff, otaOff + 512);
  const len = rd32(b, 8);
  if (crc(b.subarray(2, 32)) !== rd16(b, 0) || rd32(b, 4) !== 0x20 || b[12] !== 0x41 || otaLen !== len + 0x20 ||
    len > 0x5000 - 0x200 || crc(b.subarray(0x22, 0x40)) !== rd16(b, 0x20) || rd32(b, 0x28) !== 0x01c0a800) return -7;
  const mark = new TextEncoder().encode("FELUCCA-LOADER-1");
  let own = false;
  for (let i = 0x48; i + 16 <= 0x48 + 64 && !own; i++) own = mark.every((c, j) => b[i + j] === c) && rd32(b, 0x24) <= 0x14000;
  return own ? 0 : -7;
}

const pkg = (name) => parsePackage(new Uint8Array(readFileSync(new URL(`../assets/${name}.fwsc`, import.meta.url))));

for (const [variant, file, flLen] of [["fm1", "stage-fm1", 0x93000], ["smk37-pro", "stage-smk37-pro", 0x9d000], ["smk37-elite", "stage-smk37-elite", 0x9d000]]) {
  test(`${variant}: Skiff refuses the plain and listed stage packages, takes the sized one`, () => {
    assert.equal(skiff(pkg(file).image, flLen), -3);
    assert.equal(skiff(pkg(`${file}-listed`).image, flLen), -15);
    const sized = sizedImage(pkg(`${file}-listed`), variant);
    assert.equal(skiff(sized, flLen), 0);
  });
  test(`${variant}: the sized package is a well-formed package with the same updater`, () => {
    const listed = pkg(`${file}-listed`);
    const sized = sizedImage(listed, variant);
    const head = enc(sized.subarray(0, 0x40), 0xffff);
    assert.equal(rd32(head, 4), sized.length);
    // parse it as the image (no identity weaving here): files and their CRCs
    const count = rd16(head, 8);
    for (let i = 0; i < count; i++) {
      const e = enc(sized.subarray(0x40 + i * 0x50, 0x90 + i * 0x50), 0xffff);
      const data = sized.subarray(rd32(e, 8), rd32(e, 8) + rd32(e, 12));
      assert.equal(crc(data), rd16(e, 4));
    }
    const ota = listed.files["ota.bin"].data;
    const at = ota.byteOffset - listed.image.byteOffset;
    assert.deepEqual(sized.subarray(at, at + ota.length), ota);
  });
}
