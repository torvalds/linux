/* SPDX-License-Identifier: GPL-2.0-or-later */
/* FS-Cache tracepoints
 *
 * Copyright (C) 2021 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 */
#undef TRACE_SYSTEM
#define TRACE_SYSTEM fscache

#if !defined(_TRACE_FSCACHE_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_FSCACHE_H

#include <linux/fscache.h>
#include <linux/tracepoint.h>

/*
 * Define enums for tracing information.
 */
#ifndef __FSCACHE_DECLARE_TRACE_ENUMS_ONCE_ONLY
#define __FSCACHE_DECLARE_TRACE_ENUMS_ONCE_ONLY

enum fscache_cache_trace {
	fscache_cache_collision,
	fscache_cache_get_acquire,
	fscache_cache_new_acquire,
	fscache_cache_put_alloc_volume,
	fscache_cache_put_cache,
	fscache_cache_put_prep_failed,
	fscache_cache_put_relinquish,
	fscache_cache_put_volume,
};

enum fscache_volume_trace {
	fscache_volume_collision,
	fscache_volume_get_cookie,
	fscache_volume_get_create_work,
	fscache_volume_get_hash_collision,
	fscache_volume_free,
	fscache_volume_new_acquire,
	fscache_volume_put_cookie,
	fscache_volume_put_create_work,
	fscache_volume_put_hash_collision,
	fscache_volume_put_relinquish,
	fscache_volume_see_create_work,
	fscache_volume_see_hash_wake,
};

#endif

/*
 * Declare tracing information enums and their string mappings for display.
 */
#define fscache_cache_traces						\
	EM(fscache_cache_collision,		"*COLLIDE*")		\
	EM(fscache_cache_get_acquire,		"GET acq  ")		\
	EM(fscache_cache_new_acquire,		"NEW acq  ")		\
	EM(fscache_cache_put_alloc_volume,	"PUT alvol")		\
	EM(fscache_cache_put_cache,		"PUT cache")		\
	EM(fscache_cache_put_prep_failed,	"PUT pfail")		\
	EM(fscache_cache_put_relinquish,	"PUT relnq")		\
	E_(fscache_cache_put_volume,		"PUT vol  ")

#define fscache_volume_traces						\
	EM(fscache_volume_collision,		"*COLLIDE*")		\
	EM(fscache_volume_get_cookie,		"GET cook ")		\
	EM(fscache_volume_get_create_work,	"GET creat")		\
	EM(fscache_volume_get_hash_collision,	"GET hcoll")		\
	EM(fscache_volume_free,			"FREE     ")		\
	EM(fscache_volume_new_acquire,		"NEW acq  ")		\
	EM(fscache_volume_put_cookie,		"PUT cook ")		\
	EM(fscache_volume_put_create_work,	"PUT creat")		\
	EM(fscache_volume_put_hash_collision,	"PUT hcoll")		\
	EM(fscache_volume_put_relinquish,	"PUT relnq")		\
	EM(fscache_volume_see_create_work,	"SEE creat")		\
	E_(fscache_volume_see_hash_wake,	"SEE hwake")

/*
 * Export enum symbols via userspace.
 */
#undef EM
#undef E_
#define EM(a, b) TRACE_DEFINE_ENUM(a);
#define E_(a, b) TRACE_DEFINE_ENUM(a);

fscache_cache_traces;
fscache_volume_traces;

/*
 * Now redefine the EM() and E_() macros to map the enums to the strings that
 * will be printed in the output.
 */
#undef EM
#undef E_
#define EM(a, b)	{ a, b },
#define E_(a, b)	{ a, b }


TRACE_EVENT(fscache_cache,
	    TP_PROTO(unsigned int cache_debug_id,
		     int usage,
		     enum fscache_cache_trace where),

	    TP_ARGS(cache_debug_id, usage, where),

	    TP_STRUCT__entry(
		    __field(unsigned int,		cache		)
		    __field(int,			usage		)
		    __field(enum fscache_cache_trace,	where		)
			     ),

	    TP_fast_assign(
		    __entry->cache	= cache_debug_id;
		    __entry->usage	= usage;
		    __entry->where	= where;
			   ),

	    TP_printk("C=%08x %s r=%d",
		      __entry->cache,
		      __print_symbolic(__entry->where, fscache_cache_traces),
		      __entry->usage)
	    );

TRACE_EVENT(fscache_volume,
	    TP_PROTO(unsigned int volume_debug_id,
		     int usage,
		     enum fscache_volume_trace where),

	    TP_ARGS(volume_debug_id, usage, where),

	    TP_STRUCT__entry(
		    __field(unsigned int,		volume		)
		    __field(int,			usage		)
		    __field(enum fscache_volume_trace,	where		)
			     ),

	    TP_fast_assign(
		    __entry->volume	= volume_debug_id;
		    __entry->usage	= usage;
		    __entry->where	= where;
			   ),

	    TP_printk("V=%08x %s u=%d",
		      __entry->volume,
		      __print_symbolic(__entry->where, fscache_volume_traces),
		      __entry->usage)
	    );

#endif /* _TRACE_FSCACHE_H */

/* This part must be outside protection */
#include <trace/define_trace.h>
