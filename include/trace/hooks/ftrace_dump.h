/* SPDX-License-Identifier: GPL-2.0 */
#undef TRACE_SYSTEM
#define TRACE_SYSTEM ftrace_dump

#define TRACE_INCLUDE_PATH trace/hooks

#if !defined(_TRACE_HOOK_FTRACE_DUMP_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_HOOK_FTRACE_DUMP_H

#include <linux/trace_seq.h>
#include <linux/trace_events.h>

#include <trace/hooks/vendor_hooks.h>

#if defined(CONFIG_TRACEPOINTS) && defined(CONFIG_ANDROID_VENDOR_HOOKS)

DECLARE_HOOK(android_vh_ftrace_oops_enter,
	TP_PROTO(bool *ftrace_check),
	TP_ARGS(ftrace_check));

DECLARE_HOOK(android_vh_ftrace_oops_exit,
	TP_PROTO(bool *ftrace_check),
	TP_ARGS(ftrace_check));

DECLARE_HOOK(android_vh_ftrace_size_check,
	TP_PROTO(unsigned long size, bool *ftrace_check),
	TP_ARGS(size, ftrace_check));

DECLARE_HOOK(android_vh_ftrace_format_check,
	TP_PROTO(bool *ftrace_check),
	TP_ARGS(ftrace_check));

DECLARE_HOOK(android_vh_ftrace_dump_buffer,
	TP_PROTO(struct trace_seq *trace_buf, bool *dump_printk),
	TP_ARGS(trace_buf, dump_printk));

#else

#define trace_android_vh_ftrace_oops_enter(ftrace_check)
#define trace_android_vh_ftrace_oops_exit(ftrace_check)
#define trace_android_vh_ftrace_size_check(size, ftrace_check)
#define trace_android_vh_ftrace_format_check(ftrace_check)
#define trace_android_vh_ftrace_dump_buffer(trace_buf, dump_printk)

#endif

#endif /* _TRACE_HOOK_FTRACE_DUMP_H */
/* This part must be outside protection */
#include <trace/define_trace.h>
