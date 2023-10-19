/* SPDX-License-Identifier: BSD-3-Clause OR GPL-2.0 */
/* Copyright (c) 2018 Mellanox Technologies. All rights reserved */

#undef TRACE_SYSTEM
#define TRACE_SYSTEM objagg

#if !defined(__TRACE_OBJAGG_H) || defined(TRACE_HEADER_MULTI_READ)
#define __TRACE_OBJAGG_H

#include <linux/tracepoint.h>

struct objagg;
struct objagg_obj;

TRACE_EVENT(objagg_create,
	TP_PROTO(const struct objagg *objagg),

	TP_ARGS(objagg),

	TP_STRUCT__entry(
		__field(const void *, objagg)
	),

	TP_fast_assign(
		__entry->objagg = objagg;
	),

	TP_printk("objagg %p", __entry->objagg)
);

TRACE_EVENT(objagg_destroy,
	TP_PROTO(const struct objagg *objagg),

	TP_ARGS(objagg),

	TP_STRUCT__entry(
		__field(const void *, objagg)
	),

	TP_fast_assign(
		__entry->objagg = objagg;
	),

	TP_printk("objagg %p", __entry->objagg)
);

TRACE_EVENT(objagg_obj_create,
	TP_PROTO(const struct objagg *objagg,
		 const struct objagg_obj *obj),

	TP_ARGS(objagg, obj),

	TP_STRUCT__entry(
		__field(const void *, objagg)
		__field(const void *, obj)
	),

	TP_fast_assign(
		__entry->objagg = objagg;
		__entry->obj = obj;
	),

	TP_printk("objagg %p, obj %p", __entry->objagg, __entry->obj)
);

TRACE_EVENT(objagg_obj_destroy,
	TP_PROTO(const struct objagg *objagg,
		 const struct objagg_obj *obj),

	TP_ARGS(objagg, obj),

	TP_STRUCT__entry(
		__field(const void *, objagg)
		__field(const void *, obj)
	),

	TP_fast_assign(
		__entry->objagg = objagg;
		__entry->obj = obj;
	),

	TP_printk("objagg %p, obj %p", __entry->objagg, __entry->obj)
);

TRACE_EVENT(objagg_obj_get,
	TP_PROTO(const struct objagg *objagg,
		 const struct objagg_obj *obj,
		 unsigned int refcount),

	TP_ARGS(objagg, obj, refcount),

	TP_STRUCT__entry(
		__field(const void *, objagg)
		__field(const void *, obj)
		__field(unsigned int, refcount)
	),

	TP_fast_assign(
		__entry->objagg = objagg;
		__entry->obj = obj;
		__entry->refcount = refcount;
	),

	TP_printk("objagg %p, obj %p, refcount %u",
		  __entry->objagg, __entry->obj, __entry->refcount)
);

TRACE_EVENT(objagg_obj_put,
	TP_PROTO(const struct objagg *objagg,
		 const struct objagg_obj *obj,
		 unsigned int refcount),

	TP_ARGS(objagg, obj, refcount),

	TP_STRUCT__entry(
		__field(const void *, objagg)
		__field(const void *, obj)
		__field(unsigned int, refcount)
	),

	TP_fast_assign(
		__entry->objagg = objagg;
		__entry->obj = obj;
		__entry->refcount = refcount;
	),

	TP_printk("objagg %p, obj %p, refcount %u",
		  __entry->objagg, __entry->obj, __entry->refcount)
);

TRACE_EVENT(objagg_obj_parent_assign,
	TP_PROTO(const struct objagg *objagg,
		 const struct objagg_obj *obj,
		 const struct objagg_obj *parent,
		 unsigned int parent_refcount),

	TP_ARGS(objagg, obj, parent, parent_refcount),

	TP_STRUCT__entry(
		__field(const void *, objagg)
		__field(const void *, obj)
		__field(const void *, parent)
		__field(unsigned int, parent_refcount)
	),

	TP_fast_assign(
		__entry->objagg = objagg;
		__entry->obj = obj;
		__entry->parent = parent;
		__entry->parent_refcount = parent_refcount;
	),

	TP_printk("objagg %p, obj %p, parent %p, parent_refcount %u",
		  __entry->objagg, __entry->obj,
		  __entry->parent, __entry->parent_refcount)
);

TRACE_EVENT(objagg_obj_parent_unassign,
	TP_PROTO(const struct objagg *objagg,
		 const struct objagg_obj *obj,
		 const struct objagg_obj *parent,
		 unsigned int parent_refcount),

	TP_ARGS(objagg, obj, parent, parent_refcount),

	TP_STRUCT__entry(
		__field(const void *, objagg)
		__field(const void *, obj)
		__field(const void *, parent)
		__field(unsigned int, parent_refcount)
	),

	TP_fast_assign(
		__entry->objagg = objagg;
		__entry->obj = obj;
		__entry->parent = parent;
		__entry->parent_refcount = parent_refcount;
	),

	TP_printk("objagg %p, obj %p, parent %p, parent_refcount %u",
		  __entry->objagg, __entry->obj,
		  __entry->parent, __entry->parent_refcount)
);

TRACE_EVENT(objagg_obj_root_create,
	TP_PROTO(const struct objagg *objagg,
		 const struct objagg_obj *obj),

	TP_ARGS(objagg, obj),

	TP_STRUCT__entry(
		__field(const void *, objagg)
		__field(const void *, obj)
	),

	TP_fast_assign(
		__entry->objagg = objagg;
		__entry->obj = obj;
	),

	TP_printk("objagg %p, obj %p",
		  __entry->objagg, __entry->obj)
);

TRACE_EVENT(objagg_obj_root_destroy,
	TP_PROTO(const struct objagg *objagg,
		 const struct objagg_obj *obj),

	TP_ARGS(objagg, obj),

	TP_STRUCT__entry(
		__field(const void *, objagg)
		__field(const void *, obj)
	),

	TP_fast_assign(
		__entry->objagg = objagg;
		__entry->obj = obj;
	),

	TP_printk("objagg %p, obj %p",
		  __entry->objagg, __entry->obj)
);

#endif /* __TRACE_OBJAGG_H */

/* This part must be outside protection */
#include <trace/define_trace.h>
