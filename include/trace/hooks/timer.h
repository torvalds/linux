/* SPDX-License-Identifier: GPL-2.0 */
#undef TRACE_SYSTEM
#define TRACE_SYSTEM timer

#define TRACE_INCLUDE_PATH trace/hooks

#if !defined(_TRACE_HOOK_TIMER_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_HOOK_TIMER_H

#include <trace/hooks/vendor_hooks.h>

DECLARE_HOOK(android_vh_timer_calc_index,
	TP_PROTO(unsigned int lvl, unsigned long *expires),
	TP_ARGS(lvl, expires));

#endif /* _TRACE_HOOK_TIMER_H */
/* This part must be outside protection */
#include <trace/define_trace.h>
