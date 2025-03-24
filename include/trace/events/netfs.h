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
	EM(netfs_read_trace_read_gaps,		"READ-GAPS")	\
	EM(netfs_read_trace_read_single,	"READ-SNGL")	\
	EM(netfs_read_trace_prefetch_for_write,	"PREFETCHW")	\
	E_(netfs_read_trace_write_begin,	"WRITEBEGN")

#define netfs_write_traces					\
	EM(netfs_write_trace_copy_to_cache,	"COPY2CACH")	\
	EM(netfs_write_trace_dio_write,		"DIO-WRITE")	\
	EM(netfs_write_trace_unbuffered_write,	"UNB-WRITE")	\
	EM(netfs_write_trace_writeback,		"WRITEBACK")	\
	E_(netfs_write_trace_writethrough,	"WRITETHRU")

#define netfs_rreq_origins					\
	EM(NETFS_READAHEAD,			"RA")		\
	EM(NETFS_READPAGE,			"RP")		\
	EM(NETFS_READ_GAPS,			"RG")		\
	EM(NETFS_READ_SINGLE,			"R1")		\
	EM(NETFS_READ_FOR_WRITE,		"RW")		\
	EM(NETFS_DIO_READ,			"DR")		\
	EM(NETFS_WRITEBACK,			"WB")		\
	EM(NETFS_WRITEBACK_SINGLE,		"W1")		\
	EM(NETFS_WRITETHROUGH,			"WT")		\
	EM(NETFS_UNBUFFERED_WRITE,		"UW")		\
	EM(NETFS_DIO_WRITE,			"DW")		\
	E_(NETFS_PGPRIV2_COPY_TO_CACHE,		"2C")

#define netfs_rreq_traces					\
	EM(netfs_rreq_trace_assess,		"ASSESS ")	\
	EM(netfs_rreq_trace_copy,		"COPY   ")	\
	EM(netfs_rreq_trace_collect,		"COLLECT")	\
	EM(netfs_rreq_trace_complete,		"COMPLET")	\
	EM(netfs_rreq_trace_dirty,		"DIRTY  ")	\
	EM(netfs_rreq_trace_done,		"DONE   ")	\
	EM(netfs_rreq_trace_free,		"FREE   ")	\
	EM(netfs_rreq_trace_redirty,		"REDIRTY")	\
	EM(netfs_rreq_trace_resubmit,		"RESUBMT")	\
	EM(netfs_rreq_trace_set_abandon,	"S-ABNDN")	\
	EM(netfs_rreq_trace_set_pause,		"PAUSE  ")	\
	EM(netfs_rreq_trace_unlock,		"UNLOCK ")	\
	EM(netfs_rreq_trace_unlock_pgpriv2,	"UNLCK-2")	\
	EM(netfs_rreq_trace_unmark,		"UNMARK ")	\
	EM(netfs_rreq_trace_wait_ip,		"WAIT-IP")	\
	EM(netfs_rreq_trace_wait_pause,		"WT-PAUS")	\
	EM(netfs_rreq_trace_wait_queue,		"WAIT-Q ")	\
	EM(netfs_rreq_trace_wake_ip,		"WAKE-IP")	\
	EM(netfs_rreq_trace_wake_queue,		"WAKE-Q ")	\
	EM(netfs_rreq_trace_woke_queue,		"WOKE-Q ")	\
	EM(netfs_rreq_trace_unpause,		"UNPAUSE")	\
	E_(netfs_rreq_trace_write_done,		"WR-DONE")

#define netfs_sreq_sources					\
	EM(NETFS_SOURCE_UNKNOWN,		"----")		\
	EM(NETFS_FILL_WITH_ZEROES,		"ZERO")		\
	EM(NETFS_DOWNLOAD_FROM_SERVER,		"DOWN")		\
	EM(NETFS_READ_FROM_CACHE,		"READ")		\
	EM(NETFS_INVALID_READ,			"INVL")		\
	EM(NETFS_UPLOAD_TO_SERVER,		"UPLD")		\
	EM(NETFS_WRITE_TO_CACHE,		"WRIT")		\
	E_(NETFS_INVALID_WRITE,			"INVL")

