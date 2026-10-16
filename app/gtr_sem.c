/* ============================================================================
   gtr_sem.c · 吉他演奏语义层实现
   ----------------------------------------------------------------------------
   母本：fm1-guitar/engine/strum.js @ 主分支。翻译对照关系（函数级）：
     checkNotes      → check_notes
     PATTERNS        → 静态表 k_patterns（按 GTR_STROKE_* 索引）
     strum           → gtr_strum
     checkVel        → check_vel（内联进校验）
     mulberry32/imul → gtr_mulberry32（uint32 环绕运算，位级与 JS 一致）
     humanize        → gtr_humanize
     RHYTHMS         → 静态表 k_rhythms（按 GTR_RHYTHM_* 索引）
     expandRhythm    → gtr_expand_rhythm
     barSeed         → gtr_bar_seed
   JS Math.round(x) = floor(x + 0.5)，用 gtr_js_round 复刻 —— 这是与 JS
   微秒级逐位一致的关键。所有浮点运算顺序与 JS 相同（IEEE754 双精度）。
   ========================================================================= */
#include "gtr_sem.h"

#include <string.h>

/* JS Math.round 复刻：floor(x + 0.5)。对全部有限 x 与 Math.round 一致。
   floor 手写实现（嵌入式链接无 libm）：量级上界受微秒取整约束（|y| < 2^53），
   截断后按符号校正 —— 与 floor 逐位一致。 */
static double gtr_js_round(double x)
{
    double y = x + 0.5;
    long long t = (long long)y;          /* 向零截断 */
    if (y >= 0.0 || (double)t == y)
        return (double)t;
    return (double)(t - 1);
}

/* ---- 内置 pattern 表（strum.js PATTERNS）----
   strings = 0 表示 JS 的 null（不限弦宽，用和弦弦数）。 */
static const struct gtr_pattern k_patterns[3] = {
    /* GTR_STROKE_DOWN  */ { 0, 0, 112, 76, 6 },
    /* GTR_STROKE_UP    */ { 1, 0,  97, 61, 4 },
    /* GTR_STROKE_CHUCK */ { 0, 1,  58, 45, 0 },
};

#define GTR_CHUCK_GATE 0.07
#define GTR_MUTE_GATE  0.14

static int check_notes(const int *notes, int n)
{
    if (!notes || n <= 0) return GTR_E_NOTES;
    if (n < 2) return GTR_E_NOTES;
    for (int i = 0; i < n; i++) {
        if (notes[i] < 0 || notes[i] > 127) return GTR_E_NOTES;
        if (i > 0 && notes[i] <= notes[i - 1]) return GTR_E_NOTES;
    }
    return GTR_OK;
}

static int check_vel(int v)
{
    if (v < 1 || v > 127) return GTR_E_VEL;
    return GTR_OK;
}

int gtr_strum(const int *notes, int n_notes,
              const struct gtr_strum_opts *opts,
              struct gtr_event *out, int out_cap, int *out_len)
{
    if (!opts || !out || !out_len) return GTR_E_ARGS;
    int rc = check_notes(notes, n_notes);
    if (rc != GTR_OK) return rc;

    /* pattern 解析：id 或自定义（JS 的 string/object 两分支） */
    struct gtr_pattern pattern;
    if (opts->pattern_id >= GTR_STROKE_DOWN && opts->pattern_id <= GTR_STROKE_CHUCK) {
        pattern = k_patterns[opts->pattern_id];
    } else if (opts->pattern_id == -1) {
        const struct gtr_pattern *c = &opts->custom_pattern;
        if (c->order != 0 && c->order != 1) return GTR_E_PATTERN;
        pattern.order = c->order;
        pattern.muted = c->muted ? 1 : 0;
        pattern.velHigh = c->velHigh;
        pattern.velLow = c->velLow;
        pattern.strings = c->strings;
    } else {
        return GTR_E_PATTERN;
    }

    const double spread = opts->spread;
    const double gate = opts->gate;
    if (!(spread > 0.0)) return GTR_E_SPREAD;
    if (!(gate > 0.0)) return GTR_E_GATE;

    int velHigh = opts->velHigh != 0 ? opts->velHigh : pattern.velHigh;
    int velLow = opts->velLow != 0 ? opts->velLow : pattern.velLow;
    if (check_vel(velHigh) != GTR_OK) return GTR_E_VEL;
    if (check_vel(velLow) != GTR_OK) return GTR_E_VEL;

    const int muted = pattern.muted || opts->palm_mute;
    const double cap_gate = pattern.muted ? GTR_CHUCK_GATE
                            : (opts->palm_mute ? GTR_MUTE_GATE : gate);

