/*
 * net/sched/sch_netem.c	Network emulator
 *
 * 		This program is free software; you can redistribute it and/or
 * 		modify it under the terms of the GNU General Public License
 * 		as published by the Free Software Foundation; either version
 * 		2 of the License, or (at your option) any later version.
 *
 *  		Many of the algorithms and ideas for this came from
 *		NIST Net which is not copyrighted. 
 *
 * Authors:	Stephen Hemminger <shemminger@osdl.org>
 *		Catalin(ux aka Dino) BOIE <catab at umbrella dot ro>
 */

#include <linux/module.h>
#include <linux/bitops.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/netdevice.h>
#include <linux/skbuff.h>
#include <linux/rtnetlink.h>

#include <net/pkt_sched.h>

#define VERSION "1.2"

/*	Network Emulation Queuing algorithm.
	====================================

	Sources: [1] Mark Carson, Darrin Santay, "NIST Net - A Linux-based
		 Network Emulation Tool
		 [2] Luigi Rizzo, DummyNet for FreeBSD

	 ----------------------------------------------------------------

	 This started out as a simple way to delay outgoing packets to
	 test TCP but has grown to include most of the functionality
	 of a full blown network emulator like NISTnet. It can delay
	 packets and add random jitter (and correlation). The random
	 distribution can be loaded from a table as well to provide
	 normal, Pareto, or experimental curves. Packet loss,
	 duplication, and reordering can also be emulated.

	 This qdisc does not do classification that can be handled in
	 layering other disciplines.  It does not need to do bandwidth
	 control either since that can be handled by using token
	 bucket or other rate control.

	 The simulator is limited by the Linux timer resolution
	 and will create packet bursts on the HZ boundary (1ms).
*/

struct netem_sched_data {
	struct Qdisc	*qdisc;
	struct timer_list timer;

	u32 latency;
	u32 loss;
	u32 limit;
	u32 counter;
	u32 gap;
	u32 jitter;
	u32 duplicate;
	u32 reorder;
	u32 corrupt;

	struct crndstate {
		unsigned long last;
		unsigned long rho;
	} delay_cor, loss_cor, dup_cor, reorder_cor, corrupt_cor;

	struct disttable {
		u32  size;
		s16 table[0];
	} *delay_dist;
};

/* Time stamp put into socket buffer control block */
struct netem_skb_cb {
	psched_time_t	time_to_send;
};

/* init_crandom - initialize correlated random number generator
 * Use entropy source for initial seed.
 */
static void init_crandom(struct crndstate *state, unsigned long rho)
{
	state->rho = rho;
	state->last = net_random();
}

/* get_crandom - correlated random number generator
 * Next number depends on last value.
 * rho is scaled to avoid floating point.
 */
static unsigned long get_crandom(struct crndstate *state)
{
	u64 value, rho;
	unsigned long answer;

	if (state->rho == 0)	/* no correllation */
		return net_random();

	value = net_random();
	rho = (u64)state->rho + 1;
	answer = (value * ((1ull<<32) - rho) + state->last * rho) >> 32;
	state->last = answer;
	return answer;
}

/* tabledist - return a pseudo-randomly distributed value with mean mu and
 * std deviation sigma.  Uses table lookup to approximate the desired
 * distribution, and a uniformly-distributed pseudo-random source.
 */
static long tabledist(unsigned long mu, long sigma, 
		      struct crndstate *state, const struct disttable *dist)
{
	long t, x;
	unsigned long rnd;

	if (sigma == 0)
		return mu;

	rnd = get_crandom(state);

	/* default uniform distribution */
	if (dist == NULL) 
		return (rnd % (2*sigma)) - sigma + mu;

	t = dist->table[rnd % dist->size];
	x = (sigma % NETEM_DIST_SCALE) * t;
	if (x >= 0)
		x += NETEM_DIST_SCALE/2;
	else
		x -= NETEM_DIST_SCALE/2;

	return  x / NETEM_DIST_SCALE + (sigma / NETEM_DIST_SCALE) * t + mu;
}

