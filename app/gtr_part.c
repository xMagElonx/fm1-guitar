/* ============================================================================
   gtr_part.c · 吉他语义部件（B1.2：无模式常驻 + Ample 式白键面板）
   ----------------------------------------------------------------------------
   交互（用户拍板：不要模式开关，吉他交互常驻）：
     · 黑键 11 个 = 静默选和弦（演奏中切换不打断音乐）；
     · 白键 16 个 = Ample 式扫弦面板（官方 white_keys[16] 权威顺序）：
         W1..W6   单弦拨弦（和弦 6→1 弦各一键）
         W7       下扫（连点自动下上交替 —— Ample 招牌机制；
                  换和弦或停 2 秒后重置回下扫）
         W8       哑音下扫（chuck）
         W9       上扫
         W10      闷音下扫（palm mute）
         W11..W14 节奏型键：folk / pop / punk / reggae（按一下弹一小节）
         W15/W16  重下扫 / 强上扫
   · 官方单键直弹在这版固件里让位（换回官方固件即恢复）。
   静态内存、无动态分配、符号 gtr_part_* 前缀。算法全部来自 gtr_sem。
   ========================================================================= */
#include "gtr_part.h"
#include "gtr_sem.h"

#include <string.h>
#include <stdio.h>

#define GTR_CHORD_N   11
static const uint32_t gtr_chord_bits[GTR_CHORD_N] = {
    1u << 1,  1u << 3,  1u << 5,  1u << 8,  1u << 10,
    1u << 13, 1u << 15, 1u << 17, 1u << 20, 1u << 22, 1u << 25
};
/* 6 弦真实吉他把位（低→高），标准调弦 E2=40 A2=45 D3=50 G3=55 B3=59 E4=64 */
static const int gtr_chords[GTR_CHORD_N][6] = {
    { 41, 48, 53, 57, 60, 65 },   /* 0  F   大横按 1 品 */
    { 48, 55, 60, 64, 67, 72 },   /* 1  C   大横按 8 品 */
    { 43, 47, 50, 55, 59, 67 },   /* 2  G   开放 320003 */
    { 50, 57, 62, 65, 69, 74 },   /* 3  Dm  大横按 5 品 */
    { 45, 52, 57, 60, 64, 69 },   /* 4  Am  开放 x02210 加高音 */
    { 40, 47, 52, 55, 59, 64 },   /* 5  Em  开放 022000 */
    { 43, 47, 50, 55, 59, 65 },   /* 6  G7  开放 320001 */
    { 40, 47, 50, 56, 59, 64 },   /* 7  E7  开放 020100 */
    { 50, 57, 60, 66, 69, 74 },   /* 8  D7  带高音双 D */
    { 46, 53, 58, 62, 65, 70 },   /* 9  Bb  大横按 6 品 */
    { 45, 52, 55, 61, 64, 69 },   /* 10 A7  开放 x02020 加高音 */
};
#define GTR_CHORD_NOTES 6
#define GTR_SEQ_MASK   ((1u << 18) | (1u << 19) | (1u << 21) | (1u << 23))   /* W11-14 节奏型键 */

/* ---- 白键功能表（按官方 white_keys[16] 顺序）---- */
enum {
    WK_PLUCK6, WK_PLUCK5, WK_PLUCK4, WK_PLUCK3, WK_PLUCK2, WK_PLUCK1,
    WK_DOWN_ALT, WK_DOWN_MUTE, WK_UP, WK_PALM,
    WK_SEQ_FOLK, WK_SEQ_POP, WK_SEQ_PUNK, WK_SEQ_REGGAE,
    WK_HEAVY, WK_UP_STRONG, WK_W_N
};
/* white bit → 功能 */
static const uint8_t gtr_white_type[16] = {
    WK_PLUCK6, WK_PLUCK5, WK_PLUCK4, WK_PLUCK3, WK_PLUCK2, WK_PLUCK1,
    WK_DOWN_ALT, WK_DOWN_MUTE, WK_UP, WK_PALM,
    WK_SEQ_FOLK, WK_SEQ_POP, WK_SEQ_PUNK, WK_SEQ_REGGAE,
    WK_HEAVY, WK_UP_STRONG
};
static const uint8_t gtr_white_bit[16] = {
    0, 2, 4, 6, 7, 9, 11, 12, 14, 16, 18, 19, 21, 23, 24, 26
};

