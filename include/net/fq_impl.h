/*
 * Copyright (c) 2016 Qualcomm Atheros, Inc
 *
 * GPL v2
 *
 * Based on net/sched/sch_fq_codel.c
 */
#ifndef __NET_SCHED_FQ_IMPL_H
#define __NET_SCHED_FQ_IMPL_H

#include <net/fq.h>

/* functions that are embedded into includer */

static struct sk_buff *fq_flow_dequeue(struct fq *fq,
				       struct fq_flow *flow)
{
	struct fq_tin *tin = flow->tin;
	struct fq_flow *i;
	struct sk_buff *skb;

	lockdep_assert_held(&fq->lock);

	skb = __skb_dequeue(&flow->queue);
	if (!skb)
		return NULL;

	tin->backlog_bytes -= skb->len;
	tin->backlog_packets--;
	flow->backlog -= skb->len;
	fq->backlog--;
	fq->memory_usage -= skb->truesize;

	if (flow->backlog == 0) {
		list_del_init(&flow->backlogchain);
	} else {
		i = flow;

		list_for_each_entry_continue(i, &fq->backlogs, backlogchain)
			if (i->backlog < flow->backlog)
				break;

		list_move_tail(&flow->backlogchain,
			       &i->backlogchain);
	}

	return skb;
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

static struct fq_flow *fq_flow_classify(struct fq *fq,
					struct fq_tin *tin,
					struct sk_buff *skb,
					fq_flow_get_default_t get_default_func)
{
	struct fq_flow *flow;
	u32 hash;
	u32 idx;

	lockdep_assert_held(&fq->lock);

	hash = skb_get_hash_perturb(skb, fq->perturbation);
	idx = reciprocal_scale(hash, fq->flows_cnt);
	flow = &fq->flows[idx];

	if (flow->tin && flow->tin != tin) {
		flow = get_default_func(fq, tin, idx, skb);
		tin->collisions++;
		fq->collisions++;
	}

	if (!flow->tin)
		tin->flows++;

	return flow;
}

static void fq_recalc_backlog(struct fq *fq,
			      struct fq_tin *tin,
			      struct fq_flow *flow)
{
	struct fq_flow *i;

	if (list_empty(&flow->backlogchain))
		list_add_tail(&flow->backlogchain, &fq->backlogs);

	i = flow;
	list_for_each_entry_continue_reverse(i, &fq->backlogs,
					     backlogchain)
		if (i->backlog > flow->backlog)
			break;

	list_move(&flow->backlogchain, &i->backlogchain);
}

static void fq_tin_enqueue(struct fq *fq,
			   struct fq_tin *tin,
			   struct sk_buff *skb,
			   fq_skb_free_t free_func,
			   fq_flow_get_default_t get_default_func)
{
	struct fq_flow *flow;
	bool oom;

	lockdep_assert_held(&fq->lock);

	flow = fq_flow_classify(fq, tin, skb, get_default_func);

	flow->tin = tin;
	flow->backlog += skb->len;
	tin->backlog_bytes += skb->len;
	tin->backlog_packets++;
	fq->memory_usage += skb->truesize;
	fq->backlog++;

	fq_recalc_backlog(fq, tin, flow);

	if (list_empty(&flow->flowchain)) {
		flow->deficit = fq->quantum;
		list_add_tail(&flow->flowchain,
			      &tin->new_flows);
	}

	__skb_queue_tail(&flow->queue, skb);
	oom = (fq->memory_usage > fq->memory_limit);
	while (fq->backlog > fq->limit || oom) {
		flow = list_first_entry_or_null(&fq->backlogs,
						struct fq_flow,
						backlogchain);
		if (!flow)
			return;

		skb = fq_flow_dequeue(fq, flow);
		if (!skb)
			return;

		free_func(fq, flow->tin, flow, skb);

		flow->tin->overlimit++;
		fq->overlimit++;
		if (oom) {
			fq->overmemory++;
			oom = (fq->memory_usage > fq->memory_limit);
		}
	}
}

static void fq_flow_reset(struct fq *fq,
			  struct fq_flow *flow,
			  fq_skb_free_t free_func)
{
	struct sk_buff *skb;

	while ((skb = fq_flow_dequeue(fq, flow)))
		free_func(fq, flow->tin, flow, skb);

	if (!list_empty(&flow->flowchain))
		list_del_init(&flow->flowchain);

	if (!list_empty(&flow->backlogchain))
		list_del_init(&flow->backlogchain);

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

	WARN_ON_ONCE(tin->backlog_bytes);
	WARN_ON_ONCE(tin->backlog_packets);
}

static void fq_flow_init(struct fq_flow *flow)
{
	INIT_LIST_HEAD(&flow->flowchain);
	INIT_LIST_HEAD(&flow->backlogchain);
	__skb_queue_head_init(&flow->queue);
}

static void fq_tin_init(struct fq_tin *tin)
{
	INIT_LIST_HEAD(&tin->new_flows);
	INIT_LIST_HEAD(&tin->old_flows);
}

static int fq_init(struct fq *fq, int flows_cnt)
{
	int i;

	memset(fq, 0, sizeof(fq[0]));
	INIT_LIST_HEAD(&fq->backlogs);
	spin_lock_init(&fq->lock);
	fq->flows_cnt = max_t(u32, flows_cnt, 1);
	fq->perturbation = prandom_u32();
	fq->quantum = 300;
	fq->limit = 8192;
	fq->memory_limit = 16 << 20; /* 16 MBytes */

	fq->flows = kcalloc(fq->flows_cnt, sizeof(fq->flows[0]), GFP_KERNEL);
	if (!fq->flows)
		return -ENOMEM;

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

	kfree(fq->flows);
	fq->flows = NULL;
}

#endif
