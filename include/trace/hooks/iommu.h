/* SPDX-License-Identifier: GPL-2.0 */
#undef TRACE_SYSTEM
#define TRACE_SYSTEM iommu

#define TRACE_INCLUDE_PATH trace/hooks

#if !defined(_TRACE_HOOK_IOMMU_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_HOOK_IOMMU_H

#include <linux/types.h>

#include <trace/hooks/vendor_hooks.h>

DECLARE_RESTRICTED_HOOK(android_rvh_iommu_setup_dma_ops,
	TP_PROTO(struct device *dev, u64 dma_base, u64 dma_limit),
	TP_ARGS(dev, dma_base, dma_limit), 1);

struct iova_domain;

DECLARE_HOOK(android_vh_iommu_iovad_alloc_iova,
	TP_PROTO(struct device *dev, struct iova_domain *iovad, dma_addr_t iova, size_t size),
	TP_ARGS(dev, iovad, iova, size));

DECLARE_HOOK(android_vh_iommu_iovad_free_iova,
	TP_PROTO(struct iova_domain *iovad, dma_addr_t iova, size_t size),
	TP_ARGS(iovad, iova, size));

#endif /* _TRACE_HOOK_IOMMU_H */

/* This part must be outside protection */
#include <trace/define_trace.h>
