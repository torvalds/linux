/* SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB */
/*
 * Copyright (c) 2017, Mellanox Technologies inc.  All rights reserved.
 */

#ifndef _UVERBS_STD_TYPES__
#define _UVERBS_STD_TYPES__

#include <rdma/uverbs_types.h>
#include <rdma/uverbs_ioctl.h>
#include <rdma/ib_user_ioctl_verbs.h>

/* Returns _id, or causes a compile error if _id is not a u32.
 *
 * The uobj APIs should only be used with the write based uAPI to access
 * object IDs. The write API must use a u32 for the object handle, which is
 * checked by this macro.
 */
#define _uobj_check_id(_id) ((_id) * typecheck(u32, _id))

#define uobj_get_type(_attrs, _object)                                         \
	uapi_get_object((_attrs)->ufile->device->uapi, _object)

#define uobj_get_read(_type, _id, _attrs)                                      \
	rdma_lookup_get_uobject(uobj_get_type(_attrs, _type), (_attrs)->ufile, \
				_uobj_check_id(_id), UVERBS_LOOKUP_READ,       \
				_attrs)

#define ufd_get_read(_type, _fdnum, _attrs)                                    \
	rdma_lookup_get_uobject(uobj_get_type(_attrs, _type), (_attrs)->ufile, \
				(_fdnum)*typecheck(s32, _fdnum),               \
				UVERBS_LOOKUP_READ, _attrs)

static inline void *_uobj_get_obj_read(struct ib_uobject *uobj)
{
	if (IS_ERR(uobj))
		return NULL;
	return uobj->object;
}
#define uobj_get_obj_read(_object, _type, _id, _attrs)                         \
	((struct ib_##_object *)_uobj_get_obj_read(                            \
		uobj_get_read(_type, _id, _attrs)))

#define uobj_get_write(_type, _id, _attrs)                                     \
	rdma_lookup_get_uobject(uobj_get_type(_attrs, _type), (_attrs)->ufile, \
				_uobj_check_id(_id), UVERBS_LOOKUP_WRITE,      \
				_attrs)

int __uobj_perform_destroy(const struct uverbs_api_object *obj, u32 id,
			   struct uverbs_attr_bundle *attrs);
#define uobj_perform_destroy(_type, _id, _attrs)                               \
	__uobj_perform_destroy(uobj_get_type(_attrs, _type),                   \
			       _uobj_check_id(_id), _attrs)

struct ib_uobject *__uobj_get_destroy(const struct uverbs_api_object *obj,
				      u32 id, struct uverbs_attr_bundle *attrs);

#define uobj_get_destroy(_type, _id, _attrs)                                   \
	__uobj_get_destroy(uobj_get_type(_attrs, _type), _uobj_check_id(_id),  \
			   _attrs)

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

static inline void uobj_alloc_abort(struct ib_uobject *uobj,
				    struct uverbs_attr_bundle *attrs)
{
	rdma_alloc_abort_uobject(uobj, attrs, false);
}

static inline void uobj_finalize_uobj_create(struct ib_uobject *uobj,
					     struct uverbs_attr_bundle *attrs)
{
	/*
	 * Tell the core code that the write() handler has completed
	 * initializing the object and that the core should commit or
	 * abort this object based upon the return code from the write()
	 * method. Similar to what uverbs_finalize_uobj_create() does for
	 * ioctl()
	 */
	WARN_ON(attrs->uobject);
	attrs->uobject = uobj;
}

static inline struct ib_uobject *
__uobj_alloc(const struct uverbs_api_object *obj,
	     struct uverbs_attr_bundle *attrs, struct ib_device **ib_dev)
{
	struct ib_uobject *uobj = rdma_alloc_begin_uobject(obj, attrs);

	if (!IS_ERR(uobj))
		*ib_dev = attrs->context->device;
	return uobj;
}

#define uobj_alloc(_type, _attrs, _ib_dev)                                     \
	__uobj_alloc(uobj_get_type(_attrs, _type), _attrs, _ib_dev)

static inline void uverbs_flow_action_fill_action(struct ib_flow_action *action,
						  struct ib_uobject *uobj,
						  struct ib_device *ib_dev,
						  enum ib_flow_action_type type)
{
	atomic_set(&action->usecnt, 0);
	action->device = ib_dev;
	action->type = type;
	action->uobject = uobj;
	uobj->object = action;
}

struct ib_uflow_resources {
	size_t			max;
	size_t			num;
	size_t			collection_num;
	size_t			counters_num;
	struct ib_counters	**counters;
	struct ib_flow_action	**collection;
};

struct ib_uflow_object {
	struct ib_uobject		uobject;
	struct ib_uflow_resources	*resources;
};

struct ib_uflow_resources *flow_resources_alloc(size_t num_specs);
void flow_resources_add(struct ib_uflow_resources *uflow_res,
			enum ib_flow_spec_type type,
			void *ibobj);
void ib_uverbs_flow_resources_free(struct ib_uflow_resources *uflow_res);

static inline void ib_set_flow(struct ib_uobject *uobj, struct ib_flow *ibflow,
			       struct ib_qp *qp, struct ib_device *device,
			       struct ib_uflow_resources *uflow_res)
{
	struct ib_uflow_object *uflow;

	uobj->object = ibflow;
	ibflow->uobject = uobj;

	if (qp) {
		atomic_inc(&qp->usecnt);
		ibflow->qp = qp;
	}

	ibflow->device = device;
	uflow = container_of(uobj, typeof(*uflow), uobject);
	uflow->resources = uflow_res;
}

struct uverbs_api_object {
	const struct uverbs_obj_type *type_attrs;
	const struct uverbs_obj_type_class *type_class;
	u8 disabled:1;
	u32 id;
};

static inline u32 uobj_get_object_id(struct ib_uobject *uobj)
{
	return uobj->uapi_object->id;
}

#endif