/* ---- 状态 ---- */
static void (*gtr_sink)(uint8_t, uint8_t, uint8_t);
static int gtr_chord;                   /* 当前和弦 */
static int gtr_alt_up;                  /* W7 连点交替标记 */
static int gtr_alt_chord;               /* 交替状态隶属的和弦 */
static uint32_t gtr_last_stroke_ms;     /* 交替重置计时 */
static int gtr_seeded;
static uint32_t gtr_prev_keys;
static uint32_t gtr_now_ms;
static int gtr_seq_rhythm = -1;        /* 按住中的节奏型（-1 = 无） */
static int gtr_seq_held;               /* 节奏型键是否仍按住 */
static uint32_t gtr_bar_end_ms;        /* 当前小节的结束时刻（续排的节拍网格） */
static uint32_t gtr_bar_len_ms = 2000; /* 小节时长（barSec 2.0s） */
static char gtr_dbg[20];               /* 诊断回执（LCD 显示，宿主读取） */
static int gtr_dbg_len;

static void dbg_put(char c)            /* 越界保护：写满即丢，绝不越过缓冲区
                                          （P1 修复：gtr_part_dbg 无人调用时
                                          gtr_dbg_len 无限增长，写穿静态区，
                                          最终 wasm 越界硬崩 / 真机内存损坏） */
{
    if (gtr_dbg_len < (int)sizeof(gtr_dbg) - 1)
        gtr_dbg[gtr_dbg_len++] = c;
}

static void dbg_num(int v)             /* 写一个 0-99 的十进制数 */
{
    if (v < 0) { dbg_put('-'); v = -v; }
    if (v >= 10) dbg_put((char)('0' + v / 10));
    dbg_put((char)('0' + v % 10));
}

const char *gtr_part_dbg(void)
{
    if (gtr_dbg_len > (int)sizeof(gtr_dbg) - 1)
        gtr_dbg_len = (int)sizeof(gtr_dbg) - 1;
    gtr_dbg[gtr_dbg_len] = 0;
    gtr_dbg_len = 0;                   /* 读即清 */
    gtr_dbg_len = 0;
    return gtr_dbg;
}

/* ---- Note 队列 ---- */
static struct gtr_note_q {
    int used;
    uint32_t at_ms;
    uint8_t on, note, velocity;
} gtr_nq[64];

/* ---- 延迟扫弦队列（节奏型键的一小节排程）---- */
static struct gtr_stroke_q {
    int used;
    uint32_t at_ms;
    int stroke;
} gtr_sq[16];

static const int *gtr_current_chord(int *n)
{
    *n = GTR_CHORD_NOTES;
    return gtr_chords[gtr_chord];
}

static void gtr_queue_note(uint32_t at, int on, int note, int vel)
{
    int slot;
    for (slot = 0; slot < 64; slot++) {
        if (gtr_nq[slot].used) continue;
        gtr_nq[slot].used = 1;
        gtr_nq[slot].at_ms = at;
        gtr_nq[slot].on = on ? 1 : 0;
        gtr_nq[slot].note = (uint8_t)note;
        gtr_nq[slot].velocity = (uint8_t)vel;
        return;
    }                                  /* 队列满：丢弃（MVP） */
}

/* 每根弦当前鸣响的音高（0 = 无）。合成器对仍在发声的音高不响应重复
   note-on（B1.6.2 实测），扫弦/拨弦前必须先补 off 才能重新触发。 */
static uint8_t gtr_held[GTR_CHORD_NOTES];

static void gtr_retrigger_prepare(void)
{
    int slot, j;
    /* 1) 取消上一扫尚未下发的 gate off —— 否则它会落到本扫的 on 之后，
          把本扫刚按响的音拦腰截断（punk 250ms 间隔 < gate 400ms 必现） */
    for (slot = 0; slot < 64; slot++) {
        if (!gtr_nq[slot].used || gtr_nq[slot].on) continue;
        for (j = 0; j < GTR_CHORD_NOTES; j++)
            if (gtr_held[j] && gtr_nq[slot].note == gtr_held[j]) {
                gtr_nq[slot].used = 0;
                break;
            }
    }
    /* 2) 立即 off 上一扫仍在鸣响的音（先 off 后 on，合成器才会重新出音头） */
    for (j = 0; j < GTR_CHORD_NOTES; j++)
        if (gtr_held[j]) gtr_queue_note(gtr_now_ms, 0, gtr_held[j], 0);
}

