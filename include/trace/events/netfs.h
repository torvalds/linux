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
	EM(netfs_read_trace_dio_read,		"DIO-READ ")	\
	EM(netfs_read_trace_expanded,		"EXPANDED ")	\
	EM(netfs_read_trace_readahead,		"READAHEAD")	\
	EM(netfs_read_trace_readpage,		"READPAGE ")	\
	EM(netfs_read_trace_prefetch_for_write,	"PREFETCHW")	\
	E_(netfs_read_trace_write_begin,	"WRITEBEGN")

#define netfs_write_traces					\
	EM(netfs_write_trace_dio_write,		"DIO-WRITE")	\
	EM(netfs_write_trace_launder,		"LAUNDER  ")	\
	EM(netfs_write_trace_unbuffered_write,	"UNB-WRITE")	\
	EM(netfs_write_trace_writeback,		"WRITEBACK")	\
	E_(netfs_write_trace_writethrough,	"WRITETHRU")

#define netfs_rreq_origins					\
	EM(NETFS_READAHEAD,			"RA")		\
	EM(NETFS_READPAGE,			"RP")		\
	EM(NETFS_READ_FOR_WRITE,		"RW")		\
	EM(NETFS_WRITEBACK,			"WB")		\
	EM(NETFS_WRITETHROUGH,			"WT")		\
	EM(NETFS_LAUNDER_WRITE,			"LW")		\
	EM(NETFS_UNBUFFERED_WRITE,		"UW")		\
	EM(NETFS_DIO_READ,			"DR")		\
	E_(NETFS_DIO_WRITE,			"DW")

#define netfs_rreq_traces					\
	EM(netfs_rreq_trace_assess,		"ASSESS ")	\
	EM(netfs_rreq_trace_copy,		"COPY   ")	\
	EM(netfs_rreq_trace_done,		"DONE   ")	\
	EM(netfs_rreq_trace_free,		"FREE   ")	\
	EM(netfs_rreq_trace_redirty,		"REDIRTY")	\
	EM(netfs_rreq_trace_resubmit,		"RESUBMT")	\
	EM(netfs_rreq_trace_unlock,		"UNLOCK ")	\
	EM(netfs_rreq_trace_unmark,		"UNMARK ")	\
	EM(netfs_rreq_trace_wait_ip,		"WAIT-IP")	\
	EM(netfs_rreq_trace_wake_ip,		"WAKE-IP")	\
	E_(netfs_rreq_trace_write_done,		"WR-DONE")

#define netfs_sreq_sources					\
	EM(NETFS_FILL_WITH_ZEROES,		"ZERO")		\
	EM(NETFS_DOWNLOAD_FROM_SERVER,		"DOWN")		\
	EM(NETFS_READ_FROM_CACHE,		"READ")		\
	EM(NETFS_INVALID_READ,			"INVL")		\
	EM(NETFS_UPLOAD_TO_SERVER,		"UPLD")		\
	EM(NETFS_WRITE_TO_CACHE,		"WRIT")		\
	E_(NETFS_INVALID_WRITE,			"INVL")

#define netfs_sreq_traces					\
	EM(netfs_sreq_trace_download_instead,	"RDOWN")	\
	EM(netfs_sreq_trace_free,		"FREE ")	\
	EM(netfs_sreq_trace_limited,		"LIMIT")	\
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
	EM(netfs_fail_dio_read_short,		"dio-read-short")	\
	EM(netfs_fail_dio_read_zero,		"dio-read-zero")	\
	EM(netfs_fail_read,			"read")			\
	EM(netfs_fail_short_read,		"short-read")		\
	EM(netfs_fail_prepare_write,		"prep-write")		\
	E_(netfs_fail_write,			"write")

#define netfs_rreq_ref_traces					\
	EM(netfs_rreq_trace_get_for_outstanding,"GET OUTSTND")	\
	EM(netfs_rreq_trace_get_subreq,		"GET SUBREQ ")	\
	EM(netfs_rreq_trace_put_complete,	"PUT COMPLT ")	\
	EM(netfs_rreq_trace_put_discard,	"PUT DISCARD")	\
	EM(netfs_rreq_trace_put_failed,		"PUT FAILED ")	\
	EM(netfs_rreq_trace_put_no_submit,	"PUT NO-SUBM")	\
	EM(netfs_rreq_trace_put_return,		"PUT RETURN ")	\
	EM(netfs_rreq_trace_put_subreq,		"PUT SUBREQ ")	\
	EM(netfs_rreq_trace_put_work,		"PUT WORK   ")	\
	EM(netfs_rreq_trace_see_work,		"SEE WORK   ")	\
	E_(netfs_rreq_trace_new,		"NEW        ")

