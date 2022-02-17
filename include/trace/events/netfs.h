/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Network filesystem support module tracepoints
 *
 * Copyright (C) 2021 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 */
#undef TRACE_SYSTEM
#define TRACE_SYSTEM netfs

#if !defined(_TRACE_NETFS_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_NETFS_H

#include <linux/tracepoint.h>

/*
 * Define enums for tracing information.
 */
#define netfs_read_traces					\
	EM(netfs_read_trace_expanded,		"EXPANDED ")	\
	EM(netfs_read_trace_readahead,		"READAHEAD")	\
	EM(netfs_read_trace_readpage,		"READPAGE ")	\
	E_(netfs_read_trace_write_begin,	"WRITEBEGN")

#define netfs_rreq_traces					\
	EM(netfs_rreq_trace_assess,		"ASSESS ")	\
	EM(netfs_rreq_trace_copy,		"COPY   ")	\
	EM(netfs_rreq_trace_done,		"DONE   ")	\
	EM(netfs_rreq_trace_free,		"FREE   ")	\
	EM(netfs_rreq_trace_resubmit,		"RESUBMT")	\
	EM(netfs_rreq_trace_unlock,		"UNLOCK ")	\
	E_(netfs_rreq_trace_unmark,		"UNMARK ")

#define netfs_sreq_sources					\
	EM(NETFS_FILL_WITH_ZEROES,		"ZERO")		\
	EM(NETFS_DOWNLOAD_FROM_SERVER,		"DOWN")		\
	EM(NETFS_READ_FROM_CACHE,		"READ")		\
	E_(NETFS_INVALID_READ,			"INVL")		\

#define netfs_sreq_traces					\
	EM(netfs_sreq_trace_download_instead,	"RDOWN")	\
	EM(netfs_sreq_trace_free,		"FREE ")	\
	EM(netfs_sreq_trace_prepare,		"PREP ")	\
	EM(netfs_sreq_trace_resubmit_short,	"SHORT")	\
	EM(netfs_sreq_trace_submit,		"SUBMT")	\
	EM(netfs_sreq_trace_terminated,		"TERM ")	\
	EM(netfs_sreq_trace_write,		"WRITE")	\
	EM(netfs_sreq_trace_write_skip,		"SKIP ")	\
	E_(netfs_sreq_trace_write_term,		"WTERM")

#define netfs_failures							\
	EM(netfs_fail_check_write_begin,	"check-write-begin")	\
	EM(netfs_fail_copy_to_cache,		"copy-to-cache")	\
	EM(netfs_fail_read,			"read")			\
	EM(netfs_fail_short_readpage,		"short-readpage")	\
	EM(netfs_fail_short_write_begin,	"short-write-begin")	\
	E_(netfs_fail_prepare_write,		"prep-write")

#define netfs_rreq_ref_traces					\
	EM(netfs_rreq_trace_get_hold,		"GET HOLD   ")	\
	EM(netfs_rreq_trace_get_subreq,		"GET SUBREQ ")	\
	EM(netfs_rreq_trace_put_complete,	"PUT COMPLT ")	\
	EM(netfs_rreq_trace_put_failed,		"PUT FAILED ")	\
	EM(netfs_rreq_trace_put_hold,		"PUT HOLD   ")	\
	EM(netfs_rreq_trace_put_subreq,		"PUT SUBREQ ")	\
	E_(netfs_rreq_trace_new,		"NEW        ")

#define netfs_sreq_ref_traces					\
	EM(netfs_sreq_trace_get_copy_to_cache,	"GET COPY2C ")	\
	EM(netfs_sreq_trace_get_resubmit,	"GET RESUBMIT")	\
	EM(netfs_sreq_trace_get_short_read,	"GET SHORTRD")	\
	EM(netfs_sreq_trace_new,		"NEW        ")	\
	EM(netfs_sreq_trace_put_clear,		"PUT CLEAR  ")	\
	EM(netfs_sreq_trace_put_failed,		"PUT FAILED ")	\
	EM(netfs_sreq_trace_put_merged,		"PUT MERGED ")	\
	EM(netfs_sreq_trace_put_no_copy,	"PUT NO COPY")	\
	E_(netfs_sreq_trace_put_terminated,	"PUT TERM   ")

