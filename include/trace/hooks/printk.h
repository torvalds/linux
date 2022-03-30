/* SPDX-License-Identifier: GPL-2.0 */
#undef TRACE_SYSTEM
#define TRACE_SYSTEM printk

#define TRACE_INCLUDE_PATH trace/hooks

#if !defined(_TRACE_HOOK_PRINTK_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_HOOK_PRINTK_H

#include <trace/hooks/vendor_hooks.h>

DECLARE_HOOK(android_vh_printk_hotplug,
	TP_PROTO(int *flag),
	TP_ARGS(flag));

#endif /* _TRACE_HOOK_PRINTK_H */
/* This part must be outside protection */
#include <trace/define_trace.h>
