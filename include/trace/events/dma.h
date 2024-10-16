/* SPDX-License-Identifier: GPL-2.0 */
#undef TRACE_SYSTEM
#define TRACE_SYSTEM dma

#if !defined(_TRACE_DMA_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_DMA_H

#include <linux/tracepoint.h>
#include <linux/dma-direction.h>
#include <linux/dma-mapping.h>
#include <trace/events/mmflags.h>

TRACE_DEFINE_ENUM(DMA_BIDIRECTIONAL);
TRACE_DEFINE_ENUM(DMA_TO_DEVICE);
TRACE_DEFINE_ENUM(DMA_FROM_DEVICE);
TRACE_DEFINE_ENUM(DMA_NONE);

#define decode_dma_data_direction(dir) \
	__print_symbolic(dir, \
		{ DMA_BIDIRECTIONAL, "BIDIRECTIONAL" }, \
		{ DMA_TO_DEVICE, "TO_DEVICE" }, \
		{ DMA_FROM_DEVICE, "FROM_DEVICE" }, \
		{ DMA_NONE, "NONE" })

#define decode_dma_attrs(attrs) \
	__print_flags(attrs, "|", \
		{ DMA_ATTR_WEAK_ORDERING, "WEAK_ORDERING" }, \
		{ DMA_ATTR_WRITE_COMBINE, "WRITE_COMBINE" }, \
		{ DMA_ATTR_NO_KERNEL_MAPPING, "NO_KERNEL_MAPPING" }, \
		{ DMA_ATTR_SKIP_CPU_SYNC, "SKIP_CPU_SYNC" }, \
		{ DMA_ATTR_FORCE_CONTIGUOUS, "FORCE_CONTIGUOUS" }, \
		{ DMA_ATTR_ALLOC_SINGLE_PAGES, "ALLOC_SINGLE_PAGES" }, \
		{ DMA_ATTR_NO_WARN, "NO_WARN" }, \
		{ DMA_ATTR_PRIVILEGED, "PRIVILEGED" })

DECLARE_EVENT_CLASS(dma_map,
	TP_PROTO(struct device *dev, phys_addr_t phys_addr, dma_addr_t dma_addr,
		 size_t size, enum dma_data_direction dir, unsigned long attrs),
	TP_ARGS(dev, phys_addr, dma_addr, size, dir, attrs),

	TP_STRUCT__entry(
		__string(device, dev_name(dev))
		__field(u64, phys_addr)
		__field(u64, dma_addr)
		__field(size_t, size)
		__field(enum dma_data_direction, dir)
		__field(unsigned long, attrs)
	),

	TP_fast_assign(
		__assign_str(device);
		__entry->phys_addr = phys_addr;
		__entry->dma_addr = dma_addr;
		__entry->size = size;
		__entry->dir = dir;
		__entry->attrs = attrs;
	),

	TP_printk("%s dir=%s dma_addr=%llx size=%zu phys_addr=%llx attrs=%s",
		__get_str(device),
		decode_dma_data_direction(__entry->dir),
		__entry->dma_addr,
		__entry->size,
		__entry->phys_addr,
		decode_dma_attrs(__entry->attrs))
);

DEFINE_EVENT(dma_map, dma_map_page,
	TP_PROTO(struct device *dev, phys_addr_t phys_addr, dma_addr_t dma_addr,
		 size_t size, enum dma_data_direction dir, unsigned long attrs),
	TP_ARGS(dev, phys_addr, dma_addr, size, dir, attrs));

DEFINE_EVENT(dma_map, dma_map_resource,
	TP_PROTO(struct device *dev, phys_addr_t phys_addr, dma_addr_t dma_addr,
		 size_t size, enum dma_data_direction dir, unsigned long attrs),
	TP_ARGS(dev, phys_addr, dma_addr, size, dir, attrs));

DECLARE_EVENT_CLASS(dma_unmap,
	TP_PROTO(struct device *dev, dma_addr_t addr, size_t size,
		 enum dma_data_direction dir, unsigned long attrs),
	TP_ARGS(dev, addr, size, dir, attrs),

	TP_STRUCT__entry(
		__string(device, dev_name(dev))
		__field(u64, addr)
		__field(size_t, size)
		__field(enum dma_data_direction, dir)
		__field(unsigned long, attrs)
	),

	TP_fast_assign(
		__assign_str(device);
		__entry->addr = addr;
		__entry->size = size;
		__entry->dir = dir;
		__entry->attrs = attrs;
	),

	TP_printk("%s dir=%s dma_addr=%llx size=%zu attrs=%s",
		__get_str(device),
		decode_dma_data_direction(__entry->dir),
		__entry->addr,
		__entry->size,
		decode_dma_attrs(__entry->attrs))
);

