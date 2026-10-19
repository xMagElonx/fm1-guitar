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
#include "gtr_chord.h"

#include <string.h>
#include <stdio.h>

#define GTR_CHORD_N   11
static const uint32_t gtr_chord_bits[GTR_CHORD_N] = {
    1u << 1,  1u << 3,  1u << 5,  1u << 8,  1u << 10,
    1u << 13, 1u << 15, 1u << 17, 1u << 20, 1u << 22, 1u << 25
};
/* 6 弦真实吉他把位（低→高），标准调弦 E2=40 A2=45 D3=50 G3=55 B3=59 E4=64。
   金标指法：默认槽固件侧继续播放这些（零行为变化）。 */
static const int gtr_golden[GTR_CHORD_N][6] = {
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

/* 白键功能 → 内置节奏型 id（-1 = 不是节奏型键）。
   B1.9：查表替代原来的 4 个 case —— 以后接入 BALLAD（数据早就有，只是没键位）
   或第 5 个节奏型，只改这张表，不动 gtr_part_keys 的分派逻辑。 */
static const int8_t k_wk_rhythm[WK_W_N] = {
    -1, -1, -1, -1, -1, -1,   /* W1-6  拨弦 */
    -1, -1, -1, -1,           /* W7-10 下扫 / 哑音 / 上扫 / 闷音 */
    GTR_RHYTHM_FOLK, GTR_RHYTHM_POP, GTR_RHYTHM_PUNK, GTR_RHYTHM_REGGAE,  /* W11-14 */
    -1, -1                    /* W15-16 重下扫 / 强上扫 */
};

/* 节奏型状态的「无」哨兵。不能复用 -1：GTR_RHYTHM_USER 也是 -1，
   两者混用会让「存了自定义模式」和「没有节奏型」变成同一个值。 */
#define GTR_SEQ_NONE (-2)

/* ---- 状态 ---- */
static void (*gtr_sink)(uint8_t, uint8_t, uint8_t);
static int gtr_chord;                   /* 当前和弦 */
static int gtr_alt_up;                  /* W7 连点交替标记 */
static int gtr_alt_chord;               /* 交替状态隶属的和弦 */
static uint32_t gtr_last_stroke_ms;     /* 交替重置计时 */
static int gtr_seeded;
static uint32_t gtr_prev_keys;
static uint32_t gtr_now_ms;
static int gtr_seq_rhythm = GTR_SEQ_NONE; /* 按住中的节奏型（GTR_SEQ_NONE = 无） */
static int gtr_seq_held;               /* 节奏型键是否仍按住 */
static uint32_t gtr_bar_end_ms;        /* 当前小节的结束时刻（续排的节拍网格） */
static uint32_t gtr_bar_len_ms = 2000; /* 小节时长（barSec 2.0s） */
static char gtr_dbg[20];               /* 诊断回执（LCD 显示，宿主读取） */
static int gtr_dbg_len;

/* ---- B1.6 和弦库编辑器状态 ---- */
static gtr_chord_table_t gtr_table;    /* 可变和弦表（编辑器与存储的共用数据层） */
static gtr_chord_edit_t gtr_editor;    /* 编辑器状态机 */
static int gtr_edit_mode;              /* 1=CHORDS 页编辑模式 0=演奏模式 */
static int gtr_voicing_buf[GTR_CHORD_NOTES]; /* 生成 voicing 的返回缓冲 */
static int gtr_table_inited;           /* 表是否已初始化 */

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
    /* 单弦拨的 0-5 = 弦号，-1 = 不按弦记账（扫弦/和弦试听）。
       B1.9 修复：gtr_held 改成「关音即清」之后，拨弦分支原来靠
       「同音高 off 是否已排队」判断这根弦还响不响 —— 而它自己为同弦连拨
       排的那个 off 会先落地把 gtr_held 清掉，于是第三次连拨没有前置 off，
       合成器对仍在发声的音高不响应，那一下就哑了（实测 t=200 的 on 前面
       没有任何 off）。带上弦号后，off 只清自己这根弦，on 会把它重新立起来。 */
    int8_t str;
} gtr_nq[64];

