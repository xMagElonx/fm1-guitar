/* ============================================================================
   gtr_sem · 吉他演奏语义层（C 翻译版）
   ----------------------------------------------------------------------------
   母本：fm1-guitar/engine/strum.js（JS 参考实现，141 项断言锁定）。
   本模块是 JS → C 的逐函数翻译，行为必须与 JS 逐字段一致 —— 由
   golden 对拍测试保证（tests/golden_gen.mjs 生成、tests/test_main.c 核对）。

   纪律（演进路径文档第 3 节规则 1）：
     · 不 include 任何 SDK/宿主头文件 —— 纯 C99，任何编译器任何宿主都能编；
     · 无动态内存 —— 全部调用方提供缓冲（容量宏见下）；
     · 符号全部 gtr_ 前缀 —— unity build 安全；
     · 所有校验失败返回负错误码，绝不静默修正（JS 同纪律）。

   时间单位：秒（double）。JS Math.round 的语义用 gtr_js_round 复刻
   （floor(x+0.5)），保证与 JS 的微秒取整逐位一致。
   ========================================================================= */
#ifndef GTR_SEM_H
#define GTR_SEM_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* 容量上限（调用方缓冲必须不小于这些值） */
#define GTR_MAX_NOTES  12
#define GTR_MAX_EVENTS 64
#define GTR_MAX_STEPS  32

/* 错误码（负值） */
enum {
    GTR_OK              =  0,
    GTR_E_ARGS          = -1,  /* 通用参数非法（空指针/容量不足） */
    GTR_E_NOTES         = -2,  /* notes 非空、≥2 个、0-127 整数、严格升序 */
    GTR_E_PATTERN       = -3,  /* 未知 pattern / 自定义 order 非法 */
    GTR_E_SPREAD        = -4,  /* spread ≤ 0 */
    GTR_E_GATE          = -5,  /* gate ≤ 0 */
    GTR_E_VEL           = -6,  /* 力度端点越界（须 1-127 整数） */
    GTR_E_STRINGS       = -7,  /* pattern.strings 非 ≥2 整数 */
    GTR_E_SEED          = -8,  /* humanize seed < 1 */
    GTR_E_JITTER        = -9,  /* timingMs/velJitter < 0 */
    GTR_E_BARSEC        = -10, /* barSec ≤ 0 */
    GTR_E_RHYTHM        = -11, /* 未知节奏型 / steps 空 / beat 越界 / stroke 非法 */
    GTR_E_INTERNAL      = -12  /* 产出自检失败（时序/力度）—— 翻译写坏了 */
};

/* 拨弦方向与内置 pattern（与 JS PATTERNS 一一对应） */
enum gtr_stroke {
    GTR_STROKE_DOWN  = 0,   /* 'down'  低→高，全弦，112/76 */
    GTR_STROKE_UP    = 1,   /* 'up'    高→低，高音 4 弦，97/61 */
    GTR_STROKE_CHUCK = 2    /* 'chuck' 低→高，哑音，58/45，gate 钳 0.07 */
};

struct gtr_pattern {
    int order;      /* 0 = low-to-high, 1 = high-to-low */
    int muted;      /* 0/1 */
    int velHigh;    /* 0 = 用调用方 opts.velHigh（自定义 pattern 时 JS 也这么走） */
    int velLow;
    int strings;    /* 0 = 不限（= 和弦弦数），否则 ≥2 */
};

struct gtr_strum_opts {
    int pattern_id;             /* GTR_STROKE_* ；-1 = 用 custom_pattern */
    struct gtr_pattern custom_pattern;
    double spread;              /* 秒；call 方给 0.025 默认 */
    double gate;                /* 秒；调用方给 1.2 默认 */
    int velHigh;                /* 0 = 用 pattern 默认 */
    int velLow;                 /* 0 = 用 pattern 默认 */
    int palm_mute;              /* 0/1 事件级闷音（gate 钳 0.14） */
    int string_budget;          /* 0/1 力度决定弦数（保低音侧） */
};

struct gtr_event {
    int note;
    int velocity;
    double tOn;
    double tOff;
    int string;                 /* 原始弦位（0 = 最低音弦） */
    int stroke;                 /* 拨弦次序（0 = 第一个被拨到） */
};

/* 内置节奏型 id */
enum { GTR_RHYTHM_FOLK = 0, GTR_RHYTHM_POP = 1, GTR_RHYTHM_PUNK = 2,
       GTR_RHYTHM_REGGAE = 3, GTR_RHYTHM_BALLAD = 4 };

struct gtr_rhythm_step {
    double beat;                /* [0, beats) */
    int stroke;                 /* GTR_STROKE_* */
    int palm_mute;              /* 0/1 */
};

struct gtr_rhythm {
    int beats;                  /* ≥1 整数 */
    struct gtr_rhythm_step steps[GTR_MAX_STEPS];
    int n_steps;
};

struct gtr_stroke_sched {       /* expandRhythm 的输出行 */
    int stroke;
    double at;                  /* 相对小节起点，秒 */
    int palm_mute;
};

/* ---- 核心：和弦 + pattern + 时序 → 事件序列（strum.js strum()）----
   notes 必须严格升序（低音弦 → 高音弦）。返回 GTR_OK 或负错误码。 */
int gtr_strum(const int *notes, int n_notes,
              const struct gtr_strum_opts *opts,
              struct gtr_event *out, int out_cap, int *out_len);

/* ---- 确定性人性化（humanize()）：新写 out，不改输入 ----
   seed ≥1；timing_ms/vel_jitter ≥0（0 的轴完全不动，连 rng 都不消耗）。 */
int gtr_humanize(const struct gtr_event *ev, int n,
                 uint32_t seed, double timing_ms, double vel_jitter,
                 struct gtr_event *out, int out_cap);

/* ---- 单弦拨弦（Ample 式单音键）：从当前和弦里拨一根弦 ----
   string_idx：0 = 和弦最低音弦。产出一个事件（tOn=0，tOff=默认 gate 1.2）。 */
int gtr_pluck(const int *notes, int n_notes, int string_idx,
              int velocity, struct gtr_event *out);

/* ---- mulberry32 单步（测试用；内部状态由调用方持有）----
   与 JS mulberry32 闭包逐位一致：state 原地推进，返回 [0,1) 双精度。 */
uint32_t gtr_mulberry32_next(uint32_t *state);

/* ---- 循环小节种子派生：base + i*7919（JS barSeed()）----
   base < 1 或 index 溢出时返回 0（0 不是合法种子，调用方据此判错）。 */
uint32_t gtr_bar_seed(uint32_t base, uint32_t index);

/* ---- 节奏型 → 一小节扫弦时刻表（expandRhythm()）----
   rhythm_id ≥0 用内置五型；-1 用 custom。返回 GTR_OK 或负错误码。 */
int gtr_expand_rhythm(int rhythm_id, const struct gtr_rhythm *custom,
                      double bar_sec,
                      struct gtr_stroke_sched *out, int out_cap, int *out_len);

#ifdef __cplusplus
}
#endif
#endif /* GTR_SEM_H */
