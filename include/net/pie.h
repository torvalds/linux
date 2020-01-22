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

/**
 * struct pie_params - contains pie parameters
 * @target:		target delay in pschedtime
 * @tudpate:		interval at which drop probability is calculated
 * @limit:		total number of packets that can be in the queue
 * @alpha:		parameter to control drop probability
 * @beta:		parameter to control drop probability
 * @ecn:		is ECN marking of packets enabled
 * @bytemode:		is drop probability scaled based on pkt size
 * @dq_rate_estimator:	is Little's law used for qdelay calculation
 */
struct pie_params {
	psched_time_t target;
	u32 tupdate;
	u32 limit;
	u32 alpha;
	u32 beta;
	u8 ecn;
	u8 bytemode;
	u8 dq_rate_estimator;
};

/**
 * struct pie_vars - contains pie variables
 * @qdelay:			current queue delay
 * @qdelay_old:			queue delay in previous qdelay calculation
 * @burst_time:			burst time allowance
 * @dq_tstamp:			timestamp at which dq rate was last calculated
 * @prob:			drop probability
 * @accu_prob:			accumulated drop probability
 * @dq_count:			number of bytes dequeued in a measurement cycle
 * @avg_dq_rate:		calculated average dq rate
 * @qlen_old:			queue length during previous qdelay calculation
 * @accu_prob_overflows:	number of times accu_prob overflows
 */
struct pie_vars {
	psched_time_t qdelay;
	psched_time_t qdelay_old;
	psched_time_t burst_time;
	psched_time_t dq_tstamp;
	u64 prob;
	u64 accu_prob;
	u64 dq_count;
	u32 avg_dq_rate;
	u32 qlen_old;
	u8 accu_prob_overflows;
};

/**
 * struct pie_stats - contains pie stats
 * @packets_in:	total number of packets enqueued
 * @dropped:	packets dropped due to pie action
 * @overlimit:	packets dropped due to lack of space in queue
 * @ecn_mark:	packets marked with ECN
 * @maxq:	maximum queue size
 */
struct pie_stats {
	u32 packets_in;
	u32 dropped;
	u32 overlimit;
	u32 ecn_mark;
	u32 maxq;
};

/**
 * struct pie_skb_cb - contains private skb vars
 * @enqueue_time:	timestamp when the packet is enqueued
 * @mem_usage:		size of the skb during enqueue
 */
struct pie_skb_cb {
	psched_time_t enqueue_time;
	u32 mem_usage;
};

static inline void pie_params_init(struct pie_params *params)
{
	params->target = PSCHED_NS2TICKS(15 * NSEC_PER_MSEC);	/* 15 ms */
	params->tupdate = usecs_to_jiffies(15 * USEC_PER_MSEC);	/* 15 ms */
	params->limit = 1000;
	params->alpha = 2;
	params->beta = 20;
	params->ecn = false;
	params->bytemode = false;
	params->dq_rate_estimator = false;
}

static inline void pie_vars_init(struct pie_vars *vars)
{
	vars->burst_time = PSCHED_NS2TICKS(150 * NSEC_PER_MSEC); /* 150 ms */
	vars->dq_tstamp = DTIME_INVALID;
	vars->accu_prob = 0;
	vars->dq_count = DQCOUNT_INVALID;
	vars->avg_dq_rate = 0;
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

bool pie_drop_early(struct Qdisc *sch, struct pie_params *params,
		    struct pie_vars *vars, u32 qlen, u32 packet_size);

void pie_process_dequeue(struct sk_buff *skb, struct pie_params *params,
			 struct pie_vars *vars, u32 qlen);

void pie_calculate_probability(struct pie_params *params, struct pie_vars *vars,
			       u32 qlen);

#endif
