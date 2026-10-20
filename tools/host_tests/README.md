# host_tests · 三层引擎主机测试

一条命令：

```bash
node tools/host_tests/run_all.mjs
```

三层各自也能单跑（任意目录，不需要 cd）：

```bash
node tools/host_tests/gtr_sem/run_host_test.mjs
node tools/host_tests/gtr_chord/run_host_test.mjs
node tools/host_tests/gtr_part/run_host_test.mjs
```

## 测的是什么

| 层 | 编哪些 .c | 测什么 | 断言数（2026-10-20） |
|---|---|---|---|
| `gtr_sem` | `app/gtr_sem.c` | 扫弦时序/力度/gate、humanize、节奏型展开、barSeed、mulberry32 | 52 |
| `gtr_chord` | `app/gtr_chord.c` | 11 个和弦构造表、品位判定、音域约束 | 1584 |
| `gtr_part` | `app/gtr_sem.c` + `gtr_chord.c` + `gtr_part.c` | 按键分派、排程队列、Note 队列、同弦连拨 | 104 |

被测代码一律取自 `<仓库>/app/`。**测试目录里不放 .c 副本** —— 副本会跟固件悄悄分叉，
于是「测试全绿」和「刷进设备的是另一份代码」可以同时成立。

## 工具链

默认从 `<仓库>/build/tools/wasi-sdk-*` 找 wasi-sdk（构建固件时装机的那个）。
三条替代路径，任一即可：

```bash
export FM1_WASI_SDK=/path/to/wasi-sdk-27.0-x86_64-windows
node tools/host_tests/run_all.mjs <clang路径> <sysroot路径>
```

四条路都不通时**大声 SKIP 并打印怎么补**，不报 FAIL —— 刚 clone 下来的仓库
（`build/` 不入版本控制）跑全套不该一片红。

## 页面级测试不在这里

`tools/test_chords_sim.mjs` 和 `tools/test_strum_sim.mjs` 要浏览器模拟器产物，
改界面时单独跑那两个。三者加起来才是完整回归。

## goldens

`gtr_sem/golden.tsv` 是**考卷**：由 `gtr_sem/golden_gen.mjs` 从 JS 参考实现
（`fm1-guitar/engine/strum.js`，另一个仓库，不在本仓库内）生成。

```bash
node tools/host_tests/gtr_sem/golden_gen.mjs     # 重新生成考卷
node tools/host_tests/gtr_sem/run_demo.mjs       # 手印几个案子的击打事件，肉眼核对
node tools/host_tests/gtr_chord/run_demo.mjs     # 和弦 voicing 对金标指法对比表
```

⚠ 重新生成考卷不是「让测试变绿」的手段。改了 `golden_gen.mjs` 之后测试挂掉，
说明 C 和 JS 真的分叉了，先判断哪一边对，再同步另一边。
