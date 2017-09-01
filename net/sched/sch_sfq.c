/*
 * net/sched/sch_sfq.c	Stochastic Fairness Queueing discipline.
 *
 *		This program is free software; you can redistribute it and/or
 *		modify it under the terms of the GNU General Public License
 *		as published by the Free Software Foundation; either version
 *		2 of the License, or (at your option) any later version.
 *
 * Authors:	Alexey Kuznetsov, <kuznet@ms2.inr.ac.ru>
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
#include <linux/jhash.h>
#include <linux/slab.h>
#include <linux/vmalloc.h>
#include <net/netlink.h>
#include <net/pkt_sched.h>
#include <net/red.h>


/*	Stochastic Fairness Queuing algorithm.
	=======================================

	Source:
	Paul E. McKenney "Stochastic Fairness Queuing",
	IEEE INFOCOMM'90 Proceedings, San Francisco, 1990.

	Paul E. McKenney "Stochastic Fairness Queuing",
	"Interworking: Research and Experience", v.2, 1991, p.113-131.


	See also:
	M. Shreedhar and George Varghese "Efficient Fair
	Queuing using Deficit Round Robin", Proc. SIGCOMM 95.


	This is not the thing that is usually called (W)FQ nowadays.
	It does not use any timestamp mechanism, but instead
	processes queues in round-robin order.

	ADVANTAGE:

	- It is very cheap. Both CPU and memory requirements are minimal.

	DRAWBACKS:

	- "Stochastic" -> It is not 100% fair.
	When hash collisions occur, several flows are considered as one.

	- "Round-robin" -> It introduces larger delays than virtual clock
	based schemes, and should not be used for isolating interactive
	traffic	from non-interactive. It means, that this scheduler
	should be used as leaf of CBQ or P3, which put interactive traffic
	to higher priority band.

	We still need true WFQ for top level CSZ, but using WFQ
	for the best effort traffic is absolutely pointless:
	SFQ is superior for this purpose.

	IMPLEMENTATION:
	This implementation limits :
	- maximal queue length per flow to 127 packets.
	- max mtu to 2^18-1;
	- max 65408 flows,
	- number of hash buckets to 65536.

	It is easy to increase these values, but not in flight.  */

#define SFQ_MAX_DEPTH		127 /* max number of packets per flow */
#define SFQ_DEFAULT_FLOWS	128
#define SFQ_MAX_FLOWS		(0x10000 - SFQ_MAX_DEPTH - 1) /* max number of flows */
#define SFQ_EMPTY_SLOT		0xffff
#define SFQ_DEFAULT_HASH_DIVISOR 1024

/* We use 16 bits to store allot, and want to handle packets up to 64K
 * Scale allot by 8 (1<<3) so that no overflow occurs.
 */
#define SFQ_ALLOT_SHIFT		3
#define SFQ_ALLOT_SIZE(X)	DIV_ROUND_UP(X, 1 << SFQ_ALLOT_SHIFT)

/* This type should contain at least SFQ_MAX_DEPTH + 1 + SFQ_MAX_FLOWS values */
typedef u16 sfq_index;

/*
 * We dont use pointers to save space.
 * Small indexes [0 ... SFQ_MAX_FLOWS - 1] are 'pointers' to slots[] array
 * while following values [SFQ_MAX_FLOWS ... SFQ_MAX_FLOWS + SFQ_MAX_DEPTH]
 * are 'pointers' to dep[] array
 */
struct sfq_head {
	sfq_index	next;
	sfq_index	prev;
};

struct sfq_slot {
	struct sk_buff	*skblist_next;
	struct sk_buff	*skblist_prev;
	sfq_index	qlen; /* number of skbs in skblist */
	sfq_index	next; /* next slot in sfq RR chain */
	struct sfq_head dep; /* anchor in dep[] chains */
	unsigned short	hash; /* hash value (index in ht[]) */
	short		allot; /* credit for this slot */

