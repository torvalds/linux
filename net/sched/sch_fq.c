// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * net/sched/sch_fq.c Fair Queue Packet Scheduler (per flow pacing)
 *
 *  Copyright (C) 2013-2023 Eric Dumazet <edumazet@google.com>
 *
 *  Meant to be mostly used for locally generated traffic :
 *  Fast classification depends on skb->sk being set before reaching us.
 *  If not, (router workload), we use rxhash as fallback, with 32 bits wide hash.
 *  All packets belonging to a socket are considered as a 'flow'.
 *
 *  Flows are dynamically allocated and stored in a hash table of RB trees
 *  They are also part of one Round Robin 'queues' (new or old flows)
 *
 *  Burst avoidance (aka pacing) capability :
 *
 *  Transport (eg TCP) can set in sk->sk_pacing_rate a rate, enqueue a
 *  bunch of packets, and this packet scheduler adds delay between
 *  packets to respect rate limitation.
 *
 *  enqueue() :
 *   - lookup one RB tree (out of 1024 or more) to find the flow.
 *     If non existent flow, create it, add it to the tree.
 *     Add skb to the per flow list of skb (fifo).
 *   - Use a special fifo for high prio packets
 *
 *  dequeue() : serves flows in Round Robin
 *  Note : When a flow becomes empty, we do not immediately remove it from
 *  rb trees, for performance reasons (its expected to send additional packets,
 *  or SLAB cache will reuse socket for another flow)
 */

#include <linux/module.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/jiffies.h>
#include <linux/string.h>
#include <linux/in.h>
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/skbuff.h>
#include <linux/slab.h>
#include <linux/rbtree.h>
#include <linux/hash.h>
#include <linux/prefetch.h>
#include <linux/vmalloc.h>
#include <net/netlink.h>
#include <net/pkt_sched.h>
#include <net/sock.h>
#include <net/tcp_states.h>
#include <net/tcp.h>

struct fq_skb_cb {
	u64	time_to_send;
	u8	band;
};

static inline struct fq_skb_cb *fq_skb_cb(struct sk_buff *skb)
{
	qdisc_cb_private_validate(skb, sizeof(struct fq_skb_cb));
	return (struct fq_skb_cb *)qdisc_skb_cb(skb)->data;
}

/*
 * Per flow structure, dynamically allocated.
 * If packets have monotically increasing time_to_send, they are placed in O(1)
 * in linear list (head,tail), otherwise are placed in a rbtree (t_root).
 */
struct fq_flow {
/* First cache line : used in fq_gc(), fq_enqueue(), fq_dequeue() */
	struct rb_root	t_root;
	struct sk_buff	*head;		/* list of skbs for this flow : first skb */
	union {
		struct sk_buff *tail;	/* last skb in the list */
		unsigned long  age;	/* (jiffies | 1UL) when flow was emptied, for gc */
	};
	union {
		struct rb_node	fq_node;	/* anchor in fq_root[] trees */
		/* Following field is only used for q->internal,
		 * because q->internal is not hashed in fq_root[]
		 */
		u64		stat_fastpath_packets;
	};
	struct sock	*sk;
	u32		socket_hash;	/* sk_hash */
	int		qlen;		/* number of packets in flow queue */

/* Second cache line */
	int		credit;
	int		band;
	struct fq_flow *next;		/* next pointer in RR lists */

	struct rb_node  rate_node;	/* anchor in q->delayed tree */
	u64		time_next_packet;
};

struct fq_flow_head {
	struct fq_flow *first;
	struct fq_flow *last;
};

struct fq_perband_flows {
	struct fq_flow_head new_flows;
	struct fq_flow_head old_flows;
	int		    credit;
	int		    quantum; /* based on band nr : 576KB, 192KB, 64KB */
};

struct fq_sched_data {
/* Read mostly cache line */

	u32		quantum;
	u32		initial_quantum;
	u32		flow_refill_delay;
	u32		flow_plimit;	/* max packets per flow */
	unsigned long	flow_max_rate;	/* optional max rate per flow */
	u64		ce_threshold;
	u64		horizon;	/* horizon in ns */
	u32		orphan_mask;	/* mask for orphaned skb */
	u32		low_rate_threshold;
	struct rb_root	*fq_root;
	u8		rate_enable;
	u8		fq_trees_log;
	u8		horizon_drop;
	u8		prio2band[(TC_PRIO_MAX + 1) >> 2];
	u32		timer_slack; /* hrtimer slack in ns */

/* Read/Write fields. */

	unsigned int band_nr; /* band being serviced in fq_dequeue() */

	struct fq_perband_flows band_flows[FQ_BANDS];

	struct fq_flow	internal;	/* fastpath queue. */
	struct rb_root	delayed;	/* for rate limited flows */
	u64		time_next_delayed_flow;
	unsigned long	unthrottle_latency_ns;

	u32		band_pkt_count[FQ_BANDS];
	u32		flows;
	u32		inactive_flows; /* Flows with no packet to send. */
	u32		throttled_flows;

	u64		stat_throttled;
	struct qdisc_watchdog watchdog;
	u64		stat_gc_flows;

/* Seldom used fields. */

	u64		stat_band_drops[FQ_BANDS];
	u64		stat_ce_mark;
	u64		stat_horizon_drops;
	u64		stat_horizon_caps;
	u64		stat_flows_plimit;
	u64		stat_pkts_too_long;
	u64		stat_allocation_errors;
};

/* return the i-th 2-bit value ("crumb") */
static u8 fq_prio2band(const u8 *prio2band, unsigned int prio)
{
	return (prio2band[prio / 4] >> (2 * (prio & 0x3))) & 0x3;
}

