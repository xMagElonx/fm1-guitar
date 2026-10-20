// run_host_test.mjs · gtr_sem 层主机对拍测试
// 用法（任意目录）：node run_host_test.mjs [clang路径] [sysroot路径]
//
// gtr_sem 是纯算法层（扫弦时序/力度/gate/scheduler），不依赖和弦库与 part 层，
// 所以只编它一个 .c。考卷是 golden.tsv，由 golden_gen.mjs 从 JS 参考实现生成。
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import { runWasmTest } from '../lib/harness.mjs';

const here = path.dirname(fileURLToPath(import.meta.url));
const repoRoot = path.resolve(here, '../../..');          // tools/host_tests/gtr_sem → 仓库根

const r = await runWasmTest({ name: 'gtr_sem', here, repoRoot, sources: ['gtr_sem.c'] });
process.exit(r.code);
