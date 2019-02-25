/* Copyright (C) 2013 Cisco Systems, Inc, 2013.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * Author: Vijay Subramanian <vijaynsu@cisco.com>
 * Author: Mythili Prabhu <mysuryan@cisco.com>
 *
 * ECN support is added by Naeem Khademi <naeemk@ifi.uio.no>
 * University of Oslo, Norway.
 *
 * References:
 * RFC 8033: https://tools.ietf.org/html/rfc8034
 */

#include <linux/module.h>
#include <linux/slab.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/skbuff.h>
#include <net/pkt_sched.h>
#include <net/inet_ecn.h>

#define QUEUE_THRESHOLD 16384
#define DQCOUNT_INVALID -1
#define MAX_PROB 0xffffffffffffffff
#define PIE_SCALE 8

/* parameters used */
struct pie_params {
	psched_time_t target;	/* user specified target delay in pschedtime */
	u32 tupdate;		/* timer frequency (in jiffies) */
	u32 limit;		/* number of packets that can be enqueued */
	u32 alpha;		/* alpha and beta are between 0 and 32 */
	u32 beta;		/* and are used for shift relative to 1 */
	bool ecn;		/* true if ecn is enabled */
	bool bytemode;		/* to scale drop early prob based on pkt size */
};

/* variables used */
struct pie_vars {
	u64 prob;		/* probability but scaled by u64 limit. */
	psched_time_t burst_time;
	psched_time_t qdelay;
	psched_time_t qdelay_old;
	u64 dq_count;		/* measured in bytes */
	psched_time_t dq_tstamp;	/* drain rate */
	u64 accu_prob;		/* accumulated drop probability */
	u32 avg_dq_rate;	/* bytes per pschedtime tick,scaled */
	u32 qlen_old;		/* in bytes */
	u8 accu_prob_overflows;	/* overflows of accu_prob */
};

/* statistics gathering */
struct pie_stats {
	u32 packets_in;		/* total number of packets enqueued */
	u32 dropped;		/* packets dropped due to pie_action */
	u32 overlimit;		/* dropped due to lack of space in queue */
	u32 maxq;		/* maximum queue size */
	u32 ecn_mark;		/* packets marked with ECN */
};

/* private data for the Qdisc */
struct pie_sched_data {
	struct pie_params params;
	struct pie_vars vars;
	struct pie_stats stats;
	struct timer_list adapt_timer;
	struct Qdisc *sch;
};

static void pie_params_init(struct pie_params *params)
{
	params->alpha = 2;
	params->beta = 20;
	params->tupdate = usecs_to_jiffies(15 * USEC_PER_MSEC);	/* 15 ms */
	params->limit = 1000;	/* default of 1000 packets */
	params->target = PSCHED_NS2TICKS(15 * NSEC_PER_MSEC);	/* 15 ms */
	params->ecn = false;
	params->bytemode = false;
}

static void pie_vars_init(struct pie_vars *vars)
{
	vars->dq_count = DQCOUNT_INVALID;
	vars->accu_prob = 0;
	vars->avg_dq_rate = 0;
	/* default of 150 ms in pschedtime */
	vars->burst_time = PSCHED_NS2TICKS(150 * NSEC_PER_MSEC);
	vars->accu_prob_overflows = 0;
}

