// run_demo.mjs · 编译并运行 demo.c（生成 voicing 与金标指法对比表）
// 用法（任意目录）：node run_demo.mjs
import path from 'node:path';
import fs from 'node:fs';
import { fileURLToPath } from 'node:url';
import { execFileSync } from 'node:child_process';
import { WASI } from 'node:wasi';
import { findSdk } from '../lib/harness.mjs';

const here = path.dirname(fileURLToPath(import.meta.url));
const repoRoot = path.resolve(here, '../../..');

const sdk = findSdk(repoRoot, process.argv[2], process.argv[3]);
if (!sdk) {
  console.error('SKIP 找不到 wasi-sdk 工具链：解压到 <仓库>/build/tools/ 或设 FM1_WASI_SDK');
  process.exit(0);
}

const outWasm = path.join(here, 'demo.wasm');
const appDir = path.join(repoRoot, 'app');
execFileSync(sdk.clang,
  ['--target=wasm32-wasi', '--sysroot=' + sdk.sysroot, '-O2',
   '-I', appDir,
   path.join(appDir, 'gtr_chord.c'), path.join(here, 'demo.c'), '-o', outWasm],
  { stdio: 'inherit' });

const wasi = new WASI({ version: 'preview1', preopens: { '.': here } });
const { instance } = await WebAssembly.instantiate(fs.readFileSync(outWasm), wasi.getImportObject());
try { wasi.start(instance); }
catch (e) { if (e && typeof e.code === 'number' && e.code !== 0) throw e; }