#define netfs_sreq_traces					\
	EM(netfs_sreq_trace_add_donations,	"+DON ")	\
	EM(netfs_sreq_trace_added,		"ADD  ")	\
	EM(netfs_sreq_trace_cache_nowrite,	"CA-NW")	\
	EM(netfs_sreq_trace_cache_prepare,	"CA-PR")	\
	EM(netfs_sreq_trace_cache_write,	"CA-WR")	\
	EM(netfs_sreq_trace_cancel,		"CANCL")	\
	EM(netfs_sreq_trace_clear,		"CLEAR")	\
	EM(netfs_sreq_trace_discard,		"DSCRD")	\
	EM(netfs_sreq_trace_donate_to_prev,	"DON-P")	\
	EM(netfs_sreq_trace_donate_to_next,	"DON-N")	\
	EM(netfs_sreq_trace_download_instead,	"RDOWN")	\
	EM(netfs_sreq_trace_fail,		"FAIL ")	\
	EM(netfs_sreq_trace_free,		"FREE ")	\
	EM(netfs_sreq_trace_hit_eof,		"EOF  ")	\
	EM(netfs_sreq_trace_io_progress,	"IO   ")	\
	EM(netfs_sreq_trace_limited,		"LIMIT")	\
	EM(netfs_sreq_trace_need_clear,		"N-CLR")	\
	EM(netfs_sreq_trace_partial_read,	"PARTR")	\
	EM(netfs_sreq_trace_need_retry,		"ND-RT")	\
	EM(netfs_sreq_trace_prepare,		"PREP ")	\
	EM(netfs_sreq_trace_prep_failed,	"PRPFL")	\
	EM(netfs_sreq_trace_progress,		"PRGRS")	\
	EM(netfs_sreq_trace_reprep_failed,	"REPFL")	\
	EM(netfs_sreq_trace_retry,		"RETRY")	\
	EM(netfs_sreq_trace_short,		"SHORT")	\
	EM(netfs_sreq_trace_split,		"SPLIT")	\
	EM(netfs_sreq_trace_submit,		"SUBMT")	\
	EM(netfs_sreq_trace_superfluous,	"SPRFL")	\
	EM(netfs_sreq_trace_terminated,		"TERM ")	\
	EM(netfs_sreq_trace_wait_for,		"_WAIT")	\
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
	EM(netfs_rreq_trace_get_work,		"GET WORK   ")	\
	EM(netfs_rreq_trace_put_complete,	"PUT COMPLT ")	\
	EM(netfs_rreq_trace_put_discard,	"PUT DISCARD")	\
	EM(netfs_rreq_trace_put_failed,		"PUT FAILED ")	\
	EM(netfs_rreq_trace_put_no_submit,	"PUT NO-SUBM")	\
	EM(netfs_rreq_trace_put_return,		"PUT RETURN ")	\
	EM(netfs_rreq_trace_put_subreq,		"PUT SUBREQ ")	\
	EM(netfs_rreq_trace_put_work,		"PUT WORK   ")	\
	EM(netfs_rreq_trace_put_work_complete,	"PUT WORK CP")	\
	EM(netfs_rreq_trace_put_work_nq,	"PUT WORK NQ")	\
	EM(netfs_rreq_trace_see_work,		"SEE WORK   ")	\
	E_(netfs_rreq_trace_new,		"NEW        ")

#define netfs_sreq_ref_traces					\
	EM(netfs_sreq_trace_get_copy_to_cache,	"GET COPY2C ")	\
	EM(netfs_sreq_trace_get_resubmit,	"GET RESUBMIT")	\
	EM(netfs_sreq_trace_get_submit,		"GET SUBMIT")	\
	EM(netfs_sreq_trace_get_short_read,	"GET SHORTRD")	\
	EM(netfs_sreq_trace_new,		"NEW        ")	\
	EM(netfs_sreq_trace_put_abandon,	"PUT ABANDON")	\
	EM(netfs_sreq_trace_put_cancel,		"PUT CANCEL ")	\
	EM(netfs_sreq_trace_put_clear,		"PUT CLEAR  ")	\
	EM(netfs_sreq_trace_put_consumed,	"PUT CONSUME")	\
	EM(netfs_sreq_trace_put_done,		"PUT DONE   ")	\
	EM(netfs_sreq_trace_put_failed,		"PUT FAILED ")	\
	EM(netfs_sreq_trace_put_merged,		"PUT MERGED ")	\
	EM(netfs_sreq_trace_put_no_copy,	"PUT NO COPY")	\
	EM(netfs_sreq_trace_put_oom,		"PUT OOM    ")	\
	EM(netfs_sreq_trace_put_wip,		"PUT WIP    ")	\
	EM(netfs_sreq_trace_put_work,		"PUT WORK   ")	\
	E_(netfs_sreq_trace_put_terminated,	"PUT TERM   ")

