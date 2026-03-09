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

void font_data_get(font_data_t *fd);
bool font_data_put(font_data_t *fd);
unsigned int font_data_size(font_data_t *fd);

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
#define FONT6x8_IDX	12
#define TER10x18_IDX	13

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
			font_ter_16x32,
			font_6x8,
			font_ter_10x18;

/* Find a font with a specific name */

extern const struct font_desc *find_font(const char *name);

/* Get the default font for a specific screen size */

extern const struct font_desc *get_default_font(int xres, int yres,
						unsigned long *font_w,
						unsigned long *font_h);

/* Max. length for the name of a predefined font */
#define MAX_FONT_NAME	32

/* Extra word getters */
#define REFCOUNT(fd)	(((int *)(fd))[-1])
#define FNTSIZE(fd)	(((int *)(fd))[-2])
#define FNTSUM(fd)	(((int *)(fd))[-4])

#define FONT_EXTRA_WORDS 4

struct font_data {
	unsigned int extra[FONT_EXTRA_WORDS];
	const unsigned char data[];
} __packed;

#endif /* _VIDEO_FONT_H */