/*
 * Insert one skb into qdisc.
 * Note: parent depends on return value to account for queue length.
 * 	NET_XMIT_DROP: queue length didn't change.
 *      NET_XMIT_SUCCESS: one skb was queued.
 */
static int netem_enqueue(struct sk_buff *skb, struct Qdisc *sch)
{
	struct netem_sched_data *q = qdisc_priv(sch);
	/* We don't fill cb now as skb_unshare() may invalidate it */
	struct netem_skb_cb *cb;
	struct sk_buff *skb2;
	int ret;
	int count = 1;

	pr_debug("netem_enqueue skb=%p\n", skb);

	/* Random duplication */
	if (q->duplicate && q->duplicate >= get_crandom(&q->dup_cor))
		++count;

	/* Random packet drop 0 => none, ~0 => all */
	if (q->loss && q->loss >= get_crandom(&q->loss_cor))
		--count;

	if (count == 0) {
		sch->qstats.drops++;
		kfree_skb(skb);
		return NET_XMIT_BYPASS;
	}

	skb_orphan(skb);

	/*
	 * If we need to duplicate packet, then re-insert at top of the
	 * qdisc tree, since parent queuer expects that only one
	 * skb will be queued.
	 */
	if (count > 1 && (skb2 = skb_clone(skb, GFP_ATOMIC)) != NULL) {
		struct Qdisc *rootq = sch->dev->qdisc;
		u32 dupsave = q->duplicate; /* prevent duplicating a dup... */
		q->duplicate = 0;

		rootq->enqueue(skb2, rootq);
		q->duplicate = dupsave;
	}

	/*
	 * Randomized packet corruption.
	 * Make copy if needed since we are modifying
	 * If packet is going to be hardware checksummed, then
	 * do it now in software before we mangle it.
	 */
	if (q->corrupt && q->corrupt >= get_crandom(&q->corrupt_cor)) {
		if (!(skb = skb_unshare(skb, GFP_ATOMIC))
		    || (skb->ip_summed == CHECKSUM_PARTIAL
			&& skb_checksum_help(skb))) {
			sch->qstats.drops++;
			return NET_XMIT_DROP;
		}

		skb->data[net_random() % skb_headlen(skb)] ^= 1<<(net_random() % 8);
	}

	cb = (struct netem_skb_cb *)skb->cb;
	if (q->gap == 0 		/* not doing reordering */
	    || q->counter < q->gap 	/* inside last reordering gap */
	    || q->reorder < get_crandom(&q->reorder_cor)) {
		psched_time_t now;
		psched_tdiff_t delay;

		delay = tabledist(q->latency, q->jitter,
				  &q->delay_cor, q->delay_dist);

		PSCHED_GET_TIME(now);
		PSCHED_TADD2(now, delay, cb->time_to_send);
		++q->counter;
		ret = q->qdisc->enqueue(skb, q->qdisc);
	} else {
		/* 
		 * Do re-ordering by putting one out of N packets at the front
		 * of the queue.
		 */
		PSCHED_GET_TIME(cb->time_to_send);
		q->counter = 0;
		ret = q->qdisc->ops->requeue(skb, q->qdisc);
	}

	if (likely(ret == NET_XMIT_SUCCESS)) {
		sch->q.qlen++;
		sch->bstats.bytes += skb->len;
		sch->bstats.packets++;
	} else
		sch->qstats.drops++;

	pr_debug("netem: enqueue ret %d\n", ret);
	return ret;
}

/* Requeue packets but don't change time stamp */
static int netem_requeue(struct sk_buff *skb, struct Qdisc *sch)
{
	struct netem_sched_data *q = qdisc_priv(sch);
	int ret;

	if ((ret = q->qdisc->ops->requeue(skb, q->qdisc)) == 0) {
		sch->q.qlen++;
		sch->qstats.requeues++;
	}

	return ret;
}

