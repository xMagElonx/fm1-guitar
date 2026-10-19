/* gtr_chord.c · 实现（见 gtr_chord.h 的模型与纪律说明） */
#include "gtr_chord.h"

/* ---------------------------------------------------------------------------
   音程表与名称表 —— 移植自 FoMni（github.com/charlesvestal/fm1-fomni，
   GPL-3.0-only，© Charles Vestal）：firmware/src/dsp/omni.c 的 OM_TYPE_TONES /
   OM_TYPE_NAME / OM_NOTE_NAME 逐值复制。FoMni 的和弦模型又源自 Chordian
   （github.com/Jan125/pb.chordian，CC0-1.0）的 OM-84 传统。
   --------------------------------------------------------------------------- */
const int8_t gtr_chord_tones[GTR_CHORD_NTYPES][3] = {
    {0, 4, 7}, {0, 3, 7}, {0, 4, 10}, {0, 4, 11}, {0, 3, 10},
    {0, 3, 6}, {0, 4, 8}, {0, 5, 7}, {0, 4, 2}
};
const char *const gtr_chord_type_name[GTR_CHORD_NTYPES] = {
    "", "m", "7", "M7", "m7", "dim", "aug", "sus4", "add9"
};
const char *const gtr_chord_note_name[12] = {
    "C", "C#", "D", "Eb", "E", "F", "F#", "G", "Ab", "A", "Bb", "B"
};

/* 默认 11 槽：与 refs/fm1_open/app/gtr_part.c 的 gtr_chords 金标一一对应。
   每个金标准和弦的音高集合都经测试验证是该 (root, type) 的超集
   （tests/test_main.c 断言 3）。 */
const gtr_chord_slot_t gtr_chord_defaults[GTR_CHORD_SLOTS] = {
    { 5, GTR_CHORD_MAJ },   /* 0  F   大横按 1 品   */
    { 0, GTR_CHORD_MAJ },   /* 1  C   大横按 8 品   */
    { 7, GTR_CHORD_MAJ },   /* 2  G   开放 320003   */
    { 2, GTR_CHORD_MIN },   /* 3  Dm  大横按 5 品   */
    { 9, GTR_CHORD_MIN },   /* 4  Am  开放 x02210   */
    { 4, GTR_CHORD_MIN },   /* 5  Em  开放 022000   */
    { 7, GTR_CHORD_7   },   /* 6  G7  开放 320001   */
    { 4, GTR_CHORD_7   },   /* 7  E7  开放 020100   */
    { 2, GTR_CHORD_7   },   /* 8  D7  带高音双 D    */
    {10, GTR_CHORD_MAJ },   /* 9  Bb  大横按 6 品   */
    { 9, GTR_CHORD_7   }    /* 10 A7  开放 x02020   */
};

/* 标准调弦空弦音（低→高）：E2 A2 D3 G3 B3 E4 */
static const uint8_t string_open[GTR_CHORD_STRINGS] = {40, 45, 50, 55, 59, 64};
#define GTR_FRET_SPAN 5   /* 优先搜索窗口：空弦 +5 品 */

static int pc_of(int note) { int m = note % 12; return m < 0 ? m + 12 : m; }

int gtr_chord_pitches(int root, int type, int base, uint8_t out[3])
{
    if (!out) return GTR_CHORD_E_ARGS;
    if (root < 0 || root > 11) return GTR_CHORD_E_ROOT;
    if (type < 0 || type >= GTR_CHORD_NTYPES) return GTR_CHORD_E_TYPE;
    for (int i = 0; i < 3; i++) {
        int pc = pc_of(root + gtr_chord_tones[type][i]);
        out[i] = (uint8_t)(base + (((pc - base) % 12) + 12) % 12);
    }
    return GTR_CHORD_OK;
}

