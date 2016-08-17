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
#define	TRACE_SYSTEM_VAR zfs_txg

#if !defined(_TRACE_TXG_H) || defined(TRACE_HEADER_MULTI_READ)
#define	_TRACE_TXG_H

#include <linux/tracepoint.h>
#include <sys/types.h>

/*
 * Generic support for two argument tracepoints of the form:
 *
 * DTRACE_PROBE2(...,
 *     dsl_pool_t *, ...,
 *     uint64_t, ...);
 */

DECLARE_EVENT_CLASS(zfs_txg_class,
	TP_PROTO(dsl_pool_t *dp, uint64_t txg),
	TP_ARGS(dp, txg),
	TP_STRUCT__entry(
	    __field(uint64_t, txg)
	),
	TP_fast_assign(
	    __entry->txg = txg;
	),
	TP_printk("txg %llu", __entry->txg)
);

#define	DEFINE_TXG_EVENT(name) \
DEFINE_EVENT(zfs_txg_class, name, \
	TP_PROTO(dsl_pool_t *dp, uint64_t txg), \
	TP_ARGS(dp, txg))
DEFINE_TXG_EVENT(zfs_dsl_pool_sync__done);
DEFINE_TXG_EVENT(zfs_txg__quiescing);
DEFINE_TXG_EVENT(zfs_txg__opened);
DEFINE_TXG_EVENT(zfs_txg__syncing);
DEFINE_TXG_EVENT(zfs_txg__synced);
DEFINE_TXG_EVENT(zfs_txg__quiesced);

#endif /* _TRACE_TXG_H */

#undef TRACE_INCLUDE_PATH
#undef TRACE_INCLUDE_FILE
#define	TRACE_INCLUDE_PATH sys
#define	TRACE_INCLUDE_FILE trace_txg
#include <trace/define_trace.h>

#endif /* _KERNEL && HAVE_DECLARE_EVENT_CLASS */
