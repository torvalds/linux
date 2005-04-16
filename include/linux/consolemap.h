/*
 * consolemap.h
 *
 * Interface between console.c, selection.c  and consolemap.c
 */
#define LAT1_MAP 0
#define GRAF_MAP 1
#define IBMPC_MAP 2
#define USER_MAP 3

struct vc_data;

extern unsigned char inverse_translate(struct vc_data *conp, int glyph);
extern unsigned short *set_translate(int m, struct vc_data *vc);
extern int conv_uni_to_pc(struct vc_data *conp, long ucs);