int gtr_chord_voicing(int root, int type, uint8_t out[GTR_CHORD_STRINGS])
{
    if (!out) return GTR_CHORD_E_ARGS;
    if (root < 0 || root > 11) return GTR_CHORD_E_ROOT;
    if (type < 0 || type >= GTR_CHORD_NTYPES) return GTR_CHORD_E_TYPE;

    const int8_t *t = gtr_chord_tones[type];
    int pcs[3];
    for (int k = 0; k < 3; k++) pcs[k] = pc_of(root + t[k]);
    int fifth = pc_of(root + 7);
    int has_fifth = 0;   /* 该型是否含五音（7/sus4 之外的 shell 型没有） */
    for (int k = 0; k < 3; k++) if (pcs[k] == fifth) has_fifth = 1;

    for (int s = 0; s < GTR_CHORD_STRINGS; s++) {
        int open = string_open[s];
        int lo = (s == 0) ? open : out[s - 1] + 1;   /* 严格升序：不低于前一弦 */
        int found = -1;

        if (s == 0) {
            /* 低音偏好序：根音(+5 品内) → 五音(+5 品内，若型内有) →
               根音(一个八度内) → 任意和弦音(+5 品内)。
               根/五音贝斯最稳；7 和弦无五音时根音优先，绝不让七音进贝斯。 */
            for (int n = lo; n <= open + GTR_FRET_SPAN && found < 0; n++)
                if (pc_of(n) == pcs[0]) found = n;
            if (has_fifth) {
                for (int n = lo; n <= open + GTR_FRET_SPAN && found < 0; n++)
                    if (pc_of(n) == fifth) found = n;
            }
            for (int n = lo; n <= open + 12 && found < 0; n++)
                if (pc_of(n) == pcs[0]) found = n;
            for (int n = lo; n <= open + GTR_FRET_SPAN && found < 0; n++)
                if (pc_of(n) == pcs[0] || pc_of(n) == pcs[1] || pc_of(n) == pcs[2]) found = n;
        } else {
            /* 其余弦：最近和弦音，+5 品内 */
            for (int n = lo; n <= open + GTR_FRET_SPAN && found < 0; n++)
                if (pc_of(n) == pcs[0] || pc_of(n) == pcs[1] || pc_of(n) == pcs[2]) found = n;
        }
        /* 延展一：空弦 +12（add9/m7 等大跳型会走到这） */
        if (found < 0) {
            for (int n = lo; n <= open + 12 && found < 0; n++)
                if (pc_of(n) == pcs[0] || pc_of(n) == pcs[1] || pc_of(n) == pcs[2]) found = n;
        }
        /* 延展二（保险）：lo 起 12 个半音覆盖全部 12 个音高集合，必有解 */
        if (found < 0) {
            for (int n = lo; n <= lo + 11 && found < 0; n++)
                if (pc_of(n) == pcs[0] || pc_of(n) == pcs[1] || pc_of(n) == pcs[2]) found = n;
        }
        if (found < 0) return GTR_CHORD_E_INTERNAL;  /* 不可达 */
        out[s] = (uint8_t)found;
    }

    /* 自检：严格升序、音高范围、全部是和弦音 */
    for (int s = 1; s < GTR_CHORD_STRINGS; s++)
        if (out[s] <= out[s - 1]) return GTR_CHORD_E_INTERNAL;
    for (int s = 0; s < GTR_CHORD_STRINGS; s++) {
        if (out[s] < 40 || out[s] > 96) return GTR_CHORD_E_INTERNAL;
        int pc = pc_of(out[s]), ok = 0;
        for (int k = 0; k < 3; k++) if (pc == pcs[k]) ok = 1;
        if (!ok) return GTR_CHORD_E_INTERNAL;
    }
    return GTR_CHORD_OK;
}

