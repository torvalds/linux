#undef TRACE_SYSTEM
#define TRACE_SYSTEM swiotlb

#if !defined(_TRACE_SWIOTLB_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_SWIOTLB_H

#include <linux/tracepoint.h>

TRACE_EVENT(swiotlb_bounced,

	TP_PROTO(struct device *dev,
		 dma_addr_t dev_addr,
		 size_t size,
		 enum swiotlb_force swiotlb_force),

	TP_ARGS(dev, dev_addr, size, swiotlb_force),

	TP_STRUCT__entry(
		__string(	dev_name,	dev_name(dev)		)
		__field(	u64,	dma_mask			)
		__field(	dma_addr_t,	dev_addr		)
		__field(	size_t,	size				)
		__field(	enum swiotlb_force,	swiotlb_force	)
	),

	TP_fast_assign(
		__assign_str(dev_name, dev_name(dev));
		__entry->dma_mask = (dev->dma_mask ? *dev->dma_mask : 0);
		__entry->dev_addr = dev_addr;
		__entry->size = size;
		__entry->swiotlb_force = swiotlb_force;
	),

	TP_printk("dev_name: %s dma_mask=%llx dev_addr=%llx "
		"size=%zu %s",
		__get_str(dev_name),
		__entry->dma_mask,
		(unsigned long long)__entry->dev_addr,
		__entry->size,
		__print_symbolic(__entry->swiotlb_force,
			{ SWIOTLB_NORMAL,	"NORMAL" },
			{ SWIOTLB_FORCE,	"FORCE" },
			{ SWIOTLB_NO_FORCE,	"NO_FORCE" }))
);

#endif /*  _TRACE_SWIOTLB_H */

/* This part must be outside protection */
#include <trace/define_trace.h>
