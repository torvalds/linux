/* SPDX-License-Identifier: GPL-2.0-only */
#undef TRACE_SYSTEM
#define TRACE_SYSTEM psi

#define TRACE_INCLUDE_PATH trace/hooks

#if !defined(_TRACE_HOOK_PSI_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_HOOK_PSI_H

#include <linux/tracepoint.h>
#include <trace/hooks/vendor_hooks.h>

#if defined(CONFIG_TRACEPOINTS) && defined(CONFIG_ANDROID_VENDOR_HOOKS)

#if defined(__GENKSYMS__) || !defined(CONFIG_PSI)
struct psi_group;
struct psi_trigger;
#else
/* struct psi_group, struct psi_trigger */
#include <linux/psi_types.h>
#endif /* __GENKSYMS__ */
DECLARE_HOOK(android_vh_psi_event,
	TP_PROTO(struct psi_trigger *t),
	TP_ARGS(t));

DECLARE_HOOK(android_vh_psi_group,
	TP_PROTO(struct psi_group *group),
	TP_ARGS(group));

#else
#define trace_android_vh_psi_event(t)
#define trace_android_vh_psi_group(group)
#endif

#endif /* _TRACE_HOOK_PSI_H */

/* This part must be outside protection */
#include <trace/define_trace.h>