int gtr_chord_name(int root, int type, char *buf, int cap)
{
    if (!buf || cap < 8) return GTR_CHORD_E_ARGS;
    if (root < 0 || root > 11) return GTR_CHORD_E_ROOT;
    if (type < 0 || type >= GTR_CHORD_NTYPES) return GTR_CHORD_E_TYPE;
    const char *nn = gtr_chord_note_name[root];
    const char *tn = gtr_chord_type_name[type];
    int i = 0;
    for (const char *p = nn; *p && i < cap - 1; p++) buf[i++] = *p;
    for (const char *p = tn; *p && i < cap - 1; p++) buf[i++] = *p;
    buf[i] = '\0';
    return GTR_CHORD_OK;
}

/* ---------------------------------------------------------------------------
   可变和弦表 —— 编辑器与存储的共用数据层。
   表是「当前状态」的唯一真相：编辑器改它、pack 存它、固件读它。
   --------------------------------------------------------------------------- */

int gtr_chord_table_init(gtr_chord_table_t *t)
{
    if (!t) return GTR_CHORD_E_ARGS;
    for (int i = 0; i < GTR_CHORD_SLOTS; i++)
        t->slots[i] = gtr_chord_defaults[i];
    return GTR_CHORD_OK;
}

int gtr_chord_table_set(gtr_chord_table_t *t, int slot, int root, int type)
{
    if (!t) return GTR_CHORD_E_ARGS;
    if (slot < 0 || slot >= GTR_CHORD_SLOTS) return GTR_CHORD_E_SLOT;
    if (root < 0 || root > 11) return GTR_CHORD_E_ROOT;
    if (type < 0 || type >= GTR_CHORD_NTYPES) return GTR_CHORD_E_TYPE;
    t->slots[slot].root = (uint8_t)root;
    t->slots[slot].type = (uint8_t)type;
    return GTR_CHORD_OK;
}

int gtr_chord_table_get(const gtr_chord_table_t *t, int slot, int *root, int *type)
{
    if (!t) return GTR_CHORD_E_ARGS;
    if (slot < 0 || slot >= GTR_CHORD_SLOTS) return GTR_CHORD_E_SLOT;
    if (root) *root = t->slots[slot].root;
    if (type) *type = t->slots[slot].type;
    return GTR_CHORD_OK;
}

int gtr_chord_table_is_default(const gtr_chord_table_t *t, int slot)
{
    if (!t) return GTR_CHORD_E_ARGS;
    if (slot < 0 || slot >= GTR_CHORD_SLOTS) return GTR_CHORD_E_SLOT;
    return (t->slots[slot].root == gtr_chord_defaults[slot].root &&
            t->slots[slot].type == gtr_chord_defaults[slot].type) ? 1 : 0;
}

int gtr_chord_table_reset_slot(gtr_chord_table_t *t, int slot)
{
    if (!t) return GTR_CHORD_E_ARGS;
    if (slot < 0 || slot >= GTR_CHORD_SLOTS) return GTR_CHORD_E_SLOT;
    t->slots[slot] = gtr_chord_defaults[slot];
    return GTR_CHORD_OK;
}

/* ---------------------------------------------------------------------------
   序列化 —— 25 字节定长，嵌入 project.c 工程记录。
   布局：[0]='G' [1]='C' [2]=version [3..24]=11 槽 × (root, type)。
   unpack 两遍扫描：先全量校验（魔数/版本/每槽范围）再写入 ——
   任何一步失败表保持原样，坏工程文件不会把和弦表改花。
   len 允许大于 25（嵌入更大记录时后面跟别的字段），只读前 25 字节。
   --------------------------------------------------------------------------- */

int gtr_chord_table_pack(const gtr_chord_table_t *t, uint8_t *buf, int cap)
{
    if (!t || !buf) return GTR_CHORD_E_ARGS;
    if (cap < (int)GTR_CHORD_PACK_BYTES) return GTR_CHORD_E_PACK;
    buf[0] = (uint8_t)'G';
    buf[1] = (uint8_t)'C';
    buf[2] = (uint8_t)GTR_CHORD_PACK_VERSION;
    for (int i = 0; i < GTR_CHORD_SLOTS; i++) {
        buf[3 + i * 2 + 0] = t->slots[i].root;
        buf[3 + i * 2 + 1] = t->slots[i].type;
    }
    return (int)GTR_CHORD_PACK_BYTES;
}

