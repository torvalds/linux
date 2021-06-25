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
	TP_PROTO(struct device *dev, u64 dma_base, u64 size),
	TP_ARGS(dev, dma_base, size));

DECLARE_HOOK(android_vh_iommu_alloc_iova,
	TP_PROTO(struct device *dev, dma_addr_t iova, size_t size),
	TP_ARGS(dev, iova, size));

DECLARE_HOOK(android_vh_iommu_free_iova,
	TP_PROTO(dma_addr_t iova, size_t size),
	TP_ARGS(iova, size));
#else

#define trace_android_vh_iommu_setup_dma_ops(dev, dma_base, size)
#define trace_android_vh_iommu_alloc_iova(dev, iova, size)
#define trace_android_vh_iommu_free_iova(iova, size)

#endif

#endif /* _TRACE_HOOK_IOMMU_H */

/* This part must be outside protection */
#include <trace/define_trace.h>
