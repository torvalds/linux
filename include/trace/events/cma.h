/* SPDX-License-Identifier: GPL-2.0 */
#undef TRACE_SYSTEM
#define TRACE_SYSTEM cma

#if !defined(_TRACE_CMA_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_CMA_H

#include <linux/types.h>
#include <linux/tracepoint.h>

DECLARE_EVENT_CLASS(cma_alloc_class,

	TP_PROTO(const char *name, unsigned long pfn, const struct page *page,
		 unsigned int count, unsigned int align),

	TP_ARGS(name, pfn, page, count, align),

	TP_STRUCT__entry(
		__string(name, name)
		__field(unsigned long, pfn)
		__field(const struct page *, page)
		__field(unsigned int, count)
		__field(unsigned int, align)
	),

	TP_fast_assign(
		__assign_str(name, name);
		__entry->pfn = pfn;
		__entry->page = page;
		__entry->count = count;
		__entry->align = align;
	),

	TP_printk("name=%s pfn=%lx page=%p count=%u align=%u",
		  __get_str(name),
		  __entry->pfn,
		  __entry->page,
		  __entry->count,
		  __entry->align)
);

TRACE_EVENT(cma_release,

	TP_PROTO(const char *name, unsigned long pfn, const struct page *page,
		 unsigned int count),

	TP_ARGS(name, pfn, page, count),

	TP_STRUCT__entry(
		__string(name, name)
		__field(unsigned long, pfn)
		__field(const struct page *, page)
		__field(unsigned int, count)
	),

	TP_fast_assign(
		__assign_str(name, name);
		__entry->pfn = pfn;
		__entry->page = page;
		__entry->count = count;
	),

	TP_printk("name=%s pfn=%lx page=%p count=%u",
		  __get_str(name),
		  __entry->pfn,
		  __entry->page,
		  __entry->count)
);

TRACE_EVENT(cma_alloc_start,

	TP_PROTO(const char *name, unsigned int count, unsigned int align),

	TP_ARGS(name, count, align),

	TP_STRUCT__entry(
		__string(name, name)
		__field(unsigned int, count)
		__field(unsigned int, align)
	),

	TP_fast_assign(
		__assign_str(name, name);
		__entry->count = count;
		__entry->align = align;
	),

	TP_printk("name=%s count=%u align=%u",
		  __get_str(name),
		  __entry->count,
		  __entry->align)
);

TRACE_EVENT(cma_alloc_info,

	TP_PROTO(const char *name, const struct page *page, unsigned int count, unsigned int align, struct cma_alloc_info *info),

	TP_ARGS(name, page, count, align, info),

	TP_STRUCT__entry(
		__string(name, name)
		__field(unsigned long, pfn)
		__field(unsigned int, count)
		__field(unsigned int, align)
		__field(unsigned long, nr_migrated)
		__field(unsigned long, nr_reclaimed)
		__field(unsigned long, nr_mapped)
		__field(unsigned int, err_iso)
		__field(unsigned int, err_mig)
		__field(unsigned int, err_test)
	),

	TP_fast_assign(
		__assign_str(name, name);
		__entry->pfn = page ? page_to_pfn(page) : -1;
		__entry->count = count;
		__entry->align = align;
		__entry->nr_migrated = info->nr_migrated;
		__entry->nr_reclaimed = info->nr_reclaimed;
		__entry->nr_mapped = info->nr_mapped;
		__entry->err_iso = info->nr_isolate_fail;
		__entry->err_mig = info->nr_migrate_fail;
		__entry->err_test = info->nr_test_fail;
	),

	TP_printk("name=%s pfn=0x%lx count=%u align=%u nr_migrated=%lu nr_reclaimed=%lu nr_mapped=%lu err_iso=%u err_mig=%u err_test=%u",
		  __get_str(name),
		  __entry->pfn,
		  __entry->count,
		  __entry->align,
		  __entry->nr_migrated,
		  __entry->nr_reclaimed,
		  __entry->nr_mapped,
		  __entry->err_iso,
		  __entry->err_mig,
		  __entry->err_test)
);

DEFINE_EVENT(cma_alloc_class, cma_alloc_finish,

	TP_PROTO(const char *name, unsigned long pfn, const struct page *page,
		 unsigned int count, unsigned int align),

	TP_ARGS(name, pfn, page, count, align)
);

DEFINE_EVENT(cma_alloc_class, cma_alloc_busy_retry,

	TP_PROTO(const char *name, unsigned long pfn, const struct page *page,
		 unsigned int count, unsigned int align),

	TP_ARGS(name, pfn, page, count, align)
);

#endif /* _TRACE_CMA_H */

/* This part must be outside protection */
#include <trace/define_trace.h>
