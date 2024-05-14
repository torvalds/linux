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
#include <drm/drm_mode.h>

#define DRM_MODE_BLEND_PREMULTI		0
#define DRM_MODE_BLEND_COVERAGE		1
#define DRM_MODE_BLEND_PIXEL_NONE	2

struct drm_device;
struct drm_atomic_state;
struct drm_plane;

static inline bool drm_rotation_90_or_270(unsigned int rotation)
{
	return rotation & (DRM_MODE_ROTATE_90 | DRM_MODE_ROTATE_270);
}

#define DRM_BLEND_ALPHA_OPAQUE		0xffff

int drm_plane_create_alpha_property(struct drm_plane *plane);
int drm_plane_create_rotation_property(struct drm_plane *plane,
				       unsigned int rotation,
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
int drm_plane_create_blend_mode_property(struct drm_plane *plane,
					 unsigned int supported_modes);
#endif
