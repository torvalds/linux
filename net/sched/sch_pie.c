// SPDX-License-Identifier: GPL-2.0-only
/* Copyright (C) 2013 Cisco Systems, Inc, 2013.
 *
 * Author: Vijay Subramanian <vijaynsu@cisco.com>
 * Author: Mythili Prabhu <mysuryan@cisco.com>
 *
 * ECN support is added by Naeem Khademi <naeemk@ifi.uio.no>
 * University of Oslo, Norway.
 *
 * References:
 * RFC 8033: https://tools.ietf.org/html/rfc8033
 */

#include <linux/module.h>
#include <linux/slab.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/skbuff.h>
#include <net/pkt_sched.h>
#include <net/inet_ecn.h>
#include <net/pie.h>

/* private data for the Qdisc */
struct pie_sched_data {
	struct pie_vars vars;
	struct pie_params params;
	struct pie_stats stats;
	struct timer_list adapt_timer;
	struct Qdisc *sch;
};

bool pie_drop_early(struct Qdisc *sch, struct pie_params *params,
		    struct pie_vars *vars, u32 backlog, u32 packet_size)
{
	u64 rnd;
	u64 local_prob = vars->prob;
	u32 mtu = psched_mtu(qdisc_dev(sch));

	/* If there is still burst allowance left skip random early drop */
	if (vars->burst_time > 0)
		return false;

	/* If current delay is less than half of target, and
	 * if drop prob is low already, disable early_drop
	 */
	if ((vars->qdelay < params->target / 2) &&
	    (vars->prob < MAX_PROB / 5))
		return false;

	/* If we have fewer than 2 mtu-sized packets, disable pie_drop_early,
	 * similar to min_th in RED
	 */
	if (backlog < 2 * mtu)
		return false;

	/* If bytemode is turned on, use packet size to compute new
	 * probablity. Smaller packets will have lower drop prob in this case
	 */
	if (params->bytemode && packet_size <= mtu)
		local_prob = (u64)packet_size * div_u64(local_prob, mtu);
	else
		local_prob = vars->prob;

	if (local_prob == 0)
		vars->accu_prob = 0;
	else
		vars->accu_prob += local_prob;

	if (vars->accu_prob < (MAX_PROB / 100) * 85)
		return false;
	if (vars->accu_prob >= (MAX_PROB / 2) * 17)
		return true;

	get_random_bytes(&rnd, 8);
	if ((rnd >> BITS_PER_BYTE) < local_prob) {
		vars->accu_prob = 0;
		return true;
	}

	return false;
}
EXPORT_SYMBOL_GPL(pie_drop_early);

static int pie_qdisc_enqueue(struct sk_buff *skb, struct Qdisc *sch,
			     struct sk_buff **to_free)
{
	struct pie_sched_data *q = qdisc_priv(sch);
	bool enqueue = false;

	if (unlikely(qdisc_qlen(sch) >= sch->limit)) {
		q->stats.overlimit++;
		goto out;
	}

	if (!pie_drop_early(sch, &q->params, &q->vars, sch->qstats.backlog,
			    skb->len)) {
		enqueue = true;
	} else if (q->params.ecn && (q->vars.prob <= MAX_PROB / 10) &&
		   INET_ECN_set_ce(skb)) {
		/* If packet is ecn capable, mark it if drop probability
		 * is lower than 10%, else drop it.
		 */
		q->stats.ecn_mark++;
		enqueue = true;
	}

	/* we can enqueue the packet */
	if (enqueue) {
		/* Set enqueue time only when dq_rate_estimator is disabled. */
		if (!q->params.dq_rate_estimator)
			pie_set_enqueue_time(skb);

		q->stats.packets_in++;
		if (qdisc_qlen(sch) > q->stats.maxq)
			q->stats.maxq = qdisc_qlen(sch);

		return qdisc_enqueue_tail(skb, sch);
	}

out:
	q->stats.dropped++;
	q->vars.accu_prob = 0;
	return qdisc_drop(skb, sch, to_free);
}