static bool drop_early(struct Qdisc *sch, u32 packet_size)
{
	struct pie_sched_data *q = qdisc_priv(sch);
	u64 rnd;
	u64 local_prob = q->vars.prob;
	u32 mtu = psched_mtu(qdisc_dev(sch));

	/* If there is still burst allowance left skip random early drop */
	if (q->vars.burst_time > 0)
		return false;

	/* If current delay is less than half of target, and
	 * if drop prob is low already, disable early_drop
	 */
	if ((q->vars.qdelay < q->params.target / 2) &&
	    (q->vars.prob < MAX_PROB / 5))
		return false;

	/* If we have fewer than 2 mtu-sized packets, disable drop_early,
	 * similar to min_th in RED
	 */
	if (sch->qstats.backlog < 2 * mtu)
		return false;

	/* If bytemode is turned on, use packet size to compute new
	 * probablity. Smaller packets will have lower drop prob in this case
	 */
	if (q->params.bytemode && packet_size <= mtu)
		local_prob = (u64)packet_size * div_u64(local_prob, mtu);
	else
		local_prob = q->vars.prob;

	if (local_prob == 0) {
		q->vars.accu_prob = 0;
		q->vars.accu_prob_overflows = 0;
	}

	if (local_prob > MAX_PROB - q->vars.accu_prob)
		q->vars.accu_prob_overflows++;

	q->vars.accu_prob += local_prob;

	if (q->vars.accu_prob_overflows == 0 &&
	    q->vars.accu_prob < (MAX_PROB / 100) * 85)
		return false;
	if (q->vars.accu_prob_overflows == 8 &&
	    q->vars.accu_prob >= MAX_PROB / 2)
		return true;

	prandom_bytes(&rnd, 8);
	if (rnd < local_prob) {
		q->vars.accu_prob = 0;
		q->vars.accu_prob_overflows = 0;
		return true;
	}

	return false;
}

static int pie_qdisc_enqueue(struct sk_buff *skb, struct Qdisc *sch,
			     struct sk_buff **to_free)
{
	struct pie_sched_data *q = qdisc_priv(sch);
	bool enqueue = false;

	if (unlikely(qdisc_qlen(sch) >= sch->limit)) {
		q->stats.overlimit++;
		goto out;
	}

	if (!drop_early(sch, skb->len)) {
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
		q->stats.packets_in++;
		if (qdisc_qlen(sch) > q->stats.maxq)
			q->stats.maxq = qdisc_qlen(sch);

		return qdisc_enqueue_tail(skb, sch);
	}

out:
	q->stats.dropped++;
	q->vars.accu_prob = 0;
	q->vars.accu_prob_overflows = 0;
	return qdisc_drop(skb, sch, to_free);
}

static const struct nla_policy pie_policy[TCA_PIE_MAX + 1] = {
	[TCA_PIE_TARGET] = {.type = NLA_U32},
	[TCA_PIE_LIMIT] = {.type = NLA_U32},
	[TCA_PIE_TUPDATE] = {.type = NLA_U32},
	[TCA_PIE_ALPHA] = {.type = NLA_U32},
	[TCA_PIE_BETA] = {.type = NLA_U32},
	[TCA_PIE_ECN] = {.type = NLA_U32},
	[TCA_PIE_BYTEMODE] = {.type = NLA_U32},
};

static int pie_change(struct Qdisc *sch, struct nlattr *opt,
		      struct netlink_ext_ack *extack)
{
	struct pie_sched_data *q = qdisc_priv(sch);
	struct nlattr *tb[TCA_PIE_MAX + 1];
	unsigned int qlen, dropped = 0;
	int err;

	if (!opt)
		return -EINVAL;

