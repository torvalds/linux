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
		{ DMA_ATTR_PRIVILEGED, "PRIVILEGED" }, \
		{ DMA_ATTR_MMIO, "MMIO" })

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

#define DEFINE_MAP_EVENT(name) \
DEFINE_EVENT(dma_map, name, \
	TP_PROTO(struct device *dev, phys_addr_t phys_addr, dma_addr_t dma_addr, \
		 size_t size, enum dma_data_direction dir, unsigned long attrs), \
	TP_ARGS(dev, phys_addr, dma_addr, size, dir, attrs))

DEFINE_MAP_EVENT(dma_map_phys);

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

#define DEFINE_UNMAP_EVENT(name) \
DEFINE_EVENT(dma_unmap, name, \
	TP_PROTO(struct device *dev, dma_addr_t addr, size_t size, \
		 enum dma_data_direction dir, unsigned long attrs), \
	TP_ARGS(dev, addr, size, dir, attrs))

DEFINE_UNMAP_EVENT(dma_unmap_phys);

DECLARE_EVENT_CLASS(dma_alloc_class,
	TP_PROTO(struct device *dev, void *virt_addr, dma_addr_t dma_addr,
		 size_t size, enum dma_data_direction dir, gfp_t flags,
		 unsigned long attrs),
	TP_ARGS(dev, virt_addr, dma_addr, size, dir, flags, attrs),

	TP_STRUCT__entry(
		__string(device, dev_name(dev))
		__field(void *, virt_addr)
		__field(u64, dma_addr)
		__field(size_t, size)
		__field(gfp_t, flags)
		__field(enum dma_data_direction, dir)
		__field(unsigned long, attrs)
	),

	TP_fast_assign(
		__assign_str(device);
		__entry->virt_addr = virt_addr;
		__entry->dma_addr = dma_addr;
		__entry->size = size;
		__entry->flags = flags;
		__entry->dir = dir;
		__entry->attrs = attrs;
	),

	TP_printk("%s dir=%s dma_addr=%llx size=%zu virt_addr=%p flags=%s attrs=%s",
		__get_str(device),
		decode_dma_data_direction(__entry->dir),
		__entry->dma_addr,
		__entry->size,
		__entry->virt_addr,
		show_gfp_flags(__entry->flags),
		decode_dma_attrs(__entry->attrs))
);

#define DEFINE_ALLOC_EVENT(name) \
DEFINE_EVENT(dma_alloc_class, name, \
	TP_PROTO(struct device *dev, void *virt_addr, dma_addr_t dma_addr, \
		 size_t size, enum dma_data_direction dir, gfp_t flags, \
		 unsigned long attrs), \
	TP_ARGS(dev, virt_addr, dma_addr, size, dir, flags, attrs))

DEFINE_ALLOC_EVENT(dma_alloc);
DEFINE_ALLOC_EVENT(dma_alloc_pages);
DEFINE_ALLOC_EVENT(dma_alloc_sgt_err);

TRACE_EVENT(dma_alloc_sgt,
	TP_PROTO(struct device *dev, struct sg_table *sgt, size_t size,
		 enum dma_data_direction dir, gfp_t flags, unsigned long attrs),
	TP_ARGS(dev, sgt, size, dir, flags, attrs),

	TP_STRUCT__entry(
		__string(device, dev_name(dev))
		__dynamic_array(u64, phys_addrs, sgt->orig_nents)
		__field(u64, dma_addr)
		__field(size_t, size)
		__field(enum dma_data_direction, dir)
		__field(gfp_t, flags)
		__field(unsigned long, attrs)
	),

	TP_fast_assign(
		struct scatterlist *sg;
		int i;

		__assign_str(device);
		for_each_sg(sgt->sgl, sg, sgt->orig_nents, i)
			((u64 *)__get_dynamic_array(phys_addrs))[i] = sg_phys(sg);
		__entry->dma_addr = sg_dma_address(sgt->sgl);
		__entry->size = size;
		__entry->dir = dir;
		__entry->flags = flags;
		__entry->attrs = attrs;
	),

	TP_printk("%s dir=%s dma_addr=%llx size=%zu phys_addrs=%s flags=%s attrs=%s",
		__get_str(device),
		decode_dma_data_direction(__entry->dir),
		__entry->dma_addr,
		__entry->size,
		__print_array(__get_dynamic_array(phys_addrs),
			      __get_dynamic_array_len(phys_addrs) /
				sizeof(u64), sizeof(u64)),
		show_gfp_flags(__entry->flags),
		decode_dma_attrs(__entry->attrs))
);

