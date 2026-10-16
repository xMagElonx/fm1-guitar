/* ============================================================================
   gtr_part · 吉他语义部件接线层（B1）
   ----------------------------------------------------------------------------
   上层：gtr_sem（纯算法，JS 翻译母本）。
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

   时序：tick(now_ms) 由主循环调用，队列里到期的 Note On/Off 当场下沉。
   主循环周期的抖动直接进扫弦时序 —— B1 MVP 接受；样本级精度是 B2 课题。
   ========================================================================= */
#ifndef GTR_PART_H
#define GTR_PART_H

#include <stdint.h>

void gtr_part_set_sink(void (*sink)(uint8_t command, uint8_t d1, uint8_t d2));
/* B1.2：吉他交互常驻（无模式）。扫弦作为官方直弹之上的叠加层。 */
void gtr_part_keys(uint32_t previous_keys, uint32_t current_keys,
                   uint32_t now_ms);
void gtr_part_tick(uint32_t now_ms);
int gtr_part_active(void);   /* 恒 1（常驻）；保留给宿主门控用 */
const char *gtr_part_dbg(void);  /* 诊断回执（读即清）：W<白键> S<音数>/E<错>、B<黑键> */

#endif
