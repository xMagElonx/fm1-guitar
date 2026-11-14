# FM-1 Open Guitar · FM-1 pocket guitar firmware

**EN** | [中文](README.zh-CN.md)

A guitar-strumming firmware layer for the [M-VAVE FM-1](https://www.m-vave.com/product.html?id=fm-1)
pocket FM synthesizer, built on top of M-VAVE's official open-source firmware
[m-vave/fm1_open](https://github.com/m-vave/fm1_open) (GPL-3.0).

The 27-key keyboard becomes a **guitar playing surface**:

- **11 black keys** = chords (F C G Dm Am Em G7 E7 D7 Bb A7), selected silently while playing
- **White keys 1–6** = single-string plucks (real guitar voicings, low → high, piano logic)
- **White key 7** = strum with automatic down/up alternation on repeated taps
- **White keys 8–10** = chuck / upstrum / palm mute
- **White keys 11–14** = one full bar of folk / pop / punk / reggae, hold to keep looping
- **White keys 15–16** = heavy down / strong up

Strumming follows the industry-standard model (RealGuitar / Ample Sound): one voice per
string, onsets spread across the pick direction, velocity decreasing along the stroke,
mutes clamp the gate.

## Try it / Download

- **Browser demo** (runs the exact firmware): <https://fm1-tone-engine.app.workbuddy.host/>
- **Firmware file**: `fm1-guitar-b1.6.fwsc` — flash over USB-C with any WebMIDI installer
  (see the demo page for the flashing guide and risk notes).

## Build

Identical to upstream: `build.bat` on Windows x64 (the JieLi PI32v2 clang 4.0.1 toolchain,
plus curl/tar). See [README-upstream.md](README-upstream.md). Note: `sdk/manifest.json`
pins the exact toolchain file hashes (clang 4.0.1 r3); if your installed JieLi toolchain
revision differs, the build tells you.

The guitar layer lives in `app/gtr_sem.{h,c}` (pure C99, no SDK includes, ported from a
JavaScript reference implementation with a 52-assertion golden test suite) and
`app/gtr_part.{h,c}` (panel wiring). Both are compiled automatically by the stock build.

## License

Same as upstream: GPL-3.0-only with the FM1 SDK linking exception — see
[LICENSE.md](LICENSE.md) and [THIRD_PARTY.md](THIRD_PARTY.md). The guitar semantic
layer added by this fork is released under GPL-3.0. Hardware and upstream firmware
copyright M-VAVE.

## Credits

- Upstream firmware: [m-vave/fm1_open](https://github.com/m-vave/fm1_open) by M-VAVE
- Community lineage: Felucca (Hugelton Instruments), SLOOP, the FM-1 modding scene
  ([FM-1 Resource Hub](https://github.com/aroum/fm1-custom-fw),
  [awesome-fm-1](https://github.com/cicloid/awesome-fm-1))

You can also try FM-1 Guitar in the browser, no flashing needed, in the free community [FM-1 Emulator](https://fm1-emulator.atst.tools/).
