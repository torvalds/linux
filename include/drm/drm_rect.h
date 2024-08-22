/*
 * Copyright (C) 2011-2013 Intel Corporation
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#ifndef DRM_RECT_H
#define DRM_RECT_H

#include <linux/types.h>

/**
 * DOC: rect utils
 *
 * Utility functions to help manage rectangular areas for
 * clipping, scaling, etc. calculations.
 */

/**
 * struct drm_rect - two dimensional rectangle
 * @x1: horizontal starting coordinate (inclusive)
 * @x2: horizontal ending coordinate (exclusive)
 * @y1: vertical starting coordinate (inclusive)
 * @y2: vertical ending coordinate (exclusive)
 *
 * Note that this must match the layout of struct drm_mode_rect or the damage
 * helpers like drm_atomic_helper_damage_iter_init() break.
 */
struct drm_rect {
	int x1, y1, x2, y2;
};

/**
 * DRM_RECT_INIT - initialize a rectangle from x/y/w/h
 * @x: x coordinate
 * @y: y coordinate
 * @w: width
 * @h: height
 *
 * RETURNS:
 * A new rectangle of the specified size.
 */
#define DRM_RECT_INIT(x, y, w, h) ((struct drm_rect){ \
		.x1 = (x), \
		.y1 = (y), \
		.x2 = (x) + (w), \
		.y2 = (y) + (h) })

/**
 * DRM_RECT_FMT - printf string for &struct drm_rect
 */
#define DRM_RECT_FMT    "%dx%d%+d%+d"
/**
 * DRM_RECT_ARG - printf arguments for &struct drm_rect
 * @r: rectangle struct
 */
#define DRM_RECT_ARG(r) drm_rect_width(r), drm_rect_height(r), (r)->x1, (r)->y1

/**
 * DRM_RECT_FP_FMT - printf string for &struct drm_rect in 16.16 fixed point
 */
#define DRM_RECT_FP_FMT "%d.%06ux%d.%06u%+d.%06u%+d.%06u"
/**
 * DRM_RECT_FP_ARG - printf arguments for &struct drm_rect in 16.16 fixed point
 * @r: rectangle struct
 *
 * This is useful for e.g. printing plane source rectangles, which are in 16.16
 * fixed point.
 */
#define DRM_RECT_FP_ARG(r) \
		drm_rect_width(r) >> 16, ((drm_rect_width(r) & 0xffff) * 15625) >> 10, \
		drm_rect_height(r) >> 16, ((drm_rect_height(r) & 0xffff) * 15625) >> 10, \
		(r)->x1 >> 16, (((r)->x1 & 0xffff) * 15625) >> 10, \
		(r)->y1 >> 16, (((r)->y1 & 0xffff) * 15625) >> 10

/**
 * drm_rect_init - initialize the rectangle from x/y/w/h
 * @r: rectangle
 * @x: x coordinate
 * @y: y coordinate
 * @width: width
 * @height: height
 */
static inline void drm_rect_init(struct drm_rect *r, int x, int y,
				 int width, int height)
{
	r->x1 = x;
	r->y1 = y;
	r->x2 = x + width;
	r->y2 = y + height;
}

/**
 * drm_rect_adjust_size - adjust the size of the rectangle
 * @r: rectangle to be adjusted
 * @dw: horizontal adjustment
 * @dh: vertical adjustment
 *
 * Change the size of rectangle @r by @dw in the horizontal direction,
 * and by @dh in the vertical direction, while keeping the center
 * of @r stationary.
 *
 * Positive @dw and @dh increase the size, negative values decrease it.
 */
static inline void drm_rect_adjust_size(struct drm_rect *r, int dw, int dh)
{
	r->x1 -= dw >> 1;
	r->y1 -= dh >> 1;
	r->x2 += (dw + 1) >> 1;
	r->y2 += (dh + 1) >> 1;
}

/**
 * drm_rect_translate - translate the rectangle
 * @r: rectangle to be translated
 * @dx: horizontal translation
 * @dy: vertical translation
 *
 * Move rectangle @r by @dx in the horizontal direction,
 * and by @dy in the vertical direction.
 */
