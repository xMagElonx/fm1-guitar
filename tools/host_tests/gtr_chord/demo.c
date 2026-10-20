/* demo.c · 打印 11 个默认和弦的生成 voicing 与金标指法对比
   编译：clang --target=wasm32-wasi --sysroot=<sysroot> -O2 -I.. ../gtr_chord.c demo.c -o demo.wasm
   运行：node run_demo.mjs（或任何 WASI 宿主） */
#include <stdio.h>
#include <string.h>
#include "gtr_chord.h"

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

int main(void)
{
    uint8_t v[6];
    char nm[8];
    printf("%-4s %-26s %-26s %s\n", "和弦", "金标指法（开放式把位）", "生成 voicing（根/五音贝斯）", "一致");
    printf("%s\n", "-----------------------------------------------------------------------");
    for (int i = 0; i < 11; i++) {
        gtr_chord_name(GOLDEN[i].root, GOLDEN[i].type, nm, sizeof nm);
        int r = gtr_chord_voicing(GOLDEN[i].root, GOLDEN[i].type, v);
        char g[32], s[32];
        int p = 0;
        for (int k = 0; k < 6; k++) p += snprintf(g + p, sizeof g - (size_t)p, "%d ", GOLDEN[i].notes[k]);
        p = 0;
        if (r == GTR_CHORD_OK)
            for (int k = 0; k < 6; k++) p += snprintf(s + p, sizeof s - (size_t)p, "%d ", v[k]);
        else
            snprintf(s, sizeof s, "ERROR %d", r);
        printf("%-4s %-26s %-26s %s\n", nm, g, s,
               r == GTR_CHORD_OK && memcmp(v, GOLDEN[i].notes, 6) == 0 ? "是" : "");
    }
    printf("\n说明：金标是手工开放式把位（固件默认槽继续播放，零行为变化）；\n"
           "生成 voicing 用于用户自定义和弦（编辑器改 root/type 后实时生成）。\n"
           "两者都是合法 voicing，A/B 试听以耳朵为准。\n");

    /* ---- 编辑器会话轨迹（SEL 页状态机）---- */
    printf("\n编辑器会话轨迹（SEL 页：按黑键选中并试听，KNOB1=根音 KNOB2=类型，ENV/LFO=试听变 min/7/m7）\n");
    printf("%s\n", "---------------------------------------------------------------------------------------------");
    {
        gtr_chord_table_t t;
        gtr_chord_edit_t e;
        uint8_t pk[32];
        char nm[8];
        int gr, gt;

        gtr_chord_table_init(&t);
        gtr_chord_edit_init(&e, &t);

        /* 1. 按槽 2（默认 G） */
        gtr_chord_edit_press(&e, 2);
        gtr_chord_edit_audition(&e, &gr, &gt);
        gtr_chord_name(gr, gt, nm, sizeof nm);
        printf("  按槽 2（黑键）      → 选中并试听 %s\n", nm);

        /* 2. 按住 ENV：试听临时变 min，表不动 */
        gtr_chord_edit_modifier(&e, 1, 0);
        gtr_chord_edit_audition(&e, &gr, &gt);
        gtr_chord_name(gr, gt, nm, sizeof nm);
        printf("  按住 ENV            → 试听变 %s（表里仍是 G，mods_live=%d）\n", nm, e.mods_live);
        gtr_chord_edit_modifier(&e, 0, 0);

        /* 3. 转旋钮改本体：G → F → Fm */
        gtr_chord_edit_knob(&e, 1, -2);
        gtr_chord_table_get(&t, 2, &gr, &gt);
        gtr_chord_name(gr, gt, nm, sizeof nm);
        printf("  KNOB1 -2           → 槽 2 改为 %s（dirty=%d）\n", nm, e.dirty);
        gtr_chord_edit_knob(&e, 2, 1);
        gtr_chord_table_get(&t, 2, &gr, &gt);
        gtr_chord_name(gr, gt, nm, sizeof nm);
        printf("  KNOB2 +1           → 槽 2 改为 %s（dirty=%d）\n", nm, e.dirty);

        /* 4. 松开：停试听，选中保留 */
        gtr_chord_edit_release(&e);
        printf("  松开                → 停试听，选中槽保留（selected=%d, held=%d）\n",
               e.selected, e.held);

        /* 5. 落盘：25 字节 hex */
        gtr_chord_table_pack(&t, pk, sizeof pk);
        printf("  落盘（%d 字节）      → ", (int)GTR_CHORD_PACK_BYTES);
        for (int i = 0; i < (int)GTR_CHORD_PACK_BYTES; i++) printf("%02X ", pk[i]);
        printf("\n                      （'G''C' 魔数 + 版本 + 11 槽 × (root,type)）\n");

        /* 6. 复位 → 再落盘 = 出厂 */
        gtr_chord_edit_press(&e, 2);
        gtr_chord_edit_reset_slot(&e);
        gtr_chord_table_pack(&t, pk, sizeof pk);
        printf("  复位槽 2 后再落盘   → ");
        for (int i = 0; i < (int)GTR_CHORD_PACK_BYTES; i++) printf("%02X ", pk[i]);
        printf("\n                      （与出厂 pack 逐字节相同，测试第 12 节断言）\n");
    }
    return 0;
}
