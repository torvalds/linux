/* SPDX-License-Identifier: GPL-2.0 */
#undef TRACE_SYSTEM
#define TRACE_SYSTEM capability

#if !defined(_TRACE_CAPABILITY_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_CAPABILITY_H

#include <linux/cred.h>
#include <linux/tracepoint.h>
#include <linux/user_namespace.h>

/**
 * cap_capable - called after it's determined if a task has a particular
 * effective capability
 *
 * @cred: The credentials used
 * @target_ns: The user namespace of the resource being accessed
 * @capable_ns: The user namespace in which the credential provides the
 *              capability to access the targeted resource.
 *              This will be NULL if ret is not 0.
 * @cap: The capability to check for
 * @ret: The return value of the check: 0 if it does, -ve if it does not
 *
 * Allows to trace calls to cap_capable in commoncap.c
 */
TRACE_EVENT(cap_capable,

	TP_PROTO(const struct cred *cred, struct user_namespace *target_ns,
		const struct user_namespace *capable_ns, int cap, int ret),

	TP_ARGS(cred, target_ns, capable_ns, cap, ret),

	TP_STRUCT__entry(
		__field(const struct cred *, cred)
		__field(struct user_namespace *, target_ns)
		__field(const struct user_namespace *, capable_ns)
		__field(int, cap)
		__field(int, ret)
	),

	TP_fast_assign(
		__entry->cred       = cred;
		__entry->target_ns    = target_ns;
		__entry->capable_ns = ret == 0 ? capable_ns : NULL;
		__entry->cap        = cap;
		__entry->ret        = ret;
	),

	TP_printk("cred %p, target_ns %p, capable_ns %p, cap %d, ret %d",
		__entry->cred, __entry->target_ns, __entry->capable_ns, __entry->cap,
		__entry->ret)
);

#endif /* _TRACE_CAPABILITY_H */

/* This part must be outside protection */
#include <trace/define_trace.h>