DEFINE_EVENT(dma_unmap, dma_unmap_page,
	TP_PROTO(struct device *dev, dma_addr_t addr, size_t size,
		 enum dma_data_direction dir, unsigned long attrs),
	TP_ARGS(dev, addr, size, dir, attrs));

DEFINE_EVENT(dma_unmap, dma_unmap_resource,
	TP_PROTO(struct device *dev, dma_addr_t addr, size_t size,
		 enum dma_data_direction dir, unsigned long attrs),
	TP_ARGS(dev, addr, size, dir, attrs));

TRACE_EVENT(dma_alloc,
	TP_PROTO(struct device *dev, void *virt_addr, dma_addr_t dma_addr,
		 size_t size, gfp_t flags, unsigned long attrs),
	TP_ARGS(dev, virt_addr, dma_addr, size, flags, attrs),

	TP_STRUCT__entry(
		__string(device, dev_name(dev))
		__field(u64, phys_addr)
		__field(u64, dma_addr)
		__field(size_t, size)
		__field(gfp_t, flags)
		__field(unsigned long, attrs)
	),

	TP_fast_assign(
		__assign_str(device);
		__entry->phys_addr = virt_to_phys(virt_addr);
		__entry->dma_addr = dma_addr;
		__entry->size = size;
		__entry->flags = flags;
		__entry->attrs = attrs;
	),

	TP_printk("%s dma_addr=%llx size=%zu phys_addr=%llx flags=%s attrs=%s",
		__get_str(device),
		__entry->dma_addr,
		__entry->size,
		__entry->phys_addr,
		show_gfp_flags(__entry->flags),
		decode_dma_attrs(__entry->attrs))
);

TRACE_EVENT(dma_free,
	TP_PROTO(struct device *dev, void *virt_addr, dma_addr_t dma_addr,
		 size_t size, unsigned long attrs),
	TP_ARGS(dev, virt_addr, dma_addr, size, attrs),

	TP_STRUCT__entry(
		__string(device, dev_name(dev))
		__field(u64, phys_addr)
		__field(u64, dma_addr)
		__field(size_t, size)
		__field(unsigned long, attrs)
	),

	TP_fast_assign(
		__assign_str(device);
		__entry->phys_addr = virt_to_phys(virt_addr);
		__entry->dma_addr = dma_addr;
		__entry->size = size;
		__entry->attrs = attrs;
	),

	TP_printk("%s dma_addr=%llx size=%zu phys_addr=%llx attrs=%s",
		__get_str(device),
		__entry->dma_addr,
		__entry->size,
		__entry->phys_addr,
		decode_dma_attrs(__entry->attrs))
);

TRACE_EVENT(dma_map_sg,
	TP_PROTO(struct device *dev, struct scatterlist *sgl, int nents,
		 int ents, enum dma_data_direction dir, unsigned long attrs),
	TP_ARGS(dev, sgl, nents, ents, dir, attrs),

	TP_STRUCT__entry(
		__string(device, dev_name(dev))
		__dynamic_array(u64, phys_addrs, nents)
		__dynamic_array(u64, dma_addrs, ents)
		__dynamic_array(unsigned int, lengths, ents)
		__field(enum dma_data_direction, dir)
		__field(unsigned long, attrs)
	),

	TP_fast_assign(
		struct scatterlist *sg;
		int i;

		__assign_str(device);
		for_each_sg(sgl, sg, nents, i)
			((u64 *)__get_dynamic_array(phys_addrs))[i] = sg_phys(sg);
		for_each_sg(sgl, sg, ents, i) {
			((u64 *)__get_dynamic_array(dma_addrs))[i] =
				sg_dma_address(sg);
			((unsigned int *)__get_dynamic_array(lengths))[i] =
				sg_dma_len(sg);
		}
		__entry->dir = dir;
		__entry->attrs = attrs;
	),

	TP_printk("%s dir=%s dma_addrs=%s sizes=%s phys_addrs=%s attrs=%s",
		__get_str(device),
		decode_dma_data_direction(__entry->dir),
		__print_array(__get_dynamic_array(dma_addrs),
			      __get_dynamic_array_len(dma_addrs) /
				sizeof(u64), sizeof(u64)),
		__print_array(__get_dynamic_array(lengths),
			      __get_dynamic_array_len(lengths) /
				sizeof(unsigned int), sizeof(unsigned int)),
		__print_array(__get_dynamic_array(phys_addrs),
			      __get_dynamic_array_len(phys_addrs) /
				sizeof(u64), sizeof(u64)),
		decode_dma_attrs(__entry->attrs))
);

