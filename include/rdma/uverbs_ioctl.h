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
	UVERBS_ATTR_TYPE_IDRS_ARRAY,
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
	/* True if this is from UVERBS_ATTR_UHW */
	u8 is_udata:1;

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

	/* This weird split lets us remove some padding */
	union {
		struct {
			/*
			 * The enum attribute can select one of the attributes
			 * contained in the ids array. Currently only PTR_IN
			 * attributes are supported in the ids array.
			 */
			const struct uverbs_attr_spec *ids;
		} enum_def;

		struct {
			/*
			 * higher bits mean the namespace and lower bits mean
			 * the type id within the namespace.
			 */
			u16				obj_type;
			u16				min_len;
			u16				max_len;
			u8				access;
		} objs_arr;
	} u2;
};

/*
 * Information about the API is loaded into a radix tree. For IOCTL we start
 * with a tuple of:
 *  object_id, attr_id, method_id
 *
 * Which is a 48 bit value, with most of the bits guaranteed to be zero. Based
 * on the current kernel support this is compressed into 16 bit key for the
 * radix tree. Since this compression is entirely internal to the kernel the
 * below limits can be revised if the kernel gains additional data.
 *
 * With 64 leafs per node this is a 3 level radix tree.
 *
 * The tree encodes multiple types, and uses a scheme where OBJ_ID,0,0 returns
 * the object slot, and OBJ_ID,METH_ID,0 and returns the method slot.
 *
 * This also encodes the tables for the write() and write() extended commands
 * using the coding
 *   OBJ_ID,UVERBS_API_METHOD_IS_WRITE,command #
 *   OBJ_ID,UVERBS_API_METHOD_IS_WRITE_EX,command_ex #
 * ie the WRITE path is treated as a special method type in the ioctl
 * framework.
 */
enum uapi_radix_data {
	UVERBS_API_NS_FLAG = 1U << UVERBS_ID_NS_SHIFT,

	UVERBS_API_ATTR_KEY_BITS = 6,
	UVERBS_API_ATTR_KEY_MASK = GENMASK(UVERBS_API_ATTR_KEY_BITS - 1, 0),
	UVERBS_API_ATTR_BKEY_LEN = (1 << UVERBS_API_ATTR_KEY_BITS) - 1,
	UVERBS_API_WRITE_KEY_NUM = 1 << UVERBS_API_ATTR_KEY_BITS,

	UVERBS_API_METHOD_KEY_BITS = 5,
	UVERBS_API_METHOD_KEY_SHIFT = UVERBS_API_ATTR_KEY_BITS,
	UVERBS_API_METHOD_KEY_NUM_CORE = 22,
	UVERBS_API_METHOD_IS_WRITE = 30 << UVERBS_API_METHOD_KEY_SHIFT,
	UVERBS_API_METHOD_IS_WRITE_EX = 31 << UVERBS_API_METHOD_KEY_SHIFT,
	UVERBS_API_METHOD_KEY_NUM_DRIVER =
		(UVERBS_API_METHOD_IS_WRITE >> UVERBS_API_METHOD_KEY_SHIFT) -
		UVERBS_API_METHOD_KEY_NUM_CORE,
	UVERBS_API_METHOD_KEY_MASK = GENMASK(
		UVERBS_API_METHOD_KEY_BITS + UVERBS_API_METHOD_KEY_SHIFT - 1,
		UVERBS_API_METHOD_KEY_SHIFT),

	UVERBS_API_OBJ_KEY_BITS = 5,
	UVERBS_API_OBJ_KEY_SHIFT =
		UVERBS_API_METHOD_KEY_BITS + UVERBS_API_METHOD_KEY_SHIFT,
	UVERBS_API_OBJ_KEY_NUM_CORE = 20,
	UVERBS_API_OBJ_KEY_NUM_DRIVER =
		(1 << UVERBS_API_OBJ_KEY_BITS) - UVERBS_API_OBJ_KEY_NUM_CORE,
	UVERBS_API_OBJ_KEY_MASK = GENMASK(31, UVERBS_API_OBJ_KEY_SHIFT),

	/* This id guaranteed to not exist in the radix tree */
	UVERBS_API_KEY_ERR = 0xFFFFFFFF,
};

