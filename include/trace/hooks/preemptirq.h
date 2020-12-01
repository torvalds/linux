/* SPDX-License-Identifier: GPL-2.0 */
#undef TRACE_SYSTEM
#define TRACE_SYSTEM preemptirq

#undef TRACE_INCLUDE_PATH
#define TRACE_INCLUDE_PATH trace/hooks

#if !defined(_TRACE_HOOK_PREEMPTIRQ_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_HOOK_PREEMPTIRQ_H

#include <linux/tracepoint.h>
#include <trace/hooks/vendor_hooks.h>

#if defined(CONFIG_TRACEPOINTS) && defined(CONFIG_ANDROID_VENDOR_HOOKS)

DECLARE_RESTRICTED_HOOK(android_rvh_preempt_disable,
	TP_PROTO(unsigned long ip, unsigned long parent_ip),
	TP_ARGS(ip, parent_ip), 1);

DECLARE_RESTRICTED_HOOK(android_rvh_preempt_enable,
	TP_PROTO(unsigned long ip, unsigned long parent_ip),
	TP_ARGS(ip, parent_ip), 1);

DECLARE_RESTRICTED_HOOK(android_rvh_irqs_disable,
	TP_PROTO(unsigned long ip, unsigned long parent_ip),
	TP_ARGS(ip, parent_ip), 1);

DECLARE_RESTRICTED_HOOK(android_rvh_irqs_enable,
	TP_PROTO(unsigned long ip, unsigned long parent_ip),
	TP_ARGS(ip, parent_ip), 1);

#else

#define trace_android_rvh_preempt_disable(ip, parent_ip)
#define trace_android_rvh_preempt_enable(ip, parent_ip)
#define trace_android_rvh_irqs_disable(ip, parent_ip)
#define trace_android_rvh_irqs_enable(ip, parent_ip)

#endif

#endif /* _TRACE_HOOK_PREEMPTIRQ_H */
/* This part must be outside protection */
#include <trace/define_trace.h>
