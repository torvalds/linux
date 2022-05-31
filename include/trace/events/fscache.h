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
	fscache_volume_wait_create_work,
};

enum fscache_cookie_trace {
	fscache_cookie_collision,
	fscache_cookie_discard,
	fscache_cookie_get_attach_object,
	fscache_cookie_get_end_access,
	fscache_cookie_get_hash_collision,
	fscache_cookie_get_inval_work,
	fscache_cookie_get_lru,
	fscache_cookie_get_use_work,
	fscache_cookie_new_acquire,
	fscache_cookie_put_hash_collision,
	fscache_cookie_put_lru,
	fscache_cookie_put_object,
	fscache_cookie_put_over_queued,
	fscache_cookie_put_relinquish,
	fscache_cookie_put_withdrawn,
	fscache_cookie_put_work,
	fscache_cookie_see_active,
	fscache_cookie_see_lru_discard,
	fscache_cookie_see_lru_do_one,
	fscache_cookie_see_relinquish,
	fscache_cookie_see_withdraw,
	fscache_cookie_see_work,
};

enum fscache_active_trace {
	fscache_active_use,
	fscache_active_use_modify,
	fscache_active_unuse,
};

enum fscache_access_trace {
	fscache_access_acquire_volume,
	fscache_access_acquire_volume_end,
	fscache_access_cache_pin,
	fscache_access_cache_unpin,
	fscache_access_invalidate_cookie,
	fscache_access_invalidate_cookie_end,
	fscache_access_io_end,
	fscache_access_io_not_live,
	fscache_access_io_read,
	fscache_access_io_resize,
	fscache_access_io_wait,
	fscache_access_io_write,
	fscache_access_lookup_cookie,
	fscache_access_lookup_cookie_end,
	fscache_access_lookup_cookie_end_failed,
	fscache_access_relinquish_volume,
	fscache_access_relinquish_volume_end,
	fscache_access_unlive,
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
	EM(fscache_volume_see_hash_wake,	"SEE hwake")		\
	E_(fscache_volume_wait_create_work,	"WAIT crea")

#define fscache_cookie_traces						\
	EM(fscache_cookie_collision,		"*COLLIDE*")		\
	EM(fscache_cookie_discard,		"DISCARD  ")		\
	EM(fscache_cookie_get_attach_object,	"GET attch")		\
	EM(fscache_cookie_get_hash_collision,	"GET hcoll")		\
	EM(fscache_cookie_get_end_access,	"GQ  endac")		\
	EM(fscache_cookie_get_inval_work,	"GQ  inval")		\
	EM(fscache_cookie_get_lru,		"GET lru  ")		\
	EM(fscache_cookie_get_use_work,		"GQ  use  ")		\
	EM(fscache_cookie_new_acquire,		"NEW acq  ")		\
	EM(fscache_cookie_put_hash_collision,	"PUT hcoll")		\
	EM(fscache_cookie_put_lru,		"PUT lru  ")		\
	EM(fscache_cookie_put_object,		"PUT obj  ")		\
	EM(fscache_cookie_put_over_queued,	"PQ  overq")		\
	EM(fscache_cookie_put_relinquish,	"PUT relnq")		\
	EM(fscache_cookie_put_withdrawn,	"PUT wthdn")		\
	EM(fscache_cookie_put_work,		"PQ  work ")		\
	EM(fscache_cookie_see_active,		"-   activ")		\
	EM(fscache_cookie_see_lru_discard,	"-   x-lru")		\
	EM(fscache_cookie_see_lru_do_one,	"-   lrudo")		\
	EM(fscache_cookie_see_relinquish,	"-   x-rlq")		\
	EM(fscache_cookie_see_withdraw,		"-   x-wth")		\
	E_(fscache_cookie_see_work,		"-   work ")

#define fscache_active_traces		\
	EM(fscache_active_use,			"USE          ")	\
	EM(fscache_active_use_modify,		"USE-m        ")	\
	E_(fscache_active_unuse,		"UNUSE        ")

#define fscache_access_traces		\
	EM(fscache_access_acquire_volume,	"BEGIN acq_vol")	\
	EM(fscache_access_acquire_volume_end,	"END   acq_vol")	\
	EM(fscache_access_cache_pin,		"PIN   cache  ")	\
	EM(fscache_access_cache_unpin,		"UNPIN cache  ")	\
	EM(fscache_access_invalidate_cookie,	"BEGIN inval  ")	\
	EM(fscache_access_invalidate_cookie_end,"END   inval  ")	\
	EM(fscache_access_io_end,		"END   io     ")	\
	EM(fscache_access_io_not_live,		"END   io_notl")	\
	EM(fscache_access_io_read,		"BEGIN io_read")	\
	EM(fscache_access_io_resize,		"BEGIN io_resz")	\
	EM(fscache_access_io_wait,		"WAIT  io     ")	\
	EM(fscache_access_io_write,		"BEGIN io_writ")	\
	EM(fscache_access_lookup_cookie,	"BEGIN lookup ")	\
	EM(fscache_access_lookup_cookie_end,	"END   lookup ")	\
	EM(fscache_access_lookup_cookie_end_failed,"END   lookupf")	\
	EM(fscache_access_relinquish_volume,	"BEGIN rlq_vol")	\
	EM(fscache_access_relinquish_volume_end,"END   rlq_vol")	\
	E_(fscache_access_unlive,		"END   unlive ")

/*
 * Export enum symbols via userspace.
 */
#undef EM
#undef E_
#define EM(a, b) TRACE_DEFINE_ENUM(a);
#define E_(a, b) TRACE_DEFINE_ENUM(a);

fscache_cache_traces;
fscache_volume_traces;
fscache_cookie_traces;
fscache_access_traces;

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

TRACE_EVENT(fscache_cookie,
	    TP_PROTO(unsigned int cookie_debug_id,
		     int ref,
		     enum fscache_cookie_trace where),

	    TP_ARGS(cookie_debug_id, ref, where),

	    TP_STRUCT__entry(
		    __field(unsigned int,		cookie		)
		    __field(int,			ref		)
		    __field(enum fscache_cookie_trace,	where		)
			     ),

	    TP_fast_assign(
		    __entry->cookie	= cookie_debug_id;
		    __entry->ref	= ref;
		    __entry->where	= where;
			   ),

	    TP_printk("c=%08x %s r=%d",
		      __entry->cookie,
		      __print_symbolic(__entry->where, fscache_cookie_traces),
		      __entry->ref)
	    );

TRACE_EVENT(fscache_active,
	    TP_PROTO(unsigned int cookie_debug_id,
		     int ref,
		     int n_active,
		     int n_accesses,
		     enum fscache_active_trace why),

	    TP_ARGS(cookie_debug_id, ref, n_active, n_accesses, why),

	    TP_STRUCT__entry(
		    __field(unsigned int,		cookie		)
		    __field(int,			ref		)
		    __field(int,			n_active	)
		    __field(int,			n_accesses	)
		    __field(enum fscache_active_trace,	why		)
			     ),

	    TP_fast_assign(
		    __entry->cookie	= cookie_debug_id;
		    __entry->ref	= ref;
		    __entry->n_active	= n_active;
		    __entry->n_accesses	= n_accesses;
		    __entry->why	= why;
			   ),

	    TP_printk("c=%08x %s r=%d a=%d c=%d",
		      __entry->cookie,
		      __print_symbolic(__entry->why, fscache_active_traces),
		      __entry->ref,
		      __entry->n_accesses,
		      __entry->n_active)
	    );

TRACE_EVENT(fscache_access_cache,
	    TP_PROTO(unsigned int cache_debug_id,
		     int ref,
		     int n_accesses,
		     enum fscache_access_trace why),

	    TP_ARGS(cache_debug_id, ref, n_accesses, why),

	    TP_STRUCT__entry(
		    __field(unsigned int,		cache		)
		    __field(int,			ref		)
		    __field(int,			n_accesses	)
		    __field(enum fscache_access_trace,	why		)
			     ),

	    TP_fast_assign(
		    __entry->cache	= cache_debug_id;
		    __entry->ref	= ref;
		    __entry->n_accesses	= n_accesses;
		    __entry->why	= why;
			   ),

	    TP_printk("C=%08x %s r=%d a=%d",
		      __entry->cache,
		      __print_symbolic(__entry->why, fscache_access_traces),
		      __entry->ref,
		      __entry->n_accesses)
	    );

TRACE_EVENT(fscache_access_volume,
	    TP_PROTO(unsigned int volume_debug_id,
		     unsigned int cookie_debug_id,
		     int ref,
		     int n_accesses,
		     enum fscache_access_trace why),

	    TP_ARGS(volume_debug_id, cookie_debug_id, ref, n_accesses, why),

	    TP_STRUCT__entry(
		    __field(unsigned int,		volume		)
		    __field(unsigned int,		cookie		)
		    __field(int,			ref		)
		    __field(int,			n_accesses	)
		    __field(enum fscache_access_trace,	why		)
			     ),

	    TP_fast_assign(
		    __entry->volume	= volume_debug_id;
		    __entry->cookie	= cookie_debug_id;
		    __entry->ref	= ref;
		    __entry->n_accesses	= n_accesses;
		    __entry->why	= why;
			   ),

	    TP_printk("V=%08x c=%08x %s r=%d a=%d",
		      __entry->volume,
		      __entry->cookie,
		      __print_symbolic(__entry->why, fscache_access_traces),
		      __entry->ref,
		      __entry->n_accesses)
	    );

TRACE_EVENT(fscache_access,
	    TP_PROTO(unsigned int cookie_debug_id,
		     int ref,
		     int n_accesses,
		     enum fscache_access_trace why),

	    TP_ARGS(cookie_debug_id, ref, n_accesses, why),

	    TP_STRUCT__entry(
		    __field(unsigned int,		cookie		)
		    __field(int,			ref		)
		    __field(int,			n_accesses	)
		    __field(enum fscache_access_trace,	why		)
			     ),

	    TP_fast_assign(
		    __entry->cookie	= cookie_debug_id;
		    __entry->ref	= ref;
		    __entry->n_accesses	= n_accesses;
		    __entry->why	= why;
			   ),

	    TP_printk("c=%08x %s r=%d a=%d",
		      __entry->cookie,
		      __print_symbolic(__entry->why, fscache_access_traces),
		      __entry->ref,
		      __entry->n_accesses)
	    );

TRACE_EVENT(fscache_acquire,
	    TP_PROTO(struct fscache_cookie *cookie),

	    TP_ARGS(cookie),

	    TP_STRUCT__entry(
		    __field(unsigned int,		cookie		)
		    __field(unsigned int,		volume		)
		    __field(int,			v_ref		)
		    __field(int,			v_n_cookies	)
			     ),

	    TP_fast_assign(
		    __entry->cookie		= cookie->debug_id;
		    __entry->volume		= cookie->volume->debug_id;
		    __entry->v_ref		= refcount_read(&cookie->volume->ref);
		    __entry->v_n_cookies	= atomic_read(&cookie->volume->n_cookies);
			   ),

	    TP_printk("c=%08x V=%08x vr=%d vc=%d",
		      __entry->cookie,
		      __entry->volume, __entry->v_ref, __entry->v_n_cookies)
	    );

TRACE_EVENT(fscache_relinquish,
	    TP_PROTO(struct fscache_cookie *cookie, bool retire),

	    TP_ARGS(cookie, retire),

	    TP_STRUCT__entry(
		    __field(unsigned int,		cookie		)
		    __field(unsigned int,		volume		)
		    __field(int,			ref		)
		    __field(int,			n_active	)
		    __field(u8,				flags		)
		    __field(bool,			retire		)
			     ),

	    TP_fast_assign(
		    __entry->cookie	= cookie->debug_id;
		    __entry->volume	= cookie->volume->debug_id;
		    __entry->ref	= refcount_read(&cookie->ref);
		    __entry->n_active	= atomic_read(&cookie->n_active);
		    __entry->flags	= cookie->flags;
		    __entry->retire	= retire;
			   ),

	    TP_printk("c=%08x V=%08x r=%d U=%d f=%02x rt=%u",
		      __entry->cookie, __entry->volume, __entry->ref,
		      __entry->n_active, __entry->flags, __entry->retire)
	    );

TRACE_EVENT(fscache_invalidate,
	    TP_PROTO(struct fscache_cookie *cookie, loff_t new_size),

	    TP_ARGS(cookie, new_size),

	    TP_STRUCT__entry(
		    __field(unsigned int,		cookie		)
		    __field(loff_t,			new_size	)
			     ),

	    TP_fast_assign(
		    __entry->cookie	= cookie->debug_id;
		    __entry->new_size	= new_size;
			   ),

	    TP_printk("c=%08x sz=%llx",
		      __entry->cookie, __entry->new_size)
	    );

TRACE_EVENT(fscache_resize,
	    TP_PROTO(struct fscache_cookie *cookie, loff_t new_size),

	    TP_ARGS(cookie, new_size),

	    TP_STRUCT__entry(
		    __field(unsigned int,		cookie		)
		    __field(loff_t,			old_size	)
		    __field(loff_t,			new_size	)
			     ),

	    TP_fast_assign(
		    __entry->cookie	= cookie->debug_id;
		    __entry->old_size	= cookie->object_size;
		    __entry->new_size	= new_size;
			   ),

	    TP_printk("c=%08x os=%08llx sz=%08llx",
		      __entry->cookie,
		      __entry->old_size,
		      __entry->new_size)
	    );

#endif /* _TRACE_FSCACHE_H */

/* This part must be outside protection */
#include <trace/define_trace.h>