/*
 * f->tail and f->age share the same location.
 * We can use the low order bit to differentiate if this location points
 * to a sk_buff or contains a jiffies value, if we force this value to be odd.
 * This assumes f->tail low order bit must be 0 since alignof(struct sk_buff) >= 2
 */
static void fq_flow_set_detached(struct fq_flow *f)
{
	f->age = jiffies | 1UL;
}

static bool fq_flow_is_detached(const struct fq_flow *f)
{
	return !!(f->age & 1UL);
}

/* special value to mark a throttled flow (not on old/new list) */
static struct fq_flow throttled;

static bool fq_flow_is_throttled(const struct fq_flow *f)
{
	return f->next == &throttled;
}

enum new_flow {
	NEW_FLOW,
	OLD_FLOW
};

static void fq_flow_add_tail(struct fq_sched_data *q, struct fq_flow *flow,
			     enum new_flow list_sel)
{
	struct fq_perband_flows *pband = &q->band_flows[flow->band];
	struct fq_flow_head *head = (list_sel == NEW_FLOW) ?
					&pband->new_flows :
					&pband->old_flows;

	if (head->first)
		head->last->next = flow;
	else
		head->first = flow;
	head->last = flow;
	flow->next = NULL;
}

static void fq_flow_unset_throttled(struct fq_sched_data *q, struct fq_flow *f)
{
	rb_erase(&f->rate_node, &q->delayed);
	q->throttled_flows--;
	fq_flow_add_tail(q, f, OLD_FLOW);
}

static void fq_flow_set_throttled(struct fq_sched_data *q, struct fq_flow *f)
{
	struct rb_node **p = &q->delayed.rb_node, *parent = NULL;

	while (*p) {
		struct fq_flow *aux;

		parent = *p;
		aux = rb_entry(parent, struct fq_flow, rate_node);
		if (f->time_next_packet >= aux->time_next_packet)
			p = &parent->rb_right;
		else
			p = &parent->rb_left;
	}
	rb_link_node(&f->rate_node, parent, p);
	rb_insert_color(&f->rate_node, &q->delayed);
	q->throttled_flows++;
	q->stat_throttled++;

	f->next = &throttled;
	if (q->time_next_delayed_flow > f->time_next_packet)
		q->time_next_delayed_flow = f->time_next_packet;
}


static struct kmem_cache *fq_flow_cachep __read_mostly;


/* limit number of collected flows per round */
#define FQ_GC_MAX 8
#define FQ_GC_AGE (3*HZ)

static bool fq_gc_candidate(const struct fq_flow *f)
{
	return fq_flow_is_detached(f) &&
	       time_after(jiffies, f->age + FQ_GC_AGE);
}

static void fq_gc(struct fq_sched_data *q,
		  struct rb_root *root,
		  struct sock *sk)
{
	struct rb_node **p, *parent;
	void *tofree[FQ_GC_MAX];
	struct fq_flow *f;
	int i, fcnt = 0;

	p = &root->rb_node;
	parent = NULL;
	while (*p) {
		parent = *p;

		f = rb_entry(parent, struct fq_flow, fq_node);
		if (f->sk == sk)
			break;

		if (fq_gc_candidate(f)) {
			tofree[fcnt++] = f;
			if (fcnt == FQ_GC_MAX)
				break;
		}

		if (f->sk > sk)
			p = &parent->rb_right;
		else
			p = &parent->rb_left;
	}

	if (!fcnt)
		return;

	for (i = fcnt; i > 0; ) {
		f = tofree[--i];
		rb_erase(&f->fq_node, root);
	}
	q->flows -= fcnt;
	q->inactive_flows -= fcnt;
	q->stat_gc_flows += fcnt;

	kmem_cache_free_bulk(fq_flow_cachep, fcnt, tofree);
}

/* Fast path can be used if :
 * 1) Packet tstamp is in the past.
 * 2) FQ qlen == 0   OR
 *   (no flow is currently eligible for transmit,
 *    AND fast path queue has less than 8 packets)
 * 3) No SO_MAX_PACING_RATE on the socket (if any).
 * 4) No @maxrate attribute on this qdisc,
 *
 * FQ can not use generic TCQ_F_CAN_BYPASS infrastructure.
 */
static bool fq_fastpath_check(const struct Qdisc *sch, struct sk_buff *skb,
			      u64 now)
{
	const struct fq_sched_data *q = qdisc_priv(sch);
	const struct sock *sk;

	if (fq_skb_cb(skb)->time_to_send > now)
		return false;

	if (sch->q.qlen != 0) {
		/* Even if some packets are stored in this qdisc,
		 * we can still enable fast path if all of them are
		 * scheduled in the future (ie no flows are eligible)
		 * or in the fast path queue.
		 */
		if (q->flows != q->inactive_flows + q->throttled_flows)
			return false;

		/* Do not allow fast path queue to explode, we want Fair Queue mode
		 * under pressure.
		 */
		if (q->internal.qlen >= 8)
			return false;
	}

	sk = skb->sk;
	if (sk && sk_fullsock(sk) && !sk_is_tcp(sk) &&
	    sk->sk_max_pacing_rate != ~0UL)
		return false;

	if (q->flow_max_rate != ~0UL)
		return false;

	return true;
}

