// harness.mjs · 三层引擎（gtr_sem / gtr_chord / gtr_part）主机测试的公共底座
//
// 只做三件事：找工具链、编 wasm、跑 WASI。被测代码一律取 <repo>/app/ 下的
// 固件真源 —— 测试目录里不放 .c 副本，从根上消掉「测的不是刷进设备那份」。
//
// SKIP 约定：工具链不在（典型情形是刚 git clone 完，build/ 未纳入版本控制），
// 大声打印缺什么、怎么补，然后算 SKIP 而不是 FAIL —— clone 上跑全套不该一片红。
import fs from 'node:fs';
import path from 'node:path';
import { execFileSync, spawn } from 'node:child_process';
import { WASI } from 'node:wasi';

/* 找 wasi-sdk。优先级：命令行参数 > 环境变量 FM1_WASI_SDK > build/tools/wasi-sdk-*。
   返回 { home, clang, sysroot } 或 null。 */
export function findSdk(repoRoot, clangArg, sysrootArg) {
  if (clangArg && sysrootArg) return { home: null, clang: clangArg, sysroot: sysrootArg };

  const cands = [];
  if (process.env.FM1_WASI_SDK) cands.push(process.env.FM1_WASI_SDK);
  const toolsDir = path.join(repoRoot, 'build', 'tools');
  if (fs.existsSync(toolsDir)) {
    for (const name of fs.readdirSync(toolsDir)) {
      if (/^wasi-sdk/i.test(name)) cands.push(path.join(toolsDir, name));
    }
  }
  for (const home of cands) {
    const clang = path.join(home, 'bin', process.platform === 'win32' ? 'clang.exe' : 'clang');
    const sysroot = path.join(home, 'share', 'wasi-sysroot');
    if (fs.existsSync(clang) && fs.existsSync(sysroot)) return { home, clang, sysroot };
  }
  return null;
}

/* 编译 + 运行。sources 相对 <repo>/app。
   返回 { code, skipped, stderr }。 */
export async function runWasmTest(opts) {
  const { name, here, repoRoot, sources, entry = 'test_main.c' } = opts;
  const outWasm = path.join(here, 'test.wasm');

  const sdk = findSdk(repoRoot, opts.clang, opts.sysroot);
  if (!sdk) {
    console.log(`SKIP  ${name}：找不到 wasi-sdk 工具链。`);
    console.log(`      补法（任一）：`);
    console.log(`        1. 解压 wasi-sdk-27 到 <仓库>/build/tools/  下（目录名以 wasi-sdk 开头即可）`);
    console.log(`        2. export FM1_WASI_SDK=<sdk 解压目录>`);
    console.log(`        3. node run_host_test.mjs <clang路径> <sysroot路径>`);
    return { code: 0, skipped: true };
  }

  // golden.tsv 等用相对路径 fopen()，所以必须把 cwd 钉在测试目录
  process.chdir(here);

  const appDir = path.join(repoRoot, 'app');
  const args = ['--target=wasm32-wasi', '--sysroot=' + sdk.sysroot, '-O2', '-I', appDir];
  for (const s of sources) args.push(path.join(appDir, s));
  args.push(path.join(here, entry), '-o', outWasm);
  try {
    execFileSync(sdk.clang, args, { stdio: 'inherit' });
  } catch (e) {
    console.error(`编译 ${name} 失败（退出码 ${e.status}）`);
    return { code: typeof e.status === 'number' ? e.status : 1, skipped: false };
  }

  const wasi = new WASI({ version: 'preview1', preopens: { '.': here } });
  const { instance } = await WebAssembly.instantiate(fs.readFileSync(outWasm), wasi.getImportObject());
  let code = 0;
  try {
    wasi.start(instance);
  } catch (e) {
    if (e && typeof e.code === 'number') code = e.code;
    else throw e;
  }
  return { code, skipped: false };
}

/* 子进程方式跑某一层（run_all.mjs 用）。 */
export function runLayer(script) {
  return new Promise((resolve) => {
    const child = spawn(process.execPath, [script], { stdio: ['ignore', 'pipe', 'inherit'] });
    let out = '';
    child.stdout.on('data', (d) => { out += d.toString(); });
    child.on('close', (code) => resolve({ code, out }));
  });
}
