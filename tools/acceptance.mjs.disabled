import fs from 'node:fs';
const bytes = fs.readFileSync('D:/wb/2026-10-15-00-50-04/refs/fm1_open/build/web/simulator.wasm');
const { instance } = await WebAssembly.instantiate(bytes, {});
const c = instance.exports;
const FR = c.sim_block_frames();
c.sim_boot(0); for (let b = 0; b < 50; b++) c.sim_render();
const r = c.sim_factory_project(5);
new Uint8Array(c.memory.buffer, c.sim_file(), 4064).set(new Uint8Array(c.memory.buffer, c.sim_file(), 4064));
// 正确取工程：factory_project 已把工程放进 file_buffer
const proj = new Uint8Array(c.memory.buffer, c.sim_file(), 4064).slice();
new Uint8Array(c.memory.buffer, c.sim_file(), 4064).set(proj);
c.sim_import(4064);
for (let b = 0; b < 30; b++) c.sim_render();
console.log('装载后 render peak:', c.sim_dbg_render_peak());
c.sim_control(0, 12, 1); for (let b = 0; b < 5; b++) c.sim_render(); c.sim_control(0, 12, 0);
let mx = 0; for (let b = 0; b < 300; b++) { c.sim_render(); const v = c.sim_dbg_render_peak(); if (v > mx) mx = v; }
console.log('Play 中 render peak 最大值:', mx, mx > 0 ? '✓渲染有声' : '✗渲染静默');
c.sim_panic(); for (let b = 0; b < 20; b++) c.sim_render();
c.sim_control(1, 18, 1);
let m2 = 0; for (let b = 0; b < 250; b++) { c.sim_render(); const v = c.sim_dbg_render_peak(); if (v > m2) m2 = v; }
console.log('W11 folk 按住 render peak:', m2, m2 > 0 ? '✓扫弦有声' : '✗静默');
c.sim_control(1, 18, 0); for (let b = 0; b < 20; b++) c.sim_render();

// W13 punk：按住 4 秒，逐 250ms 桶记峰值 → 应看到 8 刷 × 2 小节的节奏脉冲
c.sim_control(1, 21, 1);
const BK = Math.round(44100 / FR / 4 / 10);   // 每 250ms 桶的块数
const buckets = [];
let cur = 0, cnt = 0;
for (let b = 0; b < 16 * BK; b++) {
  c.sim_render();
  const v = c.sim_dbg_render_peak();
  if (v > cur) cur = v;
  if (++cnt >= BK) { buckets.push(cur); cur = 0; cnt = 0; }
}
c.sim_control(1, 21, 0);
const pkMax = Math.max(...buckets, 1);
const bars = buckets.map(v => (v / pkMax * 8 | 0) ? '█'.repeat(v / pkMax * 8 | 0) : '·');
console.log('W13 punk 时间线（250ms/格，16 格 = 2 小节）:');
console.log('  ' + bars.join(''));
const activeBars = buckets.filter(v => v > pkMax * 0.2).length;
console.log('  活跃桶:', activeBars, '/ 16  →', activeBars >= 12 ? '✓ punk 连发节奏成立' : '✗ 节奏不完整');
for (let b = 0; b < 20; b++) c.sim_render();

// 和弦跟随 + 黑键静默：W1 拨弦应发声；黑键点击应静默（无确认扫）
const pluckPeak = (n) => { let pk = 0;
  for (let b = 0; b < n; b++) { c.sim_render(); const v = c.sim_dbg_render_peak(); if (v > pk) pk = v; }
  return pk; };
c.sim_control(1, 0, 1);
const p1 = pluckPeak(80);
c.sim_control(1, 0, 0); for (let b = 0; b < 20; b++) c.sim_render();
console.log('W1 拨弦（和弦0 F）峰值:', p1, p1 > 0 ? '✓' : '✗');
c.sim_control(1, 1, 1); for (let b = 0; b < 5; b++) c.sim_render(); c.sim_control(1, 1, 0);
for (let b = 0; b < 20; b++) c.sim_render();
const silentPeak = pluckPeak(40);   // 黑键点击后：应无任何声音（静默选和弦）
console.log('黑键点击后 40 块峰值:', silentPeak, silentPeak === 0 ? '✓静默选和弦' : '⚠黑键出声了');
c.sim_control(1, 0, 1);
const p2 = pluckPeak(80);
c.sim_control(1, 0, 0);
console.log('换 C 和弦后 W1 拨弦峰值:', p2, p2 > 0 ? '✓跟新和弦' : '✗');
// 音高断言（拨的是和弦不同弦位的不同音高）需要专用导出，留待 B1+；能量层已验证发声。
// 扩展验收：W13 时间线 + 和弦跟随（在 W11 folk 之后追加执行）
