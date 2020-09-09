/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2016 Qualcomm Atheros, Inc
 *
 * Based on net/sched/sch_fq_codel.c
 */
#ifndef __NET_SCHED_FQ_H
#define __NET_SCHED_FQ_H

struct fq_tin;

/**
 * struct fq_flow - per traffic flow queue
 *
 * @tin: owner of this flow. Used to manage collisions, i.e. when a packet
 *	hashes to an index which points to a flow that is already owned by a
 *	different tin the packet is destined to. In such case the implementer
 *	must provide a fallback flow
 * @flowchain: can be linked to fq_tin's new_flows or old_flows. Used for DRR++
 *	(deficit round robin) based round robin queuing similar to the one
 *	found in net/sched/sch_fq_codel.c
 * @backlogchain: can be linked to other fq_flow and fq. Used to keep track of
 *	fat flows and efficient head-dropping if packet limit is reached
 * @queue: sk_buff queue to hold packets
 * @backlog: number of bytes pending in the queue. The number of packets can be
 *	found in @queue.qlen
 * @deficit: used for DRR++
 */
struct fq_flow {
	struct fq_tin *tin;
	struct list_head flowchain;
	struct list_head backlogchain;
	struct sk_buff_head queue;
	u32 backlog;
	int deficit;
};

/**
 * struct fq_tin - a logical container of fq_flows
 *
 * Used to group fq_flows into a logical aggregate. DRR++ scheme is used to
 * pull interleaved packets out of the associated flows.
 *
 * @new_flows: linked list of fq_flow
 * @old_flows: linked list of fq_flow
 */
struct fq_tin {
	struct list_head new_flows;
	struct list_head old_flows;
	u32 backlog_bytes;
	u32 backlog_packets;
	u32 overlimit;
	u32 collisions;
	u32 flows;
	u32 tx_bytes;
	u32 tx_packets;
};

/**
 * struct fq - main container for fair queuing purposes
 *
 * @backlogs: linked to fq_flows. Used to maintain fat flows for efficient
 *	head-dropping when @backlog reaches @limit
 * @limit: max number of packets that can be queued across all flows
 * @backlog: number of packets queued across all flows
 */
struct fq {
	struct fq_flow *flows;
	struct list_head backlogs;
	spinlock_t lock;
	u32 flows_cnt;
	u32 limit;
	u32 memory_limit;
	u32 memory_usage;
	u32 quantum;
	u32 backlog;
	u32 overlimit;
	u32 overmemory;
	u32 collisions;
};

typedef struct sk_buff *fq_tin_dequeue_t(struct fq *,
					 struct fq_tin *,
					 struct fq_flow *flow);

typedef void fq_skb_free_t(struct fq *,
			   struct fq_tin *,
			   struct fq_flow *,
			   struct sk_buff *);

/* Return %true to filter (drop) the frame. */
typedef bool fq_skb_filter_t(struct fq *,
			     struct fq_tin *,
			     struct fq_flow *,
			     struct sk_buff *,
			     void *);

typedef struct fq_flow *fq_flow_get_default_t(struct fq *,
					      struct fq_tin *,
					      int idx,
					      struct sk_buff *);

#endif
