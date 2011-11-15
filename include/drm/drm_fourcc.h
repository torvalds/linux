/*
 * Copyright 2011 Intel Corporation
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
 * VA LINUX SYSTEMS AND/OR ITS SUPPLIERS BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 */

#ifndef DRM_FOURCC_H
#define DRM_FOURCC_H

/*
 * We don't use the V4L header because
 * 1) the fourcc codes are well defined and trivial to construct
 * 2) we don't want user apps to have to pull in v4l headers just for fourcc
 * 3) the v4l fourcc codes are mixed up with a bunch of other code and are
 *    part of the v4l API, so changing them to something linux-generic isn't
 *    feasible
 *
 * So the below includes the fourcc codes used by the DRM and its drivers,
 * along with potential device specific codes.
 */

#include <linux/types.h>

#define fourcc_code(a,b,c,d) ((u32)(a) | ((u32)(b) << 8) | \
			      ((u32)(c) << 16) | ((u32)(d) << 24))

/* RGB codes */
#define DRM_FOURCC_RGB332 fourcc_code('R','G','B','1')
#define DRM_FOURCC_RGB555 fourcc_code('R','G','B','O')
#define DRM_FOURCC_RGB565 fourcc_code('R','G','B','P')
#define DRM_FOURCC_RGB24  fourcc_code('R','G','B','3')
#define DRM_FOURCC_RGB32  fourcc_code('R','G','B','4')

#define DRM_FOURCC_BGR24  fourcc_code('B','G','R','3')
#define DRM_FOURCC_BGR32  fourcc_code('B','G','R','4')

/* YUV codes */
#define DRM_FOURCC_YUYV   fourcc_code('Y', 'U', 'Y', 'V')
#define DRM_FOURCC_YVYU   fourcc_code('Y', 'V', 'Y', 'U')
#define DRM_FOURCC_UYVY   fourcc_code('U', 'Y', 'V', 'Y')
#define DRM_FOURCC_VYUY   fourcc_code('V', 'Y', 'U', 'Y')

/* DRM specific codes */
#define DRM_INTEL_RGB30   fourcc_code('R','G','B','0') /* RGB x:10:10:10 */

#endif /* DRM_FOURCC_H */
