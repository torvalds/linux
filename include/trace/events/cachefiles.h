/* SPDX-License-Identifier: GPL-2.0-or-later */
/* CacheFiles tracepoints
 *
 * Copyright (C) 2016 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 */
#undef TRACE_SYSTEM
#define TRACE_SYSTEM cachefiles

#if !defined(_TRACE_CACHEFILES_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_CACHEFILES_H

#include <linux/tracepoint.h>

/*
 * Define enums for tracing information.
 */
#ifndef __CACHEFILES_DECLARE_TRACE_ENUMS_ONCE_ONLY
#define __CACHEFILES_DECLARE_TRACE_ENUMS_ONCE_ONLY

enum cachefiles_obj_ref_trace {
	cachefiles_obj_put_wait_retry = fscache_obj_ref__nr_traces,
	cachefiles_obj_put_wait_timeo,
	cachefiles_obj_ref__nr_traces
};

#endif

/*
 * Define enum -> string mappings for display.
 */
#define cachefiles_obj_kill_traces				\
	EM(FSCACHE_OBJECT_IS_STALE,	"stale")		\
	EM(FSCACHE_OBJECT_NO_SPACE,	"no_space")		\
	EM(FSCACHE_OBJECT_WAS_RETIRED,	"was_retired")		\
	E_(FSCACHE_OBJECT_WAS_CULLED,	"was_culled")

#define cachefiles_obj_ref_traces					\
	EM(fscache_obj_get_add_to_deps,		"GET add_to_deps")	\
	EM(fscache_obj_get_queue,		"GET queue")		\
	EM(fscache_obj_put_alloc_fail,		"PUT alloc_fail")	\
	EM(fscache_obj_put_attach_fail,		"PUT attach_fail")	\
	EM(fscache_obj_put_drop_obj,		"PUT drop_obj")		\
	EM(fscache_obj_put_enq_dep,		"PUT enq_dep")		\
	EM(fscache_obj_put_queue,		"PUT queue")		\
	EM(fscache_obj_put_work,		"PUT work")		\
	EM(cachefiles_obj_put_wait_retry,	"PUT wait_retry")	\
	E_(cachefiles_obj_put_wait_timeo,	"PUT wait_timeo")

/*
 * Export enum symbols via userspace.
 */
#undef EM
#undef E_
#define EM(a, b) TRACE_DEFINE_ENUM(a);
#define E_(a, b) TRACE_DEFINE_ENUM(a);

cachefiles_obj_kill_traces;
cachefiles_obj_ref_traces;

/*
 * Now redefine the EM() and E_() macros to map the enums to the strings that
 * will be printed in the output.
 */
#undef EM
#undef E_
#define EM(a, b)	{ a, b },
#define E_(a, b)	{ a, b }


TRACE_EVENT(cachefiles_ref,
	    TP_PROTO(struct cachefiles_object *obj,
		     struct fscache_cookie *cookie,
		     enum cachefiles_obj_ref_trace why,
		     int usage),

	    TP_ARGS(obj, cookie, why, usage),

	    /* Note that obj may be NULL */
	    TP_STRUCT__entry(
		    __field(unsigned int,			obj		)
		    __field(unsigned int,			cookie		)
		    __field(enum cachefiles_obj_ref_trace,	why		)
		    __field(int,				usage		)
			     ),

	    TP_fast_assign(
		    __entry->obj	= obj->fscache.debug_id;
		    __entry->cookie	= cookie->debug_id;
		    __entry->usage	= usage;
		    __entry->why	= why;
			   ),

	    TP_printk("c=%08x o=%08x u=%d %s",
		      __entry->cookie, __entry->obj, __entry->usage,
		      __print_symbolic(__entry->why, cachefiles_obj_ref_traces))
	    );

TRACE_EVENT(cachefiles_lookup,
	    TP_PROTO(struct cachefiles_object *obj,
		     struct dentry *de,
		     struct inode *inode),

	    TP_ARGS(obj, de, inode),

	    TP_STRUCT__entry(
		    __field(unsigned int,		obj	)
		    __field(struct dentry *,		de	)
		    __field(struct inode *,		inode	)
			     ),

	    TP_fast_assign(
		    __entry->obj	= obj->fscache.debug_id;
		    __entry->de		= de;
		    __entry->inode	= inode;
			   ),

	    TP_printk("o=%08x d=%p i=%p",
		      __entry->obj, __entry->de, __entry->inode)
	    );

TRACE_EVENT(cachefiles_mkdir,
	    TP_PROTO(struct cachefiles_object *obj,
		     struct dentry *de, int ret),

	    TP_ARGS(obj, de, ret),

	    TP_STRUCT__entry(
		    __field(unsigned int,		obj	)
		    __field(struct dentry *,		de	)
		    __field(int,			ret	)
			     ),

	    TP_fast_assign(
		    __entry->obj	= obj->fscache.debug_id;
		    __entry->de		= de;
		    __entry->ret	= ret;
			   ),

	    TP_printk("o=%08x d=%p r=%u",
		      __entry->obj, __entry->de, __entry->ret)
	    );