int gtr_chord_table_unpack(gtr_chord_table_t *t, const uint8_t *buf, int len)
{
    if (!t || !buf) return GTR_CHORD_E_ARGS;
    if (len < (int)GTR_CHORD_PACK_BYTES) return GTR_CHORD_E_PACK;
    if (buf[0] != (uint8_t)'G' || buf[1] != (uint8_t)'C') return GTR_CHORD_E_MAGIC;
    if (buf[2] != (uint8_t)GTR_CHORD_PACK_VERSION) return GTR_CHORD_E_VERSION;
    /* 第一遍：全量校验，不写表 */
    for (int i = 0; i < GTR_CHORD_SLOTS; i++) {
        if (buf[3 + i * 2 + 0] > 11) return GTR_CHORD_E_ROOT;
        if (buf[3 + i * 2 + 1] >= GTR_CHORD_NTYPES) return GTR_CHORD_E_TYPE;
    }
    /* 第二遍：写入 */
    for (int i = 0; i < GTR_CHORD_SLOTS; i++) {
        t->slots[i].root = buf[3 + i * 2 + 0];
        t->slots[i].type = buf[3 + i * 2 + 1];
    }
    return GTR_CHORD_OK;
}

/* ---------------------------------------------------------------------------
   紧凑差分序列化 —— 8 字节定长，用于 project.c 工程记录的备用空间。
   与 25 字节全量 pack 的区别：只存与默认表不同的槽（差分），
   默认表固件内置，不占存储。全 0xFF = 擦除态 = 无改动。
   --------------------------------------------------------------------------- */

int gtr_chord_table_pack_diff(const gtr_chord_table_t *t, uint8_t *buf, int cap)
{
    if (!t || !buf) return GTR_CHORD_E_ARGS;
    if (cap < (int)GTR_CHORD_DIFF_BYTES) return GTR_CHORD_E_PACK;

    /* 找出所有与默认不同的槽 */
    int modified[GTR_CHORD_SLOTS];
    int count = 0;
    for (int i = 0; i < GTR_CHORD_SLOTS; i++) {
        if (t->slots[i].root != gtr_chord_defaults[i].root ||
            t->slots[i].type != gtr_chord_defaults[i].type) {
            if (count >= (int)GTR_CHORD_DIFF_MAX) return GTR_CHORD_E_DIFF;
            modified[count++] = i;
        }
    }

    /* 写位图（小端） */
    uint16_t mask = 0;
    for (int i = 0; i < count; i++)
        mask |= (uint16_t)(1u << modified[i]);
    buf[0] = (uint8_t)(mask & 0xffu);
    buf[1] = (uint8_t)((mask >> 8) & 0xffu);

    /* 写改动槽数据：高半字节=root，低半字节=type */
    for (int i = 0; i < count; i++) {
        int slot = modified[i];
        buf[2 + i] = (uint8_t)((t->slots[slot].root << 4) | (t->slots[slot].type & 0x0fu));
    }

    /* 填充剩余字节为 0xFF */
    for (int i = count; i < (int)GTR_CHORD_DIFF_MAX; i++)
        buf[2 + i] = 0xff;

    return (int)GTR_CHORD_DIFF_BYTES;
}