static struct fq_flow *fq_classify(struct Qdisc *sch, struct sk_buff *skb,
				   u64 now)
{
	struct fq_sched_data *q = qdisc_priv(sch);
	struct rb_node **p, *parent;
	struct sock *sk = skb->sk;
	struct rb_root *root;
	struct fq_flow *f;

	/* SYNACK messages are attached to a TCP_NEW_SYN_RECV request socket
	 * or a listener (SYNCOOKIE mode)
	 * 1) request sockets are not full blown,
	 *    they do not contain sk_pacing_rate
	 * 2) They are not part of a 'flow' yet
	 * 3) We do not want to rate limit them (eg SYNFLOOD attack),
	 *    especially if the listener set SO_MAX_PACING_RATE
	 * 4) We pretend they are orphaned
	 */
	if (!sk || sk_listener(sk)) {
		unsigned long hash = skb_get_hash(skb) & q->orphan_mask;

		/* By forcing low order bit to 1, we make sure to not
		 * collide with a local flow (socket pointers are word aligned)
		 */
		sk = (struct sock *)((hash << 1) | 1UL);
		skb_orphan(skb);
	} else if (sk->sk_state == TCP_CLOSE) {
		unsigned long hash = skb_get_hash(skb) & q->orphan_mask;
		/*
		 * Sockets in TCP_CLOSE are non connected.
		 * Typical use case is UDP sockets, they can send packets
		 * with sendto() to many different destinations.
		 * We probably could use a generic bit advertising
		 * non connected sockets, instead of sk_state == TCP_CLOSE,
		 * if we care enough.
		 */
		sk = (struct sock *)((hash << 1) | 1UL);
	}

	if (fq_fastpath_check(sch, skb, now)) {
		q->internal.stat_fastpath_packets++;
		if (skb->sk == sk && q->rate_enable &&
		    READ_ONCE(sk->sk_pacing_status) != SK_PACING_FQ)
			smp_store_release(&sk->sk_pacing_status,
					  SK_PACING_FQ);
		return &q->internal;
	}

	root = &q->fq_root[hash_ptr(sk, q->fq_trees_log)];

	fq_gc(q, root, sk);

	p = &root->rb_node;
	parent = NULL;
	while (*p) {
		parent = *p;

		f = rb_entry(parent, struct fq_flow, fq_node);
		if (f->sk == sk) {
			/* socket might have been reallocated, so check
			 * if its sk_hash is the same.
			 * It not, we need to refill credit with
			 * initial quantum
			 */
			if (unlikely(skb->sk == sk &&
				     f->socket_hash != sk->sk_hash)) {
				f->credit = q->initial_quantum;
				f->socket_hash = sk->sk_hash;
				if (q->rate_enable)
					smp_store_release(&sk->sk_pacing_status,
							  SK_PACING_FQ);
				if (fq_flow_is_throttled(f))
					fq_flow_unset_throttled(q, f);
				f->time_next_packet = 0ULL;
			}
			return f;
		}
		if (f->sk > sk)
			p = &parent->rb_right;
		else
			p = &parent->rb_left;
	}

	f = kmem_cache_zalloc(fq_flow_cachep, GFP_ATOMIC | __GFP_NOWARN);
	if (unlikely(!f)) {
		q->stat_allocation_errors++;
		return &q->internal;
	}
	/* f->t_root is already zeroed after kmem_cache_zalloc() */

	fq_flow_set_detached(f);
	f->sk = sk;
	if (skb->sk == sk) {
		f->socket_hash = sk->sk_hash;
		if (q->rate_enable)
			smp_store_release(&sk->sk_pacing_status,
					  SK_PACING_FQ);
	}
	f->credit = q->initial_quantum;

	rb_link_node(&f->fq_node, parent, p);
	rb_insert_color(&f->fq_node, root);

	q->flows++;
	q->inactive_flows++;
	return f;
}

static struct sk_buff *fq_peek(struct fq_flow *flow)
{
	struct sk_buff *skb = skb_rb_first(&flow->t_root);
	struct sk_buff *head = flow->head;

	if (!skb)
		return head;

	if (!head)
		return skb;

	if (fq_skb_cb(skb)->time_to_send < fq_skb_cb(head)->time_to_send)
		return skb;
	return head;
}

static void fq_erase_head(struct Qdisc *sch, struct fq_flow *flow,
			  struct sk_buff *skb)
{
	if (skb == flow->head) {
		flow->head = skb->next;
	} else {
		rb_erase(&skb->rbnode, &flow->t_root);
		skb->dev = qdisc_dev(sch);
	}
}

/* Remove one skb from flow queue.
 * This skb must be the return value of prior fq_peek().
 */
static void fq_dequeue_skb(struct Qdisc *sch, struct fq_flow *flow,
			   struct sk_buff *skb)
{
	fq_erase_head(sch, flow, skb);
	skb_mark_not_on_list(skb);
	qdisc_qstats_backlog_dec(sch, skb);
	sch->q.qlen--;
}

static void flow_queue_add(struct fq_flow *flow, struct sk_buff *skb)
{
	struct rb_node **p, *parent;
	struct sk_buff *head, *aux;

	head = flow->head;
	if (!head ||
	    fq_skb_cb(skb)->time_to_send >= fq_skb_cb(flow->tail)->time_to_send) {
		if (!head)
			flow->head = skb;
		else
			flow->tail->next = skb;
		flow->tail = skb;
		skb->next = NULL;
		return;
	}

	p = &flow->t_root.rb_node;
	parent = NULL;

	while (*p) {
		parent = *p;
		aux = rb_to_skb(parent);
		if (fq_skb_cb(skb)->time_to_send >= fq_skb_cb(aux)->time_to_send)
			p = &parent->rb_right;
		else
			p = &parent->rb_left;
	}
	rb_link_node(&skb->rbnode, parent, p);
	rb_insert_color(&skb->rbnode, &flow->t_root);
}

static bool fq_packet_beyond_horizon(const struct sk_buff *skb,
				     const struct fq_sched_data *q, u64 now)
{
	return unlikely((s64)skb->tstamp > (s64)(now + q->horizon));
}

