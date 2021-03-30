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

#include <uapi/drm/drm_mode.h>

/**
 * struct drm_property_enum - symbolic values for enumerations
 * @value: numeric property value for this enum entry
 * @head: list of enum values, linked to &drm_property.enum_list
 * @name: symbolic name for the enum
 *
 * For enumeration and bitmask properties this structure stores the symbolic
 * decoding for each value. This is used for example for the rotation property.
 */
struct drm_property_enum {
	uint64_t value;
	struct list_head head;
	char name[DRM_PROP_NAME_LEN];
};

/**
 * struct drm_property - modeset object property
 *
 * This structure represent a modeset object property. It combines both the name
 * of the property with the set of permissible values. This means that when a
 * driver wants to use a property with the same name on different objects, but
 * with different value ranges, then it must create property for each one. An
 * example would be rotation of &drm_plane, when e.g. the primary plane cannot
 * be rotated. But if both the name and the value range match, then the same
 * property structure can be instantiated multiple times for the same object.
 * Userspace must be able to cope with this and cannot assume that the same
 * symbolic property will have the same modeset object ID on all modeset
 * objects.
 *
 * Properties are created by one of the special functions, as explained in
 * detail in the @flags structure member.
 *
 * To actually expose a property it must be attached to each object using
 * drm_object_attach_property(). Currently properties can only be attached to
 * &drm_connector, &drm_crtc and &drm_plane.
 *
 * Properties are also used as the generic metadatatransport for the atomic
 * IOCTL. Everything that was set directly in structures in the legacy modeset
 * IOCTLs (like the plane source or destination windows, or e.g. the links to
 * the CRTC) is exposed as a property with the DRM_MODE_PROP_ATOMIC flag set.
 */
struct drm_property {
	/**
	 * @head: per-device list of properties, for cleanup.
	 */
	struct list_head head;

	/**
	 * @base: base KMS object
	 */
	struct drm_mode_object base;

	/**
	 * @flags:
	 *
	 * Property flags and type. A property needs to be one of the following
	 * types:
	 *
	 * DRM_MODE_PROP_RANGE
	 *     Range properties report their minimum and maximum admissible unsigned values.
	 *     The KMS core verifies that values set by application fit in that
	 *     range. The range is unsigned. Range properties are created using
	 *     drm_property_create_range().
	 *
	 * DRM_MODE_PROP_SIGNED_RANGE
	 *     Range properties report their minimum and maximum admissible unsigned values.
	 *     The KMS core verifies that values set by application fit in that
	 *     range. The range is signed. Range properties are created using
	 *     drm_property_create_signed_range().
	 *
	 * DRM_MODE_PROP_ENUM
	 *     Enumerated properties take a numerical value that ranges from 0 to
	 *     the number of enumerated values defined by the property minus one,
	 *     and associate a free-formed string name to each value. Applications
	 *     can retrieve the list of defined value-name pairs and use the
	 *     numerical value to get and set property instance values. Enum
	 *     properties are created using drm_property_create_enum().
	 *
	 * DRM_MODE_PROP_BITMASK
	 *     Bitmask properties are enumeration properties that additionally
	 *     restrict all enumerated values to the 0..63 range. Bitmask property
	 *     instance values combine one or more of the enumerated bits defined
	 *     by the property. Bitmask properties are created using
	 *     drm_property_create_bitmask().
	 *
	 * DRM_MODE_PROP_OBJECT
	 *     Object properties are used to link modeset objects. This is used
	 *     extensively in the atomic support to create the display pipeline,
	 *     by linking &drm_framebuffer to &drm_plane, &drm_plane to
	 *     &drm_crtc and &drm_connector to &drm_crtc. An object property can
	 *     only link to a specific type of &drm_mode_object, this limit is
	 *     enforced by the core. Object properties are created using
	 *     drm_property_create_object().
	 *
	 *     Object properties work like blob properties, but in a more
	 *     general fashion. They are limited to atomic drivers and must have
	 *     the DRM_MODE_PROP_ATOMIC flag set.
	 *
	 * DRM_MODE_PROP_BLOB
	 *     Blob properties store a binary blob without any format restriction.
	 *     The binary blobs are created as KMS standalone objects, and blob
	 *     property instance values store the ID of their associated blob
	 *     object. Blob properties are created by calling
	 *     drm_property_create() with DRM_MODE_PROP_BLOB as the type.
	 *
	 *     Actual blob objects to contain blob data are created using
	 *     drm_property_create_blob(), or through the corresponding IOCTL.
	 *
	 *     Besides the built-in limit to only accept blob objects blob
	 *     properties work exactly like object properties. The only reasons
	 *     blob properties exist is backwards compatibility with existing
	 *     userspace.
	 *
	 * In addition a property can have any combination of the below flags:
	 *
	 * DRM_MODE_PROP_ATOMIC
	 *     Set for properties which encode atomic modeset state. Such
	 *     properties are not exposed to legacy userspace.
	 *
	 * DRM_MODE_PROP_IMMUTABLE
	 *     Set for properties whose values cannot be changed by
	 *     userspace. The kernel is allowed to update the value of these
	 *     properties. This is generally used to expose probe state to
	 *     userspace, e.g. the EDID, or the connector path property on DP
	 *     MST sinks. Kernel can update the value of an immutable property
	 *     by calling drm_object_property_set_value().
	 */
	uint32_t flags;