static const struct nla_policy pie_policy[TCA_PIE_MAX + 1] = {
	[TCA_PIE_TARGET]		= {.type = NLA_U32},
	[TCA_PIE_LIMIT]			= {.type = NLA_U32},
	[TCA_PIE_TUPDATE]		= {.type = NLA_U32},
	[TCA_PIE_ALPHA]			= {.type = NLA_U32},
	[TCA_PIE_BETA]			= {.type = NLA_U32},
	[TCA_PIE_ECN]			= {.type = NLA_U32},
	[TCA_PIE_BYTEMODE]		= {.type = NLA_U32},
	[TCA_PIE_DQ_RATE_ESTIMATOR]	= {.type = NLA_U32},
};

static int pie_change(struct Qdisc *sch, struct nlattr *opt,
		      struct netlink_ext_ack *extack)
{
	struct pie_sched_data *q = qdisc_priv(sch);
	struct nlattr *tb[TCA_PIE_MAX + 1];
	unsigned int qlen, dropped = 0;
	int err;

	err = nla_parse_nested_deprecated(tb, TCA_PIE_MAX, opt, pie_policy,
					  NULL);
	if (err < 0)
		return err;

	sch_tree_lock(sch);

	/* convert from microseconds to pschedtime */
	if (tb[TCA_PIE_TARGET]) {
		/* target is in us */
		u32 target = nla_get_u32(tb[TCA_PIE_TARGET]);

		/* convert to pschedtime */
		q->params.target = PSCHED_NS2TICKS((u64)target * NSEC_PER_USEC);
	}

	/* tupdate is in jiffies */
	if (tb[TCA_PIE_TUPDATE])
		q->params.tupdate =
			usecs_to_jiffies(nla_get_u32(tb[TCA_PIE_TUPDATE]));

	if (tb[TCA_PIE_LIMIT]) {
		u32 limit = nla_get_u32(tb[TCA_PIE_LIMIT]);

		q->params.limit = limit;
		sch->limit = limit;
	}

	if (tb[TCA_PIE_ALPHA])
		q->params.alpha = nla_get_u32(tb[TCA_PIE_ALPHA]);

	if (tb[TCA_PIE_BETA])
		q->params.beta = nla_get_u32(tb[TCA_PIE_BETA]);

	if (tb[TCA_PIE_ECN])
		q->params.ecn = nla_get_u32(tb[TCA_PIE_ECN]);

	if (tb[TCA_PIE_BYTEMODE])
		q->params.bytemode = nla_get_u32(tb[TCA_PIE_BYTEMODE]);

	if (tb[TCA_PIE_DQ_RATE_ESTIMATOR])
		q->params.dq_rate_estimator =
				nla_get_u32(tb[TCA_PIE_DQ_RATE_ESTIMATOR]);

	/* Drop excess packets if new limit is lower */
	qlen = sch->q.qlen;
	while (sch->q.qlen > sch->limit) {
		struct sk_buff *skb = __qdisc_dequeue_head(&sch->q);

		dropped += qdisc_pkt_len(skb);
		qdisc_qstats_backlog_dec(sch, skb);
		rtnl_qdisc_drop(skb, sch);
	}
	qdisc_tree_reduce_backlog(sch, qlen - sch->q.qlen, dropped);

	sch_tree_unlock(sch);
	return 0;
}

void pie_process_dequeue(struct sk_buff *skb, struct pie_params *params,
			 struct pie_vars *vars, u32 backlog)
{
	psched_time_t now = psched_get_time();
	u32 dtime = 0;

	/* If dq_rate_estimator is disabled, calculate qdelay using the
	 * packet timestamp.
	 */
	if (!params->dq_rate_estimator) {
		vars->qdelay = now - pie_get_enqueue_time(skb);

		if (vars->dq_tstamp != DTIME_INVALID)
			dtime = now - vars->dq_tstamp;

		vars->dq_tstamp = now;

		if (backlog == 0)
			vars->qdelay = 0;

		if (dtime == 0)
			return;

		goto burst_allowance_reduction;
	}

