/* SPDX-License-Identifier: GPL-2.0 */
#undef TRACE_SYSTEM
#define TRACE_SYSTEM creds

#define TRACE_INCLUDE_PATH trace/hooks
#if !defined(_TRACE_HOOK_CREDS_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_HOOK_CREDS_H
#include <trace/hooks/vendor_hooks.h>
/*
 * Following tracepoints are not exported in tracefs and provide a
 * mechanism for vendor modules to hook and extend functionality
 */
struct cred;
struct task_struct;
DECLARE_RESTRICTED_HOOK(android_rvh_commit_creds,
	TP_PROTO(const struct task_struct *task, const struct cred *new),
	TP_ARGS(task, new), 1);

DECLARE_RESTRICTED_HOOK(android_rvh_exit_creds,
	TP_PROTO(const struct task_struct *task, const struct cred *cred),
	TP_ARGS(task, cred), 1);

DECLARE_RESTRICTED_HOOK(android_rvh_override_creds,
	TP_PROTO(const struct task_struct *task, const struct cred *new),
	TP_ARGS(task, new), 1);

DECLARE_RESTRICTED_HOOK(android_rvh_revert_creds,
	TP_PROTO(const struct task_struct *task, const struct cred *old),
	TP_ARGS(task, old), 1);

#endif /* _TRACE_HOOK_CREDS_H */
/* This part must be outside protection */
#include <trace/define_trace.h>