static unsigned int netem_drop(struct Qdisc* sch)
{
	struct netem_sched_data *q = qdisc_priv(sch);
	unsigned int len = 0;

	if (q->qdisc->ops->drop && (len = q->qdisc->ops->drop(q->qdisc)) != 0) {
		sch->q.qlen--;
		sch->qstats.drops++;
	}
	return len;
}

static struct sk_buff *netem_dequeue(struct Qdisc *sch)
{
	struct netem_sched_data *q = qdisc_priv(sch);
	struct sk_buff *skb;

	skb = q->qdisc->dequeue(q->qdisc);
	if (skb) {
		const struct netem_skb_cb *cb
			= (const struct netem_skb_cb *)skb->cb;
		psched_time_t now;

		/* if more time remaining? */
		PSCHED_GET_TIME(now);

		if (PSCHED_TLESS(cb->time_to_send, now)) {
			pr_debug("netem_dequeue: return skb=%p\n", skb);
			sch->q.qlen--;
			sch->flags &= ~TCQ_F_THROTTLED;
			return skb;
		} else {
			psched_tdiff_t delay = PSCHED_TDIFF(cb->time_to_send, now);

			if (q->qdisc->ops->requeue(skb, q->qdisc) != NET_XMIT_SUCCESS) {
				sch->qstats.drops++;

				/* After this qlen is confused */
				printk(KERN_ERR "netem: queue discpline %s could not requeue\n",
				       q->qdisc->ops->id);

				sch->q.qlen--;
			}

			mod_timer(&q->timer, jiffies + PSCHED_US2JIFFIE(delay));
			sch->flags |= TCQ_F_THROTTLED;
		}
	}

	return NULL;
}

static void netem_watchdog(unsigned long arg)
{
	struct Qdisc *sch = (struct Qdisc *)arg;

	pr_debug("netem_watchdog qlen=%d\n", sch->q.qlen);
	sch->flags &= ~TCQ_F_THROTTLED;
	netif_schedule(sch->dev);
}

static void netem_reset(struct Qdisc *sch)
{
	struct netem_sched_data *q = qdisc_priv(sch);

	qdisc_reset(q->qdisc);
	sch->q.qlen = 0;
	sch->flags &= ~TCQ_F_THROTTLED;
	del_timer_sync(&q->timer);
}

/* Pass size change message down to embedded FIFO */
static int set_fifo_limit(struct Qdisc *q, int limit)
{
        struct rtattr *rta;
	int ret = -ENOMEM;

	/* Hack to avoid sending change message to non-FIFO */
	if (strncmp(q->ops->id + 1, "fifo", 4) != 0)
		return 0;

	rta = kmalloc(RTA_LENGTH(sizeof(struct tc_fifo_qopt)), GFP_KERNEL);
	if (rta) {
		rta->rta_type = RTM_NEWQDISC;
		rta->rta_len = RTA_LENGTH(sizeof(struct tc_fifo_qopt)); 
		((struct tc_fifo_qopt *)RTA_DATA(rta))->limit = limit;
		
		ret = q->ops->change(q, rta);
		kfree(rta);
	}
	return ret;
}

/*
 * Distribution data is a variable size payload containing
 * signed 16 bit values.
 */
static int get_dist_table(struct Qdisc *sch, const struct rtattr *attr)
{
	struct netem_sched_data *q = qdisc_priv(sch);
	unsigned long n = RTA_PAYLOAD(attr)/sizeof(__s16);
	const __s16 *data = RTA_DATA(attr);
	struct disttable *d;
	int i;

	if (n > 65536)
		return -EINVAL;

	d = kmalloc(sizeof(*d) + n*sizeof(d->table[0]), GFP_KERNEL);
	if (!d)
		return -ENOMEM;

	d->size = n;
	for (i = 0; i < n; i++)
		d->table[i] = data[i];
	
	spin_lock_bh(&sch->dev->queue_lock);
	d = xchg(&q->delay_dist, d);
	spin_unlock_bh(&sch->dev->queue_lock);

	kfree(d);
	return 0;
}

