// SPDX-License-Identifier: MIT
// M-Unbricker's page: a wizard that starts M-Unbricker on a working FM-1 or SMK-37 over USB MIDI, then
// guides the user while the working unit repairs the broken one (it shows its own steps on screen).
import { FINISH, Link, Unit, UnitLost, findPorts } from "./lib/protocol.js?v=dev";
import { hasOwnUpdater, loadStage, loadUnbricker, variantOf } from "./lib/assets.js?v=dev";

const $ = (id) => document.getElementById(id);
const STEPS = [...document.querySelectorAll(".step")].map((s) => s.dataset.step);
const state = { access: null, link: null, unit: null, identity: null, info: null, board: null, busy: false };

// ---- steps ----

function go(step) {
  for (const s of document.querySelectorAll(".step")) s.hidden = s.dataset.step !== step;
  const i = STEPS.indexOf(step);
  $("dots").replaceChildren(...STEPS.map((_, j) => {
    const li = document.createElement("li");
    li.className = j < i ? "done" : j === i ? "now" : "";
    return li;
  }));
  window.scrollTo({ top: 0, behavior: "smooth" });
}

for (const b of document.querySelectorAll("[data-go]")) b.addEventListener("click", () => go(b.dataset.go));

function say(id, text, kind = "") {
  $(id).textContent = text;
  $(id).className = `status ${kind}`;
}

function setBoard(board) {
  state.board = board;
  const smk = board === "smk37";
  for (const e of document.querySelectorAll(".key-go")) e.textContent = smk ? "SEQ PLAY" : "PLAY";
  for (const e of document.querySelectorAll(".key-no")) e.textContent = smk ? "<" : "OCT−";
  for (const e of document.querySelectorAll(".unit-name")) e.textContent = smk ? "SMK-37" : "FM-1";
}

function buttons(...list) {
  $("send-buttons").replaceChildren(...list.map(([label, onClick, primary]) => {
    const b = document.createElement("button");
    b.textContent = label;
    if (primary) b.className = "primary";
    b.addEventListener("click", onClick);
    return b;
  }));
}

function progress(done, total, text) {
  $("progress").hidden = false;
  $("bar").max = total;
  $("bar").value = done;
  $("progress-text").textContent = text ?? `${Math.round((done * 100) / total)}%`;
}

// ---- finding the working unit ----

function closeLink() {
  state.link?.close();
  state.link = null;
  state.unit = null;
}

async function connect() {
  if (state.busy) return;
  if (!navigator.requestMIDIAccess) {
    say("plug-status", "This browser can't reach MIDI devices. Use Chrome or Edge on a computer, Chrome on Android, or the MIDIWeb app on an iPhone or iPad.", "error");
    return;
  }
  try {
    state.access ??= await navigator.requestMIDIAccess({ sysex: true });
  } catch (cause) {
    say("plug-status", `The browser didn't allow MIDI (${cause.message}). Allow it, then press Connect again.`, "error");
    return;
  }
  const ports = findPorts(state.access);
  const select = $("port-select");
  select.replaceChildren(...ports.map((p, i) => new Option(p.name, i)));
  $("port-row").hidden = ports.length < 2;
  if (!ports.length) {
    say("plug-status", "No FM-1 or SMK-37 found. Is it switched on, and plugged in with a data cable? Wait a few seconds after switching it on.", "error");
    return;
  }
  await open(ports[Number(select.value) || 0]);
}

async function open(port) {
  closeLink();
  state.busy = true;
  say("plug-status", `Talking to ${port.name}…`);
  try {
    state.link = new Link(port.input, port.output);
    state.unit = new Unit(state.link);
    state.identity = await state.unit.identity();
    if (!state.identity) {
      say("plug-status", `${port.name} didn't answer. Some firmware takes half a minute to start: wait, then press Connect again.`, "error");
      return;
    }
    state.info = await state.unit.info();
    decide();
  } catch (cause) {
    say("plug-status", cause.message, "error");
  } finally {
    state.busy = false;
  }
}

const modelOf = (identity) => /^(?:ota-)?(.+)_\d+$/.exec(identity)?.[1] ?? null;

function boardFrom(identity, info) {
  if (info?.jedec === 0xeb6014) return "smk37";
  if (info?.jedec === 0x856014 || info?.jedec === 0xb36014) return "fm1";
  const model = modelOf(identity) ?? "";
  return model.startsWith("SMK") ? "smk37" : model === "FM-1" || model === "MiniFM" ? "fm1" : null;
}

