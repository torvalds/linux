/*
 * Copyright (c) 2017, Mellanox Technologies inc.  All rights reserved.
 *
 * This software is available to you under a choice of one of two
 * licenses.  You may choose to be licensed under the terms of the GNU
 * General Public License (GPL) Version 2, available from the file
 * COPYING in the main directory of this source tree, or the
 * OpenIB.org BSD license below:
 *
 *     Redistribution and use in source and binary forms, with or
 *     without modification, are permitted provided that the following
 *     conditions are met:
 *
 *      - Redistributions of source code must retain the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer.
 *
 *      - Redistributions in binary form must reproduce the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer in the documentation and/or other materials
 *        provided with the distribution.
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

#ifndef _UVERBS_IOCTL_
#define _UVERBS_IOCTL_

#include <rdma/uverbs_types.h>

/*
 * =======================================
 *	Verbs action specifications
 * =======================================
 */

enum uverbs_attr_type {
	UVERBS_ATTR_TYPE_NA,
	UVERBS_ATTR_TYPE_PTR_IN,
	UVERBS_ATTR_TYPE_PTR_OUT,
	UVERBS_ATTR_TYPE_IDR,
	UVERBS_ATTR_TYPE_FD,
};

enum uverbs_obj_access {
	UVERBS_ACCESS_READ,
	UVERBS_ACCESS_WRITE,
	UVERBS_ACCESS_NEW,
	UVERBS_ACCESS_DESTROY
};

enum {
	UVERBS_ATTR_SPEC_F_MANDATORY	= 1U << 0,
	/* Support extending attributes by length */
	UVERBS_ATTR_SPEC_F_MIN_SZ	= 1U << 1,
};

struct uverbs_attr_spec {
	enum uverbs_attr_type		type;
	union {
		u16				len;
		struct {
			/*
			 * higher bits mean the namespace and lower bits mean
			 * the type id within the namespace.
			 */
			u16			obj_type;
			u8			access;
		} obj;
	};
	/* Combination of bits from enum UVERBS_ATTR_SPEC_F_XXXX */
	u8				flags;
};

struct uverbs_attr_spec_hash {
	size_t				num_attrs;
	unsigned long			*mandatory_attrs_bitmask;
	struct uverbs_attr_spec		attrs[0];
};

struct uverbs_attr_bundle;
struct ib_uverbs_file;

enum {
	/*
	 * Action marked with this flag creates a context (or root for all
	 * objects).
	 */
	UVERBS_ACTION_FLAG_CREATE_ROOT = 1U << 0,
};

struct uverbs_method_spec {
	/* Combination of bits from enum UVERBS_ACTION_FLAG_XXXX */
	u32						flags;
	size_t						num_buckets;
	size_t						num_child_attrs;
	int (*handler)(struct ib_device *ib_dev, struct ib_uverbs_file *ufile,
		       struct uverbs_attr_bundle *ctx);
	struct uverbs_attr_spec_hash		*attr_buckets[0];
};

struct uverbs_method_spec_hash {
	size_t					num_methods;
	struct uverbs_method_spec		*methods[0];
};

struct uverbs_object_spec {
	const struct uverbs_obj_type		*type_attrs;
	size_t					num_buckets;
	struct uverbs_method_spec_hash		*method_buckets[0];
};

struct uverbs_object_spec_hash {
	size_t					num_objects;
	struct uverbs_object_spec		*objects[0];
};

struct uverbs_root_spec {
	size_t					num_buckets;
	struct uverbs_object_spec_hash		*object_buckets[0];
};

/*
 * =======================================
 *	Verbs definitions
 * =======================================
 */

struct uverbs_attr_def {
	u16                           id;
	struct uverbs_attr_spec       attr;
};

struct uverbs_method_def {
	u16                                  id;
	/* Combination of bits from enum UVERBS_ACTION_FLAG_XXXX */
	u32				     flags;
	size_t				     num_attrs;
	const struct uverbs_attr_def * const (*attrs)[];
	int (*handler)(struct ib_device *ib_dev, struct ib_uverbs_file *ufile,
		       struct uverbs_attr_bundle *ctx);
};

struct uverbs_object_def {
	u16					 id;
	const struct uverbs_obj_type	        *type_attrs;
	size_t				         num_methods;
	const struct uverbs_method_def * const (*methods)[];
};

struct uverbs_object_tree_def {
	size_t					 num_objects;
	const struct uverbs_object_def * const (*objects)[];
};

#define _UVERBS_OBJECT(_id, _type_attrs, ...)				\
	((const struct uverbs_object_def) {				\
	 .id = _id,							\
	 .type_attrs = _type_attrs})
#define DECLARE_UVERBS_OBJECT(_name, _id, _type_attrs, ...)		\
	const struct uverbs_object_def _name =				\
		_UVERBS_OBJECT(_id, _type_attrs, ##__VA_ARGS__)
#define _UVERBS_TREE_OBJECTS_SZ(...)					\
	(sizeof((const struct uverbs_object_def * const []){__VA_ARGS__}) / \
	 sizeof(const struct uverbs_object_def *))
#define _UVERBS_OBJECT_TREE(...)					\
	((const struct uverbs_object_tree_def) {			\
	 .num_objects = _UVERBS_TREE_OBJECTS_SZ(__VA_ARGS__),		\
	 .objects = &(const struct uverbs_object_def * const []){__VA_ARGS__} })
#define DECLARE_UVERBS_OBJECT_TREE(_name, ...)				\
	const struct uverbs_object_tree_def _name =			\
		_UVERBS_OBJECT_TREE(__VA_ARGS__)

/* =================================================
 *              Parsing infrastructure
 * =================================================
 */

struct uverbs_ptr_attr {
	union {
		u64		data;
		void	__user *ptr;
	};
	u16		len;
	/* Combination of bits from enum UVERBS_ATTR_F_XXXX */
	u16		flags;
};

struct uverbs_obj_attr {
	/* pointer to the kernel descriptor -> type, access, etc */
	const struct uverbs_obj_type	*type;
	struct ib_uobject		*uobject;
	/* fd or id in idr of this object */
	int				id;
};

struct uverbs_attr {
	/*
	 * pointer to the user-space given attribute, in order to write the
	 * new uobject's id or update flags.
	 */
	struct ib_uverbs_attr __user	*uattr;
	union {
		struct uverbs_ptr_attr	ptr_attr;
		struct uverbs_obj_attr	obj_attr;
	};
};

struct uverbs_attr_bundle_hash {
	/* if bit i is set, it means attrs[i] contains valid information */
	unsigned long *valid_bitmap;
	size_t num_attrs;
	/*
	 * arrays of attributes, each element corresponds to the specification
	 * of the attribute in the same index.
	 */
	struct uverbs_attr *attrs;
};

struct uverbs_attr_bundle {
	size_t				num_buckets;
	struct uverbs_attr_bundle_hash  hash[];
};

static inline bool uverbs_attr_is_valid_in_hash(const struct uverbs_attr_bundle_hash *attrs_hash,
						unsigned int idx)
{
	return test_bit(idx, attrs_hash->valid_bitmap);
}

#endif

