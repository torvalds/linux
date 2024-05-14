/* SPDX-License-Identifier: GPL-2.0 */
/*
 * consolemap.h
 *
 * Interface between console.c, selection.c  and consolemap.c
 */
#ifndef __LINUX_CONSOLEMAP_H__
#define __LINUX_CONSOLEMAP_H__

enum translation_map {
	LAT1_MAP,
	GRAF_MAP,
	IBMPC_MAP,
	USER_MAP,

	FIRST_MAP = LAT1_MAP,
	LAST_MAP = USER_MAP,
};

#include <linux/types.h>

struct vc_data;

#ifdef CONFIG_CONSOLE_TRANSLATIONS
u16 inverse_translate(const struct vc_data *conp, u16 glyph, bool use_unicode);
unsigned short *set_translate(enum translation_map m, struct vc_data *vc);
int conv_uni_to_pc(struct vc_data *conp, long ucs);
u32 conv_8bit_to_uni(unsigned char c);
int conv_uni_to_8bit(u32 uni);
void console_map_init(void);
#else
static inline u16 inverse_translate(const struct vc_data *conp, u16 glyph,
		bool use_unicode)
{
	return glyph;
}

static inline unsigned short *set_translate(enum translation_map m,
		struct vc_data *vc)
{
	return NULL;
}

static inline int conv_uni_to_pc(struct vc_data *conp, long ucs)
{
	return ucs > 0xff ? -1 : ucs;
}

static inline u32 conv_8bit_to_uni(unsigned char c)
{
	return c;
}

static inline int conv_uni_to_8bit(u32 uni)
{
	return uni & 0xff;
}

static inline void console_map_init(void) { }
#endif /* CONFIG_CONSOLE_TRANSLATIONS */

#endif /* __LINUX_CONSOLEMAP_H__ */