    /* 有效弦集合：pattern 弦宽截（方向端），再力度预算截（保低音侧） */
    const int n0 = n_notes;
    int width = pattern.strings != 0 ? pattern.strings : n0;
    if (width < 2 || width != (width / 1)) return GTR_E_STRINGS;
    if (width > n0) width = n0;
    int lo = 0, hi = n0 - 1;
    if (pattern.order == 1) lo = n0 - width;   /* 上扫：保高音侧 */
    else hi = width - 1;                       /* 下扫：保低音侧 */
    if (opts->string_budget && !pattern.muted) {
        double frac = (velHigh - 64) / (112.0 - 64);
        if (frac < 0) frac = 0;
        if (frac > 1) frac = 1;
        int budget = (int)gtr_js_round(3 + (width - 3) * frac);
        if (budget < 2) budget = 2;
        if (budget > width) budget = width;
        hi = lo + budget - 1;
    }
    const int n = hi - lo + 1;

    const double step = spread / (n - 1);
    if (n > out_cap) return GTR_E_ARGS;
    for (int s = 0; s < n; s++) {
        const int idx = pattern.order == 0 ? lo + s : hi - s;
        const double t_on = s * step;
        const int vel = (int)gtr_js_round(velHigh + (velLow - velHigh) *
                                          (s / (double)(n - 1)));
        out[s].note = notes[idx];
        out[s].velocity = vel;
        out[s].tOn = t_on;
        out[s].tOff = t_on + (muted ? cap_gate : gate);
        out[s].string = idx;
        out[s].stroke = s;
    }
    /* 终检（strum.js 的自洽保险，同款） */
    for (int e = 0; e < n; e++) {
        if (!(out[e].tOn >= 0) || !(out[e].tOff > out[e].tOn))
            return GTR_E_INTERNAL;
        if (out[e].velocity < 1 || out[e].velocity > 127)
            return GTR_E_INTERNAL;
        if (e > 0 && out[e].tOn < out[e - 1].tOn)
            return GTR_E_INTERNAL;
    }
    *out_len = n;
    return GTR_OK;
}

/* ---- 单弦拨弦（Ample 式单音键）---- */
int gtr_pluck(const int *notes, int n_notes, int string_idx,
              int velocity, struct gtr_event *out)
{
    if (check_notes(notes, n_notes) != GTR_OK) return GTR_E_NOTES;
    if (!out || string_idx < 0 || string_idx >= n_notes) return GTR_E_ARGS;
    if (check_vel(velocity) != GTR_OK) return GTR_E_VEL;
    out->note = notes[string_idx];
    out->velocity = velocity;
    out->tOn = 0.0;
    out->tOff = 1.2;          /* 与 strum 默认 gate 一致 */
    out->string = string_idx;
    out->stroke = 0;
    return GTR_OK;
}

/* ---- mulberry32（uint32 环绕运算，位级与 JS mulberry32 逐位一致）---- */
static uint32_t gtr_imul(uint32_t a, uint32_t b)
{
    return (uint32_t)((uint64_t)a * b);   /* mod 2^32 ≡ JS 的 32 位截断乘法 */
}

uint32_t gtr_mulberry32_next(uint32_t *state)
{
    *state = (*state + 0x6D2B79F5u) | 0u;
    uint32_t t = gtr_imul(*state ^ (*state >> 15), 1u | *state);
    t = (t + gtr_imul(t ^ (t >> 7), 61u | t)) ^ t;
    return (t ^ (t >> 14)) | 0u;
}

uint32_t gtr_bar_seed(uint32_t base, uint32_t index)
{
    if (base < 1) return 0;
    return base + index * 7919u;
}

int gtr_humanize(const struct gtr_event *ev, int n,
                 uint32_t seed, double timing_ms, double vel_jitter,
                 struct gtr_event *out, int out_cap)
{
    if (!ev || n <= 0 || !out) return GTR_E_ARGS;
    if (seed < 1) return GTR_E_SEED;
    if (!(timing_ms >= 0.0) || !(vel_jitter >= 0.0)) return GTR_E_JITTER;
    if (n > out_cap) return GTR_E_ARGS;
    uint32_t rng = seed;                 /* JS mulberry32(seed) 的内部状态 */
    for (int i = 0; i < n; i++) {
        struct gtr_event o = ev[i];
        if (timing_ms > 0.0) {
            double r0 = gtr_mulberry32_next(&rng) / 4294967296.0;
            double d_t = (r0 * 2.0 - 1.0) * (timing_ms / 1000.0);
            o.tOn = gtr_js_round((ev[i].tOn + d_t) * 1e6) / 1e6;
            if (o.tOn < 0) o.tOn = 0;
            o.tOff = gtr_js_round((o.tOn + (ev[i].tOff - ev[i].tOn)) * 1e6) / 1e6;
        }
        if (vel_jitter > 0.0) {
            double r1 = gtr_mulberry32_next(&rng) / 4294967296.0;
            int d_v = (int)gtr_js_round((r1 * 2.0 - 1.0) * vel_jitter);
            o.velocity = ev[i].velocity + d_v;
            if (o.velocity > 127) o.velocity = 127;
            if (o.velocity < 1) o.velocity = 1;
        }
        out[i] = o;
    }
    return GTR_OK;
}

