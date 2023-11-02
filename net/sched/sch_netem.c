// SPDX-License-Identifier: GPL-2.0-only
/*
 * net/sched/sch_netem.c	Network emulator
 *
 *  		Many of the algorithms and ideas for this came from
 *		NIST Net which is not copyrighted.
 *
 * Authors:	Stephen Hemminger <shemminger@osdl.org>
 *		Catalin(ux aka Dino) BOIE <catab at umbrella dot ro>
 */

#include <linux/mm.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/skbuff.h>
#include <linux/vmalloc.h>
#include <linux/rtnetlink.h>
#include <linux/reciprocal_div.h>
#include <linux/rbtree.h>

#include <net/gso.h>
#include <net/netlink.h>
#include <net/pkt_sched.h>
#include <net/inet_ecn.h>

#define VERSION "1.3"

/*	Network Emulation Queuing algorithm.
	====================================

	Sources: [1] Mark Carson, Darrin Santay, "NIST Net - A Linux-based
		 Network Emulation Tool
		 [2] Luigi Rizzo, DummyNet for FreeBSD

	 ----------------------------------------------------------------

	 This started out as a simple way to delay outgoing packets to
	 test TCP but has grown to include most of the functionality
	 of a full blown network emulator like NISTnet. It can delay
	 packets and add random jitter (and correlation). The random
	 distribution can be loaded from a table as well to provide
	 normal, Pareto, or experimental curves. Packet loss,
	 duplication, and reordering can also be emulated.

	 This qdisc does not do classification that can be handled in
	 layering other disciplines.  It does not need to do bandwidth
	 control either since that can be handled by using token
	 bucket or other rate control.

     Correlated Loss Generator models

	Added generation of correlated loss according to the
	"Gilbert-Elliot" model, a 4-state markov model.

	References:
	[1] NetemCLG Home http://netgroup.uniroma2.it/NetemCLG
	[2] S. Salsano, F. Ludovici, A. Ordine, "Definition of a general
	and intuitive loss model for packet networks and its implementation
	in the Netem module in the Linux kernel", available in [1]

	Authors: Stefano Salsano <stefano.salsano at uniroma2.it
		 Fabio Ludovici <fabio.ludovici at yahoo.it>
*/

struct disttable {
	u32  size;
	s16 table[] __counted_by(size);
};

struct netem_sched_data {
	/* internal t(ime)fifo qdisc uses t_root and sch->limit */
	struct rb_root t_root;

	/* a linear queue; reduces rbtree rebalancing when jitter is low */
	struct sk_buff	*t_head;
	struct sk_buff	*t_tail;

	/* optional qdisc for classful handling (NULL at netem init) */
	struct Qdisc	*qdisc;

	struct qdisc_watchdog watchdog;

	s64 latency;
	s64 jitter;

	u32 loss;
	u32 ecn;
	u32 limit;
	u32 counter;
	u32 gap;
	u32 duplicate;
	u32 reorder;
	u32 corrupt;
	u64 rate;
	s32 packet_overhead;
	u32 cell_size;
	struct reciprocal_value cell_size_reciprocal;
	s32 cell_overhead;

	struct crndstate {
		u32 last;
		u32 rho;
	} delay_cor, loss_cor, dup_cor, reorder_cor, corrupt_cor;

	struct prng  {
		u64 seed;
		struct rnd_state prng_state;
	} prng;

	struct disttable *delay_dist;

	enum  {
		CLG_RANDOM,
		CLG_4_STATES,
		CLG_GILB_ELL,
	} loss_model;

	enum {
		TX_IN_GAP_PERIOD = 1,
		TX_IN_BURST_PERIOD,
		LOST_IN_GAP_PERIOD,
		LOST_IN_BURST_PERIOD,
	} _4_state_model;

	enum {
		GOOD_STATE = 1,
		BAD_STATE,
	} GE_state_model;

	/* Correlated Loss Generation models */
	struct clgstate {
		/* state of the Markov chain */
		u8 state;

		/* 4-states and Gilbert-Elliot models */
		u32 a1;	/* p13 for 4-states or p for GE */
		u32 a2;	/* p31 for 4-states or r for GE */
		u32 a3;	/* p32 for 4-states or h for GE */
		u32 a4;	/* p14 for 4-states or 1-k for GE */
		u32 a5; /* p23 used only in 4-states */
	} clg;

	struct tc_netem_slot slot_config;
	struct slotstate {
		u64 slot_next;
		s32 packets_left;
		s32 bytes_left;
	} slot;

	struct disttable *slot_dist;
};

/* Time stamp put into socket buffer control block
 * Only valid when skbs are in our internal t(ime)fifo queue.
 *
 * As skb->rbnode uses same storage than skb->next, skb->prev and skb->tstamp,
 * and skb->next & skb->prev are scratch space for a qdisc,
 * we save skb->tstamp value in skb->cb[] before destroying it.
 */
struct netem_skb_cb {
	u64	        time_to_send;
};

static inline struct netem_skb_cb *netem_skb_cb(struct sk_buff *skb)
{
	/* we assume we can use skb next/prev/tstamp as storage for rb_node */
	qdisc_cb_private_validate(skb, sizeof(struct netem_skb_cb));
	return (struct netem_skb_cb *)qdisc_skb_cb(skb)->data;
}

/* init_crandom - initialize correlated random number generator
 * Use entropy source for initial seed.
 */
