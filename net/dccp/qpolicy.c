/*
 *  net/dccp/qpolicy.c
 *
 *  Policy-based packet dequeueing interface for DCCP.
 *
 *  Copyright (c) 2008 Tomasz Grobelny <tomasz@grobelny.oswiecenia.net>
 *
 *  This program is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU General Public License v2
 *  as published by the Free Software Foundation.
 */
#include "dccp.h"

/*
 *	Simple Dequeueing Policy:
 *	If tx_qlen is different from 0, enqueue up to tx_qlen elements.
 */
static void qpolicy_simple_push(struct sock *sk, struct sk_buff *skb)
{
	skb_queue_tail(&sk->sk_write_queue, skb);
}

static bool qpolicy_simple_full(struct sock *sk)
{
	return dccp_sk(sk)->dccps_tx_qlen &&
	       sk->sk_write_queue.qlen >= dccp_sk(sk)->dccps_tx_qlen;
}

static struct sk_buff *qpolicy_simple_top(struct sock *sk)
{
	return skb_peek(&sk->sk_write_queue);
}

/*
 *	Priority-based Dequeueing Policy:
 *	If tx_qlen is different from 0 and the queue has reached its upper bound
 *	of tx_qlen elements, replace older packets lowest-priority-first.
 */
static struct sk_buff *qpolicy_prio_best_skb(struct sock *sk)
{
	struct sk_buff *skb, *best = NULL;

	skb_queue_walk(&sk->sk_write_queue, skb)
		if (best == NULL || skb->priority > best->priority)
			best = skb;
	return best;
}

static struct sk_buff *qpolicy_prio_worst_skb(struct sock *sk)
{
	struct sk_buff *skb, *worst = NULL;

	skb_queue_walk(&sk->sk_write_queue, skb)
		if (worst == NULL || skb->priority < worst->priority)
			worst = skb;
	return worst;
}

static bool qpolicy_prio_full(struct sock *sk)
{
	if (qpolicy_simple_full(sk))
		dccp_qpolicy_drop(sk, qpolicy_prio_worst_skb(sk));
	return false;
}

/**
 * struct dccp_qpolicy_operations  -  TX Packet Dequeueing Interface
 * @push: add a new @skb to the write queue
 * @full: indicates that no more packets will be admitted
 * @top:  peeks at whatever the queueing policy defines as its `top'
 */
static struct dccp_qpolicy_operations {
	void		(*push)	(struct sock *sk, struct sk_buff *skb);
	bool		(*full) (struct sock *sk);
	struct sk_buff*	(*top)  (struct sock *sk);

} qpol_table[DCCPQ_POLICY_MAX] = {
	[DCCPQ_POLICY_SIMPLE] = {
		.push = qpolicy_simple_push,
		.full = qpolicy_simple_full,
		.top  = qpolicy_simple_top,
	},
	[DCCPQ_POLICY_PRIO] = {
		.push = qpolicy_simple_push,
		.full = qpolicy_prio_full,
		.top  = qpolicy_prio_best_skb,
	},
};

/*
 *	Externally visible interface
 */
void dccp_qpolicy_push(struct sock *sk, struct sk_buff *skb)
{
	qpol_table[dccp_sk(sk)->dccps_qpolicy].push(sk, skb);
}

bool dccp_qpolicy_full(struct sock *sk)
{
	return qpol_table[dccp_sk(sk)->dccps_qpolicy].full(sk);
}

void dccp_qpolicy_drop(struct sock *sk, struct sk_buff *skb)
{
	if (skb != NULL) {
		skb_unlink(skb, &sk->sk_write_queue);
		kfree_skb(skb);
	}
}

struct sk_buff *dccp_qpolicy_top(struct sock *sk)
{
	return qpol_table[dccp_sk(sk)->dccps_qpolicy].top(sk);
}

struct sk_buff *dccp_qpolicy_pop(struct sock *sk)
{
	struct sk_buff *skb = dccp_qpolicy_top(sk);

	/* Clear any skb fields that we used internally */
	skb->priority = 0;

	if (skb)
		skb_unlink(skb, &sk->sk_write_queue);
	return skb;
}