	unsigned int    backlog;
	struct red_vars vars;
};

struct sfq_sched_data {
/* frequently used fields */
	int		limit;		/* limit of total number of packets in this qdisc */
	unsigned int	divisor;	/* number of slots in hash table */
	u8		headdrop;
	u8		maxdepth;	/* limit of packets per flow */

	u32		perturbation;
	u8		cur_depth;	/* depth of longest slot */
	u8		flags;
	unsigned short  scaled_quantum; /* SFQ_ALLOT_SIZE(quantum) */
	struct tcf_proto __rcu *filter_list;
	sfq_index	*ht;		/* Hash table ('divisor' slots) */
	struct sfq_slot	*slots;		/* Flows table ('maxflows' entries) */

	struct red_parms *red_parms;
	struct tc_sfqred_stats stats;
	struct sfq_slot *tail;		/* current slot in round */

	struct sfq_head	dep[SFQ_MAX_DEPTH + 1];
					/* Linked lists of slots, indexed by depth
					 * dep[0] : list of unused flows
					 * dep[1] : list of flows with 1 packet
					 * dep[X] : list of flows with X packets
					 */

	unsigned int	maxflows;	/* number of flows in flows array */
	int		perturb_period;
	unsigned int	quantum;	/* Allotment per round: MUST BE >= MTU */
	struct timer_list perturb_timer;
};

/*
 * sfq_head are either in a sfq_slot or in dep[] array
 */
static inline struct sfq_head *sfq_dep_head(struct sfq_sched_data *q, sfq_index val)
{
	if (val < SFQ_MAX_FLOWS)
		return &q->slots[val].dep;
	return &q->dep[val - SFQ_MAX_FLOWS];
}

static unsigned int sfq_hash(const struct sfq_sched_data *q,
			     const struct sk_buff *skb)
{
	return skb_get_hash_perturb(skb, q->perturbation) & (q->divisor - 1);
}

static unsigned int sfq_classify(struct sk_buff *skb, struct Qdisc *sch,
				 int *qerr)
{
	struct sfq_sched_data *q = qdisc_priv(sch);
	struct tcf_result res;
	struct tcf_proto *fl;
	int result;

	if (TC_H_MAJ(skb->priority) == sch->handle &&
	    TC_H_MIN(skb->priority) > 0 &&
	    TC_H_MIN(skb->priority) <= q->divisor)
		return TC_H_MIN(skb->priority);

	fl = rcu_dereference_bh(q->filter_list);
	if (!fl)
		return sfq_hash(q, skb) + 1;

	*qerr = NET_XMIT_SUCCESS | __NET_XMIT_BYPASS;
	result = tc_classify(skb, fl, &res, false);
	if (result >= 0) {
#ifdef CONFIG_NET_CLS_ACT
		switch (result) {
		case TC_ACT_STOLEN:
		case TC_ACT_QUEUED:
			*qerr = NET_XMIT_SUCCESS | __NET_XMIT_STOLEN;
		case TC_ACT_SHOT:
			return 0;
		}
#endif
		if (TC_H_MIN(res.classid) <= q->divisor)
			return TC_H_MIN(res.classid);
	}
	return 0;
}

/*
 * x : slot number [0 .. SFQ_MAX_FLOWS - 1]
 */
static inline void sfq_link(struct sfq_sched_data *q, sfq_index x)
{
	sfq_index p, n;
	struct sfq_slot *slot = &q->slots[x];
	int qlen = slot->qlen;

	p = qlen + SFQ_MAX_FLOWS;
	n = q->dep[qlen].next;

	slot->dep.next = n;
	slot->dep.prev = p;

	q->dep[qlen].next = x;		/* sfq_dep_head(q, p)->next = x */
	sfq_dep_head(q, n)->prev = x;
}