static void init_crandom(struct crndstate *state, unsigned long rho)
{
	state->rho = rho;
	state->last = get_random_u32();
}

/* get_crandom - correlated random number generator
 * Next number depends on last value.
 * rho is scaled to avoid floating point.
 */
static u32 get_crandom(struct crndstate *state, struct prng *p)
{
	u64 value, rho;
	unsigned long answer;
	struct rnd_state *s = &p->prng_state;

	if (!state || state->rho == 0)	/* no correlation */
		return prandom_u32_state(s);

	value = prandom_u32_state(s);
	rho = (u64)state->rho + 1;
	answer = (value * ((1ull<<32) - rho) + state->last * rho) >> 32;
	state->last = answer;
	return answer;
}

/* loss_4state - 4-state model loss generator
 * Generates losses according to the 4-state Markov chain adopted in
 * the GI (General and Intuitive) loss model.
 */
static bool loss_4state(struct netem_sched_data *q)
{
	struct clgstate *clg = &q->clg;
	u32 rnd = prandom_u32_state(&q->prng.prng_state);

	/*
	 * Makes a comparison between rnd and the transition
	 * probabilities outgoing from the current state, then decides the
	 * next state and if the next packet has to be transmitted or lost.
	 * The four states correspond to:
	 *   TX_IN_GAP_PERIOD => successfully transmitted packets within a gap period
	 *   LOST_IN_GAP_PERIOD => isolated losses within a gap period
	 *   LOST_IN_BURST_PERIOD => lost packets within a burst period
	 *   TX_IN_BURST_PERIOD => successfully transmitted packets within a burst period
	 */
	switch (clg->state) {
	case TX_IN_GAP_PERIOD:
		if (rnd < clg->a4) {
			clg->state = LOST_IN_GAP_PERIOD;
			return true;
		} else if (clg->a4 < rnd && rnd < clg->a1 + clg->a4) {
			clg->state = LOST_IN_BURST_PERIOD;
			return true;
		} else if (clg->a1 + clg->a4 < rnd) {
			clg->state = TX_IN_GAP_PERIOD;
		}

		break;
	case TX_IN_BURST_PERIOD:
		if (rnd < clg->a5) {
			clg->state = LOST_IN_BURST_PERIOD;
			return true;
		} else {
			clg->state = TX_IN_BURST_PERIOD;
		}

		break;
	case LOST_IN_BURST_PERIOD:
		if (rnd < clg->a3)
			clg->state = TX_IN_BURST_PERIOD;
		else if (clg->a3 < rnd && rnd < clg->a2 + clg->a3) {
			clg->state = TX_IN_GAP_PERIOD;
		} else if (clg->a2 + clg->a3 < rnd) {
			clg->state = LOST_IN_BURST_PERIOD;
			return true;
		}
		break;
	case LOST_IN_GAP_PERIOD:
		clg->state = TX_IN_GAP_PERIOD;
		break;
	}

	return false;
}

/* loss_gilb_ell - Gilbert-Elliot model loss generator
 * Generates losses according to the Gilbert-Elliot loss model or
 * its special cases  (Gilbert or Simple Gilbert)
 *
 * Makes a comparison between random number and the transition
 * probabilities outgoing from the current state, then decides the
 * next state. A second random number is extracted and the comparison
 * with the loss probability of the current state decides if the next
 * packet will be transmitted or lost.
 */
static bool loss_gilb_ell(struct netem_sched_data *q)
{
	struct clgstate *clg = &q->clg;
	struct rnd_state *s = &q->prng.prng_state;

	switch (clg->state) {
	case GOOD_STATE:
		if (prandom_u32_state(s) < clg->a1)
			clg->state = BAD_STATE;
		if (prandom_u32_state(s) < clg->a4)
			return true;
		break;
	case BAD_STATE:
		if (prandom_u32_state(s) < clg->a2)
			clg->state = GOOD_STATE;
		if (prandom_u32_state(s) > clg->a3)
			return true;
	}

	return false;
}

static bool loss_event(struct netem_sched_data *q)
{
	switch (q->loss_model) {
	case CLG_RANDOM:
		/* Random packet drop 0 => none, ~0 => all */
		return q->loss && q->loss >= get_crandom(&q->loss_cor, &q->prng);

	case CLG_4_STATES:
		/* 4state loss model algorithm (used also for GI model)
		* Extracts a value from the markov 4 state loss generator,
		* if it is 1 drops a packet and if needed writes the event in
		* the kernel logs
		*/
		return loss_4state(q);

	case CLG_GILB_ELL:
		/* Gilbert-Elliot loss model algorithm
		* Extracts a value from the Gilbert-Elliot loss generator,
		* if it is 1 drops a packet and if needed writes the event in
		* the kernel logs
		*/
		return loss_gilb_ell(q);
	}

	return false;	/* not reached */
}


/* tabledist - return a pseudo-randomly distributed value with mean mu and
 * std deviation sigma.  Uses table lookup to approximate the desired
 * distribution, and a uniformly-distributed pseudo-random source.
 */