	/* If current queue is about 10 packets or more and dq_count is unset
	 * we have enough packets to calculate the drain rate. Save
	 * current time as dq_tstamp and start measurement cycle.
	 */
	if (backlog >= QUEUE_THRESHOLD && vars->dq_count == DQCOUNT_INVALID) {
		vars->dq_tstamp = psched_get_time();
		vars->dq_count = 0;
	}

	/* Calculate the average drain rate from this value. If queue length
	 * has receded to a small value viz., <= QUEUE_THRESHOLD bytes, reset
	 * the dq_count to -1 as we don't have enough packets to calculate the
	 * drain rate anymore. The following if block is entered only when we
	 * have a substantial queue built up (QUEUE_THRESHOLD bytes or more)
	 * and we calculate the drain rate for the threshold here.  dq_count is
	 * in bytes, time difference in psched_time, hence rate is in
	 * bytes/psched_time.
	 */
	if (vars->dq_count != DQCOUNT_INVALID) {
		vars->dq_count += skb->len;

		if (vars->dq_count >= QUEUE_THRESHOLD) {
			u32 count = vars->dq_count << PIE_SCALE;

			dtime = now - vars->dq_tstamp;

			if (dtime == 0)
				return;

			count = count / dtime;

			if (vars->avg_dq_rate == 0)
				vars->avg_dq_rate = count;
			else
				vars->avg_dq_rate =
				    (vars->avg_dq_rate -
				     (vars->avg_dq_rate >> 3)) + (count >> 3);

			/* If the queue has receded below the threshold, we hold
			 * on to the last drain rate calculated, else we reset
			 * dq_count to 0 to re-enter the if block when the next
			 * packet is dequeued
			 */
			if (backlog < QUEUE_THRESHOLD) {
				vars->dq_count = DQCOUNT_INVALID;
			} else {
				vars->dq_count = 0;
				vars->dq_tstamp = psched_get_time();
			}

			goto burst_allowance_reduction;
		}
	}

	return;

burst_allowance_reduction:
	if (vars->burst_time > 0) {
		if (vars->burst_time > dtime)
			vars->burst_time -= dtime;
		else
			vars->burst_time = 0;
	}
}
EXPORT_SYMBOL_GPL(pie_process_dequeue);