#define sfq_unlink(q, x, n, p)			\
	do {					\
		n = q->slots[x].dep.next;	\
		p = q->slots[x].dep.prev;	\
		sfq_dep_head(q, p)->next = n;	\
		sfq_dep_head(q, n)->prev = p;	\
	} while (0)


static inline void sfq_dec(struct sfq_sched_data *q, sfq_index x)
{
	sfq_index p, n;
	int d;

	sfq_unlink(q, x, n, p);

	d = q->slots[x].qlen--;
	if (n == p && q->cur_depth == d)
		q->cur_depth--;
	sfq_link(q, x);
}

static inline void sfq_inc(struct sfq_sched_data *q, sfq_index x)
{
	sfq_index p, n;
	int d;

	sfq_unlink(q, x, n, p);

	d = ++q->slots[x].qlen;
	if (q->cur_depth < d)
		q->cur_depth = d;
	sfq_link(q, x);
}

/* helper functions : might be changed when/if skb use a standard list_head */

/* remove one skb from tail of slot queue */
static inline struct sk_buff *slot_dequeue_tail(struct sfq_slot *slot)
{
	struct sk_buff *skb = slot->skblist_prev;

	slot->skblist_prev = skb->prev;
	skb->prev->next = (struct sk_buff *)slot;
	skb->next = skb->prev = NULL;
	return skb;
}

/* remove one skb from head of slot queue */
static inline struct sk_buff *slot_dequeue_head(struct sfq_slot *slot)
{
	struct sk_buff *skb = slot->skblist_next;

	slot->skblist_next = skb->next;
	skb->next->prev = (struct sk_buff *)slot;
	skb->next = skb->prev = NULL;
	return skb;
}

static inline void slot_queue_init(struct sfq_slot *slot)
{
	memset(slot, 0, sizeof(*slot));
	slot->skblist_prev = slot->skblist_next = (struct sk_buff *)slot;
}

/* add skb to slot queue (tail add) */
static inline void slot_queue_add(struct sfq_slot *slot, struct sk_buff *skb)
{
	skb->prev = slot->skblist_prev;
	skb->next = (struct sk_buff *)slot;
	slot->skblist_prev->next = skb;
	slot->skblist_prev = skb;
}

static unsigned int sfq_drop(struct Qdisc *sch)
{
	struct sfq_sched_data *q = qdisc_priv(sch);
	sfq_index x, d = q->cur_depth;
	struct sk_buff *skb;
	unsigned int len;
	struct sfq_slot *slot;

	/* Queue is full! Find the longest slot and drop tail packet from it */
	if (d > 1) {
		x = q->dep[d].next;
		slot = &q->slots[x];
drop:
		skb = q->headdrop ? slot_dequeue_head(slot) : slot_dequeue_tail(slot);
		len = qdisc_pkt_len(skb);
		slot->backlog -= len;
		sfq_dec(q, x);
		sch->q.qlen--;
		qdisc_qstats_drop(sch);
		qdisc_qstats_backlog_dec(sch, skb);
		kfree_skb(skb);
		return len;
	}

	if (d == 1) {
		/* It is difficult to believe, but ALL THE SLOTS HAVE LENGTH 1. */
		x = q->tail->next;
		slot = &q->slots[x];
		q->tail->next = slot->next;
		q->ht[slot->hash] = SFQ_EMPTY_SLOT;
		goto drop;
	}

	return 0;
}

/* Is ECN parameter configured */
static int sfq_prob_mark(const struct sfq_sched_data *q)
{
	return q->flags & TC_RED_ECN;
}

/* Should packets over max threshold just be marked */
static int sfq_hard_mark(const struct sfq_sched_data *q)
{
	return (q->flags & (TC_RED_ECN | TC_RED_HARDDROP)) == TC_RED_ECN;
}

static int sfq_headdrop(const struct sfq_sched_data *q)
{
	return q->headdrop;
}