#define netfs_folio_traces					\
	EM(netfs_folio_is_uptodate,		"mod-uptodate")	\
	EM(netfs_just_prefetch,			"mod-prefetch")	\
	EM(netfs_whole_folio_modify,		"mod-whole-f")	\
	EM(netfs_modify_and_clear,		"mod-n-clear")	\
	EM(netfs_streaming_write,		"mod-streamw")	\
	EM(netfs_streaming_write_cont,		"mod-streamw+")	\
	EM(netfs_flush_content,			"flush")	\
	EM(netfs_streaming_filled_page,		"mod-streamw-f") \
	EM(netfs_streaming_cont_filled_page,	"mod-streamw-f+") \
	EM(netfs_folio_trace_abandon,		"abandon")	\
	EM(netfs_folio_trace_alloc_buffer,	"alloc-buf")	\
	EM(netfs_folio_trace_cancel_copy,	"cancel-copy")	\
	EM(netfs_folio_trace_cancel_store,	"cancel-store")	\
	EM(netfs_folio_trace_clear,		"clear")	\
	EM(netfs_folio_trace_clear_cc,		"clear-cc")	\
	EM(netfs_folio_trace_clear_g,		"clear-g")	\
	EM(netfs_folio_trace_clear_s,		"clear-s")	\
	EM(netfs_folio_trace_copy_to_cache,	"mark-copy")	\
	EM(netfs_folio_trace_end_copy,		"end-copy")	\
	EM(netfs_folio_trace_filled_gaps,	"filled-gaps")	\
	EM(netfs_folio_trace_kill,		"kill")		\
	EM(netfs_folio_trace_kill_cc,		"kill-cc")	\
	EM(netfs_folio_trace_kill_g,		"kill-g")	\
	EM(netfs_folio_trace_kill_s,		"kill-s")	\
	EM(netfs_folio_trace_mkwrite,		"mkwrite")	\
	EM(netfs_folio_trace_mkwrite_plus,	"mkwrite+")	\
	EM(netfs_folio_trace_not_under_wback,	"!wback")	\
	EM(netfs_folio_trace_not_locked,	"!locked")	\
	EM(netfs_folio_trace_put,		"put")		\
	EM(netfs_folio_trace_read,		"read")		\
	EM(netfs_folio_trace_read_done,		"read-done")	\
	EM(netfs_folio_trace_read_gaps,		"read-gaps")	\
	EM(netfs_folio_trace_read_unlock,	"read-unlock")	\
	EM(netfs_folio_trace_redirtied,		"redirtied")	\
	EM(netfs_folio_trace_store,		"store")	\
	EM(netfs_folio_trace_store_copy,	"store-copy")	\
	EM(netfs_folio_trace_store_plus,	"store+")	\
	EM(netfs_folio_trace_wthru,		"wthru")	\
	E_(netfs_folio_trace_wthru_plus,	"wthru+")

#define netfs_collect_contig_traces				\
	EM(netfs_contig_trace_collect,		"Collect")	\
	EM(netfs_contig_trace_jump,		"-->JUMP-->")	\
	E_(netfs_contig_trace_unlock,		"Unlock")

#define netfs_donate_traces					\
	EM(netfs_trace_donate_tail_to_prev,	"tail-to-prev")	\
	EM(netfs_trace_donate_to_prev,		"to-prev")	\
	EM(netfs_trace_donate_to_next,		"to-next")	\
	E_(netfs_trace_donate_to_deferred_next,	"defer-next")