/* ---- 延迟扫弦队列（节奏型键的一小节排程）----
   B1.9：16 → GTR_MAX_STEPS。自定义模式最多 32 击，队列装不下就会被静默丢弃；
   内置节奏型最多 8 击，所以这个问题一直被掩盖着。 */
#define GTR_SQ_N GTR_MAX_STEPS
static struct gtr_stroke_q {
    int used;
    uint32_t at_ms;
    int stroke;
    int palm;          /* 1 = 闷音步：走 gtr_sem 的 muted 路径（gate 钳 0.14） */
} gtr_sq[GTR_SQ_N];

/* ---- B1.9 自定义节奏状态（编辑器 / 存储写入的数据层）---- */
static struct gtr_rhythm gtr_user_rhythm;
static int gtr_user_ok;        /* 1 = 存有合法模式 */
static int gtr_sq_drops;       /* 诊断：排程时队列满而丢弃的击数 */

static const int *gtr_current_chord(int *n)
{
    *n = GTR_CHORD_NOTES;
    /* 默认槽 → 金标指法（零行为变化）；改过的槽 → 生成 voicing */
    if (!gtr_table_inited || gtr_chord_table_is_default(&gtr_table, gtr_chord) == 1)
        return gtr_golden[gtr_chord];
    int root, type;
    if (gtr_chord_table_get(&gtr_table, gtr_chord, &root, &type) == GTR_CHORD_OK) {
        uint8_t v[GTR_CHORD_STRINGS];
        if (gtr_chord_voicing(root, type, v) == GTR_CHORD_OK) {
            for (int i = 0; i < GTR_CHORD_NOTES; i++)
                gtr_voicing_buf[i] = v[i];
            return gtr_voicing_buf;
        }
    }
    return gtr_golden[gtr_chord];  /*  fallback */
}

static void gtr_queue_note_str(uint32_t at, int on, int note, int vel, int str)
{
    int slot;
    for (slot = 0; slot < 64; slot++) {
        if (gtr_nq[slot].used) continue;
        gtr_nq[slot].used = 1;
        gtr_nq[slot].at_ms = at;
        gtr_nq[slot].on = on ? 1 : 0;
        gtr_nq[slot].note = (uint8_t)note;
        gtr_nq[slot].velocity = (uint8_t)vel;
        gtr_nq[slot].str = (int8_t)str;
        return;
    }                                  /* 队列满：丢弃（MVP） */
}

