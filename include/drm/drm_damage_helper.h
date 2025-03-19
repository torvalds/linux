/* SPDX-License-Identifier: GPL-2.0 OR MIT */
/**************************************************************************
 *
 * Copyright (c) 2018 VMware, Inc., Palo Alto, CA., USA
 * All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sub license, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice (including the
 * next paragraph) shall be included in all copies or substantial portions
 * of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT. IN NO EVENT SHALL
 * THE COPYRIGHT HOLDERS, AUTHORS AND/OR ITS SUPPLIERS BE LIABLE FOR ANY CLAIM,
 * DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
 * OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE
 * USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 * Authors:
 * Deepak Rawat <drawat@vmware.com>
 *
 **************************************************************************/

#ifndef DRM_DAMAGE_HELPER_H_
#define DRM_DAMAGE_HELPER_H_

#include <drm/drm_atomic_helper.h>

/**
 * drm_atomic_for_each_plane_damage - Iterator macro for plane damage.
 * @iter: The iterator to advance.
 * @rect: Return a rectangle in fb coordinate clipped to plane src.
 *
 * Note that if the first call to iterator macro return false then no need to do
 * plane update. Iterator will return full plane src when damage is not passed
 * by user-space.
 */
#define drm_atomic_for_each_plane_damage(iter, rect) \
	while (drm_atomic_helper_damage_iter_next(iter, rect))

/**
 * struct drm_atomic_helper_damage_iter - Closure structure for damage iterator.
 *
 * This structure tracks state needed to walk the list of plane damage clips.
 */
struct drm_atomic_helper_damage_iter {
	/* private: Plane src in whole number. */
	struct drm_rect plane_src;
	/* private: Rectangles in plane damage blob. */
	const struct drm_rect *clips;
	/* private: Number of rectangles in plane damage blob. */
	uint32_t num_clips;
	/* private: Current clip iterator is advancing on. */
	uint32_t curr_clip;
	/* private: Whether need full plane update. */
	bool full_update;
};

void drm_atomic_helper_check_plane_damage(struct drm_atomic_state *state,
					  struct drm_plane_state *plane_state);
int drm_atomic_helper_dirtyfb(struct drm_framebuffer *fb,
			      struct drm_file *file_priv, unsigned int flags,
			      unsigned int color, struct drm_clip_rect *clips,
			      unsigned int num_clips);
void
drm_atomic_helper_damage_iter_init(struct drm_atomic_helper_damage_iter *iter,
				   const struct drm_plane_state *old_state,
				   const struct drm_plane_state *new_state);
bool
drm_atomic_helper_damage_iter_next(struct drm_atomic_helper_damage_iter *iter,
				   struct drm_rect *rect);
bool drm_atomic_helper_damage_merged(const struct drm_plane_state *old_state,
				     const struct drm_plane_state *state,
				     struct drm_rect *rect);

#endif
