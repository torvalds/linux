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

struct console_font;

/*
 * font_data_t and helpers
 */

/**
 * font_data_t - Raw font data
 *
 * Values of type font_data_t store a pointer to raw font data. The format
 * is monochrome. Each bit sets a pixel of a stored glyph. Font data does
 * not store geometry information for the individual glyphs. Users of the
 * font have to store glyph size, pitch and character count separately.
 *
 * Font data in font_data_t is not equivalent to raw u8. Each pointer stores
 * an additional hidden header before the font data. The layout is
 *
 * +------+-----------------------------+
 * | -16  |  CRC32 Checksum (optional)  |
 * | -12  |  <Unused>                   |
 * |  -8  |  Number of data bytes       |
 * |  -4  |  Reference count            |
 * +------+-----------------------------+
 * |   0  |  Data buffer                |
 * |  ... |                             |
 * +------+-----------------------------+
 *
 * Use helpers to access font_data_t. Use font_data_buf() to get the stored data.
 */
typedef const unsigned char font_data_t;

/**
 * font_data_buf() - Returns the font data as raw bytes
 * @fd: The font data
 *
 * Returns:
 * The raw font data. The provided buffer is read-only.
 */
static inline const unsigned char *font_data_buf(font_data_t *fd)
{
	return (const unsigned char *)fd;
}

font_data_t *font_data_import(const struct console_font *font, unsigned int vpitch,
			      u32 (*calc_csum)(u32, const void *, size_t));
void font_data_get(font_data_t *fd);
bool font_data_put(font_data_t *fd);
unsigned int font_data_size(font_data_t *fd);
bool font_data_is_equal(font_data_t *lhs, font_data_t *rhs);
int font_data_export(font_data_t *fd, struct console_font *font, unsigned int vpitch);

/*
 * Font description
 */

struct font_desc {
    int idx;
    const char *name;
    unsigned int width, height;
    unsigned int charcount;
    font_data_t *data;
    int pref;
};

/* Find a font with a specific name */

extern const struct font_desc *find_font(const char *name);

/* Get the default font for a specific screen size */

extern const struct font_desc *get_default_font(int xres, int yres,
						unsigned long *font_w,
						unsigned long *font_h);

/* Max. length for the name of a predefined font */
#define MAX_FONT_NAME	32

/*
 * Built-in fonts
 */

extern const struct font_desc font_10x18;
extern const struct font_desc font_6x10;
extern const struct font_desc font_6x8;
extern const struct font_desc font_7x14;
extern const struct font_desc font_acorn_8x8;
extern const struct font_desc font_mini_4x6;
extern const struct font_desc font_pearl_8x8;
extern const struct font_desc font_sun_12x22;
extern const struct font_desc font_sun_8x16;
extern const struct font_desc font_ter_10x18;
extern const struct font_desc font_ter_16x32;
extern const struct font_desc font_vga_6x11;
extern const struct font_desc font_vga_8x16;
extern const struct font_desc font_vga_8x8;

#endif /* _VIDEO_FONT_H */
