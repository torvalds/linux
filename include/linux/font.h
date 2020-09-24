/*
 *  font.h -- `Soft' font definitions
 *
 *  Created 1995 by Geert Uytterhoeven
 *
 *  This file is subject to the terms and conditions of the GNU General Public
 *  License.  See the file COPYING in the main directory of this archive
 *  for more details.
 */

#ifndef _VIDEO_FONT_H
#define _VIDEO_FONT_H

#include <linux/types.h>

struct font_desc {
    int idx;
    const char *name;
    int width, height;
    const void *data;
    int pref;
};

#define VGA8x8_IDX	0
#define VGA8x16_IDX	1
#define PEARL8x8_IDX	2
#define VGA6x11_IDX	3
#define FONT7x14_IDX	4
#define	FONT10x18_IDX	5
#define SUN8x16_IDX	6
#define SUN12x22_IDX	7
#define ACORN8x8_IDX	8
#define	MINI4x6_IDX	9
#define FONT6x10_IDX	10
#define TER16x32_IDX	11

extern const struct font_desc	font_vga_8x8,
			font_vga_8x16,
			font_pearl_8x8,
			font_vga_6x11,
			font_7x14,
			font_10x18,
			font_sun_8x16,
			font_sun_12x22,
			font_acorn_8x8,
			font_mini_4x6,
			font_6x10,
			font_ter_16x32;

/* Find a font with a specific name */

extern const struct font_desc *find_font(const char *name);

/* Get the default font for a specific screen size */

extern const struct font_desc *get_default_font(int xres, int yres,
						u32 font_w, u32 font_h);

/* Max. length for the name of a predefined font */
#define MAX_FONT_NAME	32

/* Extra word getters */
#define REFCOUNT(fd)	(((int *)(fd))[-1])
#define FNTSIZE(fd)	(((int *)(fd))[-2])
#define FNTCHARCNT(fd)	(((int *)(fd))[-3])
#define FNTSUM(fd)	(((int *)(fd))[-4])

#define FONT_EXTRA_WORDS 4

#endif /* _VIDEO_FONT_H */