static inline __attribute_const__ u32 uapi_key_obj(u32 id)
{
	if (id & UVERBS_API_NS_FLAG) {
		id &= ~UVERBS_API_NS_FLAG;
		if (id >= UVERBS_API_OBJ_KEY_NUM_DRIVER)
			return UVERBS_API_KEY_ERR;
		id = id + UVERBS_API_OBJ_KEY_NUM_CORE;
	} else {
		if (id >= UVERBS_API_OBJ_KEY_NUM_CORE)
			return UVERBS_API_KEY_ERR;
	}

	return id << UVERBS_API_OBJ_KEY_SHIFT;
}

static inline __attribute_const__ bool uapi_key_is_object(u32 key)
{
	return (key & ~UVERBS_API_OBJ_KEY_MASK) == 0;
}

static inline __attribute_const__ u32 uapi_key_ioctl_method(u32 id)
{
	if (id & UVERBS_API_NS_FLAG) {
		id &= ~UVERBS_API_NS_FLAG;
		if (id >= UVERBS_API_METHOD_KEY_NUM_DRIVER)
			return UVERBS_API_KEY_ERR;
		id = id + UVERBS_API_METHOD_KEY_NUM_CORE;
	} else {
		id++;
		if (id >= UVERBS_API_METHOD_KEY_NUM_CORE)
			return UVERBS_API_KEY_ERR;
	}

	return id << UVERBS_API_METHOD_KEY_SHIFT;
}

static inline __attribute_const__ u32 uapi_key_write_method(u32 id)
{
	if (id >= UVERBS_API_WRITE_KEY_NUM)
		return UVERBS_API_KEY_ERR;
	return UVERBS_API_METHOD_IS_WRITE | id;
}

static inline __attribute_const__ u32 uapi_key_write_ex_method(u32 id)
{
	if (id >= UVERBS_API_WRITE_KEY_NUM)
		return UVERBS_API_KEY_ERR;
	return UVERBS_API_METHOD_IS_WRITE_EX | id;
}

static inline __attribute_const__ u32
uapi_key_attr_to_ioctl_method(u32 attr_key)
{
	return attr_key &
	       (UVERBS_API_OBJ_KEY_MASK | UVERBS_API_METHOD_KEY_MASK);
}

static inline __attribute_const__ bool uapi_key_is_ioctl_method(u32 key)
{
	unsigned int method = key & UVERBS_API_METHOD_KEY_MASK;

	return method != 0 && method < UVERBS_API_METHOD_IS_WRITE &&
	       (key & UVERBS_API_ATTR_KEY_MASK) == 0;
}

static inline __attribute_const__ bool uapi_key_is_write_method(u32 key)
{
	return (key & UVERBS_API_METHOD_KEY_MASK) == UVERBS_API_METHOD_IS_WRITE;
}

static inline __attribute_const__ bool uapi_key_is_write_ex_method(u32 key)
{
	return (key & UVERBS_API_METHOD_KEY_MASK) ==
	       UVERBS_API_METHOD_IS_WRITE_EX;
}

static inline __attribute_const__ u32 uapi_key_attrs_start(u32 ioctl_method_key)
{
	/* 0 is the method slot itself */
	return ioctl_method_key + 1;
}

static inline __attribute_const__ u32 uapi_key_attr(u32 id)
{
	/*
	 * The attr is designed to fit in the typical single radix tree node
	 * of 64 entries. Since allmost all methods have driver attributes we
	 * organize things so that the driver and core attributes interleave to
	 * reduce the length of the attributes array in typical cases.
	 */
	if (id & UVERBS_API_NS_FLAG) {
		id &= ~UVERBS_API_NS_FLAG;
		id++;
		if (id >= 1 << (UVERBS_API_ATTR_KEY_BITS - 1))
			return UVERBS_API_KEY_ERR;
		id = (id << 1) | 0;
	} else {
		if (id >= 1 << (UVERBS_API_ATTR_KEY_BITS - 1))
			return UVERBS_API_KEY_ERR;
		id = (id << 1) | 1;
	}

	return id;
}

/* Only true for ioctl methods */
static inline __attribute_const__ bool uapi_key_is_attr(u32 key)
{
	unsigned int method = key & UVERBS_API_METHOD_KEY_MASK;

	return method != 0 && method < UVERBS_API_METHOD_IS_WRITE &&
	       (key & UVERBS_API_ATTR_KEY_MASK) != 0;
}