static void gtr_queue_note(uint32_t at, int on, int note, int vel)
{
    gtr_queue_note_str(at, on, note, vel, -1);
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

static void gtr_emit_ex(int stroke_id, int vel_high, int vel_low, int palm)
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
    opts.palm_mute = palm ? 1 : 0;
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

static void gtr_emit_v(int stroke_id, int vel_high, int vel_low)
{
    gtr_emit_ex(stroke_id, vel_high, vel_low, 0);
}

static void gtr_emit(int stroke_id)
{
    gtr_emit_ex(stroke_id, 127, 100, 0);   /* 成品力度：音量拉满，不会被淹没 */
}

/* 试听一个 (root, type) 和弦：块和弦（六弦同响），500ms gate */
static void gtr_audition_chord(int root, int type)
{
    uint8_t v[GTR_CHORD_STRINGS];
    if (gtr_chord_voicing(root, type, v) != GTR_CHORD_OK) {
        dbg_put('E');
        dbg_num(-GTR_CHORD_E_INTERNAL);
        return;
    }
    gtr_retrigger_prepare();
    for (int i = 0; i < GTR_CHORD_NOTES; i++) {
        gtr_queue_note(gtr_now_ms, 1, v[i], 100);
        gtr_queue_note(gtr_now_ms + 500u, 0, v[i], 0);
    }
    memset(gtr_held, 0, sizeof gtr_held);
    for (int i = 0; i < GTR_CHORD_NOTES; i++)
        gtr_held[i] = v[i];
    dbg_put('A');
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
    uint32_t fell = previous_keys & ~current_keys;
    int w, chord_changed = (gtr_chord != gtr_alt_chord);

    gtr_now_ms = now_ms;
    if (!gtr_seeded) {
        gtr_prev_keys = current_keys;
        gtr_alt_chord = gtr_chord;
        gtr_seeded = 1;
        return;
    }
    /* 延迟初始化表和编辑器（首次按键时） */
    if (!gtr_table_inited) {
        gtr_chord_table_init(&gtr_table);
        gtr_chord_edit_init(&gtr_editor, &gtr_table);
        gtr_table_inited = 1;
    }
    /* 交替状态重置：换和弦或停 2 秒 */
    if (chord_changed || (uint32_t)(now_ms - gtr_last_stroke_ms) > 2000u)
        gtr_alt_up = 0;

    if (gtr_edit_mode) {
        /* CHORDS 页编辑模式：黑键选槽+试听，白键停用 */
        for (w = 0; w < GTR_CHORD_N; w++) {
            if (rose & gtr_chord_bits[w]) {
                gtr_chord_edit_press(&gtr_editor, w);
                gtr_chord = w;  /* 同步当前和弦（试听用） */
                int root, type;
                if (gtr_chord_edit_audition(&gtr_editor, &root, &type) == 1)
                    gtr_audition_chord(root, type);
                dbg_put('B');
                dbg_num(w);
            }
            if (fell & gtr_chord_bits[w])
                gtr_chord_edit_release(&gtr_editor);
        }
        gtr_prev_keys = current_keys;
        return;
    }

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
        int rid;
        if (!(rose & mask)) continue;
        dbg_put('W');
        dbg_num(w);
        dbg_put(' ');
        rid = k_wk_rhythm[gtr_white_type[w]];
        if (rid >= 0) {            /* B1.9：节奏型键查表，不再占用 switch 分支 */
            gtr_seq_rhythm = rid;
            gtr_schedule_bar(rid); /* 按下立即起一小节（原来是 case 分支的副作用） */
            continue;
        }
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
                    gtr_queue_note_str(gtr_now_ms, 0, gtr_held[idx], 0, idx);
                }
                gtr_queue_note_str(gtr_now_ms, 1, pe.note, pe.velocity, idx);
                gtr_queue_note_str(gtr_now_ms + 500u, 0, pe.note, 0, idx);
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
        case WK_HEAVY:     gtr_emit_v(GTR_STROKE_DOWN, 127, 100); gtr_last_stroke_ms = now_ms; break;
        case WK_UP_STRONG: gtr_emit_v(GTR_STROKE_UP, 112, 80);  gtr_last_stroke_ms = now_ms; break;
        default: break;
        }
    }
    /* 节奏型键：按下立即起一小节；按住持续循环（tick 续排），松手当前小节收尾 */
    gtr_seq_held = (current_keys & GTR_SEQ_MASK) != 0;
    if (gtr_seq_rhythm != GTR_SEQ_NONE && (rose & GTR_SEQ_MASK) && !gtr_seq_held)
        gtr_seq_rhythm = GTR_SEQ_NONE;
    gtr_prev_keys = current_keys;
}

/* 把一段模式排进延迟队列。bar_ms 是这段模式占的实际时长：整小节播放给
   gtr_bar_len_ms，单步试听也给整小节（那一击落在 0ms）。
   B2.0 从 gtr_schedule_bar 里拆出来，好让编辑器试听复用同一条排程路径 ——
   试听走别的代码就等于没验证过真正的播放路径。 */
static void gtr_schedule_pattern(int rhythm_id, const struct gtr_rhythm *cust,
                                 uint32_t bar_ms)
{
    struct gtr_stroke_sched sched[GTR_MAX_STEPS];
    int len = 0, i, slot;
    if (gtr_expand_rhythm(rhythm_id, cust, (double)bar_ms / 1000.0,
                          sched, GTR_MAX_STEPS, &len) != GTR_OK)
        return;
    for (i = 0; i < len; i++) {
        for (slot = 0; slot < GTR_SQ_N; slot++) {
            if (gtr_sq[slot].used) continue;
            gtr_sq[slot].used = 1;
            gtr_sq[slot].at_ms = gtr_now_ms + (uint32_t)(sched[i].at * 1000.0);
            gtr_sq[slot].stroke = sched[i].stroke;
            gtr_sq[slot].palm = sched[i].palm_mute;
            break;
        }
        if (slot == GTR_SQ_N) gtr_sq_drops++;   /* 队列满：记数，不再静默丢弃 */
    }
    gtr_bar_end_ms = gtr_now_ms + bar_ms;       /* 网格：按小节时长续排 */
}

