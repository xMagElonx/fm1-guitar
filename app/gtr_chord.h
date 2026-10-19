/* ============================================================================
   gtr_chord · 和弦数据模型与六弦 voicing 生成器
   ----------------------------------------------------------------------------
   母本：FoMni（github.com/charlesvestal/fm1-fomni，GPL-3.0-only）的
   (root, type) 和弦模型与 9 型音程表（移植，见 gtr_chord.c 署名）；
   voicing 生成算法为本项目原创（吉他六弦标准调弦，FM-1 扫弦板用）。

   纪律（与 gtr_sem 相同，演进路径文档第 3 节规则 1）：
     · 纯 C99，不 include 任何 SDK/宿主头文件 —— 任何编译器任何宿主都能编；
     · 无动态内存 —— 全部调用方提供缓冲；
     · 符号全部 gtr_chord_ 前缀 —— unity build 安全；
     · 所有校验失败返回负错误码，绝不静默修正。

   模型说明：
     · 11 个槽位 = FM-1 的 11 个黑键；每槽存 (root, type) 两个字节。
     · type 共 9 种（大三/小三/属七/大七/小七/减/增/sus4/add9），
       每型 3 个音程（7 和弦省五音 = shell voicing，爵士吉他标准做法）。
     · voicing = (root, type) → 6 个 MIDI 音高（标准调弦 E2=40…E4=64），
       由 gtr_chord_voicing() 生成；11 个默认槽的金标准和弦指法保持不变
       （编辑器只在用户改动某槽时才用生成结果替换）。
   ========================================================================= */
#ifndef GTR_CHORD_H
#define GTR_CHORD_H

#include <stdint.h>

#define GTR_CHORD_SLOTS   11   /* 黑键数 */
#define GTR_CHORD_NTYPES  9    /* 和弦类型数 */
#define GTR_CHORD_STRINGS 6    /* 六弦 */

/* 和弦类型（与 FoMni CH_* 一一对应） */
enum {
    GTR_CHORD_MAJ  = 0,  /* ""     大三 */
    GTR_CHORD_MIN  = 1,  /* "m"    小三 */
    GTR_CHORD_7    = 2,  /* "7"    属七 */
    GTR_CHORD_MAJ7 = 3,  /* "M7"   大七 */
    GTR_CHORD_M7   = 4,  /* "m7"   小七 */
    GTR_CHORD_DIM  = 5,  /* "dim"  减三 */
    GTR_CHORD_AUG  = 6,  /* "aug"  增三 */
    GTR_CHORD_SUS4 = 7,  /* "sus4" 挂四 */
    GTR_CHORD_ADD9 = 8   /* "add9" 加九 */
};

/* 错误码（负值） */
enum {
    GTR_CHORD_OK        =  0,
    GTR_CHORD_E_ARGS    = -1,  /* 空指针 / 容量不足 */
    GTR_CHORD_E_ROOT    = -2,  /* root 越界（须 0-11） */
    GTR_CHORD_E_TYPE    = -3,  /* type 越界（须 0-8） */
    GTR_CHORD_E_INTERNAL = -4, /* 产出自检失败（升序/范围/音高集合）—— 算法写坏了 */
    GTR_CHORD_E_MAGIC   = -5,  /* pack 魔数不匹配 */
    GTR_CHORD_E_VERSION = -6,  /* pack 版本不支持 */
    GTR_CHORD_E_PACK    = -7,  /* 缓冲区长度/容量不足 */
    GTR_CHORD_E_SLOT    = -8,  /* slot 越界（须 0-10） */
    GTR_CHORD_E_DIFF    = -9   /* 紧凑差分格式错误（位数超限/数据越界） */
};

/* 槽位：(root, type) */
typedef struct { uint8_t root, type; } gtr_chord_slot_t;

/* 音程表：每型 3 个半音（移植自 FoMni omni.c OM_TYPE_TONES，GPL-3.0-only） */
extern const int8_t gtr_chord_tones[GTR_CHORD_NTYPES][3];
/* 类型名（"" "m" "7" "M7" "m7" "dim" "aug" "sus4" "add9"） */
extern const char *const gtr_chord_type_name[GTR_CHORD_NTYPES];
/* 音名（"C" "C#" "D" "Eb" "E" "F" "F#" "G" "Ab" "A" "Bb" "B"） */
extern const char *const gtr_chord_note_name[12];
/* 默认 11 槽 —— 与固件 gtr_chords 金标准和弦一一对应（F C G Dm Am Em G7 E7 D7 Bb A7） */
extern const gtr_chord_slot_t gtr_chord_defaults[GTR_CHORD_SLOTS];

/* 取和弦三个音的 MIDI 音高（折叠到 [base, base+12) 窗口）。
   base 例：48 = C3。out 须 3 字节。 */
int gtr_chord_pitches(int root, int type, int base, uint8_t out[3]);

/* 生成六弦 voicing（标准调弦 E2=40 A2=45 D3=50 G3=55 B3=59 E4=64）。
   规则：
     · 每弦取「不低于前一弦」的最近和弦音，优先落在空弦 +5 品内；
       5 品内无解时延展到空弦 +12（一个八度内必有和弦音）。
     · 最低弦优先根音或五音（避免三音贝斯的浑浊感）；无解时任取和弦音。
     · 输出严格升序、六弦全响（不静音）、全部是和弦音。
   7 和弦生成 shell voicing（根/三/七，省五音）。 */
