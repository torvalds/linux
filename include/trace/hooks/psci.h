/* SPDX-License-Identifier: GPL-2.0 */
#undef TRACE_SYSTEM
#define TRACE_SYSTEM psci
#undef TRACE_INCLUDE_PATH

#define TRACE_INCLUDE_PATH trace/hooks

#if !defined(_TRACE_HOOK_PSCI_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_HOOK_PSCI_H

#include <trace/hooks/vendor_hooks.h>

DECLARE_RESTRICTED_HOOK(android_rvh_psci_tos_resident_on,
	TP_PROTO(int cpu, bool *resident),
	TP_ARGS(cpu, resident), 1);

DECLARE_RESTRICTED_HOOK(android_rvh_psci_cpu_suspend,
	TP_PROTO(u32 state, bool *deny),
	TP_ARGS(state, deny), 1);

/* macro versions of hooks are no longer required */

#endif /* _TRACE_HOOK_PSCI_H */
/* This part must be outside protection */
#include <trace/define_trace.h>
