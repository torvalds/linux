/* SPDX-License-Identifier: GPL-2.0-only */
#undef TRACE_SYSTEM
#define TRACE_SYSTEM psi

#define TRACE_INCLUDE_PATH trace/hooks

#if !defined(_TRACE_HOOK_PSI_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_HOOK_PSI_H

#include <trace/hooks/vendor_hooks.h>

struct psi_trigger;
struct psi_group;
DECLARE_HOOK(android_vh_psi_event,
	TP_PROTO(struct psi_trigger *t),
	TP_ARGS(t));

DECLARE_HOOK(android_vh_psi_group,
	TP_PROTO(struct psi_group *group),
	TP_ARGS(group));

#endif /* _TRACE_HOOK_PSI_H */

/* This part must be outside protection */
#include <trace/define_trace.h>