static int
sfq_enqueue(struct sk_buff *skb, struct Qdisc *sch)
{
	struct sfq_sched_data *q = qdisc_priv(sch);
	unsigned int hash, dropped;
	sfq_index x, qlen;
	struct sfq_slot *slot;
	int uninitialized_var(ret);
	struct sk_buff *head;
	int delta;

	hash = sfq_classify(skb, sch, &ret);
	if (hash == 0) {
		if (ret & __NET_XMIT_BYPASS)
			qdisc_qstats_drop(sch);
		kfree_skb(skb);
		return ret;
	}
	hash--;

	x = q->ht[hash];
	slot = &q->slots[x];
	if (x == SFQ_EMPTY_SLOT) {
		x = q->dep[0].next; /* get a free slot */
		if (x >= SFQ_MAX_FLOWS)
			return qdisc_drop(skb, sch);
		q->ht[hash] = x;
		slot = &q->slots[x];
		slot->hash = hash;
		slot->backlog = 0; /* should already be 0 anyway... */
		red_set_vars(&slot->vars);
		goto enqueue;
	}
	if (q->red_parms) {
		slot->vars.qavg = red_calc_qavg_no_idle_time(q->red_parms,
							&slot->vars,
							slot->backlog);
		switch (red_action(q->red_parms,
				   &slot->vars,
				   slot->vars.qavg)) {
		case RED_DONT_MARK:
			break;

		case RED_PROB_MARK:
			qdisc_qstats_overlimit(sch);
			if (sfq_prob_mark(q)) {
				/* We know we have at least one packet in queue */
				if (sfq_headdrop(q) &&
				    INET_ECN_set_ce(slot->skblist_next)) {
					q->stats.prob_mark_head++;
					break;
				}
				if (INET_ECN_set_ce(skb)) {
					q->stats.prob_mark++;
					break;
				}
			}
			q->stats.prob_drop++;
			goto congestion_drop;

		case RED_HARD_MARK:
			qdisc_qstats_overlimit(sch);
			if (sfq_hard_mark(q)) {
				/* We know we have at least one packet in queue */
				if (sfq_headdrop(q) &&
				    INET_ECN_set_ce(slot->skblist_next)) {
					q->stats.forced_mark_head++;
					break;
				}
				if (INET_ECN_set_ce(skb)) {
					q->stats.forced_mark++;
					break;
				}
			}
			q->stats.forced_drop++;
			goto congestion_drop;
		}
	}

	if (slot->qlen >= q->maxdepth) {
congestion_drop:
		if (!sfq_headdrop(q))
			return qdisc_drop(skb, sch);

		/* We know we have at least one packet in queue */
		head = slot_dequeue_head(slot);
		delta = qdisc_pkt_len(head) - qdisc_pkt_len(skb);
		sch->qstats.backlog -= delta;
		slot->backlog -= delta;
		qdisc_drop(head, sch);

		slot_queue_add(slot, skb);
		qdisc_tree_reduce_backlog(sch, 0, delta);
		return NET_XMIT_CN;
	}

enqueue:
	qdisc_qstats_backlog_inc(sch, skb);
	slot->backlog += qdisc_pkt_len(skb);
	slot_queue_add(slot, skb);
	sfq_inc(q, x);
	if (slot->qlen == 1) {		/* The flow is new */
		if (q->tail == NULL) {	/* It is the first flow */
			slot->next = x;
		} else {
			slot->next = q->tail->next;
			q->tail->next = x;
		}
		/* We put this flow at the end of our flow list.
		 * This might sound unfair for a new flow to wait after old ones,
		 * but we could endup servicing new flows only, and freeze old ones.
		 */
		q->tail = slot;
		/* We could use a bigger initial quantum for new flows */
		slot->allot = q->scaled_quantum;
	}
	if (++sch->q.qlen <= q->limit)
		return NET_XMIT_SUCCESS;

	qlen = slot->qlen;
	dropped = sfq_drop(sch);
	/* Return Congestion Notification only if we dropped a packet
	 * from this flow.
	 */
	if (qlen != slot->qlen) {
		qdisc_tree_reduce_backlog(sch, 0, dropped - qdisc_pkt_len(skb));
		return NET_XMIT_CN;
	}

	/* As we dropped a packet, better let upper stack know this */
	qdisc_tree_reduce_backlog(sch, 1, dropped);
	return NET_XMIT_SUCCESS;
}

