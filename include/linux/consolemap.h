/*
 * consolemap.h
 *
 * Interface between console.c, selection.c  and consolemap.c
 */
#define LAT1_MAP 0
#define GRAF_MAP 1
#define IBMPC_MAP 2
#define USER_MAP 3

#include <linux/types.h>

struct vc_data;

extern u16 inverse_translate(struct vc_data *conp, int glyph, int use_unicode);
extern unsigned short *set_translate(int m, struct vc_data *vc);
extern int conv_uni_to_pc(struct vc_data *conp, long ucs);
extern u32 conv_8bit_to_uni(unsigned char c);
void console_map_init(void);
