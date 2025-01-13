/* SPDX-License-Identifier: GPL-2.0 */
#undef TRACE_SYSTEM
#define TRACE_SYSTEM reboot

#define TRACE_INCLUDE_PATH trace/hooks

#if !defined(_TRACE_HOOK_REBOOT_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_HOOK_REBOOT_H

#include <trace/hooks/vendor_hooks.h>

DECLARE_RESTRICTED_HOOK(android_rvh_hw_protection_shutdown,
	TP_PROTO(const char *reason),
	TP_ARGS(reason), 1);

#endif /* _TRACE_HOOK_REBOOT_H */
/* This part must be outside protection */
#include <trace/define_trace.h>
