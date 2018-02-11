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
	__XDP_ACT_MAP(__XDP_ACT_SYM_FN) { -1, 0 }
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

#define devmap_ifindex(fwd, map)				\
	(!fwd ? 0 :						\
	 (!map ? 0 :						\
	  ((map->map_type == BPF_MAP_TYPE_DEVMAP) ?		\
	   ((struct net_device *)fwd)->ifindex : 0)))

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

#endif /* _TRACE_XDP_H */

#include <trace/define_trace.h>
