/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2016 Qualcomm Atheros, Inc
 *
 * Based on net/sched/sch_fq_codel.c
 */
#ifndef __NET_SCHED_FQ_IMPL_H
#define __NET_SCHED_FQ_IMPL_H

#include <net/fq.h>

/* functions that are embedded into includer */


static void
__fq_adjust_removal(struct fq *fq, struct fq_flow *flow, unsigned int packets,
		    unsigned int bytes, unsigned int truesize)
{
	struct fq_tin *tin = flow->tin;
	int idx;

	tin->backlog_bytes -= bytes;
	tin->backlog_packets -= packets;
	flow->backlog -= bytes;
	fq->backlog -= packets;
	fq->memory_usage -= truesize;

	if (flow->backlog)
		return;

	if (flow == &tin->default_flow) {
		list_del_init(&tin->tin_list);
		return;
	}

	idx = flow - fq->flows;
	__clear_bit(idx, fq->flows_bitmap);
}

static void fq_adjust_removal(struct fq *fq,
			      struct fq_flow *flow,
			      struct sk_buff *skb)
{
	__fq_adjust_removal(fq, flow, 1, skb->len, skb->truesize);
}

static struct sk_buff *fq_flow_dequeue(struct fq *fq,
				       struct fq_flow *flow)
{
	struct sk_buff *skb;

	lockdep_assert_held(&fq->lock);

	skb = __skb_dequeue(&flow->queue);
	if (!skb)
		return NULL;

	fq_adjust_removal(fq, flow, skb);

	return skb;
}

static int fq_flow_drop(struct fq *fq, struct fq_flow *flow,
			fq_skb_free_t free_func)
{
	unsigned int packets = 0, bytes = 0, truesize = 0;
	struct fq_tin *tin = flow->tin;
	struct sk_buff *skb;
	int pending;

	lockdep_assert_held(&fq->lock);

	pending = min_t(int, 32, skb_queue_len(&flow->queue) / 2);
	do {
		skb = __skb_dequeue(&flow->queue);
		if (!skb)
			break;

		packets++;
		bytes += skb->len;
		truesize += skb->truesize;
		free_func(fq, tin, flow, skb);
	} while (packets < pending);

	__fq_adjust_removal(fq, flow, packets, bytes, truesize);

	return packets;
}

static struct sk_buff *fq_tin_dequeue(struct fq *fq,
				      struct fq_tin *tin,
				      fq_tin_dequeue_t dequeue_func)
{
	struct fq_flow *flow;
	struct list_head *head;
	struct sk_buff *skb;

	lockdep_assert_held(&fq->lock);

begin:
	head = &tin->new_flows;
	if (list_empty(head)) {
		head = &tin->old_flows;
		if (list_empty(head))
			return NULL;
	}

	flow = list_first_entry(head, struct fq_flow, flowchain);

	if (flow->deficit <= 0) {
		flow->deficit += fq->quantum;
		list_move_tail(&flow->flowchain,
			       &tin->old_flows);
		goto begin;
	}

	skb = dequeue_func(fq, tin, flow);
	if (!skb) {
		/* force a pass through old_flows to prevent starvation */
		if ((head == &tin->new_flows) &&
		    !list_empty(&tin->old_flows)) {
			list_move_tail(&flow->flowchain, &tin->old_flows);
		} else {
			list_del_init(&flow->flowchain);
			flow->tin = NULL;
		}
		goto begin;
	}

	flow->deficit -= skb->len;
	tin->tx_bytes += skb->len;
	tin->tx_packets++;

	return skb;
}

static u32 fq_flow_idx(struct fq *fq, struct sk_buff *skb)
{
	u32 hash = skb_get_hash(skb);

	return reciprocal_scale(hash, fq->flows_cnt);
}

static struct fq_flow *fq_flow_classify(struct fq *fq,
					struct fq_tin *tin, u32 idx,
					struct sk_buff *skb)
{
	struct fq_flow *flow;

	lockdep_assert_held(&fq->lock);

	flow = &fq->flows[idx];
	if (flow->tin && flow->tin != tin) {
		flow = &tin->default_flow;
		tin->collisions++;
		fq->collisions++;
	}

	if (!flow->tin)
		tin->flows++;

	return flow;
}

static struct fq_flow *fq_find_fattest_flow(struct fq *fq)
{
	struct fq_tin *tin;
	struct fq_flow *flow = NULL;
	u32 len = 0;
	int i;

	for_each_set_bit(i, fq->flows_bitmap, fq->flows_cnt) {
		struct fq_flow *cur = &fq->flows[i];
		unsigned int cur_len;

		cur_len = cur->backlog;
		if (cur_len <= len)
			continue;

		flow = cur;
		len = cur_len;
	}

	list_for_each_entry(tin, &fq->tin_backlog, tin_list) {
		unsigned int cur_len = tin->default_flow.backlog;

		if (cur_len <= len)
			continue;

		flow = &tin->default_flow;
		len = cur_len;
	}

	return flow;
}

