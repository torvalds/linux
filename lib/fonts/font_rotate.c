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

/* number of bits per line */
static unsigned int font_glyph_bit_pitch(unsigned int width)
{
	return round_up(width, 8);
}

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

static void __font_glyph_rotate_90(const unsigned char *glyph,
				   unsigned int width, unsigned int height,
				   unsigned char *out)
{
	unsigned int x, y;
	unsigned int shift = (8 - (height % 8)) & 7;
	unsigned int bit_pitch = font_glyph_bit_pitch(width);
	unsigned int out_bit_pitch = font_glyph_bit_pitch(height);

	for (y = 0; y < height; y++) {
		for (x = 0; x < width; x++) {
			if (font_glyph_test_bit(glyph, x, y, bit_pitch)) {
				font_glyph_set_bit(out, out_bit_pitch - 1 - y - shift, x,
						   out_bit_pitch);
			}
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

	__font_glyph_rotate_90(glyph, width, height, out);
}
EXPORT_SYMBOL_GPL(font_glyph_rotate_90);

static void __font_glyph_rotate_180(const unsigned char *glyph,
				    unsigned int width, unsigned int height,
				    unsigned char *out)
{
	unsigned int x, y;
	unsigned int shift = (8 - (width % 8)) & 7;
	unsigned int bit_pitch = font_glyph_bit_pitch(width);

	for (y = 0; y < height; y++) {
		for (x = 0; x < width; x++) {
			if (font_glyph_test_bit(glyph, x, y, bit_pitch)) {
				font_glyph_set_bit(out, width - (1 + x + shift), height - (1 + y),
						   bit_pitch);
			}
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

	__font_glyph_rotate_180(glyph, width, height, out);
}
EXPORT_SYMBOL_GPL(font_glyph_rotate_180);

static void __font_glyph_rotate_270(const unsigned char *glyph,
				    unsigned int width, unsigned int height,
				    unsigned char *out)
{
	unsigned int x, y;
	unsigned int shift = (8 - (width % 8)) & 7;
	unsigned int bit_pitch = font_glyph_bit_pitch(width);
	unsigned int out_bit_pitch = font_glyph_bit_pitch(height);

	for (y = 0; y < height; y++) {
		for (x = 0; x < width; x++) {
			if (font_glyph_test_bit(glyph, x, y, bit_pitch))
				font_glyph_set_bit(out, y, bit_pitch - 1 - x - shift,
						   out_bit_pitch);
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

	__font_glyph_rotate_270(glyph, width, height, out);
}
EXPORT_SYMBOL_GPL(font_glyph_rotate_270);
