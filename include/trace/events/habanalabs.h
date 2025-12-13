/* SPDX-License-Identifier: GPL-2.0
 *
 * Copyright 2022-2023 HabanaLabs, Ltd.
 * All Rights Reserved.
 *
 */

#undef TRACE_SYSTEM
#define TRACE_SYSTEM habanalabs

#if !defined(_TRACE_HABANALABS_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_HABANALABS_H

#include <linux/tracepoint.h>

DECLARE_EVENT_CLASS(habanalabs_mmu_template,
	TP_PROTO(struct device *dev, u64 virt_addr, u64 phys_addr, u32 page_size, bool flush_pte),

	TP_ARGS(dev, virt_addr, phys_addr, page_size, flush_pte),

	TP_STRUCT__entry(
		__string(dname, dev_name(dev))
		__field(u64, virt_addr)
		__field(u64, phys_addr)
		__field(u32, page_size)
		__field(u8, flush_pte)
	),

	TP_fast_assign(
		__assign_str(dname);
		__entry->virt_addr = virt_addr;
		__entry->phys_addr = phys_addr;
		__entry->page_size = page_size;
		__entry->flush_pte = flush_pte;
	),

	TP_printk("%s: vaddr: %#llx, paddr: %#llx, psize: %#x, flush: %s",
		__get_str(dname),
		__entry->virt_addr,
		__entry->phys_addr,
		__entry->page_size,
		__entry->flush_pte ? "true" : "false")
);

DEFINE_EVENT(habanalabs_mmu_template, habanalabs_mmu_map,
	TP_PROTO(struct device *dev, u64 virt_addr, u64 phys_addr, u32 page_size, bool flush_pte),
	TP_ARGS(dev, virt_addr, phys_addr, page_size, flush_pte));

DEFINE_EVENT(habanalabs_mmu_template, habanalabs_mmu_unmap,
	TP_PROTO(struct device *dev, u64 virt_addr, u64 phys_addr, u32 page_size, bool flush_pte),
	TP_ARGS(dev, virt_addr, phys_addr, page_size, flush_pte));

DECLARE_EVENT_CLASS(habanalabs_dma_alloc_template,
	TP_PROTO(struct device *dev, u64 cpu_addr, u64 dma_addr, size_t size, const char *caller),

	TP_ARGS(dev, cpu_addr, dma_addr, size, caller),

	TP_STRUCT__entry(
		__string(dname, dev_name(dev))
		__field(u64, cpu_addr)
		__field(u64, dma_addr)
		__field(u32, size)
		__field(const char *, caller)
	),

	TP_fast_assign(
		__assign_str(dname);
		__entry->cpu_addr = cpu_addr;
		__entry->dma_addr = dma_addr;
		__entry->size = size;
		__entry->caller = caller;
	),

	TP_printk("%s: cpu_addr: %#llx, dma_addr: %#llx, size: %#x, caller: %s",
		__get_str(dname),
		__entry->cpu_addr,
		__entry->dma_addr,
		__entry->size,
		__entry->caller)
);

DEFINE_EVENT(habanalabs_dma_alloc_template, habanalabs_dma_alloc,
	TP_PROTO(struct device *dev, u64 cpu_addr, u64 dma_addr, size_t size, const char *caller),
	TP_ARGS(dev, cpu_addr, dma_addr, size, caller));

DEFINE_EVENT(habanalabs_dma_alloc_template, habanalabs_dma_free,
	TP_PROTO(struct device *dev, u64 cpu_addr, u64 dma_addr, size_t size, const char *caller),
	TP_ARGS(dev, cpu_addr, dma_addr, size, caller));