static s64 tabledist(s64 mu, s32 sigma,
		     struct crndstate *state,
		     struct prng *prng,
		     const struct disttable *dist)
{
	s64 x;
	long t;
	u32 rnd;

	if (sigma == 0)
		return mu;

	rnd = get_crandom(state, prng);

	/* default uniform distribution */
	if (dist == NULL)
		return ((rnd % (2 * (u32)sigma)) + mu) - sigma;

	t = dist->table[rnd % dist->size];
	x = (sigma % NETEM_DIST_SCALE) * t;
	if (x >= 0)
		x += NETEM_DIST_SCALE/2;
	else
		x -= NETEM_DIST_SCALE/2;

	return  x / NETEM_DIST_SCALE + (sigma / NETEM_DIST_SCALE) * t + mu;
}

static u64 packet_time_ns(u64 len, const struct netem_sched_data *q)
{
	len += q->packet_overhead;

	if (q->cell_size) {
		u32 cells = reciprocal_divide(len, q->cell_size_reciprocal);

		if (len > cells * q->cell_size)	/* extra cell needed for remainder */
			cells++;
		len = cells * (q->cell_size + q->cell_overhead);
	}

	return div64_u64(len * NSEC_PER_SEC, q->rate);
}

static void tfifo_reset(struct Qdisc *sch)
{
	struct netem_sched_data *q = qdisc_priv(sch);
	struct rb_node *p = rb_first(&q->t_root);

	while (p) {
		struct sk_buff *skb = rb_to_skb(p);

		p = rb_next(p);
		rb_erase(&skb->rbnode, &q->t_root);
		rtnl_kfree_skbs(skb, skb);
	}

	rtnl_kfree_skbs(q->t_head, q->t_tail);
	q->t_head = NULL;
	q->t_tail = NULL;
}

static void tfifo_enqueue(struct sk_buff *nskb, struct Qdisc *sch)
{
	struct netem_sched_data *q = qdisc_priv(sch);
	u64 tnext = netem_skb_cb(nskb)->time_to_send;

	if (!q->t_tail || tnext >= netem_skb_cb(q->t_tail)->time_to_send) {
		if (q->t_tail)
			q->t_tail->next = nskb;
		else
			q->t_head = nskb;
		q->t_tail = nskb;
	} else {
		struct rb_node **p = &q->t_root.rb_node, *parent = NULL;

		while (*p) {
			struct sk_buff *skb;

			parent = *p;
			skb = rb_to_skb(parent);
			if (tnext >= netem_skb_cb(skb)->time_to_send)
				p = &parent->rb_right;
			else
				p = &parent->rb_left;
		}
		rb_link_node(&nskb->rbnode, parent, p);
		rb_insert_color(&nskb->rbnode, &q->t_root);
	}
	sch->q.qlen++;
}

/* netem can't properly corrupt a megapacket (like we get from GSO), so instead
 * when we statistically choose to corrupt one, we instead segment it, returning
 * the first packet to be corrupted, and re-enqueue the remaining frames
 */
static struct sk_buff *netem_segment(struct sk_buff *skb, struct Qdisc *sch,
				     struct sk_buff **to_free)
{
	struct sk_buff *segs;
	netdev_features_t features = netif_skb_features(skb);

	segs = skb_gso_segment(skb, features & ~NETIF_F_GSO_MASK);

	if (IS_ERR_OR_NULL(segs)) {
		qdisc_drop(skb, sch, to_free);
		return NULL;
	}
	consume_skb(skb);
	return segs;
}

/*
 * Insert one skb into qdisc.
 * Note: parent depends on return value to account for queue length.
 * 	NET_XMIT_DROP: queue length didn't change.
 *      NET_XMIT_SUCCESS: one skb was queued.
 */
static int netem_enqueue(struct sk_buff *skb, struct Qdisc *sch,
			 struct sk_buff **to_free)
{
	struct netem_sched_data *q = qdisc_priv(sch);
	/* We don't fill cb now as skb_unshare() may invalidate it */
	struct netem_skb_cb *cb;
	struct sk_buff *skb2;
	struct sk_buff *segs = NULL;
	unsigned int prev_len = qdisc_pkt_len(skb);
	int count = 1;
	int rc = NET_XMIT_SUCCESS;
	int rc_drop = NET_XMIT_DROP;

	/* Do not fool qdisc_drop_all() */
	skb->prev = NULL;

	/* Random duplication */
	if (q->duplicate && q->duplicate >= get_crandom(&q->dup_cor, &q->prng))
		++count;

	/* Drop packet? */
	if (loss_event(q)) {
		if (q->ecn && INET_ECN_set_ce(skb))
			qdisc_qstats_drop(sch); /* mark packet */
		else
			--count;
	}
	if (count == 0) {
		qdisc_qstats_drop(sch);
		__qdisc_drop(skb, to_free);
		return NET_XMIT_SUCCESS | __NET_XMIT_BYPASS;
	}

	/* If a delay is expected, orphan the skb. (orphaning usually takes
	 * place at TX completion time, so _before_ the link transit delay)
	 */
	if (q->latency || q->jitter || q->rate)
		skb_orphan_partial(skb);

	/*
	 * If we need to duplicate packet, then re-insert at top of the
	 * qdisc tree, since parent queuer expects that only one
	 * skb will be queued.
	 */
	if (count > 1 && (skb2 = skb_clone(skb, GFP_ATOMIC)) != NULL) {
		struct Qdisc *rootq = qdisc_root_bh(sch);
		u32 dupsave = q->duplicate; /* prevent duplicating a dup... */

		q->duplicate = 0;
		rootq->enqueue(skb2, rootq, to_free);
		q->duplicate = dupsave;
		rc_drop = NET_XMIT_SUCCESS;
	}

