/* Render the STRUM page from the real firmware and write a PNG.
 *
 * Not a mock-up: this boots build/web/simulator.wasm, drives the same panel
 * API the hardware uses, and dumps the 240x240 RGB565 framebuffer.
 *
 * Usage: node tools/shot_strum.mjs [out.png] */
import fs from 'fs';
import path from 'path';
import zlib from 'zlib';
import { fileURLToPath } from 'url';

const __dirname = path.dirname(fileURLToPath(import.meta.url));
const ROOT = path.resolve(__dirname, '..');
const WASM = path.join(ROOT, 'build/web/simulator.wasm');
const OUT = process.argv[2] || path.join(ROOT, 'build/strum-page.png');

const BTN = { SEQ: 11, EDIT: 6, SAVE: 9, HOME: 8 };
const WHITE = [0, 2, 4, 6, 7, 9, 11, 12, 14, 16, 18, 19, 21, 23, 24, 26];

function png(width, height, rgb) {
  const raw = Buffer.alloc((width * 3 + 1) * height);
  for (let y = 0; y < height; y++) {
    raw[y * (width * 3 + 1)] = 0;
    rgb.copy(raw, y * (width * 3 + 1) + 1, y * width * 3, (y + 1) * width * 3);
  }
  const chunk = (type, data) => {
    const len = Buffer.alloc(4); len.writeUInt32BE(data.length);
    const body = Buffer.concat([Buffer.from(type, 'ascii'), data]);
    const crc = Buffer.alloc(4); crc.writeUInt32BE(zlib.crc32 ? zlib.crc32(body) : crc32(body));
    return Buffer.concat([len, body, crc]);
  };
  function crc32(buf) {
    let c, crc = 0xffffffff;
    for (let n = 0; n < buf.length; n++) {
      c = (crc ^ buf[n]) & 0xff;
      for (let k = 0; k < 8; k++) c = c & 1 ? 0xedb88320 ^ (c >>> 1) : c >>> 1;
      crc = (crc >>> 8) ^ c;
    }
    return (crc ^ 0xffffffff) >>> 0;
  }
  const ihdr = Buffer.alloc(13);
  ihdr.writeUInt32BE(width, 0); ihdr.writeUInt32BE(height, 4);
  ihdr[8] = 8; ihdr[9] = 2; ihdr[10] = 0; ihdr[11] = 0; ihdr[12] = 0;
  return Buffer.concat([
    Buffer.from([0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a]),
    chunk('IHDR', ihdr),
    chunk('IDAT', zlib.deflateSync(raw, { level: 9 })),
    chunk('IEND', Buffer.alloc(0)),
  ]);
}

async function main() {
  const { instance } = await WebAssembly.instantiate(fs.readFileSync(WASM), {});
  const ex = instance.exports;
  const adv = (ms) => {
    const blockMs = ex.sim_block_frames() * 1000 / 44100;
    for (let i = 0, n = Math.ceil(ms / blockMs) + 5; i < n; i++) ex.sim_render();
  };
  const tap = (b) => { ex.sim_control(0, b, 1); ex.sim_control(0, b, 0); };
  const tapKey = (bit) => { ex.sim_control(1, bit, 1); ex.sim_control(1, bit, 0); };

  ex.sim_boot(1);
  adv(150);
  ex.sim_control(0, BTN.SEQ, 1); ex.sim_control(0, BTN.EDIT, 1);
  ex.sim_control(0, BTN.EDIT, 0); ex.sim_control(0, BTN.SEQ, 0);
  adv(80);
  ex.sim_control(2, 0, 21);          /* action 21 = STRUM EDITOR */
  tap(BTN.SAVE);
  adv(120);

  /* Write a real pattern: D - U - D X U - M - U - D - U - (campfire-ish,
   * one chuck and one palm mute so all five symbols appear on screen). */
  const plan = [[0, 1], [2, 3], [4, 1], [5, 5], [6, 3], [8, 8], [10, 3], [12, 1], [15, 3]];
  for (const [step, black] of plan) {
    tapKey(WHITE[step]);          /* select / activate */
    adv(40);
    tapKey(black);                /* assign the action directly */
    adv(40);
  }
  /* Park the cursor on the chuck step so the screenshot shows the highlight. */
  tapKey(WHITE[5]);
  adv(60);
  tap(BTN.SAVE);                  /* commit -> LIVE badge */
  adv(400);

  const px = new Uint16Array(ex.memory.buffer, ex.sim_pixels(), 240 * 240);
  const rgb = Buffer.alloc(240 * 240 * 3);
  for (let i = 0; i < px.length; i++) {
    const v = px[i];
    const r = ((v >> 11) & 0x1f) * 255 / 31;
    const g = ((v >> 5) & 0x3f) * 255 / 63;
    const b = (v & 0x1f) * 255 / 31;
    rgb[i * 3] = Math.round(r); rgb[i * 3 + 1] = Math.round(g); rgb[i * 3 + 2] = Math.round(b);
  }
  fs.mkdirSync(path.dirname(OUT), { recursive: true });
  fs.writeFileSync(OUT, png(240, 240, rgb));
  console.log('wrote ' + OUT + '  (' + fs.statSync(OUT).size + ' bytes)');
}

main().catch((e) => { console.error(e); process.exit(1); });
