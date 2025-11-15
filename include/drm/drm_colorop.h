/* SPDX-License-Identifier: MIT */
/*
 * Copyright (C) 2023 Advanced Micro Devices, Inc. All rights reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE COPYRIGHT HOLDER(S) OR AUTHOR(S) BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 *
 * Authors: AMD
 *
 */

#ifndef __DRM_COLOROP_H__
#define __DRM_COLOROP_H__

#include <drm/drm_mode_object.h>
#include <drm/drm_mode.h>
#include <drm/drm_property.h>

/**
 * struct drm_colorop_state - mutable colorop state
 */
struct drm_colorop_state {
	/** @colorop: backpointer to the colorop */
	struct drm_colorop *colorop;

	/*
	 * Color properties
	 *
	 * The following fields are not always valid, their usage depends
	 * on the colorop type. See their associated comment for more
	 * information.
	 */

	/** @state: backpointer to global drm_atomic_state */
	struct drm_atomic_state *state;
};

/**
 * struct drm_colorop - DRM color operation control structure
 *
 * A colorop represents one color operation. They can be chained via
 * the 'next' pointer to build a color pipeline.
 *
 * Since colorops cannot stand-alone and are used to describe colorop
 * operations on a plane they don't have their own locking mechanism but
 * are locked and programmed along with their associated &drm_plane.
 *
 */
struct drm_colorop {
	/** @dev: parent DRM device */
	struct drm_device *dev;

	/**
	 * @head:
	 *
	 * List of all colorops on @dev, linked from &drm_mode_config.colorop_list.
	 * Invariant over the lifetime of @dev and therefore does not need
	 * locking.
	 */
	struct list_head head;

	/**
	 * @index: Position inside the mode_config.list, can be used as an array
	 * index. It is invariant over the lifetime of the colorop.
	 */
	unsigned int index;

	/** @base: base mode object */
	struct drm_mode_object base;

	/**
	 * @plane:
	 *
	 * The plane on which the colorop sits. A drm_colorop is always unique
	 * to a plane.
	 */
	struct drm_plane *plane;

	/**
	 * @state:
	 *
	 * Current atomic state for this colorop.
	 *
	 * This is protected by @mutex. Note that nonblocking atomic commits
	 * access the current colorop state without taking locks.
	 */
	struct drm_colorop_state *state;

	/*
	 * Color properties
	 *
	 * The following fields are not always valid, their usage depends
	 * on the colorop type. See their associated comment for more
	 * information.
	 */

	/** @properties: property tracking for this colorop */
	struct drm_object_properties properties;

	/**
	 * @type:
	 *
	 * Read-only
	 * Type of color operation
	 */
	enum drm_colorop_type type;

	/**
	 * @type_property:
	 *
	 * Read-only "TYPE" property for specifying the type of
	 * this color operation. The type is enum drm_colorop_type.
	 */
	struct drm_property *type_property;
};

#define obj_to_colorop(x) container_of(x, struct drm_colorop, base)

/**
 * drm_colorop_find - look up a Colorop object from its ID
 * @dev: DRM device
 * @file_priv: drm file to check for lease against.
 * @id: &drm_mode_object ID
 *
 * This can be used to look up a Colorop from its userspace ID. Only used by
 * drivers for legacy IOCTLs and interface, nowadays extensions to the KMS
 * userspace interface should be done using &drm_property.
 */
static inline struct drm_colorop *drm_colorop_find(struct drm_device *dev,
						   struct drm_file *file_priv,
						   uint32_t id)
{
	struct drm_mode_object *mo;

	mo = drm_mode_object_find(dev, file_priv, id, DRM_MODE_OBJECT_COLOROP);
	return mo ? obj_to_colorop(mo) : NULL;
}

struct drm_colorop_state *
drm_atomic_helper_colorop_duplicate_state(struct drm_colorop *colorop);

void drm_colorop_atomic_destroy_state(struct drm_colorop *colorop,
				      struct drm_colorop_state *state);

/**
 * drm_colorop_reset - reset colorop atomic state
 * @colorop: drm colorop
 *
 * Resets the atomic state for @colorop by freeing the state pointer (which might
 * be NULL, e.g. at driver load time) and allocating a new empty state object.
 */
void drm_colorop_reset(struct drm_colorop *colorop);

/**
 * drm_colorop_index - find the index of a registered colorop
 * @colorop: colorop to find index for
 *
 * Given a registered colorop, return the index of that colorop within a DRM
 * device's list of colorops.
 */
static inline unsigned int drm_colorop_index(const struct drm_colorop *colorop)
{
	return colorop->index;
}

#define drm_for_each_colorop(colorop, dev) \
	list_for_each_entry(colorop, &(dev)->mode_config.colorop_list, head)

/**
 * drm_get_colorop_type_name - return a string for colorop type
 * @type: colorop type to compute name of
 *
 * In contrast to the other drm_get_*_name functions this one here returns a
 * const pointer and hence is threadsafe.
 */
const char *drm_get_colorop_type_name(enum drm_colorop_type type);

#endif /* __DRM_COLOROP_H__ */
