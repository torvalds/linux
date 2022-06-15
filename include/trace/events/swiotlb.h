/* SPDX-License-Identifier: GPL-2.0 */
#undef TRACE_SYSTEM
#define TRACE_SYSTEM swiotlb

#if !defined(_TRACE_SWIOTLB_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_SWIOTLB_H

#include <linux/tracepoint.h>

TRACE_EVENT(swiotlb_bounced,
	TP_PROTO(struct device *dev, dma_addr_t dev_addr, size_t size),
	TP_ARGS(dev, dev_addr, size),

	TP_STRUCT__entry(
		__string(dev_name, dev_name(dev))
		__field(u64, dma_mask)
		__field(dma_addr_t, dev_addr)
		__field(size_t, size)
		__field(bool, force)
	),

	TP_fast_assign(
		__assign_str(dev_name, dev_name(dev));
		__entry->dma_mask = (dev->dma_mask ? *dev->dma_mask : 0);
		__entry->dev_addr = dev_addr;
		__entry->size = size;
		__entry->force = is_swiotlb_force_bounce(dev);
	),

	TP_printk("dev_name: %s dma_mask=%llx dev_addr=%llx size=%zu %s",
		__get_str(dev_name),
		__entry->dma_mask,
		(unsigned long long)__entry->dev_addr,
		__entry->size,
		__entry->force ? "FORCE" : "NORMAL")
);

#endif /*  _TRACE_SWIOTLB_H */

/* This part must be outside protection */
#include <trace/define_trace.h>