	err = nla_parse_nested(tb, TCA_PIE_MAX, opt, pie_policy, NULL);
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

static void pie_process_dequeue(struct Qdisc *sch, struct sk_buff *skb)
{
	struct pie_sched_data *q = qdisc_priv(sch);
	int qlen = sch->qstats.backlog;	/* current queue size in bytes */

	/* If current queue is about 10 packets or more and dq_count is unset
	 * we have enough packets to calculate the drain rate. Save
	 * current time as dq_tstamp and start measurement cycle.
	 */
	if (qlen >= QUEUE_THRESHOLD && q->vars.dq_count == DQCOUNT_INVALID) {
		q->vars.dq_tstamp = psched_get_time();
		q->vars.dq_count = 0;
	}

	/* Calculate the average drain rate from this value.  If queue length
	 * has receded to a small value viz., <= QUEUE_THRESHOLD bytes,reset
	 * the dq_count to -1 as we don't have enough packets to calculate the
	 * drain rate anymore The following if block is entered only when we
	 * have a substantial queue built up (QUEUE_THRESHOLD bytes or more)
	 * and we calculate the drain rate for the threshold here.  dq_count is
	 * in bytes, time difference in psched_time, hence rate is in
	 * bytes/psched_time.
	 */
	if (q->vars.dq_count != DQCOUNT_INVALID) {
		q->vars.dq_count += skb->len;

		if (q->vars.dq_count >= QUEUE_THRESHOLD) {
			psched_time_t now = psched_get_time();
			u32 dtime = now - q->vars.dq_tstamp;
			u32 count = q->vars.dq_count << PIE_SCALE;

			if (dtime == 0)
				return;

			count = count / dtime;

			if (q->vars.avg_dq_rate == 0)
				q->vars.avg_dq_rate = count;
			else
				q->vars.avg_dq_rate =
				    (q->vars.avg_dq_rate -
				     (q->vars.avg_dq_rate >> 3)) + (count >> 3);

			/* If the queue has receded below the threshold, we hold
			 * on to the last drain rate calculated, else we reset
			 * dq_count to 0 to re-enter the if block when the next
			 * packet is dequeued
			 */
			if (qlen < QUEUE_THRESHOLD) {
				q->vars.dq_count = DQCOUNT_INVALID;
			} else {
				q->vars.dq_count = 0;
				q->vars.dq_tstamp = psched_get_time();
			}

			if (q->vars.burst_time > 0) {
				if (q->vars.burst_time > dtime)
					q->vars.burst_time -= dtime;
				else
					q->vars.burst_time = 0;
			}
		}
	}
}

static void calculate_probability(struct Qdisc *sch)
{
	struct pie_sched_data *q = qdisc_priv(sch);
	u32 qlen = sch->qstats.backlog;	/* queue size in bytes */
	psched_time_t qdelay = 0;	/* in pschedtime */
	psched_time_t qdelay_old = q->vars.qdelay;	/* in pschedtime */
	s64 delta = 0;		/* determines the change in probability */
	u64 oldprob;
	u64 alpha, beta;
	u32 power;
	bool update_prob = true;

	q->vars.qdelay_old = q->vars.qdelay;

	if (q->vars.avg_dq_rate > 0)
		qdelay = (qlen << PIE_SCALE) / q->vars.avg_dq_rate;
	else
		qdelay = 0;

	/* If qdelay is zero and qlen is not, it means qlen is very small, less
	 * than dequeue_rate, so we do not update probabilty in this round
	 */
	if (qdelay == 0 && qlen != 0)
		update_prob = false;

	/* In the algorithm, alpha and beta are between 0 and 2 with typical
	 * value for alpha as 0.125. In this implementation, we use values 0-32
	 * passed from user space to represent this. Also, alpha and beta have
	 * unit of HZ and need to be scaled before they can used to update
	 * probability. alpha/beta are updated locally below by scaling down
	 * by 16 to come to 0-2 range.
	 */
	alpha = ((u64)q->params.alpha * (MAX_PROB / PSCHED_TICKS_PER_SEC)) >> 4;
	beta = ((u64)q->params.beta * (MAX_PROB / PSCHED_TICKS_PER_SEC)) >> 4;

	/* We scale alpha and beta differently depending on how heavy the
	 * congestion is. Please see RFC 8033 for details.
	 */
	if (q->vars.prob < MAX_PROB / 10) {
		alpha >>= 1;
		beta >>= 1;

		power = 100;
		while (q->vars.prob < div_u64(MAX_PROB, power) &&
		       power <= 1000000) {
			alpha >>= 2;
			beta >>= 2;
			power *= 10;
		}
	}

	/* alpha and beta should be between 0 and 32, in multiples of 1/16 */
	delta += alpha * (u64)(qdelay - q->params.target);
	delta += beta * (u64)(qdelay - qdelay_old);

	oldprob = q->vars.prob;

	/* to ensure we increase probability in steps of no more than 2% */
	if (delta > (s64)(MAX_PROB / (100 / 2)) &&
	    q->vars.prob >= MAX_PROB / 10)
		delta = (MAX_PROB / 100) * 2;

	/* Non-linear drop:
	 * Tune drop probability to increase quickly for high delays(>= 250ms)
	 * 250ms is derived through experiments and provides error protection
	 */

	if (qdelay > (PSCHED_NS2TICKS(250 * NSEC_PER_MSEC)))
		delta += MAX_PROB / (100 / 2);

	q->vars.prob += delta;

	if (delta > 0) {
		/* prevent overflow */
		if (q->vars.prob < oldprob) {
			q->vars.prob = MAX_PROB;
			/* Prevent normalization error. If probability is at
			 * maximum value already, we normalize it here, and
			 * skip the check to do a non-linear drop in the next
			 * section.
			 */
			update_prob = false;
		}
	} else {
		/* prevent underflow */
		if (q->vars.prob > oldprob)
			q->vars.prob = 0;
	}

	/* Non-linear drop in probability: Reduce drop probability quickly if
	 * delay is 0 for 2 consecutive Tupdate periods.
	 */

	if (qdelay == 0 && qdelay_old == 0 && update_prob)
		q->vars.prob = (q->vars.prob * 98) / 100;

	q->vars.qdelay = qdelay;
	q->vars.qlen_old = qlen;

	/* We restart the measurement cycle if the following conditions are met
	 * 1. If the delay has been low for 2 consecutive Tupdate periods
	 * 2. Calculated drop probability is zero
	 * 3. We have atleast one estimate for the avg_dq_rate ie.,
	 *    is a non-zero value
	 */
	if ((q->vars.qdelay < q->params.target / 2) &&
	    (q->vars.qdelay_old < q->params.target / 2) &&
	    q->vars.prob == 0 &&
	    q->vars.avg_dq_rate > 0)
		pie_vars_init(&q->vars);
}

static void pie_timer(struct timer_list *t)
{
	struct pie_sched_data *q = from_timer(q, t, adapt_timer);
	struct Qdisc *sch = q->sch;
	spinlock_t *root_lock = qdisc_lock(qdisc_root_sleeping(sch));

	spin_lock(root_lock);
	calculate_probability(sch);

	/* reset the timer to fire after 'tupdate'. tupdate is in jiffies. */
	if (q->params.tupdate)
		mod_timer(&q->adapt_timer, jiffies + q->params.tupdate);
	spin_unlock(root_lock);
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

	opts = nla_nest_start(skb, TCA_OPTIONS);
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
	    nla_put_u32(skb, TCA_PIE_BYTEMODE, q->params.bytemode))
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
		.prob		= q->vars.prob,
		.delay		= ((u32)PSCHED_TICKS2NS(q->vars.qdelay)) /
				   NSEC_PER_USEC,
		/* unscale and return dq_rate in bytes per sec */
		.avg_dq_rate	= q->vars.avg_dq_rate *
				  (PSCHED_TICKS_PER_SEC) >> PIE_SCALE,
		.packets_in	= q->stats.packets_in,
		.overlimit	= q->stats.overlimit,
		.maxq		= q->stats.maxq,
		.dropped	= q->stats.dropped,
		.ecn_mark	= q->stats.ecn_mark,
	};

	return gnet_stats_copy_app(d, &st, sizeof(st));
}

static struct sk_buff *pie_qdisc_dequeue(struct Qdisc *sch)
{
	struct sk_buff *skb = qdisc_dequeue_head(sch);

	if (!skb)
		return NULL;

	pie_process_dequeue(sch, skb);
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
	.id = "pie",
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
