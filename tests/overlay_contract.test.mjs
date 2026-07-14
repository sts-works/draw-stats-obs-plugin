import assert from "node:assert/strict";
import { readFileSync } from "node:fs";

const html = readFileSync(new URL("../data/overlay/index.html", import.meta.url), "utf8");

assert.match(html, /fetch\('\/state'/);
assert.match(html, /setInterval\(refresh, 250\)/);
assert.match(html, /Recording/);
assert.match(html, /Live activity/);
assert.match(html, /Recent activity/);
for (const kind of ["keyboard", "text", "click", "drag", "wheel"]) {
  assert.match(html, new RegExp(`${kind}:`));
}
assert.doesNotMatch(html, /session_token|artwork_id|processName|windowTitle/);

console.log("overlay contract ok");