	/*
	 * Randomized packet corruption.
	 * Make copy if needed since we are modifying
	 * If packet is going to be hardware checksummed, then
	 * do it now in software before we mangle it.
	 */
	if (q->corrupt && q->corrupt >= get_crandom(&q->corrupt_cor, &q->prng)) {
		if (skb_is_gso(skb)) {
			skb = netem_segment(skb, sch, to_free);
			if (!skb)
				return rc_drop;
			segs = skb->next;
			skb_mark_not_on_list(skb);
			qdisc_skb_cb(skb)->pkt_len = skb->len;
		}

		skb = skb_unshare(skb, GFP_ATOMIC);
		if (unlikely(!skb)) {
			qdisc_qstats_drop(sch);
			goto finish_segs;
		}
		if (skb->ip_summed == CHECKSUM_PARTIAL &&
		    skb_checksum_help(skb)) {
			qdisc_drop(skb, sch, to_free);
			skb = NULL;
			goto finish_segs;
		}

		skb->data[get_random_u32_below(skb_headlen(skb))] ^=
			1<<get_random_u32_below(8);
	}

	if (unlikely(sch->q.qlen >= sch->limit)) {
		/* re-link segs, so that qdisc_drop_all() frees them all */
		skb->next = segs;
		qdisc_drop_all(skb, sch, to_free);
		return rc_drop;
	}

	qdisc_qstats_backlog_inc(sch, skb);

	cb = netem_skb_cb(skb);
	if (q->gap == 0 ||		/* not doing reordering */
	    q->counter < q->gap - 1 ||	/* inside last reordering gap */
	    q->reorder < get_crandom(&q->reorder_cor, &q->prng)) {
		u64 now;
		s64 delay;

		delay = tabledist(q->latency, q->jitter,
				  &q->delay_cor, &q->prng, q->delay_dist);

		now = ktime_get_ns();

		if (q->rate) {
			struct netem_skb_cb *last = NULL;

			if (sch->q.tail)
				last = netem_skb_cb(sch->q.tail);
			if (q->t_root.rb_node) {
				struct sk_buff *t_skb;
				struct netem_skb_cb *t_last;

				t_skb = skb_rb_last(&q->t_root);
				t_last = netem_skb_cb(t_skb);
				if (!last ||
				    t_last->time_to_send > last->time_to_send)
					last = t_last;
			}
			if (q->t_tail) {
				struct netem_skb_cb *t_last =
					netem_skb_cb(q->t_tail);

				if (!last ||
				    t_last->time_to_send > last->time_to_send)
					last = t_last;
			}

			if (last) {
				/*
				 * Last packet in queue is reference point (now),
				 * calculate this time bonus and subtract
				 * from delay.
				 */
				delay -= last->time_to_send - now;
				delay = max_t(s64, 0, delay);
				now = last->time_to_send;
			}

			delay += packet_time_ns(qdisc_pkt_len(skb), q);
		}

		cb->time_to_send = now + delay;
		++q->counter;
		tfifo_enqueue(skb, sch);
	} else {
		/*
		 * Do re-ordering by putting one out of N packets at the front
		 * of the queue.
		 */
		cb->time_to_send = ktime_get_ns();
		q->counter = 0;

		__qdisc_enqueue_head(skb, &sch->q);
		sch->qstats.requeues++;
	}

finish_segs:
	if (segs) {
		unsigned int len, last_len;
		int nb;

		len = skb ? skb->len : 0;
		nb = skb ? 1 : 0;

		while (segs) {
			skb2 = segs->next;
			skb_mark_not_on_list(segs);
			qdisc_skb_cb(segs)->pkt_len = segs->len;
			last_len = segs->len;
			rc = qdisc_enqueue(segs, sch, to_free);
			if (rc != NET_XMIT_SUCCESS) {
				if (net_xmit_drop_count(rc))
					qdisc_qstats_drop(sch);
			} else {
				nb++;
				len += last_len;
			}
			segs = skb2;
		}
		/* Parent qdiscs accounted for 1 skb of size @prev_len */
		qdisc_tree_reduce_backlog(sch, -(nb - 1), -(len - prev_len));
	} else if (!skb) {
		return NET_XMIT_DROP;
	}
	return NET_XMIT_SUCCESS;
}

/* Delay the next round with a new future slot with a
 * correct number of bytes and packets.
 */

static void get_slot_next(struct netem_sched_data *q, u64 now)
{
	s64 next_delay;

	if (!q->slot_dist)
		next_delay = q->slot_config.min_delay +
				(get_random_u32() *
				 (q->slot_config.max_delay -
				  q->slot_config.min_delay) >> 32);
	else
		next_delay = tabledist(q->slot_config.dist_delay,
				       (s32)(q->slot_config.dist_jitter),
				       NULL, &q->prng, q->slot_dist);

	q->slot.slot_next = now + next_delay;
	q->slot.packets_left = q->slot_config.max_packets;
	q->slot.bytes_left = q->slot_config.max_bytes;
}

static struct sk_buff *netem_peek(struct netem_sched_data *q)
{
	struct sk_buff *skb = skb_rb_first(&q->t_root);
	u64 t1, t2;

	if (!skb)
		return q->t_head;
	if (!q->t_head)
		return skb;

	t1 = netem_skb_cb(skb)->time_to_send;
	t2 = netem_skb_cb(q->t_head)->time_to_send;
	if (t1 < t2)
		return skb;
	return q->t_head;
}