int gtr_chord_voicing(int root, int type, uint8_t out[GTR_CHORD_STRINGS]);

/* 名字合成：root+type → "Em" / "G7" / "Bb"。buf 须 ≥ 8 字节。 */
int gtr_chord_name(int root, int type, char *buf, int cap);

/* ---- 可变和弦表（编辑器与存储用）---- */
typedef struct {
    gtr_chord_slot_t slots[GTR_CHORD_SLOTS];
} gtr_chord_table_t;

int gtr_chord_table_init(gtr_chord_table_t *t);                  /* 填默认 11 槽 */
int gtr_chord_table_set(gtr_chord_table_t *t, int slot, int root, int type);
int gtr_chord_table_get(const gtr_chord_table_t *t, int slot, int *root, int *type);
int gtr_chord_table_is_default(const gtr_chord_table_t *t, int slot);  /* 1=默认 0=改过 负=错误 */
int gtr_chord_table_reset_slot(gtr_chord_table_t *t, int slot);  /* 单槽恢复默认 */

/* ---- 序列化（嵌入 project.c 工程记录用，25 字节定长）----
   布局：[0]='G' [1]='C' [2]=version(1) [3..24]=11 槽 × (root, type)。
   unpack 全量校验：魔数/版本/每槽取值范围，坏数据报错不静默
   （两遍扫描：先校验后写入，失败时表保持原样）。 */
#define GTR_CHORD_PACK_VERSION 1u
#define GTR_CHORD_PACK_BYTES   25u

int gtr_chord_table_pack(const gtr_chord_table_t *t, uint8_t *buf, int cap);   /* 返回字节数 */
int gtr_chord_table_unpack(gtr_chord_table_t *t, const uint8_t *buf, int len);

/* ---- 紧凑差分序列化（8 字节定长，嵌入 project.c 工程记录备用空间）----
   布局：[0..1] = 位图（小端 uint16，bit 0-10 对应槽 0-10）
         [2..7] = 最多 6 个改动槽，每槽 1 字节（高半字节=root 0-11，低半字节=type 0-8）
   全 0xFF（flash 擦除态）= 无改动（全部默认）。
   改动槽超过 6 个时 pack 返回 GTR_CHORD_E_DIFF。
   unpack 两遍扫描：先校验（位图/位数/每槽范围）再写入，失败时表保持原样。 */
#define GTR_CHORD_DIFF_BYTES   8u
#define GTR_CHORD_DIFF_MAX     6u

int gtr_chord_table_pack_diff(const gtr_chord_table_t *t, uint8_t *buf, int cap);
int gtr_chord_table_unpack_diff(gtr_chord_table_t *t, const uint8_t *buf, int len);

/* ---- 编辑器状态机（SEL 页，移植 FoMni ui.c 的 pad_down/knob_set 逻辑）----
   交互（与 FoMni 一致，两处小改进见 .c 注释）：
     · 按黑键 = 选中该槽并试听（选中态在松开后保留，旋钮继续编辑它）；
     · KNOB1 = 根音(0-11)，KNOB2 = 类型(0-8)，钳制不外溢，改动即置脏；
     · ENV/LFO 修饰键 = 试听时临时变 minor/7/m7（Omnichord 三行传统），不动表；
     · 试听查询返回 0=未按住 / 1=试听中(含修饰变体) / 负=错误。
   固件侧规则：默认槽播放金标指法（零行为变化），改过的槽播放生成 voicing。 */
typedef struct {
    gtr_chord_table_t *table;   /* 被编辑的表（调用方持有并负责落盘） */
    int selected;               /* 当前选中槽（0-10；初始 0，同 FoMni pad_sel） */
    int held;                   /* 选中槽的键按住中（试听） */
    int env, lfo;               /* 修饰键状态 */
    int mods_live;              /* 修饰变体对试听生效中（按槽/修饰键变化时置 1，转旋钮后清 0） */
    int dirty;                  /* 有未落盘修改 */
} gtr_chord_edit_t;

int gtr_chord_edit_init(gtr_chord_edit_t *e, gtr_chord_table_t *t);
int gtr_chord_edit_press(gtr_chord_edit_t *e, int slot);
int gtr_chord_edit_release(gtr_chord_edit_t *e);
int gtr_chord_edit_knob(gtr_chord_edit_t *e, int knob, int delta);   /* knob: 1=root 2=type */
int gtr_chord_edit_modifier(gtr_chord_edit_t *e, int env, int lfo);
int gtr_chord_edit_reset_slot(gtr_chord_edit_t *e);                  /* 选中槽恢复默认 */
int gtr_chord_edit_saved(gtr_chord_edit_t *e);                       /* 固件落盘后调用，清脏 */
int gtr_chord_edit_audition(const gtr_chord_edit_t *e, int *root, int *type);

#ifdef __cplusplus
}
#endif

#endif /* GTR_CHORD_H */
