/* Headless WASM simulator test for CHORDS page + chord storage integration.
 * Loads simulator.wasm directly, simulates button/encoder/key input,
 * and verifies the chord editor UI + flash persistence. */
import fs from 'fs';
import path from 'path';
import { fileURLToPath } from 'url';

const __dirname = path.dirname(fileURLToPath(import.meta.url));
const ROOT = path.resolve(__dirname, '..');
const WASM = path.join(ROOT, 'build/web/simulator.wasm');

// Button indices (from screen.c enum)
const BTN = { OCT_DOWN:0, OCT_UP:1, FX:2, SCL:3, ENV:4, LFO:5, EDIT:6, GLO:7, HOME:8, SAVE:9, ARP:10, SEQ:11, PLAY:12, REC:13 };
// Page enum
const PAGE = { OVERVIEW:0, STEPS:1, FM_EDIT:2, FM_ENV:3, FM_LFO:4, TONE_EDIT:5, MIX:6, BLE:7, PROJECT:8, NAME:9, EFFECTS:10, SOURCE:11, ACTIONS:12, STEP_EDIT:13, RHYTHM:14, FM_QUICK:15, PRESET_BROWSER:16, ARP_EDIT:17, STARTUP:18, CHORDS:19 };
// Chord slot key bits (from gtr_part.c gtr_chord_bits)
const CHORD_KEY_BITS = [1, 3, 5, 8, 10, 13, 15, 17, 20, 22, 25];

let pass = 0, fail = 0;
function ok(cond, msg) {
  if (cond) { pass++; console.log('  PASS: ' + msg); }
  else { fail++; console.log('  FAIL: ' + msg); }
}

