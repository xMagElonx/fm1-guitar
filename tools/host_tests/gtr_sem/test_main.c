/* test_main.c · gtr_sem 主机对拍测试
   读 golden.tsv（JS 参考实现生成的考卷），逐案运行 gtr_sem 并核对。
   退出码 0 = 全绿。case 清单与 golden_gen.mjs 必须保持同步（改那个 = 改考卷）。 */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "gtr_sem.h"

#define MAXN 64

static int failures = 0, checks = 0;
static void check(int ok, const char *what)
{
    checks++;
    if (!ok) { failures++; printf("  FAIL  %s\n", what); }
}

static const int A_NOTES[5]  = {45, 52, 57, 61, 64};
static const int E6_NOTES[6] = {40, 47, 52, 56, 59, 64};

/* ---- golden 存储 ---- */
struct s_case { int id, n; struct gtr_event ev[MAXN]; };
struct r_case { int id, n; struct gtr_stroke_sched st[MAXN]; };
static struct s_case s_cases[16]; static int n_s;
static struct r_case r_cases[8];  static int n_r;
static struct { unsigned base, index; long long want; } b_cases[8]; static int n_b;
static struct { unsigned seed; long long o[3]; } m_cases[8]; static int n_m;

static int run_case(int id, struct gtr_event *out, int *len)
{
    struct gtr_strum_opts o;
    memset(&o, 0, sizeof o);
    switch (id) {
    case 1: o.pattern_id = GTR_STROKE_DOWN;  o.spread = 0.025; o.gate = 1.2;
            return gtr_strum(A_NOTES, 5, &o, out, MAXN, len);
    case 2: o.pattern_id = GTR_STROKE_UP;    o.spread = 0.025; o.gate = 1.2;
            return gtr_strum(A_NOTES, 5, &o, out, MAXN, len);
    case 3: o.pattern_id = GTR_STROKE_CHUCK; o.spread = 0.025; o.gate = 1.2;
            return gtr_strum(E6_NOTES, 6, &o, out, MAXN, len);
    case 4: o.pattern_id = GTR_STROKE_DOWN;  o.spread = 0.025; o.gate = 1.2;
            o.palm_mute = 1; o.velHigh = 90; o.velLow = 60;
            return gtr_strum(A_NOTES, 5, &o, out, MAXN, len);
    case 5: o.pattern_id = GTR_STROKE_DOWN;  o.spread = 0.025; o.gate = 1.2;
            o.string_budget = 1; o.velHigh = 80; o.velLow = 60;
            return gtr_strum(A_NOTES, 5, &o, out, MAXN, len);
    case 6: o.pattern_id = -1; o.custom_pattern.order = 1;
            o.custom_pattern.velHigh = 100; o.custom_pattern.velLow = 70;
            o.custom_pattern.strings = 3; o.spread = 0.02; o.gate = 0.9;
            return gtr_strum(E6_NOTES, 6, &o, out, MAXN, len);
    case 101: { struct gtr_event base[MAXN]; int bl = 0;
              o.pattern_id = GTR_STROKE_DOWN; o.spread = 0.025; o.gate = 1.2;
              gtr_strum(A_NOTES, 5, &o, base, MAXN, &bl);
              *len = bl;
              return gtr_humanize(base, bl, 12345, 6.0, 10.0, out, MAXN); }
    case 102: { struct gtr_event base[MAXN]; int bl = 0;
              o.pattern_id = GTR_STROKE_DOWN; o.spread = 0.025; o.gate = 1.2;
              gtr_strum(A_NOTES, 5, &o, base, MAXN, &bl);
              *len = bl;
              return gtr_humanize(base, bl, 99, 0.0, 10.0, out, MAXN); }
    case 103: { struct gtr_event base[MAXN]; int bl = 0;
              o.pattern_id = GTR_STROKE_CHUCK; o.spread = 0.025; o.gate = 1.2;
              gtr_strum(E6_NOTES, 6, &o, base, MAXN, &bl);
              *len = bl;
              return gtr_humanize(base, bl, 7, 12.0, 0.0, out, MAXN); }
    }
    return GTR_E_ARGS;
}