static int get_correlation(struct Qdisc *sch, const struct rtattr *attr)
{
	struct netem_sched_data *q = qdisc_priv(sch);
	const struct tc_netem_corr *c = RTA_DATA(attr);

	if (RTA_PAYLOAD(attr) != sizeof(*c))
		return -EINVAL;

	init_crandom(&q->delay_cor, c->delay_corr);
	init_crandom(&q->loss_cor, c->loss_corr);
	init_crandom(&q->dup_cor, c->dup_corr);
	return 0;
}

static int get_reorder(struct Qdisc *sch, const struct rtattr *attr)
{
	struct netem_sched_data *q = qdisc_priv(sch);
	const struct tc_netem_reorder *r = RTA_DATA(attr);

	if (RTA_PAYLOAD(attr) != sizeof(*r))
		return -EINVAL;

	q->reorder = r->probability;
	init_crandom(&q->reorder_cor, r->correlation);
	return 0;
}

static int get_corrupt(struct Qdisc *sch, const struct rtattr *attr)
{
	struct netem_sched_data *q = qdisc_priv(sch);
	const struct tc_netem_corrupt *r = RTA_DATA(attr);

	if (RTA_PAYLOAD(attr) != sizeof(*r))
		return -EINVAL;

	q->corrupt = r->probability;
	init_crandom(&q->corrupt_cor, r->correlation);
	return 0;
}

/* Parse netlink message to set options */
static int netem_change(struct Qdisc *sch, struct rtattr *opt)
{
	struct netem_sched_data *q = qdisc_priv(sch);
	struct tc_netem_qopt *qopt;
	int ret;
	
	if (opt == NULL || RTA_PAYLOAD(opt) < sizeof(*qopt))
		return -EINVAL;

	qopt = RTA_DATA(opt);
	ret = set_fifo_limit(q->qdisc, qopt->limit);
	if (ret) {
		pr_debug("netem: can't set fifo limit\n");
		return ret;
	}
	
	q->latency = qopt->latency;
	q->jitter = qopt->jitter;
	q->limit = qopt->limit;
	q->gap = qopt->gap;
	q->counter = 0;
	q->loss = qopt->loss;
	q->duplicate = qopt->duplicate;

	/* for compatiablity with earlier versions.
	 * if gap is set, need to assume 100% probablity
	 */
	q->reorder = ~0;

	/* Handle nested options after initial queue options.
	 * Should have put all options in nested format but too late now.
	 */ 
	if (RTA_PAYLOAD(opt) > sizeof(*qopt)) {
		struct rtattr *tb[TCA_NETEM_MAX];
		if (rtattr_parse(tb, TCA_NETEM_MAX, 
				 RTA_DATA(opt) + sizeof(*qopt),
				 RTA_PAYLOAD(opt) - sizeof(*qopt)))
			return -EINVAL;

		if (tb[TCA_NETEM_CORR-1]) {
			ret = get_correlation(sch, tb[TCA_NETEM_CORR-1]);
			if (ret)
				return ret;
		}

		if (tb[TCA_NETEM_DELAY_DIST-1]) {
			ret = get_dist_table(sch, tb[TCA_NETEM_DELAY_DIST-1]);
			if (ret)
				return ret;
		}

		if (tb[TCA_NETEM_REORDER-1]) {
			ret = get_reorder(sch, tb[TCA_NETEM_REORDER-1]);
			if (ret)
				return ret;
		}

		if (tb[TCA_NETEM_CORRUPT-1]) {
			ret = get_corrupt(sch, tb[TCA_NETEM_CORRUPT-1]);
			if (ret)
				return ret;
		}
	}

	return 0;
}

/*
 * Special case version of FIFO queue for use by netem.
 * It queues in order based on timestamps in skb's
 */
struct fifo_sched_data {
	u32 limit;
};

