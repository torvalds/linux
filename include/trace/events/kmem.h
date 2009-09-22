#undef TRACE_SYSTEM
#define TRACE_SYSTEM kmem

#if !defined(_TRACE_KMEM_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_KMEM_H

#include <linux/types.h>
#include <linux/tracepoint.h>

/*
 * The order of these masks is important. Matching masks will be seen
 * first and the left over flags will end up showing by themselves.
 *
 * For example, if we have GFP_KERNEL before GFP_USER we wil get:
 *
 *  GFP_KERNEL|GFP_HARDWALL
 *
 * Thus most bits set go first.
 */
#define show_gfp_flags(flags)						\
	(flags) ? __print_flags(flags, "|",				\
	{(unsigned long)GFP_HIGHUSER_MOVABLE,	"GFP_HIGHUSER_MOVABLE"}, \
	{(unsigned long)GFP_HIGHUSER,		"GFP_HIGHUSER"},	\
	{(unsigned long)GFP_USER,		"GFP_USER"},		\
	{(unsigned long)GFP_TEMPORARY,		"GFP_TEMPORARY"},	\
	{(unsigned long)GFP_KERNEL,		"GFP_KERNEL"},		\
	{(unsigned long)GFP_NOFS,		"GFP_NOFS"},		\
	{(unsigned long)GFP_ATOMIC,		"GFP_ATOMIC"},		\
	{(unsigned long)GFP_NOIO,		"GFP_NOIO"},		\
	{(unsigned long)__GFP_HIGH,		"GFP_HIGH"},		\
	{(unsigned long)__GFP_WAIT,		"GFP_WAIT"},		\
	{(unsigned long)__GFP_IO,		"GFP_IO"},		\
	{(unsigned long)__GFP_COLD,		"GFP_COLD"},		\
	{(unsigned long)__GFP_NOWARN,		"GFP_NOWARN"},		\
	{(unsigned long)__GFP_REPEAT,		"GFP_REPEAT"},		\
	{(unsigned long)__GFP_NOFAIL,		"GFP_NOFAIL"},		\
	{(unsigned long)__GFP_NORETRY,		"GFP_NORETRY"},		\
	{(unsigned long)__GFP_COMP,		"GFP_COMP"},		\
	{(unsigned long)__GFP_ZERO,		"GFP_ZERO"},		\
	{(unsigned long)__GFP_NOMEMALLOC,	"GFP_NOMEMALLOC"},	\
	{(unsigned long)__GFP_HARDWALL,		"GFP_HARDWALL"},	\
	{(unsigned long)__GFP_THISNODE,		"GFP_THISNODE"},	\
	{(unsigned long)__GFP_RECLAIMABLE,	"GFP_RECLAIMABLE"},	\
	{(unsigned long)__GFP_MOVABLE,		"GFP_MOVABLE"}		\
	) : "GFP_NOWAIT"

TRACE_EVENT(kmalloc,

	TP_PROTO(unsigned long call_site,
		 const void *ptr,
		 size_t bytes_req,
		 size_t bytes_alloc,
		 gfp_t gfp_flags),

	TP_ARGS(call_site, ptr, bytes_req, bytes_alloc, gfp_flags),

	TP_STRUCT__entry(
		__field(	unsigned long,	call_site	)
		__field(	const void *,	ptr		)
		__field(	size_t,		bytes_req	)
		__field(	size_t,		bytes_alloc	)
		__field(	gfp_t,		gfp_flags	)
	),

	TP_fast_assign(
		__entry->call_site	= call_site;
		__entry->ptr		= ptr;
		__entry->bytes_req	= bytes_req;
		__entry->bytes_alloc	= bytes_alloc;
		__entry->gfp_flags	= gfp_flags;
	),

	TP_printk("call_site=%lx ptr=%p bytes_req=%zu bytes_alloc=%zu gfp_flags=%s",
		__entry->call_site,
		__entry->ptr,
		__entry->bytes_req,
		__entry->bytes_alloc,
		show_gfp_flags(__entry->gfp_flags))
);