int main(void)
{
    FILE *f = fopen("golden.tsv", "r");
    if (!f) { printf("golden.tsv 打不开\n"); return 2; }
    char line[512];
    while (fgets(line, sizeof line, f)) {
        char kind; int id, n;
        if (sscanf(line, "%c %d %d", &kind, &id, &n) < 2) continue;
        if ((kind == 'S' || kind == 'H') && n <= MAXN) {
            struct s_case *c = &s_cases[n_s++];
            c->id = id; c->n = n;
            for (int i = 0; i < n; i++) {
                if (!fgets(line, sizeof line, f)) break;
                long long on = 0, off = 0;
                char ep;
                sscanf(line, " %c %d %d %lld %lld %d %d", &ep, &c->ev[i].note,
                       &c->ev[i].velocity, &on, &off,
                       &c->ev[i].string, &c->ev[i].stroke);
                c->ev[i].tOn = on / 1e6;
                c->ev[i].tOff = off / 1e6;
            }
        } else if (kind == 'R' && n <= MAXN) {
            struct r_case *c = &r_cases[n_r++];
            c->id = id; c->n = n;
            for (int i = 0; i < n; i++) {
                if (!fgets(line, sizeof line, f)) break;
                long long at = 0; int pm = 0;
                sscanf(line, " T %d %lld %d", &c->st[i].stroke, &at, &pm);
                c->st[i].at = at / 1e6;
                c->st[i].palm_mute = pm;
            }
        } else if (kind == 'P') {
            int note = 0, vel = 0, sidx = 0; long long on = 0, off = 0; int stroke = 0;
            if (sscanf(line, " P %d %d %lld %lld %d %d",
                       &note, &vel, &on, &off, &sidx, &stroke) >= 4) {
                struct gtr_event pe; int rc;
                char what[96];
                rc = gtr_pluck(A_NOTES, 5, sidx, vel, &pe);
                snprintf(what, sizeof what, "P pluck rc");
                check(rc == GTR_OK, what);
                check(pe.note == note && pe.velocity == vel &&
                      (long long)llround(pe.tOn * 1e6) == on &&
                      (long long)llround(pe.tOff * 1e6) == off &&
                      pe.string == sidx && pe.stroke == stroke,
                      "P pluck 逐字段一致");
            }
        } else if (kind == 'B' && n_b < 8) {
            sscanf(line, "B %u %u %lld", &b_cases[n_b].base,
                   &b_cases[n_b].index, &b_cases[n_b].want);
            n_b++;
        } else if (kind == 'M' && n_m < 8) {
            sscanf(line, "M %u %lld %lld %lld", &m_cases[n_m].seed,
                   &m_cases[n_m].o[0], &m_cases[n_m].o[1], &m_cases[n_m].o[2]);
            n_m++;
        }
    }
    fclose(f);

    /* ---- S/H 案 ---- */
    for (int ci = 0; ci < n_s; ci++) {
        struct s_case *c = &s_cases[ci];
        struct gtr_event got[MAXN]; int len = 0;
        char what[96];
        int rc = run_case(c->id, got, &len);
        snprintf(what, sizeof what, "case %d rc", c->id);
        check(rc == GTR_OK, what);
        if (rc != GTR_OK) continue;
        snprintf(what, sizeof what, "case %d 事件数 %d==%d", c->id, len, c->n);
        check(len == c->n, what);
        int bad = -1;
        for (int i = 0; i < len && i < c->n; i++) {
            if (got[i].note != c->ev[i].note ||
                got[i].velocity != c->ev[i].velocity ||
                (long long)llround(got[i].tOn * 1e6) !=
                (long long)llround(c->ev[i].tOn * 1e6) ||
                (long long)llround(got[i].tOff * 1e6) !=
                (long long)llround(c->ev[i].tOff * 1e6) ||
                got[i].string != c->ev[i].string ||
                got[i].stroke != c->ev[i].stroke) { bad = i; break; }
        }
        snprintf(what, sizeof what, "case %d 逐字段一致", c->id);
        check(bad < 0, what);
    }

    /* ---- R 案 ---- */
    for (int ci = 0; ci < n_r; ci++) {
        struct r_case *c = &r_cases[ci];
        struct gtr_stroke_sched sched[MAXN]; int len = 0;
        char what[96];
        int rc = gtr_expand_rhythm(c->id == 201 ? GTR_RHYTHM_FOLK : GTR_RHYTHM_REGGAE,
                                   NULL, c->id == 201 ? 2.0 : 1.8,
                                   sched, MAXN, &len);
        snprintf(what, sizeof what, "R case %d rc", c->id);
        check(rc == GTR_OK, what);
        if (rc != GTR_OK) continue;
        snprintf(what, sizeof what, "R case %d 行数 %d==%d", c->id, len, c->n);
        check(len == c->n, what);
        int bad = -1;
        for (int i = 0; i < len && i < c->n; i++) {
            if (sched[i].stroke != c->st[i].stroke ||
                (long long)llround(sched[i].at * 1e6) !=
                (long long)llround(c->st[i].at * 1e6) ||
                sched[i].palm_mute != c->st[i].palm_mute) { bad = i; break; }
        }
        snprintf(what, sizeof what, "R case %d 逐字段一致", c->id);
        check(bad < 0, what);
    }

    /* ---- B 案 ---- */
    for (int ci = 0; ci < n_b; ci++) {
        char what[96];
        snprintf(what, sizeof what, "barSeed(%u,%u)", b_cases[ci].base, b_cases[ci].index);
        check(gtr_bar_seed(b_cases[ci].base, b_cases[ci].index) ==
              (unsigned)b_cases[ci].want, what);
    }

    /* ---- M 案：mulberry32 位级对拍 ---- */
    for (int ci = 0; ci < n_m; ci++) {
        uint32_t st = m_cases[ci].seed;
        char what[96];
        for (int k = 0; k < 3; k++) {
            double x = gtr_mulberry32_next(&st) / 4294967296.0;
            long long got_us = (long long)llround(x * 1e9);
            snprintf(what, sizeof what, "mulberry(%u)[%d]", m_cases[ci].seed, k);
            check(got_us == m_cases[ci].o[k], what);
        }
    }

    /* ---- C 侧独有：错误码断言（JS 抛错 = C 返回负码）---- */
    struct gtr_event ev[MAXN]; int len = 0;
    const int bad_notes[3] = {45, 45, 57};
    struct gtr_strum_opts o; memset(&o, 0, sizeof o);
    o.pattern_id = GTR_STROKE_DOWN; o.spread = 0.025; o.gate = 1.2;
    check(gtr_strum(bad_notes, 3, &o, ev, MAXN, &len) == GTR_E_NOTES,
          "非升序 notes → GTR_E_NOTES");
    check(gtr_strum(A_NOTES, 1, &o, ev, MAXN, &len) == GTR_E_NOTES,
          "单音 → GTR_E_NOTES");
    o.spread = 0.0;
    check(gtr_strum(A_NOTES, 5, &o, ev, MAXN, &len) == GTR_E_SPREAD,
          "spread=0 → GTR_E_SPREAD");
    o.spread = 0.025;
    check(gtr_humanize(ev, 5, 0, 6.0, 10.0, ev, MAXN) == GTR_E_SEED,
          "seed=0 → GTR_E_SEED");

    if (failures == 0)
        printf("结论：全部通过（%d 项断言）。\n", checks);
    else
        printf("结论：%d 过 %d 挂。\n", checks - failures, failures);
    return failures == 0 ? 0 : 1;
}