static int tfifo_enqueue(struct sk_buff *nskb, struct Qdisc *sch)
{
	struct fifo_sched_data *q = qdisc_priv(sch);
	struct sk_buff_head *list = &sch->q;
	const struct netem_skb_cb *ncb
		= (const struct netem_skb_cb *)nskb->cb;
	struct sk_buff *skb;

	if (likely(skb_queue_len(list) < q->limit)) {
		skb_queue_reverse_walk(list, skb) {
			const struct netem_skb_cb *cb
				= (const struct netem_skb_cb *)skb->cb;

			if (!PSCHED_TLESS(ncb->time_to_send, cb->time_to_send))
				break;
		}

		__skb_queue_after(list, skb, nskb);

		sch->qstats.backlog += nskb->len;
		sch->bstats.bytes += nskb->len;
		sch->bstats.packets++;

		return NET_XMIT_SUCCESS;
	}

	return qdisc_drop(nskb, sch);
}

static int tfifo_init(struct Qdisc *sch, struct rtattr *opt)
{
	struct fifo_sched_data *q = qdisc_priv(sch);

	if (opt) {
		struct tc_fifo_qopt *ctl = RTA_DATA(opt);
		if (RTA_PAYLOAD(opt) < sizeof(*ctl))
			return -EINVAL;

		q->limit = ctl->limit;
	} else
		q->limit = max_t(u32, sch->dev->tx_queue_len, 1);

	return 0;
}

static int tfifo_dump(struct Qdisc *sch, struct sk_buff *skb)
{
	struct fifo_sched_data *q = qdisc_priv(sch);
	struct tc_fifo_qopt opt = { .limit = q->limit };

	RTA_PUT(skb, TCA_OPTIONS, sizeof(opt), &opt);
	return skb->len;

rtattr_failure:
	return -1;
}

static struct Qdisc_ops tfifo_qdisc_ops = {
	.id		=	"tfifo",
	.priv_size	=	sizeof(struct fifo_sched_data),
	.enqueue	=	tfifo_enqueue,
	.dequeue	=	qdisc_dequeue_head,
	.requeue	=	qdisc_requeue,
	.drop		=	qdisc_queue_drop,
	.init		=	tfifo_init,
	.reset		=	qdisc_reset_queue,
	.change		=	tfifo_init,
	.dump		=	tfifo_dump,
};

static int netem_init(struct Qdisc *sch, struct rtattr *opt)
{
	struct netem_sched_data *q = qdisc_priv(sch);
	int ret;

	if (!opt)
		return -EINVAL;

	init_timer(&q->timer);
	q->timer.function = netem_watchdog;
	q->timer.data = (unsigned long) sch;

	q->qdisc = qdisc_create_dflt(sch->dev, &tfifo_qdisc_ops);
	if (!q->qdisc) {
		pr_debug("netem: qdisc create failed\n");
		return -ENOMEM;
	}

	ret = netem_change(sch, opt);
	if (ret) {
		pr_debug("netem: change failed\n");
		qdisc_destroy(q->qdisc);
	}
	return ret;
}

static void netem_destroy(struct Qdisc *sch)
{
	struct netem_sched_data *q = qdisc_priv(sch);

	del_timer_sync(&q->timer);
	qdisc_destroy(q->qdisc);
	kfree(q->delay_dist);
}

static int netem_dump(struct Qdisc *sch, struct sk_buff *skb)
{
	const struct netem_sched_data *q = qdisc_priv(sch);
	unsigned char	 *b = skb->tail;
	struct rtattr *rta = (struct rtattr *) b;
	struct tc_netem_qopt qopt;
	struct tc_netem_corr cor;
	struct tc_netem_reorder reorder;
	struct tc_netem_corrupt corrupt;

	qopt.latency = q->latency;
	qopt.jitter = q->jitter;
	qopt.limit = q->limit;
	qopt.loss = q->loss;
	qopt.gap = q->gap;
	qopt.duplicate = q->duplicate;
	RTA_PUT(skb, TCA_OPTIONS, sizeof(qopt), &qopt);

	cor.delay_corr = q->delay_cor.rho;
	cor.loss_corr = q->loss_cor.rho;
	cor.dup_corr = q->dup_cor.rho;
	RTA_PUT(skb, TCA_NETEM_CORR, sizeof(cor), &cor);

	reorder.probability = q->reorder;
	reorder.correlation = q->reorder_cor.rho;
	RTA_PUT(skb, TCA_NETEM_REORDER, sizeof(reorder), &reorder);

	corrupt.probability = q->corrupt;
	corrupt.correlation = q->corrupt_cor.rho;
	RTA_PUT(skb, TCA_NETEM_CORRUPT, sizeof(corrupt), &corrupt);

	rta->rta_len = skb->tail - b;

	return skb->len;

rtattr_failure:
	skb_trim(skb, b - skb->data);
	return -1;
}

