/* SPDX-License-Identifier: GPL-2.0 */
#undef TRACE_SYSTEM
#define TRACE_SYSTEM gic

#define TRACE_INCLUDE_PATH trace/hooks

#if !defined(_TRACE_HOOK_GIC_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_HOOK_GIC_H

#include <linux/irqdomain.h>

#include <linux/tracepoint.h>
#include <trace/hooks/vendor_hooks.h>

#if defined(CONFIG_TRACEPOINTS) && defined(CONFIG_ANDROID_VENDOR_HOOKS)

DECLARE_HOOK(android_vh_gic_resume,
	TP_PROTO(struct irq_domain *domain, void __iomem *dist_base),
	TP_ARGS(domain, dist_base));

#else

#define trace_android_vh_gic_resume(domain, dist_base)

#endif

#endif /* _TRACE_HOOK_GIC_H */
/* This part must be outside protection */
#include <trace/define_trace.h>
