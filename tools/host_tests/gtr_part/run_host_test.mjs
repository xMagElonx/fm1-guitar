// run_host_test.mjs · gtr_part 层主机测试
// 用法（任意目录）：node run_host_test.mjs [clang路径] [sysroot路径]
//
// gtr_part 是「按键 → 事件 → Note 队列」的中间层，也是整个固件里唯一带状态的
// 一层（gtr_held / gtr_sq / gtr_nq）。它依赖 gtr_sem 与 gtr_chord，所以三个 .c
// 一起编。测试自带虚拟时钟：每 advance(1ms) 就 tick 一次，断言可以精确到毫秒。
//
// 这一层是 2026-10-20 才第一次有测试覆盖的（此前只被「编进固件、上真机听」验证）。
// 上一次补覆盖就立刻揪出一个回归：同弦连拨第三次不出声。
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import { runWasmTest } from '../lib/harness.mjs';

const here = path.dirname(fileURLToPath(import.meta.url));
const repoRoot = path.resolve(here, '../../..');

const r = await runWasmTest({
  name: 'gtr_part', here, repoRoot,
  sources: ['gtr_sem.c', 'gtr_chord.c', 'gtr_part.c'],
});
process.exit(r.code);
