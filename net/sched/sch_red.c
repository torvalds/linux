/*
 * net/sched/sch_red.c	Random Early Detection queue.
 *
 *		This program is free software; you can redistribute it and/or
 *		modify it under the terms of the GNU General Public License
 *		as published by the Free Software Foundation; either version
 *		2 of the License, or (at your option) any later version.
 *
 * Authors:	Alexey Kuznetsov, <kuznet@ms2.inr.ac.ru>
 *
 * Changes:
 * J Hadi Salim 980914:	computation fixes
 * Alexey Makarenko <makar@phoenix.kharkov.ua> 990814: qave on idle link was calculated incorrectly.
 * J Hadi Salim 980816:  ECN support
 */

#include <linux/config.h>
#include <linux/module.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/netdevice.h>
#include <linux/skbuff.h>
#include <net/pkt_sched.h>
#include <net/inet_ecn.h>
#include <net/red.h>


/*	Parameters, settable by user:
	-----------------------------

	limit		- bytes (must be > qth_max + burst)

	Hard limit on queue length, should be chosen >qth_max
	to allow packet bursts. This parameter does not
	affect the algorithms behaviour and can be chosen
	arbitrarily high (well, less than ram size)
	Really, this limit will never be reached
	if RED works correctly.
 */

struct red_sched_data
{
	u32			limit;		/* HARD maximal queue length */
	unsigned char		flags;
	struct red_parms	parms;
	struct red_stats	stats;
};

static inline int red_use_ecn(struct red_sched_data *q)
{
	return q->flags & TC_RED_ECN;
}

static inline int red_use_harddrop(struct red_sched_data *q)
{
	return q->flags & TC_RED_HARDDROP;
}

static int red_enqueue(struct sk_buff *skb, struct Qdisc* sch)
{
	struct red_sched_data *q = qdisc_priv(sch);

	q->parms.qavg = red_calc_qavg(&q->parms, sch->qstats.backlog);

	if (red_is_idling(&q->parms))
		red_end_of_idle_period(&q->parms);

	switch (red_action(&q->parms, q->parms.qavg)) {
		case RED_DONT_MARK:
			break;

		case RED_PROB_MARK:
			sch->qstats.overlimits++;
			if (!red_use_ecn(q) || !INET_ECN_set_ce(skb)) {
				q->stats.prob_drop++;
				goto congestion_drop;
			}

			q->stats.prob_mark++;
			break;

		case RED_HARD_MARK:
			sch->qstats.overlimits++;
			if (red_use_harddrop(q) || !red_use_ecn(q) ||
			    !INET_ECN_set_ce(skb)) {
				q->stats.forced_drop++;
				goto congestion_drop;
			}

			q->stats.forced_mark++;
			break;
	}

	if (sch->qstats.backlog + skb->len <= q->limit)
		return qdisc_enqueue_tail(skb, sch);

	q->stats.pdrop++;
	return qdisc_drop(skb, sch);

congestion_drop:
	qdisc_drop(skb, sch);
	return NET_XMIT_CN;
}

static int red_requeue(struct sk_buff *skb, struct Qdisc* sch)
{
	struct red_sched_data *q = qdisc_priv(sch);

	if (red_is_idling(&q->parms))
		red_end_of_idle_period(&q->parms);

	return qdisc_requeue(skb, sch);
}

static struct sk_buff * red_dequeue(struct Qdisc* sch)
{
	struct sk_buff *skb;
	struct red_sched_data *q = qdisc_priv(sch);

	skb = qdisc_dequeue_head(sch);

	if (skb == NULL && !red_is_idling(&q->parms))
		red_start_of_idle_period(&q->parms);

	return skb;
}

static unsigned int red_drop(struct Qdisc* sch)
{
	struct sk_buff *skb;
	struct red_sched_data *q = qdisc_priv(sch);

	skb = qdisc_dequeue_tail(sch);
	if (skb) {
		unsigned int len = skb->len;
		q->stats.other++;
		qdisc_drop(skb, sch);
		return len;
	}

	if (!red_is_idling(&q->parms))
		red_start_of_idle_period(&q->parms);

	return 0;
}

