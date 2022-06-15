/* SPDX-License-Identifier: GPL-2.0 */
#undef TRACE_SYSTEM
#define TRACE_SYSTEM power
#define TRACE_INCLUDE_PATH trace/hooks

#if !defined(_TRACE_HOOK_POWER_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_HOOK_POWER_H
#include <linux/tracepoint.h>
#include <trace/hooks/vendor_hooks.h>
/*
 * Following tracepoints are not exported in tracefs and provide a
 * mechanism for vendor modules to hook and extend functionality
 */
#ifdef __GENKSYMS__
enum freq_qos_req_type;
struct freq_constraints;
struct freq_qos_request;
struct task_struct;
#else
/* enum freq_qos_req_type, struct freq_constraints, struct freq_qos_request */
#include <linux/pm_qos.h>
/* struct task_struct */
#include <linux/sched.h>
#endif /* __GENKSYMS__ */
DECLARE_HOOK(android_vh_try_to_freeze_todo,
	TP_PROTO(unsigned int todo, unsigned int elapsed_msecs, bool wq_busy),
	TP_ARGS(todo, elapsed_msecs, wq_busy));

DECLARE_HOOK(android_vh_try_to_freeze_todo_unfrozen,
	TP_PROTO(struct task_struct *p),
	TP_ARGS(p));


DECLARE_HOOK(android_vh_freq_qos_add_request,
	TP_PROTO(struct freq_constraints *qos, struct freq_qos_request *req,
		enum freq_qos_req_type type, int value, int ret),
	TP_ARGS(qos, req, type, value, ret));

DECLARE_HOOK(android_vh_freq_qos_update_request,
		TP_PROTO(struct freq_qos_request *req, int value),
		TP_ARGS(req, value));

DECLARE_HOOK(android_vh_freq_qos_remove_request,
		TP_PROTO(struct freq_qos_request *req),
		TP_ARGS(req));

/* macro versions of hooks are no longer required */

#endif /* _TRACE_HOOK_POWER_H */
/* This part must be outside protection */
#include <trace/define_trace.h>
