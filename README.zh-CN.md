# FM-1 Open Guitar · FM-1 口袋吉他固件

[EN](README.md) | **中文**

在 M-VAVE 官方开源固件 [m-vave/fm1_open](https://github.com/m-vave/fm1_open)（GPL-3.0）
基础上，为 [M-VAVE FM-1](https://www.m-vave.com/product.html?id=fm-1) 口袋 FM 合成器
增加的**吉他演奏层**。

27 键键盘变成**吉他演奏面板**：

- **11 根黑键** = 和弦（F C G Dm Am Em G7 E7 D7 Bb A7），演奏中静默切换不打断音乐
- **白键 1–6** = 单弦拨弦（真实吉他把位，左低右高）
- **白键 7** = 扫弦，连点自动下扫/上扫交替
- **白键 8–10** = 哑音扫 / 上扫 / 闷音下扫
- **白键 11–14** = 一键一整小节节奏型（folk / pop / punk / reggae），按住持续循环
- **白键 15–16** = 重下扫 / 强上扫

## 试玩 / 下载

- **浏览器试玩**（运行的就是固件本身）：<https://fm1-tone-engine.app.workbuddy.host/>
- 固件文件 `fm1-guitar-b1.6.fwsc`，USB-C 浏览器刷入（见试玩页的刷机指南与风险说明）。

## 构建 / 许可

与上游一致：Windows x64 + `build.bat`（杰理 PI32v2 clang 工具链）。吉他层在
`app/gtr_sem.{h,c}` 与 `app/gtr_part.{h,c}`，随官方构建自动编译。许可同上游
（GPL-3.0-only + SDK 链接例外），详见 [LICENSE.md](LICENSE.md) 与 [THIRD_PARTY.md](THIRD_PARTY.md)。

上游原始说明见 [README-upstream.md](README-upstream.md) / [README-upstream.zh-CN.md](README-upstream.zh-CN.md)。