#ifndef __NETFS_DECLARE_TRACE_ENUMS_ONCE_ONLY
#define __NETFS_DECLARE_TRACE_ENUMS_ONCE_ONLY

#undef EM
#undef E_
#define EM(a, b) a,
#define E_(a, b) a

enum netfs_read_trace { netfs_read_traces } __mode(byte);
enum netfs_rreq_trace { netfs_rreq_traces } __mode(byte);
enum netfs_sreq_trace { netfs_sreq_traces } __mode(byte);
enum netfs_failure { netfs_failures } __mode(byte);
enum netfs_rreq_ref_trace { netfs_rreq_ref_traces } __mode(byte);
enum netfs_sreq_ref_trace { netfs_sreq_ref_traces } __mode(byte);

#endif

/*
 * Export enum symbols via userspace.
 */
#undef EM
#undef E_
#define EM(a, b) TRACE_DEFINE_ENUM(a);
#define E_(a, b) TRACE_DEFINE_ENUM(a);

netfs_read_traces;
netfs_rreq_traces;
netfs_sreq_sources;
netfs_sreq_traces;
netfs_failures;
netfs_rreq_ref_traces;
netfs_sreq_ref_traces;

/*
 * Now redefine the EM() and E_() macros to map the enums to the strings that
 * will be printed in the output.
 */
#undef EM
#undef E_
#define EM(a, b)	{ a, b },
#define E_(a, b)	{ a, b }

TRACE_EVENT(netfs_read,
	    TP_PROTO(struct netfs_io_request *rreq,
		     loff_t start, size_t len,
		     enum netfs_read_trace what),

	    TP_ARGS(rreq, start, len, what),

	    TP_STRUCT__entry(
		    __field(unsigned int,		rreq		)
		    __field(unsigned int,		cookie		)
		    __field(loff_t,			start		)
		    __field(size_t,			len		)
		    __field(enum netfs_read_trace,	what		)
		    __field(unsigned int,		netfs_inode	)
			     ),

	    TP_fast_assign(
		    __entry->rreq	= rreq->debug_id;
		    __entry->cookie	= rreq->cache_resources.debug_id;
		    __entry->start	= start;
		    __entry->len	= len;
		    __entry->what	= what;
		    __entry->netfs_inode = rreq->inode->i_ino;
			   ),

	    TP_printk("R=%08x %s c=%08x ni=%x s=%llx %zx",
		      __entry->rreq,
		      __print_symbolic(__entry->what, netfs_read_traces),
		      __entry->cookie,
		      __entry->netfs_inode,
		      __entry->start, __entry->len)
	    );

TRACE_EVENT(netfs_rreq,
	    TP_PROTO(struct netfs_io_request *rreq,
		     enum netfs_rreq_trace what),

	    TP_ARGS(rreq, what),

	    TP_STRUCT__entry(
		    __field(unsigned int,		rreq		)
		    __field(unsigned int,		flags		)
		    __field(enum netfs_rreq_trace,	what		)
			     ),

	    TP_fast_assign(
		    __entry->rreq	= rreq->debug_id;
		    __entry->flags	= rreq->flags;
		    __entry->what	= what;
			   ),

	    TP_printk("R=%08x %s f=%02x",
		      __entry->rreq,
		      __print_symbolic(__entry->what, netfs_rreq_traces),
		      __entry->flags)
	    );

TRACE_EVENT(netfs_sreq,
	    TP_PROTO(struct netfs_io_subrequest *sreq,
		     enum netfs_sreq_trace what),

	    TP_ARGS(sreq, what),

	    TP_STRUCT__entry(
		    __field(unsigned int,		rreq		)
		    __field(unsigned short,		index		)
		    __field(short,			error		)
		    __field(unsigned short,		flags		)
		    __field(enum netfs_io_source,	source		)
		    __field(enum netfs_sreq_trace,	what		)
		    __field(size_t,			len		)
		    __field(size_t,			transferred	)
		    __field(loff_t,			start		)
			     ),

	    TP_fast_assign(
		    __entry->rreq	= sreq->rreq->debug_id;
		    __entry->index	= sreq->debug_index;
		    __entry->error	= sreq->error;
		    __entry->flags	= sreq->flags;
		    __entry->source	= sreq->source;
		    __entry->what	= what;
		    __entry->len	= sreq->len;
		    __entry->transferred = sreq->transferred;
		    __entry->start	= sreq->start;
			   ),

	    TP_printk("R=%08x[%u] %s %s f=%02x s=%llx %zx/%zx e=%d",
		      __entry->rreq, __entry->index,
		      __print_symbolic(__entry->source, netfs_sreq_sources),
		      __print_symbolic(__entry->what, netfs_sreq_traces),
		      __entry->flags,
		      __entry->start, __entry->transferred, __entry->len,
		      __entry->error)
	    );