/* 节奏型键：排程一小节的扫弦（延迟队列，tick 到点逐刷下发）。
   B1.9：rhythm_id == GTR_RHYTHM_USER 时把已存的自定义模式交给展开器。 */
static void gtr_schedule_bar(int rhythm_id)
{
    const struct gtr_rhythm *cust = NULL;
    if (rhythm_id == GTR_RHYTHM_USER) {
        if (!gtr_user_ok) return;               /* 没存过模式 → 不出声 */
        cust = &gtr_user_rhythm;
    }
    gtr_schedule_pattern(rhythm_id, cust, gtr_bar_len_ms);
}

void gtr_part_tick(uint32_t now_ms)
{
    int slot;
    gtr_now_ms = now_ms;       /* 修复：时钟每次 tick 刷新（原冻结在最后按键时刻，
                                  导致节奏型键的整小节排程沉没） */
    if (!gtr_sink) return;
    /* 到期的延迟扫弦 → 立即展开成音符入队 */
    for (slot = 0; slot < GTR_SQ_N; slot++) {
        if (!gtr_sq[slot].used) continue;
        if ((int32_t)(now_ms - gtr_sq[slot].at_ms) < 0) continue;
        /* 闷音步必须走 palm=1：否则自定义模式里的闷音会悄悄退化成普通下扫
           （gate 400ms 而不是 140ms，听起来就是没闷住）。 */
        gtr_emit_ex(gtr_sq[slot].stroke, 127, 100, gtr_sq[slot].palm);
        gtr_sq[slot].used = 0;
    }
    /* 按住节奏型键 → 当前小节弹完自动续下一小节（循环）。
       用 != GTR_SEQ_NONE 而不是 >= 0：GTR_RHYTHM_USER 是 -1，
       写成 >= 0 会让自定义模式永远续不上（现在 trigger 不按住，
       等 M2 给了自定义模式一个键位，这个洞就会显形）。 */
    if (gtr_seq_held && gtr_seq_rhythm != GTR_SEQ_NONE &&
        (int32_t)(now_ms - gtr_bar_end_ms) >= 0) {
        int any = 0;
        for (slot = 0; slot < GTR_SQ_N; slot++) if (gtr_sq[slot].used) { any = 1; break; }
        if (!any) gtr_schedule_bar(gtr_seq_rhythm);   /* 小节到点且排空 → 续排 */
    }
    /* 到期的 Note On/Off */
    for (slot = 0; slot < 64; slot++) {
        int j;
        if (!gtr_nq[slot].used) continue;
        if ((int32_t)(now_ms - gtr_nq[slot].at_ms) < 0) continue;
        if (gtr_nq[slot].on) {
            /* 单弦拨的 on 自己把这条弦重新立起来：它前面那个 off 是同一次
               重触发的前半截，不能让它把「正在响」记成「已经停了」。 */
            if (gtr_nq[slot].str >= 0)
                gtr_held[gtr_nq[slot].str] = gtr_nq[slot].note;
            gtr_sink(0x90, gtr_nq[slot].note, gtr_nq[slot].velocity);
        } else {
            /* 关音时同步清掉 gtr_held。B1.9 修复：它原先只增不减，记的是
               「曾经按下的音」而不是「仍在响的音」，于是每次重触发的
               gtr_retrigger_prepare 都会为早就停掉的音再补一遍 off
               （实测 4 击模式多出 18 个多余 off，占着 64 格 Note 队列）。 */
            if (gtr_nq[slot].str >= 0) {
                /* 只清自己这根弦：别的弦可能正响着同一个音高 */
                if (gtr_held[gtr_nq[slot].str] == gtr_nq[slot].note)
                    gtr_held[gtr_nq[slot].str] = 0;
            } else {
                for (j = 0; j < GTR_CHORD_NOTES; j++)
                    if (gtr_held[j] && gtr_held[j] == gtr_nq[slot].note)
                        gtr_held[j] = 0;
            }
            gtr_sink(0x80, gtr_nq[slot].note, 0);
        }
        gtr_nq[slot].used = 0;
    }
}

