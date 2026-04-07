// SPDX-License-Identifier: GPL-2.0-only
/*
 * Font rotation
 *
 *    Copyright (C) 2005 Antonino Daplas <adaplas @pol.net>
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file COPYING in the main directory of this archive for
 * more details.
 */

#include <linux/export.h>
#include <linux/math.h>
#include <linux/string.h>

#include "font.h"

static inline int pattern_test_bit(u32 x, u32 y, u32 pitch, const char *pat)
{
	u32 tmp = (y * pitch) + x, index = tmp / 8,  bit = tmp % 8;

	pat += index;
	return (*pat) & (0x80 >> bit);
}

static inline void pattern_set_bit(u32 x, u32 y, u32 pitch, char *pat)
{
	u32 tmp = (y * pitch) + x, index = tmp / 8, bit = tmp % 8;

	pat += index;

	(*pat) |= 0x80 >> bit;
}

static inline void rotate_cw(const char *in, char *out, u32 width, u32 height)
{
	int i, j, h = height, w = width;
	int shift = (8 - (height % 8)) & 7;

	width = (width + 7) & ~7;
	height = (height + 7) & ~7;

	for (i = 0; i < h; i++) {
		for (j = 0; j < w; j++) {
			if (pattern_test_bit(j, i, width, in))
				pattern_set_bit(height - 1 - i - shift, j,
						height, out);
		}
	}
}

/**
 * font_glyph_rotate_90 - Rotate a glyph pattern by 90° in clockwise direction
 * @glyph: The glyph to rotate
 * @width: The glyph width in bits per scanline
 * @height: The number of scanlines in the glyph
 * @out: The rotated glyph bitmap
 *
 * The parameters @width and @height refer to the input glyph given in @glyph.
 * The caller has to provide the output buffer @out of sufficient size to hold
 * the rotated glyph. Rotating by 90° flips the width and height for the output
 * glyph. Depending on the glyph pitch, the size of the output glyph can be
 * different than the size of the input. Callers have to take this into account
 * when allocating the output memory.
 */
void font_glyph_rotate_90(const unsigned char *glyph, unsigned int width, unsigned int height,
			  unsigned char *out)
{
	memset(out, 0, font_glyph_size(height, width)); /* flip width/height */

	rotate_cw(glyph, out, width, height);
}
EXPORT_SYMBOL_GPL(font_glyph_rotate_90);

static inline void rotate_ud(const char *in, char *out, u32 width, u32 height)
{
	int i, j;
	int shift = (8 - (width % 8)) & 7;

	width = (width + 7) & ~7;

	for (i = 0; i < height; i++) {
		for (j = 0; j < width - shift; j++) {
			if (pattern_test_bit(j, i, width, in))
				pattern_set_bit(width - (1 + j + shift),
						height - (1 + i),
						width, out);
		}
	}
}

/**
 * font_glyph_rotate_180 - Rotate a glyph pattern by 180°
 * @glyph: The glyph to rotate
 * @width: The glyph width in bits per scanline
 * @height: The number of scanlines in the glyph
 * @out: The rotated glyph bitmap
 *
 * The parameters @width and @height refer to the input glyph given in @glyph.
 * The caller has to provide the output buffer @out of sufficient size to hold
 * the rotated glyph.
 */
void font_glyph_rotate_180(const unsigned char *glyph, unsigned int width, unsigned int height,
			   unsigned char *out)
{
	memset(out, 0, font_glyph_size(width, height));

	rotate_ud(glyph, out, width, height);
}
EXPORT_SYMBOL_GPL(font_glyph_rotate_180);

static inline void rotate_ccw(const char *in, char *out, u32 width, u32 height)
{
	int i, j, h = height, w = width;
	int shift = (8 - (width % 8)) & 7;

	width = (width + 7) & ~7;
	height = (height + 7) & ~7;

	for (i = 0; i < h; i++) {
		for (j = 0; j < w; j++) {
			if (pattern_test_bit(j, i, width, in))
				pattern_set_bit(i, width - 1 - j - shift,
						height, out);
		}
	}
}

/**
 * font_glyph_rotate_270 - Rotate a glyph pattern by 270° in clockwise direction
 * @glyph: The glyph to rotate
 * @width: The glyph width in bits per scanline
 * @height: The number of scanlines in the glyph
 * @out: The rotated glyph bitmap
 *
 * The parameters @width and @height refer to the input glyph given in @glyph.
 * The caller has to provide the output buffer @out of sufficient size to hold
 * the rotated glyph. Rotating by 270° flips the width and height for the output
 * glyph. Depending on the glyph pitch, the size of the output glyph can be
 * different than the size of the input. Callers have to take this into account
 * when allocating the output memory.
 */
void font_glyph_rotate_270(const unsigned char *glyph, unsigned int width, unsigned int height,
			   unsigned char *out)
{
	memset(out, 0, font_glyph_size(height, width)); /* flip width/height */

	rotate_ccw(glyph, out, width, height);
}
EXPORT_SYMBOL_GPL(font_glyph_rotate_270);
