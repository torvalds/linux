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
#include <net/flow_keys.h>


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
	This implementation limits maximal queue length to 128;
	max mtu to 2^18-1; max 128 flows, number of hash buckets to 1024.
	The only goal of this restrictions was that all data
	fit into one 4K page on 32bit arches.

	It is easy to increase these values, but not in flight.  */

#define SFQ_DEPTH		128 /* max number of packets per flow */
#define SFQ_SLOTS		128 /* max number of flows */
#define SFQ_EMPTY_SLOT		255
#define SFQ_DEFAULT_HASH_DIVISOR 1024

/* We use 16 bits to store allot, and want to handle packets up to 64K
 * Scale allot by 8 (1<<3) so that no overflow occurs.
 */
#define SFQ_ALLOT_SHIFT		3
#define SFQ_ALLOT_SIZE(X)	DIV_ROUND_UP(X, 1 << SFQ_ALLOT_SHIFT)

/* This type should contain at least SFQ_DEPTH + SFQ_SLOTS values */
typedef unsigned char sfq_index;

/*
 * We dont use pointers to save space.
 * Small indexes [0 ... SFQ_SLOTS - 1] are 'pointers' to slots[] array
 * while following values [SFQ_SLOTS ... SFQ_SLOTS + SFQ_DEPTH - 1]
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
	sfq_index	next; /* next slot in sfq chain */
	struct sfq_head dep; /* anchor in dep[] chains */
	unsigned short	hash; /* hash value (index in ht[]) */
	short		allot; /* credit for this slot */
};

struct sfq_sched_data {
/* Parameters */
	int		perturb_period;
	unsigned int	quantum;	/* Allotment per round: MUST BE >= MTU */
	int		limit;
	unsigned int	divisor;	/* number of slots in hash table */
/* Variables */
	struct tcf_proto *filter_list;
	struct timer_list perturb_timer;
	u32		perturbation;
	sfq_index	cur_depth;	/* depth of longest slot */
	unsigned short  scaled_quantum; /* SFQ_ALLOT_SIZE(quantum) */
	struct sfq_slot *tail;		/* current slot in round */
	sfq_index	*ht;		/* Hash table (divisor slots) */
	struct sfq_slot	slots[SFQ_SLOTS];
	struct sfq_head	dep[SFQ_DEPTH];	/* Linked list of slots, indexed by depth */
};

/*
 * sfq_head are either in a sfq_slot or in dep[] array
 */
static inline struct sfq_head *sfq_dep_head(struct sfq_sched_data *q, sfq_index val)
{
	if (val < SFQ_SLOTS)
		return &q->slots[val].dep;
	return &q->dep[val - SFQ_SLOTS];
}

/*
 * In order to be able to quickly rehash our queue when timer changes
 * q->perturbation, we store flow_keys in skb->cb[]
 */
struct sfq_skb_cb {
       struct flow_keys        keys;
};

static inline struct sfq_skb_cb *sfq_skb_cb(const struct sk_buff *skb)
{
       BUILD_BUG_ON(sizeof(skb->cb) <
               sizeof(struct qdisc_skb_cb) + sizeof(struct sfq_skb_cb));
       return (struct sfq_skb_cb *)qdisc_skb_cb(skb)->data;
}

static unsigned int sfq_hash(const struct sfq_sched_data *q,
			     const struct sk_buff *skb)
{
	const struct flow_keys *keys = &sfq_skb_cb(skb)->keys;
	unsigned int hash;

	hash = jhash_3words((__force u32)keys->dst,
			    (__force u32)keys->src ^ keys->ip_proto,
			    (__force u32)keys->ports, q->perturbation);
	return hash & (q->divisor - 1);
}

