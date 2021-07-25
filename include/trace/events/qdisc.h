#undef TRACE_SYSTEM
#define TRACE_SYSTEM qdisc

#if !defined(_TRACE_QDISC_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_QDISC_H

#include <linux/skbuff.h>
#include <linux/netdevice.h>
#include <linux/tracepoint.h>
#include <linux/ftrace.h>
#include <linux/pkt_sched.h>
#include <net/sch_generic.h>

TRACE_EVENT(qdisc_dequeue,

	TP_PROTO(struct Qdisc *qdisc, const struct netdev_queue *txq,
		 int packets, struct sk_buff *skb),

	TP_ARGS(qdisc, txq, packets, skb),

	TP_STRUCT__entry(
		__field(	struct Qdisc *,		qdisc	)
		__field(const	struct netdev_queue *,	txq	)
		__field(	int,			packets	)
		__field(	void *,			skbaddr	)
		__field(	int,			ifindex	)
		__field(	u32,			handle	)
		__field(	u32,			parent	)
		__field(	unsigned long,		txq_state)
	),

	/* skb==NULL indicate packets dequeued was 0, even when packets==1 */
	TP_fast_assign(
		__entry->qdisc		= qdisc;
		__entry->txq		= txq;
		__entry->packets	= skb ? packets : 0;
		__entry->skbaddr	= skb;
		__entry->ifindex	= txq->dev ? txq->dev->ifindex : 0;
		__entry->handle		= qdisc->handle;
		__entry->parent		= qdisc->parent;
		__entry->txq_state	= txq->state;
	),

	TP_printk("dequeue ifindex=%d qdisc handle=0x%X parent=0x%X txq_state=0x%lX packets=%d skbaddr=%px",
		  __entry->ifindex, __entry->handle, __entry->parent,
		  __entry->txq_state, __entry->packets, __entry->skbaddr )
);

TRACE_EVENT(qdisc_enqueue,

	TP_PROTO(struct Qdisc *qdisc, const struct netdev_queue *txq, struct sk_buff *skb),

	TP_ARGS(qdisc, txq, skb),

	TP_STRUCT__entry(
		__field(struct Qdisc *, qdisc)
		__field(void *,	skbaddr)
		__field(int, ifindex)
		__field(u32, handle)
		__field(u32, parent)
	),

	TP_fast_assign(
		__entry->qdisc = qdisc;
		__entry->skbaddr = skb;
		__entry->ifindex = txq->dev ? txq->dev->ifindex : 0;
		__entry->handle	 = qdisc->handle;
		__entry->parent	 = qdisc->parent;
	),

	TP_printk("enqueue ifindex=%d qdisc handle=0x%X parent=0x%X skbaddr=%px",
		  __entry->ifindex, __entry->handle, __entry->parent, __entry->skbaddr)
);

TRACE_EVENT(qdisc_reset,

	TP_PROTO(struct Qdisc *q),

	TP_ARGS(q),

	TP_STRUCT__entry(
		__string(	dev,		qdisc_dev(q)	)
		__string(	kind,		q->ops->id	)
		__field(	u32,		parent		)
		__field(	u32,		handle		)
	),

	TP_fast_assign(
		__assign_str(dev, qdisc_dev(q));
		__assign_str(kind, q->ops->id);
		__entry->parent = q->parent;
		__entry->handle = q->handle;
	),

	TP_printk("dev=%s kind=%s parent=%x:%x handle=%x:%x", __get_str(dev),
		  __get_str(kind), TC_H_MAJ(__entry->parent) >> 16, TC_H_MIN(__entry->parent),
		  TC_H_MAJ(__entry->handle) >> 16, TC_H_MIN(__entry->handle))
);

TRACE_EVENT(qdisc_destroy,

	TP_PROTO(struct Qdisc *q),

	TP_ARGS(q),

	TP_STRUCT__entry(
		__string(	dev,		qdisc_dev(q)	)
		__string(	kind,		q->ops->id	)
		__field(	u32,		parent		)
		__field(	u32,		handle		)
	),

	TP_fast_assign(
		__assign_str(dev, qdisc_dev(q));
		__assign_str(kind, q->ops->id);
		__entry->parent = q->parent;
		__entry->handle = q->handle;
	),

	TP_printk("dev=%s kind=%s parent=%x:%x handle=%x:%x", __get_str(dev),
		  __get_str(kind), TC_H_MAJ(__entry->parent) >> 16, TC_H_MIN(__entry->parent),
		  TC_H_MAJ(__entry->handle) >> 16, TC_H_MIN(__entry->handle))
);

TRACE_EVENT(qdisc_create,

	TP_PROTO(const struct Qdisc_ops *ops, struct net_device *dev, u32 parent),

	TP_ARGS(ops, dev, parent),

	TP_STRUCT__entry(
		__string(	dev,		dev->name	)
		__string(	kind,		ops->id		)
		__field(	u32,		parent		)
	),

	TP_fast_assign(
		__assign_str(dev, dev->name);
		__assign_str(kind, ops->id);
		__entry->parent = parent;
	),

	TP_printk("dev=%s kind=%s parent=%x:%x",
		  __get_str(dev), __get_str(kind),
		  TC_H_MAJ(__entry->parent) >> 16, TC_H_MIN(__entry->parent))
);

#endif /* _TRACE_QDISC_H */

/* This part must be outside protection */
#include <trace/define_trace.h>
