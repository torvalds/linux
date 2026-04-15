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

#include <linux/errno.h>
#include <linux/export.h>
#include <linux/math.h>
#include <linux/overflow.h>
#include <linux/slab.h>
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

/**
 * font_data_rotate - Rotate font data by multiples of 90°
 * @fd: The font data to rotate
 * @width: The glyph width in bits per scanline
 * @height: The number of scanlines in the glyph
 * @charcount: The number of glyphs in the font
 * @steps: Number of rotation steps of 90°
 * @buf: Preallocated output buffer; can be NULL
 * @bufsize: The size of @buf in bytes; can be NULL
 *
 * The parameters @width and @height refer to the visible number of pixels
 * and scanlines in a single glyph. The number of glyphs is given in @charcount.
 * Rotation happens in steps of 90°. The @steps parameter can have any value,
 * but only 0 to 3 produce distinct results. With 4 or higher, a full rotation
 * has been performed. You can pass any value for @steps and the helper will
 * perform the appropriate rotation. Note that the returned buffer is not
 * compatible with font_data_t. It only contains glyph data in the same format
 * as returned by font_data_buf(). Callers are responsible to free the returned
 * buffer with kfree(). Font rotation typically happens when displays get
 * re-oriented. To avoid unnecessary re-allocation of the memory buffer, the
 * caller can pass in an earlier result buffer in @buf for reuse. The old and
 * new buffer sizes are given and retrieved by the caller in @bufsize. The
 * allocation semantics are compatible with krealloc().
 *
 * Returns:
 * A buffer with rotated glyphs on success, or an error pointer otherwise
 */
unsigned char *font_data_rotate(font_data_t *fd, unsigned int width, unsigned int height,
				unsigned int charcount, unsigned int steps,
				unsigned char *buf, size_t *bufsize)
{
	const unsigned char *src = font_data_buf(fd);
	unsigned int s_cellsize = font_glyph_size(width, height);
	unsigned int d_cellsize, i;
	unsigned char *dst;
	size_t size;

	steps %= 4;

	switch (steps) {
	case 0:
	case 2:
		d_cellsize = s_cellsize;
		break;
	case 1:
	case 3:
		d_cellsize = font_glyph_size(height, width); /* flip width/height */
		break;
	}

	if (check_mul_overflow(charcount, d_cellsize, &size))
		return ERR_PTR(-EINVAL);

	if (!buf || !bufsize || size > *bufsize) {
		dst = kmalloc_array(charcount, d_cellsize, GFP_KERNEL);
		if (!dst)
			return ERR_PTR(-ENOMEM);

		kfree(buf);
		buf = dst;
		if (bufsize)
			*bufsize = size;
	} else {
		dst = buf;
	}

	switch (steps) {
	case 0:
		memcpy(dst, src, size);
		break;
	case 1:
		memset(dst, 0, size);
		for (i = 0; i < charcount; ++i) {
			__font_glyph_rotate_90(src, width, height, dst);
			src += s_cellsize;
			dst += d_cellsize;
		}
		break;
	case 2:
		memset(dst, 0, size);
		for (i = 0; i < charcount; ++i) {
			__font_glyph_rotate_180(src, width, height, dst);
			src += s_cellsize;
			dst += d_cellsize;
		}
		break;
	case 3:
		memset(dst, 0, size);
		for (i = 0; i < charcount; ++i) {
			__font_glyph_rotate_270(src, width, height, dst);
			src += s_cellsize;
			dst += d_cellsize;
		}
		break;
	}

	return buf;
}
EXPORT_SYMBOL_GPL(font_data_rotate);