static void red_reset(struct Qdisc* sch)
{
	struct red_sched_data *q = qdisc_priv(sch);

	qdisc_reset_queue(sch);
	red_restart(&q->parms);
}

static int red_change(struct Qdisc *sch, struct rtattr *opt)
{
	struct red_sched_data *q = qdisc_priv(sch);
	struct rtattr *tb[TCA_RED_MAX];
	struct tc_red_qopt *ctl;

	if (opt == NULL || rtattr_parse_nested(tb, TCA_RED_MAX, opt))
		return -EINVAL;

	if (tb[TCA_RED_PARMS-1] == NULL ||
	    RTA_PAYLOAD(tb[TCA_RED_PARMS-1]) < sizeof(*ctl) ||
	    tb[TCA_RED_STAB-1] == NULL ||
	    RTA_PAYLOAD(tb[TCA_RED_STAB-1]) < RED_STAB_SIZE)
		return -EINVAL;

	ctl = RTA_DATA(tb[TCA_RED_PARMS-1]);

	sch_tree_lock(sch);
	q->flags = ctl->flags;
	q->limit = ctl->limit;

	red_set_parms(&q->parms, ctl->qth_min, ctl->qth_max, ctl->Wlog,
				 ctl->Plog, ctl->Scell_log,
				 RTA_DATA(tb[TCA_RED_STAB-1]));

	if (skb_queue_empty(&sch->q))
		red_end_of_idle_period(&q->parms);

	sch_tree_unlock(sch);
	return 0;
}

static int red_init(struct Qdisc* sch, struct rtattr *opt)
{
	return red_change(sch, opt);
}

static int red_dump(struct Qdisc *sch, struct sk_buff *skb)
{
	struct red_sched_data *q = qdisc_priv(sch);
	struct rtattr *opts = NULL;
	struct tc_red_qopt opt = {
		.limit		= q->limit,
		.flags		= q->flags,
		.qth_min	= q->parms.qth_min >> q->parms.Wlog,
		.qth_max	= q->parms.qth_max >> q->parms.Wlog,
		.Wlog		= q->parms.Wlog,
		.Plog		= q->parms.Plog,
		.Scell_log	= q->parms.Scell_log,
	};

	opts = RTA_NEST(skb, TCA_OPTIONS);
	RTA_PUT(skb, TCA_RED_PARMS, sizeof(opt), &opt);
	return RTA_NEST_END(skb, opts);

rtattr_failure:
	return RTA_NEST_CANCEL(skb, opts);
}

static int red_dump_stats(struct Qdisc *sch, struct gnet_dump *d)
{
	struct red_sched_data *q = qdisc_priv(sch);
	struct tc_red_xstats st = {
		.early	= q->stats.prob_drop + q->stats.forced_drop,
		.pdrop	= q->stats.pdrop,
		.other	= q->stats.other,
		.marked	= q->stats.prob_mark + q->stats.forced_mark,
	};

	return gnet_stats_copy_app(d, &st, sizeof(st));
}

static struct Qdisc_ops red_qdisc_ops = {
	.id		=	"red",
	.priv_size	=	sizeof(struct red_sched_data),
	.enqueue	=	red_enqueue,
	.dequeue	=	red_dequeue,
	.requeue	=	red_requeue,
	.drop		=	red_drop,
	.init		=	red_init,
	.reset		=	red_reset,
	.change		=	red_change,
	.dump		=	red_dump,
	.dump_stats	=	red_dump_stats,
	.owner		=	THIS_MODULE,
};

static int __init red_module_init(void)
{
	return register_qdisc(&red_qdisc_ops);
}

static void __exit red_module_exit(void)
{
	unregister_qdisc(&red_qdisc_ops);
}

module_init(red_module_init)
module_exit(red_module_exit)

MODULE_LICENSE("GPL");
