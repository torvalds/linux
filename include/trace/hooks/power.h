/* SPDX-License-Identifier: GPL-2.0 */
#undef TRACE_SYSTEM
#define TRACE_SYSTEM power
#define TRACE_INCLUDE_PATH trace/hooks

#if !defined(_TRACE_HOOK_POWER_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_HOOK_POWER_H
#include <trace/hooks/vendor_hooks.h>

struct task_struct;
DECLARE_HOOK(android_vh_try_to_freeze_todo,
	TP_PROTO(unsigned int todo, unsigned int elapsed_msecs, bool wq_busy),
	TP_ARGS(todo, elapsed_msecs, wq_busy));

DECLARE_HOOK(android_vh_try_to_freeze_todo_unfrozen,
	TP_PROTO(struct task_struct *p),
	TP_ARGS(p));

enum freq_qos_req_type;
struct freq_qos_request;
struct freq_constraints;

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

DECLARE_HOOK(android_vh_hibernate_state,
		TP_PROTO(int error),
		TP_ARGS(error));

#endif /* _TRACE_HOOK_POWER_H */
/* This part must be outside protection */
#include <trace/define_trace.h>
