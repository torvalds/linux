/*
 * `Soft' font definitions
 *
 *    Created 1995 by Geert Uytterhoeven
 *    Rewritten 1998 by Martin Mares <mj@ucw.cz>
 *
 *	2001 - Documented with DocBook
 *	- Brad Douglas <brad@neruo.com>
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file COPYING in the main directory of this archive
 * for more details.
 */

#include <linux/container_of.h>
#include <linux/kd.h>
#include <linux/module.h>
#include <linux/overflow.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/types.h>

#if defined(__mc68000__)
#include <asm/setup.h>
#endif

#include "font.h"

#define console_font_pitch(font) DIV_ROUND_UP((font)->width, 8)

/*
 * Helpers for font_data_t
 */

/* Extra word getters */
#define REFCOUNT(fd)	(((int *)(fd))[-1])
#define FNTSIZE(fd)	(((int *)(fd))[-2])
#define FNTSUM(fd)	(((int *)(fd))[-4])

static struct font_data *to_font_data_struct(font_data_t *fd)
{
	return container_of(fd, struct font_data, data[0]);
}

static bool font_data_is_internal(font_data_t *fd)
{
	return !REFCOUNT(fd); /* internal fonts have no reference counting */
}

static void font_data_free(font_data_t *fd)
{
	kfree(to_font_data_struct(fd));
}

/**
 * font_data_import - Allocates and initializes font data from user space
 * @font: A font from user space
 * @vpitch: The size of a single glyph in @font in bytes
 * @calc_csum: An optional helper to calculate a chechsum
 *
 * Font data from user space must be translated to the kernel's format. The
 * font's glyph geometry and data is provided in @font. The parameter @vpitch
 * gives the number of bytes per glyph, including trailing bytes.
 *
 * The parameter @calc_csum is optional. Fbcon passes crc32() to calculate the
 * font data's checksum.
 *
 * Returns:
 * Newly initialized font data on success, or a pointer-encoded errno value otherwise.
 */
font_data_t *font_data_import(const struct console_font *font, unsigned int vpitch,
			      u32 (*calc_csum)(u32, const void *, size_t))
{
	unsigned int pitch = console_font_pitch(font);
	unsigned int h = font->height;
	unsigned int charcount = font->charcount;
	const unsigned char *data = font->data;
	u32 csum = 0;
	struct font_data *font_data;
	int size, alloc_size;
	unsigned int i;
	font_data_t *fd;

	/* Check for integer overflow in font-size calculation */
	if (check_mul_overflow(h, pitch, &size) ||
	    check_mul_overflow(size, charcount, &size))
		return ERR_PTR(-EINVAL);

	/* Check for overflow in allocation size calculation */
	if (check_add_overflow(sizeof(*font_data), size, &alloc_size))
		return ERR_PTR(-EINVAL);

	font_data = kmalloc(alloc_size, GFP_USER);
	if (!font_data)
		return ERR_PTR(-ENOMEM);
	memset(font_data->extra, 0, sizeof(font_data->extra));

	for (i = 0; i < charcount; ++i)
		memcpy(font_data->data + i * h * pitch, data + i * vpitch * pitch, h * pitch);

	if (calc_csum)
		csum = calc_csum(0, font_data->data, size);

	fd = font_data->data;
	REFCOUNT(fd) = 1; /* start with reference acquired */
	FNTSIZE(fd) = size;
	FNTSUM(fd) = csum;

	return fd;
}
EXPORT_SYMBOL_GPL(font_data_import);

/**
 * font_data_get - Acquires a reference on font data
 * @fd: Font data
 *
 * Font data from user space is reference counted. The helper
 * font_data_get() increases the reference counter by one. Invoke
 * font_data_put() to release the reference.
 *
 * Internal font data is located in read-only memory. In this case
 * the helper returns success without modifying the counter field.
 * It is still required to call font_data_put() on internal font data.
 */