TRACE_EVENT(kmem_cache_alloc,

	TP_PROTO(unsigned long call_site,
		 const void *ptr,
		 size_t bytes_req,
		 size_t bytes_alloc,
		 gfp_t gfp_flags),

	TP_ARGS(call_site, ptr, bytes_req, bytes_alloc, gfp_flags),

	TP_STRUCT__entry(
		__field(	unsigned long,	call_site	)
		__field(	const void *,	ptr		)
		__field(	size_t,		bytes_req	)
		__field(	size_t,		bytes_alloc	)
		__field(	gfp_t,		gfp_flags	)
	),

	TP_fast_assign(
		__entry->call_site	= call_site;
		__entry->ptr		= ptr;
		__entry->bytes_req	= bytes_req;
		__entry->bytes_alloc	= bytes_alloc;
		__entry->gfp_flags	= gfp_flags;
	),

	TP_printk("call_site=%lx ptr=%p bytes_req=%zu bytes_alloc=%zu gfp_flags=%s",
		__entry->call_site,
		__entry->ptr,
		__entry->bytes_req,
		__entry->bytes_alloc,
		show_gfp_flags(__entry->gfp_flags))
);

TRACE_EVENT(kmalloc_node,

	TP_PROTO(unsigned long call_site,
		 const void *ptr,
		 size_t bytes_req,
		 size_t bytes_alloc,
		 gfp_t gfp_flags,
		 int node),

	TP_ARGS(call_site, ptr, bytes_req, bytes_alloc, gfp_flags, node),

	TP_STRUCT__entry(
		__field(	unsigned long,	call_site	)
		__field(	const void *,	ptr		)
		__field(	size_t,		bytes_req	)
		__field(	size_t,		bytes_alloc	)
		__field(	gfp_t,		gfp_flags	)
		__field(	int,		node		)
	),

	TP_fast_assign(
		__entry->call_site	= call_site;
		__entry->ptr		= ptr;
		__entry->bytes_req	= bytes_req;
		__entry->bytes_alloc	= bytes_alloc;
		__entry->gfp_flags	= gfp_flags;
		__entry->node		= node;
	),

	TP_printk("call_site=%lx ptr=%p bytes_req=%zu bytes_alloc=%zu gfp_flags=%s node=%d",
		__entry->call_site,
		__entry->ptr,
		__entry->bytes_req,
		__entry->bytes_alloc,
		show_gfp_flags(__entry->gfp_flags),
		__entry->node)
);

TRACE_EVENT(kmem_cache_alloc_node,

	TP_PROTO(unsigned long call_site,
		 const void *ptr,
		 size_t bytes_req,
		 size_t bytes_alloc,
		 gfp_t gfp_flags,
		 int node),

	TP_ARGS(call_site, ptr, bytes_req, bytes_alloc, gfp_flags, node),

	TP_STRUCT__entry(
		__field(	unsigned long,	call_site	)
		__field(	const void *,	ptr		)
		__field(	size_t,		bytes_req	)
		__field(	size_t,		bytes_alloc	)
		__field(	gfp_t,		gfp_flags	)
		__field(	int,		node		)
	),

	TP_fast_assign(
		__entry->call_site	= call_site;
		__entry->ptr		= ptr;
		__entry->bytes_req	= bytes_req;
		__entry->bytes_alloc	= bytes_alloc;
		__entry->gfp_flags	= gfp_flags;
		__entry->node		= node;
	),

	TP_printk("call_site=%lx ptr=%p bytes_req=%zu bytes_alloc=%zu gfp_flags=%s node=%d",
		__entry->call_site,
		__entry->ptr,
		__entry->bytes_req,
		__entry->bytes_alloc,
		show_gfp_flags(__entry->gfp_flags),
		__entry->node)
);

TRACE_EVENT(kfree,

	TP_PROTO(unsigned long call_site, const void *ptr),

	TP_ARGS(call_site, ptr),

	TP_STRUCT__entry(
		__field(	unsigned long,	call_site	)
		__field(	const void *,	ptr		)
	),

	TP_fast_assign(
		__entry->call_site	= call_site;
		__entry->ptr		= ptr;
	),

	TP_printk("call_site=%lx ptr=%p", __entry->call_site, __entry->ptr)
);

TRACE_EVENT(kmem_cache_free,

	TP_PROTO(unsigned long call_site, const void *ptr),

	TP_ARGS(call_site, ptr),

	TP_STRUCT__entry(
		__field(	unsigned long,	call_site	)
		__field(	const void *,	ptr		)
	),

	TP_fast_assign(
		__entry->call_site	= call_site;
		__entry->ptr		= ptr;
	),

	TP_printk("call_site=%lx ptr=%p", __entry->call_site, __entry->ptr)
);

TRACE_EVENT(mm_page_free_direct,

	TP_PROTO(struct page *page, unsigned int order),

	TP_ARGS(page, order),

	TP_STRUCT__entry(
		__field(	struct page *,	page		)
		__field(	unsigned int,	order		)
	),

	TP_fast_assign(
		__entry->page		= page;
		__entry->order		= order;
	),

	TP_printk("page=%p pfn=%lu order=%d",
			__entry->page,
			page_to_pfn(__entry->page),
			__entry->order)
);

