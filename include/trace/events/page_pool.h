/* SPDX-License-Identifier: GPL-2.0 */
#undef TRACE_SYSTEM
#define TRACE_SYSTEM page_pool

#if !defined(_TRACE_PAGE_POOL_H) || defined(TRACE_HEADER_MULTI_READ)
#define      _TRACE_PAGE_POOL_H

#include <linux/types.h>
#include <linux/tracepoint.h>

#include <trace/events/mmflags.h>
#include <net/page_pool/types.h>

TRACE_EVENT(page_pool_release,

	TP_PROTO(const struct page_pool *pool,
		 s32 inflight, u32 hold, u32 release),

	TP_ARGS(pool, inflight, hold, release),

	TP_STRUCT__entry(
		__field(const struct page_pool *, pool)
		__field(s32,	inflight)
		__field(u32,	hold)
		__field(u32,	release)
		__field(u64,	cnt)
	),

	TP_fast_assign(
		__entry->pool		= pool;
		__entry->inflight	= inflight;
		__entry->hold		= hold;
		__entry->release	= release;
		__entry->cnt		= pool->destroy_cnt;
	),

	TP_printk("page_pool=%p inflight=%d hold=%u release=%u cnt=%llu",
		__entry->pool, __entry->inflight, __entry->hold,
		__entry->release, __entry->cnt)
);

TRACE_EVENT(page_pool_state_release,

	TP_PROTO(const struct page_pool *pool,
		 const struct page *page, u32 release),

	TP_ARGS(pool, page, release),

	TP_STRUCT__entry(
		__field(const struct page_pool *,	pool)
		__field(const struct page *,		page)
		__field(u32,				release)
		__field(unsigned long,			pfn)
	),

	TP_fast_assign(
		__entry->pool		= pool;
		__entry->page		= page;
		__entry->release	= release;
		__entry->pfn		= page_to_pfn(page);
	),

	TP_printk("page_pool=%p page=%p pfn=0x%lx release=%u",
		  __entry->pool, __entry->page, __entry->pfn, __entry->release)
);

TRACE_EVENT(page_pool_state_hold,

	TP_PROTO(const struct page_pool *pool,
		 const struct page *page, u32 hold),

	TP_ARGS(pool, page, hold),

	TP_STRUCT__entry(
		__field(const struct page_pool *,	pool)
		__field(const struct page *,		page)
		__field(u32,				hold)
		__field(unsigned long,			pfn)
	),

	TP_fast_assign(
		__entry->pool	= pool;
		__entry->page	= page;
		__entry->hold	= hold;
		__entry->pfn	= page_to_pfn(page);
	),

	TP_printk("page_pool=%p page=%p pfn=0x%lx hold=%u",
		  __entry->pool, __entry->page, __entry->pfn, __entry->hold)
);

TRACE_EVENT(page_pool_update_nid,

	TP_PROTO(const struct page_pool *pool, int new_nid),

	TP_ARGS(pool, new_nid),

	TP_STRUCT__entry(
		__field(const struct page_pool *, pool)
		__field(int,			  pool_nid)
		__field(int,			  new_nid)
	),

	TP_fast_assign(
		__entry->pool		= pool;
		__entry->pool_nid	= pool->p.nid;
		__entry->new_nid	= new_nid;
	),

	TP_printk("page_pool=%p pool_nid=%d new_nid=%d",
		  __entry->pool, __entry->pool_nid, __entry->new_nid)
);

#endif /* _TRACE_PAGE_POOL_H */

/* This part must be outside protection */
#include <trace/define_trace.h>
