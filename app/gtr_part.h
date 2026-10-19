/* ============================================================================
   gtr_part · 吉他语义部件接线层（B1）
   ----------------------------------------------------------------------------
   上层：gtr_sem（纯算法，JS 翻译母本）+ gtr_chord（和弦数据模型与编辑器）。
   下层：宿主回调（screen.c 注入的 MIDI post）。
   本层做的事：把 27 个音名键的按下/释放边沿翻译成「选和弦 / 触发扫弦」，
   把语义层的事件队列按毫秒时刻下沉为 Note On/Off。

   键位映射（B1 MVP，GTR 草案的首次落地；全部是本文件顶部的常量表，
   Workbench 晨检后如需调整只改这里）：
     · 音名键 30..40（黑键区假设）= 和弦选择，按下即选中，扫弦始终用
       最后选中的和弦；上电默认 0 号和弦。
     · 音名键 14..29（白键区假设）分三段手势：
         14..19 = 下扫    20..24 = 上扫    25..29 = 哑音扫（chuck）
     · 激活开关：ENV 按住时进入 GTR 模式（screen.c 传入），松手回官方行为。
       MWP：不做锁存，避免与官方 UI 的 ENV 页冲突。

   B1.6 和弦库编辑器（CHORDS 页）：
     · gtr_part_edit_mode(1) 进入编辑模式：黑键选槽+试听，白键停用；
     · KNOB1=根音 KNOB2=类型，ENV/LFO=修饰预览（min/7/m7）；
     · 默认槽播放金标指法（零行为变化），改过的槽播放生成 voicing；
     · gtr_part_edit_pack_diff / unpack_diff 对接 project.c 存储。

   时序：tick(now_ms) 由主循环调用，队列里到期的 Note On/Off 当场下沉。
   主循环周期的抖动直接进扫弦时序 —— B1 MVP 接受；样本级精度是 B2 课题。
   ========================================================================= */
#ifndef GTR_PART_H
#define GTR_PART_H

#include <stdint.h>
#include "gtr_sem.h"   /* B1.9：自定义节奏 API 暴露 struct gtr_rhythm，需完整类型 */

void gtr_part_set_sink(void (*sink)(uint8_t command, uint8_t d1, uint8_t d2));
/* B1.2：吉他交互常驻（无模式）。扫弦作为官方直弹之上的叠加层。 */
void gtr_part_keys(uint32_t previous_keys, uint32_t current_keys,
                   uint32_t now_ms);
void gtr_part_tick(uint32_t now_ms);
int gtr_part_active(void);   /* 恒 1（常驻）；保留给宿主门控用 */
const char *gtr_part_dbg(void);  /* 诊断回执（读即清）：W<白键> S<音数>/E<错>、B<黑键> */

/* ---- B1.6 和弦库编辑器 API（CHORDS 页用）---- */
void gtr_part_edit_init(void);                    /* 初始化表+编辑器（上电调一次） */
void gtr_part_edit_mode(int on);                  /* 1=编辑模式 0=演奏模式 */
int  gtr_part_edit_knob(int knob, int delta);     /* knob: 1=root 2=type; 返回 1=有改动 */
int  gtr_part_edit_modifier(int env, int lfo);    /* ENV/LFO 修饰键状态 */
int  gtr_part_edit_reset_slot(void);              /* 选中槽恢复默认; 返回 1=有改动 */
int  gtr_part_edit_is_dirty(void);                /* 有未落盘修改 → 1 */
void gtr_part_edit_saved(void);                   /* 固件落盘后调用，清脏 */
int  gtr_part_edit_pack_diff(uint8_t *buf, int cap);   /* 打包 8 字节差分 */
int  gtr_part_edit_unpack_diff(const uint8_t *buf, int len); /* 解包 8 字节差分 */

/* 显示查询 */
int  gtr_part_edit_selected(void);                /* 当前选中槽 0-10 */
int  gtr_part_edit_slot_root(int slot);           /* 槽的 root 值 0-11; 负=错误 */
int  gtr_part_edit_slot_type(int slot);           /* 槽的 type 值 0-8; 负=错误 */
int  gtr_part_edit_slot_name(int slot, char *buf, int cap); /* 槽名 "F" / "Cm" 等 */
int  gtr_part_edit_slot_is_default(int slot);     /* 1=默认 0=改过 负=错误 */

/* ---- B1.9 扫弦节奏编辑器 M1：自定义模式数据通路 ----
   节奏型 id 由 gtr_sem 定义（GTR_RHYTHM_FOLK..BALLAD）；
   GTR_RHYTHM_USER 走 gtr_expand_rhythm 的 custom 分支，
   模式由 gtr_part_set_user_rhythm() 存入本层。 */
#define GTR_RHYTHM_USER (-1)

int  gtr_part_set_user_rhythm(const struct gtr_rhythm *r);
/* 校验并存入自定义模式；返回 GTR_OK 表示接受，否则为 gtr_sem 的错误码。
   校验用的是真正的展开器，所以"存进去一定能放出来"。 */
int  gtr_part_user_rhythm_steps(void);
/* 已存模式的步数；0 = 还没存过。 */
int  gtr_part_trigger_rhythm(int rhythm_id);
/* 立即起一小节（等效于按住节奏型键的第一拍）；GTR_OK = 已排程。
   目前只用于测试与编辑器试听：下一次 gtr_part_keys() 会按实际键态重算按住状态。 */
void gtr_part_stop_rhythm(void);
/* 停止续排（已排进队列的击仍会播完，不打断当前小节）。 */
int  gtr_part_dropped_strokes(void);
/* 诊断：排程时队列满而丢弃的击数，正常应恒为 0。 */

#endif
