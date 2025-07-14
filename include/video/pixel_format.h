/* SPDX-License-Identifier: GPL-2.0 */

#ifndef VIDEO_PIXEL_FORMAT_H
#define VIDEO_PIXEL_FORMAT_H

struct pixel_format {
	unsigned char bits_per_pixel;
	bool indexed;
	union {
		struct {
			struct {
				unsigned char offset;
				unsigned char length;
			} alpha, red, green, blue;
		};
		struct {
			unsigned char offset;
			unsigned char length;
		} index;
	};
};

#define PIXEL_FORMAT_C8 \
	{ 8, true, { .index = {0, 8}, } }

#define PIXEL_FORMAT_XRGB1555 \
	{ 16, false, { .alpha = {0, 0}, .red = {10, 5}, .green = {5, 5}, .blue = {0, 5} } }

#define PIXEL_FORMAT_RGB565 \
	{ 16, false, { .alpha = {0, 0}, .red = {11, 5}, .green = {5, 6}, .blue = {0, 5} } }

#define PIXEL_FORMAT_RGB888 \
	{ 24, false, { .alpha = {0, 0}, .red = {16, 8}, .green = {8, 8}, .blue = {0, 8} } }

#define PIXEL_FORMAT_XRGB8888 \
	{ 32, false, { .alpha = {0, 0}, .red = {16, 8}, .green = {8, 8}, .blue = {0, 8} } }

#define PIXEL_FORMAT_XBGR8888 \
	{ 32, false, { .alpha = {0, 0}, .red = {0, 8}, .green = {8, 8}, .blue = {16, 8} } }

#define PIXEL_FORMAT_XRGB2101010 \
	{ 32, false, { .alpha = {0, 0}, .red = {20, 10}, .green = {10, 10}, .blue = {0, 10} } }

#define __pixel_format_cmp_field(lhs, rhs, name) \
	{ \
		int ret = ((lhs)->name) - ((rhs)->name); \
		if (ret) \
			return ret; \
	}

#define __pixel_format_cmp_bitfield(lhs, rhs, name) \
	{ \
		__pixel_format_cmp_field(lhs, rhs, name.offset); \
		__pixel_format_cmp_field(lhs, rhs, name.length); \
	}

/**
 * pixel_format_cmp - Compares two pixel-format descriptions
 *
 * @lhs: a pixel-format description
 * @rhs: a pixel-format description
 *
 * Compares two pixel-format descriptions for their order. The semantics
 * are equivalent to memcmp().
 *
 * Returns:
 * 0 if both arguments describe the same pixel format, less-than-zero if lhs < rhs,
 * or greater-than-zero if lhs > rhs.
 */
static inline int pixel_format_cmp(const struct pixel_format *lhs, const struct pixel_format *rhs)
{
	__pixel_format_cmp_field(lhs, rhs, bits_per_pixel);
	__pixel_format_cmp_field(lhs, rhs, indexed);

	if (lhs->indexed) {
		__pixel_format_cmp_bitfield(lhs, rhs, index);
	} else {
		__pixel_format_cmp_bitfield(lhs, rhs, alpha);
		__pixel_format_cmp_bitfield(lhs, rhs, red);
		__pixel_format_cmp_bitfield(lhs, rhs, green);
		__pixel_format_cmp_bitfield(lhs, rhs, blue);
	}

	return 0;
}

/**
 * pixel_format_equal - Compares two pixel-format descriptions for equality
 *
 * @lhs: a pixel-format description
 * @rhs: a pixel-format description
 *
 * Returns:
 * True if both arguments describe the same pixel format, or false otherwise.
 */
static inline bool pixel_format_equal(const struct pixel_format *lhs,
				      const struct pixel_format *rhs)
{
	return !pixel_format_cmp(lhs, rhs);
}

#endif