int gtr_chord_table_unpack_diff(gtr_chord_table_t *t, const uint8_t *buf, int len)
{
    if (!t || !buf) return GTR_CHORD_E_ARGS;
    if (len < (int)GTR_CHORD_DIFF_BYTES) return GTR_CHORD_E_PACK;

    /* 全 0xFF = 擦除态 = 无改动，初始化为默认表 */
    int all_ff = 1;
    for (int i = 0; i < (int)GTR_CHORD_DIFF_BYTES; i++)
        if (buf[i] != 0xff) { all_ff = 0; break; }
    if (all_ff)
        return gtr_chord_table_init(t);

    /* 读位图并校验：只允许 bit 0-10 */
    uint16_t mask = (uint16_t)((uint16_t)buf[0] | ((uint16_t)buf[1] << 8));
    if (mask & (uint16_t)~0x07ffu) return GTR_CHORD_E_DIFF;

    /* 数位数，超过上限报错 */
    int count = 0;
    for (int i = 0; i < GTR_CHORD_SLOTS; i++)
        if (mask & (uint16_t)(1u << i)) count++;
    if (count > (int)GTR_CHORD_DIFF_MAX) return GTR_CHORD_E_DIFF;

    /* 先初始化为默认表（保证失败时表保持原样的语义由调用方处理：
       这里先 init 再校验，校验失败时表已是默认态——调用方应在
       校验前保存原表或使用临时表） */
    int rc = gtr_chord_table_init(t);
    if (rc != GTR_CHORD_OK) return rc;

    /* 第一遍：全量校验数据字节，不写表 */
    int idx = 0;
    for (int i = 0; i < GTR_CHORD_SLOTS; i++) {
        if (!(mask & (uint16_t)(1u << i))) continue;
        uint8_t byte = buf[2 + idx];
        int root = byte >> 4;
        int type = byte & 0x0f;
        if (root > 11) return GTR_CHORD_E_ROOT;
        if (type >= GTR_CHORD_NTYPES) return GTR_CHORD_E_TYPE;
        idx++;
    }

    /* 第二遍：写入 */
    idx = 0;
    for (int i = 0; i < GTR_CHORD_SLOTS; i++) {
        if (!(mask & (uint16_t)(1u << i))) continue;
        uint8_t byte = buf[2 + idx];
        t->slots[i].root = (uint8_t)(byte >> 4);
        t->slots[i].type = (uint8_t)(byte & 0x0f);
        idx++;
    }

    return GTR_CHORD_OK;
}

/* ---------------------------------------------------------------------------
   编辑器状态机 —— SEL 页交互（移植 FoMni ui.c 的 pad_down/knob_set 逻辑）。
   交互（与 FoMni 一致，两处小改进）：
     · 按黑键 = 选中该槽并试听；松开后选中态保留，旋钮继续编辑它；
     · KNOB1 = 根音(0-11)，KNOB2 = 类型(0-8)，钳制不外溢；
     · ENV/LFO 修饰键 = 试听时临时变 min/7/m7（Omnichord 三行传统），
       绝对类型替换，不动表。

   改进一（相对 FoMni）：修饰键「按住即生效」。FoMni 只在修饰键按下的
   那一刻取样；本实现在 press 时读取当前 env/lfo 状态 —— 先按住 ENV 再
   按黑键，听到的立刻就是小调和弦，不用先按键再补修饰。

   改进二（相对 FoMni）：转旋钮后修饰变体退场（mods_live 清 0）。FoMni
   里修饰键按住期间旋钮改的是本体、听到的却是变体，容易改错；本实现
   一旦转旋钮就回到本体试听，听到的即编辑的。要变体预览再按一次修饰键。

   dirty 的精度：只有真改了值才置脏（边界钳制不算改）—— 固件据此决定
   要不要写 flash，写次数有限，不能拿空写耗寿命。
   --------------------------------------------------------------------------- */

int gtr_chord_edit_init(gtr_chord_edit_t *e, gtr_chord_table_t *t)
{
    if (!e || !t) return GTR_CHORD_E_ARGS;
    e->table = t;
    e->selected = 0;   /* 同 FoMni pad_sel 初值 */
    e->held = 0;
    e->env = 0;
    e->lfo = 0;
    e->mods_live = 0;
    e->dirty = 0;
    return GTR_CHORD_OK;
}