#define netfs_folioq_traces					\
	EM(netfs_trace_folioq_alloc_buffer,	"alloc-buf")	\
	EM(netfs_trace_folioq_clear,		"clear")	\
	EM(netfs_trace_folioq_delete,		"delete")	\
	EM(netfs_trace_folioq_make_space,	"make-space")	\
	EM(netfs_trace_folioq_rollbuf_init,	"roll-init")	\
	E_(netfs_trace_folioq_read_progress,	"r-progress")

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
enum netfs_collect_contig_trace { netfs_collect_contig_traces } __mode(byte);
enum netfs_donate_trace { netfs_donate_traces } __mode(byte);
enum netfs_folioq_trace { netfs_folioq_traces } __mode(byte);

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
netfs_collect_contig_traces;
netfs_donate_traces;
netfs_folioq_traces;

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
		    __field(unsigned int,		rreq)
		    __field(unsigned int,		cookie)
		    __field(loff_t,			i_size)
		    __field(loff_t,			start)
		    __field(size_t,			len)
		    __field(enum netfs_read_trace,	what)
		    __field(unsigned int,		netfs_inode)
			     ),

	    TP_fast_assign(
		    __entry->rreq	= rreq->debug_id;
		    __entry->cookie	= rreq->cache_resources.debug_id;
		    __entry->i_size	= rreq->i_size;
		    __entry->start	= start;
		    __entry->len	= len;
		    __entry->what	= what;
		    __entry->netfs_inode = rreq->inode->i_ino;
			   ),

	    TP_printk("R=%08x %s c=%08x ni=%x s=%llx l=%zx sz=%llx",
		      __entry->rreq,
		      __print_symbolic(__entry->what, netfs_read_traces),
		      __entry->cookie,
		      __entry->netfs_inode,
		      __entry->start, __entry->len, __entry->i_size)
	    );

TRACE_EVENT(netfs_rreq,
	    TP_PROTO(struct netfs_io_request *rreq,
		     enum netfs_rreq_trace what),

	    TP_ARGS(rreq, what),

	    TP_STRUCT__entry(
		    __field(unsigned int,		rreq)
		    __field(unsigned int,		flags)
		    __field(enum netfs_io_origin,	origin)
		    __field(enum netfs_rreq_trace,	what)
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
		    __field(unsigned int,		rreq)
		    __field(unsigned short,		index)
		    __field(short,			error)
		    __field(unsigned short,		flags)
		    __field(enum netfs_io_source,	source)
		    __field(enum netfs_sreq_trace,	what)
		    __field(u8,				slot)
		    __field(size_t,			len)
		    __field(size_t,			transferred)
		    __field(loff_t,			start)
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
		    __entry->slot	= sreq->io_iter.folioq_slot;
			   ),

	    TP_printk("R=%08x[%x] %s %s f=%02x s=%llx %zx/%zx s=%u e=%d",
		      __entry->rreq, __entry->index,
		      __print_symbolic(__entry->source, netfs_sreq_sources),
		      __print_symbolic(__entry->what, netfs_sreq_traces),
		      __entry->flags,
		      __entry->start, __entry->transferred, __entry->len,
		      __entry->slot, __entry->error)
	    );

