// golden_gen.mjs · 从 JS 参考实现生成 golden.tsv（C 测试的考卷）
// 用法：node golden_gen.mjs          （写本目录下的 golden.tsv）
//
// 母本：fm1-guitar/engine/strum.js —— JS 参考实现，唯一权威。
// 它不属于本仓库，按以下顺序找：
//   1. 环境变量 FM1_JS_ENGINE 指向的 strum.js
//   2. <仓库>/../fm1-guitar/engine/strum.js   （本机常见布局：两个仓库并列）
// 找不到就明确报错，不要静默写一份旧的。
//
// 注意：改这个文件 = 改考卷。改了之后 C 测试可能挂 —— 那说明两侧真的分叉了，
// 必须先判断哪一边对，再同步，不能只为了让测试变绿而重写 C 实现。
import { createRequire } from 'node:module';
import { writeFileSync, existsSync } from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

const here = path.dirname(fileURLToPath(import.meta.url));
const repoRoot = path.resolve(here, '../../..');

const candidates = [
  process.env.FM1_JS_ENGINE,
  path.join(repoRoot, '../fm1-guitar/engine/strum.js'),
].filter(Boolean);
const src = candidates.find((p) => existsSync(p));
if (!src) {
  console.error('找不到 JS 参考实现 strum.js。试过：');
  for (const c of candidates) console.error('  ' + c);
  console.error('用 FM1_JS_ENGINE=<strum.js 路径> 指定后重跑。');
  process.exit(2);
}

const STRUM = createRequire(import.meta.url)(src);

const us = (t) => Math.round(t * 1e6);
const lines = [];
const S = (id, events) => {
  lines.push(`S ${id} ${events.length}`);
  for (const e of events)
    lines.push(`E ${e.note} ${e.velocity} ${us(e.tOn)} ${us(e.tOff)} ${e.string} ${e.stroke}`);
};
const H = (id, events) => S(id, events);
const R = (id, rows) => {
  lines.push(`R ${id} ${rows.length}`);
  const sid = { down: 0, up: 1, chuck: 2 };
  for (const r of rows) lines.push(`T ${sid[r.stroke]} ${us(r.at)} ${r.palmMute ? 1 : 0}`);
};

const A = [45, 52, 57, 61, 64];                       // A 和弦
const E6 = [40, 47, 52, 56, 59, 64];                  // E6 和弦
const base = { pattern: 'down', spread: 0.025, gate: 1.2 };

// ---- S：strum 六案 ----
const s1 = STRUM.strum(A, { ...base });
S(1, s1);
S(2, STRUM.strum(A, { pattern: 'up', spread: 0.025, gate: 1.2 }));
S(3, STRUM.strum(E6, { pattern: 'chuck', spread: 0.025, gate: 1.2 }));
S(4, STRUM.strum(A, { ...base, palmMute: true, velHigh: 90, velLow: 60 }));
S(5, STRUM.strum(A, { ...base, stringBudget: true, velHigh: 80, velLow: 60 }));
S(6, STRUM.strum(E6, { pattern: { order: 'high-to-low', muted: false,
  velHigh: 100, velLow: 70, strings: 3 }, spread: 0.02, gate: 0.9 }));

// ---- H：humanize 三案（id 101–103，与 S 案隔离）----
H(101, STRUM.humanize(s1, { seed: 12345, timingMs: 6, velJitter: 10 }));
H(102, STRUM.humanize(s1, { seed: 99, timingMs: 0, velJitter: 10 }));
H(103, STRUM.humanize(
  STRUM.strum(E6, { pattern: 'chuck', spread: 0.025, gate: 1.2 }),
  { seed: 7, timingMs: 12, velJitter: 0 }));

// ---- R：节奏型两案（id 201–202）----
R(201, STRUM.expandRhythm('folk', 2.0));
R(202, STRUM.expandRhythm('reggae', 1.8));

// ---- P：单弦拨弦（和弦第 idx 根弦 + gate 1.2）----
{
  const notes = A, idx = 2, vel = 90;
  lines.push(`P ${notes[idx]} ${vel} 0 ${Math.round(1.2 * 1e6)} ${idx} 0`);
}

// ---- B：barSeed 四组 ----
for (const [b, i] of [[1, 0], [1, 1], [7, 3], [7919, 1000]])
  lines.push(`B ${b} ${i} ${STRUM.barSeed(b, i)}`);

// ---- M：mulberry32 三组（各取前 3 个输出，直接咬合位级实现）----
for (const seed of [1, 12345, 7919]) {
  const rng = STRUM.mulberry32(seed);
  const o = [rng(), rng(), rng()].map((x) => Math.round(x * 1e9));
  lines.push(`M ${seed} ${o.join(' ')}`);
}

writeFileSync(path.join(here, 'golden.tsv'), lines.join('\n') + '\n');
console.log('golden.tsv written:', lines.length, 'lines   (母本 ' + src + ')');
