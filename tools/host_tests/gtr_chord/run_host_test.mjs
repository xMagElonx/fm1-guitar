// run_host_test.mjs · gtr_chord 层主机测试
// 用法（任意目录）：node run_host_test.mjs [clang路径] [sysroot路径]
//
// gtr_chord 只依赖自己的头文件（和弦构造表 + 指法/品位判定），
// 不需要 gtr_sem，所以只编它一个 .c。
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import { runWasmTest } from '../lib/harness.mjs';

const here = path.dirname(fileURLToPath(import.meta.url));
const repoRoot = path.resolve(here, '../../..');

const r = await runWasmTest({ name: 'gtr_chord', here, repoRoot, sources: ['gtr_chord.c'] });
process.exit(r.code);
