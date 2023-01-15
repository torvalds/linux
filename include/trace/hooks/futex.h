/* SPDX-License-Identifier: GPL-2.0 */
#undef TRACE_SYSTEM
#define TRACE_SYSTEM futex
#undef TRACE_INCLUDE_PATH
#define TRACE_INCLUDE_PATH trace/hooks
#if !defined(_TRACE_HOOK_FUTEX_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_HOOK_FUTEX_H
#include <linux/tracepoint.h>
#include <trace/hooks/vendor_hooks.h>
#include <linux/plist.h>
/*
 * Following tracepoints are not exported in tracefs and provide a
 * mechanism for vendor modules to hook and extend functionality
 */
#if defined(CONFIG_TRACEPOINTS) && defined(CONFIG_ANDROID_VENDOR_HOOKS)
DECLARE_HOOK(android_vh_alter_futex_plist_add,
	TP_PROTO(struct plist_node *node,
		 struct plist_head *head,
		 bool *already_on_hb),
	TP_ARGS(node, head, already_on_hb));
#else
#define trace_android_vh_alter_futex_plist_add(node, head, already_on_hb)
#endif
#endif /* _TRACE_HOOK_FUTEX_H */
/* This part must be outside protection */
#include <trace/define_trace.h>