static int fq_enqueue(struct sk_buff *skb, struct Qdisc *sch,
		      struct sk_buff **to_free)
{
	struct fq_sched_data *q = qdisc_priv(sch);
	struct fq_flow *f;
	u64 now;
	u8 band;

	band = fq_prio2band(q->prio2band, skb->priority & TC_PRIO_MAX);
	if (unlikely(q->band_pkt_count[band] >= sch->limit)) {
		q->stat_band_drops[band]++;
		return qdisc_drop(skb, sch, to_free);
	}

	now = ktime_get_ns();
	if (!skb->tstamp) {
		fq_skb_cb(skb)->time_to_send = now;
	} else {
		/* Check if packet timestamp is too far in the future. */
		if (fq_packet_beyond_horizon(skb, q, now)) {
			if (q->horizon_drop) {
					q->stat_horizon_drops++;
					return qdisc_drop(skb, sch, to_free);
			}
			q->stat_horizon_caps++;
			skb->tstamp = now + q->horizon;
		}
		fq_skb_cb(skb)->time_to_send = skb->tstamp;
	}

	f = fq_classify(sch, skb, now);

	if (f != &q->internal) {
		if (unlikely(f->qlen >= q->flow_plimit)) {
			q->stat_flows_plimit++;
			return qdisc_drop(skb, sch, to_free);
		}

		if (fq_flow_is_detached(f)) {
			fq_flow_add_tail(q, f, NEW_FLOW);
			if (time_after(jiffies, f->age + q->flow_refill_delay))
				f->credit = max_t(u32, f->credit, q->quantum);
		}

		f->band = band;
		q->band_pkt_count[band]++;
		fq_skb_cb(skb)->band = band;
		if (f->qlen == 0)
			q->inactive_flows--;
	}

	f->qlen++;
	/* Note: this overwrites f->age */
	flow_queue_add(f, skb);

	qdisc_qstats_backlog_inc(sch, skb);
	sch->q.qlen++;

	return NET_XMIT_SUCCESS;
}

static void fq_check_throttled(struct fq_sched_data *q, u64 now)
{
	unsigned long sample;
	struct rb_node *p;

	if (q->time_next_delayed_flow > now)
		return;

	/* Update unthrottle latency EWMA.
	 * This is cheap and can help diagnosing timer/latency problems.
	 */
	sample = (unsigned long)(now - q->time_next_delayed_flow);
	q->unthrottle_latency_ns -= q->unthrottle_latency_ns >> 3;
	q->unthrottle_latency_ns += sample >> 3;

	q->time_next_delayed_flow = ~0ULL;
	while ((p = rb_first(&q->delayed)) != NULL) {
		struct fq_flow *f = rb_entry(p, struct fq_flow, rate_node);

		if (f->time_next_packet > now) {
			q->time_next_delayed_flow = f->time_next_packet;
			break;
		}
		fq_flow_unset_throttled(q, f);
	}
}

static struct fq_flow_head *fq_pband_head_select(struct fq_perband_flows *pband)
{
	if (pband->credit <= 0)
		return NULL;

	if (pband->new_flows.first)
		return &pband->new_flows;

	return pband->old_flows.first ? &pband->old_flows : NULL;
}

static struct sk_buff *fq_dequeue(struct Qdisc *sch)
{
	struct fq_sched_data *q = qdisc_priv(sch);
	struct fq_perband_flows *pband;
	struct fq_flow_head *head;
	struct sk_buff *skb;
	struct fq_flow *f;
	unsigned long rate;
	int retry;
	u32 plen;
	u64 now;

	if (!sch->q.qlen)
		return NULL;

	skb = fq_peek(&q->internal);
	if (unlikely(skb)) {
		q->internal.qlen--;
		fq_dequeue_skb(sch, &q->internal, skb);
		goto out;
	}

	now = ktime_get_ns();
	fq_check_throttled(q, now);
	retry = 0;
	pband = &q->band_flows[q->band_nr];
begin:
	head = fq_pband_head_select(pband);
	if (!head) {
		while (++retry <= FQ_BANDS) {
			if (++q->band_nr == FQ_BANDS)
				q->band_nr = 0;
			pband = &q->band_flows[q->band_nr];
			pband->credit = min(pband->credit + pband->quantum,
					    pband->quantum);
			goto begin;
		}
		if (q->time_next_delayed_flow != ~0ULL)
			qdisc_watchdog_schedule_range_ns(&q->watchdog,
							q->time_next_delayed_flow,
							q->timer_slack);
		return NULL;
	}
	f = head->first;
	retry = 0;
	if (f->credit <= 0) {
		f->credit += q->quantum;
		head->first = f->next;
		fq_flow_add_tail(q, f, OLD_FLOW);
		goto begin;
	}

	skb = fq_peek(f);
	if (skb) {
		u64 time_next_packet = max_t(u64, fq_skb_cb(skb)->time_to_send,
					     f->time_next_packet);

		if (now < time_next_packet) {
			head->first = f->next;
			f->time_next_packet = time_next_packet;
			fq_flow_set_throttled(q, f);
			goto begin;
		}
		prefetch(&skb->end);
		if ((s64)(now - time_next_packet - q->ce_threshold) > 0) {
			INET_ECN_set_ce(skb);
			q->stat_ce_mark++;
		}
		if (--f->qlen == 0)
			q->inactive_flows++;
		q->band_pkt_count[fq_skb_cb(skb)->band]--;
		fq_dequeue_skb(sch, f, skb);
	} else {
		head->first = f->next;
		/* force a pass through old_flows to prevent starvation */
		if (head == &pband->new_flows) {
			fq_flow_add_tail(q, f, OLD_FLOW);
		} else {
			fq_flow_set_detached(f);
		}
		goto begin;
	}
	plen = qdisc_pkt_len(skb);
	f->credit -= plen;
	pband->credit -= plen;

