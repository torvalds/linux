/* SPDX-License-Identifier: GPL-2.0 */
#undef TRACE_SYSTEM
#define TRACE_SYSTEM page_ref

#if !defined(_TRACE_PAGE_REF_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_PAGE_REF_H

#include <linux/types.h>
#include <linux/page_ref.h>
#include <linux/tracepoint.h>
#include <trace/events/mmflags.h>

DECLARE_EVENT_CLASS(page_ref_mod_template,

	TP_PROTO(struct page *page, int v),

	TP_ARGS(page, v),

	TP_STRUCT__entry(
		__field(unsigned long, pfn)
		__field(unsigned long, flags)
		__field(int, count)
		__field(int, mapcount)
		__field(void *, mapping)
		__field(int, mt)
		__field(int, val)
	),

	TP_fast_assign(
		__entry->pfn = page_to_pfn(page);
		__entry->flags = page->flags;
		__entry->count = page_ref_count(page);
		__entry->mapcount = atomic_read(&page->_mapcount);
		__entry->mapping = page->mapping;
		__entry->mt = get_pageblock_migratetype(page);
		__entry->val = v;
	),

	TP_printk("pfn=0x%lx flags=%s count=%d mapcount=%d mapping=%p mt=%d val=%d",
		__entry->pfn,
		show_page_flags(__entry->flags & PAGEFLAGS_MASK),
		__entry->count,
		__entry->mapcount, __entry->mapping, __entry->mt,
		__entry->val)
);

DEFINE_EVENT(page_ref_mod_template, page_ref_set,

	TP_PROTO(struct page *page, int v),

	TP_ARGS(page, v)
);

DEFINE_EVENT(page_ref_mod_template, page_ref_mod,

	TP_PROTO(struct page *page, int v),

	TP_ARGS(page, v)
);

DECLARE_EVENT_CLASS(page_ref_mod_and_test_template,

	TP_PROTO(struct page *page, int v, int ret),

	TP_ARGS(page, v, ret),

	TP_STRUCT__entry(
		__field(unsigned long, pfn)
		__field(unsigned long, flags)
		__field(int, count)
		__field(int, mapcount)
		__field(void *, mapping)
		__field(int, mt)
		__field(int, val)
		__field(int, ret)
	),

	TP_fast_assign(
		__entry->pfn = page_to_pfn(page);
		__entry->flags = page->flags;
		__entry->count = page_ref_count(page);
		__entry->mapcount = atomic_read(&page->_mapcount);
		__entry->mapping = page->mapping;
		__entry->mt = get_pageblock_migratetype(page);
		__entry->val = v;
		__entry->ret = ret;
	),

	TP_printk("pfn=0x%lx flags=%s count=%d mapcount=%d mapping=%p mt=%d val=%d ret=%d",
		__entry->pfn,
		show_page_flags(__entry->flags & PAGEFLAGS_MASK),
		__entry->count,
		__entry->mapcount, __entry->mapping, __entry->mt,
		__entry->val, __entry->ret)
);

DEFINE_EVENT(page_ref_mod_and_test_template, page_ref_mod_and_test,

	TP_PROTO(struct page *page, int v, int ret),

	TP_ARGS(page, v, ret)
);

DEFINE_EVENT(page_ref_mod_and_test_template, page_ref_mod_and_return,

	TP_PROTO(struct page *page, int v, int ret),

	TP_ARGS(page, v, ret)
);

DEFINE_EVENT(page_ref_mod_and_test_template, page_ref_mod_unless,

	TP_PROTO(struct page *page, int v, int ret),

	TP_ARGS(page, v, ret)
);

DEFINE_EVENT(page_ref_mod_and_test_template, page_ref_freeze,

	TP_PROTO(struct page *page, int v, int ret),

	TP_ARGS(page, v, ret)
);

DEFINE_EVENT(page_ref_mod_template, page_ref_unfreeze,

	TP_PROTO(struct page *page, int v),

	TP_ARGS(page, v)
);

#endif /* _TRACE_PAGE_COUNT_H */

/* This part must be outside protection */
#include <trace/define_trace.h>
