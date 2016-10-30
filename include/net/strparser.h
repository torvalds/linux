/*
 * Stream Parser
 *
 * Copyright (c) 2016 Tom Herbert <tom@herbertland.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2
 * as published by the Free Software Foundation.
 */

#ifndef __NET_STRPARSER_H_
#define __NET_STRPARSER_H_

#include <linux/skbuff.h>
#include <net/sock.h>

#define STRP_STATS_ADD(stat, count) ((stat) += (count))
#define STRP_STATS_INCR(stat) ((stat)++)

struct strp_stats {
	unsigned long long rx_msgs;
	unsigned long long rx_bytes;
	unsigned int rx_mem_fail;
	unsigned int rx_need_more_hdr;
	unsigned int rx_msg_too_big;
	unsigned int rx_msg_timeouts;
	unsigned int rx_bad_hdr_len;
};

struct strp_aggr_stats {
	unsigned long long rx_msgs;
	unsigned long long rx_bytes;
	unsigned int rx_mem_fail;
	unsigned int rx_need_more_hdr;
	unsigned int rx_msg_too_big;
	unsigned int rx_msg_timeouts;
	unsigned int rx_bad_hdr_len;
	unsigned int rx_aborts;
	unsigned int rx_interrupted;
	unsigned int rx_unrecov_intr;
};

struct strparser;

/* Callbacks are called with lock held for the attached socket */
struct strp_callbacks {
	int (*parse_msg)(struct strparser *strp, struct sk_buff *skb);
	void (*rcv_msg)(struct strparser *strp, struct sk_buff *skb);
	int (*read_sock_done)(struct strparser *strp, int err);
	void (*abort_parser)(struct strparser *strp, int err);
};

struct strp_rx_msg {
	int full_len;
	int offset;
};

static inline struct strp_rx_msg *strp_rx_msg(struct sk_buff *skb)
{
	return (struct strp_rx_msg *)((void *)skb->cb +
		offsetof(struct qdisc_skb_cb, data));
}

/* Structure for an attached lower socket */
struct strparser {
	struct sock *sk;

	u32 rx_stopped : 1;
	u32 rx_paused : 1;
	u32 rx_aborted : 1;
	u32 rx_interrupted : 1;
	u32 rx_unrecov_intr : 1;

	struct sk_buff **rx_skb_nextp;
	struct timer_list rx_msg_timer;
	struct sk_buff *rx_skb_head;
	unsigned int rx_need_bytes;
	struct delayed_work rx_delayed_work;
	struct work_struct rx_work;
	struct strp_stats stats;
	struct strp_callbacks cb;
};

/* Must be called with lock held for attached socket */
static inline void strp_pause(struct strparser *strp)
{
	strp->rx_paused = 1;
}

/* May be called without holding lock for attached socket */
void strp_unpause(struct strparser *strp);

static inline void save_strp_stats(struct strparser *strp,
				   struct strp_aggr_stats *agg_stats)
{
	/* Save psock statistics in the mux when psock is being unattached. */

#define SAVE_PSOCK_STATS(_stat) (agg_stats->_stat +=		\
				 strp->stats._stat)
	SAVE_PSOCK_STATS(rx_msgs);
	SAVE_PSOCK_STATS(rx_bytes);
	SAVE_PSOCK_STATS(rx_mem_fail);
	SAVE_PSOCK_STATS(rx_need_more_hdr);
	SAVE_PSOCK_STATS(rx_msg_too_big);
	SAVE_PSOCK_STATS(rx_msg_timeouts);
	SAVE_PSOCK_STATS(rx_bad_hdr_len);
#undef SAVE_PSOCK_STATS

	if (strp->rx_aborted)
		agg_stats->rx_aborts++;
	if (strp->rx_interrupted)
		agg_stats->rx_interrupted++;
	if (strp->rx_unrecov_intr)
		agg_stats->rx_unrecov_intr++;
}

static inline void aggregate_strp_stats(struct strp_aggr_stats *stats,
					struct strp_aggr_stats *agg_stats)
{
#define SAVE_PSOCK_STATS(_stat) (agg_stats->_stat += stats->_stat)
	SAVE_PSOCK_STATS(rx_msgs);
	SAVE_PSOCK_STATS(rx_bytes);
	SAVE_PSOCK_STATS(rx_mem_fail);
	SAVE_PSOCK_STATS(rx_need_more_hdr);
	SAVE_PSOCK_STATS(rx_msg_too_big);
	SAVE_PSOCK_STATS(rx_msg_timeouts);
	SAVE_PSOCK_STATS(rx_bad_hdr_len);
	SAVE_PSOCK_STATS(rx_aborts);
	SAVE_PSOCK_STATS(rx_interrupted);
	SAVE_PSOCK_STATS(rx_unrecov_intr);
#undef SAVE_PSOCK_STATS

}

void strp_done(struct strparser *strp);
void strp_stop(struct strparser *strp);
void strp_check_rcv(struct strparser *strp);
int strp_init(struct strparser *strp, struct sock *csk,
	      struct strp_callbacks *cb);
void strp_data_ready(struct strparser *strp);

#endif /* __NET_STRPARSER_H_ */
