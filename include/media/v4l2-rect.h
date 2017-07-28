/*
 * v4l2-rect.h - v4l2_rect helper functions
 *
 * Copyright 2014 Cisco Systems, Inc. and/or its affiliates. All rights reserved.
 *
 * This program is free software; you may redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2 of the License.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#ifndef _V4L2_RECT_H_
#define _V4L2_RECT_H_

#include <linux/videodev2.h>

/**
 * v4l2_rect_set_size_to() - copy the width/height values.
 * @r: rect whose width and height fields will be set
 * @size: rect containing the width and height fields you need.
 */
static inline void v4l2_rect_set_size_to(struct v4l2_rect *r,
					 const struct v4l2_rect *size)
{
	r->width = size->width;
	r->height = size->height;
}

/**
 * v4l2_rect_set_min_size() - width and height of r should be >= min_size.
 * @r: rect whose width and height will be modified
 * @min_size: rect containing the minimal width and height
 */
static inline void v4l2_rect_set_min_size(struct v4l2_rect *r,
					  const struct v4l2_rect *min_size)
{
	if (r->width < min_size->width)
		r->width = min_size->width;
	if (r->height < min_size->height)
		r->height = min_size->height;
}

/**
 * v4l2_rect_set_max_size() - width and height of r should be <= max_size
 * @r: rect whose width and height will be modified
 * @max_size: rect containing the maximum width and height
 */
static inline void v4l2_rect_set_max_size(struct v4l2_rect *r,
					  const struct v4l2_rect *max_size)
{
	if (r->width > max_size->width)
		r->width = max_size->width;
	if (r->height > max_size->height)
		r->height = max_size->height;
}

/**
 * v4l2_rect_map_inside()- r should be inside boundary.
 * @r: rect that will be modified
 * @boundary: rect containing the boundary for @r
 */
static inline void v4l2_rect_map_inside(struct v4l2_rect *r,
					const struct v4l2_rect *boundary)
{
	v4l2_rect_set_max_size(r, boundary);
	if (r->left < boundary->left)
		r->left = boundary->left;
	if (r->top < boundary->top)
		r->top = boundary->top;
	if (r->left + r->width > boundary->width)
		r->left = boundary->width - r->width;
	if (r->top + r->height > boundary->height)
		r->top = boundary->height - r->height;
}

/**
 * v4l2_rect_same_size() - return true if r1 has the same size as r2
 * @r1: rectangle.
 * @r2: rectangle.
 *
 * Return true if both rectangles have the same size.
 */
static inline bool v4l2_rect_same_size(const struct v4l2_rect *r1,
				       const struct v4l2_rect *r2)
{
	return r1->width == r2->width && r1->height == r2->height;
}

/**
 * v4l2_rect_intersect() - calculate the intersection of two rects.
 * @r: intersection of @r1 and @r2.
 * @r1: rectangle.
 * @r2: rectangle.
 */
static inline void v4l2_rect_intersect(struct v4l2_rect *r,
				       const struct v4l2_rect *r1,
				       const struct v4l2_rect *r2)
{
	int right, bottom;

	r->top = max(r1->top, r2->top);
	r->left = max(r1->left, r2->left);
	bottom = min(r1->top + r1->height, r2->top + r2->height);
	right = min(r1->left + r1->width, r2->left + r2->width);
	r->height = max(0, bottom - r->top);
	r->width = max(0, right - r->left);
}

/**
 * v4l2_rect_scale() - scale rect r by to/from
 * @r: rect to be scaled.
 * @from: from rectangle.
 * @to: to rectangle.
 *
 * This scales rectangle @r horizontally by @to->width / @from->width and
 * vertically by @to->height / @from->height.
 *
 * Typically @r is a rectangle inside @from and you want the rectangle as
 * it would appear after scaling @from to @to. So the resulting @r will
 * be the scaled rectangle inside @to.
 */
static inline void v4l2_rect_scale(struct v4l2_rect *r,
				   const struct v4l2_rect *from,
				   const struct v4l2_rect *to)
{
	if (from->width == 0 || from->height == 0) {
		r->left = r->top = r->width = r->height = 0;
		return;
	}
	r->left = (((r->left - from->left) * to->width) / from->width) & ~1;
	r->width = ((r->width * to->width) / from->width) & ~1;
	r->top = ((r->top - from->top) * to->height) / from->height;
	r->height = (r->height * to->height) / from->height;
}

/**
 * v4l2_rect_overlap() - do r1 and r2 overlap?
 * @r1: rectangle.
 * @r2: rectangle.
 *
 * Returns true if @r1 and @r2 overlap.
 */
static inline bool v4l2_rect_overlap(const struct v4l2_rect *r1,
				     const struct v4l2_rect *r2)
{
	/*
	 * IF the left side of r1 is to the right of the right side of r2 OR
	 *    the left side of r2 is to the right of the right side of r1 THEN
	 * they do not overlap.
	 */
	if (r1->left >= r2->left + r2->width ||
	    r2->left >= r1->left + r1->width)
		return false;
	/*
	 * IF the top side of r1 is below the bottom of r2 OR
	 *    the top side of r2 is below the bottom of r1 THEN
	 * they do not overlap.
	 */
	if (r1->top >= r2->top + r2->height ||
	    r2->top >= r1->top + r1->height)
		return false;
	return true;
}

#endif
