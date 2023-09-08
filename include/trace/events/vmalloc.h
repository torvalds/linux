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

/**
 * purge_vmap_area_lazy - called when vmap areas were lazily freed
 * @start:		purging start address
 * @end:		purging end address
 * @npurged:	numbed of purged vmap areas
 *
 * This event is used for a debug purpose. It gives some
 * indication about start:end range and how many objects
 * are released.
 */
TRACE_EVENT(purge_vmap_area_lazy,

	TP_PROTO(unsigned long start, unsigned long end,
		unsigned int npurged),

	TP_ARGS(start, end, npurged),

	TP_STRUCT__entry(
		__field(unsigned long, start)
		__field(unsigned long, end)
		__field(unsigned int, npurged)
	),

	TP_fast_assign(
		__entry->start = start;
		__entry->end = end;
		__entry->npurged = npurged;
	),

	TP_printk("start=0x%lx end=0x%lx num_purged=%u",
		__entry->start, __entry->end, __entry->npurged)
);

/**
 * free_vmap_area_noflush - called when a vmap area is freed
 * @va_start:		a start address of VA
 * @nr_lazy:		number of current lazy pages
 * @nr_lazy_max:	number of maximum lazy pages
 *
 * This event is used for a debug purpose. It gives some
 * indication about a VA that is released, number of current
 * outstanding areas and a maximum allowed threshold before
 * dropping all of them.
 */
TRACE_EVENT(free_vmap_area_noflush,

	TP_PROTO(unsigned long va_start, unsigned long nr_lazy,
		unsigned long nr_lazy_max),

	TP_ARGS(va_start, nr_lazy, nr_lazy_max),

	TP_STRUCT__entry(
		__field(unsigned long, va_start)
		__field(unsigned long, nr_lazy)
		__field(unsigned long, nr_lazy_max)
	),

	TP_fast_assign(
		__entry->va_start = va_start;
		__entry->nr_lazy = nr_lazy;
		__entry->nr_lazy_max = nr_lazy_max;
	),

	TP_printk("va_start=0x%lx nr_lazy=%lu nr_lazy_max=%lu",
		__entry->va_start, __entry->nr_lazy, __entry->nr_lazy_max)
);

#endif /*  _TRACE_VMALLOC_H */

/* This part must be outside protection */
#include <trace/define_trace.h>