static unsigned int sfq_classify(struct sk_buff *skb, struct Qdisc *sch,
				 int *qerr)
{
	struct sfq_sched_data *q = qdisc_priv(sch);
	struct tcf_result res;
	int result;

	if (TC_H_MAJ(skb->priority) == sch->handle &&
	    TC_H_MIN(skb->priority) > 0 &&
	    TC_H_MIN(skb->priority) <= q->divisor)
		return TC_H_MIN(skb->priority);

	if (!q->filter_list) {
		skb_flow_dissect(skb, &sfq_skb_cb(skb)->keys);
		return sfq_hash(q, skb) + 1;
	}

	*qerr = NET_XMIT_SUCCESS | __NET_XMIT_BYPASS;
	result = tc_classify(skb, q->filter_list, &res);
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
 * x : slot number [0 .. SFQ_SLOTS - 1]
 */
static inline void sfq_link(struct sfq_sched_data *q, sfq_index x)
{
	sfq_index p, n;
	int qlen = q->slots[x].qlen;

	p = qlen + SFQ_SLOTS;
	n = q->dep[qlen].next;

	q->slots[x].dep.next = n;
	q->slots[x].dep.prev = p;

	q->dep[qlen].next = x;		/* sfq_dep_head(q, p)->next = x */
	sfq_dep_head(q, n)->prev = x;
}

#define sfq_unlink(q, x, n, p)			\
	n = q->slots[x].dep.next;		\
	p = q->slots[x].dep.prev;		\
	sfq_dep_head(q, p)->next = n;		\
	sfq_dep_head(q, n)->prev = p


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

#define	slot_queue_walk(slot, skb)		\
	for (skb = slot->skblist_next;		\
	     skb != (struct sk_buff *)slot;	\
	     skb = skb->next)

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
		skb = slot_dequeue_tail(slot);
		len = qdisc_pkt_len(skb);
		sfq_dec(q, x);
		kfree_skb(skb);
		sch->q.qlen--;
		sch->qstats.drops++;
		sch->qstats.backlog -= len;
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

static int
sfq_enqueue(struct sk_buff *skb, struct Qdisc *sch)
{
	struct sfq_sched_data *q = qdisc_priv(sch);
	unsigned int hash;
	sfq_index x, qlen;
	struct sfq_slot *slot;
	int uninitialized_var(ret);

	hash = sfq_classify(skb, sch, &ret);
	if (hash == 0) {
		if (ret & __NET_XMIT_BYPASS)
			sch->qstats.drops++;
		kfree_skb(skb);
		return ret;
	}
	hash--;

	x = q->ht[hash];
	slot = &q->slots[x];
	if (x == SFQ_EMPTY_SLOT) {
		x = q->dep[0].next; /* get a free slot */
		q->ht[hash] = x;
		slot = &q->slots[x];
		slot->hash = hash;
	}

	/* If selected queue has length q->limit, do simple tail drop,
	 * i.e. drop _this_ packet.
	 */
	if (slot->qlen >= q->limit)
		return qdisc_drop(skb, sch);

	sch->qstats.backlog += qdisc_pkt_len(skb);
	slot_queue_add(slot, skb);
	sfq_inc(q, x);
	if (slot->qlen == 1) {		/* The flow is new */
		if (q->tail == NULL) {	/* It is the first flow */
			slot->next = x;
			q->tail = slot;
		} else {
			slot->next = q->tail->next;
			q->tail->next = x;
		}
		slot->allot = q->scaled_quantum;
	}
	if (++sch->q.qlen <= q->limit)
		return NET_XMIT_SUCCESS;

	qlen = slot->qlen;
	sfq_drop(sch);
	/* Return Congestion Notification only if we dropped a packet
	 * from this flow.
	 */
	if (qlen != slot->qlen)
		return NET_XMIT_CN;

	/* As we dropped a packet, better let upper stack know this */
	qdisc_tree_decrease_qlen(sch, 1);
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
	sch->qstats.backlog -= qdisc_pkt_len(skb);

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
static void sfq_rehash(struct sfq_sched_data *q)
{
	struct sk_buff *skb;
	int i;
	struct sfq_slot *slot;
	struct sk_buff_head list;

	__skb_queue_head_init(&list);

	for (i = 0; i < SFQ_SLOTS; i++) {
		slot = &q->slots[i];
		if (!slot->qlen)
			continue;
		while (slot->qlen) {
			skb = slot_dequeue_head(slot);
			sfq_dec(q, i);
			__skb_queue_tail(&list, skb);
		}
		q->ht[slot->hash] = SFQ_EMPTY_SLOT;
	}
	q->tail = NULL;

	while ((skb = __skb_dequeue(&list)) != NULL) {
		unsigned int hash = sfq_hash(q, skb);
		sfq_index x = q->ht[hash];

		slot = &q->slots[x];
		if (x == SFQ_EMPTY_SLOT) {
			x = q->dep[0].next; /* get a free slot */
			q->ht[hash] = x;
			slot = &q->slots[x];
			slot->hash = hash;
		}
		slot_queue_add(slot, skb);
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
}

static void sfq_perturbation(unsigned long arg)
{
	struct Qdisc *sch = (struct Qdisc *)arg;
	struct sfq_sched_data *q = qdisc_priv(sch);
	spinlock_t *root_lock = qdisc_lock(qdisc_root_sleeping(sch));

	spin_lock(root_lock);
	q->perturbation = net_random();
	if (!q->filter_list && q->tail)
		sfq_rehash(q);
	spin_unlock(root_lock);

	if (q->perturb_period)
		mod_timer(&q->perturb_timer, jiffies + q->perturb_period);
}

static int sfq_change(struct Qdisc *sch, struct nlattr *opt)
{
	struct sfq_sched_data *q = qdisc_priv(sch);
	struct tc_sfq_qopt *ctl = nla_data(opt);
	unsigned int qlen;

	if (opt->nla_len < nla_attr_size(sizeof(*ctl)))
		return -EINVAL;

	if (ctl->divisor &&
	    (!is_power_of_2(ctl->divisor) || ctl->divisor > 65536))
		return -EINVAL;

	sch_tree_lock(sch);
	q->quantum = ctl->quantum ? : psched_mtu(qdisc_dev(sch));
	q->scaled_quantum = SFQ_ALLOT_SIZE(q->quantum);
	q->perturb_period = ctl->perturb_period * HZ;
	if (ctl->limit)
		q->limit = min_t(u32, ctl->limit, SFQ_DEPTH - 1);
	if (ctl->divisor)
		q->divisor = ctl->divisor;
	qlen = sch->q.qlen;
	while (sch->q.qlen > q->limit)
		sfq_drop(sch);
	qdisc_tree_decrease_qlen(sch, qlen - sch->q.qlen);

	del_timer(&q->perturb_timer);
	if (q->perturb_period) {
		mod_timer(&q->perturb_timer, jiffies + q->perturb_period);
		q->perturbation = net_random();
	}
	sch_tree_unlock(sch);
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
	if (addr) {
		if (is_vmalloc_addr(addr))
			vfree(addr);
		else
			kfree(addr);
	}
}

static void sfq_destroy(struct Qdisc *sch)
{
	struct sfq_sched_data *q = qdisc_priv(sch);

	tcf_destroy_chain(&q->filter_list);
	q->perturb_period = 0;
	del_timer_sync(&q->perturb_timer);
	sfq_free(q->ht);
}

static int sfq_init(struct Qdisc *sch, struct nlattr *opt)
{
	struct sfq_sched_data *q = qdisc_priv(sch);
	int i;

	q->perturb_timer.function = sfq_perturbation;
	q->perturb_timer.data = (unsigned long)sch;
	init_timer_deferrable(&q->perturb_timer);

	for (i = 0; i < SFQ_DEPTH; i++) {
		q->dep[i].next = i + SFQ_SLOTS;
		q->dep[i].prev = i + SFQ_SLOTS;
	}

	q->limit = SFQ_DEPTH - 1;
	q->cur_depth = 0;
	q->tail = NULL;
	q->divisor = SFQ_DEFAULT_HASH_DIVISOR;
	q->quantum = psched_mtu(qdisc_dev(sch));
	q->scaled_quantum = SFQ_ALLOT_SIZE(q->quantum);
	q->perturb_period = 0;
	q->perturbation = net_random();

	if (opt) {
		int err = sfq_change(sch, opt);
		if (err)
			return err;
	}

	q->ht = sfq_alloc(sizeof(q->ht[0]) * q->divisor);
	if (!q->ht) {
		sfq_destroy(sch);
		return -ENOMEM;
	}
	for (i = 0; i < q->divisor; i++)
		q->ht[i] = SFQ_EMPTY_SLOT;

	for (i = 0; i < SFQ_SLOTS; i++) {
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
	struct tc_sfq_qopt opt;

	opt.quantum = q->quantum;
	opt.perturb_period = q->perturb_period / HZ;

	opt.limit = q->limit;
	opt.divisor = q->divisor;
	opt.flows = q->limit;

	NLA_PUT(skb, TCA_OPTIONS, sizeof(opt), &opt);

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

static struct tcf_proto **sfq_find_tcf(struct Qdisc *sch, unsigned long cl)
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
	struct sk_buff *skb;

	if (idx != SFQ_EMPTY_SLOT) {
		const struct sfq_slot *slot = &q->slots[idx];

		xstats.allot = slot->allot << SFQ_ALLOT_SHIFT;
		qs.qlen = slot->qlen;
		slot_queue_walk(slot, skb)
			qs.backlog += qdisc_pkt_len(skb);
	}
	if (gnet_stats_copy_queue(d, &qs) < 0)
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