DECLARE_EVENT_CLASS(dma_free_class,
	TP_PROTO(struct device *dev, void *virt_addr, dma_addr_t dma_addr,
		 size_t size, enum dma_data_direction dir, unsigned long attrs),
	TP_ARGS(dev, virt_addr, dma_addr, size, dir, attrs),

	TP_STRUCT__entry(
		__string(device, dev_name(dev))
		__field(void *, virt_addr)
		__field(u64, dma_addr)
		__field(size_t, size)
		__field(enum dma_data_direction, dir)
		__field(unsigned long, attrs)
	),

	TP_fast_assign(
		__assign_str(device);
		__entry->virt_addr = virt_addr;
		__entry->dma_addr = dma_addr;
		__entry->size = size;
		__entry->dir = dir;
		__entry->attrs = attrs;
	),

	TP_printk("%s dir=%s dma_addr=%llx size=%zu virt_addr=%p attrs=%s",
		__get_str(device),
		decode_dma_data_direction(__entry->dir),
		__entry->dma_addr,
		__entry->size,
		__entry->virt_addr,
		decode_dma_attrs(__entry->attrs))
);

#define DEFINE_FREE_EVENT(name) \
DEFINE_EVENT(dma_free_class, name, \
	TP_PROTO(struct device *dev, void *virt_addr, dma_addr_t dma_addr, \
		 size_t size, enum dma_data_direction dir, unsigned long attrs), \
	TP_ARGS(dev, virt_addr, dma_addr, size, dir, attrs))

DEFINE_FREE_EVENT(dma_free);
DEFINE_FREE_EVENT(dma_free_pages);

TRACE_EVENT(dma_free_sgt,
	TP_PROTO(struct device *dev, struct sg_table *sgt, size_t size,
		 enum dma_data_direction dir),
	TP_ARGS(dev, sgt, size, dir),

	TP_STRUCT__entry(
		__string(device, dev_name(dev))
		__dynamic_array(u64, phys_addrs, sgt->orig_nents)
		__field(u64, dma_addr)
		__field(size_t, size)
		__field(enum dma_data_direction, dir)
	),

	TP_fast_assign(
		struct scatterlist *sg;
		int i;

		__assign_str(device);
		for_each_sg(sgt->sgl, sg, sgt->orig_nents, i)
			((u64 *)__get_dynamic_array(phys_addrs))[i] = sg_phys(sg);
		__entry->dma_addr = sg_dma_address(sgt->sgl);
		__entry->size = size;
		__entry->dir = dir;
	),

	TP_printk("%s dir=%s dma_addr=%llx size=%zu phys_addrs=%s",
		__get_str(device),
		decode_dma_data_direction(__entry->dir),
		__entry->dma_addr,
		__entry->size,
		__print_array(__get_dynamic_array(phys_addrs),
			      __get_dynamic_array_len(phys_addrs) /
				sizeof(u64), sizeof(u64)))
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

TRACE_EVENT(dma_map_sg_err,
	TP_PROTO(struct device *dev, struct scatterlist *sgl, int nents,
		 int err, enum dma_data_direction dir, unsigned long attrs),
	TP_ARGS(dev, sgl, nents, err, dir, attrs),

	TP_STRUCT__entry(
		__string(device, dev_name(dev))
		__dynamic_array(u64, phys_addrs, nents)
		__field(int, err)
		__field(enum dma_data_direction, dir)
		__field(unsigned long, attrs)
	),

	TP_fast_assign(
		struct scatterlist *sg;
		int i;

		__assign_str(device);
		for_each_sg(sgl, sg, nents, i)
			((u64 *)__get_dynamic_array(phys_addrs))[i] = sg_phys(sg);
		__entry->err = err;
		__entry->dir = dir;
		__entry->attrs = attrs;
	),

	TP_printk("%s dir=%s dma_addrs=%s err=%d attrs=%s",
		__get_str(device),
		decode_dma_data_direction(__entry->dir),
		__print_array(__get_dynamic_array(phys_addrs),
			      __get_dynamic_array_len(phys_addrs) /
				sizeof(u64), sizeof(u64)),
		__entry->err,
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

#define DEFINE_SYNC_SINGLE_EVENT(name) \
DEFINE_EVENT(dma_sync_single, name, \
	TP_PROTO(struct device *dev, dma_addr_t dma_addr, size_t size, \
		 enum dma_data_direction dir), \
	TP_ARGS(dev, dma_addr, size, dir))

DEFINE_SYNC_SINGLE_EVENT(dma_sync_single_for_cpu);
DEFINE_SYNC_SINGLE_EVENT(dma_sync_single_for_device);

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

#define DEFINE_SYNC_SG_EVENT(name) \
DEFINE_EVENT(dma_sync_sg, name, \
	TP_PROTO(struct device *dev, struct scatterlist *sg, int nents, \
		 enum dma_data_direction dir), \
	TP_ARGS(dev, sg, nents, dir))

DEFINE_SYNC_SG_EVENT(dma_sync_sg_for_cpu);
DEFINE_SYNC_SG_EVENT(dma_sync_sg_for_device);

#endif /*  _TRACE_DMA_H */

/* This part must be outside protection */
#include <trace/define_trace.h>