	if (!q->rate_enable)
		goto out;

	rate = q->flow_max_rate;

	/* If EDT time was provided for this skb, we need to
	 * update f->time_next_packet only if this qdisc enforces
	 * a flow max rate.
	 */
	if (!skb->tstamp) {
		if (skb->sk)
			rate = min(READ_ONCE(skb->sk->sk_pacing_rate), rate);

		if (rate <= q->low_rate_threshold) {
			f->credit = 0;
		} else {
			plen = max(plen, q->quantum);
			if (f->credit > 0)
				goto out;
		}
	}
	if (rate != ~0UL) {
		u64 len = (u64)plen * NSEC_PER_SEC;

		if (likely(rate))
			len = div64_ul(len, rate);
		/* Since socket rate can change later,
		 * clamp the delay to 1 second.
		 * Really, providers of too big packets should be fixed !
		 */
		if (unlikely(len > NSEC_PER_SEC)) {
			len = NSEC_PER_SEC;
			q->stat_pkts_too_long++;
		}
		/* Account for schedule/timers drifts.
		 * f->time_next_packet was set when prior packet was sent,
		 * and current time (@now) can be too late by tens of us.
		 */
		if (f->time_next_packet)
			len -= min(len/2, now - f->time_next_packet);
		f->time_next_packet = now + len;
	}
out:
	qdisc_bstats_update(sch, skb);
	return skb;
}

static void fq_flow_purge(struct fq_flow *flow)
{
	struct rb_node *p = rb_first(&flow->t_root);

	while (p) {
		struct sk_buff *skb = rb_to_skb(p);

		p = rb_next(p);
		rb_erase(&skb->rbnode, &flow->t_root);
		rtnl_kfree_skbs(skb, skb);
	}
	rtnl_kfree_skbs(flow->head, flow->tail);
	flow->head = NULL;
	flow->qlen = 0;
}

static void fq_reset(struct Qdisc *sch)
{
	struct fq_sched_data *q = qdisc_priv(sch);
	struct rb_root *root;
	struct rb_node *p;
	struct fq_flow *f;
	unsigned int idx;

	sch->q.qlen = 0;
	sch->qstats.backlog = 0;

	fq_flow_purge(&q->internal);

	if (!q->fq_root)
		return;

	for (idx = 0; idx < (1U << q->fq_trees_log); idx++) {
		root = &q->fq_root[idx];
		while ((p = rb_first(root)) != NULL) {
			f = rb_entry(p, struct fq_flow, fq_node);
			rb_erase(p, root);

			fq_flow_purge(f);

			kmem_cache_free(fq_flow_cachep, f);
		}
	}
	for (idx = 0; idx < FQ_BANDS; idx++) {
		q->band_flows[idx].new_flows.first = NULL;
		q->band_flows[idx].old_flows.first = NULL;
	}
	q->delayed		= RB_ROOT;
	q->flows		= 0;
	q->inactive_flows	= 0;
	q->throttled_flows	= 0;
}

static void fq_rehash(struct fq_sched_data *q,
		      struct rb_root *old_array, u32 old_log,
		      struct rb_root *new_array, u32 new_log)
{
	struct rb_node *op, **np, *parent;
	struct rb_root *oroot, *nroot;
	struct fq_flow *of, *nf;
	int fcnt = 0;
	u32 idx;

	for (idx = 0; idx < (1U << old_log); idx++) {
		oroot = &old_array[idx];
		while ((op = rb_first(oroot)) != NULL) {
			rb_erase(op, oroot);
			of = rb_entry(op, struct fq_flow, fq_node);
			if (fq_gc_candidate(of)) {
				fcnt++;
				kmem_cache_free(fq_flow_cachep, of);
				continue;
			}
			nroot = &new_array[hash_ptr(of->sk, new_log)];

			np = &nroot->rb_node;
			parent = NULL;
			while (*np) {
				parent = *np;

				nf = rb_entry(parent, struct fq_flow, fq_node);
				BUG_ON(nf->sk == of->sk);

				if (nf->sk > of->sk)
					np = &parent->rb_right;
				else
					np = &parent->rb_left;
			}

			rb_link_node(&of->fq_node, parent, np);
			rb_insert_color(&of->fq_node, nroot);
		}
	}
	q->flows -= fcnt;
	q->inactive_flows -= fcnt;
	q->stat_gc_flows += fcnt;
}

static void fq_free(void *addr)
{
	kvfree(addr);
}

static int fq_resize(struct Qdisc *sch, u32 log)
{
	struct fq_sched_data *q = qdisc_priv(sch);
	struct rb_root *array;
	void *old_fq_root;
	u32 idx;

	if (q->fq_root && log == q->fq_trees_log)
		return 0;

	/* If XPS was setup, we can allocate memory on right NUMA node */
	array = kvmalloc_node(sizeof(struct rb_root) << log, GFP_KERNEL | __GFP_RETRY_MAYFAIL,
			      netdev_queue_numa_node_read(sch->dev_queue));
	if (!array)
		return -ENOMEM;

	for (idx = 0; idx < (1U << log); idx++)
		array[idx] = RB_ROOT;

	sch_tree_lock(sch);

	old_fq_root = q->fq_root;
	if (old_fq_root)
		fq_rehash(q, old_fq_root, q->fq_trees_log, array, log);

	q->fq_root = array;
	q->fq_trees_log = log;

	sch_tree_unlock(sch);

	fq_free(old_fq_root);

	return 0;
}

static const struct netlink_range_validation iq_range = {
	.max = INT_MAX,
};