static int netem_dump_class(struct Qdisc *sch, unsigned long cl,
			  struct sk_buff *skb, struct tcmsg *tcm)
{
	struct netem_sched_data *q = qdisc_priv(sch);

	if (cl != 1) 	/* only one class */
		return -ENOENT;

	tcm->tcm_handle |= TC_H_MIN(1);
	tcm->tcm_info = q->qdisc->handle;

	return 0;
}

static int netem_graft(struct Qdisc *sch, unsigned long arg, struct Qdisc *new,
		     struct Qdisc **old)
{
	struct netem_sched_data *q = qdisc_priv(sch);

	if (new == NULL)
		new = &noop_qdisc;

	sch_tree_lock(sch);
	*old = xchg(&q->qdisc, new);
	qdisc_reset(*old);
	sch->q.qlen = 0;
	sch_tree_unlock(sch);

	return 0;
}

static struct Qdisc *netem_leaf(struct Qdisc *sch, unsigned long arg)
{
	struct netem_sched_data *q = qdisc_priv(sch);
	return q->qdisc;
}

static unsigned long netem_get(struct Qdisc *sch, u32 classid)
{
	return 1;
}

static void netem_put(struct Qdisc *sch, unsigned long arg)
{
}

static int netem_change_class(struct Qdisc *sch, u32 classid, u32 parentid, 
			    struct rtattr **tca, unsigned long *arg)
{
	return -ENOSYS;
}

static int netem_delete(struct Qdisc *sch, unsigned long arg)
{
	return -ENOSYS;
}

static void netem_walk(struct Qdisc *sch, struct qdisc_walker *walker)
{
	if (!walker->stop) {
		if (walker->count >= walker->skip)
			if (walker->fn(sch, 1, walker) < 0) {
				walker->stop = 1;
				return;
			}
		walker->count++;
	}
}

static struct tcf_proto **netem_find_tcf(struct Qdisc *sch, unsigned long cl)
{
	return NULL;
}

static struct Qdisc_class_ops netem_class_ops = {
	.graft		=	netem_graft,
	.leaf		=	netem_leaf,
	.get		=	netem_get,
	.put		=	netem_put,
	.change		=	netem_change_class,
	.delete		=	netem_delete,
	.walk		=	netem_walk,
	.tcf_chain	=	netem_find_tcf,
	.dump		=	netem_dump_class,
};

static struct Qdisc_ops netem_qdisc_ops = {
	.id		=	"netem",
	.cl_ops		=	&netem_class_ops,
	.priv_size	=	sizeof(struct netem_sched_data),
	.enqueue	=	netem_enqueue,
	.dequeue	=	netem_dequeue,
	.requeue	=	netem_requeue,
	.drop		=	netem_drop,
	.init		=	netem_init,
	.reset		=	netem_reset,
	.destroy	=	netem_destroy,
	.change		=	netem_change,
	.dump		=	netem_dump,
	.owner		=	THIS_MODULE,
};


static int __init netem_module_init(void)
{
	pr_info("netem: version " VERSION "\n");
	return register_qdisc(&netem_qdisc_ops);
}
static void __exit netem_module_exit(void)
{
	unregister_qdisc(&netem_qdisc_ops);
}
module_init(netem_module_init)
module_exit(netem_module_exit)
MODULE_LICENSE("GPL");
