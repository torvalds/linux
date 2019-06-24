/* SPDX-License-Identifier: GPL-2.0 */
#undef TRACE_SYSTEM
#define TRACE_SYSTEM page_pool

#if !defined(_TRACE_PAGE_POOL_H) || defined(TRACE_HEADER_MULTI_READ)
#define      _TRACE_PAGE_POOL_H

#include <linux/types.h>
#include <linux/tracepoint.h>

#include <net/page_pool.h>

TRACE_EVENT(page_pool_inflight,

	TP_PROTO(const struct page_pool *pool,
		 s32 inflight, u32 hold, u32 release),

	TP_ARGS(pool, inflight, hold, release),

	TP_STRUCT__entry(
		__field(const struct page_pool *, pool)
		__field(s32,	inflight)
		__field(u32,	hold)
		__field(u32,	release)
	),

	TP_fast_assign(
		__entry->pool		= pool;
		__entry->inflight	= inflight;
		__entry->hold		= hold;
		__entry->release	= release;
	),

	TP_printk("page_pool=%p inflight=%d hold=%u release=%u",
	  __entry->pool, __entry->inflight, __entry->hold, __entry->release)
);

TRACE_EVENT(page_pool_state_release,

	TP_PROTO(const struct page_pool *pool,
		 const struct page *page, u32 release),

	TP_ARGS(pool, page, release),

	TP_STRUCT__entry(
		__field(const struct page_pool *,	pool)
		__field(const struct page *,		page)
		__field(u32,				release)
	),

	TP_fast_assign(
		__entry->pool		= pool;
		__entry->page		= page;
		__entry->release	= release;
	),

	TP_printk("page_pool=%p page=%p release=%u",
		  __entry->pool, __entry->page, __entry->release)
);

TRACE_EVENT(page_pool_state_hold,

	TP_PROTO(const struct page_pool *pool,
		 const struct page *page, u32 hold),

	TP_ARGS(pool, page, hold),

	TP_STRUCT__entry(
		__field(const struct page_pool *,	pool)
		__field(const struct page *,		page)
		__field(u32,				hold)
	),

	TP_fast_assign(
		__entry->pool	= pool;
		__entry->page	= page;
		__entry->hold	= hold;
	),

	TP_printk("page_pool=%p page=%p hold=%u",
		  __entry->pool, __entry->page, __entry->hold)
);

#endif /* _TRACE_PAGE_POOL_H */

/* This part must be outside protection */
#include <trace/define_trace.h>
