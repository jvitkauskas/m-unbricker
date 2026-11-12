// SPDX-License-Identifier: MIT
// runApp() against a simulated MIDI mode (m-bootloader src/midi_update.c load_app / run_app).
import { test } from "node:test";
import assert from "node:assert/strict";
import { readFileSync } from "node:fs";
import { Link, Unit, pack7, parseMessage } from "../lib/protocol.js";

function frame(command, body) {
  let sum = 0;
  for (const b of body) sum += b;
  const d = Uint8Array.from([0, 0x59, command, body.length & 0xff, (body.length >> 8) & 0xff, body.length >> 16, ...body, ~sum & 0xff]);
  return Uint8Array.from([0xf0, ...pack7(d), 0xf7]);
}
const crc16 = (d) => { let r = 0; for (const b of d) { r ^= b << 8; for (let i = 0; i < 8; i++) r = ((r << 1) ^ (r & 0x8000 ? 0x1021 : 0)) & 0xffff; } return r; };

function fakeUnit(board, { refuse = false } = {}) {
  const input = { state: "connected", onmidimessage: null };
  let pending = null;
  const ram = [];
  const deliver = (bytes) => setTimeout(() => input.onmidimessage?.({ data: bytes }), 0);
  const ask = (addr, n) => { pending = { addr, n }; deliver(frame(0x30, Uint8Array.of(0, addr & 255, (addr >> 8) & 255, (addr >> 16) & 255, addr >>> 24, n & 255, n >> 8, 0))); };
  let size = 0, phase = 0, trailer = null, at = 0, runBody = null;
  const output = {
    state: "connected",
    send(bytes) {
      const m = parseMessage(Uint8Array.from(bytes));
      if (m.command === 0x25) { runBody = m.body.slice(); size = m.body[4] | (m.body[5] << 8) | (m.body[6] << 16) | (m.body[7] << 24); ask(size - 8, 8); return; }
      if (m.command !== 0x30) return;
      const data = m.body.subarray(8);
      if (phase === 0) {
        trailer = data.slice();
        const ok = String.fromCharCode(...trailer.subarray(0, 4)) === "APP1" && trailer[6] === board && trailer[7] === 1;
        if (!ok || refuse) { deliver(frame(0, Uint8Array.of(1))); return; }
        phase = 1; at = 0; ask(0, Math.min(512, size));
        return;
      }
      ram.push(...data); at += data.length;
      if (at < size) { ask(at, Math.min(512, size - at)); return; }
      const all = Uint8Array.from(ram);
      const good = crc16(all.subarray(0, size - 8)) === (all[size - 4] | (all[size - 3] << 8));
      deliver(good ? frame(0x25, runBody) : frame(0, Uint8Array.of(1)));
    },
  };
  return { input, output };
}

for (const [name, board] of [["fm1", 1], ["smk37", 2]]) {
  test(`runApp loads m-unbricker-${name}.bin`, async () => {
    const app = new Uint8Array(readFileSync(new URL(`../assets/m-unbricker-${name}.bin`, import.meta.url)));
    const { input, output } = fakeUnit(board);
    const unit = new Unit(new Link(input, output));
    let last = 0;
    await unit.runApp(app, { canRun: true }, { onProgress: (d) => { last = d; } });
    assert.equal(last, app.length);
  });
}

test("runApp needs a unit that says it runs apps", async () => {
  const { input, output } = fakeUnit(1);
  const unit = new Unit(new Link(input, output));
  await assert.rejects(unit.runApp(new Uint8Array(32), { canRun: false }), /can't run apps/);
  await assert.rejects(unit.runApp(new Uint8Array(32), null), /can't run apps/);
});

test("a refused app is an error", async () => {
  const app = new Uint8Array(readFileSync(new URL("../assets/m-unbricker-fm1.bin", import.meta.url)));
  const { input, output } = fakeUnit(2); // the wrong board
  const unit = new Unit(new Link(input, output));
  await assert.rejects(unit.runApp(app, { canRun: true }), /refused/);
});
