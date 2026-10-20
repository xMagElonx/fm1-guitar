/* test_main.c · gtr_chord 主机测试（自含黄金断言，无外部考卷）
   退出码 0 = 全绿。金标来源：refs/fm1_open/app/gtr_part.c gtr_chords。 */
#include <stdio.h>
#include <string.h>
#include "gtr_chord.h"

static int failures = 0, checks = 0;
static void check(int ok, const char *what)
{
    checks++;
    if (!ok) { failures++; printf("  FAIL  %s\n", what); }
}

/* ---- 固件金标准和弦（gtr_part.c gtr_chords[11][6]，逐值抄录）---- */
static const struct { int root, type; const char *nm; int notes[6]; } GOLDEN[11] = {
    { 5, GTR_CHORD_MAJ, "F",  {41, 48, 53, 57, 60, 65} },
    { 0, GTR_CHORD_MAJ, "C",  {48, 55, 60, 64, 67, 72} },
    { 7, GTR_CHORD_MAJ, "G",  {43, 47, 50, 55, 59, 67} },
    { 2, GTR_CHORD_MIN, "Dm", {50, 57, 62, 65, 69, 74} },
    { 9, GTR_CHORD_MIN, "Am", {45, 52, 57, 60, 64, 69} },
    { 4, GTR_CHORD_MIN, "Em", {40, 47, 52, 55, 59, 64} },
    { 7, GTR_CHORD_7,   "G7", {43, 47, 50, 55, 59, 65} },
    { 4, GTR_CHORD_7,   "E7", {40, 47, 50, 56, 59, 64} },
    { 2, GTR_CHORD_7,   "D7", {50, 57, 60, 66, 69, 74} },
    {10, GTR_CHORD_MAJ, "Bb", {46, 53, 58, 62, 65, 70} },
    { 9, GTR_CHORD_7,   "A7", {45, 52, 55, 61, 64, 69} },
};

static int pc_of(int note) { int m = note % 12; return m < 0 ? m + 12 : m; }

/* 模型的三个音高集合（pitch classes） */
static void model_pcs(int root, int type, int out[3])
{
    for (int k = 0; k < 3; k++) {
        int m = (root + gtr_chord_tones[type][k]) % 12;
        out[k] = m < 0 ? m + 12 : m;
    }
}

