/* test_main.c · gtr_part 主机测试（B1.9 M1：自定义节奏数据通路）
   ----------------------------------------------------------------------------
   退出码 0 = 全绿。

   被测对象是固件真源 <仓库>/app/ 下的三个 .c —— 测试目录里不放 .c 副本，
   所以不存在「测的不是刷进设备那份」的问题。
   本文件补上一块一直没人测的空白：gtr_part 只被「编进固件、上真机听」验证过，
   数据层（白键分派、排程队列、Note 队列）从来没有断言保护。

   时钟：gtr_part 是纯事件驱动的（宿主每毫秒调一次 tick），所以测试自带
   虚拟时钟 —— T 只增不减，每 advance() 一步就 tick 一次。这样扫弦时刻
   可以被断言到毫秒，不依赖真实时间，跑多快都一样。
   ========================================================================= */
#include <stdio.h>
#include <string.h>

#include "gtr_part.h"   /* 已 include gtr_sem.h */

static int failures = 0, checks = 0;
static void check(int ok, const char *what)
{
    checks++;
    if (!ok) { failures++; printf("  FAIL  %s\n", what); }
}

/* ---- 虚拟时钟 + fake sink ---- */
#define EV_CAP 4096
static struct ev_rec { uint32_t at; int on; int note; int vel; } ev[EV_CAP];
static int n_ev, ev_ovf;
static uint32_t T, TICK_NOW;

static void sink(uint8_t cmd, uint8_t d1, uint8_t d2)
{
    if (n_ev >= EV_CAP) { ev_ovf++; return; }
    ev[n_ev].at   = TICK_NOW;
    ev[n_ev].on   = (cmd == 0x90) ? 1 : 0;
    ev[n_ev].note = d1;
    ev[n_ev].vel  = d2;
    n_ev++;
}

static void reset_ev(void) { n_ev = 0; ev_ovf = 0; }

static void advance(uint32_t ms)
{
    uint32_t i;
    for (i = 0; i < ms; i++) { T++; TICK_NOW = T; gtr_part_tick(T); }
}

/* ---- 某一击的 gate（ms）：第一个 on 到它之后同 note 的第一个 off ----
   返回 -1 = 没配上对。用来分辨「切音 70ms / 闷音 140ms / 普通下扫 400ms」，
   因为「闷音悄悄退化成正扫」在只看音头数的时候是看不出来的。 */
static int first_gate(uint32_t from)
{
    int i, j;
    for (i = 0; i < n_ev; i++) {
        if (!ev[i].on || ev[i].at < from) continue;
        for (j = i + 1; j < n_ev; j++)
            if (!ev[j].on && ev[j].note == ev[i].note)
                return (int)(ev[j].at - ev[i].at);
        return -1;
    }
    return -1;
}

/* ---- 事件查询 ---- */
static int count_on(uint32_t from, uint32_t to)
{
    int i, n = 0;
    for (i = 0; i < n_ev; i++)
        if (ev[i].on && ev[i].at >= from && ev[i].at < to) n++;
    return n;
}

static int count_off(uint32_t from, uint32_t to)
{
    int i, n = 0;
    for (i = 0; i < n_ev; i++)
        if (!ev[i].on && ev[i].at >= from && ev[i].at < to) n++;
    return n;
}

/* 相邻 note-on 间隔 > 100ms 视为新的一「击」；返回击数，起点写进 out。
   只对击间隔 ≥ 400ms 的用例有意义（内置节奏型最短 500ms 一击）。 */
static int strokes(uint32_t *out, int cap)
{
    int i, n = 0;
    for (i = 0; i < n_ev; i++) {
        if (!ev[i].on) continue;
        if (n == 0 || ev[i].at - out[n - 1] > 100u) {
            if (n < cap) out[n] = ev[i].at;
            n++;
        }
    }
    return n;
}

/* 第一击里某个音的 gate 长度（off_at - on_at），毫秒；找不到返回 -1 */
static int gate_of_first_note(void)
{
    int i, j;
    for (i = 0; i < n_ev; i++) {
        if (!ev[i].on) continue;
        for (j = i + 1; j < n_ev; j++)
            if (!ev[j].on && ev[j].note == ev[i].note)
                return (int)(ev[j].at - ev[i].at);
        return -1;
    }
    return -1;
}

