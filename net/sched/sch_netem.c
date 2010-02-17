/*
 * net/sched/sch_netem.c	Network emulator
 *
 * 		This program is free software; you can redistribute it and/or
 * 		modify it under the terms of the GNU General Public License
 * 		as published by the Free Software Foundation; either version
 * 		2 of the License.
 *
 *  		Many of the algorithms and ideas for this came from
 *		NIST Net which is not copyrighted.
 *
 * Authors:	Stephen Hemminger <shemminger@osdl.org>
 *		Catalin(ux aka Dino) BOIE <catab at umbrella dot ro>
 */

#include <linux/module.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/skbuff.h>
#include <linux/rtnetlink.h>

#include <net/netlink.h>
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
*/

struct netem_sched_data {
	struct Qdisc	*qdisc;
	struct qdisc_watchdog watchdog;

	psched_tdiff_t latency;
	psched_tdiff_t jitter;

	u32 loss;
	u32 limit;
	u32 counter;
	u32 gap;
	u32 duplicate;
	u32 reorder;
	u32 corrupt;

	struct crndstate {
		u32 last;
		u32 rho;
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

static inline struct netem_skb_cb *netem_skb_cb(struct sk_buff *skb)
{
	BUILD_BUG_ON(sizeof(skb->cb) <
		sizeof(struct qdisc_skb_cb) + sizeof(struct netem_skb_cb));
	return (struct netem_skb_cb *)qdisc_skb_cb(skb)->data;
}

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
static u32 get_crandom(struct crndstate *state)
{
	u64 value, rho;
	unsigned long answer;

	if (state->rho == 0)	/* no correlation */
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
static psched_tdiff_t tabledist(psched_tdiff_t mu, psched_tdiff_t sigma,
				struct crndstate *state,
				const struct disttable *dist)
{
	psched_tdiff_t x;
	long t;
	u32 rnd;

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
		return NET_XMIT_SUCCESS | __NET_XMIT_BYPASS;
	}

	skb_orphan(skb);

	/*
	 * If we need to duplicate packet, then re-insert at top of the
	 * qdisc tree, since parent queuer expects that only one
	 * skb will be queued.
	 */
	if (count > 1 && (skb2 = skb_clone(skb, GFP_ATOMIC)) != NULL) {
		struct Qdisc *rootq = qdisc_root(sch);
		u32 dupsave = q->duplicate; /* prevent duplicating a dup... */
		q->duplicate = 0;

		qdisc_enqueue_root(skb2, rootq);
		q->duplicate = dupsave;
	}

	/*
	 * Randomized packet corruption.
	 * Make copy if needed since we are modifying
	 * If packet is going to be hardware checksummed, then
	 * do it now in software before we mangle it.
	 */
	if (q->corrupt && q->corrupt >= get_crandom(&q->corrupt_cor)) {
		if (!(skb = skb_unshare(skb, GFP_ATOMIC)) ||
		    (skb->ip_summed == CHECKSUM_PARTIAL &&
		     skb_checksum_help(skb))) {
			sch->qstats.drops++;
			return NET_XMIT_DROP;
		}

		skb->data[net_random() % skb_headlen(skb)] ^= 1<<(net_random() % 8);
	}

	cb = netem_skb_cb(skb);
	if (q->gap == 0 || 		/* not doing reordering */
	    q->counter < q->gap || 	/* inside last reordering gap */
	    q->reorder < get_crandom(&q->reorder_cor)) {
		psched_time_t now;
		psched_tdiff_t delay;

		delay = tabledist(q->latency, q->jitter,
				  &q->delay_cor, q->delay_dist);

		now = psched_get_time();
		cb->time_to_send = now + delay;
		++q->counter;
		ret = qdisc_enqueue(skb, q->qdisc);
	} else {
		/*
		 * Do re-ordering by putting one out of N packets at the front
		 * of the queue.
		 */
		cb->time_to_send = psched_get_time();
		q->counter = 0;

		__skb_queue_head(&q->qdisc->q, skb);
		q->qdisc->qstats.backlog += qdisc_pkt_len(skb);
		q->qdisc->qstats.requeues++;
		ret = NET_XMIT_SUCCESS;
	}

	if (likely(ret == NET_XMIT_SUCCESS)) {
		sch->q.qlen++;
		sch->bstats.bytes += qdisc_pkt_len(skb);
		sch->bstats.packets++;
	} else if (net_xmit_drop_count(ret)) {
		sch->qstats.drops++;
	}

	pr_debug("netem: enqueue ret %d\n", ret);
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

	if (sch->flags & TCQ_F_THROTTLED)
		return NULL;

	skb = q->qdisc->ops->peek(q->qdisc);
	if (skb) {
		const struct netem_skb_cb *cb = netem_skb_cb(skb);
		psched_time_t now = psched_get_time();

		/* if more time remaining? */
		if (cb->time_to_send <= now) {
			skb = qdisc_dequeue_peeked(q->qdisc);
			if (unlikely(!skb))
				return NULL;

#ifdef CONFIG_NET_CLS_ACT
			/*
			 * If it's at ingress let's pretend the delay is
			 * from the network (tstamp will be updated).
			 */
			if (G_TC_FROM(skb->tc_verd) & AT_INGRESS)
				skb->tstamp.tv64 = 0;
#endif
			pr_debug("netem_dequeue: return skb=%p\n", skb);
			sch->q.qlen--;
			return skb;
		}

		qdisc_watchdog_schedule(&q->watchdog, cb->time_to_send);
	}

	return NULL;
}

static void netem_reset(struct Qdisc *sch)
{
	struct netem_sched_data *q = qdisc_priv(sch);

	qdisc_reset(q->qdisc);
	sch->q.qlen = 0;
	qdisc_watchdog_cancel(&q->watchdog);
}

/*
 * Distribution data is a variable size payload containing
 * signed 16 bit values.
 */
static int get_dist_table(struct Qdisc *sch, const struct nlattr *attr)
{
	struct netem_sched_data *q = qdisc_priv(sch);
	unsigned long n = nla_len(attr)/sizeof(__s16);
	const __s16 *data = nla_data(attr);
	spinlock_t *root_lock;
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

	root_lock = qdisc_root_sleeping_lock(sch);

	spin_lock_bh(root_lock);
	kfree(q->delay_dist);
	q->delay_dist = d;
	spin_unlock_bh(root_lock);
	return 0;
}

static void get_correlation(struct Qdisc *sch, const struct nlattr *attr)
{
	struct netem_sched_data *q = qdisc_priv(sch);
	const struct tc_netem_corr *c = nla_data(attr);

	init_crandom(&q->delay_cor, c->delay_corr);
	init_crandom(&q->loss_cor, c->loss_corr);
	init_crandom(&q->dup_cor, c->dup_corr);
}

static void get_reorder(struct Qdisc *sch, const struct nlattr *attr)
{
	struct netem_sched_data *q = qdisc_priv(sch);
	const struct tc_netem_reorder *r = nla_data(attr);

	q->reorder = r->probability;
	init_crandom(&q->reorder_cor, r->correlation);
}

static void get_corrupt(struct Qdisc *sch, const struct nlattr *attr)
{
	struct netem_sched_data *q = qdisc_priv(sch);
	const struct tc_netem_corrupt *r = nla_data(attr);

	q->corrupt = r->probability;
	init_crandom(&q->corrupt_cor, r->correlation);
}

static const struct nla_policy netem_policy[TCA_NETEM_MAX + 1] = {
	[TCA_NETEM_CORR]	= { .len = sizeof(struct tc_netem_corr) },
	[TCA_NETEM_REORDER]	= { .len = sizeof(struct tc_netem_reorder) },
	[TCA_NETEM_CORRUPT]	= { .len = sizeof(struct tc_netem_corrupt) },
};

static int parse_attr(struct nlattr *tb[], int maxtype, struct nlattr *nla,
		      const struct nla_policy *policy, int len)
{
	int nested_len = nla_len(nla) - NLA_ALIGN(len);

	if (nested_len < 0)
		return -EINVAL;
	if (nested_len >= nla_attr_size(0))
		return nla_parse(tb, maxtype, nla_data(nla) + NLA_ALIGN(len),
				 nested_len, policy);
	memset(tb, 0, sizeof(struct nlattr *) * (maxtype + 1));
	return 0;
}

/* Parse netlink message to set options */
static int netem_change(struct Qdisc *sch, struct nlattr *opt)
{
	struct netem_sched_data *q = qdisc_priv(sch);
	struct nlattr *tb[TCA_NETEM_MAX + 1];
	struct tc_netem_qopt *qopt;
	int ret;

	if (opt == NULL)
		return -EINVAL;

	qopt = nla_data(opt);
	ret = parse_attr(tb, TCA_NETEM_MAX, opt, netem_policy, sizeof(*qopt));
	if (ret < 0)
		return ret;

	ret = fifo_set_limit(q->qdisc, qopt->limit);
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

	/* for compatibility with earlier versions.
	 * if gap is set, need to assume 100% probability
	 */
	if (q->gap)
		q->reorder = ~0;

	if (tb[TCA_NETEM_CORR])
		get_correlation(sch, tb[TCA_NETEM_CORR]);

	if (tb[TCA_NETEM_DELAY_DIST]) {
		ret = get_dist_table(sch, tb[TCA_NETEM_DELAY_DIST]);
		if (ret)
			return ret;
	}

	if (tb[TCA_NETEM_REORDER])
		get_reorder(sch, tb[TCA_NETEM_REORDER]);

	if (tb[TCA_NETEM_CORRUPT])
		get_corrupt(sch, tb[TCA_NETEM_CORRUPT]);

	return 0;
}

/*
 * Special case version of FIFO queue for use by netem.
 * It queues in order based on timestamps in skb's
 */
struct fifo_sched_data {
	u32 limit;
	psched_time_t oldest;
};

static int tfifo_enqueue(struct sk_buff *nskb, struct Qdisc *sch)
{
	struct fifo_sched_data *q = qdisc_priv(sch);
	struct sk_buff_head *list = &sch->q;
	psched_time_t tnext = netem_skb_cb(nskb)->time_to_send;
	struct sk_buff *skb;

	if (likely(skb_queue_len(list) < q->limit)) {
		/* Optimize for add at tail */
		if (likely(skb_queue_empty(list) || tnext >= q->oldest)) {
			q->oldest = tnext;
			return qdisc_enqueue_tail(nskb, sch);
		}

		skb_queue_reverse_walk(list, skb) {
			const struct netem_skb_cb *cb = netem_skb_cb(skb);

			if (tnext >= cb->time_to_send)
				break;
		}

		__skb_queue_after(list, skb, nskb);

		sch->qstats.backlog += qdisc_pkt_len(nskb);
		sch->bstats.bytes += qdisc_pkt_len(nskb);
		sch->bstats.packets++;

		return NET_XMIT_SUCCESS;
	}

	return qdisc_reshape_fail(nskb, sch);
}

static int tfifo_init(struct Qdisc *sch, struct nlattr *opt)
{
	struct fifo_sched_data *q = qdisc_priv(sch);

	if (opt) {
		struct tc_fifo_qopt *ctl = nla_data(opt);
		if (nla_len(opt) < sizeof(*ctl))
			return -EINVAL;

		q->limit = ctl->limit;
	} else
		q->limit = max_t(u32, qdisc_dev(sch)->tx_queue_len, 1);

	q->oldest = PSCHED_PASTPERFECT;
	return 0;
}

static int tfifo_dump(struct Qdisc *sch, struct sk_buff *skb)
{
	struct fifo_sched_data *q = qdisc_priv(sch);
	struct tc_fifo_qopt opt = { .limit = q->limit };

	NLA_PUT(skb, TCA_OPTIONS, sizeof(opt), &opt);
	return skb->len;

nla_put_failure:
	return -1;
}

static struct Qdisc_ops tfifo_qdisc_ops __read_mostly = {
	.id		=	"tfifo",
	.priv_size	=	sizeof(struct fifo_sched_data),
	.enqueue	=	tfifo_enqueue,
	.dequeue	=	qdisc_dequeue_head,
	.peek		=	qdisc_peek_head,
	.drop		=	qdisc_queue_drop,
	.init		=	tfifo_init,
	.reset		=	qdisc_reset_queue,
	.change		=	tfifo_init,
	.dump		=	tfifo_dump,
};

static int netem_init(struct Qdisc *sch, struct nlattr *opt)
{
	struct netem_sched_data *q = qdisc_priv(sch);
	int ret;

	if (!opt)
		return -EINVAL;

	qdisc_watchdog_init(&q->watchdog, sch);

	q->qdisc = qdisc_create_dflt(qdisc_dev(sch), sch->dev_queue,
				     &tfifo_qdisc_ops,
				     TC_H_MAKE(sch->handle, 1));
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

	qdisc_watchdog_cancel(&q->watchdog);
	qdisc_destroy(q->qdisc);
	kfree(q->delay_dist);
}

static int netem_dump(struct Qdisc *sch, struct sk_buff *skb)
{
	const struct netem_sched_data *q = qdisc_priv(sch);
	unsigned char *b = skb_tail_pointer(skb);
	struct nlattr *nla = (struct nlattr *) b;
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
	NLA_PUT(skb, TCA_OPTIONS, sizeof(qopt), &qopt);

	cor.delay_corr = q->delay_cor.rho;
	cor.loss_corr = q->loss_cor.rho;
	cor.dup_corr = q->dup_cor.rho;
	NLA_PUT(skb, TCA_NETEM_CORR, sizeof(cor), &cor);

	reorder.probability = q->reorder;
	reorder.correlation = q->reorder_cor.rho;
	NLA_PUT(skb, TCA_NETEM_REORDER, sizeof(reorder), &reorder);

	corrupt.probability = q->corrupt;
	corrupt.correlation = q->corrupt_cor.rho;
	NLA_PUT(skb, TCA_NETEM_CORRUPT, sizeof(corrupt), &corrupt);

	nla->nla_len = skb_tail_pointer(skb) - b;

	return skb->len;

nla_put_failure:
	nlmsg_trim(skb, b);
	return -1;
}

static struct Qdisc_ops netem_qdisc_ops __read_mostly = {
	.id		=	"netem",
	.priv_size	=	sizeof(struct netem_sched_data),
	.enqueue	=	netem_enqueue,
	.dequeue	=	netem_dequeue,
	.peek		=	qdisc_peek_dequeued,
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