/** What the connected unit is, and the next step for it. */
function decide() {
  const { identity, info } = state;
  const board = boardFrom(identity, info);
  if (!board) {
    say("plug-status", `"${identity}" isn't an FM-1 or SMK-37 this page knows.`, "error");
    return;
  }
  setBoard(board);
  $("progress").hidden = true;
  say("send-status", "");
  go("send");
  if (info && info.canRun) {
    $("send-text").textContent = `Found your ${board === "smk37" ? "SMK-37" : "FM-1"}. This sends M-Unbricker to it. Its firmware and settings stay as they are.`;
    buttons(["Send M-Unbricker", sendApp, true]);
  } else if (info) {
    $("send-text").textContent = info.kind === "bootloader"
      ? "This unit's m-bootloader is too old to run M-Unbricker. Switch it off and on to start its firmware, then connect again."
      : "This unit's helper is too old to run M-Unbricker. Switch it off and on, then connect again.";
    buttons(["Back", () => go("plug")]);
  } else if (identity.startsWith("ota-")) {
    $("send-text").textContent = "This unit is waiting for a firmware update: it may be the broken one. Plug in the working unit instead.";
    buttons(["Back", () => go("plug")]);
  } else if (hasOwnUpdater(identity) || !variantOf(modelOf(identity))) {
    $("send-text").textContent = `Its firmware (${identity}) can't start the helper this page needs. If it has m-bootloader: hold PLAY (SEQ PLAY) while switching it on, choose MIDI MODE, and connect again. Or use another working unit.`;
    buttons(["Back", () => go("plug")]);
  } else {
    $("send-text").textContent = `Found your ${board === "smk37" ? "SMK-37" : "FM-1"}. First a small helper starts on it, then M-Unbricker is sent. Its firmware and settings stay as they are.`;
    buttons(["Start", startHelper, true]);
  }
}

// ---- the helper (m-bootloader's RAM stage), for a unit running firmware ----

/** Waits for a unit to come back after a restart; returns its port. */
async function reconnect(seconds, wanted) {
  closeLink();
  const end = Date.now() + seconds * 1000;
  while (Date.now() < end) {
    await new Promise((r) => setTimeout(r, 1000));
    for (const port of findPorts(state.access)) {
      const link = new Link(port.input, port.output);
      const unit = new Unit(link);
      const identity = await unit.identity(800);
      const info = identity ? await unit.info(600) : null;
      link.close();
      if (identity && wanted(identity, info)) return port;
    }
  }
  return null;
}

async function startHelper() {
  if (state.busy) return;
  state.busy = true;
  buttons();
  try {
    const variant = variantOf(modelOf(state.identity));
    // Which package a firmware takes: Felucca-family firmware (Felucca 9xx, Skiff 7xx, fm1-base 1100)
    // wants a flash.bin listed, and newer ones (Skiff) at the board's full size; M-VAVE's (013-016) and
    // Baud Girl's (102) refuse a blank one. A refusal changes nothing, so the next can follow.
    const family = Number(/_(\d+)$/.exec(state.identity)?.[1]) >= 200;
    let accepted = false;
    for (const kind of family ? ["sized", "listed", "plain"] : ["plain", "listed", "sized"]) {
      const { image, end } = await loadStage(variant, kind);
      try {
        await state.unit.serveUpdate(image, {
          finish: FINISH.APP,
          idleMs: 8000,
          onProgress: (done) => progress(Math.min(done, end), end, "Sending the helper"),
        });
        accepted = true;
        break;
      } catch (cause) {
        if (!(cause instanceof UnitLost)) throw cause;
      }
    }
    if (!accepted) throw new Error("The firmware didn't accept the helper, and keeps running. See \"Something not working?\" below.");
    progress(1, 1, "The unit is restarting into the helper…");
    const port = await reconnect(45, (identity, info) => info?.kind === "stage");
    if (!port) throw new Error("The helper didn't appear within 45 seconds. Switch the unit off and on, and start again.");
    state.busy = false;
    await open(port);
    if (state.info?.canRun) await sendApp(); // straight on: the helper is what was asked for
  } catch (cause) {
    say("send-status", cause.message, "error");
    buttons(["Try again", startHelper, true]);
  } finally {
    state.busy = false;
  }
}

// ---- M-Unbricker itself ----

async function sendApp() {
  if (state.busy) return;
  state.busy = true;
  buttons();
  try {
    const app = await loadUnbricker(state.board);
    progress(0, app.length, "Sending M-Unbricker");
    await state.unit.runApp(app, state.info, {
      onProgress: (done, total) => progress(done, total, `Sending M-Unbricker: ${Math.round((done * 100) / total)}%`),
    });
    closeLink();
    go("follow");
  } catch (cause) {
    say("send-status", cause.message, "error");
    buttons(["Try again", sendApp, true]);
  } finally {
    state.busy = false;
  }
}

$("connect").addEventListener("click", connect);
$("port-select").addEventListener("change", () => {
  const port = findPorts(state.access)[Number($("port-select").value)];
  if (port) open(port);
});
go("intro");
