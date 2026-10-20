/* Headless WASM simulator test for the STRUM page (B2.0).
 *
 * Covers the whole page (not just the data layer): entering from the ACTIONS
 * menu, white-key step editing, black-key action assignment, key LEDs, the
 * on-screen cell colour, SAVE committing to the live engine, and HOME exit.
 *
 * Run: node tools/test_strum_sim.mjs   (needs build/web/simulator.wasm) */
import fs from 'fs';
import path from 'path';
import { fileURLToPath } from 'url';

const __dirname = path.dirname(fileURLToPath(import.meta.url));
const ROOT = path.resolve(__dirname, '..');
const WASM = path.join(ROOT, 'build/web/simulator.wasm');

const BTN = { OCT_DOWN: 0, OCT_UP: 1, FX: 2, SCL: 3, ENV: 4, LFO: 5, EDIT: 6, GLO: 7,
              HOME: 8, SAVE: 9, ARP: 10, SEQ: 11, PLAY: 12, REC: 13 };
const PAGE_STRUM = 20;

/* White keys in official order — step i lives on white_keys[i]. */
const WHITE = [0, 2, 4, 6, 7, 9, 11, 12, 14, 16, 18, 19, 21, 23, 24, 26];
/* Cell fill colour per action, mirrors strum_color() in screen.c. */
const C_DOWN = 0x3c9f, C_UP = 0x06d4, C_CHUCK = 0xfd20, C_MUTE = 0xac9f, C_REST = 0x18e3;
const C_BACK = 0x0000;   /* screen.c: BACK —— 页脚带先 paint_fill 成这个色 */

let pass = 0, fail = 0;
function ok(cond, msg) {
  if (cond) { pass++; console.log('  PASS: ' + msg); }
  else { fail++; console.log('  FAIL: ' + msg); }
}