void font_data_get(font_data_t *fd)
{
	if (font_data_is_internal(fd))
		return; /* never ref static data */

	if (WARN_ON(!REFCOUNT(fd)))
		return; /* should never be 0 */
	++REFCOUNT(fd);
}
EXPORT_SYMBOL_GPL(font_data_get);

/**
 * font_data_put - Release a reference on font data
 * @fd: Font data
 *
 * Font data from user space is reference counted. The helper
 * font_data_put() decreases the reference counter by one. If this was
 * the final reference, it frees the allocated memory.
 *
 * Internal font data is located in read-only memory. In this case
 * the helper returns success without modifying the counter field.
 *
 * Returns:
 * True if the font data's memory buffer has been freed, false otherwise.
 */
bool font_data_put(font_data_t *fd)
{
	unsigned int count;

	if (font_data_is_internal(fd))
		return false; /* never unref static data */

	if (WARN_ON(!REFCOUNT(fd)))
		return false; /* should never be 0 */

	count = --REFCOUNT(fd);
	if (!count)
		font_data_free(fd);

	return !count;
}
EXPORT_SYMBOL_GPL(font_data_put);

/**
 * font_data_size - Return size of the font data in bytes
 * @fd: Font data
 *
 * Returns:
 * The number of bytes in the given font data.
 */
unsigned int font_data_size(font_data_t *fd)
{
	return FNTSIZE(fd);
}
EXPORT_SYMBOL_GPL(font_data_size);

/**
 * font_data_is_equal - Compares font data for equality
 * @lhs: Left-hand side font data
 * @rhs: Right-hand-size font data
 *
 * Font data is equal if is constain the same sequence of values. The
 * helper also use the checksum, if both arguments contain it. Font data
 * coming from different origins, internal or from user space, is never
 * equal. Allowing this would break reference counting.
 *
 * Returns:
 * True if the given font data is equal, false otherwise.
 */
bool font_data_is_equal(font_data_t *lhs, font_data_t *rhs)
{
	if (font_data_is_internal(lhs) != font_data_is_internal(rhs))
		return false;
	if (font_data_size(lhs) != font_data_size(rhs))
		return false;
	if (FNTSUM(lhs) && FNTSUM(rhs) && FNTSUM(lhs) != FNTSUM(rhs))
		return false;

	return !memcmp(lhs, rhs, FNTSIZE(lhs));
}
EXPORT_SYMBOL_GPL(font_data_is_equal);

/**
 * font_data_export - Stores font data for user space
 * @fd: Font data
 * @font: A font for user space
 * @vpitch: The size of a single glyph in @font in bytes
 *
 * Store the font data given in @fd to the font in @font. Values and
 * pointers in @font are pre-initialized. This helper mostly checks some
 * corner cases and translates glyph sizes according to the value given
 * @vpitch.
 *
 * Returns:
 * 0 on success, or a negative errno code otherwise.
 */
int font_data_export(font_data_t *fd, struct console_font *font, unsigned int vpitch)
{
	const unsigned char *font_data = font_data_buf(fd);
	unsigned char *data = font->data;
	unsigned int pitch = console_font_pitch(font);
	unsigned int glyphsize, i;

	if (!font->width || !font->height || !font->charcount || !font->data)
		return 0;

	glyphsize = font->height * pitch;

	if (font->charcount * glyphsize > font_data_size(fd))
		return -EINVAL;

	for (i = 0; i < font->charcount; i++) {
		memcpy(data, font_data, glyphsize);
		memset(data + glyphsize, 0, pitch * vpitch - glyphsize);
		data += pitch * vpitch;
		font_data += glyphsize;
	}

	return 0;
}
EXPORT_SYMBOL_GPL(font_data_export);

/*
 * Font lookup
 */

