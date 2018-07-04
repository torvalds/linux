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

static inline struct ib_uobject *__uobj_get(const struct uverbs_obj_type *type,
					    bool write,
					    struct ib_uverbs_file *ufile,
					    int id)
{
	return rdma_lookup_get_uobject(type, ufile, id, write);
}

#define uobj_get_type(_object) UVERBS_OBJECT(_object).type_attrs

#define uobj_get_read(_type, _id, _ufile)                                      \
	__uobj_get(uobj_get_type(_type), false, _ufile, _id)

static inline void *_uobj_get_obj_read(const struct uverbs_obj_type *type,
				       int id, struct ib_uverbs_file *ufile)
{
	struct ib_uobject *uobj = __uobj_get(type, false, ufile, id);

	if (IS_ERR(uobj))
		return NULL;
	return uobj->object;
}
#define uobj_get_obj_read(_object, _type, _id, _ufile)                         \
	((struct ib_##_object *)_uobj_get_obj_read(uobj_get_type(_type), _id,  \
						   _ufile))

#define uobj_get_write(_type, _id, _ufile)                                     \
	__uobj_get(uobj_get_type(_type), true, _ufile, _id)

int __uobj_perform_destroy(const struct uverbs_obj_type *type, int id,
			   struct ib_uverbs_file *ufile, int success_res);
#define uobj_perform_destroy(_type, _id, _ufile, _success_res)                 \
	__uobj_perform_destroy(uobj_get_type(_type), _id, _ufile, _success_res)

static inline void uobj_put_read(struct ib_uobject *uobj)
{
	rdma_lookup_put_uobject(uobj, false);
}

#define uobj_put_obj_read(_obj)					\
	uobj_put_read((_obj)->uobject)

static inline void uobj_put_write(struct ib_uobject *uobj)
{
	rdma_lookup_put_uobject(uobj, true);
}

static inline int __must_check uobj_remove_commit(struct ib_uobject *uobj)
{
	return rdma_remove_commit_uobject(uobj);
}

static inline void uobj_alloc_commit(struct ib_uobject *uobj)
{
	rdma_alloc_commit_uobject(uobj);
}

static inline void uobj_alloc_abort(struct ib_uobject *uobj)
{
	rdma_alloc_abort_uobject(uobj);
}

static inline struct ib_uobject *__uobj_alloc(const struct uverbs_obj_type *type,
					      struct ib_uverbs_file *ufile)
{
	return rdma_alloc_begin_uobject(type, ufile);
}

#define uobj_alloc(_type, _ufile) __uobj_alloc(uobj_get_type(_type), _ufile)

#endif