#define netfs_sreq_ref_traces					\
	EM(netfs_sreq_trace_get_copy_to_cache,	"GET COPY2C ")	\
	EM(netfs_sreq_trace_get_resubmit,	"GET RESUBMIT")	\
	EM(netfs_sreq_trace_get_short_read,	"GET SHORTRD")	\
	EM(netfs_sreq_trace_new,		"NEW        ")	\
	EM(netfs_sreq_trace_put_clear,		"PUT CLEAR  ")	\
	EM(netfs_sreq_trace_put_discard,	"PUT DISCARD")	\
	EM(netfs_sreq_trace_put_failed,		"PUT FAILED ")	\
	EM(netfs_sreq_trace_put_merged,		"PUT MERGED ")	\
	EM(netfs_sreq_trace_put_no_copy,	"PUT NO COPY")	\
	EM(netfs_sreq_trace_put_wip,		"PUT WIP    ")	\
	EM(netfs_sreq_trace_put_work,		"PUT WORK   ")	\
	E_(netfs_sreq_trace_put_terminated,	"PUT TERM   ")

#define netfs_folio_traces					\
	/* The first few correspond to enum netfs_how_to_modify */	\
	EM(netfs_folio_is_uptodate,		"mod-uptodate")	\
	EM(netfs_just_prefetch,			"mod-prefetch")	\
	EM(netfs_whole_folio_modify,		"mod-whole-f")	\
	EM(netfs_modify_and_clear,		"mod-n-clear")	\
	EM(netfs_streaming_write,		"mod-streamw")	\
	EM(netfs_streaming_write_cont,		"mod-streamw+")	\
	EM(netfs_flush_content,			"flush")	\
	EM(netfs_streaming_filled_page,		"mod-streamw-f") \
	EM(netfs_streaming_cont_filled_page,	"mod-streamw-f+") \
	/* The rest are for writeback */			\
	EM(netfs_folio_trace_clear,		"clear")	\
	EM(netfs_folio_trace_clear_s,		"clear-s")	\
	EM(netfs_folio_trace_clear_g,		"clear-g")	\
	EM(netfs_folio_trace_copy_to_cache,	"copy")		\
	EM(netfs_folio_trace_end_copy,		"end-copy")	\
	EM(netfs_folio_trace_filled_gaps,	"filled-gaps")	\
	EM(netfs_folio_trace_kill,		"kill")		\
	EM(netfs_folio_trace_launder,		"launder")	\
	EM(netfs_folio_trace_mkwrite,		"mkwrite")	\
	EM(netfs_folio_trace_mkwrite_plus,	"mkwrite+")	\
	EM(netfs_folio_trace_read_gaps,		"read-gaps")	\
	EM(netfs_folio_trace_redirty,		"redirty")	\
	EM(netfs_folio_trace_redirtied,		"redirtied")	\
	EM(netfs_folio_trace_store,		"store")	\
	EM(netfs_folio_trace_store_plus,	"store+")	\
	EM(netfs_folio_trace_wthru,		"wthru")	\
	E_(netfs_folio_trace_wthru_plus,	"wthru+")

#ifndef __NETFS_DECLARE_TRACE_ENUMS_ONCE_ONLY
#define __NETFS_DECLARE_TRACE_ENUMS_ONCE_ONLY

#undef EM
#undef E_
#define EM(a, b) a,
#define E_(a, b) a

enum netfs_read_trace { netfs_read_traces } __mode(byte);
enum netfs_write_trace { netfs_write_traces } __mode(byte);
enum netfs_rreq_trace { netfs_rreq_traces } __mode(byte);
enum netfs_sreq_trace { netfs_sreq_traces } __mode(byte);
enum netfs_failure { netfs_failures } __mode(byte);
enum netfs_rreq_ref_trace { netfs_rreq_ref_traces } __mode(byte);
enum netfs_sreq_ref_trace { netfs_sreq_ref_traces } __mode(byte);
enum netfs_folio_trace { netfs_folio_traces } __mode(byte);

#endif

/*
 * Export enum symbols via userspace.
 */
