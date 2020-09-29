/* SPDX-License-Identifier: GPL-2.0 */
#undef TRACE_SYSTEM
#define TRACE_SYSTEM wqlockup
#define TRACE_INCLUDE_PATH trace/hooks

#if !defined(_TRACE_HOOK_WQLOCKUP_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_HOOK_WQLOCKUP_H
#include <linux/tracepoint.h>
#include <trace/hooks/vendor_hooks.h>
/*
 * Following tracepoints are not exported in tracefs and provide a
 * mechanism for vendor modules to hook and extend functionality
 */
#if defined(CONFIG_TRACEPOINTS) && defined(CONFIG_ANDROID_VENDOR_HOOKS)
DECLARE_HOOK(android_vh_wq_lockup_pool,
	TP_PROTO(int cpu, unsigned long pool_ts),
	TP_ARGS(cpu, pool_ts));
#else
#define trace_android_vh_wq_lockup_pool(cpu, pool_ts)
#endif

#endif /* _TRACE_HOOK_WQLOCKUP_H */
/* This part must be outside protection */
#include <trace/define_trace.h>
