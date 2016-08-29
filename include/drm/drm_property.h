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

#ifndef __DRM_PROPERTY_H__
#define __DRM_PROPERTY_H__

#include <linux/list.h>
#include <linux/ctype.h>
#include <drm/drm_mode_object.h>

struct drm_property_blob {
	struct drm_mode_object base;
	struct drm_device *dev;
	struct list_head head_global;
	struct list_head head_file;
	size_t length;
	unsigned char data[];
};

struct drm_property_enum {
	uint64_t value;
	struct list_head head;
	char name[DRM_PROP_NAME_LEN];
};

struct drm_property {
	struct list_head head;
	struct drm_mode_object base;
	uint32_t flags;
	char name[DRM_PROP_NAME_LEN];
	uint32_t num_values;
	uint64_t *values;
	struct drm_device *dev;

	struct list_head enum_list;
};

struct drm_prop_enum_list {
	int type;
	char *name;
};

#define obj_to_property(x) container_of(x, struct drm_property, base)

static inline bool drm_property_type_is(struct drm_property *property,
		uint32_t type)
{
	/* instanceof for props.. handles extended type vs original types: */
	if (property->flags & DRM_MODE_PROP_EXTENDED_TYPE)
		return (property->flags & DRM_MODE_PROP_EXTENDED_TYPE) == type;
	return property->flags & type;
}

struct drm_property *drm_property_create(struct drm_device *dev, int flags,
					 const char *name, int num_values);
struct drm_property *drm_property_create_enum(struct drm_device *dev, int flags,
					      const char *name,
					      const struct drm_prop_enum_list *props,
					      int num_values);
struct drm_property *drm_property_create_bitmask(struct drm_device *dev,
						 int flags, const char *name,
						 const struct drm_prop_enum_list *props,
						 int num_props,
						 uint64_t supported_bits);
struct drm_property *drm_property_create_range(struct drm_device *dev, int flags,
					       const char *name,
					       uint64_t min, uint64_t max);
struct drm_property *drm_property_create_signed_range(struct drm_device *dev,
						      int flags, const char *name,
						      int64_t min, int64_t max);
struct drm_property *drm_property_create_object(struct drm_device *dev,
						int flags, const char *name, uint32_t type);
struct drm_property *drm_property_create_bool(struct drm_device *dev, int flags,
					      const char *name);
int drm_property_add_enum(struct drm_property *property, int index,
			  uint64_t value, const char *name);
void drm_property_destroy(struct drm_device *dev, struct drm_property *property);

struct drm_property_blob *drm_property_create_blob(struct drm_device *dev,
						   size_t length,
						   const void *data);
struct drm_property_blob *drm_property_lookup_blob(struct drm_device *dev,
						   uint32_t id);
int drm_property_replace_global_blob(struct drm_device *dev,
				     struct drm_property_blob **replace,
				     size_t length,
				     const void *data,
				     struct drm_mode_object *obj_holds_id,
				     struct drm_property *prop_holds_id);
struct drm_property_blob *drm_property_reference_blob(struct drm_property_blob *blob);
void drm_property_unreference_blob(struct drm_property_blob *blob);

static inline struct drm_property *drm_property_find(struct drm_device *dev,
		uint32_t id)
{
	struct drm_mode_object *mo;
	mo = drm_mode_object_find(dev, id, DRM_MODE_OBJECT_PROPERTY);
	return mo ? obj_to_property(mo) : NULL;
}

#endif
