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
#define	TRACE_SYSTEM_VAR zfs_multilist

#if !defined(_TRACE_MULTILIST_H) || defined(TRACE_HEADER_MULTI_READ)
#define	_TRACE_MULTILIST_H

#include <linux/tracepoint.h>
#include <sys/types.h>

/*
 * Generic support for three argument tracepoints of the form:
 *
 * DTRACE_PROBE3(...,
 *     multilist_t *, ...,
 *     unsigned int, ...,
 *     void *, ...);
 */
/* BEGIN CSTYLED */
DECLARE_EVENT_CLASS(zfs_multilist_insert_remove_class,
	TP_PROTO(multilist_t *ml, unsigned sublist_idx, void *obj),
	TP_ARGS(ml, sublist_idx, obj),
	TP_STRUCT__entry(
	    __field(size_t,		ml_offset)
	    __field(uint64_t,		ml_num_sublists)

	    __field(unsigned int,	sublist_idx)
	),
	TP_fast_assign(
	    __entry->ml_offset		= ml->ml_offset;
	    __entry->ml_num_sublists	= ml->ml_num_sublists;

	    __entry->sublist_idx	= sublist_idx;
	),
	TP_printk("ml { offset %ld numsublists %llu sublistidx %u } ",
	    __entry->ml_offset, __entry->ml_num_sublists, __entry->sublist_idx)
);
/* END CSTYLED */

/* BEGIN CSTYLED */
#define	DEFINE_MULTILIST_INSERT_REMOVE_EVENT(name) \
DEFINE_EVENT(zfs_multilist_insert_remove_class, name, \
	TP_PROTO(multilist_t *ml, unsigned int sublist_idx, void *obj), \
	TP_ARGS(ml, sublist_idx, obj))
/* END CSTYLED */
DEFINE_MULTILIST_INSERT_REMOVE_EVENT(zfs_multilist__insert);
DEFINE_MULTILIST_INSERT_REMOVE_EVENT(zfs_multilist__remove);

#endif /* _TRACE_MULTILIST_H */

#undef TRACE_INCLUDE_PATH
#undef TRACE_INCLUDE_FILE
#define	TRACE_INCLUDE_PATH sys
#define	TRACE_INCLUDE_FILE trace_multilist
#include <trace/define_trace.h>

#endif /* _KERNEL && HAVE_DECLARE_EVENT_CLASS */