int main(void)
{
    char nm[8];
    uint8_t v[6], p[3];

    /* ---- 1. 音程表完整性 ---- */
    for (int t = 0; t < GTR_CHORD_NTYPES; t++) {
        for (int k = 0; k < 3; k++) {
            check(gtr_chord_tones[t][k] >= 0 && gtr_chord_tones[t][k] <= 11,
                  "音程在 0-11");
        }
        check(gtr_chord_tones[t][0] != gtr_chord_tones[t][1] &&
              gtr_chord_tones[t][1] != gtr_chord_tones[t][2] &&
              gtr_chord_tones[t][0] != gtr_chord_tones[t][2],
              "型内三个音程互不相同");
    }

    /* ---- 2. 默认槽表 ---- */
    for (int i = 0; i < GTR_CHORD_SLOTS; i++) {
        check(gtr_chord_defaults[i].root >= 0 && gtr_chord_defaults[i].root <= 11,
              "默认槽 root 在 0-11");
        check(gtr_chord_defaults[i].type >= 0 && gtr_chord_defaults[i].type < GTR_CHORD_NTYPES,
              "默认槽 type 合法");
    }

    /* ---- 3. 金标等价：模型的音高集合 ⊆ 金标指法的音高集合 ----
       （7 和弦金标含五音、模型是 shell voicing，故是超集而非相等） */
    for (int i = 0; i < 11; i++) {
        int mp[3];
        model_pcs(GOLDEN[i].root, GOLDEN[i].type, mp);
        for (int k = 0; k < 3; k++) {
            int hit = 0;
            for (int s = 0; s < 6; s++)
                if (pc_of(GOLDEN[i].notes[s]) == mp[k]) hit = 1;
            check(hit, "金标包含模型音");
        }
    }

    /* ---- 4. 名字合成：11 个默认槽必须产出金标名字 ---- */
    for (int i = 0; i < 11; i++) {
        int r = gtr_chord_name(GOLDEN[i].root, GOLDEN[i].type, nm, sizeof nm);
        check(r == GTR_CHORD_OK && strcmp(nm, GOLDEN[i].nm) == 0,
              "名字合成");
    }

    /* ---- 5. voicing 合法性：12 根 × 9 型 = 108 组 ---- */
    static const int STRING_OPEN[6] = {40, 45, 50, 55, 59, 64};
    int bass_root_or_fifth = 0, extended = 0, max_span = 0, exact_golden = 0;
    for (int root = 0; root < 12; root++) {
        for (int type = 0; type < GTR_CHORD_NTYPES; type++) {
            int r = gtr_chord_voicing(root, type, v);
            char what[64];
            snprintf(what, sizeof what, "voicing(%d,%d) 返回 OK", root, type);
            check(r == GTR_CHORD_OK, what);
            if (r != GTR_CHORD_OK) continue;
            for (int s = 1; s < 6; s++) {
                snprintf(what, sizeof what, "voicing(%d,%d) 严格升序", root, type);
                check(v[s] > v[s - 1], what);
            }
            int mp[3];
            model_pcs(root, type, mp);
            for (int s = 0; s < 6; s++) {
                snprintf(what, sizeof what, "voicing(%d,%d) 音在和弦内", root, type);
                int ok = 0;
                for (int k = 0; k < 3; k++) if (pc_of(v[s]) == mp[k]) ok = 1;
                check(ok, what);
            }
            /* 统计（部分转为断言）：低音须为根/五音；记录延展与跨度 */
            int fifth = (root + 7) % 12;
            if (pc_of(v[0]) == mp[0] || pc_of(v[0]) == fifth) bass_root_or_fifth++;
            for (int s = 0; s < 6; s++)
                if (v[s] > STRING_OPEN[s] + 5) extended++;
            int span = v[5] - v[0];
            if (span > max_span) max_span = span;
            /* 与金标逐音比对（仅统计）：默认 11 槽的 (root,type) 生成结果 */
            for (int i = 0; i < 11; i++)
                if (GOLDEN[i].root == root && GOLDEN[i].type == type &&
                    memcmp(v, GOLDEN[i].notes, 6) == 0) exact_golden++;
        }
    }
    check(bass_root_or_fifth == 108, "108 组 voicing 低音全部为根音或五音");
    printf("  [统计] 108 组 voicing：低音根/五音 %d 组；+5 品外延展 %d 个音；最大跨度 %d 半音；"
           "逐音复现金标 %d/11（金标是开放式把位，生成器产出根/五音贝斯的密集 voicing，"
           "默认槽在固件侧继续播放金标）\n",
           bass_root_or_fifth, extended, max_span, exact_golden);

    /* ---- 7. pitches：折叠窗口 ---- */
    check(gtr_chord_pitches(4, GTR_CHORD_MIN, 48, p) == GTR_CHORD_OK &&
          p[0] == 52 && p[1] == 55 && p[2] == 59, "Em 三音折叠到 C3 窗口");
    check(gtr_chord_pitches(0, GTR_CHORD_MAJ, 48, p) == GTR_CHORD_OK &&
          p[0] == 48 && p[1] == 52 && p[2] == 55, "C 三音折叠到 C3 窗口");

    /* ---- 8. 错误码 ---- */
    check(gtr_chord_voicing(-1, 0, v) == GTR_CHORD_E_ROOT, "root=-1 报错");
    check(gtr_chord_voicing(12, 0, v) == GTR_CHORD_E_ROOT, "root=12 报错");
    check(gtr_chord_voicing(0, -1, v) == GTR_CHORD_E_TYPE, "type=-1 报错");
    check(gtr_chord_voicing(0, 9, v) == GTR_CHORD_E_TYPE, "type=9 报错");
    check(gtr_chord_voicing(0, 0, NULL) == GTR_CHORD_E_ARGS, "voicing 空指针报错");
    check(gtr_chord_pitches(0, 0, 48, NULL) == GTR_CHORD_E_ARGS, "pitches 空指针报错");
    check(gtr_chord_name(0, 0, NULL, 8) == GTR_CHORD_E_ARGS, "name 空指针报错");
    check(gtr_chord_name(0, 0, nm, 4) == GTR_CHORD_E_ARGS, "name 容量不足报错");
    check(gtr_chord_name(12, 0, nm, 8) == GTR_CHORD_E_ROOT, "name root 越界报错");

    /* ---- 9. 可变和弦表 ---- */
    {
        gtr_chord_table_t t;
        int gr, gt;
        check(gtr_chord_table_init(&t) == GTR_CHORD_OK, "table_init 返回 OK");
        for (int i = 0; i < GTR_CHORD_SLOTS; i++)
            check(t.slots[i].root == gtr_chord_defaults[i].root &&
                  t.slots[i].type == gtr_chord_defaults[i].type,
                  "table_init 填默认槽");
        check(gtr_chord_table_init(NULL) == GTR_CHORD_E_ARGS, "table_init 空指针报错");

        check(gtr_chord_table_set(&t, 3, 1, GTR_CHORD_MIN) == GTR_CHORD_OK, "table_set 合法");
        check(gtr_chord_table_get(&t, 3, &gr, &gt) == GTR_CHORD_OK &&
              gr == 1 && gt == GTR_CHORD_MIN, "table_get 读回");
        check(gtr_chord_table_get(&t, 3, NULL, NULL) == GTR_CHORD_OK, "table_get 输出指针可空");
        check(gtr_chord_table_set(&t, -1, 0, 0) == GTR_CHORD_E_SLOT, "table_set slot=-1 报错");
        check(gtr_chord_table_set(&t, 11, 0, 0) == GTR_CHORD_E_SLOT, "table_set slot=11 报错");
        check(gtr_chord_table_set(&t, 0, -1, 0) == GTR_CHORD_E_ROOT, "table_set root=-1 报错");
        check(gtr_chord_table_set(&t, 0, 12, 0) == GTR_CHORD_E_ROOT, "table_set root=12 报错");
        check(gtr_chord_table_set(&t, 0, 0, -1) == GTR_CHORD_E_TYPE, "table_set type=-1 报错");
        check(gtr_chord_table_set(&t, 0, 0, 9) == GTR_CHORD_E_TYPE, "table_set type=9 报错");
        check(gtr_chord_table_set(NULL, 0, 0, 0) == GTR_CHORD_E_ARGS, "table_set 空指针报错");

        check(gtr_chord_table_is_default(&t, 3) == 0, "改过的槽 is_default=0");
        check(gtr_chord_table_is_default(&t, 4) == 1, "没动的槽 is_default=1");
        check(gtr_chord_table_is_default(&t, -1) == GTR_CHORD_E_SLOT, "is_default slot 越界报错");
        check(gtr_chord_table_is_default(NULL, 0) == GTR_CHORD_E_ARGS, "is_default 空指针报错");

        check(gtr_chord_table_reset_slot(&t, 3) == GTR_CHORD_OK, "reset_slot 返回 OK");
        check(gtr_chord_table_is_default(&t, 3) == 1, "reset 后恢复默认");
        check(gtr_chord_table_reset_slot(&t, 11) == GTR_CHORD_E_SLOT, "reset_slot slot 越界报错");
        check(gtr_chord_table_reset_slot(NULL, 0) == GTR_CHORD_E_ARGS, "reset_slot 空指针报错");
    }

    /* ---- 10. 序列化往返 ---- */
    {
        gtr_chord_table_t t, t2, t3;
        uint8_t pk[40], pk2[40], bad[40], big[64];
        gtr_chord_table_init(&t);
        /* 全表改成互不相同的值，往返必须逐字节一致 */
        for (int i = 0; i < GTR_CHORD_SLOTS; i++)
            gtr_chord_table_set(&t, i, (i * 5 + 3) % 12, (i * 4 + 2) % GTR_CHORD_NTYPES);

        int n = gtr_chord_table_pack(&t, pk, sizeof pk);
        check(n == (int)GTR_CHORD_PACK_BYTES, "pack 返回 25 字节");
        check(pk[0] == 'G' && pk[1] == 'C' && pk[2] == GTR_CHORD_PACK_VERSION,
              "pack 魔数与版本");
        for (int i = 0; i < GTR_CHORD_SLOTS; i++)
            check(pk[3 + i * 2] == t.slots[i].root && pk[4 + i * 2] == t.slots[i].type,
                  "pack 槽位布局");
        check(gtr_chord_table_pack(&t, pk, 24) == GTR_CHORD_E_PACK, "pack 容量不足报错");
        check(gtr_chord_table_pack(NULL, pk, sizeof pk) == GTR_CHORD_E_ARGS, "pack 空指针报错");

        gtr_chord_table_init(&t2);
        check(gtr_chord_table_unpack(&t2, pk, n) == GTR_CHORD_OK, "unpack 返回 OK");
        check(memcmp(t2.slots, t.slots, sizeof t.slots) == 0, "unpack 往返逐槽一致");
        gtr_chord_table_pack(&t2, pk2, sizeof pk2);
        check(memcmp(pk, pk2, GTR_CHORD_PACK_BYTES) == 0, "pack→unpack→pack 字节稳定");

        /* 坏数据：魔数/版本/越界值/短缓冲 */
        memcpy(bad, pk, sizeof bad);
        bad[0] = 'X';
        check(gtr_chord_table_unpack(&t2, bad, n) == GTR_CHORD_E_MAGIC, "unpack 坏魔数报错");
        memcpy(bad, pk, sizeof bad);
        bad[2] = 99;
        check(gtr_chord_table_unpack(&t2, bad, n) == GTR_CHORD_E_VERSION, "unpack 坏版本报错");
        memcpy(bad, pk, sizeof bad);
        bad[3 + 5 * 2] = 12;            /* 槽 5 的 root 越界 */
        check(gtr_chord_table_unpack(&t2, bad, n) == GTR_CHORD_E_ROOT, "unpack root 越界报错");
        memcpy(bad, pk, sizeof bad);
        bad[3 + 7 * 2 + 1] = 9;         /* 槽 7 的 type 越界 */
        check(gtr_chord_table_unpack(&t2, bad, n) == GTR_CHORD_E_TYPE, "unpack type 越界报错");
        check(gtr_chord_table_unpack(&t2, pk, 24) == GTR_CHORD_E_PACK, "unpack 短缓冲报错");
        check(gtr_chord_table_unpack(NULL, pk, n) == GTR_CHORD_E_ARGS, "unpack 空指针报错");

        /* 原子性：坏数据 unpack 失败后，表必须保持原样（两遍扫描的要点） */
        gtr_chord_table_init(&t3);
        gtr_chord_table_set(&t3, 2, 0, GTR_CHORD_MAJ);   /* 槽 2 默认是 G，改成 C */
        memcpy(bad, pk, sizeof bad);
        bad[3 + 9 * 2] = 200;           /* 槽 9 root 越界 */
        check(gtr_chord_table_unpack(&t3, bad, n) == GTR_CHORD_E_ROOT, "坏数据 unpack 报错");
        {
            int gr, gt;
            gtr_chord_table_get(&t3, 2, &gr, &gt);
            check(gr == 0 && gt == GTR_CHORD_MAJ, "unpack 失败：槽 2 保持原样");
            check(gtr_chord_table_is_default(&t3, 3) == 1, "unpack 失败：其余槽没被动过");
        }

        /* len 大于 25（嵌入更大记录）时只读前 25 字节 */
        memcpy(big, pk, sizeof big);
        big[40] = 0xAB; big[41] = 0xCD; /* 后面跟别的字段 */
        gtr_chord_table_init(&t2);
        check(gtr_chord_table_unpack(&t2, big, 64) == GTR_CHORD_OK &&
              memcmp(t2.slots, t.slots, sizeof t.slots) == 0,
              "unpack 容忍尾部多余字节");
    }

    /* ---- 11. 编辑器状态机 ---- */
    {
        gtr_chord_table_t t;
        gtr_chord_edit_t e;
        int gr, gt;
        gtr_chord_table_init(&t);
        check(gtr_chord_edit_init(&e, &t) == GTR_CHORD_OK, "edit_init 返回 OK");
        check(e.selected == 0 && e.held == 0 && e.dirty == 0, "edit_init 初始状态");
        check(gtr_chord_edit_init(NULL, &t) == GTR_CHORD_E_ARGS, "edit_init 空指针报错");
        check(gtr_chord_edit_init(&e, NULL) == GTR_CHORD_E_ARGS, "edit_init 空表报错");

        check(gtr_chord_edit_audition(&e, &gr, &gt) == 0, "未按住不发声");

        check(gtr_chord_edit_press(&e, 6) == GTR_CHORD_OK, "press 槽 6");
        check(e.selected == 6 && e.held == 1, "press 选中并按住");
        check(gtr_chord_edit_audition(&e, &gr, &gt) == 1 &&
              gr == 7 && gt == GTR_CHORD_7, "试听槽 6 = G7");
        check(gtr_chord_edit_press(&e, -1) == GTR_CHORD_E_SLOT, "press slot=-1 报错");
        check(gtr_chord_edit_press(&e, 11) == GTR_CHORD_E_SLOT, "press slot=11 报错");
        check(e.selected == 6, "press 失败不改选中");

        check(gtr_chord_edit_release(&e) == GTR_CHORD_OK, "release 返回 OK");
        check(e.held == 0 && e.selected == 6, "松开停试听、保留选中");
        check(gtr_chord_edit_audition(&e, &gr, &gt) == 0, "松开后不发声");

        /* 旋钮：编辑松开后保留的选中槽 */
        check(gtr_chord_edit_knob(&e, 1, -2) == GTR_CHORD_OK, "KNOB1 -2");
        check(gtr_chord_table_get(&t, 6, &gr, &gt) == GTR_CHORD_OK &&
              gr == 5 && gt == GTR_CHORD_7, "G7 根音 7→5（F7）");
        check(e.dirty == 1, "改动置脏");
        check(gtr_chord_edit_knob(&e, 2, 1) == GTR_CHORD_OK, "KNOB2 +1");
        check(gtr_chord_table_get(&t, 6, &gr, &gt) == GTR_CHORD_OK &&
              gr == 5 && gt == GTR_CHORD_MAJ7, "F7 类型 → FM7");
        check(gtr_chord_edit_knob(&e, 0, 1) == GTR_CHORD_E_ARGS, "knob 编号 0 报错");
        check(gtr_chord_edit_knob(&e, 3, 1) == GTR_CHORD_E_ARGS, "knob 编号 3 报错");

        /* 钳制：边界不外溢 */
        check(gtr_chord_edit_knob(&e, 1, -99) == GTR_CHORD_OK, "KNOB1 大幅负移");
        check(gtr_chord_table_get(&t, 6, &gr, &gt) == GTR_CHORD_OK && gr == 0,
              "root 钳制在 0");
        check(gtr_chord_edit_knob(&e, 1, 99) == GTR_CHORD_OK, "KNOB1 大幅正移");
        check(gtr_chord_table_get(&t, 6, &gr, &gt) == GTR_CHORD_OK && gr == 11,
              "root 钳制在 11");
        check(gtr_chord_edit_knob(&e, 2, -99) == GTR_CHORD_OK, "KNOB2 大幅负移");
        check(gtr_chord_table_get(&t, 6, &gr, &gt) == GTR_CHORD_OK && gt == 0,
              "type 钳制在 0");
        check(gtr_chord_edit_knob(&e, 2, 99) == GTR_CHORD_OK, "KNOB2 大幅正移");
        check(gtr_chord_table_get(&t, 6, &gr, &gt) == GTR_CHORD_OK &&
              gt == GTR_CHORD_NTYPES - 1, "type 钳制在 8");

        /* 边界空转不置脏（flash 写次数有限，不能拿空写耗寿命） */
        check(gtr_chord_edit_saved(&e) == GTR_CHORD_OK && e.dirty == 0, "saved 清脏");
        check(gtr_chord_edit_knob(&e, 1, 5) == GTR_CHORD_OK, "root 已在 11，再 +5");
        check(gtr_chord_table_get(&t, 6, &gr, &gt) == GTR_CHORD_OK && gr == 11,
              "root 仍在 11（钳制）");
        check(e.dirty == 0, "边界空转不置脏");

        /* 修饰键：试听变体，不动表（此时槽 6 = (11, add9)） */
        gtr_chord_edit_press(&e, 6);
        check(gtr_chord_edit_modifier(&e, 1, 0) == GTR_CHORD_OK, "按住 ENV");
        check(e.mods_live == 1, "修饰生效");
        check(gtr_chord_edit_audition(&e, &gr, &gt) == 1 && gt == GTR_CHORD_MIN,
              "ENV → 试听变 min");
        check(gtr_chord_table_get(&t, 6, &gr, &gt) == GTR_CHORD_OK &&
              gt == GTR_CHORD_ADD9, "表里的类型没被动");
        check(gtr_chord_edit_modifier(&e, 0, 1) == GTR_CHORD_OK, "改按 LFO");
        check(gtr_chord_edit_audition(&e, &gr, &gt) == 1 && gt == GTR_CHORD_7,
              "LFO → 试听变 7");
        check(gtr_chord_edit_modifier(&e, 1, 1) == GTR_CHORD_OK, "同按 ENV+LFO");
        check(gtr_chord_edit_audition(&e, &gr, &gt) == 1 && gt == GTR_CHORD_M7,
              "同按 → 试听变 m7");
        check(gtr_chord_edit_modifier(&e, 0, 0) == GTR_CHORD_OK, "全松开");
        check(e.mods_live == 0, "修饰退场");
        check(gtr_chord_edit_audition(&e, &gr, &gt) == 1 && gt == GTR_CHORD_ADD9,
              "试听回本体");

        /* 改进一：先按修饰键再按槽，变体立即生效 */
        gtr_chord_edit_modifier(&e, 1, 0);   /* ENV 按住不动 */
        gtr_chord_edit_press(&e, 2);         /* 按槽 2（默认 G） */
        check(gtr_chord_edit_audition(&e, &gr, &gt) == 1 &&
              gr == 7 && gt == GTR_CHORD_MIN, "按住 ENV 按槽 → 立刻 Gm");
        gtr_chord_edit_modifier(&e, 0, 0);

        /* 改进二：转旋钮后修饰变体退场，听到的即编辑的 */
        gtr_chord_edit_press(&e, 2);
        gtr_chord_edit_modifier(&e, 0, 1);   /* LFO */
        check(gtr_chord_edit_audition(&e, &gr, &gt) == 1 && gt == GTR_CHORD_7,
              "LFO 按住 → 试听 G7");
        gtr_chord_edit_knob(&e, 2, 1);       /* 类型 MAJ→MIN */
        check(gtr_chord_table_get(&t, 2, &gr, &gt) == GTR_CHORD_OK && gt == GTR_CHORD_MIN,
              "旋钮改的是本体（G→Gm）");
        check(gtr_chord_edit_audition(&e, &gr, &gt) == 1 && gt == GTR_CHORD_MIN,
              "转旋钮后试听回本体（不再是 LFO 变体）");
        check(e.mods_live == 0, "mods_live 已清");
        gtr_chord_edit_modifier(&e, 0, 0);

        /* 复位选中槽 */
        gtr_chord_edit_press(&e, 2);
        check(gtr_chord_edit_reset_slot(&e) == GTR_CHORD_OK, "reset_slot 返回 OK");
        check(gtr_chord_table_is_default(&t, 2) == 1, "槽 2 恢复默认 G");
        check(e.dirty == 1, "复位真改了 → 置脏");
        check(gtr_chord_edit_saved(&e) == GTR_CHORD_OK && e.dirty == 0, "落盘清脏");
        check(gtr_chord_edit_reset_slot(&e) == GTR_CHORD_OK, "复位已是默认的槽");
        check(e.dirty == 0, "复位默认槽不置脏");

        /* 错误分支 */
        check(gtr_chord_edit_audition(NULL, &gr, &gt) == GTR_CHORD_E_ARGS, "audition 空指针报错");
        check(gtr_chord_edit_audition(&e, NULL, NULL) == 1, "audition 输出指针可空");
        check(gtr_chord_edit_release(NULL) == GTR_CHORD_E_ARGS, "release 空指针报错");
        check(gtr_chord_edit_knob(NULL, 1, 1) == GTR_CHORD_E_ARGS, "knob 空指针报错");
        check(gtr_chord_edit_modifier(NULL, 1, 0) == GTR_CHORD_E_ARGS, "modifier 空指针报错");
        check(gtr_chord_edit_saved(NULL) == GTR_CHORD_E_ARGS, "saved 空指针报错");
        check(gtr_chord_edit_reset_slot(NULL) == GTR_CHORD_E_ARGS, "reset_slot 空指针报错");
    }

    /* ---- 12. 集成场景：编辑 → 落盘 → 重开 → 复位 ---- */
    {
        gtr_chord_table_t t, t2, t0;
        gtr_chord_edit_t e;
        uint8_t pk[32], pk2[32], pk0[32];
        int gr, gt;
        gtr_chord_table_init(&t);
        gtr_chord_edit_init(&e, &t);

        /* 会话：选中槽 2（G），KNOB1 -2 改成 F，KNOB2 +1 改成 Fm */
        gtr_chord_edit_press(&e, 2);
        gtr_chord_edit_knob(&e, 1, -2);   /* G(7) → F(5) */
        gtr_chord_edit_knob(&e, 2, 1);    /* MAJ → MIN */
        check(e.dirty == 1, "会话后有未落盘修改");
        gtr_chord_table_get(&t, 2, &gr, &gt);
        check(gr == 5 && gt == GTR_CHORD_MIN, "槽 2 = Fm");

        /* 落盘 → 全新表重开（模拟断电重进） */
        check(gtr_chord_table_pack(&t, pk, sizeof pk) == (int)GTR_CHORD_PACK_BYTES, "落盘");
        gtr_chord_edit_saved(&e);
        check(e.dirty == 0, "落盘后清脏");
        gtr_chord_table_init(&t2);
        check(gtr_chord_table_unpack(&t2, pk, GTR_CHORD_PACK_BYTES) == GTR_CHORD_OK, "重开读档");
        gtr_chord_table_get(&t2, 2, &gr, &gt);
        check(gr == 5 && gt == GTR_CHORD_MIN, "重开后槽 2 仍是 Fm");
        check(gtr_chord_table_is_default(&t2, 2) == 0, "重开后该槽标记为改过");
        check(gtr_chord_table_is_default(&t2, 5) == 1, "重开后没动过的槽仍是默认");

        /* 复位 → 再落盘，必须与出厂 pack 逐字节相同 */
        gtr_chord_edit_init(&e, &t2);
        gtr_chord_edit_press(&e, 2);
        gtr_chord_edit_reset_slot(&e);
        check(gtr_chord_table_is_default(&t2, 2) == 1, "复位后恢复默认 G");
        gtr_chord_table_init(&t0);
        gtr_chord_table_pack(&t0, pk0, sizeof pk0);
        gtr_chord_table_pack(&t2, pk2, sizeof pk2);
        check(memcmp(pk0, pk2, GTR_CHORD_PACK_BYTES) == 0,
              "全部复位后落盘 = 出厂 pack（逐字节相同）");
    }

    /* ---- 13. 紧凑差分序列化（8 字节，project.c 存储用）---- */
    {
        gtr_chord_table_t t, t2;
        uint8_t d[GTR_CHORD_DIFF_BYTES], d2[GTR_CHORD_DIFF_BYTES];
        int gr, gt;

        /* 默认表 → pack：位图=0，数据全 0xFF */
        gtr_chord_table_init(&t);
        check(gtr_chord_table_pack_diff(&t, d, sizeof d) == (int)GTR_CHORD_DIFF_BYTES, "diff: 默认表 pack");
        check(d[0] == 0 && d[1] == 0, "diff: 默认表位图=0");
        for (int i = 2; i < (int)GTR_CHORD_DIFF_BYTES; i++)
            check(d[i] == 0xff, "diff: 默认表数据=0xFF");

        /* 改 1 个槽 → pack → unpack 往返 */
        gtr_chord_table_set(&t, 2, 5, GTR_CHORD_MIN);  /* G → Fm */
        check(gtr_chord_table_pack_diff(&t, d, sizeof d) == (int)GTR_CHORD_DIFF_BYTES, "diff: 1 槽 pack");
        check(d[0] == 0x04 && d[1] == 0x00, "diff: 1 槽位图 bit2");
        check(d[2] == ((5 << 4) | GTR_CHORD_MIN), "diff: 1 槽数据 (5,min)");
        check(d[3] == 0xff, "diff: 1 槽剩余填充 0xFF");

        gtr_chord_table_init(&t2);
        check(gtr_chord_table_unpack_diff(&t2, d, GTR_CHORD_DIFF_BYTES) == GTR_CHORD_OK, "diff: 1 槽 unpack");
        gtr_chord_table_get(&t2, 2, &gr, &gt);
        check(gr == 5 && gt == GTR_CHORD_MIN, "diff: 1 槽往返值正确");
        check(gtr_chord_table_is_default(&t2, 0) == 1, "diff: 1 槽往返其他槽默认");

        /* 改 6 个槽（上限）→ pack → unpack 往返 */
        gtr_chord_table_init(&t);
        gtr_chord_table_set(&t, 0, 1, GTR_CHORD_7);
        gtr_chord_table_set(&t, 3, 3, GTR_CHORD_MAJ7);
        gtr_chord_table_set(&t, 5, 6, GTR_CHORD_SUS4);
        gtr_chord_table_set(&t, 7, 8, GTR_CHORD_DIM);
        gtr_chord_table_set(&t, 9, 11, GTR_CHORD_AUG);
        gtr_chord_table_set(&t, 10, 0, GTR_CHORD_ADD9);
        check(gtr_chord_table_pack_diff(&t, d, sizeof d) == (int)GTR_CHORD_DIFF_BYTES, "diff: 6 槽 pack");
        gtr_chord_table_init(&t2);
        check(gtr_chord_table_unpack_diff(&t2, d, GTR_CHORD_DIFF_BYTES) == GTR_CHORD_OK, "diff: 6 槽 unpack");
        for (int s = 0; s < GTR_CHORD_SLOTS; s++) {
            int r1, t1, r2, t2v;
            gtr_chord_table_get(&t, s, &r1, &t1);
            gtr_chord_table_get(&t2, s, &r2, &t2v);
            check(r1 == r2 && t1 == t2v, "diff: 6 槽往返逐槽一致");
        }

        /* 改 7 个槽（超限）→ pack 报错 */
        gtr_chord_table_set(&t, 1, 2, GTR_CHORD_M7);
        check(gtr_chord_table_pack_diff(&t, d, sizeof d) == GTR_CHORD_E_DIFF, "diff: 7 槽超限报错");

        /* 全 0xFF（擦除态）→ unpack 得默认表 */
        memset(d, 0xff, sizeof d);
        gtr_chord_table_init(&t2);
        gtr_chord_table_set(&t2, 0, 1, 1);  /* 先弄脏，验证 unpack 会重置 */
        check(gtr_chord_table_unpack_diff(&t2, d, GTR_CHORD_DIFF_BYTES) == GTR_CHORD_OK, "diff: 擦除态 unpack OK");
        check(gtr_chord_table_is_default(&t2, 0) == 1, "diff: 擦除态 → 默认表");

        /* 坏位图（bit 11-15 有置位）→ 报错 */
        d[0] = 0x00; d[1] = 0xF8;  /* bits 11-15 set */
        check(gtr_chord_table_unpack_diff(&t2, d, GTR_CHORD_DIFF_BYTES) == GTR_CHORD_E_DIFF, "diff: 坏位图报错");

        /* 坏数据（root > 11）→ 报错 */
        d[0] = 0x01; d[1] = 0x00; d[2] = 0xF3;  /* root=15, type=3 */
        check(gtr_chord_table_unpack_diff(&t2, d, GTR_CHORD_DIFF_BYTES) == GTR_CHORD_E_ROOT, "diff: root 越界报错");

        /* 坏数据（type >= 9）→ 报错 */
        d[0] = 0x01; d[1] = 0x00; d[2] = 0x5F;  /* root=5, type=15 */
        check(gtr_chord_table_unpack_diff(&t2, d, GTR_CHORD_DIFF_BYTES) == GTR_CHORD_E_TYPE, "diff: type 越界报错");

        /* 位数超上限（位图声称 7 个槽）→ 报错 */
        d[0] = 0x7F; d[1] = 0x00;  /* bits 0-6 set = 7 slots */
        for (int i = 2; i < (int)GTR_CHORD_DIFF_BYTES; i++) d[i] = 0x00;
        check(gtr_chord_table_unpack_diff(&t2, d, GTR_CHORD_DIFF_BYTES) == GTR_CHORD_E_DIFF, "diff: 位数超限报错");

        /* pack → unpack → pack 字节稳定 */
        gtr_chord_table_init(&t);
        gtr_chord_table_set(&t, 4, 7, GTR_CHORD_7);
        gtr_chord_table_set(&t, 8, 0, GTR_CHORD_MAJ);
        check(gtr_chord_table_pack_diff(&t, d, sizeof d) == (int)GTR_CHORD_DIFF_BYTES, "diff: 往返 pack 1");
        check(gtr_chord_table_unpack_diff(&t2, d, GTR_CHORD_DIFF_BYTES) == GTR_CHORD_OK, "diff: 往返 unpack");
        check(gtr_chord_table_pack_diff(&t2, d2, sizeof d2) == (int)GTR_CHORD_DIFF_BYTES, "diff: 往返 pack 2");
        check(memcmp(d, d2, GTR_CHORD_DIFF_BYTES) == 0, "diff: pack→unpack→pack 字节稳定");

        /* 缓冲区太小 → 报错 */
        check(gtr_chord_table_pack_diff(&t, d, 4) == GTR_CHORD_E_PACK, "diff: pack 缓冲太小");
        check(gtr_chord_table_unpack_diff(&t2, d, 4) == GTR_CHORD_E_PACK, "diff: unpack 长度不足");

        /* 空指针 → 报错 */
        check(gtr_chord_table_pack_diff(NULL, d, sizeof d) == GTR_CHORD_E_ARGS, "diff: pack 空指针");
        check(gtr_chord_table_unpack_diff(NULL, d, GTR_CHORD_DIFF_BYTES) == GTR_CHORD_E_ARGS, "diff: unpack 空指针");
    }

    printf("gtr_chord: %d 项检查，%d 项失败\n", checks, failures);
    return failures ? 1 : 0;
}
