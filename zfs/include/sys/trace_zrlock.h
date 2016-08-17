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
#define	TRACE_SYSTEM_VAR zfs_zrlock

#if !defined(_TRACE_ZRLOCK_H) || defined(TRACE_HEADER_MULTI_READ)
#define	_TRACE_ZRLOCK_H

#include <linux/tracepoint.h>
#include <sys/types.h>

/*
 * Generic support for two argument tracepoints of the form:
 *
 * DTRACE_PROBE2(...,
 *     zrlock_t *, ...,
 *     uint32_t, ...);
 */

DECLARE_EVENT_CLASS(zfs_zrlock_class,
	TP_PROTO(zrlock_t *zrl, uint32_t n),
	TP_ARGS(zrl, n),
	TP_STRUCT__entry(
	    __field(int32_t,		refcount)
#ifdef	ZFS_DEBUG
	    __field(pid_t,		owner_pid)
	    __field(const char *,	caller)
#endif
	    __field(uint32_t,		n)
	),
	TP_fast_assign(
	    __entry->refcount	= zrl->zr_refcount;
#ifdef	ZFS_DEBUG
	    __entry->owner_pid	= zrl->zr_owner ? zrl->zr_owner->pid : 0;
	    __entry->caller	= zrl->zr_caller;
#endif
	    __entry->n		= n;
	),
#ifdef	ZFS_DEBUG
	TP_printk("zrl { refcount %d owner_pid %d caller %s } n %u",
	    __entry->refcount, __entry->owner_pid, __entry->caller,
	    __entry->n)
#else
	TP_printk("zrl { refcount %d } n %u",
	    __entry->refcount, __entry->n)
#endif
);

#define	DEFINE_ZRLOCK_EVENT(name) \
DEFINE_EVENT(zfs_zrlock_class, name, \
	TP_PROTO(zrlock_t *zrl, uint32_t n), \
	TP_ARGS(zrl, n))
DEFINE_ZRLOCK_EVENT(zfs_zrlock__reentry);

#endif /* _TRACE_ZRLOCK_H */

#undef TRACE_INCLUDE_PATH
#undef TRACE_INCLUDE_FILE
#define	TRACE_INCLUDE_PATH sys
#define	TRACE_INCLUDE_FILE trace_zrlock
#include <trace/define_trace.h>

#endif /* _KERNEL && HAVE_DECLARE_EVENT_CLASS */