static void netem_erase_head(struct netem_sched_data *q, struct sk_buff *skb)
{
	if (skb == q->t_head) {
		q->t_head = skb->next;
		if (!q->t_head)
			q->t_tail = NULL;
	} else {
		rb_erase(&skb->rbnode, &q->t_root);
	}
}

static struct sk_buff *netem_dequeue(struct Qdisc *sch)
{
	struct netem_sched_data *q = qdisc_priv(sch);
	struct sk_buff *skb;

tfifo_dequeue:
	skb = __qdisc_dequeue_head(&sch->q);
	if (skb) {
		qdisc_qstats_backlog_dec(sch, skb);
deliver:
		qdisc_bstats_update(sch, skb);
		return skb;
	}
	skb = netem_peek(q);
	if (skb) {
		u64 time_to_send;
		u64 now = ktime_get_ns();

		/* if more time remaining? */
		time_to_send = netem_skb_cb(skb)->time_to_send;
		if (q->slot.slot_next && q->slot.slot_next < time_to_send)
			get_slot_next(q, now);

		if (time_to_send <= now && q->slot.slot_next <= now) {
			netem_erase_head(q, skb);
			sch->q.qlen--;
			qdisc_qstats_backlog_dec(sch, skb);
			skb->next = NULL;
			skb->prev = NULL;
			/* skb->dev shares skb->rbnode area,
			 * we need to restore its value.
			 */
			skb->dev = qdisc_dev(sch);

			if (q->slot.slot_next) {
				q->slot.packets_left--;
				q->slot.bytes_left -= qdisc_pkt_len(skb);
				if (q->slot.packets_left <= 0 ||
				    q->slot.bytes_left <= 0)
					get_slot_next(q, now);
			}

			if (q->qdisc) {
				unsigned int pkt_len = qdisc_pkt_len(skb);
				struct sk_buff *to_free = NULL;
				int err;

				err = qdisc_enqueue(skb, q->qdisc, &to_free);
				kfree_skb_list(to_free);
				if (err != NET_XMIT_SUCCESS &&
				    net_xmit_drop_count(err)) {
					qdisc_qstats_drop(sch);
					qdisc_tree_reduce_backlog(sch, 1,
								  pkt_len);
				}
				goto tfifo_dequeue;
			}
			goto deliver;
		}

		if (q->qdisc) {
			skb = q->qdisc->ops->dequeue(q->qdisc);
			if (skb)
				goto deliver;
		}

		qdisc_watchdog_schedule_ns(&q->watchdog,
					   max(time_to_send,
					       q->slot.slot_next));
	}

	if (q->qdisc) {
		skb = q->qdisc->ops->dequeue(q->qdisc);
		if (skb)
			goto deliver;
	}
	return NULL;
}

static void netem_reset(struct Qdisc *sch)
{
	struct netem_sched_data *q = qdisc_priv(sch);

	qdisc_reset_queue(sch);
	tfifo_reset(sch);
	if (q->qdisc)
		qdisc_reset(q->qdisc);
	qdisc_watchdog_cancel(&q->watchdog);
}

static void dist_free(struct disttable *d)
{
	kvfree(d);
}

/*
 * Distribution data is a variable size payload containing
 * signed 16 bit values.
 */

static int get_dist_table(struct disttable **tbl, const struct nlattr *attr)
{
	size_t n = nla_len(attr)/sizeof(__s16);
	const __s16 *data = nla_data(attr);
	struct disttable *d;
	int i;

	if (!n || n > NETEM_DIST_MAX)
		return -EINVAL;

	d = kvmalloc(struct_size(d, table, n), GFP_KERNEL);
	if (!d)
		return -ENOMEM;

	d->size = n;
	for (i = 0; i < n; i++)
		d->table[i] = data[i];

	*tbl = d;
	return 0;
}

static void get_slot(struct netem_sched_data *q, const struct nlattr *attr)
{
	const struct tc_netem_slot *c = nla_data(attr);

	q->slot_config = *c;
	if (q->slot_config.max_packets == 0)
		q->slot_config.max_packets = INT_MAX;
	if (q->slot_config.max_bytes == 0)
		q->slot_config.max_bytes = INT_MAX;

	/* capping dist_jitter to the range acceptable by tabledist() */
	q->slot_config.dist_jitter = min_t(__s64, INT_MAX, abs(q->slot_config.dist_jitter));

	q->slot.packets_left = q->slot_config.max_packets;
	q->slot.bytes_left = q->slot_config.max_bytes;
	if (q->slot_config.min_delay | q->slot_config.max_delay |
	    q->slot_config.dist_jitter)
		q->slot.slot_next = ktime_get_ns();
	else
		q->slot.slot_next = 0;
}

static void get_correlation(struct netem_sched_data *q, const struct nlattr *attr)
{
	const struct tc_netem_corr *c = nla_data(attr);

	init_crandom(&q->delay_cor, c->delay_corr);
	init_crandom(&q->loss_cor, c->loss_corr);
	init_crandom(&q->dup_cor, c->dup_corr);
}

static void get_reorder(struct netem_sched_data *q, const struct nlattr *attr)
{
	const struct tc_netem_reorder *r = nla_data(attr);

	q->reorder = r->probability;
	init_crandom(&q->reorder_cor, r->correlation);
}

