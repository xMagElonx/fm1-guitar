// run_all.mjs · 一条命令跑完三层引擎测试
// 用法（任意目录）：node run_all.mjs [clang路径] [sysroot路径]
//
// 另外两套页面级测试（tools/test_chords_sim.mjs、tools/test_strum_sim.mjs）
// 需要浏览器模拟器产物，不在这里跑；改界面时单独跑那两个。
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import { runLayer } from './lib/harness.mjs';

const here = path.dirname(fileURLToPath(import.meta.url));

const layers = [
  ['gtr_sem',   'gtr_sem/run_host_test.mjs'],
  ['gtr_chord', 'gtr_chord/run_host_test.mjs'],
  ['gtr_part',  'gtr_part/run_host_test.mjs'],
];

let failed = 0, skipped = 0, passed = 0;
const rows = [];

for (const [name, rel] of layers) {
  const script = path.join(here, rel);
  const { code, out } = await runLayer(script, process.argv.slice(2));
  const summary = out.split('\n').filter((l) => /结论|项检查|SKIP/.test(l)).pop() || '';
  if (code !== 0) {
    failed++;
    rows.push([name, 'FAIL', summary]);
  } else if (/SKIP/.test(summary)) {
    skipped++;
    /* 原文已在上面整段打回，汇总行只留一句短标题，避免重复三大段 */
    rows.push([name, 'SKIP', '缺少 wasi-sdk 工具链（补法见上）']);
  } else {
    passed++;
    rows.push([name, 'PASS', summary]);
  }
  /* SKIP/Fail 的子进程 stdout 是被 pipe 收走的，用户看不见。这里把原文打回来 ——
     SKIP 的价值就在「缺什么、怎么补」那几行，吞掉它等于把指南藏起来。 */
  if (code !== 0 || /SKIP/.test(summary)) {
    console.log(out.trimEnd());
    console.log('');
  }
}

console.log('');
console.log('================ 引擎测试汇总 ================');
for (const [name, verdict, summary] of rows) {
  console.log(`  ${verdict.padEnd(4)} ${name.padEnd(10)} ${summary.trim()}`);
}
console.log('==============================================');
console.log(`  ${passed} 层通过，${skipped} 层跳过，${failed} 层失败`);
if (failed > 0) process.exit(1);