TRACE_EVENT(netfs_failure,
	    TP_PROTO(struct netfs_io_request *rreq,
		     struct netfs_io_subrequest *sreq,
		     int error, enum netfs_failure what),

	    TP_ARGS(rreq, sreq, error, what),

	    TP_STRUCT__entry(
		    __field(unsigned int,		rreq		)
		    __field(unsigned short,		index		)
		    __field(short,			error		)
		    __field(unsigned short,		flags		)
		    __field(enum netfs_io_source,	source		)
		    __field(enum netfs_failure,		what		)
		    __field(size_t,			len		)
		    __field(size_t,			transferred	)
		    __field(loff_t,			start		)
			     ),

	    TP_fast_assign(
		    __entry->rreq	= rreq->debug_id;
		    __entry->index	= sreq ? sreq->debug_index : 0;
		    __entry->error	= error;
		    __entry->flags	= sreq ? sreq->flags : 0;
		    __entry->source	= sreq ? sreq->source : NETFS_INVALID_READ;
		    __entry->what	= what;
		    __entry->len	= sreq ? sreq->len : 0;
		    __entry->transferred = sreq ? sreq->transferred : 0;
		    __entry->start	= sreq ? sreq->start : 0;
			   ),

	    TP_printk("R=%08x[%u] %s f=%02x s=%llx %zx/%zx %s e=%d",
		      __entry->rreq, __entry->index,
		      __print_symbolic(__entry->source, netfs_sreq_sources),
		      __entry->flags,
		      __entry->start, __entry->transferred, __entry->len,
		      __print_symbolic(__entry->what, netfs_failures),
		      __entry->error)
	    );

TRACE_EVENT(netfs_rreq_ref,
	    TP_PROTO(unsigned int rreq_debug_id, int ref,
		     enum netfs_rreq_ref_trace what),

	    TP_ARGS(rreq_debug_id, ref, what),

	    TP_STRUCT__entry(
		    __field(unsigned int,		rreq		)
		    __field(int,			ref		)
		    __field(enum netfs_rreq_ref_trace,	what		)
			     ),

	    TP_fast_assign(
		    __entry->rreq	= rreq_debug_id;
		    __entry->ref	= ref;
		    __entry->what	= what;
			   ),

	    TP_printk("R=%08x %s r=%u",
		      __entry->rreq,
		      __print_symbolic(__entry->what, netfs_rreq_ref_traces),
		      __entry->ref)
	    );

TRACE_EVENT(netfs_sreq_ref,
	    TP_PROTO(unsigned int rreq_debug_id, unsigned int subreq_debug_index,
		     int ref, enum netfs_sreq_ref_trace what),

	    TP_ARGS(rreq_debug_id, subreq_debug_index, ref, what),

	    TP_STRUCT__entry(
		    __field(unsigned int,		rreq		)
		    __field(unsigned int,		subreq		)
		    __field(int,			ref		)
		    __field(enum netfs_sreq_ref_trace,	what		)
			     ),

	    TP_fast_assign(
		    __entry->rreq	= rreq_debug_id;
		    __entry->subreq	= subreq_debug_index;
		    __entry->ref	= ref;
		    __entry->what	= what;
			   ),

	    TP_printk("R=%08x[%x] %s r=%u",
		      __entry->rreq,
		      __entry->subreq,
		      __print_symbolic(__entry->what, netfs_sreq_ref_traces),
		      __entry->ref)
	    );

#undef EM
#undef E_
#endif /* _TRACE_NETFS_H */

/* This part must be outside protection */
#include <trace/define_trace.h>
