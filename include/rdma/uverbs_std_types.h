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

#ifndef _UVERBS_STD_TYPES__
#define _UVERBS_STD_TYPES__

#include <rdma/uverbs_types.h>
#include <rdma/uverbs_ioctl.h>
#include <rdma/ib_user_ioctl_verbs.h>

#if IS_ENABLED(CONFIG_INFINIBAND_USER_ACCESS)
const struct uverbs_object_tree_def *uverbs_default_get_objects(void);
#else
static inline const struct uverbs_object_tree_def *uverbs_default_get_objects(void)
{
	return NULL;
}
#endif

/* Returns _id, or causes a compile error if _id is not a u32.
 *
 * The uobj APIs should only be used with the write based uAPI to access
 * object IDs. The write API must use a u32 for the object handle, which is
 * checked by this macro.
 */
#define _uobj_check_id(_id) ((_id) * typecheck(u32, _id))

#define uobj_get_type(_ufile, _object)                                         \
	uapi_get_object((_ufile)->device->uapi, _object)

#define uobj_get_read(_type, _id, _ufile)                                      \
	rdma_lookup_get_uobject(uobj_get_type(_ufile, _type), _ufile,          \
				_uobj_check_id(_id), UVERBS_LOOKUP_READ)

#define ufd_get_read(_type, _fdnum, _ufile)                                    \
	rdma_lookup_get_uobject(uobj_get_type(_ufile, _type), _ufile,          \
				(_fdnum)*typecheck(s32, _fdnum),               \
				UVERBS_LOOKUP_READ)

static inline void *_uobj_get_obj_read(struct ib_uobject *uobj)
{
	if (IS_ERR(uobj))
		return NULL;
	return uobj->object;
}
#define uobj_get_obj_read(_object, _type, _id, _ufile)                         \
	((struct ib_##_object *)_uobj_get_obj_read(                            \
		uobj_get_read(_type, _id, _ufile)))

#define uobj_get_write(_type, _id, _ufile)                                     \
	rdma_lookup_get_uobject(uobj_get_type(_ufile, _type), _ufile,          \
				_uobj_check_id(_id), UVERBS_LOOKUP_WRITE)

int __uobj_perform_destroy(const struct uverbs_api_object *obj, u32 id,
			   struct ib_uverbs_file *ufile, int success_res);
#define uobj_perform_destroy(_type, _id, _ufile, _success_res)                 \
	__uobj_perform_destroy(uobj_get_type(_ufile, _type),                   \
			       _uobj_check_id(_id), _ufile, _success_res)

struct ib_uobject *__uobj_get_destroy(const struct uverbs_api_object *obj,
				      u32 id, struct ib_uverbs_file *ufile);

#define uobj_get_destroy(_type, _id, _ufile)                                   \
	__uobj_get_destroy(uobj_get_type(_ufile, _type), _uobj_check_id(_id),  \
			   _ufile)

static inline void uobj_put_destroy(struct ib_uobject *uobj)
{
	rdma_lookup_put_uobject(uobj, UVERBS_LOOKUP_DESTROY);
}

static inline void uobj_put_read(struct ib_uobject *uobj)
{
	rdma_lookup_put_uobject(uobj, UVERBS_LOOKUP_READ);
}

#define uobj_put_obj_read(_obj)					\
	uobj_put_read((_obj)->uobject)

static inline void uobj_put_write(struct ib_uobject *uobj)
{
	rdma_lookup_put_uobject(uobj, UVERBS_LOOKUP_WRITE);
}

static inline int __must_check uobj_alloc_commit(struct ib_uobject *uobj,
						 int success_res)
{
	int ret = rdma_alloc_commit_uobject(uobj);

	if (ret)
		return ret;
	return success_res;
}

static inline void uobj_alloc_abort(struct ib_uobject *uobj)
{
	rdma_alloc_abort_uobject(uobj);
}

static inline struct ib_uobject *
__uobj_alloc(const struct uverbs_api_object *obj, struct ib_uverbs_file *ufile,
	     struct ib_device **ib_dev)
{
	struct ib_uobject *uobj = rdma_alloc_begin_uobject(obj, ufile);

	if (!IS_ERR(uobj))
		*ib_dev = uobj->context->device;
	return uobj;
}

#define uobj_alloc(_type, _ufile, _ib_dev)                                     \
	__uobj_alloc(uobj_get_type(_ufile, _type), _ufile, _ib_dev)

#endif

