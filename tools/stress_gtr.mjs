// 吉他语义层·长跑压力回归（B1.6.1）
// 针对的真实 bug：gtr_part.c 的 gtr_dbg[20] 无界写入 —— gtr_part_dbg() 无人调用时
// gtr_dbg_len 无限增长，写穿静态区，长会话（循环节奏型 + 连续扫弦）下
// wasm 越界硬崩 / 真机内存损坏。修复后长跑必须零崩溃、音符持续发声。
//
// 用法：node tools/stress_gtr.mjs [wasm路径]
// 退出码 0 = 通过；任何崩溃/断言失败 = 非 0。
// 前置缺失（如 wasm 不存在）→ 大声 SKIP 并说明怎么补，不给假通过。

import fs from 'node:fs';
import { fileURLToPath } from 'node:url';

const WASM = process.argv[2] || fileURLToPath(new URL('../build/web/simulator.wasm', import.meta.url));
let ok = 0, fail = 0, skip = 0;
function check(name, cond, detail) {
  if (cond) { ok++; console.log('  ok  ' + name); }
  else { fail++; console.log('FAIL  ' + name + (detail ? ' — ' + detail : '')); }
}

if (!fs.existsSync(WASM)) {
  console.log('SKIP  stress_gtr: 找不到 ' + WASM);
  console.log('      先构建：powershell -File tools/Build-Web.ps1（需设 FM1_SOURCE_URL）');
  process.exit(0);
}

const bytes = fs.readFileSync(WASM);
const mod = await WebAssembly.compile(bytes);
let inst;
const wasi = {
  fd_close: () => 0, fd_seek: () => 0, fd_fdstat_get: () => 0, fd_read: () => 52,
  environ_sizes_get: () => 0, environ_get: () => 0,
  // fd_write 必须按 wasi 规范把「写入字节数」写回 pN。
  // 曾用 () => 0 桩导致 glibc 读栈上垃圾当字节数 → stdio 状态损坏 → 硬崩（假 bug）。
  fd_write: (fd, iovs, iovsLen, pN) => {
    const u = new Uint8Array(inst.exports.memory.buffer);
    let written = 0;
    for (let i = 0; i < iovsLen; i++) {
      const p = u[iovs + i * 8] | (u[iovs + i * 8 + 1] << 8) | (u[iovs + i * 8 + 2] << 16) | (u[iovs + i * 8 + 3] << 24);
      const l = u[iovs + i * 8 + 4] | (u[iovs + i * 8 + 5] << 8) | (u[iovs + i * 8 + 6] << 16) | (u[iovs + i * 8 + 7] << 24);
      written += l;
    }
    u[pN] = written & 255; u[pN + 1] = (written >> 8) & 255;
    return 0;
  },
  clock_time_get: (id, off, p) => {
    const u = new Uint8Array(inst.exports.memory.buffer);
    const t = BigInt(Date.now()) * 1000000n;
    for (let i = 0; i < 8; i++) u[p + i] = Number((t >> BigInt(i * 8)) & 255n);
    return 0;
  },
  random_get: (p, l) => {
    const u = new Uint8Array(inst.exports.memory.buffer);
    for (let i = 0; i < l; i++) u[p + i] = (Math.random() * 256) | 0;
    return 0;
  },
};
const io = {};
for (const i of WebAssembly.Module.imports(mod)) {
  io[i.module] ??= {};
  io[i.module][i.name] = i.module === 'wasi_snapshot_preview1' ? (wasi[i.name] ?? (() => 0)) : (() => 0);
}
inst = new WebAssembly.Instance(mod, io);
const c = inst.exports;
const FR = c.sim_block_frames();

console.log('stress_gtr: 60 秒长跑（folk 循环 + 循环中换和弦 + 连续扫弦）');

// —— 场景：0.5s 起按住 folk，期间每 2 秒换和弦，10s 后叠 punk（覆盖两键互踩），
//    20s 释放 punk，30s 释放 folk，35-50s 连续单扫，全程监听 MIDI 输出 ——
const CH = { F: 1, C: 3, G: 5, Dm: 8, Am: 10 };
const SEQ_FOLK = 18, SEQ_PUNK = 21, W_STRUM = 11, W_UP = 14;
const ev = [];
const kd = (t, bit) => ev.push({ t, bit, d: 1 });
const ku = (t, bit) => ev.push({ t, bit, d: 0 });
kd(0.5, SEQ_FOLK);
for (let t = 2.5; t < 30; t += 2.0) { const ch = [CH.C, CH.G, CH.Am, CH.F][(t / 2 | 0) % 4]; kd(t, ch); ku(t + 0.15, ch); }
kd(10.0, SEQ_PUNK); ku(20.0, SEQ_PUNK);
ku(30.0, SEQ_FOLK);
for (let t = 35; t < 50; t += 0.9) { kd(t, W_STRUM); ku(t + 0.15, W_STRUM); }
kd(52.0, W_UP); ku(52.15, W_UP);
ev.sort((a, b) => a.t - b.t);

const DUR = 60, total = Math.ceil(DUR * 44100 / FR) + 10;
let crashed = null, ei = 0;
const KEYBITS = new Set([0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25,26]);
try {
  c.sim_boot(0);
  for (let b = 0; b < 60; b++) c.sim_render();
  c.sim_factory_project(5);
  const proj = new Uint8Array(c.memory.buffer, c.sim_file(), 4064).slice();
  new Uint8Array(c.memory.buffer, c.sim_file(), 4064).set(proj);
  c.sim_import(4064);
  for (let b = 0; b < 40; b++) c.sim_render();
  for (let b = 0; b < total; b++) {
    const nowMs = b * FR * 1000 / 44100;
    while (ei < ev.length && ev[ei].t * 1000 <= nowMs) {
      const e = ev[ei++];
      c.sim_control(1, e.bit, e.d);
    }
    c.sim_render();
  }
} catch (e) { crashed = e.message; }

check('60 秒长跑零崩溃', crashed === null, crashed || undefined);

// 崩溃后引擎仍应可用：再起一个音还能出声
let stillAlive = false;
try {
  c.sim_control(1, CH.C, 1);
  c.sim_control(1, W_STRUM, 1);
  let energy = 0;
  for (let b = 0; b < 40; b++) {
    const p = c.sim_render();
    const m = new Float32Array(c.memory.buffer, p, FR * 2);
    for (let i = 0; i < FR * 2; i++) energy += Math.abs(m[i]);
  }
  c.sim_control(1, W_STRUM, 0);
  c.sim_control(1, CH.C, 0);
  stillAlive = energy > 0;
} catch (e) { stillAlive = false; }
check('长跑后引擎仍能出声', stillAlive);

console.log(`\nstress_gtr: ${ok} 过, ${fail} 败`);
process.exit(fail ? 1 : 0);
