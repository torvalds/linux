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

#include <sys/list.h>

#if defined(_KERNEL) && defined(HAVE_DECLARE_EVENT_CLASS)

#undef TRACE_SYSTEM
#define	TRACE_SYSTEM zfs

#undef TRACE_SYSTEM_VAR
#define	TRACE_SYSTEM_VAR zfs_zio

#if !defined(_TRACE_ZIO_H) || defined(TRACE_HEADER_MULTI_READ)
#define	_TRACE_ZIO_H

#include <linux/tracepoint.h>
#include <sys/types.h>
#include <sys/trace_common.h> /* For ZIO macros */

/* BEGIN CSTYLED */
TRACE_EVENT(zfs_zio__delay__miss,
	TP_PROTO(zio_t *zio, hrtime_t now),
	TP_ARGS(zio, now),
	TP_STRUCT__entry(
	    ZIO_TP_STRUCT_ENTRY
	    __field(hrtime_t, now)
	),
	TP_fast_assign(
	    ZIO_TP_FAST_ASSIGN
	    __entry->now = now;
	),
	TP_printk("now %llu " ZIO_TP_PRINTK_FMT, __entry->now,
	    ZIO_TP_PRINTK_ARGS)
);

TRACE_EVENT(zfs_zio__delay__hit,
	TP_PROTO(zio_t *zio, hrtime_t now, hrtime_t diff),
	TP_ARGS(zio, now, diff),
	TP_STRUCT__entry(
	    ZIO_TP_STRUCT_ENTRY
	    __field(hrtime_t, now)
	    __field(hrtime_t, diff)
	),
	TP_fast_assign(
	    ZIO_TP_FAST_ASSIGN
	    __entry->now = now;
	    __entry->diff = diff;
	),
	TP_printk("now %llu diff %llu " ZIO_TP_PRINTK_FMT, __entry->now,
	    __entry->diff, ZIO_TP_PRINTK_ARGS)
);

TRACE_EVENT(zfs_zio__delay__skip,
	TP_PROTO(zio_t *zio),
	TP_ARGS(zio),
	TP_STRUCT__entry(ZIO_TP_STRUCT_ENTRY),
	TP_fast_assign(ZIO_TP_FAST_ASSIGN),
	TP_printk(ZIO_TP_PRINTK_FMT, ZIO_TP_PRINTK_ARGS)
);
/* END CSTYLED */

#endif /* _TRACE_ZIO_H */

#undef TRACE_INCLUDE_PATH
#undef TRACE_INCLUDE_FILE
#define	TRACE_INCLUDE_PATH sys
#define	TRACE_INCLUDE_FILE trace_zio
#include <trace/define_trace.h>

#endif /* _KERNEL && HAVE_DECLARE_EVENT_CLASS */