TRACE_EVENT(netfs_failure,
	    TP_PROTO(struct netfs_io_request *rreq,
		     struct netfs_io_subrequest *sreq,
		     int error, enum netfs_failure what),

	    TP_ARGS(rreq, sreq, error, what),

	    TP_STRUCT__entry(
		    __field(unsigned int,		rreq)
		    __field(short,			index)
		    __field(short,			error)
		    __field(unsigned short,		flags)
		    __field(enum netfs_io_source,	source)
		    __field(enum netfs_failure,		what)
		    __field(size_t,			len)
		    __field(size_t,			transferred)
		    __field(loff_t,			start)
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

	    TP_printk("R=%08x[%x] %s f=%02x s=%llx %zx/%zx %s e=%d",
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
		    __field(unsigned int,		rreq)
		    __field(int,			ref)
		    __field(enum netfs_rreq_ref_trace,	what)
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
		    __field(unsigned int,		rreq)
		    __field(unsigned int,		subreq)
		    __field(int,			ref)
		    __field(enum netfs_sreq_ref_trace,	what)
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
		    struct address_space *__m = READ_ONCE(folio->mapping);
		    __entry->ino = __m ? __m->host->i_ino : 0;
		    __entry->why = why;
		    __entry->index = folio->index;
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
		    __field(unsigned long long,		start)
		    __field(size_t,			len)
		    __field(unsigned int,		flags)
		    __field(unsigned int,		ino)
			     ),

	    TP_fast_assign(
		    __entry->start	= iocb->ki_pos;
		    __entry->len	= iov_iter_count(from);
		    __entry->ino	= iocb->ki_filp->f_inode->i_ino;
		    __entry->flags	= iocb->ki_flags;
			   ),

	    TP_printk("WRITE-ITER i=%x s=%llx l=%zx f=%x",
		      __entry->ino, __entry->start, __entry->len, __entry->flags)
	    );

TRACE_EVENT(netfs_write,
	    TP_PROTO(const struct netfs_io_request *wreq,
		     enum netfs_write_trace what),

	    TP_ARGS(wreq, what),

	    TP_STRUCT__entry(
		    __field(unsigned int,		wreq)
		    __field(unsigned int,		cookie)
		    __field(unsigned int,		ino)
		    __field(enum netfs_write_trace,	what)
		    __field(unsigned long long,		start)
		    __field(unsigned long long,		len)
			     ),

	    TP_fast_assign(
		    struct netfs_inode *__ctx = netfs_inode(wreq->inode);
		    struct fscache_cookie *__cookie = netfs_i_cookie(__ctx);
		    __entry->wreq	= wreq->debug_id;
		    __entry->cookie	= __cookie ? __cookie->debug_id : 0;
		    __entry->ino	= wreq->inode->i_ino;
		    __entry->what	= what;
		    __entry->start	= wreq->start;
		    __entry->len	= wreq->len;
			   ),

	    TP_printk("R=%08x %s c=%08x i=%x by=%llx-%llx",
		      __entry->wreq,
		      __print_symbolic(__entry->what, netfs_write_traces),
		      __entry->cookie,
		      __entry->ino,
		      __entry->start, __entry->start + __entry->len - 1)
	    );

TRACE_EVENT(netfs_collect,
	    TP_PROTO(const struct netfs_io_request *wreq),

	    TP_ARGS(wreq),

	    TP_STRUCT__entry(
		    __field(unsigned int,		wreq)
		    __field(unsigned int,		len)
		    __field(unsigned long long,		transferred)
		    __field(unsigned long long,		start)
			     ),

	    TP_fast_assign(
		    __entry->wreq	= wreq->debug_id;
		    __entry->start	= wreq->start;
		    __entry->len	= wreq->len;
		    __entry->transferred = wreq->transferred;
			   ),

	    TP_printk("R=%08x s=%llx-%llx",
		      __entry->wreq,
		      __entry->start + __entry->transferred,
		      __entry->start + __entry->len)
	    );

TRACE_EVENT(netfs_collect_sreq,
	    TP_PROTO(const struct netfs_io_request *wreq,
		     const struct netfs_io_subrequest *subreq),

	    TP_ARGS(wreq, subreq),

	    TP_STRUCT__entry(
		    __field(unsigned int,		wreq)
		    __field(unsigned int,		subreq)
		    __field(unsigned int,		stream)
		    __field(unsigned int,		len)
		    __field(unsigned int,		transferred)
		    __field(unsigned long long,		start)
			     ),

	    TP_fast_assign(
		    __entry->wreq	= wreq->debug_id;
		    __entry->subreq	= subreq->debug_index;
		    __entry->stream	= subreq->stream_nr;
		    __entry->start	= subreq->start;
		    __entry->len	= subreq->len;
		    __entry->transferred = subreq->transferred;
			   ),

	    TP_printk("R=%08x[%u:%02x] s=%llx t=%x/%x",
		      __entry->wreq, __entry->stream, __entry->subreq,
		      __entry->start, __entry->transferred, __entry->len)
	    );

TRACE_EVENT(netfs_collect_folio,
	    TP_PROTO(const struct netfs_io_request *wreq,
		     const struct folio *folio,
		     unsigned long long fend,
		     unsigned long long collected_to),

	    TP_ARGS(wreq, folio, fend, collected_to),

	    TP_STRUCT__entry(
		    __field(unsigned int,	wreq)
		    __field(unsigned long,	index)
		    __field(unsigned long long,	fend)
		    __field(unsigned long long,	cleaned_to)
		    __field(unsigned long long,	collected_to)
			     ),

	    TP_fast_assign(
		    __entry->wreq	= wreq->debug_id;
		    __entry->index	= folio->index;
		    __entry->fend	= fend;
		    __entry->cleaned_to	= wreq->cleaned_to;
		    __entry->collected_to = collected_to;
			   ),

	    TP_printk("R=%08x ix=%05lx r=%llx-%llx t=%llx/%llx",
		      __entry->wreq, __entry->index,
		      (unsigned long long)__entry->index * PAGE_SIZE, __entry->fend,
		      __entry->cleaned_to, __entry->collected_to)
	    );

TRACE_EVENT(netfs_collect_state,
	    TP_PROTO(const struct netfs_io_request *wreq,
		     unsigned long long collected_to,
		     unsigned int notes),

	    TP_ARGS(wreq, collected_to, notes),

	    TP_STRUCT__entry(
		    __field(unsigned int,	wreq)
		    __field(unsigned int,	notes)
		    __field(unsigned long long,	collected_to)
		    __field(unsigned long long,	cleaned_to)
			     ),

	    TP_fast_assign(
		    __entry->wreq	= wreq->debug_id;
		    __entry->notes	= notes;
		    __entry->collected_to = collected_to;
		    __entry->cleaned_to	= wreq->cleaned_to;
			   ),

	    TP_printk("R=%08x col=%llx cln=%llx n=%x",
		      __entry->wreq, __entry->collected_to,
		      __entry->cleaned_to,
		      __entry->notes)
	    );

TRACE_EVENT(netfs_collect_gap,
	    TP_PROTO(const struct netfs_io_request *wreq,
		     const struct netfs_io_stream *stream,
		     unsigned long long jump_to, char type),

	    TP_ARGS(wreq, stream, jump_to, type),

	    TP_STRUCT__entry(
		    __field(unsigned int,	wreq)
		    __field(unsigned char,	stream)
		    __field(unsigned char,	type)
		    __field(unsigned long long,	from)
		    __field(unsigned long long,	to)
			     ),

	    TP_fast_assign(
		    __entry->wreq	= wreq->debug_id;
		    __entry->stream	= stream->stream_nr;
		    __entry->from	= stream->collected_to;
		    __entry->to		= jump_to;
		    __entry->type	= type;
			   ),

	    TP_printk("R=%08x[%x:] %llx->%llx %c",
		      __entry->wreq, __entry->stream,
		      __entry->from, __entry->to, __entry->type)
	    );

TRACE_EVENT(netfs_collect_stream,
	    TP_PROTO(const struct netfs_io_request *wreq,
		     const struct netfs_io_stream *stream),

	    TP_ARGS(wreq, stream),

	    TP_STRUCT__entry(
		    __field(unsigned int,	wreq)
		    __field(unsigned char,	stream)
		    __field(unsigned long long,	collected_to)
		    __field(unsigned long long,	front)
			     ),

	    TP_fast_assign(
		    __entry->wreq	= wreq->debug_id;
		    __entry->stream	= stream->stream_nr;
		    __entry->collected_to = stream->collected_to;
		    __entry->front	= stream->front ? stream->front->start : UINT_MAX;
			   ),

	    TP_printk("R=%08x[%x:] cto=%llx frn=%llx",
		      __entry->wreq, __entry->stream,
		      __entry->collected_to, __entry->front)
	    );

TRACE_EVENT(netfs_folioq,
	    TP_PROTO(const struct folio_queue *fq,
		     enum netfs_folioq_trace trace),

	    TP_ARGS(fq, trace),

	    TP_STRUCT__entry(
		    __field(unsigned int,		rreq)
		    __field(unsigned int,		id)
		    __field(enum netfs_folioq_trace,	trace)
			     ),

	    TP_fast_assign(
		    __entry->rreq	= fq ? fq->rreq_id : 0;
		    __entry->id		= fq ? fq->debug_id : 0;
		    __entry->trace	= trace;
			   ),

	    TP_printk("R=%08x fq=%x %s",
		      __entry->rreq, __entry->id,
		      __print_symbolic(__entry->trace, netfs_folioq_traces))
	    );

#undef EM
#undef E_
#endif /* _TRACE_NETFS_H */

/* This part must be outside protection */
#include <trace/define_trace.h>