DECLARE_EVENT_CLASS(habanalabs_dma_map_template,
	TP_PROTO(struct device *dev, u64 phys_addr, u64 dma_addr, size_t len,
			enum dma_data_direction dir, const char *caller),

	TP_ARGS(dev, phys_addr, dma_addr, len, dir, caller),

	TP_STRUCT__entry(
		__string(dname, dev_name(dev))
		__field(u64, phys_addr)
		__field(u64, dma_addr)
		__field(u32, len)
		__field(int, dir)
		__field(const char *, caller)
	),

	TP_fast_assign(
		__assign_str(dname);
		__entry->phys_addr = phys_addr;
		__entry->dma_addr = dma_addr;
		__entry->len = len;
		__entry->dir = dir;
		__entry->caller = caller;
	),

	TP_printk("%s: phys_addr: %#llx, dma_addr: %#llx, len: %#x, dir: %d, caller: %s",
		__get_str(dname),
		__entry->phys_addr,
		__entry->dma_addr,
		__entry->len,
		__entry->dir,
		__entry->caller)
);

DEFINE_EVENT(habanalabs_dma_map_template, habanalabs_dma_map_page,
	TP_PROTO(struct device *dev, u64 phys_addr, u64 dma_addr, size_t len,
			enum dma_data_direction dir, const char *caller),
	TP_ARGS(dev, phys_addr, dma_addr, len, dir, caller));

DEFINE_EVENT(habanalabs_dma_map_template, habanalabs_dma_unmap_page,
	TP_PROTO(struct device *dev, u64 phys_addr, u64 dma_addr, size_t len,
			enum dma_data_direction dir, const char *caller),
	TP_ARGS(dev, phys_addr, dma_addr, len, dir, caller));

DECLARE_EVENT_CLASS(habanalabs_comms_template,
	TP_PROTO(struct device *dev, char *op_str),

	TP_ARGS(dev, op_str),

	TP_STRUCT__entry(
		__string(dname, dev_name(dev))
		__field(char *, op_str)
	),

	TP_fast_assign(
		__assign_str(dname);
		__entry->op_str = op_str;
	),

	TP_printk("%s: cmd: %s",
		__get_str(dname),
		__entry->op_str)
);

DEFINE_EVENT(habanalabs_comms_template, habanalabs_comms_protocol_cmd,
	TP_PROTO(struct device *dev, char *op_str),
	TP_ARGS(dev, op_str));

DEFINE_EVENT(habanalabs_comms_template, habanalabs_comms_send_cmd,
	TP_PROTO(struct device *dev, char *op_str),
	TP_ARGS(dev, op_str));

DEFINE_EVENT(habanalabs_comms_template, habanalabs_comms_wait_status,
	TP_PROTO(struct device *dev, char *op_str),
	TP_ARGS(dev, op_str));

DEFINE_EVENT(habanalabs_comms_template, habanalabs_comms_wait_status_done,
	TP_PROTO(struct device *dev, char *op_str),
	TP_ARGS(dev, op_str));

DECLARE_EVENT_CLASS(habanalabs_reg_access_template,
	TP_PROTO(struct device *dev, u32 addr, u32 val),

	TP_ARGS(dev, addr, val),

	TP_STRUCT__entry(
		__string(dname, dev_name(dev))
		__field(u32, addr)
		__field(u32, val)
	),

	TP_fast_assign(
		__assign_str(dname);
		__entry->addr = addr;
		__entry->val = val;
	),

	TP_printk("%s: addr: %#x, val: %#x",
		__get_str(dname),
		__entry->addr,
		__entry->val)
);

DEFINE_EVENT(habanalabs_reg_access_template, habanalabs_rreg32,
	TP_PROTO(struct device *dev, u32 addr, u32 val),
	TP_ARGS(dev, addr, val));

DEFINE_EVENT(habanalabs_reg_access_template, habanalabs_wreg32,
	TP_PROTO(struct device *dev, u32 addr, u32 val),
	TP_ARGS(dev, addr, val));

DEFINE_EVENT(habanalabs_reg_access_template, habanalabs_elbi_read,
	TP_PROTO(struct device *dev, u32 addr, u32 val),
	TP_ARGS(dev, addr, val));

DEFINE_EVENT(habanalabs_reg_access_template, habanalabs_elbi_write,
	TP_PROTO(struct device *dev, u32 addr, u32 val),
	TP_ARGS(dev, addr, val));

#endif /* if !defined(_TRACE_HABANALABS_H) || defined(TRACE_HEADER_MULTI_READ) */

/* This part must be outside protection */
#include <trace/define_trace.h>