#undef EM
#undef E_
#define EM(a, b) TRACE_DEFINE_ENUM(a);
#define E_(a, b) TRACE_DEFINE_ENUM(a);

netfs_read_traces;
netfs_write_traces;
netfs_rreq_origins;
netfs_rreq_traces;
netfs_sreq_sources;
netfs_sreq_traces;
netfs_failures;
netfs_rreq_ref_traces;
netfs_sreq_ref_traces;
netfs_folio_traces;

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
		    __field(enum netfs_io_origin,	origin		)
		    __field(enum netfs_rreq_trace,	what		)
			     ),

	    TP_fast_assign(
		    __entry->rreq	= rreq->debug_id;
		    __entry->flags	= rreq->flags;
		    __entry->origin	= rreq->origin;
		    __entry->what	= what;
			   ),

	    TP_printk("R=%08x %s %s f=%02x",
		      __entry->rreq,
		      __print_symbolic(__entry->origin, netfs_rreq_origins),
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
		    __field(short,			index		)
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
		    __entry->index	= sreq ? sreq->debug_index : -1;
		    __entry->error	= error;
		    __entry->flags	= sreq ? sreq->flags : 0;
		    __entry->source	= sreq ? sreq->source : NETFS_INVALID_READ;
		    __entry->what	= what;
		    __entry->len	= sreq ? sreq->len : rreq->len;
		    __entry->transferred = sreq ? sreq->transferred : 0;
		    __entry->start	= sreq ? sreq->start : 0;
			   ),

	    TP_printk("R=%08x[%d] %s f=%02x s=%llx %zx/%zx %s e=%d",
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

TRACE_EVENT(netfs_folio,
	    TP_PROTO(struct folio *folio, enum netfs_folio_trace why),

	    TP_ARGS(folio, why),

	    TP_STRUCT__entry(
		    __field(ino_t,			ino)
		    __field(pgoff_t,			index)
		    __field(unsigned int,		nr)
		    __field(enum netfs_folio_trace,	why)
			     ),

	    TP_fast_assign(
		    __entry->ino = folio->mapping->host->i_ino;
		    __entry->why = why;
		    __entry->index = folio_index(folio);
		    __entry->nr = folio_nr_pages(folio);
			   ),

	    TP_printk("i=%05lx ix=%05lx-%05lx %s",
		      __entry->ino, __entry->index, __entry->index + __entry->nr - 1,
		      __print_symbolic(__entry->why, netfs_folio_traces))
	    );

TRACE_EVENT(netfs_write_iter,
	    TP_PROTO(const struct kiocb *iocb, const struct iov_iter *from),

	    TP_ARGS(iocb, from),

	    TP_STRUCT__entry(
		    __field(unsigned long long,		start		)
		    __field(size_t,			len		)
		    __field(unsigned int,		flags		)
			     ),

	    TP_fast_assign(
		    __entry->start	= iocb->ki_pos;
		    __entry->len	= iov_iter_count(from);
		    __entry->flags	= iocb->ki_flags;
			   ),

	    TP_printk("WRITE-ITER s=%llx l=%zx f=%x",
		      __entry->start, __entry->len, __entry->flags)
	    );

TRACE_EVENT(netfs_write,
	    TP_PROTO(const struct netfs_io_request *wreq,
		     enum netfs_write_trace what),

	    TP_ARGS(wreq, what),

	    TP_STRUCT__entry(
		    __field(unsigned int,		wreq		)
		    __field(unsigned int,		cookie		)
		    __field(enum netfs_write_trace,	what		)
		    __field(unsigned long long,		start		)
		    __field(size_t,			len		)
			     ),

	    TP_fast_assign(
		    struct netfs_inode *__ctx = netfs_inode(wreq->inode);
		    struct fscache_cookie *__cookie = netfs_i_cookie(__ctx);
		    __entry->wreq	= wreq->debug_id;
		    __entry->cookie	= __cookie ? __cookie->debug_id : 0;
		    __entry->what	= what;
		    __entry->start	= wreq->start;
		    __entry->len	= wreq->len;
			   ),

	    TP_printk("R=%08x %s c=%08x by=%llx-%llx",
		      __entry->wreq,
		      __print_symbolic(__entry->what, netfs_write_traces),
		      __entry->cookie,
		      __entry->start, __entry->start + __entry->len - 1)
	    );

#undef EM
#undef E_
#endif /* _TRACE_NETFS_H */

/* This part must be outside protection */
#include <trace/define_trace.h>
