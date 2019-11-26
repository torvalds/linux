/* SPDX-License-Identifier: GPL-2.0 */
#undef TRACE_SYSTEM
#define TRACE_SYSTEM xdp

#if !defined(_TRACE_XDP_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_XDP_H

#include <linux/netdevice.h>
#include <linux/filter.h>
#include <linux/tracepoint.h>
#include <linux/bpf.h>

#define __XDP_ACT_MAP(FN)	\
	FN(ABORTED)		\
	FN(DROP)		\
	FN(PASS)		\
	FN(TX)			\
	FN(REDIRECT)

#define __XDP_ACT_TP_FN(x)	\
	TRACE_DEFINE_ENUM(XDP_##x);
#define __XDP_ACT_SYM_FN(x)	\
	{ XDP_##x, #x },
#define __XDP_ACT_SYM_TAB	\
	__XDP_ACT_MAP(__XDP_ACT_SYM_FN) { -1, NULL }
__XDP_ACT_MAP(__XDP_ACT_TP_FN)

TRACE_EVENT(xdp_exception,

	TP_PROTO(const struct net_device *dev,
		 const struct bpf_prog *xdp, u32 act),

	TP_ARGS(dev, xdp, act),

	TP_STRUCT__entry(
		__field(int, prog_id)
		__field(u32, act)
		__field(int, ifindex)
	),

	TP_fast_assign(
		__entry->prog_id	= xdp->aux->id;
		__entry->act		= act;
		__entry->ifindex	= dev->ifindex;
	),

	TP_printk("prog_id=%d action=%s ifindex=%d",
		  __entry->prog_id,
		  __print_symbolic(__entry->act, __XDP_ACT_SYM_TAB),
		  __entry->ifindex)
);

TRACE_EVENT(xdp_bulk_tx,

	TP_PROTO(const struct net_device *dev,
		 int sent, int drops, int err),

	TP_ARGS(dev, sent, drops, err),

	TP_STRUCT__entry(
		__field(int, ifindex)
		__field(u32, act)
		__field(int, drops)
		__field(int, sent)
		__field(int, err)
	),

	TP_fast_assign(
		__entry->ifindex	= dev->ifindex;
		__entry->act		= XDP_TX;
		__entry->drops		= drops;
		__entry->sent		= sent;
		__entry->err		= err;
	),

	TP_printk("ifindex=%d action=%s sent=%d drops=%d err=%d",
		  __entry->ifindex,
		  __print_symbolic(__entry->act, __XDP_ACT_SYM_TAB),
		  __entry->sent, __entry->drops, __entry->err)
);

DECLARE_EVENT_CLASS(xdp_redirect_template,

	TP_PROTO(const struct net_device *dev,
		 const struct bpf_prog *xdp,
		 int to_ifindex, int err,
		 const struct bpf_map *map, u32 map_index),

	TP_ARGS(dev, xdp, to_ifindex, err, map, map_index),

	TP_STRUCT__entry(
		__field(int, prog_id)
		__field(u32, act)
		__field(int, ifindex)
		__field(int, err)
		__field(int, to_ifindex)
		__field(u32, map_id)
		__field(int, map_index)
	),

	TP_fast_assign(
		__entry->prog_id	= xdp->aux->id;
		__entry->act		= XDP_REDIRECT;
		__entry->ifindex	= dev->ifindex;
		__entry->err		= err;
		__entry->to_ifindex	= to_ifindex;
		__entry->map_id		= map ? map->id : 0;
		__entry->map_index	= map_index;
	),

	TP_printk("prog_id=%d action=%s ifindex=%d to_ifindex=%d err=%d",
		  __entry->prog_id,
		  __print_symbolic(__entry->act, __XDP_ACT_SYM_TAB),
		  __entry->ifindex, __entry->to_ifindex,
		  __entry->err)
);

DEFINE_EVENT(xdp_redirect_template, xdp_redirect,
	TP_PROTO(const struct net_device *dev,
		 const struct bpf_prog *xdp,
		 int to_ifindex, int err,
		 const struct bpf_map *map, u32 map_index),
	TP_ARGS(dev, xdp, to_ifindex, err, map, map_index)
);

DEFINE_EVENT(xdp_redirect_template, xdp_redirect_err,
	TP_PROTO(const struct net_device *dev,
		 const struct bpf_prog *xdp,
		 int to_ifindex, int err,
		 const struct bpf_map *map, u32 map_index),
	TP_ARGS(dev, xdp, to_ifindex, err, map, map_index)
);

#define _trace_xdp_redirect(dev, xdp, to)		\
	 trace_xdp_redirect(dev, xdp, to, 0, NULL, 0);

#define _trace_xdp_redirect_err(dev, xdp, to, err)	\
	 trace_xdp_redirect_err(dev, xdp, to, err, NULL, 0);

DEFINE_EVENT_PRINT(xdp_redirect_template, xdp_redirect_map,
	TP_PROTO(const struct net_device *dev,
		 const struct bpf_prog *xdp,
		 int to_ifindex, int err,
		 const struct bpf_map *map, u32 map_index),
	TP_ARGS(dev, xdp, to_ifindex, err, map, map_index),
	TP_printk("prog_id=%d action=%s ifindex=%d to_ifindex=%d err=%d"
		  " map_id=%d map_index=%d",
		  __entry->prog_id,
		  __print_symbolic(__entry->act, __XDP_ACT_SYM_TAB),
		  __entry->ifindex, __entry->to_ifindex,
		  __entry->err,
		  __entry->map_id, __entry->map_index)
);

DEFINE_EVENT_PRINT(xdp_redirect_template, xdp_redirect_map_err,
	TP_PROTO(const struct net_device *dev,
		 const struct bpf_prog *xdp,
		 int to_ifindex, int err,
		 const struct bpf_map *map, u32 map_index),
	TP_ARGS(dev, xdp, to_ifindex, err, map, map_index),
	TP_printk("prog_id=%d action=%s ifindex=%d to_ifindex=%d err=%d"
		  " map_id=%d map_index=%d",
		  __entry->prog_id,
		  __print_symbolic(__entry->act, __XDP_ACT_SYM_TAB),
		  __entry->ifindex, __entry->to_ifindex,
		  __entry->err,
		  __entry->map_id, __entry->map_index)
);

#ifndef __DEVMAP_OBJ_TYPE
#define __DEVMAP_OBJ_TYPE
struct _bpf_dtab_netdev {
	struct net_device *dev;
};
#endif /* __DEVMAP_OBJ_TYPE */

#define devmap_ifindex(fwd, map)				\
	((map->map_type == BPF_MAP_TYPE_DEVMAP ||		\
	  map->map_type == BPF_MAP_TYPE_DEVMAP_HASH) ?		\
	  ((struct _bpf_dtab_netdev *)fwd)->dev->ifindex : 0)

#define _trace_xdp_redirect_map(dev, xdp, fwd, map, idx)		\
	 trace_xdp_redirect_map(dev, xdp, devmap_ifindex(fwd, map),	\
				0, map, idx)

#define _trace_xdp_redirect_map_err(dev, xdp, fwd, map, idx, err)	\
	 trace_xdp_redirect_map_err(dev, xdp, devmap_ifindex(fwd, map),	\
				    err, map, idx)

TRACE_EVENT(xdp_cpumap_kthread,

	TP_PROTO(int map_id, unsigned int processed,  unsigned int drops,
		 int sched),

	TP_ARGS(map_id, processed, drops, sched),

	TP_STRUCT__entry(
		__field(int, map_id)
		__field(u32, act)
		__field(int, cpu)
		__field(unsigned int, drops)
		__field(unsigned int, processed)
		__field(int, sched)
	),

	TP_fast_assign(
		__entry->map_id		= map_id;
		__entry->act		= XDP_REDIRECT;
		__entry->cpu		= smp_processor_id();
		__entry->drops		= drops;
		__entry->processed	= processed;
		__entry->sched	= sched;
	),

	TP_printk("kthread"
		  " cpu=%d map_id=%d action=%s"
		  " processed=%u drops=%u"
		  " sched=%d",
		  __entry->cpu, __entry->map_id,
		  __print_symbolic(__entry->act, __XDP_ACT_SYM_TAB),
		  __entry->processed, __entry->drops,
		  __entry->sched)
);

TRACE_EVENT(xdp_cpumap_enqueue,

	TP_PROTO(int map_id, unsigned int processed,  unsigned int drops,
		 int to_cpu),

	TP_ARGS(map_id, processed, drops, to_cpu),

	TP_STRUCT__entry(
		__field(int, map_id)
		__field(u32, act)
		__field(int, cpu)
		__field(unsigned int, drops)
		__field(unsigned int, processed)
		__field(int, to_cpu)
	),

	TP_fast_assign(
		__entry->map_id		= map_id;
		__entry->act		= XDP_REDIRECT;
		__entry->cpu		= smp_processor_id();
		__entry->drops		= drops;
		__entry->processed	= processed;
		__entry->to_cpu		= to_cpu;
	),

	TP_printk("enqueue"
		  " cpu=%d map_id=%d action=%s"
		  " processed=%u drops=%u"
		  " to_cpu=%d",
		  __entry->cpu, __entry->map_id,
		  __print_symbolic(__entry->act, __XDP_ACT_SYM_TAB),
		  __entry->processed, __entry->drops,
		  __entry->to_cpu)
);

TRACE_EVENT(xdp_devmap_xmit,

	TP_PROTO(const struct bpf_map *map, u32 map_index,
		 int sent, int drops,
		 const struct net_device *from_dev,
		 const struct net_device *to_dev, int err),

	TP_ARGS(map, map_index, sent, drops, from_dev, to_dev, err),

	TP_STRUCT__entry(
		__field(int, map_id)
		__field(u32, act)
		__field(u32, map_index)
		__field(int, drops)
		__field(int, sent)
		__field(int, from_ifindex)
		__field(int, to_ifindex)
		__field(int, err)
	),

	TP_fast_assign(
		__entry->map_id		= map->id;
		__entry->act		= XDP_REDIRECT;
		__entry->map_index	= map_index;
		__entry->drops		= drops;
		__entry->sent		= sent;
		__entry->from_ifindex	= from_dev->ifindex;
		__entry->to_ifindex	= to_dev->ifindex;
		__entry->err		= err;
	),

	TP_printk("ndo_xdp_xmit"
		  " map_id=%d map_index=%d action=%s"
		  " sent=%d drops=%d"
		  " from_ifindex=%d to_ifindex=%d err=%d",
		  __entry->map_id, __entry->map_index,
		  __print_symbolic(__entry->act, __XDP_ACT_SYM_TAB),
		  __entry->sent, __entry->drops,
		  __entry->from_ifindex, __entry->to_ifindex, __entry->err)
);

/* Expect users already include <net/xdp.h>, but not xdp_priv.h */
#include <net/xdp_priv.h>

#define __MEM_TYPE_MAP(FN)	\
	FN(PAGE_SHARED)		\
	FN(PAGE_ORDER0)		\
	FN(PAGE_POOL)		\
	FN(ZERO_COPY)

#define __MEM_TYPE_TP_FN(x)	\
	TRACE_DEFINE_ENUM(MEM_TYPE_##x);
#define __MEM_TYPE_SYM_FN(x)	\
	{ MEM_TYPE_##x, #x },
#define __MEM_TYPE_SYM_TAB	\
	__MEM_TYPE_MAP(__MEM_TYPE_SYM_FN) { -1, 0 }
__MEM_TYPE_MAP(__MEM_TYPE_TP_FN)

TRACE_EVENT(mem_disconnect,

	TP_PROTO(const struct xdp_mem_allocator *xa),

	TP_ARGS(xa),

	TP_STRUCT__entry(
		__field(const struct xdp_mem_allocator *,	xa)
		__field(u32,		mem_id)
		__field(u32,		mem_type)
		__field(const void *,	allocator)
	),

	TP_fast_assign(
		__entry->xa		= xa;
		__entry->mem_id		= xa->mem.id;
		__entry->mem_type	= xa->mem.type;
		__entry->allocator	= xa->allocator;
	),

	TP_printk("mem_id=%d mem_type=%s allocator=%p",
		  __entry->mem_id,
		  __print_symbolic(__entry->mem_type, __MEM_TYPE_SYM_TAB),
		  __entry->allocator
	)
);

TRACE_EVENT(mem_connect,

	TP_PROTO(const struct xdp_mem_allocator *xa,
		 const struct xdp_rxq_info *rxq),

	TP_ARGS(xa, rxq),

	TP_STRUCT__entry(
		__field(const struct xdp_mem_allocator *,	xa)
		__field(u32,		mem_id)
		__field(u32,		mem_type)
		__field(const void *,	allocator)
		__field(const struct xdp_rxq_info *,		rxq)
		__field(int,		ifindex)
	),

	TP_fast_assign(
		__entry->xa		= xa;
		__entry->mem_id		= xa->mem.id;
		__entry->mem_type	= xa->mem.type;
		__entry->allocator	= xa->allocator;
		__entry->rxq		= rxq;
		__entry->ifindex	= rxq->dev->ifindex;
	),

	TP_printk("mem_id=%d mem_type=%s allocator=%p"
		  " ifindex=%d",
		  __entry->mem_id,
		  __print_symbolic(__entry->mem_type, __MEM_TYPE_SYM_TAB),
		  __entry->allocator,
		  __entry->ifindex
	)
);

TRACE_EVENT(mem_return_failed,

	TP_PROTO(const struct xdp_mem_info *mem,
		 const struct page *page),

	TP_ARGS(mem, page),

	TP_STRUCT__entry(
		__field(const struct page *,	page)
		__field(u32,		mem_id)
		__field(u32,		mem_type)
	),

	TP_fast_assign(
		__entry->page		= page;
		__entry->mem_id		= mem->id;
		__entry->mem_type	= mem->type;
	),

	TP_printk("mem_id=%d mem_type=%s page=%p",
		  __entry->mem_id,
		  __print_symbolic(__entry->mem_type, __MEM_TYPE_SYM_TAB),
		  __entry->page
	)
);

#endif /* _TRACE_XDP_H */

#include <trace/define_trace.h>