/* 某一击（时间窗口内）的 note-on 音高集合是否等于 expect */
static int notes_match(uint32_t from, uint32_t to, const int *expect, int n)
{
    int i, k, hit = 0;
    for (i = 0; i < n_ev; i++) {
        if (!ev[i].on || ev[i].at < from || ev[i].at >= to) continue;
        hit = 0;
        for (k = 0; k < n; k++) if (ev[i].note == expect[k]) hit = 1;
        if (!hit) return 0;
    }
    return 1;
}

/* 每个 note-on 都在后面有同音高的 note-off（无挂音） */
static int no_stuck_notes(void)
{
    int i, j;
    for (i = 0; i < n_ev; i++) {
        if (!ev[i].on) continue;
        for (j = i + 1; j < n_ev; j++)
            if (!ev[j].on && ev[j].note == ev[i].note) break;
        if (j == n_ev) return 0;
    }
    return 1;
}

/* ---- 构造节奏模式 ---- */
static void mk(struct gtr_rhythm *r, int beats, int n)
{
    memset(r, 0, sizeof *r);
    r->beats = beats;
    r->n_steps = n;
}

static void step(struct gtr_rhythm *r, int i, double beat, int stroke, int palm)
{
    r->steps[i].beat = beat;
    r->steps[i].stroke = stroke;
    r->steps[i].palm_mute = palm;
}

/* 白键序号 → 按键位（与固件 white_keys[16] 一致），用来按"第几个白键"发按键 */
static const int WKEY[16] = { 0, 2, 4, 6, 7, 9, 11, 12, 14, 16, 18, 19, 21, 23, 24, 26 };

/* 默认和弦 0 = F 的金标指法（gtr_part.c gtr_golden[0]），下扫全 6 弦、
   上扫保高音侧 4 弦。 */
static const int F_ALL[6]  = { 41, 48, 53, 57, 60, 65 };
static const int F_TOP4[4] = { 53, 57, 60, 65 };

