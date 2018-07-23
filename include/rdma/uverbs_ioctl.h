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
#include <linux/uaccess.h>
#include <rdma/rdma_user_ioctl.h>
#include <rdma/ib_user_ioctl_verbs.h>
#include <rdma/ib_user_ioctl_cmds.h>

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
	UVERBS_ATTR_TYPE_ENUM_IN,
};

enum uverbs_obj_access {
	UVERBS_ACCESS_READ,
	UVERBS_ACCESS_WRITE,
	UVERBS_ACCESS_NEW,
	UVERBS_ACCESS_DESTROY
};

/* Specification of a single attribute inside the ioctl message */
/* good size 16 */
struct uverbs_attr_spec {
	u8 type;

	/*
	 * Support extending attributes by length. Allow the user to provide
	 * more bytes than ptr.len, but check that everything after is zero'd
	 * by the user.
	 */
	u8 zero_trailing:1;
	/*
	 * Valid only for PTR_IN. Allocate and copy the data inside
	 * the parser
	 */
	u8 alloc_and_copy:1;
	u8 mandatory:1;

	union {
		struct {
			/* Current known size to kernel */
			u16 len;
			/* User isn't allowed to provide something < min_len */
			u16 min_len;
		} ptr;

		struct {
			/*
			 * higher bits mean the namespace and lower bits mean
			 * the type id within the namespace.
			 */
			u16 obj_type;
			u8 access;
		} obj;

		struct {
			u8 num_elems;
		} enum_def;
	} u;