static void fq_tin_enqueue(struct fq *fq,
			   struct fq_tin *tin, u32 idx,
			   struct sk_buff *skb,
			   fq_skb_free_t free_func)
{
	struct fq_flow *flow;
	bool oom;

	lockdep_assert_held(&fq->lock);

	flow = fq_flow_classify(fq, tin, idx, skb);

	if (!flow->backlog) {
		if (flow != &tin->default_flow)
			__set_bit(idx, fq->flows_bitmap);
		else if (list_empty(&tin->tin_list))
			list_add(&tin->tin_list, &fq->tin_backlog);
	}

	flow->tin = tin;
	flow->backlog += skb->len;
	tin->backlog_bytes += skb->len;
	tin->backlog_packets++;
	fq->memory_usage += skb->truesize;
	fq->backlog++;

	if (list_empty(&flow->flowchain)) {
		flow->deficit = fq->quantum;
		list_add_tail(&flow->flowchain,
			      &tin->new_flows);
	}

	__skb_queue_tail(&flow->queue, skb);
	oom = (fq->memory_usage > fq->memory_limit);
	while (fq->backlog > fq->limit || oom) {
		flow = fq_find_fattest_flow(fq);
		if (!flow)
			return;

		if (!fq_flow_drop(fq, flow, free_func))
			return;

		flow->tin->overlimit++;
		fq->overlimit++;
		if (oom) {
			fq->overmemory++;
			oom = (fq->memory_usage > fq->memory_limit);
		}
	}
}

static void fq_flow_filter(struct fq *fq,
			   struct fq_flow *flow,
			   fq_skb_filter_t filter_func,
			   void *filter_data,
			   fq_skb_free_t free_func)
{
	struct fq_tin *tin = flow->tin;
	struct sk_buff *skb, *tmp;

	lockdep_assert_held(&fq->lock);

	skb_queue_walk_safe(&flow->queue, skb, tmp) {
		if (!filter_func(fq, tin, flow, skb, filter_data))
			continue;

		__skb_unlink(skb, &flow->queue);
		fq_adjust_removal(fq, flow, skb);
		free_func(fq, tin, flow, skb);
	}
}

static void fq_tin_filter(struct fq *fq,
			  struct fq_tin *tin,
			  fq_skb_filter_t filter_func,
			  void *filter_data,
			  fq_skb_free_t free_func)
{
	struct fq_flow *flow;

	lockdep_assert_held(&fq->lock);

	list_for_each_entry(flow, &tin->new_flows, flowchain)
		fq_flow_filter(fq, flow, filter_func, filter_data, free_func);
	list_for_each_entry(flow, &tin->old_flows, flowchain)
		fq_flow_filter(fq, flow, filter_func, filter_data, free_func);
}

static void fq_flow_reset(struct fq *fq,
			  struct fq_flow *flow,
			  fq_skb_free_t free_func)
{
	struct fq_tin *tin = flow->tin;
	struct sk_buff *skb;

	while ((skb = fq_flow_dequeue(fq, flow)))
		free_func(fq, tin, flow, skb);

	if (!list_empty(&flow->flowchain)) {
		list_del_init(&flow->flowchain);
		if (list_empty(&tin->new_flows) &&
		    list_empty(&tin->old_flows))
			list_del_init(&tin->tin_list);
	}

	flow->tin = NULL;

	WARN_ON_ONCE(flow->backlog);
}

static void fq_tin_reset(struct fq *fq,
			 struct fq_tin *tin,
			 fq_skb_free_t free_func)
{
	struct list_head *head;
	struct fq_flow *flow;

	for (;;) {
		head = &tin->new_flows;
		if (list_empty(head)) {
			head = &tin->old_flows;
			if (list_empty(head))
				break;
		}

		flow = list_first_entry(head, struct fq_flow, flowchain);
		fq_flow_reset(fq, flow, free_func);
	}

	WARN_ON_ONCE(!list_empty(&tin->tin_list));
	WARN_ON_ONCE(tin->backlog_bytes);
	WARN_ON_ONCE(tin->backlog_packets);
}

static void fq_flow_init(struct fq_flow *flow)
{
	INIT_LIST_HEAD(&flow->flowchain);
	__skb_queue_head_init(&flow->queue);
}

static void fq_tin_init(struct fq_tin *tin)
{
	INIT_LIST_HEAD(&tin->new_flows);
	INIT_LIST_HEAD(&tin->old_flows);
	INIT_LIST_HEAD(&tin->tin_list);
	fq_flow_init(&tin->default_flow);
}

static int fq_init(struct fq *fq, int flows_cnt)
{
	int i;

	memset(fq, 0, sizeof(fq[0]));
	spin_lock_init(&fq->lock);
	INIT_LIST_HEAD(&fq->tin_backlog);
	fq->flows_cnt = max_t(u32, flows_cnt, 1);
	fq->quantum = 300;
	fq->limit = 8192;
	fq->memory_limit = 16 << 20; /* 16 MBytes */

	fq->flows = kvcalloc(fq->flows_cnt, sizeof(fq->flows[0]), GFP_KERNEL);
	if (!fq->flows)
		return -ENOMEM;

	fq->flows_bitmap = bitmap_zalloc(fq->flows_cnt, GFP_KERNEL);
	if (!fq->flows_bitmap) {
		kvfree(fq->flows);
		fq->flows = NULL;
		return -ENOMEM;
	}

	for (i = 0; i < fq->flows_cnt; i++)
		fq_flow_init(&fq->flows[i]);

	return 0;
}

static void fq_reset(struct fq *fq,
		     fq_skb_free_t free_func)
{
	int i;

	for (i = 0; i < fq->flows_cnt; i++)
		fq_flow_reset(fq, &fq->flows[i], free_func);

	kvfree(fq->flows);
	fq->flows = NULL;

	bitmap_free(fq->flows_bitmap);
	fq->flows_bitmap = NULL;
}

#endif
