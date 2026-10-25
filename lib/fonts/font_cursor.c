// SPDX-License-Identifier: GPL-2.0-only

#include <linux/bug.h>
#include <linux/export.h>
#include <linux/string.h>

#include "font.h"

/**
 * font_glyph_cursor - Create a cursor glyph
 * @width: The glyph width in bits per scanline
 * @height: The number of scanlines in the glyph
 * @start: The first scanline of the cursor block
 * @end: The first scanline after the cursor block
 * @out: The cursor-glyph bitmap
 *
 * The parameters @width and @height refer to the glyph size. The caller
 * has to provide the output buffer @out of sufficient size to hold the
 * cursor glyph. The arguments in @start and @end describe the number of
 * scanlines occupied by the cursor block; counting from the top of the
 * glyph. The helper fills these scanlines with a pattern and clears the
 * remaining scanlines.
 */
void font_glyph_cursor(unsigned int width, unsigned int height,
		       unsigned int start, unsigned int end, unsigned char *out)
{
	unsigned int pitch = font_glyph_pitch(width);
	const unsigned char *out_end = out + height * pitch;
	unsigned int size;

	if (WARN_ON_ONCE(start > height))
		start = height;
	if (WARN_ON_ONCE(end > height))
		end = height;

	if (end < start)
		end = start;

	/* clear scanlines before cursor block */
	size = start * pitch;
	memset(out, 0x00, size);
	out += size;

	/* set cursor block */
	size = (end - start) * pitch;
	memset(out, 0xff, size);
	out += size;

	/* clear scanlines below cursor block */
	size = (height - end) * pitch;
	memset(out, 0x00, size);
	out += size;

	WARN_ON_ONCE(out != out_end);
}
EXPORT_SYMBOL_GPL(font_glyph_cursor);