static const struct nla_policy fq_policy[TCA_FQ_MAX + 1] = {
	[TCA_FQ_UNSPEC]			= { .strict_start_type = TCA_FQ_TIMER_SLACK },

	[TCA_FQ_PLIMIT]			= { .type = NLA_U32 },
	[TCA_FQ_FLOW_PLIMIT]		= { .type = NLA_U32 },
	[TCA_FQ_QUANTUM]		= { .type = NLA_U32 },
	[TCA_FQ_INITIAL_QUANTUM]	= NLA_POLICY_FULL_RANGE(NLA_U32, &iq_range),
	[TCA_FQ_RATE_ENABLE]		= { .type = NLA_U32 },
	[TCA_FQ_FLOW_DEFAULT_RATE]	= { .type = NLA_U32 },
	[TCA_FQ_FLOW_MAX_RATE]		= { .type = NLA_U32 },
	[TCA_FQ_BUCKETS_LOG]		= { .type = NLA_U32 },
	[TCA_FQ_FLOW_REFILL_DELAY]	= { .type = NLA_U32 },
	[TCA_FQ_ORPHAN_MASK]		= { .type = NLA_U32 },
	[TCA_FQ_LOW_RATE_THRESHOLD]	= { .type = NLA_U32 },
	[TCA_FQ_CE_THRESHOLD]		= { .type = NLA_U32 },
	[TCA_FQ_TIMER_SLACK]		= { .type = NLA_U32 },
	[TCA_FQ_HORIZON]		= { .type = NLA_U32 },
	[TCA_FQ_HORIZON_DROP]		= { .type = NLA_U8 },
	[TCA_FQ_PRIOMAP]		= {
			.type = NLA_BINARY,
			.len = sizeof(struct tc_prio_qopt),
		},
	[TCA_FQ_WEIGHTS]		= {
			.type = NLA_BINARY,
			.len = FQ_BANDS * sizeof(s32),
		},
};

/* compress a u8 array with all elems <= 3 to an array of 2-bit fields */
static void fq_prio2band_compress_crumb(const u8 *in, u8 *out)
{
	const int num_elems = TC_PRIO_MAX + 1;
	int i;

	memset(out, 0, num_elems / 4);
	for (i = 0; i < num_elems; i++)
		out[i / 4] |= in[i] << (2 * (i & 0x3));
}

static void fq_prio2band_decompress_crumb(const u8 *in, u8 *out)
{
	const int num_elems = TC_PRIO_MAX + 1;
	int i;

	for (i = 0; i < num_elems; i++)
		out[i] = fq_prio2band(in, i);
}

static int fq_load_weights(struct fq_sched_data *q,
			   const struct nlattr *attr,
			   struct netlink_ext_ack *extack)
{
	s32 *weights = nla_data(attr);
	int i;

	for (i = 0; i < FQ_BANDS; i++) {
		if (weights[i] < FQ_MIN_WEIGHT) {
			NL_SET_ERR_MSG_FMT_MOD(extack, "Weight %d less that minimum allowed %d",
					       weights[i], FQ_MIN_WEIGHT);
			return -EINVAL;
		}
	}
	for (i = 0; i < FQ_BANDS; i++)
		q->band_flows[i].quantum = weights[i];
	return 0;
}

static int fq_load_priomap(struct fq_sched_data *q,
			   const struct nlattr *attr,
			   struct netlink_ext_ack *extack)
{
	const struct tc_prio_qopt *map = nla_data(attr);
	int i;

	if (map->bands != FQ_BANDS) {
		NL_SET_ERR_MSG_MOD(extack, "FQ only supports 3 bands");
		return -EINVAL;
	}
	for (i = 0; i < TC_PRIO_MAX + 1; i++) {
		if (map->priomap[i] >= FQ_BANDS) {
			NL_SET_ERR_MSG_FMT_MOD(extack, "FQ priomap field %d maps to a too high band %d",
					       i, map->priomap[i]);
			return -EINVAL;
		}
	}
	fq_prio2band_compress_crumb(map->priomap, q->prio2band);
	return 0;
}

static int fq_change(struct Qdisc *sch, struct nlattr *opt,
		     struct netlink_ext_ack *extack)
{
	struct fq_sched_data *q = qdisc_priv(sch);
	struct nlattr *tb[TCA_FQ_MAX + 1];
	int err, drop_count = 0;
	unsigned drop_len = 0;
	u32 fq_log;

	err = nla_parse_nested_deprecated(tb, TCA_FQ_MAX, opt, fq_policy,
					  NULL);
	if (err < 0)
		return err;

	sch_tree_lock(sch);

	fq_log = q->fq_trees_log;

	if (tb[TCA_FQ_BUCKETS_LOG]) {
		u32 nval = nla_get_u32(tb[TCA_FQ_BUCKETS_LOG]);

		if (nval >= 1 && nval <= ilog2(256*1024))
			fq_log = nval;
		else
			err = -EINVAL;
	}
	if (tb[TCA_FQ_PLIMIT])
		sch->limit = nla_get_u32(tb[TCA_FQ_PLIMIT]);

	if (tb[TCA_FQ_FLOW_PLIMIT])
		q->flow_plimit = nla_get_u32(tb[TCA_FQ_FLOW_PLIMIT]);

	if (tb[TCA_FQ_QUANTUM]) {
		u32 quantum = nla_get_u32(tb[TCA_FQ_QUANTUM]);

		if (quantum > 0 && quantum <= (1 << 20)) {
			q->quantum = quantum;
		} else {
			NL_SET_ERR_MSG_MOD(extack, "invalid quantum");
			err = -EINVAL;
		}
	}