TRACE_EVENT(mm_pagevec_free,

	TP_PROTO(struct page *page, int cold),

	TP_ARGS(page, cold),

	TP_STRUCT__entry(
		__field(	struct page *,	page		)
		__field(	int,		cold		)
	),

	TP_fast_assign(
		__entry->page		= page;
		__entry->cold		= cold;
	),

	TP_printk("page=%p pfn=%lu order=0 cold=%d",
			__entry->page,
			page_to_pfn(__entry->page),
			__entry->cold)
);

TRACE_EVENT(mm_page_alloc,

	TP_PROTO(struct page *page, unsigned int order,
			gfp_t gfp_flags, int migratetype),

	TP_ARGS(page, order, gfp_flags, migratetype),

	TP_STRUCT__entry(
		__field(	struct page *,	page		)
		__field(	unsigned int,	order		)
		__field(	gfp_t,		gfp_flags	)
		__field(	int,		migratetype	)
	),

	TP_fast_assign(
		__entry->page		= page;
		__entry->order		= order;
		__entry->gfp_flags	= gfp_flags;
		__entry->migratetype	= migratetype;
	),

	TP_printk("page=%p pfn=%lu order=%d migratetype=%d gfp_flags=%s",
		__entry->page,
		page_to_pfn(__entry->page),
		__entry->order,
		__entry->migratetype,
		show_gfp_flags(__entry->gfp_flags))
);

TRACE_EVENT(mm_page_alloc_zone_locked,

	TP_PROTO(struct page *page, unsigned int order, int migratetype),

	TP_ARGS(page, order, migratetype),

	TP_STRUCT__entry(
		__field(	struct page *,	page		)
		__field(	unsigned int,	order		)
		__field(	int,		migratetype	)
	),

	TP_fast_assign(
		__entry->page		= page;
		__entry->order		= order;
		__entry->migratetype	= migratetype;
	),

	TP_printk("page=%p pfn=%lu order=%u migratetype=%d percpu_refill=%d",
		__entry->page,
		page_to_pfn(__entry->page),
		__entry->order,
		__entry->migratetype,
		__entry->order == 0)
);

TRACE_EVENT(mm_page_pcpu_drain,

	TP_PROTO(struct page *page, int order, int migratetype),

	TP_ARGS(page, order, migratetype),

	TP_STRUCT__entry(
		__field(	struct page *,	page		)
		__field(	int,		order		)
		__field(	int,		migratetype	)
	),

	TP_fast_assign(
		__entry->page		= page;
		__entry->order		= order;
		__entry->migratetype	= migratetype;
	),

	TP_printk("page=%p pfn=%lu order=%d migratetype=%d",
		__entry->page,
		page_to_pfn(__entry->page),
		__entry->order,
		__entry->migratetype)
);

TRACE_EVENT(mm_page_alloc_extfrag,

	TP_PROTO(struct page *page,
			int alloc_order, int fallback_order,
			int alloc_migratetype, int fallback_migratetype),

	TP_ARGS(page,
		alloc_order, fallback_order,
		alloc_migratetype, fallback_migratetype),

	TP_STRUCT__entry(
		__field(	struct page *,	page			)
		__field(	int,		alloc_order		)
		__field(	int,		fallback_order		)
		__field(	int,		alloc_migratetype	)
		__field(	int,		fallback_migratetype	)
	),

	TP_fast_assign(
		__entry->page			= page;
		__entry->alloc_order		= alloc_order;
		__entry->fallback_order		= fallback_order;
		__entry->alloc_migratetype	= alloc_migratetype;
		__entry->fallback_migratetype	= fallback_migratetype;
	),

	TP_printk("page=%p pfn=%lu alloc_order=%d fallback_order=%d pageblock_order=%d alloc_migratetype=%d fallback_migratetype=%d fragmenting=%d change_ownership=%d",
		__entry->page,
		page_to_pfn(__entry->page),
		__entry->alloc_order,
		__entry->fallback_order,
		pageblock_order,
		__entry->alloc_migratetype,
		__entry->fallback_migratetype,
		__entry->fallback_order < pageblock_order,
		__entry->alloc_migratetype == __entry->fallback_migratetype)
);

#endif /* _TRACE_KMEM_H */

/* This part must be outside protection */
#include <trace/define_trace.h>
