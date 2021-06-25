/* SPDX-License-Identifier: GPL-2.0 */
#undef TRACE_SYSTEM
#define TRACE_SYSTEM user
#undef TRACE_INCLUDE_PATH
#define TRACE_INCLUDE_PATH trace/hooks
#if !defined(_TRACE_HOOK_USER_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_HOOK_USER_H
#include <linux/tracepoint.h>
#include <trace/hooks/vendor_hooks.h>

struct user_struct;
DECLARE_HOOK(android_vh_alloc_uid,
	TP_PROTO(struct user_struct *user),
	TP_ARGS(user));

DECLARE_HOOK(android_vh_free_user,
	TP_PROTO(struct user_struct *up),
	TP_ARGS(up));

#endif /* _TRACE_HOOK_USER_H */
/* This part must be outside protection */
#include <trace/define_trace.h>

