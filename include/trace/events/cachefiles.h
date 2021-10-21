/* SPDX-License-Identifier: GPL-2.0-or-later */
/* CacheFiles tracepoints
 *
 * Copyright (C) 2021 Red Hat, Inc. All Rights Reserved.
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

enum cachefiles_error_trace {
	cachefiles_trace_fallocate_error,
	cachefiles_trace_getxattr_error,
	cachefiles_trace_link_error,
	cachefiles_trace_lookup_error,
	cachefiles_trace_mkdir_error,
	cachefiles_trace_notify_change_error,
	cachefiles_trace_open_error,
	cachefiles_trace_read_error,
	cachefiles_trace_remxattr_error,
	cachefiles_trace_rename_error,
	cachefiles_trace_seek_error,
	cachefiles_trace_setxattr_error,
	cachefiles_trace_statfs_error,
	cachefiles_trace_tmpfile_error,
	cachefiles_trace_trunc_error,
	cachefiles_trace_unlink_error,
	cachefiles_trace_write_error,
};

#endif

/*
 * Define enum -> string mappings for display.
 */
#define cachefiles_error_traces						\
	EM(cachefiles_trace_fallocate_error,	"fallocate")		\
	EM(cachefiles_trace_getxattr_error,	"getxattr")		\
	EM(cachefiles_trace_link_error,		"link")			\
	EM(cachefiles_trace_lookup_error,	"lookup")		\
	EM(cachefiles_trace_mkdir_error,	"mkdir")		\
	EM(cachefiles_trace_notify_change_error, "notify_change")	\
	EM(cachefiles_trace_open_error,		"open")			\
	EM(cachefiles_trace_read_error,		"read")			\
	EM(cachefiles_trace_remxattr_error,	"remxattr")		\
	EM(cachefiles_trace_rename_error,	"rename")		\
	EM(cachefiles_trace_seek_error,		"seek")			\
	EM(cachefiles_trace_setxattr_error,	"setxattr")		\
	EM(cachefiles_trace_statfs_error,	"statfs")		\
	EM(cachefiles_trace_tmpfile_error,	"tmpfile")		\
	EM(cachefiles_trace_trunc_error,	"trunc")		\
	EM(cachefiles_trace_unlink_error,	"unlink")		\
	E_(cachefiles_trace_write_error,	"write")


/*
 * Export enum symbols via userspace.
 */
#undef EM
#undef E_
#define EM(a, b) TRACE_DEFINE_ENUM(a);
#define E_(a, b) TRACE_DEFINE_ENUM(a);

cachefiles_error_traces;

/*
 * Now redefine the EM() and E_() macros to map the enums to the strings that
 * will be printed in the output.
 */
#undef EM
#undef E_
#define EM(a, b)	{ a, b },
#define E_(a, b)	{ a, b }


TRACE_EVENT(cachefiles_vfs_error,
	    TP_PROTO(struct cachefiles_object *obj, struct inode *backer,
		     int error, enum cachefiles_error_trace where),

	    TP_ARGS(obj, backer, error, where),

	    TP_STRUCT__entry(
		    __field(unsigned int,			obj	)
		    __field(unsigned int,			backer	)
		    __field(enum cachefiles_error_trace,	where	)
		    __field(short,				error	)
			     ),

	    TP_fast_assign(
		    __entry->obj	= obj ? obj->debug_id : 0;
		    __entry->backer	= backer->i_ino;
		    __entry->error	= error;
		    __entry->where	= where;
			   ),

	    TP_printk("o=%08x b=%08x %s e=%d",
		      __entry->obj,
		      __entry->backer,
		      __print_symbolic(__entry->where, cachefiles_error_traces),
		      __entry->error)
	    );

TRACE_EVENT(cachefiles_io_error,
	    TP_PROTO(struct cachefiles_object *obj, struct inode *backer,
		     int error, enum cachefiles_error_trace where),

	    TP_ARGS(obj, backer, error, where),

	    TP_STRUCT__entry(
		    __field(unsigned int,			obj	)
		    __field(unsigned int,			backer	)
		    __field(enum cachefiles_error_trace,	where	)
		    __field(short,				error	)
			     ),

	    TP_fast_assign(
		    __entry->obj	= obj ? obj->debug_id : 0;
		    __entry->backer	= backer->i_ino;
		    __entry->error	= error;
		    __entry->where	= where;
			   ),

	    TP_printk("o=%08x b=%08x %s e=%d",
		      __entry->obj,
		      __entry->backer,
		      __print_symbolic(__entry->where, cachefiles_error_traces),
		      __entry->error)
	    );

#endif /* _TRACE_CACHEFILES_H */

/* This part must be outside protection */
#include <trace/define_trace.h>
