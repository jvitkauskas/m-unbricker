// SPDX-License-Identifier: MIT
// Which board the page takes a unit for (issue #1: an SMK-37 Pro with the 0x856014 flash chip).
import { test } from "node:test";
import assert from "node:assert/strict";
import { boardFrom } from "../lib/assets.js";

const info = (jedec) => ({ kind: "stage", canRun: true, jedec });

test("the model's name wins over the flash chip", () => {
  assert.equal(boardFrom("ota-SMK-37 Pro_900", info(0x856014)), "smk37");
  assert.equal(boardFrom("ota-SMK-37 Pro_900", info(0xeb6014)), "smk37");
  assert.equal(boardFrom("ota-FM-1_900", info(0x856014)), "fm1");
  assert.equal(boardFrom("ota-FM-1_900", info(0xb36014)), "fm1");
  assert.equal(boardFrom("SMK-37 Elite_016", null), "smk37");
  assert.equal(boardFrom("SMK-37+VA_102", null), "smk37");
  assert.equal(boardFrom("MiniFM_015", null), "fm1");
});

test("the flash chip only when the name is unknown", () => {
  assert.equal(boardFrom("Jangada_101", info(0xeb6014)), "smk37");
  assert.equal(boardFrom("Jangada_101", info(0x856014)), "fm1");
  assert.equal(boardFrom("Jangada_101", null), null);
  assert.equal(boardFrom("something else", info(0x123456)), null);
});