static void get_corrupt(struct netem_sched_data *q, const struct nlattr *attr)
{
	const struct tc_netem_corrupt *r = nla_data(attr);

	q->corrupt = r->probability;
	init_crandom(&q->corrupt_cor, r->correlation);
}

static void get_rate(struct netem_sched_data *q, const struct nlattr *attr)
{
	const struct tc_netem_rate *r = nla_data(attr);

	q->rate = r->rate;
	q->packet_overhead = r->packet_overhead;
	q->cell_size = r->cell_size;
	q->cell_overhead = r->cell_overhead;
	if (q->cell_size)
		q->cell_size_reciprocal = reciprocal_value(q->cell_size);
	else
		q->cell_size_reciprocal = (struct reciprocal_value) { 0 };
}

static int get_loss_clg(struct netem_sched_data *q, const struct nlattr *attr)
{
	const struct nlattr *la;
	int rem;

	nla_for_each_nested(la, attr, rem) {
		u16 type = nla_type(la);

		switch (type) {
		case NETEM_LOSS_GI: {
			const struct tc_netem_gimodel *gi = nla_data(la);

			if (nla_len(la) < sizeof(struct tc_netem_gimodel)) {
				pr_info("netem: incorrect gi model size\n");
				return -EINVAL;
			}

			q->loss_model = CLG_4_STATES;

			q->clg.state = TX_IN_GAP_PERIOD;
			q->clg.a1 = gi->p13;
			q->clg.a2 = gi->p31;
			q->clg.a3 = gi->p32;
			q->clg.a4 = gi->p14;
			q->clg.a5 = gi->p23;
			break;
		}

		case NETEM_LOSS_GE: {
			const struct tc_netem_gemodel *ge = nla_data(la);

			if (nla_len(la) < sizeof(struct tc_netem_gemodel)) {
				pr_info("netem: incorrect ge model size\n");
				return -EINVAL;
			}

			q->loss_model = CLG_GILB_ELL;
			q->clg.state = GOOD_STATE;
			q->clg.a1 = ge->p;
			q->clg.a2 = ge->r;
			q->clg.a3 = ge->h;
			q->clg.a4 = ge->k1;
			break;
		}

		default:
			pr_info("netem: unknown loss type %u\n", type);
			return -EINVAL;
		}
	}

	return 0;
}

static const struct nla_policy netem_policy[TCA_NETEM_MAX + 1] = {
	[TCA_NETEM_CORR]	= { .len = sizeof(struct tc_netem_corr) },
	[TCA_NETEM_REORDER]	= { .len = sizeof(struct tc_netem_reorder) },
	[TCA_NETEM_CORRUPT]	= { .len = sizeof(struct tc_netem_corrupt) },
	[TCA_NETEM_RATE]	= { .len = sizeof(struct tc_netem_rate) },
	[TCA_NETEM_LOSS]	= { .type = NLA_NESTED },
	[TCA_NETEM_ECN]		= { .type = NLA_U32 },
	[TCA_NETEM_RATE64]	= { .type = NLA_U64 },
	[TCA_NETEM_LATENCY64]	= { .type = NLA_S64 },
	[TCA_NETEM_JITTER64]	= { .type = NLA_S64 },
	[TCA_NETEM_SLOT]	= { .len = sizeof(struct tc_netem_slot) },
	[TCA_NETEM_PRNG_SEED]	= { .type = NLA_U64 },
};

static int parse_attr(struct nlattr *tb[], int maxtype, struct nlattr *nla,
		      const struct nla_policy *policy, int len)
{
	int nested_len = nla_len(nla) - NLA_ALIGN(len);

	if (nested_len < 0) {
		pr_info("netem: invalid attributes len %d\n", nested_len);
		return -EINVAL;
	}

	if (nested_len >= nla_attr_size(0))
		return nla_parse_deprecated(tb, maxtype,
					    nla_data(nla) + NLA_ALIGN(len),
					    nested_len, policy, NULL);

	memset(tb, 0, sizeof(struct nlattr *) * (maxtype + 1));
	return 0;
}

/* Parse netlink message to set options */
static int netem_change(struct Qdisc *sch, struct nlattr *opt,
			struct netlink_ext_ack *extack)
{
	struct netem_sched_data *q = qdisc_priv(sch);
	struct nlattr *tb[TCA_NETEM_MAX + 1];
	struct disttable *delay_dist = NULL;
	struct disttable *slot_dist = NULL;
	struct tc_netem_qopt *qopt;
	struct clgstate old_clg;
	int old_loss_model = CLG_RANDOM;
	int ret;

	qopt = nla_data(opt);
	ret = parse_attr(tb, TCA_NETEM_MAX, opt, netem_policy, sizeof(*qopt));
	if (ret < 0)
		return ret;

	if (tb[TCA_NETEM_DELAY_DIST]) {
		ret = get_dist_table(&delay_dist, tb[TCA_NETEM_DELAY_DIST]);
		if (ret)
			goto table_free;
	}

	if (tb[TCA_NETEM_SLOT_DIST]) {
		ret = get_dist_table(&slot_dist, tb[TCA_NETEM_SLOT_DIST]);
		if (ret)
			goto table_free;
	}

	sch_tree_lock(sch);
	/* backup q->clg and q->loss_model */
	old_clg = q->clg;
	old_loss_model = q->loss_model;

	if (tb[TCA_NETEM_LOSS]) {
		ret = get_loss_clg(q, tb[TCA_NETEM_LOSS]);
		if (ret) {
			q->loss_model = old_loss_model;
			q->clg = old_clg;
			goto unlock;
		}
	} else {
		q->loss_model = CLG_RANDOM;
	}