	/* This weird split of the enum lets us remove some padding */
	union {
		struct {
			/*
			 * The enum attribute can select one of the attributes
			 * contained in the ids array. Currently only PTR_IN
			 * attributes are supported in the ids array.
			 */
			const struct uverbs_attr_spec *ids;
		} enum_def;
	} u2;
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

/*
 * =======================================
 *	Attribute Specifications
 * =======================================
 */

#define UVERBS_ATTR_SIZE(_min_len, _len)			\
	.u.ptr.min_len = _min_len, .u.ptr.len = _len

#define UVERBS_ATTR_NO_DATA() UVERBS_ATTR_SIZE(0, 0)

/*
 * Specifies a uapi structure that cannot be extended. The user must always
 * supply the whole structure and nothing more. The structure must be declared
 * in a header under include/uapi/rdma.
 */
#define UVERBS_ATTR_TYPE(_type)					\
	.u.ptr.min_len = sizeof(_type), .u.ptr.len = sizeof(_type)
/*
 * Specifies a uapi structure where the user must provide at least up to
 * member 'last'.  Anything after last and up until the end of the structure
 * can be non-zero, anything longer than the end of the structure must be
 * zero. The structure must be declared in a header under include/uapi/rdma.
 */
#define UVERBS_ATTR_STRUCT(_type, _last)                                       \
	.zero_trailing = 1,                                                    \
	UVERBS_ATTR_SIZE(((uintptr_t)(&((_type *)0)->_last + 1)),              \
			 sizeof(_type))
/*
 * Specifies at least min_len bytes must be passed in, but the amount can be
 * larger, up to the protocol maximum size. No check for zeroing is done.
 */
#define UVERBS_ATTR_MIN_SIZE(_min_len) UVERBS_ATTR_SIZE(_min_len, USHRT_MAX)

/* Must be used in the '...' of any UVERBS_ATTR */
#define UA_ALLOC_AND_COPY .alloc_and_copy = 1
#define UA_MANDATORY .mandatory = 1
#define UA_OPTIONAL .mandatory = 0

#define UVERBS_ATTR_IDR(_attr_id, _idr_type, _access, ...)                     \
	(&(const struct uverbs_attr_def){                                      \
		.id = _attr_id,                                                \
		.attr = { .type = UVERBS_ATTR_TYPE_IDR,                        \
			  .u.obj.obj_type = _idr_type,                         \
			  .u.obj.access = _access,                             \
			  __VA_ARGS__ } })

#define UVERBS_ATTR_FD(_attr_id, _fd_type, _access, ...)                       \
	(&(const struct uverbs_attr_def){                                      \
		.id = (_attr_id) +                                             \
		      BUILD_BUG_ON_ZERO((_access) != UVERBS_ACCESS_NEW &&      \
					(_access) != UVERBS_ACCESS_READ),      \
		.attr = { .type = UVERBS_ATTR_TYPE_FD,                         \
			  .u.obj.obj_type = _fd_type,                          \
			  .u.obj.access = _access,                             \
			  __VA_ARGS__ } })

#define UVERBS_ATTR_PTR_IN(_attr_id, _type, ...)                               \
	(&(const struct uverbs_attr_def){                                      \
		.id = _attr_id,                                                \
		.attr = { .type = UVERBS_ATTR_TYPE_PTR_IN,                     \
			  _type,                                               \
			  __VA_ARGS__ } })

#define UVERBS_ATTR_PTR_OUT(_attr_id, _type, ...)                              \
	(&(const struct uverbs_attr_def){                                      \
		.id = _attr_id,                                                \
		.attr = { .type = UVERBS_ATTR_TYPE_PTR_OUT,                    \
			  _type,                                               \
			  __VA_ARGS__ } })

/* _enum_arry should be a 'static const union uverbs_attr_spec[]' */
#define UVERBS_ATTR_ENUM_IN(_attr_id, _enum_arr, ...)                          \
	(&(const struct uverbs_attr_def){                                      \
		.id = _attr_id,                                                \
		.attr = { .type = UVERBS_ATTR_TYPE_ENUM_IN,                    \
			  .u2.enum_def.ids = _enum_arr,                        \
			  .u.enum_def.num_elems = ARRAY_SIZE(_enum_arr),       \
			  __VA_ARGS__ },                                       \
	})

/*
 * This spec is used in order to pass information to the hardware driver in a
 * legacy way. Every verb that could get driver specific data should get this
 * spec.
 */
#define UVERBS_ATTR_UHW()                                                      \
	UVERBS_ATTR_PTR_IN(UVERBS_ATTR_UHW_IN,                                 \
			   UVERBS_ATTR_MIN_SIZE(0),			       \
			   UA_OPTIONAL),				       \
	UVERBS_ATTR_PTR_OUT(UVERBS_ATTR_UHW_OUT,                               \
			    UVERBS_ATTR_MIN_SIZE(0),			       \
			    UA_OPTIONAL)

/*
 * =======================================
 *	Declaration helpers
 * =======================================
 */

#define DECLARE_UVERBS_OBJECT_TREE(_name, ...)                                 \
	static const struct uverbs_object_def *const _name##_ptr[] = {         \
		__VA_ARGS__,                                                   \
	};                                                                     \
	static const struct uverbs_object_tree_def _name = {                   \
		.num_objects = ARRAY_SIZE(_name##_ptr),                        \
		.objects = &_name##_ptr,                                       \
	}

/* =================================================
 *              Parsing infrastructure
 * =================================================
 */

struct uverbs_ptr_attr {
	/*
	 * If UVERBS_ATTR_SPEC_F_ALLOC_AND_COPY is set then the 'ptr' is
	 * used.
	 */
	union {
		void *ptr;
		u64 data;
	};
	u16		len;
	/* Combination of bits from enum UVERBS_ATTR_F_XXXX */
	u16		flags;
	u8		enum_id;
};

struct uverbs_obj_attr {
	struct ib_uobject		*uobject;
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

static inline bool uverbs_attr_is_valid(const struct uverbs_attr_bundle *attrs_bundle,
					unsigned int idx)
{
	u16 idx_bucket = idx >>	UVERBS_ID_NS_SHIFT;

	if (attrs_bundle->num_buckets <= idx_bucket)
		return false;

	return uverbs_attr_is_valid_in_hash(&attrs_bundle->hash[idx_bucket],
					    idx & ~UVERBS_ID_NS_MASK);
}

#define IS_UVERBS_COPY_ERR(_ret)		((_ret) && (_ret) != -ENOENT)

static inline const struct uverbs_attr *uverbs_attr_get(const struct uverbs_attr_bundle *attrs_bundle,
							u16 idx)
{
	u16 idx_bucket = idx >>	UVERBS_ID_NS_SHIFT;

	if (!uverbs_attr_is_valid(attrs_bundle, idx))
		return ERR_PTR(-ENOENT);

	return &attrs_bundle->hash[idx_bucket].attrs[idx & ~UVERBS_ID_NS_MASK];
}

static inline int uverbs_attr_get_enum_id(const struct uverbs_attr_bundle *attrs_bundle,
					  u16 idx)
{
	const struct uverbs_attr *attr = uverbs_attr_get(attrs_bundle, idx);

	if (IS_ERR(attr))
		return PTR_ERR(attr);

	return attr->ptr_attr.enum_id;
}

static inline void *uverbs_attr_get_obj(const struct uverbs_attr_bundle *attrs_bundle,
					u16 idx)
{
	const struct uverbs_attr *attr;

	attr = uverbs_attr_get(attrs_bundle, idx);
	if (IS_ERR(attr))
		return ERR_CAST(attr);

	return attr->obj_attr.uobject->object;
}

static inline struct ib_uobject *uverbs_attr_get_uobject(const struct uverbs_attr_bundle *attrs_bundle,
							 u16 idx)
{
	const struct uverbs_attr *attr = uverbs_attr_get(attrs_bundle, idx);

	if (IS_ERR(attr))
		return ERR_CAST(attr);

	return attr->obj_attr.uobject;
}

static inline int
uverbs_attr_get_len(const struct uverbs_attr_bundle *attrs_bundle, u16 idx)
{
	const struct uverbs_attr *attr = uverbs_attr_get(attrs_bundle, idx);

	if (IS_ERR(attr))
		return PTR_ERR(attr);

	return attr->ptr_attr.len;
}

static inline int uverbs_copy_to(const struct uverbs_attr_bundle *attrs_bundle,
				 size_t idx, const void *from, size_t size)
{
	const struct uverbs_attr *attr = uverbs_attr_get(attrs_bundle, idx);
	u16 flags;
	size_t min_size;

	if (IS_ERR(attr))
		return PTR_ERR(attr);

	min_size = min_t(size_t, attr->ptr_attr.len, size);
	if (copy_to_user(u64_to_user_ptr(attr->ptr_attr.data), from, min_size))
		return -EFAULT;

	flags = attr->ptr_attr.flags | UVERBS_ATTR_F_VALID_OUTPUT;
	if (put_user(flags, &attr->uattr->flags))
		return -EFAULT;

	return 0;
}

static inline bool uverbs_attr_ptr_is_inline(const struct uverbs_attr *attr)
{
	return attr->ptr_attr.len <= sizeof(attr->ptr_attr.data);
}

static inline void *uverbs_attr_get_alloced_ptr(
	const struct uverbs_attr_bundle *attrs_bundle, u16 idx)
{
	const struct uverbs_attr *attr = uverbs_attr_get(attrs_bundle, idx);

	if (IS_ERR(attr))
		return (void *)attr;

	return uverbs_attr_ptr_is_inline(attr) ? (void *)&attr->ptr_attr.data :
						 attr->ptr_attr.ptr;
}

static inline int _uverbs_copy_from(void *to,
				    const struct uverbs_attr_bundle *attrs_bundle,
				    size_t idx,
				    size_t size)
{
	const struct uverbs_attr *attr = uverbs_attr_get(attrs_bundle, idx);

	if (IS_ERR(attr))
		return PTR_ERR(attr);

	/*
	 * Validation ensures attr->ptr_attr.len >= size. If the caller is
	 * using UVERBS_ATTR_SPEC_F_MIN_SZ_OR_ZERO then it must call
	 * uverbs_copy_from_or_zero.
	 */
	if (unlikely(size < attr->ptr_attr.len))
		return -EINVAL;

	if (uverbs_attr_ptr_is_inline(attr))
		memcpy(to, &attr->ptr_attr.data, attr->ptr_attr.len);
	else if (copy_from_user(to, u64_to_user_ptr(attr->ptr_attr.data),
				attr->ptr_attr.len))
		return -EFAULT;

	return 0;
}

static inline int _uverbs_copy_from_or_zero(void *to,
					    const struct uverbs_attr_bundle *attrs_bundle,
					    size_t idx,
					    size_t size)
{
	const struct uverbs_attr *attr = uverbs_attr_get(attrs_bundle, idx);
	size_t min_size;

	if (IS_ERR(attr))
		return PTR_ERR(attr);

	min_size = min_t(size_t, size, attr->ptr_attr.len);

	if (uverbs_attr_ptr_is_inline(attr))
		memcpy(to, &attr->ptr_attr.data, min_size);
	else if (copy_from_user(to, u64_to_user_ptr(attr->ptr_attr.data),
				min_size))
		return -EFAULT;

	if (size > min_size)
		memset(to + min_size, 0, size - min_size);

	return 0;
}

#define uverbs_copy_from(to, attrs_bundle, idx)				      \
	_uverbs_copy_from(to, attrs_bundle, idx, sizeof(*to))

#define uverbs_copy_from_or_zero(to, attrs_bundle, idx)			      \
	_uverbs_copy_from_or_zero(to, attrs_bundle, idx, sizeof(*to))

/* =================================================
 *	 Definitions -> Specs infrastructure
 * =================================================
 */

/*
 * uverbs_alloc_spec_tree - Merges different common and driver specific feature
 *	into one parsing tree that every uverbs command will be parsed upon.
 *
 * @num_trees: Number of trees in the array @trees.
 * @trees: Array of pointers to tree root definitions to merge. Each such tree
 *	   possibly contains objects, methods and attributes definitions.
 *
 * Returns:
 *	uverbs_root_spec *: The root of the merged parsing tree.
 *	On error, we return an error code. Error is checked via IS_ERR.
 *
 * The following merges could take place:
 * a. Two trees representing the same method with different handler
 *	-> We take the handler of the tree that its handler != NULL
 *	   and its index in the trees array is greater. The incentive for that
 *	   is that developers are expected to first merge common trees and then
 *	   merge trees that gives specialized the behaviour.
 * b. Two trees representing the same object with different
 *    type_attrs (struct uverbs_obj_type):
 *	-> We take the type_attrs of the tree that its type_attr != NULL
 *	   and its index in the trees array is greater. This could be used
 *	   in order to override the free function, allocation size, etc.
 * c. Two trees representing the same method attribute (same id but possibly
 *    different attributes):
 *	-> ERROR (-ENOENT), we believe that's not the programmer's intent.
 *
 * An object without any methods is considered invalid and will abort the
 * function with -ENOENT error.
 */
#if IS_ENABLED(CONFIG_INFINIBAND_USER_ACCESS)
struct uverbs_root_spec *uverbs_alloc_spec_tree(unsigned int num_trees,
						const struct uverbs_object_tree_def **trees);
void uverbs_free_spec_tree(struct uverbs_root_spec *root);
#else
static inline struct uverbs_root_spec *uverbs_alloc_spec_tree(unsigned int num_trees,
							      const struct uverbs_object_tree_def **trees)
{
	return NULL;
}

static inline void uverbs_free_spec_tree(struct uverbs_root_spec *root)
{
}
#endif

#endif
