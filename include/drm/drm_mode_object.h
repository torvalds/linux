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

#ifndef __DRM_MODESET_H__
#define __DRM_MODESET_H__

#include <linux/kref.h>
struct drm_object_properties;
struct drm_property;

struct drm_mode_object {
	uint32_t id;
	uint32_t type;
	struct drm_object_properties *properties;
	struct kref refcount;
	void (*free_cb)(struct kref *kref);
};

#define DRM_OBJECT_MAX_PROPERTY 24
struct drm_object_properties {
	int count, atomic_count;
	/* NOTE: if we ever start dynamically destroying properties (ie.
	 * not at drm_mode_config_cleanup() time), then we'd have to do
	 * a better job of detaching property from mode objects to avoid
	 * dangling property pointers:
	 */
	struct drm_property *properties[DRM_OBJECT_MAX_PROPERTY];
	/* do not read/write values directly, but use drm_object_property_get_value()
	 * and drm_object_property_set_value():
	 */
	uint64_t values[DRM_OBJECT_MAX_PROPERTY];
};

/* Avoid boilerplate.  I'm tired of typing. */
#define DRM_ENUM_NAME_FN(fnname, list)				\
	const char *fnname(int val)				\
	{							\
		int i;						\
		for (i = 0; i < ARRAY_SIZE(list); i++) {	\
			if (list[i].type == val)		\
				return list[i].name;		\
		}						\
		return "(unknown)";				\
	}

struct drm_mode_object *drm_mode_object_find(struct drm_device *dev,
					     uint32_t id, uint32_t type);
void drm_mode_object_reference(struct drm_mode_object *obj);
void drm_mode_object_unreference(struct drm_mode_object *obj);

int drm_object_property_set_value(struct drm_mode_object *obj,
				  struct drm_property *property,
				  uint64_t val);
int drm_object_property_get_value(struct drm_mode_object *obj,
				  struct drm_property *property,
				  uint64_t *value);

void drm_object_attach_property(struct drm_mode_object *obj,
				struct drm_property *property,
				uint64_t init_val);
#endif