static inline void drm_rect_translate(struct drm_rect *r, int dx, int dy)
{
	r->x1 += dx;
	r->y1 += dy;
	r->x2 += dx;
	r->y2 += dy;
}

/**
 * drm_rect_translate_to - translate the rectangle to an absolute position
 * @r: rectangle to be translated
 * @x: horizontal position
 * @y: vertical position
 *
 * Move rectangle @r to @x in the horizontal direction,
 * and to @y in the vertical direction.
 */
static inline void drm_rect_translate_to(struct drm_rect *r, int x, int y)
{
	drm_rect_translate(r, x - r->x1, y - r->y1);
}

/**
 * drm_rect_downscale - downscale a rectangle
 * @r: rectangle to be downscaled
 * @horz: horizontal downscale factor
 * @vert: vertical downscale factor
 *
 * Divide the coordinates of rectangle @r by @horz and @vert.
 */
static inline void drm_rect_downscale(struct drm_rect *r, int horz, int vert)
{
	r->x1 /= horz;
	r->y1 /= vert;
	r->x2 /= horz;
	r->y2 /= vert;
}

/**
 * drm_rect_width - determine the rectangle width
 * @r: rectangle whose width is returned
 *
 * RETURNS:
 * The width of the rectangle.
 */
static inline int drm_rect_width(const struct drm_rect *r)
{
	return r->x2 - r->x1;
}

/**
 * drm_rect_height - determine the rectangle height
 * @r: rectangle whose height is returned
 *
 * RETURNS:
 * The height of the rectangle.
 */
static inline int drm_rect_height(const struct drm_rect *r)
{
	return r->y2 - r->y1;
}

/**
 * drm_rect_visible - determine if the rectangle is visible
 * @r: rectangle whose visibility is returned
 *
 * RETURNS:
 * %true if the rectangle is visible, %false otherwise.
 */
static inline bool drm_rect_visible(const struct drm_rect *r)
{
	return drm_rect_width(r) > 0 && drm_rect_height(r) > 0;
}

/**
 * drm_rect_equals - determine if two rectangles are equal
 * @r1: first rectangle
 * @r2: second rectangle
 *
 * RETURNS:
 * %true if the rectangles are equal, %false otherwise.
 */
static inline bool drm_rect_equals(const struct drm_rect *r1,
				   const struct drm_rect *r2)
{
	return r1->x1 == r2->x1 && r1->x2 == r2->x2 &&
		r1->y1 == r2->y1 && r1->y2 == r2->y2;
}

/**
 * drm_rect_fp_to_int - Convert a rect in 16.16 fixed point form to int form.
 * @dst: rect to be stored the converted value
 * @src: rect in 16.16 fixed point form
 */
static inline void drm_rect_fp_to_int(struct drm_rect *dst,
				      const struct drm_rect *src)
{
	drm_rect_init(dst, src->x1 >> 16, src->y1 >> 16,
		      drm_rect_width(src) >> 16,
		      drm_rect_height(src) >> 16);
}

/**
 * drm_rect_overlap - Check if two rectangles overlap
 * @a: first rectangle
 * @b: second rectangle
 *
 * RETURNS:
 * %true if the rectangles overlap, %false otherwise.
 */
static inline bool drm_rect_overlap(const struct drm_rect *a,
				    const struct drm_rect *b)
{
	return (a->x2 > b->x1 && b->x2 > a->x1 &&
		a->y2 > b->y1 && b->y2 > a->y1);
}

bool drm_rect_intersect(struct drm_rect *r, const struct drm_rect *clip);
bool drm_rect_clip_scaled(struct drm_rect *src, struct drm_rect *dst,
			  const struct drm_rect *clip);
int drm_rect_calc_hscale(const struct drm_rect *src,
			 const struct drm_rect *dst,
			 int min_hscale, int max_hscale);
int drm_rect_calc_vscale(const struct drm_rect *src,
			 const struct drm_rect *dst,
			 int min_vscale, int max_vscale);
void drm_rect_debug_print(const char *prefix,
			  const struct drm_rect *r, bool fixed_point);
void drm_rect_rotate(struct drm_rect *r,
		     int width, int height,
		     unsigned int rotation);
void drm_rect_rotate_inv(struct drm_rect *r,
			 int width, int height,
			 unsigned int rotation);

#endif