	if (delay_dist)
		swap(q->delay_dist, delay_dist);
	if (slot_dist)
		swap(q->slot_dist, slot_dist);
	sch->limit = qopt->limit;

	q->latency = PSCHED_TICKS2NS(qopt->latency);
	q->jitter = PSCHED_TICKS2NS(qopt->jitter);
	q->limit = qopt->limit;
	q->gap = qopt->gap;
	q->counter = 0;
	q->loss = qopt->loss;
	q->duplicate = qopt->duplicate;

	/* for compatibility with earlier versions.
	 * if gap is set, need to assume 100% probability
	 */
	if (q->gap)
		q->reorder = ~0;

	if (tb[TCA_NETEM_CORR])
		get_correlation(q, tb[TCA_NETEM_CORR]);

	if (tb[TCA_NETEM_REORDER])
		get_reorder(q, tb[TCA_NETEM_REORDER]);

	if (tb[TCA_NETEM_CORRUPT])
		get_corrupt(q, tb[TCA_NETEM_CORRUPT]);

	if (tb[TCA_NETEM_RATE])
		get_rate(q, tb[TCA_NETEM_RATE]);

	if (tb[TCA_NETEM_RATE64])
		q->rate = max_t(u64, q->rate,
				nla_get_u64(tb[TCA_NETEM_RATE64]));

	if (tb[TCA_NETEM_LATENCY64])
		q->latency = nla_get_s64(tb[TCA_NETEM_LATENCY64]);

	if (tb[TCA_NETEM_JITTER64])
		q->jitter = nla_get_s64(tb[TCA_NETEM_JITTER64]);

	if (tb[TCA_NETEM_ECN])
		q->ecn = nla_get_u32(tb[TCA_NETEM_ECN]);

	if (tb[TCA_NETEM_SLOT])
		get_slot(q, tb[TCA_NETEM_SLOT]);

	/* capping jitter to the range acceptable by tabledist() */
	q->jitter = min_t(s64, abs(q->jitter), INT_MAX);

	if (tb[TCA_NETEM_PRNG_SEED])
		q->prng.seed = nla_get_u64(tb[TCA_NETEM_PRNG_SEED]);
	else
		q->prng.seed = get_random_u64();
	prandom_seed_state(&q->prng.prng_state, q->prng.seed);

unlock:
	sch_tree_unlock(sch);

table_free:
	dist_free(delay_dist);
	dist_free(slot_dist);
	return ret;
}

static int netem_init(struct Qdisc *sch, struct nlattr *opt,
		      struct netlink_ext_ack *extack)
{
	struct netem_sched_data *q = qdisc_priv(sch);
	int ret;

	qdisc_watchdog_init(&q->watchdog, sch);

	if (!opt)
		return -EINVAL;

	q->loss_model = CLG_RANDOM;
	ret = netem_change(sch, opt, extack);
	if (ret)
		pr_info("netem: change failed\n");
	return ret;
}

static void netem_destroy(struct Qdisc *sch)
{
	struct netem_sched_data *q = qdisc_priv(sch);

	qdisc_watchdog_cancel(&q->watchdog);
	if (q->qdisc)
		qdisc_put(q->qdisc);
	dist_free(q->delay_dist);
	dist_free(q->slot_dist);
}

static int dump_loss_model(const struct netem_sched_data *q,
			   struct sk_buff *skb)
{
	struct nlattr *nest;

	nest = nla_nest_start_noflag(skb, TCA_NETEM_LOSS);
	if (nest == NULL)
		goto nla_put_failure;

	switch (q->loss_model) {
	case CLG_RANDOM:
		/* legacy loss model */
		nla_nest_cancel(skb, nest);
		return 0;	/* no data */

	case CLG_4_STATES: {
		struct tc_netem_gimodel gi = {
			.p13 = q->clg.a1,
			.p31 = q->clg.a2,
			.p32 = q->clg.a3,
			.p14 = q->clg.a4,
			.p23 = q->clg.a5,
		};

		if (nla_put(skb, NETEM_LOSS_GI, sizeof(gi), &gi))
			goto nla_put_failure;
		break;
	}
	case CLG_GILB_ELL: {
		struct tc_netem_gemodel ge = {
			.p = q->clg.a1,
			.r = q->clg.a2,
			.h = q->clg.a3,
			.k1 = q->clg.a4,
		};

		if (nla_put(skb, NETEM_LOSS_GE, sizeof(ge), &ge))
			goto nla_put_failure;
		break;
	}
	}

	nla_nest_end(skb, nest);
	return 0;

nla_put_failure:
	nla_nest_cancel(skb, nest);
	return -1;
}