TRACE_EVENT(dma_unmap_sg,
	TP_PROTO(struct device *dev, struct scatterlist *sgl, int nents,
		 enum dma_data_direction dir, unsigned long attrs),
	TP_ARGS(dev, sgl, nents, dir, attrs),

	TP_STRUCT__entry(
		__string(device, dev_name(dev))
		__dynamic_array(u64, addrs, nents)
		__field(enum dma_data_direction, dir)
		__field(unsigned long, attrs)
	),

	TP_fast_assign(
		struct scatterlist *sg;
		int i;

		__assign_str(device);
		for_each_sg(sgl, sg, nents, i)
			((u64 *)__get_dynamic_array(addrs))[i] = sg_phys(sg);
		__entry->dir = dir;
		__entry->attrs = attrs;
	),

	TP_printk("%s dir=%s phys_addrs=%s attrs=%s",
		__get_str(device),
		decode_dma_data_direction(__entry->dir),
		__print_array(__get_dynamic_array(addrs),
			      __get_dynamic_array_len(addrs) /
				sizeof(u64), sizeof(u64)),
		decode_dma_attrs(__entry->attrs))
);

DECLARE_EVENT_CLASS(dma_sync_single,
	TP_PROTO(struct device *dev, dma_addr_t dma_addr, size_t size,
		 enum dma_data_direction dir),
	TP_ARGS(dev, dma_addr, size, dir),

	TP_STRUCT__entry(
		__string(device, dev_name(dev))
		__field(u64, dma_addr)
		__field(size_t, size)
		__field(enum dma_data_direction, dir)
	),

	TP_fast_assign(
		__assign_str(device);
		__entry->dma_addr = dma_addr;
		__entry->size = size;
		__entry->dir = dir;
	),

	TP_printk("%s dir=%s dma_addr=%llx size=%zu",
		__get_str(device),
		decode_dma_data_direction(__entry->dir),
		__entry->dma_addr,
		__entry->size)
);

DEFINE_EVENT(dma_sync_single, dma_sync_single_for_cpu,
	TP_PROTO(struct device *dev, dma_addr_t dma_addr, size_t size,
		 enum dma_data_direction dir),
	TP_ARGS(dev, dma_addr, size, dir));

DEFINE_EVENT(dma_sync_single, dma_sync_single_for_device,
	TP_PROTO(struct device *dev, dma_addr_t dma_addr, size_t size,
		 enum dma_data_direction dir),
	TP_ARGS(dev, dma_addr, size, dir));

DECLARE_EVENT_CLASS(dma_sync_sg,
	TP_PROTO(struct device *dev, struct scatterlist *sgl, int nents,
		 enum dma_data_direction dir),
	TP_ARGS(dev, sgl, nents, dir),

	TP_STRUCT__entry(
		__string(device, dev_name(dev))
		__dynamic_array(u64, dma_addrs, nents)
		__dynamic_array(unsigned int, lengths, nents)
		__field(enum dma_data_direction, dir)
	),

	TP_fast_assign(
		struct scatterlist *sg;
		int i;

		__assign_str(device);
		for_each_sg(sgl, sg, nents, i) {
			((u64 *)__get_dynamic_array(dma_addrs))[i] =
				sg_dma_address(sg);
			((unsigned int *)__get_dynamic_array(lengths))[i] =
				sg_dma_len(sg);
		}
		__entry->dir = dir;
	),

	TP_printk("%s dir=%s dma_addrs=%s sizes=%s",
		__get_str(device),
		decode_dma_data_direction(__entry->dir),
		__print_array(__get_dynamic_array(dma_addrs),
			      __get_dynamic_array_len(dma_addrs) /
				sizeof(u64), sizeof(u64)),
		__print_array(__get_dynamic_array(lengths),
			      __get_dynamic_array_len(lengths) /
				sizeof(unsigned int), sizeof(unsigned int)))
);

DEFINE_EVENT(dma_sync_sg, dma_sync_sg_for_cpu,
	TP_PROTO(struct device *dev, struct scatterlist *sg, int nents,
		 enum dma_data_direction dir),
	TP_ARGS(dev, sg, nents, dir));

DEFINE_EVENT(dma_sync_sg, dma_sync_sg_for_device,
	TP_PROTO(struct device *dev, struct scatterlist *sg, int nents,
		 enum dma_data_direction dir),
	TP_ARGS(dev, sg, nents, dir));

#endif /*  _TRACE_DMA_H */

/* This part must be outside protection */
#include <trace/define_trace.h>