static struct sk_buff *
sfq_dequeue(struct Qdisc *sch)
{
	struct sfq_sched_data *q = qdisc_priv(sch);
	struct sk_buff *skb;
	sfq_index a, next_a;
	struct sfq_slot *slot;

	/* No active slots */
	if (q->tail == NULL)
		return NULL;

next_slot:
	a = q->tail->next;
	slot = &q->slots[a];
	if (slot->allot <= 0) {
		q->tail = slot;
		slot->allot += q->scaled_quantum;
		goto next_slot;
	}
	skb = slot_dequeue_head(slot);
	sfq_dec(q, a);
	qdisc_bstats_update(sch, skb);
	sch->q.qlen--;
	qdisc_qstats_backlog_dec(sch, skb);
	slot->backlog -= qdisc_pkt_len(skb);
	/* Is the slot empty? */
	if (slot->qlen == 0) {
		q->ht[slot->hash] = SFQ_EMPTY_SLOT;
		next_a = slot->next;
		if (a == next_a) {
			q->tail = NULL; /* no more active slots */
			return skb;
		}
		q->tail->next = next_a;
	} else {
		slot->allot -= SFQ_ALLOT_SIZE(qdisc_pkt_len(skb));
	}
	return skb;
}

static void
sfq_reset(struct Qdisc *sch)
{
	struct sk_buff *skb;

	while ((skb = sfq_dequeue(sch)) != NULL)
		kfree_skb(skb);
}

/*
 * When q->perturbation is changed, we rehash all queued skbs
 * to avoid OOO (Out Of Order) effects.
 * We dont use sfq_dequeue()/sfq_enqueue() because we dont want to change
 * counters.
 */
static void sfq_rehash(struct Qdisc *sch)
{
	struct sfq_sched_data *q = qdisc_priv(sch);
	struct sk_buff *skb;
	int i;
	struct sfq_slot *slot;
	struct sk_buff_head list;
	int dropped = 0;
	unsigned int drop_len = 0;

	__skb_queue_head_init(&list);

	for (i = 0; i < q->maxflows; i++) {
		slot = &q->slots[i];
		if (!slot->qlen)
			continue;
		while (slot->qlen) {
			skb = slot_dequeue_head(slot);
			sfq_dec(q, i);
			__skb_queue_tail(&list, skb);
		}
		slot->backlog = 0;
		red_set_vars(&slot->vars);
		q->ht[slot->hash] = SFQ_EMPTY_SLOT;
	}
	q->tail = NULL;

	while ((skb = __skb_dequeue(&list)) != NULL) {
		unsigned int hash = sfq_hash(q, skb);
		sfq_index x = q->ht[hash];

		slot = &q->slots[x];
		if (x == SFQ_EMPTY_SLOT) {
			x = q->dep[0].next; /* get a free slot */
			if (x >= SFQ_MAX_FLOWS) {
drop:
				qdisc_qstats_backlog_dec(sch, skb);
				drop_len += qdisc_pkt_len(skb);
				kfree_skb(skb);
				dropped++;
				continue;
			}
			q->ht[hash] = x;
			slot = &q->slots[x];
			slot->hash = hash;
		}
		if (slot->qlen >= q->maxdepth)
			goto drop;
		slot_queue_add(slot, skb);
		if (q->red_parms)
			slot->vars.qavg = red_calc_qavg(q->red_parms,
							&slot->vars,
							slot->backlog);
		slot->backlog += qdisc_pkt_len(skb);
		sfq_inc(q, x);
		if (slot->qlen == 1) {		/* The flow is new */
			if (q->tail == NULL) {	/* It is the first flow */
				slot->next = x;
			} else {
				slot->next = q->tail->next;
				q->tail->next = x;
			}
			q->tail = slot;
			slot->allot = q->scaled_quantum;
		}
	}
	sch->q.qlen -= dropped;
	qdisc_tree_reduce_backlog(sch, dropped, drop_len);
}