	/**
	 * @name: symbolic name of the properties
	 */
	char name[DRM_PROP_NAME_LEN];

	/**
	 * @num_values: size of the @values array.
	 */
	uint32_t num_values;

	/**
	 * @values:
	 *
	 * Array with limits and values for the property. The
	 * interpretation of these limits is dependent upon the type per @flags.
	 */
	uint64_t *values;

	/**
	 * @dev: DRM device
	 */
	struct drm_device *dev;

	/**
	 * @enum_list:
	 *
	 * List of &drm_prop_enum_list structures with the symbolic names for
	 * enum and bitmask values.
	 */
	struct list_head enum_list;
};

/**
 * struct drm_property_blob - Blob data for &drm_property
 * @base: base KMS object
 * @dev: DRM device
 * @head_global: entry on the global blob list in
 * 	&drm_mode_config.property_blob_list.
 * @head_file: entry on the per-file blob list in &drm_file.blobs list.
 * @length: size of the blob in bytes, invariant over the lifetime of the object
 * @data: actual data, embedded at the end of this structure
 *
 * Blobs are used to store bigger values than what fits directly into the 64
 * bits available for a &drm_property.
 *
 * Blobs are reference counted using drm_property_blob_get() and
 * drm_property_blob_put(). They are created using drm_property_create_blob().
 */
struct drm_property_blob {
	struct drm_mode_object base;
	struct drm_device *dev;
	struct list_head head_global;
	struct list_head head_file;
	size_t length;
	void *data;
};

struct drm_prop_enum_list {
	int type;
	const char *name;
};

#define obj_to_property(x) container_of(x, struct drm_property, base)
#define obj_to_blob(x) container_of(x, struct drm_property_blob, base)

/**
 * drm_property_type_is - check the type of a property
 * @property: property to check
 * @type: property type to compare with
 *
 * This is a helper function becauase the uapi encoding of property types is
 * a bit special for historical reasons.
 */
static inline bool drm_property_type_is(struct drm_property *property,
					uint32_t type)
{
	/* instanceof for props.. handles extended type vs original types: */
	if (property->flags & DRM_MODE_PROP_EXTENDED_TYPE)
		return (property->flags & DRM_MODE_PROP_EXTENDED_TYPE) == type;
	return property->flags & type;
}

struct drm_property *drm_property_create(struct drm_device *dev,
					 u32 flags, const char *name,
					 int num_values);
struct drm_property *drm_property_create_enum(struct drm_device *dev,
					      u32 flags, const char *name,
					      const struct drm_prop_enum_list *props,
					      int num_values);
struct drm_property *drm_property_create_bitmask(struct drm_device *dev,
						 u32 flags, const char *name,
						 const struct drm_prop_enum_list *props,
						 int num_props,
						 uint64_t supported_bits);
struct drm_property *drm_property_create_range(struct drm_device *dev,
					       u32 flags, const char *name,
					       uint64_t min, uint64_t max);
struct drm_property *drm_property_create_signed_range(struct drm_device *dev,
						      u32 flags, const char *name,
						      int64_t min, int64_t max);
struct drm_property *drm_property_create_object(struct drm_device *dev,
						u32 flags, const char *name,
						uint32_t type);
struct drm_property *drm_property_create_bool(struct drm_device *dev,
					      u32 flags, const char *name);
int drm_property_add_enum(struct drm_property *property,
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
bool drm_property_replace_blob(struct drm_property_blob **blob,
			       struct drm_property_blob *new_blob);
struct drm_property_blob *drm_property_blob_get(struct drm_property_blob *blob);
void drm_property_blob_put(struct drm_property_blob *blob);

/**
 * drm_property_find - find property object
 * @dev: DRM device
 * @file_priv: drm file to check for lease against.
 * @id: property object id
 *
 * This function looks up the property object specified by id and returns it.
 */
static inline struct drm_property *drm_property_find(struct drm_device *dev,
						     struct drm_file *file_priv,
						     uint32_t id)
{
	struct drm_mode_object *mo;
	mo = drm_mode_object_find(dev, file_priv, id, DRM_MODE_OBJECT_PROPERTY);
	return mo ? obj_to_property(mo) : NULL;
}

#endif
