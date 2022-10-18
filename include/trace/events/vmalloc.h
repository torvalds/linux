/* SPDX-License-Identifier: GPL-2.0 */
#undef TRACE_SYSTEM
#define TRACE_SYSTEM vmalloc

#if !defined(_TRACE_VMALLOC_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_VMALLOC_H

#include <linux/tracepoint.h>

/**
 * alloc_vmap_area - called when a new vmap allocation occurs
 * @addr:	an allocated address
 * @size:	a requested size
 * @align:	a requested alignment
 * @vstart:	a requested start range
 * @vend:	a requested end range
 * @failed:	an allocation failed or not
 *
 * This event is used for a debug purpose, it can give an extra
 * information for a developer about how often it occurs and which
 * parameters are passed for further validation.
 */
TRACE_EVENT(alloc_vmap_area,

	TP_PROTO(unsigned long addr, unsigned long size, unsigned long align,
		unsigned long vstart, unsigned long vend, int failed),

	TP_ARGS(addr, size, align, vstart, vend, failed),

	TP_STRUCT__entry(
		__field(unsigned long, addr)
		__field(unsigned long, size)
		__field(unsigned long, align)
		__field(unsigned long, vstart)
		__field(unsigned long, vend)
		__field(int, failed)
	),

	TP_fast_assign(
		__entry->addr = addr;
		__entry->size = size;
		__entry->align = align;
		__entry->vstart = vstart;
		__entry->vend = vend;
		__entry->failed = failed;
	),

	TP_printk("va_start: %lu size=%lu align=%lu vstart=0x%lx vend=0x%lx failed=%d",
		__entry->addr, __entry->size, __entry->align,
		__entry->vstart, __entry->vend, __entry->failed)
);

#endif /*  _TRACE_VMALLOC_H */

/* This part must be outside protection */
#include <trace/define_trace.h>