static void sfq_perturbation(unsigned long arg)
{
	struct Qdisc *sch = (struct Qdisc *)arg;
	struct sfq_sched_data *q = qdisc_priv(sch);
	spinlock_t *root_lock = qdisc_lock(qdisc_root_sleeping(sch));

	spin_lock(root_lock);
	q->perturbation = prandom_u32();
	if (!q->filter_list && q->tail)
		sfq_rehash(sch);
	spin_unlock(root_lock);

	if (q->perturb_period)
		mod_timer(&q->perturb_timer, jiffies + q->perturb_period);
}

static int sfq_change(struct Qdisc *sch, struct nlattr *opt)
{
	struct sfq_sched_data *q = qdisc_priv(sch);
	struct tc_sfq_qopt *ctl = nla_data(opt);
	struct tc_sfq_qopt_v1 *ctl_v1 = NULL;
	unsigned int qlen, dropped = 0;
	struct red_parms *p = NULL;

	if (opt->nla_len < nla_attr_size(sizeof(*ctl)))
		return -EINVAL;
	if (opt->nla_len >= nla_attr_size(sizeof(*ctl_v1)))
		ctl_v1 = nla_data(opt);
	if (ctl->divisor &&
	    (!is_power_of_2(ctl->divisor) || ctl->divisor > 65536))
		return -EINVAL;
	if (ctl_v1 && ctl_v1->qth_min) {
		p = kmalloc(sizeof(*p), GFP_KERNEL);
		if (!p)
			return -ENOMEM;
	}
	sch_tree_lock(sch);
	if (ctl->quantum) {
		q->quantum = ctl->quantum;
		q->scaled_quantum = SFQ_ALLOT_SIZE(q->quantum);
	}
	q->perturb_period = ctl->perturb_period * HZ;
	if (ctl->flows)
		q->maxflows = min_t(u32, ctl->flows, SFQ_MAX_FLOWS);
	if (ctl->divisor) {
		q->divisor = ctl->divisor;
		q->maxflows = min_t(u32, q->maxflows, q->divisor);
	}
	if (ctl_v1) {
		if (ctl_v1->depth)
			q->maxdepth = min_t(u32, ctl_v1->depth, SFQ_MAX_DEPTH);
		if (p) {
			swap(q->red_parms, p);
			red_set_parms(q->red_parms,
				      ctl_v1->qth_min, ctl_v1->qth_max,
				      ctl_v1->Wlog,
				      ctl_v1->Plog, ctl_v1->Scell_log,
				      NULL,
				      ctl_v1->max_P);
		}
		q->flags = ctl_v1->flags;
		q->headdrop = ctl_v1->headdrop;
	}
	if (ctl->limit) {
		q->limit = min_t(u32, ctl->limit, q->maxdepth * q->maxflows);
		q->maxflows = min_t(u32, q->maxflows, q->limit);
	}

	qlen = sch->q.qlen;
	while (sch->q.qlen > q->limit)
		dropped += sfq_drop(sch);
	qdisc_tree_reduce_backlog(sch, qlen - sch->q.qlen, dropped);

	del_timer(&q->perturb_timer);
	if (q->perturb_period) {
		mod_timer(&q->perturb_timer, jiffies + q->perturb_period);
		q->perturbation = prandom_u32();
	}
	sch_tree_unlock(sch);
	kfree(p);
	return 0;
}