	if (tb[TCA_FQ_INITIAL_QUANTUM])
		q->initial_quantum = nla_get_u32(tb[TCA_FQ_INITIAL_QUANTUM]);

	if (tb[TCA_FQ_FLOW_DEFAULT_RATE])
		pr_warn_ratelimited("sch_fq: defrate %u ignored.\n",
				    nla_get_u32(tb[TCA_FQ_FLOW_DEFAULT_RATE]));

	if (tb[TCA_FQ_FLOW_MAX_RATE]) {
		u32 rate = nla_get_u32(tb[TCA_FQ_FLOW_MAX_RATE]);

		q->flow_max_rate = (rate == ~0U) ? ~0UL : rate;
	}
	if (tb[TCA_FQ_LOW_RATE_THRESHOLD])
		q->low_rate_threshold =
			nla_get_u32(tb[TCA_FQ_LOW_RATE_THRESHOLD]);

	if (tb[TCA_FQ_RATE_ENABLE]) {
		u32 enable = nla_get_u32(tb[TCA_FQ_RATE_ENABLE]);

		if (enable <= 1)
			q->rate_enable = enable;
		else
			err = -EINVAL;
	}

	if (tb[TCA_FQ_FLOW_REFILL_DELAY]) {
		u32 usecs_delay = nla_get_u32(tb[TCA_FQ_FLOW_REFILL_DELAY]) ;

		q->flow_refill_delay = usecs_to_jiffies(usecs_delay);
	}

	if (!err && tb[TCA_FQ_PRIOMAP])
		err = fq_load_priomap(q, tb[TCA_FQ_PRIOMAP], extack);

	if (!err && tb[TCA_FQ_WEIGHTS])
		err = fq_load_weights(q, tb[TCA_FQ_WEIGHTS], extack);

	if (tb[TCA_FQ_ORPHAN_MASK])
		q->orphan_mask = nla_get_u32(tb[TCA_FQ_ORPHAN_MASK]);

	if (tb[TCA_FQ_CE_THRESHOLD])
		q->ce_threshold = (u64)NSEC_PER_USEC *
				  nla_get_u32(tb[TCA_FQ_CE_THRESHOLD]);

	if (tb[TCA_FQ_TIMER_SLACK])
		q->timer_slack = nla_get_u32(tb[TCA_FQ_TIMER_SLACK]);

	if (tb[TCA_FQ_HORIZON])
		q->horizon = (u64)NSEC_PER_USEC *
				  nla_get_u32(tb[TCA_FQ_HORIZON]);

	if (tb[TCA_FQ_HORIZON_DROP])
		q->horizon_drop = nla_get_u8(tb[TCA_FQ_HORIZON_DROP]);

	if (!err) {

		sch_tree_unlock(sch);
		err = fq_resize(sch, fq_log);
		sch_tree_lock(sch);
	}
	while (sch->q.qlen > sch->limit) {
		struct sk_buff *skb = fq_dequeue(sch);

		if (!skb)
			break;
		drop_len += qdisc_pkt_len(skb);
		rtnl_kfree_skbs(skb, skb);
		drop_count++;
	}
	qdisc_tree_reduce_backlog(sch, drop_count, drop_len);

	sch_tree_unlock(sch);
	return err;
}

static void fq_destroy(struct Qdisc *sch)
{
	struct fq_sched_data *q = qdisc_priv(sch);

	fq_reset(sch);
	fq_free(q->fq_root);
	qdisc_watchdog_cancel(&q->watchdog);
}

static int fq_init(struct Qdisc *sch, struct nlattr *opt,
		   struct netlink_ext_ack *extack)
{
	struct fq_sched_data *q = qdisc_priv(sch);
	int i, err;

	sch->limit		= 10000;
	q->flow_plimit		= 100;
	q->quantum		= 2 * psched_mtu(qdisc_dev(sch));
	q->initial_quantum	= 10 * psched_mtu(qdisc_dev(sch));
	q->flow_refill_delay	= msecs_to_jiffies(40);
	q->flow_max_rate	= ~0UL;
	q->time_next_delayed_flow = ~0ULL;
	q->rate_enable		= 1;
	for (i = 0; i < FQ_BANDS; i++) {
		q->band_flows[i].new_flows.first = NULL;
		q->band_flows[i].old_flows.first = NULL;
	}
	q->band_flows[0].quantum = 9 << 16;
	q->band_flows[1].quantum = 3 << 16;
	q->band_flows[2].quantum = 1 << 16;
	q->delayed		= RB_ROOT;
	q->fq_root		= NULL;
	q->fq_trees_log		= ilog2(1024);
	q->orphan_mask		= 1024 - 1;
	q->low_rate_threshold	= 550000 / 8;

	q->timer_slack = 10 * NSEC_PER_USEC; /* 10 usec of hrtimer slack */

	q->horizon = 10ULL * NSEC_PER_SEC; /* 10 seconds */
	q->horizon_drop = 1; /* by default, drop packets beyond horizon */

	/* Default ce_threshold of 4294 seconds */
	q->ce_threshold		= (u64)NSEC_PER_USEC * ~0U;

	fq_prio2band_compress_crumb(sch_default_prio2band, q->prio2band);
	qdisc_watchdog_init_clockid(&q->watchdog, sch, CLOCK_MONOTONIC);

	if (opt)
		err = fq_change(sch, opt, extack);
	else
		err = fq_resize(sch, q->fq_trees_log);

	return err;
}

