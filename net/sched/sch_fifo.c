/*
 * net/sched/sch_fifo.c	The simplest FIFO queue.
 *
 *		This program is free software; you can redistribute it and/or
 *		modify it under the terms of the GNU General Public License
 *		as published by the Free Software Foundation; either version
 *		2 of the License, or (at your option) any later version.
 *
 * Authors:	Alexey Kuznetsov, <kuznet@ms2.inr.ac.ru>
 */

#include <linux/config.h>
#include <linux/module.h>
#include <asm/uaccess.h>
#include <asm/system.h>
#include <linux/bitops.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/string.h>
#include <linux/mm.h>
#include <linux/socket.h>
#include <linux/sockios.h>
#include <linux/in.h>
#include <linux/errno.h>
#include <linux/interrupt.h>
#include <linux/if_ether.h>
#include <linux/inet.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/notifier.h>
#include <net/ip.h>
#include <net/route.h>
#include <linux/skbuff.h>
#include <net/sock.h>
#include <net/pkt_sched.h>

/* 1 band FIFO pseudo-"scheduler" */

struct fifo_sched_data
{
	unsigned limit;
};

static int
bfifo_enqueue(struct sk_buff *skb, struct Qdisc* sch)
{
	struct fifo_sched_data *q = qdisc_priv(sch);

	if (likely(sch->qstats.backlog + skb->len <= q->limit))
		return qdisc_enqueue_tail(skb, sch);

	return qdisc_reshape_fail(skb, sch);
}

static int
pfifo_enqueue(struct sk_buff *skb, struct Qdisc* sch)
{
	struct fifo_sched_data *q = qdisc_priv(sch);

	if (likely(skb_queue_len(&sch->q) < q->limit))
		return qdisc_enqueue_tail(skb, sch);

	return qdisc_reshape_fail(skb, sch);
}

static int fifo_init(struct Qdisc *sch, struct rtattr *opt)
{
	struct fifo_sched_data *q = qdisc_priv(sch);

	if (opt == NULL) {
		unsigned int limit = sch->dev->tx_queue_len ? : 1;

		if (sch->ops == &bfifo_qdisc_ops)
			q->limit = limit*sch->dev->mtu;
		else	
			q->limit = limit;
	} else {
		struct tc_fifo_qopt *ctl = RTA_DATA(opt);
		if (opt->rta_len < RTA_LENGTH(sizeof(*ctl)))
			return -EINVAL;
		q->limit = ctl->limit;
	}
	return 0;
}

static int fifo_dump(struct Qdisc *sch, struct sk_buff *skb)
{
	struct fifo_sched_data *q = qdisc_priv(sch);
	unsigned char	 *b = skb->tail;
	struct tc_fifo_qopt opt;

	opt.limit = q->limit;
	RTA_PUT(skb, TCA_OPTIONS, sizeof(opt), &opt);

	return skb->len;

rtattr_failure:
	skb_trim(skb, b - skb->data);
	return -1;
}

struct Qdisc_ops pfifo_qdisc_ops = {
	.next		=	NULL,
	.cl_ops		=	NULL,
	.id		=	"pfifo",
	.priv_size	=	sizeof(struct fifo_sched_data),
	.enqueue	=	pfifo_enqueue,
	.dequeue	=	qdisc_dequeue_head,
	.requeue	=	qdisc_requeue,
	.drop		=	qdisc_queue_drop,
	.init		=	fifo_init,
	.reset		=	qdisc_reset_queue,
	.destroy	=	NULL,
	.change		=	fifo_init,
	.dump		=	fifo_dump,
	.owner		=	THIS_MODULE,
};

struct Qdisc_ops bfifo_qdisc_ops = {
	.next		=	NULL,
	.cl_ops		=	NULL,
	.id		=	"bfifo",
	.priv_size	=	sizeof(struct fifo_sched_data),
	.enqueue	=	bfifo_enqueue,
	.dequeue	=	qdisc_dequeue_head,
	.requeue	=	qdisc_requeue,
	.drop		=	qdisc_queue_drop,
	.init		=	fifo_init,
	.reset		=	qdisc_reset_queue,
	.destroy	=	NULL,
	.change		=	fifo_init,
	.dump		=	fifo_dump,
	.owner		=	THIS_MODULE,
};

EXPORT_SYMBOL(bfifo_qdisc_ops);
EXPORT_SYMBOL(pfifo_qdisc_ops);
