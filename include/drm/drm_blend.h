/*
 * Copyright (c) 2016 Intel Corporation
 *
 * Permission to use, copy, modify, distribute, and sell this software and its
 * documentation for any purpose is hereby granted without fee, provided that
 * the above copyright notice appear in all copies and that both that copyright
 * notice and this permission notice appear in supporting documentation, and
 * that the name of the copyright holders not be used in advertising or
 * publicity pertaining to distribution of the software without specific,
 * written prior permission.  The copyright holders make no representations
 * about the suitability of this software for any purpose.  It is provided "as
 * is" without express or implied warranty.
 *
 * THE COPYRIGHT HOLDERS DISCLAIM ALL WARRANTIES WITH REGARD TO THIS SOFTWARE,
 * INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS, IN NO
 * EVENT SHALL THE COPYRIGHT HOLDERS BE LIABLE FOR ANY SPECIAL, INDIRECT OR
 * CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE,
 * DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER
 * TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR PERFORMANCE
 * OF THIS SOFTWARE.
 */

#ifndef __DRM_BLEND_H__
#define __DRM_BLEND_H__

#include <linux/list.h>
#include <linux/ctype.h>

struct drm_device;
struct drm_atomic_state;

/*
 * Rotation property bits. DRM_ROTATE_<degrees> rotates the image by the
 * specified amount in degrees in counter clockwise direction. DRM_REFLECT_X and
 * DRM_REFLECT_Y reflects the image along the specified axis prior to rotation
 */
#define DRM_ROTATE_0	BIT(0)
#define DRM_ROTATE_90	BIT(1)
#define DRM_ROTATE_180	BIT(2)
#define DRM_ROTATE_270	BIT(3)
#define DRM_ROTATE_MASK (DRM_ROTATE_0   | DRM_ROTATE_90 | \
			 DRM_ROTATE_180 | DRM_ROTATE_270)
#define DRM_REFLECT_X	BIT(4)
#define DRM_REFLECT_Y	BIT(5)
#define DRM_REFLECT_MASK (DRM_REFLECT_X | DRM_REFLECT_Y)

struct drm_property *drm_mode_create_rotation_property(struct drm_device *dev,
						       unsigned int supported_rotations);
unsigned int drm_rotation_simplify(unsigned int rotation,
				   unsigned int supported_rotations);

int drm_plane_create_zpos_property(struct drm_plane *plane,
				   unsigned int zpos,
				   unsigned int min, unsigned int max);
int drm_plane_create_zpos_immutable_property(struct drm_plane *plane,
					     unsigned int zpos);
int drm_atomic_normalize_zpos(struct drm_device *dev,
			      struct drm_atomic_state *state);
#endif