TRACE_EVENT(cachefiles_create,
	    TP_PROTO(struct cachefiles_object *obj,
		     struct dentry *de, int ret),

	    TP_ARGS(obj, de, ret),

	    TP_STRUCT__entry(
		    __field(unsigned int,		obj	)
		    __field(struct dentry *,		de	)
		    __field(int,			ret	)
			     ),

	    TP_fast_assign(
		    __entry->obj	= obj->fscache.debug_id;
		    __entry->de		= de;
		    __entry->ret	= ret;
			   ),

	    TP_printk("o=%08x d=%p r=%u",
		      __entry->obj, __entry->de, __entry->ret)
	    );

TRACE_EVENT(cachefiles_unlink,
	    TP_PROTO(struct cachefiles_object *obj,
		     struct dentry *de,
		     enum fscache_why_object_killed why),

	    TP_ARGS(obj, de, why),

	    /* Note that obj may be NULL */
	    TP_STRUCT__entry(
		    __field(unsigned int,		obj		)
		    __field(struct dentry *,		de		)
		    __field(enum fscache_why_object_killed, why		)
			     ),

	    TP_fast_assign(
		    __entry->obj	= obj->fscache.debug_id;
		    __entry->de		= de;
		    __entry->why	= why;
			   ),

	    TP_printk("o=%08x d=%p w=%s",
		      __entry->obj, __entry->de,
		      __print_symbolic(__entry->why, cachefiles_obj_kill_traces))
	    );

TRACE_EVENT(cachefiles_rename,
	    TP_PROTO(struct cachefiles_object *obj,
		     struct dentry *de,
		     struct dentry *to,
		     enum fscache_why_object_killed why),

	    TP_ARGS(obj, de, to, why),

	    /* Note that obj may be NULL */
	    TP_STRUCT__entry(
		    __field(unsigned int,		obj		)
		    __field(struct dentry *,		de		)
		    __field(struct dentry *,		to		)
		    __field(enum fscache_why_object_killed, why		)
			     ),

	    TP_fast_assign(
		    __entry->obj	= obj->fscache.debug_id;
		    __entry->de		= de;
		    __entry->to		= to;
		    __entry->why	= why;
			   ),

	    TP_printk("o=%08x d=%p t=%p w=%s",
		      __entry->obj, __entry->de, __entry->to,
		      __print_symbolic(__entry->why, cachefiles_obj_kill_traces))
	    );

TRACE_EVENT(cachefiles_mark_active,
	    TP_PROTO(struct cachefiles_object *obj,
		     struct dentry *de),

	    TP_ARGS(obj, de),

	    /* Note that obj may be NULL */
	    TP_STRUCT__entry(
		    __field(unsigned int,		obj		)
		    __field(struct dentry *,		de		)
			     ),

	    TP_fast_assign(
		    __entry->obj	= obj->fscache.debug_id;
		    __entry->de		= de;
			   ),

	    TP_printk("o=%08x d=%p",
		      __entry->obj, __entry->de)
	    );

TRACE_EVENT(cachefiles_wait_active,
	    TP_PROTO(struct cachefiles_object *obj,
		     struct dentry *de,
		     struct cachefiles_object *xobj),

	    TP_ARGS(obj, de, xobj),

	    /* Note that obj may be NULL */
	    TP_STRUCT__entry(
		    __field(unsigned int,		obj		)
		    __field(unsigned int,		xobj		)
		    __field(struct dentry *,		de		)
		    __field(u16,			flags		)
		    __field(u16,			fsc_flags	)
			     ),

	    TP_fast_assign(
		    __entry->obj	= obj->fscache.debug_id;
		    __entry->de		= de;
		    __entry->xobj	= xobj->fscache.debug_id;
		    __entry->flags	= xobj->flags;
		    __entry->fsc_flags	= xobj->fscache.flags;
			   ),

	    TP_printk("o=%08x d=%p wo=%08x wf=%x wff=%x",
		      __entry->obj, __entry->de, __entry->xobj,
		      __entry->flags, __entry->fsc_flags)
	    );

TRACE_EVENT(cachefiles_mark_inactive,
	    TP_PROTO(struct cachefiles_object *obj,
		     struct dentry *de,
		     struct inode *inode),

	    TP_ARGS(obj, de, inode),

	    /* Note that obj may be NULL */
	    TP_STRUCT__entry(
		    __field(unsigned int,		obj		)
		    __field(struct dentry *,		de		)
		    __field(struct inode *,		inode		)
			     ),

	    TP_fast_assign(
		    __entry->obj	= obj->fscache.debug_id;
		    __entry->de		= de;
		    __entry->inode	= inode;
			   ),

	    TP_printk("o=%08x d=%p i=%p",
		      __entry->obj, __entry->de, __entry->inode)
	    );

TRACE_EVENT(cachefiles_mark_buried,
	    TP_PROTO(struct cachefiles_object *obj,
		     struct dentry *de,
		     enum fscache_why_object_killed why),

	    TP_ARGS(obj, de, why),

	    /* Note that obj may be NULL */
	    TP_STRUCT__entry(
		    __field(unsigned int,		obj		)
		    __field(struct dentry *,		de		)
		    __field(enum fscache_why_object_killed, why		)
			     ),

	    TP_fast_assign(
		    __entry->obj	= obj->fscache.debug_id;
		    __entry->de		= de;
		    __entry->why	= why;
			   ),

	    TP_printk("o=%08x d=%p w=%s",
		      __entry->obj, __entry->de,
		      __print_symbolic(__entry->why, cachefiles_obj_kill_traces))
	    );

#endif /* _TRACE_CACHEFILES_H */

/* This part must be outside protection */
#include <trace/define_trace.h>