static int fq_dump(struct Qdisc *sch, struct sk_buff *skb)
{
	struct fq_sched_data *q = qdisc_priv(sch);
	u64 ce_threshold = q->ce_threshold;
	struct tc_prio_qopt prio = {
		.bands = FQ_BANDS,
	};
	u64 horizon = q->horizon;
	struct nlattr *opts;
	s32 weights[3];

	opts = nla_nest_start_noflag(skb, TCA_OPTIONS);
	if (opts == NULL)
		goto nla_put_failure;

	/* TCA_FQ_FLOW_DEFAULT_RATE is not used anymore */

	do_div(ce_threshold, NSEC_PER_USEC);
	do_div(horizon, NSEC_PER_USEC);

	if (nla_put_u32(skb, TCA_FQ_PLIMIT, sch->limit) ||
	    nla_put_u32(skb, TCA_FQ_FLOW_PLIMIT, q->flow_plimit) ||
	    nla_put_u32(skb, TCA_FQ_QUANTUM, q->quantum) ||
	    nla_put_u32(skb, TCA_FQ_INITIAL_QUANTUM, q->initial_quantum) ||
	    nla_put_u32(skb, TCA_FQ_RATE_ENABLE, q->rate_enable) ||
	    nla_put_u32(skb, TCA_FQ_FLOW_MAX_RATE,
			min_t(unsigned long, q->flow_max_rate, ~0U)) ||
	    nla_put_u32(skb, TCA_FQ_FLOW_REFILL_DELAY,
			jiffies_to_usecs(q->flow_refill_delay)) ||
	    nla_put_u32(skb, TCA_FQ_ORPHAN_MASK, q->orphan_mask) ||
	    nla_put_u32(skb, TCA_FQ_LOW_RATE_THRESHOLD,
			q->low_rate_threshold) ||
	    nla_put_u32(skb, TCA_FQ_CE_THRESHOLD, (u32)ce_threshold) ||
	    nla_put_u32(skb, TCA_FQ_BUCKETS_LOG, q->fq_trees_log) ||
	    nla_put_u32(skb, TCA_FQ_TIMER_SLACK, q->timer_slack) ||
	    nla_put_u32(skb, TCA_FQ_HORIZON, (u32)horizon) ||
	    nla_put_u8(skb, TCA_FQ_HORIZON_DROP, q->horizon_drop))
		goto nla_put_failure;

	fq_prio2band_decompress_crumb(q->prio2band, prio.priomap);
	if (nla_put(skb, TCA_FQ_PRIOMAP, sizeof(prio), &prio))
		goto nla_put_failure;

	weights[0] = q->band_flows[0].quantum;
	weights[1] = q->band_flows[1].quantum;
	weights[2] = q->band_flows[2].quantum;
	if (nla_put(skb, TCA_FQ_WEIGHTS, sizeof(weights), &weights))
		goto nla_put_failure;

	return nla_nest_end(skb, opts);

nla_put_failure:
	return -1;
}

static int fq_dump_stats(struct Qdisc *sch, struct gnet_dump *d)
{
	struct fq_sched_data *q = qdisc_priv(sch);
	struct tc_fq_qd_stats st;
	int i;

	st.pad = 0;

	sch_tree_lock(sch);

	st.gc_flows		  = q->stat_gc_flows;
	st.highprio_packets	  = 0;
	st.fastpath_packets	  = q->internal.stat_fastpath_packets;
	st.tcp_retrans		  = 0;
	st.throttled		  = q->stat_throttled;
	st.flows_plimit		  = q->stat_flows_plimit;
	st.pkts_too_long	  = q->stat_pkts_too_long;
	st.allocation_errors	  = q->stat_allocation_errors;
	st.time_next_delayed_flow = q->time_next_delayed_flow + q->timer_slack -
				    ktime_get_ns();
	st.flows		  = q->flows;
	st.inactive_flows	  = q->inactive_flows;
	st.throttled_flows	  = q->throttled_flows;
	st.unthrottle_latency_ns  = min_t(unsigned long,
					  q->unthrottle_latency_ns, ~0U);
	st.ce_mark		  = q->stat_ce_mark;
	st.horizon_drops	  = q->stat_horizon_drops;
	st.horizon_caps		  = q->stat_horizon_caps;
	for (i = 0; i < FQ_BANDS; i++) {
		st.band_drops[i]  = q->stat_band_drops[i];
		st.band_pkt_count[i] = q->band_pkt_count[i];
	}
	sch_tree_unlock(sch);

	return gnet_stats_copy_app(d, &st, sizeof(st));
}

static struct Qdisc_ops fq_qdisc_ops __read_mostly = {
	.id		=	"fq",
	.priv_size	=	sizeof(struct fq_sched_data),

	.enqueue	=	fq_enqueue,
	.dequeue	=	fq_dequeue,
	.peek		=	qdisc_peek_dequeued,
	.init		=	fq_init,
	.reset		=	fq_reset,
	.destroy	=	fq_destroy,
	.change		=	fq_change,
	.dump		=	fq_dump,
	.dump_stats	=	fq_dump_stats,
	.owner		=	THIS_MODULE,
};

static int __init fq_module_init(void)
{
	int ret;

	fq_flow_cachep = kmem_cache_create("fq_flow_cache",
					   sizeof(struct fq_flow),
					   0, SLAB_HWCACHE_ALIGN, NULL);
	if (!fq_flow_cachep)
		return -ENOMEM;

	ret = register_qdisc(&fq_qdisc_ops);
	if (ret)
		kmem_cache_destroy(fq_flow_cachep);
	return ret;
}

static void __exit fq_module_exit(void)
{
	unregister_qdisc(&fq_qdisc_ops);
	kmem_cache_destroy(fq_flow_cachep);
}

module_init(fq_module_init)
module_exit(fq_module_exit)
MODULE_AUTHOR("Eric Dumazet");
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Fair Queue Packet Scheduler");
