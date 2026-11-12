// SPDX-License-Identifier: MIT
// What this page carries: m-bootloader's RAM stage packages (from M-Flasher's web page, m-bootloader
// 2.3, MIT) and m-unbricker for each board.
import { parsePackage } from "./formats.js?v=dev";

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

/** The stage package for a variant; `listed` for Felucca-family firmware. */
export async function loadStage(variant, listed) {
  return parsePackage(await fetchBytes(`${STAGE[variant]}${listed ? "-listed" : ""}.fwsc`));
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