static const struct font_desc *fonts[] = {
#ifdef CONFIG_FONT_8x8
	&font_vga_8x8,
#endif
#ifdef CONFIG_FONT_8x16
	&font_vga_8x16,
#endif
#ifdef CONFIG_FONT_6x11
	&font_vga_6x11,
#endif
#ifdef CONFIG_FONT_7x14
	&font_7x14,
#endif
#ifdef CONFIG_FONT_SUN8x16
	&font_sun_8x16,
#endif
#ifdef CONFIG_FONT_SUN12x22
	&font_sun_12x22,
#endif
#ifdef CONFIG_FONT_10x18
	&font_10x18,
#endif
#ifdef CONFIG_FONT_ACORN_8x8
	&font_acorn_8x8,
#endif
#ifdef CONFIG_FONT_PEARL_8x8
	&font_pearl_8x8,
#endif
#ifdef CONFIG_FONT_MINI_4x6
	&font_mini_4x6,
#endif
#ifdef CONFIG_FONT_6x10
	&font_6x10,
#endif
#ifdef CONFIG_FONT_TER10x18
	&font_ter_10x18,
#endif
#ifdef CONFIG_FONT_TER16x32
	&font_ter_16x32,
#endif
#ifdef CONFIG_FONT_6x8
	&font_6x8,
#endif
};

#define num_fonts ARRAY_SIZE(fonts)

#ifdef NO_FONTS
#error No fonts configured.
#endif


/**
 *	find_font - find a font
 *	@name: string name of a font
 *
 *	Find a specified font with string name @name.
 *
 *	Returns %NULL if no font found, or a pointer to the
 *	specified font.
 *
 */
const struct font_desc *find_font(const char *name)
{
	unsigned int i;

	BUILD_BUG_ON(!num_fonts);
	for (i = 0; i < num_fonts; i++)
		if (!strcmp(fonts[i]->name, name))
			return fonts[i];
	return NULL;
}
EXPORT_SYMBOL(find_font);


/**
 *	get_default_font - get default font
 *	@xres: screen size of X
 *	@yres: screen size of Y
 *	@font_w: bit array of supported widths (1 - FB_MAX_BLIT_WIDTH)
 *	@font_h: bit array of supported heights (1 - FB_MAX_BLIT_HEIGHT)
 *
 *	Get the default font for a specified screen size.
 *	Dimensions are in pixels.
 *
 *	font_w or font_h being NULL means all values are supported.
 *
 *	Returns %NULL if no font is found, or a pointer to the
 *	chosen font.
 *
 */
const struct font_desc *get_default_font(int xres, int yres,
					 unsigned long *font_w,
					 unsigned long *font_h)
{
	int i, c, cc, res;
	const struct font_desc *f, *g;

	g = NULL;
	cc = -10000;
	for (i = 0; i < num_fonts; i++) {
		f = fonts[i];
		c = f->pref;
#if defined(__mc68000__)
#ifdef CONFIG_FONT_PEARL_8x8
		if (MACH_IS_AMIGA && f->idx == PEARL8x8_IDX)
			c = 100;
#endif
#ifdef CONFIG_FONT_6x11
		if (MACH_IS_MAC && xres < 640 && f->idx == VGA6x11_IDX)
			c = 100;
#endif
#endif
		if ((yres < 400) == (f->height <= 8))
			c += 1000;

		/* prefer a bigger font for high resolution */
		res = (xres / f->width) * (yres / f->height) / 1000;
		if (res > 20)
			c += 20 - res;

		if ((!font_w || test_bit(f->width - 1, font_w)) &&
		    (!font_h || test_bit(f->height - 1, font_h)))
			c += 1000;

		if (c > cc) {
			cc = c;
			g = f;
		}
	}
	return g;
}
EXPORT_SYMBOL(get_default_font);

MODULE_AUTHOR("James Simmons <jsimmons@users.sf.net>");
MODULE_DESCRIPTION("Console Fonts");
MODULE_LICENSE("GPL");
