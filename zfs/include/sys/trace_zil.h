/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License (the "License").
 * You may not use this file except in compliance with the License.
 *
 * You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
 * or http://www.opensolaris.org/os/licensing.
 * See the License for the specific language governing permissions
 * and limitations under the License.
 *
 * When distributing Covered Code, include this CDDL HEADER in each
 * file and include the License file at usr/src/OPENSOLARIS.LICENSE.
 * If applicable, add the following below this CDDL HEADER, with the
 * fields enclosed by brackets "[]" replaced with your own identifying
 * information: Portions Copyright [yyyy] [name of copyright owner]
 *
 * CDDL HEADER END
 */

#if defined(_KERNEL) && defined(HAVE_DECLARE_EVENT_CLASS)

#undef TRACE_SYSTEM
#define	TRACE_SYSTEM zfs

#undef TRACE_SYSTEM_VAR
#define	TRACE_SYSTEM_VAR zfs_zil

#if !defined(_TRACE_ZIL_H) || defined(TRACE_HEADER_MULTI_READ)
#define	_TRACE_ZIL_H

#include <linux/tracepoint.h>
#include <sys/types.h>

/*
 * Generic support for one argument tracepoints of the form:
 *
 * DTRACE_PROBE1(...,
 *     zilog_t *, ...);
 */
/* BEGIN CSTYLED */
DECLARE_EVENT_CLASS(zfs_zil_class,
	TP_PROTO(zilog_t *zilog),
	TP_ARGS(zilog),
	TP_STRUCT__entry(
	    __field(uint64_t,	zl_lr_seq)
	    __field(uint64_t,	zl_commit_lr_seq)
	    __field(uint64_t,	zl_destroy_txg)
	    __field(uint64_t,	zl_replaying_seq)
	    __field(uint32_t,	zl_suspend)
	    __field(uint8_t,	zl_suspending)
	    __field(uint8_t,	zl_keep_first)
	    __field(uint8_t,	zl_replay)
	    __field(uint8_t,	zl_stop_sync)
	    __field(uint8_t,	zl_writer)
	    __field(uint8_t,	zl_logbias)
	    __field(uint8_t,	zl_sync)
	    __field(int,	zl_parse_error)
	    __field(uint64_t,	zl_parse_blk_seq)
	    __field(uint64_t,	zl_parse_lr_seq)
	    __field(uint64_t,	zl_parse_blk_count)
	    __field(uint64_t,	zl_parse_lr_count)
	    __field(uint64_t,	zl_next_batch)
	    __field(uint64_t,	zl_com_batch)
	    __field(uint64_t,	zl_cur_used)
	    __field(clock_t,	zl_replay_time)
	    __field(uint64_t,	zl_replay_blks)
	),
	TP_fast_assign(
	    __entry->zl_lr_seq		= zilog->zl_lr_seq;
	    __entry->zl_commit_lr_seq	= zilog->zl_commit_lr_seq;
	    __entry->zl_destroy_txg	= zilog->zl_destroy_txg;
	    __entry->zl_replaying_seq	= zilog->zl_replaying_seq;
	    __entry->zl_suspend		= zilog->zl_suspend;
	    __entry->zl_suspending	= zilog->zl_suspending;
	    __entry->zl_keep_first	= zilog->zl_keep_first;
	    __entry->zl_replay		= zilog->zl_replay;
	    __entry->zl_stop_sync	= zilog->zl_stop_sync;
	    __entry->zl_writer		= zilog->zl_writer;
	    __entry->zl_logbias		= zilog->zl_logbias;
	    __entry->zl_sync		= zilog->zl_sync;
	    __entry->zl_parse_error	= zilog->zl_parse_error;
	    __entry->zl_parse_blk_seq	= zilog->zl_parse_blk_seq;
	    __entry->zl_parse_lr_seq	= zilog->zl_parse_lr_seq;
	    __entry->zl_parse_blk_count	= zilog->zl_parse_blk_count;
	    __entry->zl_parse_lr_count	= zilog->zl_parse_lr_count;
	    __entry->zl_next_batch	= zilog->zl_next_batch;
	    __entry->zl_com_batch	= zilog->zl_com_batch;
	    __entry->zl_cur_used	= zilog->zl_cur_used;
	    __entry->zl_replay_time	= zilog->zl_replay_time;
	    __entry->zl_replay_blks	= zilog->zl_replay_blks;
	),
	TP_printk("zl { lr_seq %llu commit_lr_seq %llu destroy_txg %llu "
	    "replaying_seq %llu suspend %u suspending %u keep_first %u "
	    "replay %u stop_sync %u writer %u logbias %u sync %u "
	    "parse_error %u parse_blk_seq %llu parse_lr_seq %llu "
	    "parse_blk_count %llu parse_lr_count %llu next_batch %llu "
	    "com_batch %llu cur_used %llu replay_time %lu replay_blks %llu }",
	    __entry->zl_lr_seq, __entry->zl_commit_lr_seq,
	    __entry->zl_destroy_txg, __entry->zl_replaying_seq,
	    __entry->zl_suspend, __entry->zl_suspending, __entry->zl_keep_first,
	    __entry->zl_replay, __entry->zl_stop_sync, __entry->zl_writer,
	    __entry->zl_logbias, __entry->zl_sync, __entry->zl_parse_error,
	    __entry->zl_parse_blk_seq, __entry->zl_parse_lr_seq,
	    __entry->zl_parse_blk_count, __entry->zl_parse_lr_count,
	    __entry->zl_next_batch, __entry->zl_com_batch, __entry->zl_cur_used,
	    __entry->zl_replay_time, __entry->zl_replay_blks)
);
/* END CSTYLED */

/* BEGIN CSTYLED */
#define	DEFINE_ZIL_EVENT(name) \
DEFINE_EVENT(zfs_zil_class, name, \
	TP_PROTO(zilog_t *zilog), \
	TP_ARGS(zilog))
DEFINE_ZIL_EVENT(zfs_zil__cw1);
DEFINE_ZIL_EVENT(zfs_zil__cw2);
/* END CSTYLED */

#endif /* _TRACE_ZIL_H */

#undef TRACE_INCLUDE_PATH
#undef TRACE_INCLUDE_FILE
#define	TRACE_INCLUDE_PATH sys
#define	TRACE_INCLUDE_FILE trace_zil
#include <trace/define_trace.h>

#endif /* _KERNEL && HAVE_DECLARE_EVENT_CLASS */