/* ==========================================================================
   B1.6 和弦库编辑器 API 实现
   ========================================================================== */

void gtr_part_edit_init(void)
{
    if (!gtr_table_inited) {
        gtr_chord_table_init(&gtr_table);
        gtr_chord_edit_init(&gtr_editor, &gtr_table);
        gtr_table_inited = 1;
    }
}

void gtr_part_edit_mode(int on)
{
    gtr_edit_mode = on ? 1 : 0;
    if (gtr_edit_mode && !gtr_table_inited)
        gtr_part_edit_init();
}

int gtr_part_edit_knob(int knob, int delta)
{
    if (!gtr_table_inited) return 0;
    gtr_chord_edit_knob(&gtr_editor, knob, delta);
    return gtr_editor.dirty;
}

int gtr_part_edit_modifier(int env, int lfo)
{
    if (!gtr_table_inited) return GTR_CHORD_E_ARGS;
    return gtr_chord_edit_modifier(&gtr_editor, env, lfo);
}

int gtr_part_edit_reset_slot(void)
{
    if (!gtr_table_inited) return 0;
    int before = gtr_editor.dirty;
    gtr_chord_edit_reset_slot(&gtr_editor);
    return gtr_editor.dirty != before ? 1 : 0;
}

int gtr_part_edit_is_dirty(void)
{
    return gtr_table_inited ? gtr_editor.dirty : 0;
}

void gtr_part_edit_saved(void)
{
    if (gtr_table_inited)
        gtr_chord_edit_saved(&gtr_editor);
}

int gtr_part_edit_pack_diff(uint8_t *buf, int cap)
{
    if (!gtr_table_inited) gtr_part_edit_init();
    return gtr_chord_table_pack_diff(&gtr_table, buf, cap);
}

int gtr_part_edit_unpack_diff(const uint8_t *buf, int len)
{
    if (!gtr_table_inited) gtr_part_edit_init();
    return gtr_chord_table_unpack_diff(&gtr_table, buf, len);
}

int gtr_part_edit_selected(void)
{
    return gtr_table_inited ? gtr_editor.selected : 0;
}

int gtr_part_edit_slot_root(int slot)
{
    if (!gtr_table_inited || slot < 0 || slot >= GTR_CHORD_SLOTS) return -1;
    return gtr_table.slots[slot].root;
}

int gtr_part_edit_slot_type(int slot)
{
    if (!gtr_table_inited || slot < 0 || slot >= GTR_CHORD_SLOTS) return -1;
    return gtr_table.slots[slot].type;
}

int gtr_part_edit_slot_name(int slot, char *buf, int cap)
{
    if (!gtr_table_inited || slot < 0 || slot >= GTR_CHORD_SLOTS || !buf)
        return GTR_CHORD_E_ARGS;
    return gtr_chord_name(gtr_table.slots[slot].root, gtr_table.slots[slot].type, buf, cap);
}

int gtr_part_edit_slot_is_default(int slot)
{
    if (!gtr_table_inited || slot < 0 || slot >= GTR_CHORD_SLOTS) return -1;
    return gtr_chord_table_is_default(&gtr_table, slot);
}

/* ==========================================================================
   B1.9 扫弦节奏编辑器 M1：自定义模式数据通路
   ========================================================================== */

int gtr_part_set_user_rhythm(const struct gtr_rhythm *r)
{
    struct gtr_stroke_sched probe[GTR_MAX_STEPS];
    int len = 0, i, rc;

    if (!r) return GTR_E_ARGS;
    /* 步必须严格递增。同一毫秒上的两击会在同一个 tick 里被展开，第二击的
       gtr_retrigger_prepare 会把第一击刚按响的音 off 掉 —— 听起来像丢了一拍。
       步进网格天然严格递增，走到这里还不递增就是编辑器算错了，直接拒。 */
    for (i = 1; i < r->n_steps; i++)
        if (!(r->steps[i].beat > r->steps[i - 1].beat)) return GTR_E_RHYTHM;
    /* 用真正的展开器当校验器：beats 范围 / stroke 合法性 / n_steps 上限
       全由它把关。所以「存进去的一定放得出来」不是承诺，是同一次调用。 */
    rc = gtr_expand_rhythm(GTR_RHYTHM_USER, r, (double)gtr_bar_len_ms / 1000.0,
                           probe, GTR_MAX_STEPS, &len);
    if (rc != GTR_OK) return rc;
    (void)len;
    gtr_user_rhythm = *r;
    gtr_user_ok = 1;
    return GTR_OK;
}