static void *sfq_alloc(size_t sz)
{
	void *ptr = kmalloc(sz, GFP_KERNEL | __GFP_NOWARN);

	if (!ptr)
		ptr = vmalloc(sz);
	return ptr;
}

static void sfq_free(void *addr)
{
	kvfree(addr);
}

static void sfq_destroy(struct Qdisc *sch)
{
	struct sfq_sched_data *q = qdisc_priv(sch);

	tcf_destroy_chain(&q->filter_list);
	q->perturb_period = 0;
	del_timer_sync(&q->perturb_timer);
	sfq_free(q->ht);
	sfq_free(q->slots);
	kfree(q->red_parms);
}

static int sfq_init(struct Qdisc *sch, struct nlattr *opt)
{
	struct sfq_sched_data *q = qdisc_priv(sch);
	int i;

	q->perturb_timer.function = sfq_perturbation;
	q->perturb_timer.data = (unsigned long)sch;
	init_timer_deferrable(&q->perturb_timer);

	for (i = 0; i < SFQ_MAX_DEPTH + 1; i++) {
		q->dep[i].next = i + SFQ_MAX_FLOWS;
		q->dep[i].prev = i + SFQ_MAX_FLOWS;
	}

	q->limit = SFQ_MAX_DEPTH;
	q->maxdepth = SFQ_MAX_DEPTH;
	q->cur_depth = 0;
	q->tail = NULL;
	q->divisor = SFQ_DEFAULT_HASH_DIVISOR;
	q->maxflows = SFQ_DEFAULT_FLOWS;
	q->quantum = psched_mtu(qdisc_dev(sch));
	q->scaled_quantum = SFQ_ALLOT_SIZE(q->quantum);
	q->perturb_period = 0;
	q->perturbation = prandom_u32();

	if (opt) {
		int err = sfq_change(sch, opt);
		if (err)
			return err;
	}

	q->ht = sfq_alloc(sizeof(q->ht[0]) * q->divisor);
	q->slots = sfq_alloc(sizeof(q->slots[0]) * q->maxflows);
	if (!q->ht || !q->slots) {
		/* Note: sfq_destroy() will be called by our caller */
		return -ENOMEM;
	}

	for (i = 0; i < q->divisor; i++)
		q->ht[i] = SFQ_EMPTY_SLOT;

	for (i = 0; i < q->maxflows; i++) {
		slot_queue_init(&q->slots[i]);
		sfq_link(q, i);
	}
	if (q->limit >= 1)
		sch->flags |= TCQ_F_CAN_BYPASS;
	else
		sch->flags &= ~TCQ_F_CAN_BYPASS;
	return 0;
}

static int sfq_dump(struct Qdisc *sch, struct sk_buff *skb)
{
	struct sfq_sched_data *q = qdisc_priv(sch);
	unsigned char *b = skb_tail_pointer(skb);
	struct tc_sfq_qopt_v1 opt;
	struct red_parms *p = q->red_parms;

	memset(&opt, 0, sizeof(opt));
	opt.v0.quantum	= q->quantum;
	opt.v0.perturb_period = q->perturb_period / HZ;
	opt.v0.limit	= q->limit;
	opt.v0.divisor	= q->divisor;
	opt.v0.flows	= q->maxflows;
	opt.depth	= q->maxdepth;
	opt.headdrop	= q->headdrop;

	if (p) {
		opt.qth_min	= p->qth_min >> p->Wlog;
		opt.qth_max	= p->qth_max >> p->Wlog;
		opt.Wlog	= p->Wlog;
		opt.Plog	= p->Plog;
		opt.Scell_log	= p->Scell_log;
		opt.max_P	= p->max_P;
	}
	memcpy(&opt.stats, &q->stats, sizeof(opt.stats));
	opt.flags	= q->flags;

	if (nla_put(skb, TCA_OPTIONS, sizeof(opt), &opt))
		goto nla_put_failure;

	return skb->len;

nla_put_failure:
	nlmsg_trim(skb, b);
	return -1;
}

