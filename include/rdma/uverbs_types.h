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

#ifndef _UVERBS_TYPES_
#define _UVERBS_TYPES_

#include <linux/kernel.h>
#include <rdma/ib_verbs.h>

struct uverbs_obj_type;
struct uverbs_api_object;

enum rdma_lookup_mode {
	UVERBS_LOOKUP_READ,
	UVERBS_LOOKUP_WRITE,
	/*
	 * Destroy is like LOOKUP_WRITE, except that the uobject is not
	 * locked.  uobj_destroy is used to convert a LOOKUP_DESTROY lock into
	 * a LOOKUP_WRITE lock.
	 */
	UVERBS_LOOKUP_DESTROY,
};

/*
 * The following sequences are valid:
 * Success flow:
 *   alloc_begin
 *   alloc_commit
 *    [..]
 * Access flow:
 *   lookup_get(exclusive=false) & uverbs_try_lock_object
 *   lookup_put(exclusive=false) via rdma_lookup_put_uobject
 * Destruction flow:
 *   lookup_get(exclusive=true) & uverbs_try_lock_object
 *   remove_commit
 *   remove_handle (optional)
 *   lookup_put(exclusive=true) via rdma_lookup_put_uobject
 *
 * Allocate Error flow #1
 *   alloc_begin
 *   alloc_abort
 * Allocate Error flow #2
 *   alloc_begin
 *   remove_commit
 *   alloc_abort
 * Allocate Error flow #3
 *   alloc_begin
 *   alloc_commit (fails)
 *   remove_commit
 *   alloc_abort
 *
 * In all cases the caller must hold the ufile kref until alloc_commit or
 * alloc_abort returns.
 */
struct uverbs_obj_type_class {
	struct ib_uobject *(*alloc_begin)(const struct uverbs_api_object *obj,
					  struct ib_uverbs_file *ufile);
	/* This consumes the kref on uobj */
	int (*alloc_commit)(struct ib_uobject *uobj);
	/* This does not consume the kref on uobj */
	void (*alloc_abort)(struct ib_uobject *uobj);

	struct ib_uobject *(*lookup_get)(const struct uverbs_api_object *obj,
					 struct ib_uverbs_file *ufile, s64 id,
					 enum rdma_lookup_mode mode);
	void (*lookup_put)(struct ib_uobject *uobj, enum rdma_lookup_mode mode);
	/* This does not consume the kref on uobj */
	int __must_check (*destroy_hw)(struct ib_uobject *uobj,
				       enum rdma_remove_reason why,
				       struct uverbs_attr_bundle *attrs);
	void (*remove_handle)(struct ib_uobject *uobj);
};

struct uverbs_obj_type {
	const struct uverbs_obj_type_class * const type_class;
	size_t	     obj_size;
};

/*
 * Objects type classes which support a detach state (object is still alive but
 * it's not attached to any context need to make sure:
 * (a) no call through to a driver after a detach is called
 * (b) detach isn't called concurrently with context_cleanup
 */

struct uverbs_obj_idr_type {
	/*
	 * In idr based objects, uverbs_obj_type_class points to a generic
	 * idr operations. In order to specialize the underlying types (e.g. CQ,
	 * QPs, etc.), we add destroy_object specific callbacks.
	 */
	struct uverbs_obj_type  type;

	/* Free driver resources from the uobject, make the driver uncallable,
	 * and move the uobject to the detached state. If the object was
	 * destroyed by the user's request, a failure should leave the uobject
	 * completely unchanged.
	 */
	int __must_check (*destroy_object)(struct ib_uobject *uobj,
					   enum rdma_remove_reason why,
					   struct uverbs_attr_bundle *attrs);
};

struct ib_uobject *rdma_lookup_get_uobject(const struct uverbs_api_object *obj,
					   struct ib_uverbs_file *ufile, s64 id,
					   enum rdma_lookup_mode mode,
					   struct uverbs_attr_bundle *attrs);
void rdma_lookup_put_uobject(struct ib_uobject *uobj,
			     enum rdma_lookup_mode mode);
struct ib_uobject *rdma_alloc_begin_uobject(const struct uverbs_api_object *obj,
					    struct ib_uverbs_file *ufile,
					    struct uverbs_attr_bundle *attrs);
void rdma_alloc_abort_uobject(struct ib_uobject *uobj,
			      struct uverbs_attr_bundle *attrs);
int __must_check rdma_alloc_commit_uobject(struct ib_uobject *uobj,
					   struct uverbs_attr_bundle *attrs);

struct uverbs_obj_fd_type {
	/*
	 * In fd based objects, uverbs_obj_type_ops points to generic
	 * fd operations. In order to specialize the underlying types (e.g.
	 * completion_channel), we use fops, name and flags for fd creation.
	 * context_closed is called when the context is closed either when
	 * the driver is removed or the process terminated.
	 */
	struct uverbs_obj_type  type;
	int (*context_closed)(struct ib_uobject *uobj,
			      enum rdma_remove_reason why);
	const struct file_operations	*fops;
	const char			*name;
	int				flags;
};

extern const struct uverbs_obj_type_class uverbs_idr_class;
extern const struct uverbs_obj_type_class uverbs_fd_class;
void uverbs_close_fd(struct file *f);

#define UVERBS_BUILD_BUG_ON(cond) (sizeof(char[1 - 2 * !!(cond)]) -	\
				   sizeof(char))
#define UVERBS_TYPE_ALLOC_FD(_obj_size, _context_closed, _fops, _name, _flags)\
	((&((const struct uverbs_obj_fd_type)				\
	 {.type = {							\
		.type_class = &uverbs_fd_class,				\
		.obj_size = (_obj_size) +				\
			UVERBS_BUILD_BUG_ON((_obj_size) <               \
					    sizeof(struct ib_uobject)), \
	 },								\
	 .context_closed = _context_closed,				\
	 .fops = _fops,							\
	 .name = _name,							\
	 .flags = _flags}))->type)
#define UVERBS_TYPE_ALLOC_IDR_SZ(_size, _destroy_object)	\
	((&((const struct uverbs_obj_idr_type)				\
	 {.type = {							\
		.type_class = &uverbs_idr_class,			\
		.obj_size = (_size) +					\
			UVERBS_BUILD_BUG_ON((_size) <			\
					    sizeof(struct ib_uobject))	\
	 },								\
	 .destroy_object = _destroy_object,}))->type)
#define UVERBS_TYPE_ALLOC_IDR(_destroy_object)			\
	 UVERBS_TYPE_ALLOC_IDR_SZ(sizeof(struct ib_uobject),	\
				  _destroy_object)

#endif
