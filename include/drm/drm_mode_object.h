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
struct drm_device;

/**
 * struct drm_mode_object - base structure for modeset objects
 * @id: userspace visible identifier
 * @type: type of the object, one of DRM_MODE_OBJECT\_\*
 * @properties: properties attached to this object, including values
 * @refcount: reference count for objects which with dynamic lifetime
 * @free_cb: free function callback, only set for objects with dynamic lifetime
 *
 * Base structure for modeset objects visible to userspace. Objects can be
 * looked up using drm_mode_object_find(). Besides basic uapi interface
 * properties like @id and @type it provides two services:
 *
 * - It tracks attached properties and their values. This is used by &drm_crtc,
 *   &drm_plane and &drm_connector. Properties are attached by calling
 *   drm_object_attach_property() before the object is visible to userspace.
 *
 * - For objects with dynamic lifetimes (as indicated by a non-NULL @free_cb) it
 *   provides reference counting through drm_mode_object_get() and
 *   drm_mode_object_put(). This is used by &drm_framebuffer, &drm_connector
 *   and &drm_property_blob. These objects provide specialized reference
 *   counting wrappers.
 */
struct drm_mode_object {
	uint32_t id;
	uint32_t type;
	struct drm_object_properties *properties;
	struct kref refcount;
	void (*free_cb)(struct kref *kref);
};

#define DRM_OBJECT_MAX_PROPERTY 24
/**
 * struct drm_object_properties - property tracking for &drm_mode_object
 */
struct drm_object_properties {
	/**
	 * @count: number of valid properties, must be less than or equal to
	 * DRM_OBJECT_MAX_PROPERTY.
	 */

	int count;
	/**
	 * @properties: Array of pointers to &drm_property.
	 *
	 * NOTE: if we ever start dynamically destroying properties (ie.
	 * not at drm_mode_config_cleanup() time), then we'd have to do
	 * a better job of detaching property from mode objects to avoid
	 * dangling property pointers:
	 */
	struct drm_property *properties[DRM_OBJECT_MAX_PROPERTY];

	/**
	 * @values: Array to store the property values, matching @properties. Do
	 * not read/write values directly, but use
	 * drm_object_property_get_value() and drm_object_property_set_value().
	 *
	 * Note that atomic drivers do not store mutable properties in this
	 * array, but only the decoded values in the corresponding state
	 * structure. The decoding is done using the &drm_crtc.atomic_get_property and
	 * &drm_crtc.atomic_set_property hooks for &struct drm_crtc. For
	 * &struct drm_plane the hooks are &drm_plane_funcs.atomic_get_property and
	 * &drm_plane_funcs.atomic_set_property. And for &struct drm_connector
	 * the hooks are &drm_connector_funcs.atomic_get_property and
	 * &drm_connector_funcs.atomic_set_property .
	 *
	 * Hence atomic drivers should not use drm_object_property_set_value()
	 * and drm_object_property_get_value() on mutable objects, i.e. those
	 * without the DRM_MODE_PROP_IMMUTABLE flag set.
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
void drm_mode_object_get(struct drm_mode_object *obj);
void drm_mode_object_put(struct drm_mode_object *obj);

/**
 * drm_mode_object_reference - acquire a mode object reference
 * @obj: DRM mode object
 *
 * This is a compatibility alias for drm_mode_object_get() and should not be
 * used by new code.
 */
static inline void drm_mode_object_reference(struct drm_mode_object *obj)
{
	drm_mode_object_get(obj);
}

/**
 * drm_mode_object_unreference - release a mode object reference
 * @obj: DRM mode object
 *
 * This is a compatibility alias for drm_mode_object_put() and should not be
 * used by new code.
 */
static inline void drm_mode_object_unreference(struct drm_mode_object *obj)
{
	drm_mode_object_put(obj);
}

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
