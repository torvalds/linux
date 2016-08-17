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
#define	TRACE_SYSTEM_VAR zfs_dmu

#if !defined(_TRACE_DMU_H) || defined(TRACE_HEADER_MULTI_READ)
#define	_TRACE_DMU_H

#include <linux/tracepoint.h>
#include <sys/types.h>

/*
 * Generic support for three argument tracepoints of the form:
 *
 * DTRACE_PROBE3(...,
 *     dmu_tx_t *, ...,
 *     uint64_t, ...,
 *     uint64_t, ...);
 */

DECLARE_EVENT_CLASS(zfs_delay_mintime_class,
	TP_PROTO(dmu_tx_t *tx, uint64_t dirty, uint64_t min_tx_time),
	TP_ARGS(tx, dirty, min_tx_time),
	TP_STRUCT__entry(
	    __field(uint64_t,			tx_txg)
	    __field(uint64_t,			tx_lastsnap_txg)
	    __field(uint64_t,			tx_lasttried_txg)
	    __field(boolean_t,			tx_anyobj)
	    __field(boolean_t,			tx_waited)
	    __field(hrtime_t,			tx_start)
	    __field(boolean_t,			tx_wait_dirty)
	    __field(int,			tx_err)
#ifdef DEBUG_DMU_TX
	    __field(uint64_t,			tx_space_towrite)
	    __field(uint64_t,			tx_space_tofree)
	    __field(uint64_t,			tx_space_tooverwrite)
	    __field(uint64_t,			tx_space_tounref)
	    __field(int64_t,			tx_space_written)
	    __field(int64_t,			tx_space_freed)
#endif
	    __field(uint64_t,			min_tx_time)
	    __field(uint64_t,			dirty)
	),
	TP_fast_assign(
	    __entry->tx_txg			= tx->tx_txg;
	    __entry->tx_lastsnap_txg		= tx->tx_lastsnap_txg;
	    __entry->tx_lasttried_txg		= tx->tx_lasttried_txg;
	    __entry->tx_anyobj			= tx->tx_anyobj;
	    __entry->tx_waited			= tx->tx_waited;
	    __entry->tx_start			= tx->tx_start;
	    __entry->tx_wait_dirty		= tx->tx_wait_dirty;
	    __entry->tx_err			= tx->tx_err;
#ifdef DEBUG_DMU_TX
	    __entry->tx_space_towrite		= tx->tx_space_towrite;
	    __entry->tx_space_tofree		= tx->tx_space_tofree;
	    __entry->tx_space_tooverwrite	= tx->tx_space_tooverwrite;
	    __entry->tx_space_tounref		= tx->tx_space_tounref;
	    __entry->tx_space_written		= tx->tx_space_written.rc_count;
	    __entry->tx_space_freed		= tx->tx_space_freed.rc_count;
#endif
	    __entry->dirty			= dirty;
	    __entry->min_tx_time		= min_tx_time;
	),
	TP_printk("tx { txg %llu lastsnap_txg %llu tx_lasttried_txg %llu "
	    "anyobj %d waited %d start %llu wait_dirty %d err %i "
#ifdef DEBUG_DMU_TX
	    "space_towrite %llu space_tofree %llu space_tooverwrite %llu "
	    "space_tounref %llu space_written %lli space_freed %lli "
#endif
	    "} dirty %llu min_tx_time %llu",
	    __entry->tx_txg, __entry->tx_lastsnap_txg,
	    __entry->tx_lasttried_txg, __entry->tx_anyobj, __entry->tx_waited,
	    __entry->tx_start, __entry->tx_wait_dirty, __entry->tx_err,
#ifdef DEBUG_DMU_TX
	    __entry->tx_space_towrite, __entry->tx_space_tofree,
	    __entry->tx_space_tooverwrite, __entry->tx_space_tounref,
	    __entry->tx_space_written, __entry->tx_space_freed,
#endif
	    __entry->dirty, __entry->min_tx_time)
);

#define	DEFINE_DELAY_MINTIME_EVENT(name) \
DEFINE_EVENT(zfs_delay_mintime_class, name, \
	TP_PROTO(dmu_tx_t *tx, uint64_t dirty, uint64_t min_tx_time), \
	TP_ARGS(tx, dirty, min_tx_time))
DEFINE_DELAY_MINTIME_EVENT(zfs_delay__mintime);

#endif /* _TRACE_DMU_H */

#undef TRACE_INCLUDE_PATH
#undef TRACE_INCLUDE_FILE
#define	TRACE_INCLUDE_PATH sys
#define	TRACE_INCLUDE_FILE trace_dmu
#include <trace/define_trace.h>

#endif /* _KERNEL && HAVE_DECLARE_EVENT_CLASS */
