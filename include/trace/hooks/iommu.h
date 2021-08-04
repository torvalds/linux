/* SPDX-License-Identifier: GPL-2.0 */
#undef TRACE_SYSTEM
#define TRACE_SYSTEM iommu

#define TRACE_INCLUDE_PATH trace/hooks

#if !defined(_TRACE_HOOK_IOMMU_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_HOOK_IOMMU_H

#include <linux/types.h>

#include <linux/tracepoint.h>
#include <trace/hooks/vendor_hooks.h>

#if defined(CONFIG_TRACEPOINTS) && defined(CONFIG_ANDROID_VENDOR_HOOKS)

DECLARE_HOOK(android_vh_iommu_setup_dma_ops,
	TP_PROTO(struct device *dev, u64 dma_base, u64 dma_limit),
	TP_ARGS(dev, dma_base, dma_limit));

#else

#define trace_android_vh_iommu_setup_dma_ops(dev, dma_base, dma_limit)

#endif

#endif /* _TRACE_HOOK_IOMMU_H */

/* This part must be outside protection */
#include <trace/define_trace.h>