static void gtr_emit_v(int stroke_id, int vel_high, int vel_low)
{
    const int *chord; int n = 0, i;
    struct gtr_strum_opts opts;
    struct gtr_event ev[GTR_MAX_EVENTS];
    int len = 0;

    chord = gtr_current_chord(&n);
    memset(&opts, 0, sizeof opts);
    opts.pattern_id = stroke_id;
    opts.spread = 0.065;   /* 65ms：可闻的扫弦感 */
    opts.gate = 0.4;       /* 0.4s：单扫的余音长度（循环扫弦由重触发接管） */
    opts.velHigh = vel_high;
    opts.velLow = vel_low;
    if (gtr_strum(chord, n, &opts, ev, GTR_MAX_EVENTS, &len) != GTR_OK) {
        dbg_put('E');
        dbg_num(-GTR_E_INTERNAL);        /* 粗报：内部失败 */
        return;
    }
    dbg_put('S');
    dbg_num(len);
    gtr_retrigger_prepare();
    for (i = 0; i < len; i++) {
        gtr_queue_note(gtr_now_ms + (uint32_t)(ev[i].tOn * 1000.0), 1,
                       ev[i].note, ev[i].velocity);
        gtr_queue_note(gtr_now_ms + (uint32_t)(ev[i].tOff * 1000.0), 0,
                       ev[i].note, 0);
    }
    memset(gtr_held, 0, sizeof gtr_held);
    for (i = 0; i < len; i++)
        gtr_held[ev[i].string % GTR_CHORD_NOTES] = (uint8_t)ev[i].note;
}

static void gtr_emit(int stroke_id)
{
    gtr_emit_v(stroke_id, 127, 100);   /* 成品力度：音量拉满，不会被淹没 */
}

int gtr_part_latch(void) { return 1; }          /* 常驻：兼容旧查询 */
int gtr_part_active(void) { return 1; }

void gtr_part_set_sink(void (*sink)(uint8_t, uint8_t, uint8_t))
{
    gtr_sink = sink;
}

static void gtr_schedule_bar(int rhythm_id);   /* 前置声明：白键 switch 调用 */

void gtr_part_keys(uint32_t previous_keys, uint32_t current_keys,
                   uint32_t now_ms)
{
    uint32_t rose = current_keys & ~previous_keys;
    int w, chord_changed = (gtr_chord != gtr_alt_chord);

    gtr_now_ms = now_ms;
    if (!gtr_seeded) {
        gtr_prev_keys = current_keys;
        gtr_alt_chord = gtr_chord;
        gtr_seeded = 1;
        return;
    }
    /* 交替状态重置：换和弦或停 2 秒 */
    if (chord_changed || (uint32_t)(now_ms - gtr_last_stroke_ms) > 2000u)
        gtr_alt_up = 0;

    /* 黑键 = 静默选和弦 */
    for (w = 0; w < GTR_CHORD_N; w++) {
        if (rose & gtr_chord_bits[w]) {
            gtr_chord = w;                 /* 静默选和弦：演奏中切换不打断音乐 */
            gtr_alt_chord = w;
            gtr_alt_up = 0;
        }
    }

    /* 白键 = Ample 式扫弦面板 */
    for (w = 0; w < 16; w++) {
        uint32_t mask = 1u << gtr_white_bit[w];
        if (!(rose & mask)) continue;
        dbg_put('W');
        dbg_num(w);
        dbg_put(' ');
        switch (gtr_white_type[w]) {
        case WK_PLUCK6: case WK_PLUCK5: case WK_PLUCK4:
        case WK_PLUCK3: case WK_PLUCK2: case WK_PLUCK1: {
            const int *chord; int cn = 0;
            int idx = gtr_white_type[w];   /* 左→右 = 低→高（钢琴逻辑） */
            int vel = 120;
            struct gtr_event pe;
            chord = gtr_current_chord(&cn);
            if (gtr_pluck(chord, cn, idx, vel, &pe) == GTR_OK) {
                if (gtr_held[idx]) {       /* 同弦连拨：先 off 才能重触发 */
                    int slot2;
                    for (slot2 = 0; slot2 < 64; slot2++)
                        if (gtr_nq[slot2].used && !gtr_nq[slot2].on &&
                            gtr_nq[slot2].note == gtr_held[idx]) {
                            gtr_nq[slot2].used = 0;
                            break;
                        }
                    gtr_queue_note(gtr_now_ms, 0, gtr_held[idx], 0);
                }
                gtr_queue_note(gtr_now_ms, 1, pe.note, pe.velocity);
                gtr_queue_note(gtr_now_ms + 500u, 0, pe.note, 0);
                gtr_held[idx] = (uint8_t)pe.note;
            }
            break;
        }
        case WK_DOWN_ALT:
            gtr_emit_v(gtr_alt_up ? GTR_STROKE_UP : GTR_STROKE_DOWN, 0, 0);
            gtr_alt_up = !gtr_alt_up;
            gtr_last_stroke_ms = now_ms;
            break;
        case WK_DOWN_MUTE: gtr_emit(GTR_STROKE_CHUCK); gtr_last_stroke_ms = now_ms; break;
        case WK_UP:        gtr_emit(GTR_STROKE_UP);    gtr_last_stroke_ms = now_ms; break;
        case WK_PALM:      gtr_emit_v(GTR_STROKE_DOWN, 80, 55); gtr_last_stroke_ms = now_ms; break;
        case WK_SEQ_FOLK:   gtr_seq_rhythm = GTR_RHYTHM_FOLK;   break;
        case WK_SEQ_POP:    gtr_seq_rhythm = GTR_RHYTHM_POP;    break;
        case WK_SEQ_PUNK:   gtr_seq_rhythm = GTR_RHYTHM_PUNK;   break;
        case WK_SEQ_REGGAE: gtr_seq_rhythm = GTR_RHYTHM_REGGAE; break;
        case WK_HEAVY:     gtr_emit_v(GTR_STROKE_DOWN, 127, 100); gtr_last_stroke_ms = now_ms; break;
        case WK_UP_STRONG: gtr_emit_v(GTR_STROKE_UP, 112, 80);  gtr_last_stroke_ms = now_ms; break;
        default: break;
        }
    }
    /* 节奏型键：按下立即起一小节；按住持续循环（tick 续排），松手当前小节收尾 */
    gtr_seq_held = (current_keys & GTR_SEQ_MASK) != 0;
    if (gtr_seq_rhythm >= 0 && (rose & GTR_SEQ_MASK) && !gtr_seq_held)
        gtr_seq_rhythm = -1;
    gtr_prev_keys = current_keys;
}