/*
 * This returns a value in the range [0 to UVERBS_API_ATTR_BKEY_LEN),
 * basically it undoes the reservation of 0 in the ID numbering. attr_key
 * must already be masked with UVERBS_API_ATTR_KEY_MASK, or be the output of
 * uapi_key_attr().
 */
static inline __attribute_const__ u32 uapi_bkey_attr(u32 attr_key)
{
	return attr_key - 1;
}

static inline __attribute_const__ u32 uapi_bkey_to_key_attr(u32 attr_bkey)
{
	return attr_bkey + 1;
}

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
	int (*handler)(struct uverbs_attr_bundle *attrs);
};

struct uverbs_object_def {
	u16					 id;
	const struct uverbs_obj_type	        *type_attrs;
	size_t				         num_methods;
	const struct uverbs_method_def * const (*methods)[];
};

enum uapi_definition_kind {
	UAPI_DEF_END = 0,
	UAPI_DEF_OBJECT_START,
	UAPI_DEF_WRITE,
	UAPI_DEF_CHAIN_OBJ_TREE,
	UAPI_DEF_CHAIN,
	UAPI_DEF_IS_SUPPORTED_FUNC,
	UAPI_DEF_IS_SUPPORTED_DEV_FN,
};

enum uapi_definition_scope {
	UAPI_SCOPE_OBJECT = 1,
	UAPI_SCOPE_METHOD = 2,
};

struct uapi_definition {
	u8 kind;
	u8 scope;
	union {
		struct {
			u16 object_id;
		} object_start;
		struct {
			u16 command_num;
			u8 is_ex:1;
			u8 has_udata:1;
			u8 has_resp:1;
			u8 req_size;
			u8 resp_size;
		} write;
	};

	union {
		bool (*func_is_supported)(struct ib_device *device);
		int (*func_write)(struct uverbs_attr_bundle *attrs);
		const struct uapi_definition *chain;
		const struct uverbs_object_def *chain_obj_tree;
		size_t needs_fn_offset;
	};
};

/* Define things connected to object_id */
#define DECLARE_UVERBS_OBJECT(_object_id, ...)                                 \
	{                                                                      \
		.kind = UAPI_DEF_OBJECT_START,                                 \
		.object_start = { .object_id = _object_id },                   \
	},                                                                     \
		##__VA_ARGS__

/* Use in a var_args of DECLARE_UVERBS_OBJECT */
#define DECLARE_UVERBS_WRITE(_command_num, _func, _cmd_desc, ...)              \
	{                                                                      \
		.kind = UAPI_DEF_WRITE,                                        \
		.scope = UAPI_SCOPE_OBJECT,                                    \
		.write = { .is_ex = 0, .command_num = _command_num },          \
		.func_write = _func,                                           \
		_cmd_desc,                                                     \
	},                                                                     \
		##__VA_ARGS__

/* Use in a var_args of DECLARE_UVERBS_OBJECT */
#define DECLARE_UVERBS_WRITE_EX(_command_num, _func, _cmd_desc, ...)           \
	{                                                                      \
		.kind = UAPI_DEF_WRITE,                                        \
		.scope = UAPI_SCOPE_OBJECT,                                    \
		.write = { .is_ex = 1, .command_num = _command_num },          \
		.func_write = _func,                                           \
		_cmd_desc,                                                     \
	},                                                                     \
		##__VA_ARGS__

/*
 * Object is only supported if the function pointer named ibdev_fn in struct
 * ib_device is not NULL.
 */
#define UAPI_DEF_OBJ_NEEDS_FN(ibdev_fn)                                        \
	{                                                                      \
		.kind = UAPI_DEF_IS_SUPPORTED_DEV_FN,                          \
		.scope = UAPI_SCOPE_OBJECT,                                    \
		.needs_fn_offset =                                             \
			offsetof(struct ib_device_ops, ibdev_fn) +             \
			BUILD_BUG_ON_ZERO(sizeof_field(struct ib_device_ops,   \
						       ibdev_fn) !=            \
					  sizeof(void *)),                     \
	}

/*
 * Method is only supported if the function pointer named ibdev_fn in struct
 * ib_device is not NULL.
 */
#define UAPI_DEF_METHOD_NEEDS_FN(ibdev_fn)                                     \
	{                                                                      \
		.kind = UAPI_DEF_IS_SUPPORTED_DEV_FN,                          \
		.scope = UAPI_SCOPE_METHOD,                                    \
		.needs_fn_offset =                                             \
			offsetof(struct ib_device_ops, ibdev_fn) +             \
			BUILD_BUG_ON_ZERO(sizeof_field(struct ib_device_ops,   \
						       ibdev_fn) !=            \
					  sizeof(void *)),                     \
	}

