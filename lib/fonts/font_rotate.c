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

static unsigned int __font_glyph_pos(unsigned int x, unsigned int y, unsigned int bit_pitch,
				     unsigned int *bit)
{
	unsigned int off = y * bit_pitch + x;
	unsigned int bit_shift = off % 8;

	*bit = 0x80 >> bit_shift; /* MSB has position 0, LSB has position 7 */

	return off / 8;
}

static bool font_glyph_test_bit(const unsigned char *glyph, unsigned int x, unsigned int y,
				unsigned int bit_pitch)
{
	unsigned int bit;
	unsigned int i = __font_glyph_pos(x, y, bit_pitch, &bit);

	return glyph[i] & bit;
}

static void font_glyph_set_bit(unsigned char *glyph, unsigned int x, unsigned int y,
			       unsigned int bit_pitch)
{
	unsigned int bit;
	unsigned int i = __font_glyph_pos(x, y, bit_pitch, &bit);

	glyph[i] |= bit;
}

static inline void rotate_cw(const char *in, char *out, u32 width, u32 height)
{
	int i, j, h = height, w = width;
	int shift = (8 - (height % 8)) & 7;

	width = (width + 7) & ~7;
	height = (height + 7) & ~7;

	for (i = 0; i < h; i++) {
		for (j = 0; j < w; j++) {
			if (font_glyph_test_bit(in, j, i, width))
				font_glyph_set_bit(out, height - 1 - i - shift, j, height);
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
			if (font_glyph_test_bit(in, j, i, width))
				font_glyph_set_bit(out, width - (1 + j + shift),
						   height - (1 + i), width);
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
			if (font_glyph_test_bit(in, j, i, width))
				font_glyph_set_bit(out, i, width - 1 - j - shift, height);
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
