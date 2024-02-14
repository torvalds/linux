/* SPDX-License-Identifier: GPL-2.0-only */
#undef TRACE_SYSTEM
#define TRACE_SYSTEM gic_v3
#define TRACE_INCLUDE_PATH trace/hooks

#if !defined(_TRACE_HOOK_GIC_V3_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_HOOK_GIC_V3_H

#include <trace/hooks/vendor_hooks.h>

/*
 * Following tracepoints are not exported in tracefs and provide a
 * mechanism for vendor modules to hook and extend functionality
 */
struct irq_data;
struct cpumask;
struct gic_chip_data_v3;
DECLARE_HOOK(android_vh_gic_v3_affinity_init,
	TP_PROTO(int irq, u32 offset, u64 *affinity),
	TP_ARGS(irq, offset, affinity));
DECLARE_RESTRICTED_HOOK(android_rvh_gic_v3_set_affinity,
	TP_PROTO(struct irq_data *d, const struct cpumask *mask_val,
		 u64 *affinity, bool force, void __iomem *base,
		 void __iomem *rbase, u64 redist_stride),
	TP_ARGS(d, mask_val, affinity, force, base, rbase, redist_stride),
	1);
DECLARE_HOOK(android_vh_gic_v3_suspend,
	TP_PROTO(struct gic_chip_data_v3 *gd),
	TP_ARGS(gd));

#endif /* _TRACE_HOOK_GIC_V3_H */
/* This part must be outside protection */
#include <trace/define_trace.h>