int gtr_part_user_rhythm_steps(void)
{
    return gtr_user_ok ? gtr_user_rhythm.n_steps : 0;
}

int gtr_part_trigger_rhythm(int rhythm_id)
{
    if (rhythm_id == GTR_RHYTHM_USER) {
        if (!gtr_user_ok) return GTR_E_ARGS;      /* 还没存过模式 → 不出声 */
    } else if (rhythm_id < GTR_RHYTHM_FOLK || rhythm_id > GTR_RHYTHM_BALLAD) {
        return GTR_E_RHYTHM;
    }
    gtr_seq_rhythm = rhythm_id;
    gtr_schedule_bar(rhythm_id);
    return GTR_OK;
}

void gtr_part_stop_rhythm(void)
{
    /* 只停「续排」：已排进队列的击照常播完当前小节，不硬切。 */
    gtr_seq_held = 0;
    gtr_seq_rhythm = GTR_SEQ_NONE;
}

int gtr_part_dropped_strokes(void)
{
    return gtr_sq_drops;
}


/* ==========================================================================
   B2.0 扫弦节奏编辑器（STRUM 页）：16 步 × 5 动作
   --------------------------------------------------------------------------
   16 步 = 4 拍 × 4 个十六分，一步一格。走「十六格全自由」方案：每格独立
   选动作，方向不按格子位置自动推断 —— 纯下拨的 punk 节奏在物理上完全成立，
   任何「偶数格必是下扫」的摆臂法则都会把它改错。

   动作码 → 引擎参数的映射只有一处（gtr_strum_note）。界面、单步试听、整
   小节播放全部经过它，所以屏上显示什么就一定听到什么。

   内存：16 字节编辑缓冲 + 3 个状态量，全部静态。落盘留给 M3（project
   记录正好 4096 字节已填满，需要单独腾空间）。
   ========================================================================== */

#define GTR_STRUM_BEATS 4

static uint8_t gtr_step_action[GTR_STRUM_STEPS] = {
    GTR_STEP_REST, GTR_STEP_REST, GTR_STEP_REST, GTR_STEP_REST,
    GTR_STEP_REST, GTR_STEP_REST, GTR_STEP_REST, GTR_STEP_REST,
    GTR_STEP_REST, GTR_STEP_REST, GTR_STEP_REST, GTR_STEP_REST,
    GTR_STEP_REST, GTR_STEP_REST, GTR_STEP_REST, GTR_STEP_REST
};
static int gtr_strum_cursor, gtr_strum_edit, gtr_strum_dirty, gtr_strum_stored;

/* 动作码 → (stroke, palm_mute)。返回 0 = 这一步不出声（休止或非法码）。 */
static int gtr_strum_note(int action, int *stroke, int *palm)
{
    switch (action) {
    case GTR_STEP_DOWN:  *stroke = GTR_STROKE_DOWN;  *palm = 0; return 1;
    case GTR_STEP_UP:    *stroke = GTR_STROKE_UP;    *palm = 0; return 1;
    case GTR_STEP_CHUCK: *stroke = GTR_STROKE_CHUCK; *palm = 0; return 1;
    case GTR_STEP_MUTE:  *stroke = GTR_STROKE_DOWN;  *palm = 1; return 1;
    default:             return 0;
    }
}

/* 16 步 → struct gtr_rhythm。beat 用 i*beats/steps 算，不写死 0.25：
   将来换 8/32 步网格只改 GTR_STRUM_STEPS。 */