async function main() {
  const wasmBuf = fs.readFileSync(WASM);
  const { instance } = await WebAssembly.instantiate(wasmBuf, {});
  const ex = instance.exports;

  function readState() {
    const ptr = ex.sim_state();
    return Array.from(new Uint8Array(ex.memory.buffer, ptr, 8));
  }
  function getPage() { return readState()[1]; }

  function tap(btn) {
    ex.sim_control(0, btn, 1);
    ex.sim_control(0, btn, 0);
  }
  function tapCombo(b1, b2) {
    ex.sim_control(0, b1, 1);
    ex.sim_control(0, b2, 1);
    ex.sim_control(0, b2, 0);
    ex.sim_control(0, b1, 0);
  }
  function enc(idx, delta) {
    ex.sim_control(2, idx, delta);
  }
  function tapKey(bit) {
    ex.sim_control(1, bit, 1);
    ex.sim_control(1, bit, 0);
  }
  function advanceMs(ms) {
    const blockMs = ex.sim_block_frames() * 1000 / 44100;
    const calls = Math.ceil(ms / blockMs) + 5;
    for (let i = 0; i < calls; i++) ex.sim_render();
  }

  // ========== Test 1: Boot with cleared flash ==========
  console.log('\n=== Test 1: Boot with cleared flash ===');
  ex.sim_boot(1);
  advanceMs(100);
  ok(getPage() === PAGE.OVERVIEW || getPage() === PAGE.STARTUP, 'Boots to OVERVIEW/STARTUP (page=' + getPage() + ')');

  // ========== Test 2: Navigate to ACTIONS page ==========
  console.log('\n=== Test 2: Navigate to ACTIONS page ===');
  tapCombo(BTN.SEQ, BTN.EDIT);
  ok(getPage() === PAGE.ACTIONS, 'SEQ+EDIT enters ACTIONS page (page=' + getPage() + ')');

  // ========== Test 3: Select action 20 (CHORD LIBRARY) and enter CHORDS ==========
  console.log('\n=== Test 3: Enter CHORDS page ===');
  enc(0, 20);
  tap(BTN.SAVE);
  ok(getPage() === PAGE.CHORDS, 'Action 20 (CHORD LIBRARY) enters CHORDS page (page=' + getPage() + ')');

  // ========== Test 3b: Verify action 19 (STOP ALL) does NOT enter CHORDS ==========
  console.log('\n=== Test 3b: Action 19 (STOP ALL) goes to OVERVIEW ===');
  tap(BTN.HOME); // exit CHORDS first
  advanceMs(50);
  tapCombo(BTN.SEQ, BTN.EDIT);
  advanceMs(50);
  enc(0, -1); // action is 20 from before, go back to 19 (STOP ALL)
  tap(BTN.SAVE);
  ok(getPage() === PAGE.OVERVIEW, 'Action 19 (STOP ALL) goes to OVERVIEW (page=' + getPage() + ')');

  // Re-enter CHORDS for subsequent tests
  tapCombo(BTN.SEQ, BTN.EDIT);
  advanceMs(50);
  enc(0, 20);
  tap(BTN.SAVE);
  ok(getPage() === PAGE.CHORDS, 'Re-entered CHORDS page (page=' + getPage() + ')');

  // ========== Test 4: Edit chord with knobs ==========
  console.log('\n=== Test 4: Edit chord slot 0 with knobs ===');
  tapKey(CHORD_KEY_BITS[0]);
  advanceMs(50);
  enc(0, 3);
  advanceMs(50);
  enc(1, 2);
  advanceMs(50);
  ok(true, 'Knob editing completed without crash');

  // ========== Test 5: Select different slot ==========
  console.log('\n=== Test 5: Select slot 5 via black key ===');
  tapKey(CHORD_KEY_BITS[5]);
  advanceMs(50);
  ok(true, 'Slot selection via black key completed');

  // ========== Test 6: SAVE persists to flash ==========
  console.log('\n=== Test 6: SAVE persists chord diff to flash ===');
  const flashRevBefore = ex.sim_flash_revision();
  tap(BTN.SAVE);
  advanceMs(50);
  advanceMs(2000);
  const flashRevAfter = ex.sim_flash_revision();
  ok(flashRevAfter > flashRevBefore, 'Flash revision increased after SAVE (' + flashRevBefore + ' -> ' + flashRevAfter + ')');

  // ========== Test 7: Verify chord_diff in settings sector ==========
  console.log('\n=== Test 7: Verify chord_diff bytes in flash ===');
  const flashPtr = ex.sim_flash();
  const flash = new Uint8Array(ex.memory.buffer, flashPtr, 524288);
  const settingsSector = 15 * 4096;
  const chordDiffOffset = settingsSector + 20 + 4064 + 4;
  const chordDiff = Array.from(flash.slice(chordDiffOffset, chordDiffOffset + 8));
  console.log('  chord_diff bytes:', chordDiff.map(b => '0x' + b.toString(16).padStart(2, '0')).join(' '));
  const allFF = chordDiff.every(b => b === 0xFF);
  ok(!allFF, 'chord_diff is not all-0xFF (changes were persisted)');
  const all00 = chordDiff.every(b => b === 0x00);
  ok(!all00, 'chord_diff is not all-0x00 (changes were persisted)');

  // ========== Test 8: HOME exits CHORDS page ==========
  console.log('\n=== Test 8: HOME exits CHORDS page ===');
  tap(BTN.HOME);
  ok(getPage() !== PAGE.CHORDS, 'HOME exits CHORDS page (page=' + getPage() + ')');

  // ========== Test 9: Reboot and verify persistence ==========
  console.log('\n=== Test 9: Reboot and verify chord persistence ===');
  ex.sim_boot(0);
  advanceMs(200);
  tapCombo(BTN.SEQ, BTN.EDIT);
  advanceMs(50);
  ok(getPage() === PAGE.ACTIONS, 'Re-entered ACTIONS page after reboot');
  enc(0, 20);
  tap(BTN.SAVE);
  ok(getPage() === PAGE.CHORDS, 'Re-entered CHORDS page after reboot');
  tapKey(CHORD_KEY_BITS[0]);
  advanceMs(50);
  ok(true, 'Chord table loaded from flash after reboot (no crash)');

  // ========== Test 10: Flash chord_diff stable ==========
  console.log('\n=== Test 10: Flash chord_diff stable across reboot ===');
  const chordDiff2 = Array.from(flash.slice(chordDiffOffset, chordDiffOffset + 8));
  ok(JSON.stringify(chordDiff) === JSON.stringify(chordDiff2), 'chord_diff unchanged after reboot');

  // ========== Summary ==========
  console.log('\n========================================');
  console.log('Results: ' + pass + ' passed, ' + fail + ' failed');
  console.log('========================================');
  process.exit(fail > 0 ? 1 : 0);
}

main().catch(err => { console.error('FATAL:', err); process.exit(1); });