int gtr_chord_edit_press(gtr_chord_edit_t *e, int slot)
{
    if (!e || !e->table) return GTR_CHORD_E_ARGS;
    if (slot < 0 || slot >= GTR_CHORD_SLOTS) return GTR_CHORD_E_SLOT;
    e->selected = slot;
    e->held = 1;
    /* 修饰键若正按住，对新选中的槽立即生效（改进一） */
    e->mods_live = (e->env || e->lfo) ? 1 : 0;
    return GTR_CHORD_OK;
}

int gtr_chord_edit_release(gtr_chord_edit_t *e)
{
    if (!e) return GTR_CHORD_E_ARGS;
    e->held = 0;   /* 选中态保留：松开后旋钮继续编辑这个槽 */
    return GTR_CHORD_OK;
}

int gtr_chord_edit_knob(gtr_chord_edit_t *e, int knob, int delta)
{
    if (!e || !e->table) return GTR_CHORD_E_ARGS;
    if (knob != 1 && knob != 2) return GTR_CHORD_E_ARGS;
    int slot = e->selected;
    int root = e->table->slots[slot].root;
    int type = e->table->slots[slot].type;
    int changed = 0;
    if (knob == 1) {
        int nr = root + delta;
        if (nr < 0) nr = 0;          /* 钳制不外溢 */
        if (nr > 11) nr = 11;
        if (nr != root) { e->table->slots[slot].root = (uint8_t)nr; changed = 1; }
    } else {
        int nt = type + delta;
        if (nt < 0) nt = 0;
        if (nt >= GTR_CHORD_NTYPES) nt = GTR_CHORD_NTYPES - 1;
        if (nt != type) { e->table->slots[slot].type = (uint8_t)nt; changed = 1; }
    }
    e->mods_live = 0;   /* 改进二：转旋钮即编辑本体，修饰变体退场 */
    if (changed) e->dirty = 1;
    return GTR_CHORD_OK;
}

int gtr_chord_edit_modifier(gtr_chord_edit_t *e, int env, int lfo)
{
    if (!e) return GTR_CHORD_E_ARGS;
    e->env = env ? 1 : 0;
    e->lfo = lfo ? 1 : 0;
    e->mods_live = (e->env || e->lfo) ? 1 : 0;
    return GTR_CHORD_OK;
}

int gtr_chord_edit_reset_slot(gtr_chord_edit_t *e)
{
    if (!e || !e->table) return GTR_CHORD_E_ARGS;
    int was_default = gtr_chord_table_is_default(e->table, e->selected);
    if (was_default < 0) return was_default;
    int rc = gtr_chord_table_reset_slot(e->table, e->selected);
    if (rc != GTR_CHORD_OK) return rc;
    if (!was_default) e->dirty = 1;   /* 本来就是默认的，复位不置脏 */
    return GTR_CHORD_OK;
}

int gtr_chord_edit_saved(gtr_chord_edit_t *e)
{
    if (!e) return GTR_CHORD_E_ARGS;
    e->dirty = 0;
    return GTR_CHORD_OK;
}

int gtr_chord_edit_audition(const gtr_chord_edit_t *e, int *root, int *type)
{
    if (!e || !e->table) return GTR_CHORD_E_ARGS;
    if (!e->held) return 0;   /* 未按住：不发声 */
    int r = e->table->slots[e->selected].root;
    int t = e->table->slots[e->selected].type;
    if (e->mods_live && (e->env || e->lfo)) {
        /* 修饰变体：ENV=min / LFO=7 / 同按=m7（绝对类型替换，不动表） */
        if (e->env && e->lfo) t = GTR_CHORD_M7;
        else if (e->env) t = GTR_CHORD_MIN;
        else t = GTR_CHORD_7;
    }
    if (root) *root = r;
    if (type) *type = t;
    return 1;
}