/* Call a function to determine if the entire object is supported or not */
#define UAPI_DEF_IS_OBJ_SUPPORTED(_func)                                       \
	{                                                                      \
		.kind = UAPI_DEF_IS_SUPPORTED_FUNC,                            \
		.scope = UAPI_SCOPE_OBJECT, .func_is_supported = _func,        \
	}

/* Include another struct uapi_definition in this one */
#define UAPI_DEF_CHAIN(_def_var)                                               \
	{                                                                      \
		.kind = UAPI_DEF_CHAIN, .chain = _def_var,                     \
	}

/* Temporary until the tree base description is replaced */
#define UAPI_DEF_CHAIN_OBJ_TREE(_object_enum, _object_ptr, ...)                \
	{                                                                      \
		.kind = UAPI_DEF_CHAIN_OBJ_TREE,                               \
		.object_start = { .object_id = _object_enum },                 \
		.chain_obj_tree = _object_ptr,                                 \
	},								       \
		##__VA_ARGS__
#define UAPI_DEF_CHAIN_OBJ_TREE_NAMED(_object_enum, ...)                       \
	UAPI_DEF_CHAIN_OBJ_TREE(_object_enum, &UVERBS_OBJECT(_object_enum),    \
				##__VA_ARGS__)

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
	UVERBS_ATTR_SIZE(offsetofend(_type, _last), sizeof(_type))
/*
 * Specifies at least min_len bytes must be passed in, but the amount can be
 * larger, up to the protocol maximum size. No check for zeroing is done.
 */
#define UVERBS_ATTR_MIN_SIZE(_min_len) UVERBS_ATTR_SIZE(_min_len, USHRT_MAX)

/* Must be used in the '...' of any UVERBS_ATTR */
#define UA_ALLOC_AND_COPY .alloc_and_copy = 1
#define UA_MANDATORY .mandatory = 1
#define UA_OPTIONAL .mandatory = 0

/*
 * min_len must be bigger than 0 and _max_len must be smaller than 4095.  Only
 * READ\WRITE accesses are supported.
 */
#define UVERBS_ATTR_IDRS_ARR(_attr_id, _idr_type, _access, _min_len, _max_len, \
			     ...)                                              \
	(&(const struct uverbs_attr_def){                                      \
		.id = (_attr_id) +                                             \
		      BUILD_BUG_ON_ZERO((_min_len) == 0 ||                     \
					(_max_len) >                           \
						PAGE_SIZE / sizeof(void *) ||  \
					(_min_len) > (_max_len) ||             \
					(_access) == UVERBS_ACCESS_NEW ||      \
					(_access) == UVERBS_ACCESS_DESTROY),   \
		.attr = { .type = UVERBS_ATTR_TYPE_IDRS_ARRAY,                 \
			  .u2.objs_arr.obj_type = _idr_type,                   \
			  .u2.objs_arr.access = _access,                       \
			  .u2.objs_arr.min_len = _min_len,                     \
			  .u2.objs_arr.max_len = _max_len,                     \
			  __VA_ARGS__ } })

/*
 * Only for use with UVERBS_ATTR_IDR, allows any uobject type to be accepted,
 * the user must validate the type of the uobject instead.
 */
#define UVERBS_IDR_ANY_OBJECT 0xFFFF

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

/* An input value that is a member in the enum _enum_type. */
#define UVERBS_ATTR_CONST_IN(_attr_id, _enum_type, ...)                        \
	UVERBS_ATTR_PTR_IN(                                                    \
		_attr_id,                                                      \
		UVERBS_ATTR_SIZE(                                              \
			sizeof(u64) + BUILD_BUG_ON_ZERO(!sizeof(_enum_type)),  \
			sizeof(u64)),                                          \
		__VA_ARGS__)

/*
 * An input value that is a bitwise combination of values of _enum_type.
 * This permits the flag value to be passed as either a u32 or u64, it must
 * be retrieved via uverbs_get_flag().
 */
