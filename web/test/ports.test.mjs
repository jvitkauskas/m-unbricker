// SPDX-License-Identifier: MIT
// Which MIDI ports the page tries: a unit's port name can't be relied on, as macOS keeps a device's earlier name.
import { test } from "node:test";
import assert from "node:assert/strict";
import { findPorts, portNames } from "../lib/protocol.js";

function access(names, gone = []) {
  const port = (name) => ({ name, state: gone.includes(name) ? "disconnected" : "connected" });
  const map = (list) => new Map(list.map((name, i) => [String(i), port(name)]));
  return { inputs: map(names), outputs: map(names) };
}

test("by name: ports named like a unit only", () => {
  const a = access(["IAC Driver Bus 1", "Felucca Update", "Jangada Update", "FM-1"]);
  assert.deepEqual(findPorts(a).map((p) => p.name), ["Felucca Update", "FM-1"]);
});

test("any: every port, those named like a unit first", () => {
  const a = access(["IAC Driver Bus 1", "Jangada Update", "SMK-37 Update"]);
  assert.deepEqual(findPorts(a, { any: true }).map((p) => p.name), ["SMK-37 Update", "IAC Driver Bus 1", "Jangada Update"]);
});

test("disconnected ports are left out", () => {
  const a = access(["FM-1", "Jangada Update"], ["FM-1"]);
  assert.deepEqual(findPorts(a, { any: true }).map((p) => p.name), ["Jangada Update"]);
  assert.deepEqual(portNames(a), ["Jangada Update"]);
});