/* 节奏型键：排程一小节的扫弦（延迟队列，tick 到点逐刷下发） */
static void gtr_schedule_bar(int rhythm_id)
{
    struct gtr_stroke_sched sched[GTR_MAX_STEPS];
    int len = 0, i, slot;
    int erc = gtr_expand_rhythm(rhythm_id, NULL, 2.0, sched, GTR_MAX_STEPS, &len);
    if (erc != GTR_OK)
        return;
    for (i = 0; i < len; i++) {
        for (slot = 0; slot < 16; slot++) {
            if (gtr_sq[slot].used) continue;
            gtr_sq[slot].used = 1;
            gtr_sq[slot].at_ms = gtr_now_ms + (uint32_t)(sched[i].at * 1000.0);
            gtr_sq[slot].stroke = sched[i].stroke;
            break;
        }
    }
    gtr_bar_end_ms = gtr_now_ms + gtr_bar_len_ms;   /* 网格：按小节时长续排 */
}

void gtr_part_tick(uint32_t now_ms)
{
    int slot;
    gtr_now_ms = now_ms;       /* 修复：时钟每次 tick 刷新（原冻结在最后按键时刻，
                                  导致节奏型键的整小节排程沉没） */
    if (!gtr_sink) return;
    /* 到期的延迟扫弦 → 立即展开成音符入队 */
    for (slot = 0; slot < 16; slot++) {
        if (!gtr_sq[slot].used) continue;
        if ((int32_t)(now_ms - gtr_sq[slot].at_ms) < 0) continue;
        gtr_emit(gtr_sq[slot].stroke);
        gtr_sq[slot].used = 0;
    }
    /* 按住节奏型键 → 当前小节弹完自动续下一小节（循环） */
    if (gtr_seq_held && gtr_seq_rhythm >= 0 &&
        (int32_t)(now_ms - gtr_bar_end_ms) >= 0) {
        int any = 0;
        for (slot = 0; slot < 16; slot++) if (gtr_sq[slot].used) { any = 1; break; }
        if (!any) gtr_schedule_bar(gtr_seq_rhythm);   /* 小节到点且排空 → 续排 */
    }
    /* 到期的 Note On/Off */
    for (slot = 0; slot < 64; slot++) {
        if (!gtr_nq[slot].used) continue;
        if ((int32_t)(now_ms - gtr_nq[slot].at_ms) < 0) continue;
        if (gtr_nq[slot].on)
            gtr_sink(0x90, gtr_nq[slot].note, gtr_nq[slot].velocity);
        else
            gtr_sink(0x80, gtr_nq[slot].note, 0);
        gtr_nq[slot].used = 0;
    }
}