#define UVERBS_ATTR_FLAGS_IN(_attr_id, _enum_type, ...)                        \
	UVERBS_ATTR_PTR_IN(                                                    \
		_attr_id,                                                      \
		UVERBS_ATTR_SIZE(sizeof(u32) + BUILD_BUG_ON_ZERO(              \
						       !sizeof(_enum_type *)), \
				 sizeof(u64)),                                 \
		__VA_ARGS__)

/*
 * This spec is used in order to pass information to the hardware driver in a
 * legacy way. Every verb that could get driver specific data should get this
 * spec.
 */
#define UVERBS_ATTR_UHW()                                                      \
	UVERBS_ATTR_PTR_IN(UVERBS_ATTR_UHW_IN,                                 \
			   UVERBS_ATTR_MIN_SIZE(0),			       \
			   UA_OPTIONAL,                                        \
			   .is_udata = 1),				       \
	UVERBS_ATTR_PTR_OUT(UVERBS_ATTR_UHW_OUT,                               \
			    UVERBS_ATTR_MIN_SIZE(0),			       \
			    UA_OPTIONAL,                                       \
			    .is_udata = 1)

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
	u16		uattr_idx;
	u8		enum_id;
};

struct uverbs_obj_attr {
	struct ib_uobject		*uobject;
	const struct uverbs_api_attr	*attr_elm;
};

struct uverbs_objs_arr_attr {
	struct ib_uobject **uobjects;
	u16 len;
};

struct uverbs_attr {
	union {
		struct uverbs_ptr_attr	ptr_attr;
		struct uverbs_obj_attr	obj_attr;
		struct uverbs_objs_arr_attr objs_arr_attr;
	};
};

struct uverbs_attr_bundle {
	struct ib_udata driver_udata;
	struct ib_udata ucore;
	struct ib_uverbs_file *ufile;
	struct ib_ucontext *context;
	DECLARE_BITMAP(attr_present, UVERBS_API_ATTR_BKEY_LEN);
	struct uverbs_attr attrs[];
};

static inline bool uverbs_attr_is_valid(const struct uverbs_attr_bundle *attrs_bundle,
					unsigned int idx)
{
	return test_bit(uapi_bkey_attr(uapi_key_attr(idx)),
			attrs_bundle->attr_present);
}

/**
 * rdma_udata_to_drv_context - Helper macro to get the driver's context out of
 *                             ib_udata which is embedded in uverbs_attr_bundle.
 *
 * If udata is not NULL this cannot fail. Otherwise a NULL udata will result
 * in a NULL ucontext pointer, as a safety precaution. Callers should be using
 * 'udata' to determine if the driver call is in user or kernel mode, not
 * 'ucontext'.
 *
 */
#define rdma_udata_to_drv_context(udata, drv_dev_struct, member)               \
	(udata ? container_of(container_of(udata, struct uverbs_attr_bundle,   \
					   driver_udata)                       \
				      ->context,                               \
			      drv_dev_struct, member) :                        \
		 (drv_dev_struct *)NULL)

#define IS_UVERBS_COPY_ERR(_ret)		((_ret) && (_ret) != -ENOENT)