static int gtr_strum_build(struct gtr_rhythm *r)
{
    int i, n = 0, stroke, palm;
    r->beats = GTR_STRUM_BEATS;
    for (i = 0; i < GTR_STRUM_STEPS; ++i) {
        if (!gtr_strum_note(gtr_step_action[i], &stroke, &palm)) continue;
        if (n >= GTR_MAX_STEPS) break;
        r->steps[n].beat = (double)i * (double)GTR_STRUM_BEATS / (double)GTR_STRUM_STEPS;
        r->steps[n].stroke = stroke;
        r->steps[n].palm_mute = palm;
        ++n;
    }
    r->n_steps = n;
    return n;
}

void gtr_part_strum_edit_mode(int on)
{
    gtr_strum_edit = on ? 1 : 0;
}

int gtr_part_strum_edit_active(void) { return gtr_strum_edit; }
int gtr_part_strum_cursor(void)      { return gtr_strum_cursor; }
int gtr_part_strum_is_dirty(void)    { return gtr_strum_dirty; }
int gtr_part_strum_stored(void)      { return gtr_strum_stored; }

int gtr_part_strum_get(int step)
{
    if (step < 0 || step >= GTR_STRUM_STEPS) return -1;
    return gtr_step_action[step];
}

int gtr_part_strum_set(int step, int action)
{
    if (step < 0 || step >= GTR_STRUM_STEPS) return 0;
    if (action < 0 || action >= GTR_STEP_ACTIONS) return 0;
    if (gtr_step_action[step] == (uint8_t)action) return 0;
    gtr_step_action[step] = (uint8_t)action;
    gtr_strum_dirty = 1;
    gtr_strum_stored = 0;     /* 改过之后，RAM 里的模式不再等于编辑缓冲 */
    return 1;
}

/* 白键连点循环：REST → D → U → X → M → REST。空步第一下就变成下扫，
   跟 X0X 步进器「空步一按即激活」一致。 */
int gtr_part_strum_cycle(int step)
{
    if (step < 0 || step >= GTR_STRUM_STEPS) return 0;
    return gtr_part_strum_set(step, (gtr_step_action[step] + 1) % GTR_STEP_ACTIONS);
}

int gtr_part_strum_select(int step)
{
    if (step < 0 || step >= GTR_STRUM_STEPS) return 0;
    if (gtr_strum_cursor == step) return 0;
    gtr_strum_cursor = step;
    return 1;
}

int gtr_part_strum_clear(void)
{
    int i, changed = 0;
    for (i = 0; i < GTR_STRUM_STEPS; ++i)
        if (gtr_step_action[i] != GTR_STEP_REST) {
            gtr_step_action[i] = GTR_STEP_REST;
            changed = 1;
        }
    if (changed) {
        gtr_strum_dirty = 1;
        gtr_strum_stored = 0;
    }
    return changed;
}

/* 提交编辑缓冲 → 演奏层。全休止等于「撤掉自定义模式」，退回内置节奏型。 */
int gtr_part_strum_commit(void)
{
    struct gtr_rhythm r;
    int rc;
    memset(&r, 0, sizeof r);
    if (gtr_strum_build(&r) == 0) {
        gtr_user_ok = 0;
        gtr_strum_stored = 0;
        gtr_strum_dirty = 0;
        return GTR_OK;
    }
    rc = gtr_part_set_user_rhythm(&r);
    if (rc == GTR_OK) {
        gtr_strum_stored = 1;
        gtr_strum_dirty = 0;
    }
    return rc;
}

/* 单步试听：把这一击当成「1 拍 1 步」的最小模式排出去。
   特意不去动 gtr_user_rhythm —— 试听不该改写用户正在编的模式。 */
int gtr_part_strum_audition(int step)
{
    struct gtr_rhythm one;
    int stroke, palm;
    if (step < 0 || step >= GTR_STRUM_STEPS) return GTR_E_ARGS;
    if (!gtr_strum_note(gtr_step_action[step], &stroke, &palm)) return GTR_E_ARGS;
    one.beats = 1;
    one.n_steps = 1;
    one.steps[0].beat = 0.0;
    one.steps[0].stroke = stroke;
    one.steps[0].palm_mute = palm;
    gtr_schedule_pattern(GTR_RHYTHM_USER, &one, gtr_bar_len_ms);
    return GTR_OK;
}
