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

struct uverbs_obj_type_class {
	/*
	 * Get an ib_uobject that corresponds to the given id from ucontext,
	 * These functions could create or destroy objects if required.
	 * The action will be finalized only when commit, abort or put fops are
	 * called.
	 * The flow of the different actions is:
	 * [alloc]:	 Starts with alloc_begin. The handlers logic is than
	 *		 executed. If the handler is successful, alloc_commit
	 *		 is called and the object is inserted to the repository.
	 *		 Once alloc_commit completes the object is visible to
	 *		 other threads and userspace.
	 e		 Otherwise, alloc_abort is called and the object is
	 *		 destroyed.
	 * [lookup]:	 Starts with lookup_get which fetches and locks the
	 *		 object. After the handler finished using the object, it
	 *		 needs to call lookup_put to unlock it. The exclusive
	 *		 flag indicates if the object is locked for exclusive
	 *		 access.
	 * [remove]:	 Starts with lookup_get with exclusive flag set. This
	 *		 locks the object for exclusive access. If the handler
	 *		 code completed successfully, remove_commit is called
	 *		 and the ib_uobject is removed from the context's
	 *		 uobjects repository and put. The object itself is
	 *		 destroyed as well. Once remove succeeds new krefs to
	 *		 the object cannot be acquired by other threads or
	 *		 userspace and the hardware driver is removed from the
	 *		 object. Other krefs on the object may still exist.
	 *		 If the handler code failed, lookup_put should be
	 *		 called. This callback is used when the context
	 *		 is destroyed as well (process termination,
	 *		 reset flow).
	 */
	struct ib_uobject *(*alloc_begin)(const struct uverbs_obj_type *type,
					  struct ib_ucontext *ucontext);
	void (*alloc_commit)(struct ib_uobject *uobj);
	void (*alloc_abort)(struct ib_uobject *uobj);

	struct ib_uobject *(*lookup_get)(const struct uverbs_obj_type *type,
					 struct ib_ucontext *ucontext, int id,
					 bool exclusive);
	void (*lookup_put)(struct ib_uobject *uobj, bool exclusive);
	/*
	 * Must be called with the exclusive lock held. If successful uobj is
	 * invalid on return. On failure uobject is left completely
	 * unchanged
	 */
	int __must_check (*remove_commit)(struct ib_uobject *uobj,
					  enum rdma_remove_reason why);
	u8    needs_kfree_rcu;
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
					   enum rdma_remove_reason why);
};

struct ib_uobject *rdma_lookup_get_uobject(const struct uverbs_obj_type *type,
					   struct ib_ucontext *ucontext,
					   int id, bool exclusive);
void rdma_lookup_put_uobject(struct ib_uobject *uobj, bool exclusive);
struct ib_uobject *rdma_alloc_begin_uobject(const struct uverbs_obj_type *type,
					    struct ib_ucontext *ucontext);
void rdma_alloc_abort_uobject(struct ib_uobject *uobj);
int __must_check rdma_remove_commit_uobject(struct ib_uobject *uobj);
int rdma_alloc_commit_uobject(struct ib_uobject *uobj);
int rdma_explicit_destroy(struct ib_uobject *uobject);

struct uverbs_obj_fd_type {
	/*
	 * In fd based objects, uverbs_obj_type_ops points to generic
	 * fd operations. In order to specialize the underlying types (e.g.
	 * completion_channel), we use fops, name and flags for fd creation.
	 * context_closed is called when the context is closed either when
	 * the driver is removed or the process terminated.
	 */
	struct uverbs_obj_type  type;
	int (*context_closed)(struct ib_uobject_file *uobj_file,
			      enum rdma_remove_reason why);
	const struct file_operations	*fops;
	const char			*name;
	int				flags;
};

extern const struct uverbs_obj_type_class uverbs_idr_class;
extern const struct uverbs_obj_type_class uverbs_fd_class;

#define UVERBS_BUILD_BUG_ON(cond) (sizeof(char[1 - 2 * !!(cond)]) -	\
				   sizeof(char))
#define UVERBS_TYPE_ALLOC_FD(_obj_size, _context_closed, _fops, _name, _flags)\
	((&((const struct uverbs_obj_fd_type)				\
	 {.type = {							\
		.type_class = &uverbs_fd_class,				\
		.obj_size = (_obj_size) +				\
			UVERBS_BUILD_BUG_ON((_obj_size) < sizeof(struct ib_uobject_file)), \
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