static struct Qdisc *sfq_leaf(struct Qdisc *sch, unsigned long arg)
{
	return NULL;
}

static unsigned long sfq_get(struct Qdisc *sch, u32 classid)
{
	return 0;
}

static unsigned long sfq_bind(struct Qdisc *sch, unsigned long parent,
			      u32 classid)
{
	/* we cannot bypass queue discipline anymore */
	sch->flags &= ~TCQ_F_CAN_BYPASS;
	return 0;
}

static void sfq_put(struct Qdisc *q, unsigned long cl)
{
}

static struct tcf_proto __rcu **sfq_find_tcf(struct Qdisc *sch,
					     unsigned long cl)
{
	struct sfq_sched_data *q = qdisc_priv(sch);

	if (cl)
		return NULL;
	return &q->filter_list;
}

static int sfq_dump_class(struct Qdisc *sch, unsigned long cl,
			  struct sk_buff *skb, struct tcmsg *tcm)
{
	tcm->tcm_handle |= TC_H_MIN(cl);
	return 0;
}

static int sfq_dump_class_stats(struct Qdisc *sch, unsigned long cl,
				struct gnet_dump *d)
{
	struct sfq_sched_data *q = qdisc_priv(sch);
	sfq_index idx = q->ht[cl - 1];
	struct gnet_stats_queue qs = { 0 };
	struct tc_sfq_xstats xstats = { 0 };

	if (idx != SFQ_EMPTY_SLOT) {
		const struct sfq_slot *slot = &q->slots[idx];

		xstats.allot = slot->allot << SFQ_ALLOT_SHIFT;
		qs.qlen = slot->qlen;
		qs.backlog = slot->backlog;
	}
	if (gnet_stats_copy_queue(d, NULL, &qs, qs.qlen) < 0)
		return -1;
	return gnet_stats_copy_app(d, &xstats, sizeof(xstats));
}

static void sfq_walk(struct Qdisc *sch, struct qdisc_walker *arg)
{
	struct sfq_sched_data *q = qdisc_priv(sch);
	unsigned int i;

	if (arg->stop)
		return;

	for (i = 0; i < q->divisor; i++) {
		if (q->ht[i] == SFQ_EMPTY_SLOT ||
		    arg->count < arg->skip) {
			arg->count++;
			continue;
		}
		if (arg->fn(sch, i + 1, arg) < 0) {
			arg->stop = 1;
			break;
		}
		arg->count++;
	}
}

static const struct Qdisc_class_ops sfq_class_ops = {
	.leaf		=	sfq_leaf,
	.get		=	sfq_get,
	.put		=	sfq_put,
	.tcf_chain	=	sfq_find_tcf,
	.bind_tcf	=	sfq_bind,
	.unbind_tcf	=	sfq_put,
	.dump		=	sfq_dump_class,
	.dump_stats	=	sfq_dump_class_stats,
	.walk		=	sfq_walk,
};

static struct Qdisc_ops sfq_qdisc_ops __read_mostly = {
	.cl_ops		=	&sfq_class_ops,
	.id		=	"sfq",
	.priv_size	=	sizeof(struct sfq_sched_data),
	.enqueue	=	sfq_enqueue,
	.dequeue	=	sfq_dequeue,
	.peek		=	qdisc_peek_dequeued,
	.drop		=	sfq_drop,
	.init		=	sfq_init,
	.reset		=	sfq_reset,
	.destroy	=	sfq_destroy,
	.change		=	NULL,
	.dump		=	sfq_dump,
	.owner		=	THIS_MODULE,
};

static int __init sfq_module_init(void)
{
	return register_qdisc(&sfq_qdisc_ops);
}
static void __exit sfq_module_exit(void)
{
	unregister_qdisc(&sfq_qdisc_ops);
}
module_init(sfq_module_init)
module_exit(sfq_module_exit)
MODULE_LICENSE("GPL");