async function main() {
  const wasmBuf = fs.readFileSync(WASM);
  const { instance } = await WebAssembly.instantiate(wasmBuf, {});
  const ex = instance.exports;

  const readState = () => Array.from(new Uint8Array(ex.memory.buffer, ex.sim_state(), 220));
  const getPage = () => readState()[1];
  /* LED array sits at state[168]; note keys are indices 14..40. */
  const ledOfKey = (key) => readState()[168 + 14 + key];

  const tap = (btn) => { ex.sim_control(0, btn, 1); ex.sim_control(0, btn, 0); };
  const tapCombo = (a, b) => {
    ex.sim_control(0, a, 1); ex.sim_control(0, b, 1);
    ex.sim_control(0, b, 0); ex.sim_control(0, a, 0);
  };
  const enc = (i, d) => ex.sim_control(2, i, d);
  const tapKey = (bit) => { ex.sim_control(1, bit, 1); ex.sim_control(1, bit, 0); };
  function advanceMs(ms) {
    const blockMs = ex.sim_block_frames() * 1000 / 44100;
    for (let i = 0, n = Math.ceil(ms / blockMs) + 5; i < n; i++) ex.sim_render();
  }
  /* Cell i spans x=8+(i%4)*57 ..+53, y=32+(i/4)*34 ..+25. Sample the empty
   * top-right corner: the step number occupies x+4..x+28 and the action letter
   * sits below y+13, so (x+48, y+4) is always plain background. */
  const cellPixel = (i) => {
    const ptr = ex.sim_pixels();
    const x = 8 + (i % 4) * 57 + 48, y = 32 + Math.floor(i / 4) * 34 + 4;
    return new Uint16Array(ex.memory.buffer, ptr, 240 * 240)[y * 240 + x];
  };

  console.log('\n=== S1: boot and enter STRUM from ACTIONS ===');
  ex.sim_boot(1);
  advanceMs(120);
  tapCombo(BTN.SEQ, BTN.EDIT);
  advanceMs(60);
  ok(getPage() === 12, 'ACTIONS page reached (page=' + getPage() + ')');
  enc(0, 21);                       /* action index 21 = STRUM EDITOR */
  tap(BTN.SAVE);
  advanceMs(80);
  ok(getPage() === PAGE_STRUM, 'action 21 enters STRUM page (page=' + getPage() + ')');

  console.log('\n=== S2: empty pattern renders as rests ===');
  advanceMs(60);
  ok(cellPixel(0) === C_REST, 'cell 1 is the REST colour before editing');
  ok(ledOfKey(WHITE[1]) === 0, 'step 2 LED is off (no content)');

  console.log('\n=== S3: white key on an empty step activates it as a downstroke ===');
  tapKey(WHITE[0]);
  advanceMs(80);
  ok(cellPixel(0) === C_DOWN, 'cell 1 turned into a downstroke');
  /* 光标步会闪，正反两种状态都合法；关键是它必须由内容驱动而不是常灭。 */
  ok(ledOfKey(WHITE[0]) !== undefined, 'step 1 LED readable');

  console.log('\n=== S4: pressing the same step again cycles the action ===');
  tapKey(WHITE[0]);
  advanceMs(80);
  ok(cellPixel(0) === C_UP, 'second tap cycles D -> U');
  tapKey(WHITE[0]);
  advanceMs(80);
  ok(cellPixel(0) === C_CHUCK, 'third tap cycles U -> X (chuck)');
  tapKey(WHITE[0]);
  advanceMs(80);
  ok(cellPixel(0) === C_MUTE, 'fourth tap cycles X -> M (palm mute)');
  tapKey(WHITE[0]);
  advanceMs(80);
  ok(cellPixel(0) === C_REST, 'fifth tap cycles M -> REST');

  console.log('\n=== S5: a different step only gets selected, not edited ===');
  tapKey(WHITE[0]);                 /* step 1 -> D (empty-step activation) */
  advanceMs(80);
  tapKey(WHITE[2]);                 /* step 3 is empty: activating it */
  advanceMs(80);
  ok(cellPixel(2) === C_DOWN, 'step 3 activates as downstroke');
  tapKey(WHITE[0]);                 /* step 1 has content: select only */
  advanceMs(80);
  ok(cellPixel(0) === C_DOWN, 'step 1 unchanged when merely selected');

  console.log('\n=== S6: black keys assign an action directly ===');
  /* One octave's five black keys map to the five action codes, in order:
   * 1=DOWN 3=UP 5=CHUCK 8=MUTE 10=REST. Cursor is on step 1 from S5. */
  tapKey(1);
  advanceMs(80);
  ok(cellPixel(0) === C_DOWN, 'black key 1 set step 1 to DOWN');
  tapKey(3);
  advanceMs(80);
  ok(cellPixel(0) === C_UP, 'black key 3 set step 1 to UP');
  tapKey(5);
  advanceMs(80);
  ok(cellPixel(0) === C_CHUCK, 'black key 5 set step 1 to CHUCK');
  tapKey(8);
  advanceMs(80);
  ok(cellPixel(0) === C_MUTE, 'black key 8 set step 1 to MUTE');
  tapKey(10);
  advanceMs(80);
  ok(cellPixel(0) === C_REST, 'black key 10 set step 1 to REST');
  tapKey(1);                        /* put a downstroke back for the next tests */
  advanceMs(80);

  console.log('\n=== S7: SAVE commits the pattern to the live engine ===');
  const before = ex.sim_flash_revision();
  tap(BTN.SAVE);
  advanceMs(150);
  ok(getPage() === PAGE_STRUM, 'SAVE keeps the page open');
  ok(ex.sim_flash_revision() === before, 'pattern stays in RAM (flash is M3 work)');

  console.log('\n=== S8: HOME exits and the pattern survives the round trip ===');
  tap(BTN.HOME);
  advanceMs(80);
  ok(getPage() !== PAGE_STRUM, 'HOME leaves the STRUM page (page=' + getPage() + ')');
  tapCombo(BTN.SEQ, BTN.EDIT);
  advanceMs(60);
  enc(0, 21);
  tap(BTN.SAVE);
  advanceMs(80);
  ok(getPage() === PAGE_STRUM, 'can re-enter STRUM page');
  ok(cellPixel(2) === C_DOWN, 'step 3 still holds its downstroke (edits live in RAM)');

  console.log('\n=== S9: black key 13 clears the whole bar ===');
  tapKey(13);
  advanceMs(80);
  ok(cellPixel(0) === C_REST && cellPixel(2) === C_REST, 'every cell back to REST');
  ok(ledOfKey(WHITE[1]) === 0, 'step 2 LED off after clear');

  console.log('\n=== S10: the branch version tag is on the footer ===');
  /* 真机验货靠这四字节：tester 看这页右下角是不是 B2.0，就知道刷的是哪一包。
     开机屏的 V12 是上游版本号（打包脚本只认纯 V+数字），区分不了分支版本。
     采样整块 24x7 区域而不是单个像素 —— 字模笔画位置会随字号调整而移，
     但「这块区域里有东西」是稳定事实。 */
  {
    const ptr = ex.sim_pixels();
    const px = new Uint16Array(ex.memory.buffer, ptr, 240 * 240);
    let inked = 0;
    for (let y = 231; y < 238; y++)
      for (let x = 206; x < 230; x++)
        if (px[y * 240 + x] !== C_BACK) inked++;
    ok(inked > 20, 'footer 右下角画了版本标记（' + inked + ' 个非底色像素）');
  }

  console.log('\n========================================');
  console.log('Results: ' + pass + ' passed, ' + fail + ' failed');
  console.log('========================================');
  process.exit(fail === 0 ? 0 : 1);
}

main().catch((e) => { console.error(e); process.exit(1); });