void pie_calculate_probability(struct pie_params *params, struct pie_vars *vars,
			       u32 backlog)
{
	psched_time_t qdelay = 0;	/* in pschedtime */
	psched_time_t qdelay_old = 0;	/* in pschedtime */
	s64 delta = 0;		/* determines the change in probability */
	u64 oldprob;
	u64 alpha, beta;
	u32 power;
	bool update_prob = true;

	if (params->dq_rate_estimator) {
		qdelay_old = vars->qdelay;
		vars->qdelay_old = vars->qdelay;

		if (vars->avg_dq_rate > 0)
			qdelay = (backlog << PIE_SCALE) / vars->avg_dq_rate;
		else
			qdelay = 0;
	} else {
		qdelay = vars->qdelay;
		qdelay_old = vars->qdelay_old;
	}

	/* If qdelay is zero and backlog is not, it means backlog is very small,
	 * so we do not update probability in this round.
	 */
	if (qdelay == 0 && backlog != 0)
		update_prob = false;

	/* In the algorithm, alpha and beta are between 0 and 2 with typical
	 * value for alpha as 0.125. In this implementation, we use values 0-32
	 * passed from user space to represent this. Also, alpha and beta have
	 * unit of HZ and need to be scaled before they can used to update
	 * probability. alpha/beta are updated locally below by scaling down
	 * by 16 to come to 0-2 range.
	 */
	alpha = ((u64)params->alpha * (MAX_PROB / PSCHED_TICKS_PER_SEC)) >> 4;
	beta = ((u64)params->beta * (MAX_PROB / PSCHED_TICKS_PER_SEC)) >> 4;

	/* We scale alpha and beta differently depending on how heavy the
	 * congestion is. Please see RFC 8033 for details.
	 */
	if (vars->prob < MAX_PROB / 10) {
		alpha >>= 1;
		beta >>= 1;

		power = 100;
		while (vars->prob < div_u64(MAX_PROB, power) &&
		       power <= 1000000) {
			alpha >>= 2;
			beta >>= 2;
			power *= 10;
		}
	}

	/* alpha and beta should be between 0 and 32, in multiples of 1/16 */
	delta += alpha * (qdelay - params->target);
	delta += beta * (qdelay - qdelay_old);

	oldprob = vars->prob;

	/* to ensure we increase probability in steps of no more than 2% */
	if (delta > (s64)(MAX_PROB / (100 / 2)) &&
	    vars->prob >= MAX_PROB / 10)
		delta = (MAX_PROB / 100) * 2;

	/* Non-linear drop:
	 * Tune drop probability to increase quickly for high delays(>= 250ms)
	 * 250ms is derived through experiments and provides error protection
	 */

	if (qdelay > (PSCHED_NS2TICKS(250 * NSEC_PER_MSEC)))
		delta += MAX_PROB / (100 / 2);

	vars->prob += delta;

	if (delta > 0) {
		/* prevent overflow */
		if (vars->prob < oldprob) {
			vars->prob = MAX_PROB;
			/* Prevent normalization error. If probability is at
			 * maximum value already, we normalize it here, and
			 * skip the check to do a non-linear drop in the next
			 * section.
			 */
			update_prob = false;
		}
	} else {
		/* prevent underflow */
		if (vars->prob > oldprob)
			vars->prob = 0;
	}

	/* Non-linear drop in probability: Reduce drop probability quickly if
	 * delay is 0 for 2 consecutive Tupdate periods.
	 */

	if (qdelay == 0 && qdelay_old == 0 && update_prob)
		/* Reduce drop probability to 98.4% */
		vars->prob -= vars->prob / 64;

	vars->qdelay = qdelay;
	vars->backlog_old = backlog;

	/* We restart the measurement cycle if the following conditions are met
	 * 1. If the delay has been low for 2 consecutive Tupdate periods
	 * 2. Calculated drop probability is zero
	 * 3. If average dq_rate_estimator is enabled, we have at least one
	 *    estimate for the avg_dq_rate ie., is a non-zero value
	 */
	if ((vars->qdelay < params->target / 2) &&
	    (vars->qdelay_old < params->target / 2) &&
	    vars->prob == 0 &&
	    (!params->dq_rate_estimator || vars->avg_dq_rate > 0)) {
		pie_vars_init(vars);
	}

	if (!params->dq_rate_estimator)
		vars->qdelay_old = qdelay;
}
EXPORT_SYMBOL_GPL(pie_calculate_probability);

static void pie_timer(struct timer_list *t)
{
	struct pie_sched_data *q = from_timer(q, t, adapt_timer);
	struct Qdisc *sch = q->sch;
	spinlock_t *root_lock;

	rcu_read_lock();
	root_lock = qdisc_lock(qdisc_root_sleeping(sch));
	spin_lock(root_lock);
	pie_calculate_probability(&q->params, &q->vars, sch->qstats.backlog);

	/* reset the timer to fire after 'tupdate'. tupdate is in jiffies. */
	if (q->params.tupdate)
		mod_timer(&q->adapt_timer, jiffies + q->params.tupdate);
	spin_unlock(root_lock);
	rcu_read_unlock();
}

static int pie_init(struct Qdisc *sch, struct nlattr *opt,
		    struct netlink_ext_ack *extack)
{
	struct pie_sched_data *q = qdisc_priv(sch);

	pie_params_init(&q->params);
	pie_vars_init(&q->vars);
	sch->limit = q->params.limit;

	q->sch = sch;
	timer_setup(&q->adapt_timer, pie_timer, 0);

	if (opt) {
		int err = pie_change(sch, opt, extack);

		if (err)
			return err;
	}

	mod_timer(&q->adapt_timer, jiffies + HZ / 2);
	return 0;
}