int main(void)
{
    struct gtr_rhythm r;
    uint32_t g[8];
    int n, i;
    uint32_t base;

    gtr_part_set_sink(sink);
    TICK_NOW = T = 0;
    gtr_part_keys(0, 0, 0);          /* 第一帧只做 seed */
    check(n_ev == 0, "seed 帧不产生任何事件");

    /* ================= S1 没存过模式 ================= */
    check(gtr_part_user_rhythm_steps() == 0, "上电未存模式 → steps=0");
    check(gtr_part_trigger_rhythm(GTR_RHYTHM_USER) == GTR_E_ARGS,
          "未存模式时 trigger(USER) → GTR_E_ARGS");
    reset_ev(); advance(300);
    check(n_ev == 0, "未存模式时 trigger 之后不出声");

    /* ================= S2 非法模式必须被拒 ================= */
    check(gtr_part_set_user_rhythm(NULL) == GTR_E_ARGS, "NULL → GTR_E_ARGS");

    mk(&r, 4, 0);
    check(gtr_part_set_user_rhythm(&r) == GTR_E_RHYTHM, "n_steps=0 → GTR_E_RHYTHM");

    mk(&r, 0, 1); step(&r, 0, 0.0, GTR_STROKE_DOWN, 0);
    check(gtr_part_set_user_rhythm(&r) == GTR_E_RHYTHM, "beats=0 → GTR_E_RHYTHM");

    mk(&r, 4, 1); step(&r, 0, 4.0, GTR_STROKE_DOWN, 0);
    check(gtr_part_set_user_rhythm(&r) == GTR_E_RHYTHM, "beat == beats（越界）→ GTR_E_RHYTHM");

    mk(&r, 4, 1); step(&r, 0, 0.0, 7, 0);
    check(gtr_part_set_user_rhythm(&r) == GTR_E_RHYTHM, "stroke=7（非法）→ GTR_E_RHYTHM");

    mk(&r, 4, 2); step(&r, 0, 1.0, GTR_STROKE_DOWN, 0); step(&r, 1, 1.0, GTR_STROKE_DOWN, 0);
    check(gtr_part_set_user_rhythm(&r) == GTR_E_RHYTHM, "重复 beat → GTR_E_RHYTHM");

    mk(&r, 4, 2); step(&r, 0, 2.0, GTR_STROKE_DOWN, 0); step(&r, 1, 1.0, GTR_STROKE_DOWN, 0);
    check(gtr_part_set_user_rhythm(&r) == GTR_E_RHYTHM, "乱序 beat → GTR_E_RHYTHM");

    check(gtr_part_user_rhythm_steps() == 0, "全被拒之后仍然没有模式");

    /* ================= S3 存一条合法模式 ================= */
    /* 4 拍一小节、2000ms 一小节 → 一拍 500ms：下 下 上 哑 */
    mk(&r, 4, 4);
    step(&r, 0, 0.0, GTR_STROKE_DOWN, 0);
    step(&r, 1, 1.0, GTR_STROKE_DOWN, 0);
    step(&r, 2, 2.0, GTR_STROKE_UP,   0);
    step(&r, 3, 3.0, GTR_STROKE_CHUCK, 0);
    check(gtr_part_set_user_rhythm(&r) == GTR_OK, "合法 4 步模式 → GTR_OK");
    check(gtr_part_user_rhythm_steps() == 4, "steps 记成了 4");

    /* ================= S4 触发并按毫秒核对时刻 ================= */
    reset_ev();
    base = T;
    check(gtr_part_trigger_rhythm(GTR_RHYTHM_USER) == GTR_OK, "trigger(USER) → GTR_OK");
    advance(2200);

    n = strokes(g, 8);
    check(n == 4, "一小节 4 击（0/500/1000/1500ms）");
    for (i = 0; i < n && i < 4; i++) {
        uint32_t want = base + (uint32_t)(i * 500);
        int d = (int)g[i] - (int)want;
        if (d < 0) d = -d;
        check(d <= 2, "击打时刻与 beat×拍长对齐（±2ms）");
    }
    /* 下 6 弦 + 下 6 弦 + 上 4 弦 + 哑 6 弦 = 22 个音头 */
    check(count_on(0, 0xFFFFFFFFu) == 22, "音头总数 22（6+6+4+6）");
    check(count_off(0, 0xFFFFFFFFu) == 22, "note-off 数与 note-on 数相等（不残留）");
    check(no_stuck_notes(), "没有挂音：每个 on 都有对应的 off");
    check(notes_match(g[0], g[0] + 200, F_ALL,  6), "第 1 击音高 = F 和弦全 6 弦");
    check(notes_match(g[2], g[2] + 200, F_TOP4, 4), "第 3 击（上扫）只取高音侧 4 弦");
    check(ev_ovf == 0, "事件记录缓冲没溢出");
    check(gtr_part_dropped_strokes() == 0, "4 击全部排进队列，零丢弃");

    /* ================= S5 闷音步走 muted 路径 ================= */
    /* 同一条下扫，palm=0 → gate 400ms；palm=1 → gate 140ms（gtr_sem 钳位）。
       这是「自定义模式能不能表达闷音」的唯一判据。 */
    mk(&r, 1, 1); step(&r, 0, 0.0, GTR_STROKE_DOWN, 0);
    check(gtr_part_set_user_rhythm(&r) == GTR_OK, "1 步 plain 模式被接受");
    reset_ev();
    check(gtr_part_trigger_rhythm(GTR_RHYTHM_USER) == GTR_OK, "trigger plain");
    advance(900);
    check(gate_of_first_note() == 400, "普通下扫 gate = 400ms");

    mk(&r, 1, 1); step(&r, 0, 0.0, GTR_STROKE_DOWN, 1);
    check(gtr_part_set_user_rhythm(&r) == GTR_OK, "1 步 palm 模式被接受");
    reset_ev();
    check(gtr_part_trigger_rhythm(GTR_RHYTHM_USER) == GTR_OK, "trigger palm");
    advance(900);
    check(gate_of_first_note() == 140, "闷音步 gate = 140ms（palm_mute 落到了 gtr_sem）");

    /* ================= S6 32 步满格：不许丢击 ================= */
    /* M1 之前排程队列只有 16 槽，第 17 击起被静默丢弃 —— 自定义模式必然踩到。 */
    mk(&r, 32, 32);
    for (i = 0; i < 32; i++) step(&r, i, (double)i, GTR_STROKE_DOWN, 0);
    check(gtr_part_set_user_rhythm(&r) == GTR_OK, "32 步满格模式被接受");
    reset_ev();
    check(gtr_part_trigger_rhythm(GTR_RHYTHM_USER) == GTR_OK, "trigger 32 步");
    advance(2600);
    check(gtr_part_dropped_strokes() == 0, "32 击零丢弃（队列已扩到 32）");
    check(count_on(0, 0xFFFFFFFFu) == 192, "32 击 × 6 弦 = 192 个音头，一个不少");
    check(no_stuck_notes(), "32 步模式没有挂音");

    /* ================= S7 内置节奏型键：零行为变化 ================= */
    /* W11 = 白键 bit 18 = FOLK。按下立即起一小节（6 击：下下上上下上 → 30 音头）。 */
    reset_ev();
    gtr_part_keys(0, 1u << 18, T);
    {
        uint32_t key_ms = T;
        advance(1);   /* 节奏型键只排程，真正的音头由 tick 下发（宿主同一轮就会调） */
        check(n_ev > 0 && (int)(ev[0].at - key_ms) <= 2,
              "白键按下后一个 tick 内起拍");
    }
    advance(1899);
    check(count_on(0, 0xFFFFFFFFu) == 30, "FOLK 一小节 30 音头（3 下 3 上）");
    check(gtr_part_dropped_strokes() == 0, "FOLK 零丢弃");

    /* 按住不放 → 小节到点自动续第二小节 */
    advance(400);
    check(count_on(0, 0xFFFFFFFFu) > 30, "按住不放 → 自动续下一小节");
    gtr_part_keys(1u << 18, 0, T);
    advance(2400);

    /* ================= S8 stop_rhythm 不打断当前小节 ================= */
    reset_ev();
    gtr_part_keys(0, 1u << 18, T);
    advance(150);
    gtr_part_stop_rhythm();
    advance(2000);
    check(count_on(0, 0xFFFFFFFFu) == 30, "stop 之后本小节照常弹完，且不再续排");
    gtr_part_keys(1u << 18, 0, T);
    advance(100);

    /* ================= S9 第 5 个内置节奏型（BALLAD，无键位） ================= */
    /* 数据早就有，只是一直没有键。trigger 是它第一次能被听到的入口。 */
    reset_ev();
    check(gtr_part_trigger_rhythm(GTR_RHYTHM_BALLAD) == GTR_OK, "trigger(BALLAD) → GTR_OK");
    advance(2700);
    check(count_on(0, 0xFFFFFFFFu) == 24, "BALLAD 一小节 24 音头（2 下 3 上）");
    check(gtr_part_trigger_rhythm(99) == GTR_E_RHYTHM, "trigger(99) → GTR_E_RHYTHM");
    check(gtr_part_trigger_rhythm(-5) == GTR_E_RHYTHM, "trigger(-5) → GTR_E_RHYTHM");

    /* ================= S10 被拒的设置不动已存的模式 ================= */
    check(gtr_part_user_rhythm_steps() == 32, "被拒的 set 没有清掉已存的 32 步模式");
    mk(&r, 4, 2);
    step(&r, 0, 1.0, GTR_STROKE_DOWN, 0);
    step(&r, 1, 0.0, GTR_STROKE_DOWN, 0);
    check(gtr_part_set_user_rhythm(&r) == GTR_E_RHYTHM, "坏模式被拒");
    check(gtr_part_user_rhythm_steps() == 32, "拒完之后仍是 32 步");


    /* ================= S11 节奏编辑器：动作码与光标 ================= */
    check(gtr_part_strum_clear() == 0, "上电就是全休止，clear 无改动");
    check(gtr_part_strum_cursor() == 0, "光标从第 1 步开始");
    for (i = 0; i < GTR_STRUM_STEPS; i++)
        if (gtr_part_strum_get((int)i) != GTR_STEP_REST) break;
    check(i == GTR_STRUM_STEPS, "16 格初始全是休止");

    check(gtr_part_strum_get(-1) == -1, "get(-1) → -1");
    check(gtr_part_strum_get(GTR_STRUM_STEPS) == -1, "get(16) → -1");
    check(gtr_part_strum_set(GTR_STRUM_STEPS, GTR_STEP_DOWN) == 0, "set(16) 无改动");
    check(gtr_part_strum_set(0, 99) == 0, "非法动作码无改动");

    /* 白键连点：REST → D → U → X → M → REST（空步第一下就成下扫） */
    check(gtr_part_strum_cycle(0) == 1, "空步 cycle 有改动");
    check(gtr_part_strum_get(0) == GTR_STEP_DOWN, "第一下 → 下扫");
    gtr_part_strum_cycle(0);
    check(gtr_part_strum_get(0) == GTR_STEP_UP, "第二下 → 上扫");
    gtr_part_strum_cycle(0);
    check(gtr_part_strum_get(0) == GTR_STEP_CHUCK, "第三下 → 切音");
    gtr_part_strum_cycle(0);
    check(gtr_part_strum_get(0) == GTR_STEP_MUTE, "第四下 → 闷音");
    gtr_part_strum_cycle(0);
    check(gtr_part_strum_get(0) == GTR_STEP_REST, "第五下 → 回到休止");
    check(gtr_part_strum_select(7) == 1, "光标移到第 8 步");
    check(gtr_part_strum_cursor() == 7, "光标 = 7");
    check(gtr_part_strum_select(7) == 0, "重复选同一步不算改动");

    /* ================= S12 提交：16 格 → gtr_rhythm ================= */
    gtr_part_strum_clear();
    for (i = 0; i < GTR_STRUM_STEPS; i += 2)          /* 8 分型：D U D U ... */
        gtr_part_strum_set(i, (i / 2) % 2 == 0 ? GTR_STEP_DOWN : GTR_STEP_UP);
    check(gtr_part_strum_is_dirty() == 1, "改过之后 dirty = 1");
    check(gtr_part_strum_stored() == 0, "还没提交 → stored = 0");
    check(gtr_part_strum_commit() == GTR_OK, "提交 → GTR_OK");
    check(gtr_part_strum_is_dirty() == 0, "提交后 dirty 清 0");
    check(gtr_part_strum_stored() == 1, "提交后 stored = 1");
    check(gtr_part_user_rhythm_steps() == 8, "16 格里 8 个非空 → 模式 8 步");

    reset_ev();
    check(gtr_part_trigger_rhythm(GTR_RHYTHM_USER) == GTR_OK, "提交的模式能直接触发");
    /* 2300ms：最后一击在 1750ms，gate 400ms 的尾巴到 2150 —— 窗口必须盖住它，
       否则 off 数天然少于 on 数，断言会变成在考测试而不是考代码。 */
    advance(2300);
    check(count_on(0, 0xFFFFFFFFu) == 40, "4 下 4 上一小节 40 音头（6*4 + 4*4）");
    check(count_off(0, 0xFFFFFFFFu) == count_on(0, 0xFFFFFFFFu), "off 数 == on 数");

    /* ================= S13 动作真的不同：gate 分辨切音与闷音 ================= */
    /* 「闷音悄悄退化成正扫」只看音头数是看不出来的 —— 这里用 gate 断言。 */
    gtr_part_strum_clear();
    gtr_part_strum_set(0, GTR_STEP_CHUCK);
    check(gtr_part_strum_commit() == GTR_OK, "单步切音提交");
    reset_ev();
    gtr_part_trigger_rhythm(GTR_RHYTHM_USER);
    advance(200);
    check(first_gate(0) >= 55 && first_gate(0) <= 85, "切音 gate ≈ 70ms（非正扫 400ms）");

    gtr_part_strum_clear();
    gtr_part_strum_set(0, GTR_STEP_MUTE);
    check(gtr_part_strum_commit() == GTR_OK, "单步闷音提交");
    reset_ev();
    gtr_part_trigger_rhythm(GTR_RHYTHM_USER);
    advance(250);
    check(first_gate(0) >= 120 && first_gate(0) <= 165, "闷音 gate ≈ 140ms");

    gtr_part_strum_clear();
    gtr_part_strum_set(0, GTR_STEP_DOWN);
    check(gtr_part_strum_commit() == GTR_OK, "单步下扫提交");
    reset_ev();
    gtr_part_trigger_rhythm(GTR_RHYTHM_USER);
    advance(700);
    check(first_gate(0) >= 380 && first_gate(0) <= 430, "普通下扫 gate 仍是 400ms");

    /* ================= S14 试听与撤下 ================= */
    /* 先存一个 8 步模式，验证试听不会把它冲掉 */
    gtr_part_strum_clear();
    for (i = 0; i < GTR_STRUM_STEPS; i += 2)
        gtr_part_strum_set(i, (i / 2) % 2 == 0 ? GTR_STEP_DOWN : GTR_STEP_UP);
    gtr_part_strum_commit();
    check(gtr_part_user_rhythm_steps() == 8, "已存 8 步模式");

    reset_ev();
    check(gtr_part_strum_audition(0) == GTR_OK, "试听第 1 步 → GTR_OK");
    advance(120);
    check(count_on(0, 0xFFFFFFFFu) == 6, "试听一次下扫 6 个音头");
    check(gtr_part_user_rhythm_steps() == 8, "试听没有改写已存的 8 步模式");

    gtr_part_strum_set(1, GTR_STEP_REST);
    check(gtr_part_strum_audition(1) == GTR_E_ARGS, "试听空步 → GTR_E_ARGS");
    check(gtr_part_strum_audition(99) == GTR_E_ARGS, "试听越界 → GTR_E_ARGS");

    /* 全休止提交 = 撤掉自定义模式，退回内置节奏 */
    gtr_part_strum_clear();
    check(gtr_part_strum_commit() == GTR_OK, "全休止提交 → GTR_OK");
    check(gtr_part_user_rhythm_steps() == 0, "全休止 = 撤掉自定义模式");
    check(gtr_part_trigger_rhythm(GTR_RHYTHM_USER) == GTR_E_ARGS,
          "撤掉之后 trigger(USER) → GTR_E_ARGS");
    reset_ev();
    advance(200);
    check(n_ev == 0, "撤掉之后不出声");

    /* ================= S7 单弦拨：前 6 个白键 =================
       这条路径从 B1.2 起就在（gtr_pluck + 同弦连拨先 off），但一直没有断言。
       B1.9 改 gtr_held 的记账方式时正是从这里踩空了一次。 */
    /* ---- S7a 六键 = 六弦，左→右音高递增 ---- */
    reset_ev();
    for (i = 0; i < 6; i++) {
        gtr_part_keys(0, 1u << WKEY[i], T);      /* 按下 */
        gtr_part_keys(1u << WKEY[i], 0, T);      /* 同帧松手：点一下 */
        advance(600);                            /* 让 500ms 的 gate 走完 */
    }
    check(count_on(0, 0xFFFFFFFFu) == 6, "六个白键各拨出一声（6 个音头）");
    {
        int asc = 1, last = -1, distinct = 0, prev = -1;
        for (i = 0; i < n_ev; i++) {
            if (!ev[i].on) continue;
            if (ev[i].note == prev) distinct = 0; else distinct++;
            if (last >= 0 && ev[i].note <= last) asc = 0;
            last = prev = ev[i].note;
        }
        check(asc, "左→右音高严格递增（钢琴逻辑：最左是最低弦）");
        check(distinct == 6, "六根弦音高互不相同");
    }
    { int g = first_gate(0);
      check(g >= 498 && g <= 500, "单弦拨余音 500ms（实际记在 tick 上，容差 1 拍）"); }
    check(no_stuck_notes(), "六声拨完不留挂音");

    /* ---- S7b 同弦连拨：每一声都要先 off 再 on ---- */
    reset_ev();
    for (i = 0; i < 3; i++) {
        gtr_part_keys(0, 1u << WKEY[0], T);
        gtr_part_keys(1u << WKEY[0], 0, T);
        advance(100);
    }
    advance(800);
    check(count_on(0, 0xFFFFFFFFu) == 3, "同弦连拨 3 次 = 3 个音头（一次都不哑）");
    {
        int sounding = 0, bad = 0;
        for (i = 0; i < n_ev; i++) {
            if (ev[i].on) {
                if (sounding) bad = 1;   /* 上一声还没 off 就重按 → 合成器忽略 */
                sounding = 1;
            } else {
                sounding = 0;
            }
        }
        check(!bad, "连拨时每一声都先 off 再 on（合成器才会出音头）");
    }
    {
        int last_on = -1, last_off = -1;
        for (i = n_ev - 1; i >= 0; i--) if (ev[i].on) { last_on = i; break; }
        for (i = last_on + 1; i < n_ev; i++) if (!ev[i].on) { last_off = i; break; }
        int g = (last_off > last_on) ? (int)(ev[last_off].at - ev[last_on].at) : -1;
        check(g >= 498 && g <= 500,
              "最后一声余音完整 500ms（不被上一声残留的 off 截断）");
    }
    check(count_off(0, 0xFFFFFFFFu) == count_on(0, 0xFFFFFFFFu), "off 数 == on 数");
    check(no_stuck_notes(), "连拨之后不留挂音");

    /* ---- S7c 拨一根弦不该影响另一根 ---- */
    reset_ev();
    gtr_part_keys(0, 1u << WKEY[0], T);
    gtr_part_keys(1u << WKEY[0], 0, T);
    advance(50);
    gtr_part_keys(0, 1u << WKEY[5], T);          /* 50ms 后拨最高弦 */
    gtr_part_keys(1u << WKEY[5], 0, T);
    advance(800);
    check(count_on(0, 0xFFFFFFFFu) == 2, "两根弦各一声");
    { int g = first_gate(0);
      check(g >= 498 && g <= 500, "拨高音弦没有提前掐掉低音弦的余音"); }
    check(no_stuck_notes(), "两根弦都不留挂音");

    if (failures == 0)
        printf("结论：全部通过（%d 项断言）。\n", checks);
    else
        printf("结论：%d 过 %d 挂。\n", checks - failures, failures);
    return failures == 0 ? 0 : 1;
}