/* ---- 内置节奏型表（strum.js RHYTHMS，摆臂法则）---- */
static const struct gtr_rhythm k_rhythms[5] = {
    /* FOLK  */ { 4, { { 0.0, GTR_STROKE_DOWN, 0 }, { 1.0, GTR_STROKE_DOWN, 0 },
                       { 1.5, GTR_STROKE_UP, 0 }, { 2.5, GTR_STROKE_UP, 0 },
                       { 3.0, GTR_STROKE_DOWN, 0 }, { 3.5, GTR_STROKE_UP, 0 } }, 6 },
    /* POP   */ { 4, { { 0.0, GTR_STROKE_DOWN, 0 }, { 0.5, GTR_STROKE_UP, 0 },
                       { 1.0, GTR_STROKE_DOWN, 0 }, { 1.5, GTR_STROKE_UP, 0 },
                       { 2.0, GTR_STROKE_DOWN, 0 }, { 2.5, GTR_STROKE_UP, 0 },
                       { 3.0, GTR_STROKE_DOWN, 0 }, { 3.5, GTR_STROKE_UP, 0 } }, 8 },
    /* PUNK  */ { 4, { { 0.0, GTR_STROKE_DOWN, 0 }, { 0.5, GTR_STROKE_DOWN, 0 },
                       { 1.0, GTR_STROKE_DOWN, 0 }, { 1.5, GTR_STROKE_DOWN, 0 },
                       { 2.0, GTR_STROKE_DOWN, 0 }, { 2.5, GTR_STROKE_DOWN, 0 },
                       { 3.0, GTR_STROKE_DOWN, 0 }, { 3.5, GTR_STROKE_DOWN, 0 } }, 8 },
    /* REGGAE*/ { 4, { { 0.5, GTR_STROKE_UP, 0 }, { 1.5, GTR_STROKE_UP, 0 },
                       { 2.5, GTR_STROKE_UP, 0 }, { 3.5, GTR_STROKE_UP, 0 } }, 4 },
    /* BALLAD*/ { 4, { { 0.0, GTR_STROKE_DOWN, 0 }, { 1.5, GTR_STROKE_UP, 0 },
                       { 2.5, GTR_STROKE_UP, 0 }, { 3.0, GTR_STROKE_DOWN, 0 },
                       { 3.5, GTR_STROKE_UP, 0 } }, 5 },
};

int gtr_expand_rhythm(int rhythm_id, const struct gtr_rhythm *custom,
                      double bar_sec,
                      struct gtr_stroke_sched *out, int out_cap, int *out_len)
{
    if (!(bar_sec > 0.0)) return GTR_E_BARSEC;
    const struct gtr_rhythm *r;
    if (rhythm_id >= GTR_RHYTHM_FOLK && rhythm_id <= GTR_RHYTHM_BALLAD) {
        r = &k_rhythms[rhythm_id];
    } else if (rhythm_id == -1) {
        if (!custom) return GTR_E_ARGS;
        r = custom;
    } else {
        return GTR_E_RHYTHM;
    }
    if (r->beats < 1 || r->n_steps <= 0 || r->n_steps > GTR_MAX_STEPS)
        return GTR_E_RHYTHM;
    const double per_beat = bar_sec / r->beats;
    if (r->n_steps > out_cap) return GTR_E_ARGS;
    for (int i = 0; i < r->n_steps; i++) {
        const struct gtr_rhythm_step *st = &r->steps[i];
        if (!(st->beat >= 0.0) || !(st->beat < r->beats)) return GTR_E_RHYTHM;
        if (st->stroke < GTR_STROKE_DOWN || st->stroke > GTR_STROKE_CHUCK)
            return GTR_E_RHYTHM;
        out[i].stroke = st->stroke;
        out[i].at = st->beat * per_beat;
        out[i].palm_mute = st->palm_mute ? 1 : 0;
    }
    *out_len = r->n_steps;
    return GTR_OK;
}