static int pie_dump(struct Qdisc *sch, struct sk_buff *skb)
{
	struct pie_sched_data *q = qdisc_priv(sch);
	struct nlattr *opts;

	opts = nla_nest_start_noflag(skb, TCA_OPTIONS);
	if (!opts)
		goto nla_put_failure;

	/* convert target from pschedtime to us */
	if (nla_put_u32(skb, TCA_PIE_TARGET,
			((u32)PSCHED_TICKS2NS(q->params.target)) /
			NSEC_PER_USEC) ||
	    nla_put_u32(skb, TCA_PIE_LIMIT, sch->limit) ||
	    nla_put_u32(skb, TCA_PIE_TUPDATE,
			jiffies_to_usecs(q->params.tupdate)) ||
	    nla_put_u32(skb, TCA_PIE_ALPHA, q->params.alpha) ||
	    nla_put_u32(skb, TCA_PIE_BETA, q->params.beta) ||
	    nla_put_u32(skb, TCA_PIE_ECN, q->params.ecn) ||
	    nla_put_u32(skb, TCA_PIE_BYTEMODE, q->params.bytemode) ||
	    nla_put_u32(skb, TCA_PIE_DQ_RATE_ESTIMATOR,
			q->params.dq_rate_estimator))
		goto nla_put_failure;

	return nla_nest_end(skb, opts);

nla_put_failure:
	nla_nest_cancel(skb, opts);
	return -1;
}

static int pie_dump_stats(struct Qdisc *sch, struct gnet_dump *d)
{
	struct pie_sched_data *q = qdisc_priv(sch);
	struct tc_pie_xstats st = {
		.prob		= q->vars.prob << BITS_PER_BYTE,
		.delay		= ((u32)PSCHED_TICKS2NS(q->vars.qdelay)) /
				   NSEC_PER_USEC,
		.packets_in	= q->stats.packets_in,
		.overlimit	= q->stats.overlimit,
		.maxq		= q->stats.maxq,
		.dropped	= q->stats.dropped,
		.ecn_mark	= q->stats.ecn_mark,
	};

	/* avg_dq_rate is only valid if dq_rate_estimator is enabled */
	st.dq_rate_estimating = q->params.dq_rate_estimator;

	/* unscale and return dq_rate in bytes per sec */
	if (q->params.dq_rate_estimator)
		st.avg_dq_rate = q->vars.avg_dq_rate *
				 (PSCHED_TICKS_PER_SEC) >> PIE_SCALE;

	return gnet_stats_copy_app(d, &st, sizeof(st));
}

static struct sk_buff *pie_qdisc_dequeue(struct Qdisc *sch)
{
	struct pie_sched_data *q = qdisc_priv(sch);
	struct sk_buff *skb = qdisc_dequeue_head(sch);

	if (!skb)
		return NULL;

	pie_process_dequeue(skb, &q->params, &q->vars, sch->qstats.backlog);
	return skb;
}

static void pie_reset(struct Qdisc *sch)
{
	struct pie_sched_data *q = qdisc_priv(sch);

	qdisc_reset_queue(sch);
	pie_vars_init(&q->vars);
}

static void pie_destroy(struct Qdisc *sch)
{
	struct pie_sched_data *q = qdisc_priv(sch);

	q->params.tupdate = 0;
	del_timer_sync(&q->adapt_timer);
}

static struct Qdisc_ops pie_qdisc_ops __read_mostly = {
	.id		= "pie",
	.priv_size	= sizeof(struct pie_sched_data),
	.enqueue	= pie_qdisc_enqueue,
	.dequeue	= pie_qdisc_dequeue,
	.peek		= qdisc_peek_dequeued,
	.init		= pie_init,
	.destroy	= pie_destroy,
	.reset		= pie_reset,
	.change		= pie_change,
	.dump		= pie_dump,
	.dump_stats	= pie_dump_stats,
	.owner		= THIS_MODULE,
};
MODULE_ALIAS_NET_SCH("pie");

static int __init pie_module_init(void)
{
	return register_qdisc(&pie_qdisc_ops);
}

static void __exit pie_module_exit(void)
{
	unregister_qdisc(&pie_qdisc_ops);
}

module_init(pie_module_init);
module_exit(pie_module_exit);

MODULE_DESCRIPTION("Proportional Integral controller Enhanced (PIE) scheduler");
MODULE_AUTHOR("Vijay Subramanian");
MODULE_AUTHOR("Mythili Prabhu");
MODULE_LICENSE("GPL");