static int netem_dump(struct Qdisc *sch, struct sk_buff *skb)
{
	const struct netem_sched_data *q = qdisc_priv(sch);
	struct nlattr *nla = (struct nlattr *) skb_tail_pointer(skb);
	struct tc_netem_qopt qopt;
	struct tc_netem_corr cor;
	struct tc_netem_reorder reorder;
	struct tc_netem_corrupt corrupt;
	struct tc_netem_rate rate;
	struct tc_netem_slot slot;

	qopt.latency = min_t(psched_time_t, PSCHED_NS2TICKS(q->latency),
			     UINT_MAX);
	qopt.jitter = min_t(psched_time_t, PSCHED_NS2TICKS(q->jitter),
			    UINT_MAX);
	qopt.limit = q->limit;
	qopt.loss = q->loss;
	qopt.gap = q->gap;
	qopt.duplicate = q->duplicate;
	if (nla_put(skb, TCA_OPTIONS, sizeof(qopt), &qopt))
		goto nla_put_failure;

	if (nla_put(skb, TCA_NETEM_LATENCY64, sizeof(q->latency), &q->latency))
		goto nla_put_failure;

	if (nla_put(skb, TCA_NETEM_JITTER64, sizeof(q->jitter), &q->jitter))
		goto nla_put_failure;

	cor.delay_corr = q->delay_cor.rho;
	cor.loss_corr = q->loss_cor.rho;
	cor.dup_corr = q->dup_cor.rho;
	if (nla_put(skb, TCA_NETEM_CORR, sizeof(cor), &cor))
		goto nla_put_failure;

	reorder.probability = q->reorder;
	reorder.correlation = q->reorder_cor.rho;
	if (nla_put(skb, TCA_NETEM_REORDER, sizeof(reorder), &reorder))
		goto nla_put_failure;

	corrupt.probability = q->corrupt;
	corrupt.correlation = q->corrupt_cor.rho;
	if (nla_put(skb, TCA_NETEM_CORRUPT, sizeof(corrupt), &corrupt))
		goto nla_put_failure;

	if (q->rate >= (1ULL << 32)) {
		if (nla_put_u64_64bit(skb, TCA_NETEM_RATE64, q->rate,
				      TCA_NETEM_PAD))
			goto nla_put_failure;
		rate.rate = ~0U;
	} else {
		rate.rate = q->rate;
	}
	rate.packet_overhead = q->packet_overhead;
	rate.cell_size = q->cell_size;
	rate.cell_overhead = q->cell_overhead;
	if (nla_put(skb, TCA_NETEM_RATE, sizeof(rate), &rate))
		goto nla_put_failure;

	if (q->ecn && nla_put_u32(skb, TCA_NETEM_ECN, q->ecn))
		goto nla_put_failure;

	if (dump_loss_model(q, skb) != 0)
		goto nla_put_failure;

	if (q->slot_config.min_delay | q->slot_config.max_delay |
	    q->slot_config.dist_jitter) {
		slot = q->slot_config;
		if (slot.max_packets == INT_MAX)
			slot.max_packets = 0;
		if (slot.max_bytes == INT_MAX)
			slot.max_bytes = 0;
		if (nla_put(skb, TCA_NETEM_SLOT, sizeof(slot), &slot))
			goto nla_put_failure;
	}

	if (nla_put_u64_64bit(skb, TCA_NETEM_PRNG_SEED, q->prng.seed,
			      TCA_NETEM_PAD))
		goto nla_put_failure;

	return nla_nest_end(skb, nla);

nla_put_failure:
	nlmsg_trim(skb, nla);
	return -1;
}

static int netem_dump_class(struct Qdisc *sch, unsigned long cl,
			  struct sk_buff *skb, struct tcmsg *tcm)
{
	struct netem_sched_data *q = qdisc_priv(sch);

	if (cl != 1 || !q->qdisc) 	/* only one class */
		return -ENOENT;

	tcm->tcm_handle |= TC_H_MIN(1);
	tcm->tcm_info = q->qdisc->handle;

	return 0;
}

static int netem_graft(struct Qdisc *sch, unsigned long arg, struct Qdisc *new,
		     struct Qdisc **old, struct netlink_ext_ack *extack)
{
	struct netem_sched_data *q = qdisc_priv(sch);

	*old = qdisc_replace(sch, new, &q->qdisc);
	return 0;
}

static struct Qdisc *netem_leaf(struct Qdisc *sch, unsigned long arg)
{
	struct netem_sched_data *q = qdisc_priv(sch);
	return q->qdisc;
}

static unsigned long netem_find(struct Qdisc *sch, u32 classid)
{
	return 1;
}

static void netem_walk(struct Qdisc *sch, struct qdisc_walker *walker)
{
	if (!walker->stop) {
		if (!tc_qdisc_stats_dump(sch, 1, walker))
			return;
	}
}

static const struct Qdisc_class_ops netem_class_ops = {
	.graft		=	netem_graft,
	.leaf		=	netem_leaf,
	.find		=	netem_find,
	.walk		=	netem_walk,
	.dump		=	netem_dump_class,
};

static struct Qdisc_ops netem_qdisc_ops __read_mostly = {
	.id		=	"netem",
	.cl_ops		=	&netem_class_ops,
	.priv_size	=	sizeof(struct netem_sched_data),
	.enqueue	=	netem_enqueue,
	.dequeue	=	netem_dequeue,
	.peek		=	qdisc_peek_dequeued,
	.init		=	netem_init,
	.reset		=	netem_reset,
	.destroy	=	netem_destroy,
	.change		=	netem_change,
	.dump		=	netem_dump,
	.owner		=	THIS_MODULE,
};


static int __init netem_module_init(void)
{
	pr_info("netem: version " VERSION "\n");
	return register_qdisc(&netem_qdisc_ops);
}
static void __exit netem_module_exit(void)
{
	unregister_qdisc(&netem_qdisc_ops);
}
module_init(netem_module_init)
module_exit(netem_module_exit)
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Network characteristics emulator qdisc");
