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
	unsigned long long msgs;
	unsigned long long bytes;
	unsigned int mem_fail;
	unsigned int need_more_hdr;
	unsigned int msg_too_big;
	unsigned int msg_timeouts;
	unsigned int bad_hdr_len;
};

struct strp_aggr_stats {
	unsigned long long msgs;
	unsigned long long bytes;
	unsigned int mem_fail;
	unsigned int need_more_hdr;
	unsigned int msg_too_big;
	unsigned int msg_timeouts;
	unsigned int bad_hdr_len;
	unsigned int aborts;
	unsigned int interrupted;
	unsigned int unrecov_intr;
};

struct strparser;

/* Callbacks are called with lock held for the attached socket */
struct strp_callbacks {
	int (*parse_msg)(struct strparser *strp, struct sk_buff *skb);
	void (*rcv_msg)(struct strparser *strp, struct sk_buff *skb);
	int (*read_sock_done)(struct strparser *strp, int err);
	void (*abort_parser)(struct strparser *strp, int err);
	void (*lock)(struct strparser *strp);
	void (*unlock)(struct strparser *strp);
};

struct strp_msg {
	int full_len;
	int offset;
};

static inline struct strp_msg *strp_msg(struct sk_buff *skb)
{
	return (struct strp_msg *)((void *)skb->cb +
		offsetof(struct qdisc_skb_cb, data));
}

/* Structure for an attached lower socket */
struct strparser {
	struct sock *sk;

	u32 stopped : 1;
	u32 paused : 1;
	u32 aborted : 1;
	u32 interrupted : 1;
	u32 unrecov_intr : 1;

	struct sk_buff **skb_nextp;
	struct timer_list msg_timer;
	struct sk_buff *skb_head;
	unsigned int need_bytes;
	struct delayed_work delayed_work;
	struct work_struct work;
	struct strp_stats stats;
	struct strp_callbacks cb;
};

/* Must be called with lock held for attached socket */
static inline void strp_pause(struct strparser *strp)
{
	strp->paused = 1;
}

/* May be called without holding lock for attached socket */
void strp_unpause(struct strparser *strp);

static inline void save_strp_stats(struct strparser *strp,
				   struct strp_aggr_stats *agg_stats)
{
	/* Save psock statistics in the mux when psock is being unattached. */

#define SAVE_PSOCK_STATS(_stat) (agg_stats->_stat +=		\
				 strp->stats._stat)
	SAVE_PSOCK_STATS(msgs);
	SAVE_PSOCK_STATS(bytes);
	SAVE_PSOCK_STATS(mem_fail);
	SAVE_PSOCK_STATS(need_more_hdr);
	SAVE_PSOCK_STATS(msg_too_big);
	SAVE_PSOCK_STATS(msg_timeouts);
	SAVE_PSOCK_STATS(bad_hdr_len);
#undef SAVE_PSOCK_STATS

	if (strp->aborted)
		agg_stats->aborts++;
	if (strp->interrupted)
		agg_stats->interrupted++;
	if (strp->unrecov_intr)
		agg_stats->unrecov_intr++;
}

static inline void aggregate_strp_stats(struct strp_aggr_stats *stats,
					struct strp_aggr_stats *agg_stats)
{
#define SAVE_PSOCK_STATS(_stat) (agg_stats->_stat += stats->_stat)
	SAVE_PSOCK_STATS(msgs);
	SAVE_PSOCK_STATS(bytes);
	SAVE_PSOCK_STATS(mem_fail);
	SAVE_PSOCK_STATS(need_more_hdr);
	SAVE_PSOCK_STATS(msg_too_big);
	SAVE_PSOCK_STATS(msg_timeouts);
	SAVE_PSOCK_STATS(bad_hdr_len);
	SAVE_PSOCK_STATS(aborts);
	SAVE_PSOCK_STATS(interrupted);
	SAVE_PSOCK_STATS(unrecov_intr);
#undef SAVE_PSOCK_STATS

}

void strp_done(struct strparser *strp);
void strp_stop(struct strparser *strp);
void strp_check_rcv(struct strparser *strp);
int strp_init(struct strparser *strp, struct sock *sk,
	      const struct strp_callbacks *cb);
void strp_data_ready(struct strparser *strp);
int strp_process(struct strparser *strp, struct sk_buff *orig_skb,
		 unsigned int orig_offset, size_t orig_len,
		 size_t max_msg_size, long timeo);

#endif /* __NET_STRPARSER_H_ */