static inline const struct uverbs_attr *uverbs_attr_get(const struct uverbs_attr_bundle *attrs_bundle,
							u16 idx)
{
	if (!uverbs_attr_is_valid(attrs_bundle, idx))
		return ERR_PTR(-ENOENT);

	return &attrs_bundle->attrs[uapi_bkey_attr(uapi_key_attr(idx))];
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

void uverbs_finalize_uobj_create(const struct uverbs_attr_bundle *attrs_bundle,
				 u16 idx);

/*
 * uverbs_attr_ptr_get_array_size() - Get array size pointer by a ptr
 * attribute.
 * @attrs: The attribute bundle
 * @idx: The ID of the attribute
 * @elem_size: The size of the element in the array
 */
static inline int
uverbs_attr_ptr_get_array_size(struct uverbs_attr_bundle *attrs, u16 idx,
			       size_t elem_size)
{
	int size = uverbs_attr_get_len(attrs, idx);

	if (size < 0)
		return size;

	if (size % elem_size)
		return -EINVAL;

	return size / elem_size;
}

/**
 * uverbs_attr_get_uobjs_arr() - Provides array's properties for attribute for
 * UVERBS_ATTR_TYPE_IDRS_ARRAY.
 * @arr: Returned pointer to array of pointers for uobjects or NULL if
 *       the attribute isn't provided.
 *
 * Return: The array length or 0 if no attribute was provided.
 */
static inline int uverbs_attr_get_uobjs_arr(
	const struct uverbs_attr_bundle *attrs_bundle, u16 attr_idx,
	struct ib_uobject ***arr)
{
	const struct uverbs_attr *attr =
			uverbs_attr_get(attrs_bundle, attr_idx);

	if (IS_ERR(attr)) {
		*arr = NULL;
		return 0;
	}

	*arr = attr->objs_arr_attr.uobjects;

	return attr->objs_arr_attr.len;
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

static inline struct ib_ucontext *
ib_uverbs_get_ucontext(const struct uverbs_attr_bundle *attrs)
{
	return ib_uverbs_get_ucontext_file(attrs->ufile);
}

#if IS_ENABLED(CONFIG_INFINIBAND_USER_ACCESS)
int uverbs_get_flags64(u64 *to, const struct uverbs_attr_bundle *attrs_bundle,
		       size_t idx, u64 allowed_bits);
int uverbs_get_flags32(u32 *to, const struct uverbs_attr_bundle *attrs_bundle,
		       size_t idx, u64 allowed_bits);
int uverbs_copy_to(const struct uverbs_attr_bundle *attrs_bundle, size_t idx,
		   const void *from, size_t size);
__malloc void *_uverbs_alloc(struct uverbs_attr_bundle *bundle, size_t size,
			     gfp_t flags);

static inline __malloc void *uverbs_alloc(struct uverbs_attr_bundle *bundle,
					  size_t size)
{
	return _uverbs_alloc(bundle, size, GFP_KERNEL);
}

static inline __malloc void *uverbs_zalloc(struct uverbs_attr_bundle *bundle,
					   size_t size)
{
	return _uverbs_alloc(bundle, size, GFP_KERNEL | __GFP_ZERO);
}
int _uverbs_get_const(s64 *to, const struct uverbs_attr_bundle *attrs_bundle,
		      size_t idx, s64 lower_bound, u64 upper_bound,
		      s64 *def_val);
int uverbs_copy_to_struct_or_zero(const struct uverbs_attr_bundle *bundle,
				  size_t idx, const void *from, size_t size);
#else
static inline int
uverbs_get_flags64(u64 *to, const struct uverbs_attr_bundle *attrs_bundle,
		   size_t idx, u64 allowed_bits)
{
	return -EINVAL;
}
static inline int
uverbs_get_flags32(u32 *to, const struct uverbs_attr_bundle *attrs_bundle,
		   size_t idx, u64 allowed_bits)
{
	return -EINVAL;
}
static inline int uverbs_copy_to(const struct uverbs_attr_bundle *attrs_bundle,
				 size_t idx, const void *from, size_t size)
{
	return -EINVAL;
}
static inline __malloc void *uverbs_alloc(struct uverbs_attr_bundle *bundle,
					  size_t size)
{
	return ERR_PTR(-EINVAL);
}
static inline __malloc void *uverbs_zalloc(struct uverbs_attr_bundle *bundle,
					   size_t size)
{
	return ERR_PTR(-EINVAL);
}
static inline int
_uverbs_get_const(s64 *to, const struct uverbs_attr_bundle *attrs_bundle,
		  size_t idx, s64 lower_bound, u64 upper_bound,
		  s64 *def_val)
{
	return -EINVAL;
}
static inline int
uverbs_copy_to_struct_or_zero(const struct uverbs_attr_bundle *bundle,
			      size_t idx, const void *from, size_t size)
{
	return -EINVAL;
}
#endif

#define uverbs_get_const(_to, _attrs_bundle, _idx)                             \
	({                                                                     \
		s64 _val;                                                      \
		int _ret = _uverbs_get_const(&_val, _attrs_bundle, _idx,       \
					     type_min(typeof(*_to)),           \
					     type_max(typeof(*_to)), NULL);    \
		(*_to) = _val;                                                 \
		_ret;                                                          \
	})

#define uverbs_get_const_default(_to, _attrs_bundle, _idx, _default)           \
	({                                                                     \
		s64 _val;                                                      \
		s64 _def_val = _default;                                       \
		int _ret =                                                     \
			_uverbs_get_const(&_val, _attrs_bundle, _idx,          \
					  type_min(typeof(*_to)),              \
					  type_max(typeof(*_to)), &_def_val);  \
		(*_to) = _val;                                                 \
		_ret;                                                          \
	})
#endif
