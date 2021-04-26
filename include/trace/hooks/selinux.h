/* SPDX-License-Identifier: GPL-2.0 */
#undef TRACE_SYSTEM
#define TRACE_SYSTEM selinux

#define TRACE_INCLUDE_PATH trace/hooks
#if !defined(_TRACE_HOOK_SELINUX_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_HOOK_SELINUX_H
#include <linux/tracepoint.h>
#include <trace/hooks/vendor_hooks.h>
/*
 * Following tracepoints are not exported in tracefs and provide a
 * mechanism for vendor modules to hook and extend functionality
 */
struct selinux_state;
DECLARE_HOOK(android_vh_selinux_is_initialized,
	TP_PROTO(const struct selinux_state *state),
	TP_ARGS(state));

#endif /* _TRACE_HOOK_SELINUX_H */
/* This part must be outside protection */
#include <trace/define_trace.h>
