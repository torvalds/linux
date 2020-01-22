/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef __NET_SCHED_PIE_H
#define __NET_SCHED_PIE_H

#include <linux/ktime.h>
#include <linux/skbuff.h>
#include <linux/types.h>
#include <net/inet_ecn.h>
#include <net/pkt_sched.h>

#define MAX_PROB	U64_MAX
#define DTIME_INVALID	U64_MAX
#define QUEUE_THRESHOLD	16384
#define DQCOUNT_INVALID	-1
#define PIE_SCALE	8

/* parameters used */
struct pie_params {
	psched_time_t target;	/* user specified target delay in pschedtime */
	u32 tupdate;		/* timer frequency (in jiffies) */
	u32 limit;		/* number of packets that can be enqueued */
	u32 alpha;		/* alpha and beta are between 0 and 32 */
	u32 beta;		/* and are used for shift relative to 1 */
	bool ecn;		/* true if ecn is enabled */
	bool bytemode;		/* to scale drop early prob based on pkt size */
	u8 dq_rate_estimator;	/* to calculate delay using Little's law */
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

/* private skb vars */
struct pie_skb_cb {
	psched_time_t enqueue_time;
};

static inline void pie_params_init(struct pie_params *params)
{
	params->alpha = 2;
	params->beta = 20;
	params->tupdate = usecs_to_jiffies(15 * USEC_PER_MSEC);	/* 15 ms */
	params->limit = 1000;	/* default of 1000 packets */
	params->target = PSCHED_NS2TICKS(15 * NSEC_PER_MSEC);	/* 15 ms */
	params->ecn = false;
	params->bytemode = false;
	params->dq_rate_estimator = false;
}

static inline void pie_vars_init(struct pie_vars *vars)
{
	vars->dq_count = DQCOUNT_INVALID;
	vars->dq_tstamp = DTIME_INVALID;
	vars->accu_prob = 0;
	vars->avg_dq_rate = 0;
	/* default of 150 ms in pschedtime */
	vars->burst_time = PSCHED_NS2TICKS(150 * NSEC_PER_MSEC);
	vars->accu_prob_overflows = 0;
}

static inline struct pie_skb_cb *get_pie_cb(const struct sk_buff *skb)
{
	qdisc_cb_private_validate(skb, sizeof(struct pie_skb_cb));
	return (struct pie_skb_cb *)qdisc_skb_cb(skb)->data;
}

static inline psched_time_t pie_get_enqueue_time(const struct sk_buff *skb)
{
	return get_pie_cb(skb)->enqueue_time;
}

static inline void pie_set_enqueue_time(struct sk_buff *skb)
{
	get_pie_cb(skb)->enqueue_time = psched_get_time();
}

#endif
