// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * net/sched/sch_htb.c	Hierarchical token bucket, feed tree version
 *
 * Authors:	Martin Devera, <devik@cdi.cz>
 *
 * Credits (in time order) for older HTB versions:
 *              Stef Coene <stef.coene@docum.org>
 *			HTB support at LARTC mailing list
 *		Ondrej Kraus, <krauso@barr.cz>
 *			found missing INIT_QDISC(htb)
 *		Vladimir Smelhaus, Aamer Akhter, Bert Hubert
 *			helped a lot to locate nasty class stall bug
 *		Andi Kleen, Jamal Hadi, Bert Hubert
 *			code review and helpful comments on shaping
 *		Tomasz Wrona, <tw@eter.tym.pl>
 *			created test case so that I was able to fix nasty bug
 *		Wilfried Weissmann
 *			spotted bug in dequeue code and helped with fix
 *		Jiri Fojtasek
 *			fixed requeue routine
 *		and many others. thanks.
 */
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/errno.h>
#include <linux/skbuff.h>
#include <linux/list.h>
#include <linux/compiler.h>
#include <linux/rbtree.h>
#include <linux/workqueue.h>
#include <linux/slab.h>
#include <net/netlink.h>
#include <net/sch_generic.h>
#include <net/pkt_sched.h>
#include <net/pkt_cls.h>

/* HTB algorithm.
    Author: devik@cdi.cz
    ========================================================================
    HTB is like TBF with multiple classes. It is also similar to CBQ because
    it allows to assign priority to each class in hierarchy.
    In fact it is another implementation of Floyd's formal sharing.

    Levels:
    Each class is assigned level. Leaf has ALWAYS level 0 and root
    classes have level TC_HTB_MAXDEPTH-1. Interior nodes has level
    one less than their parent.
*/

static int htb_hysteresis __read_mostly = 0; /* whether to use mode hysteresis for speedup */
#define HTB_VER 0x30011		/* major must be matched with number supplied by TC as version */

#if HTB_VER >> 16 != TC_HTB_PROTOVER
#error "Mismatched sch_htb.c and pkt_sch.h"
#endif

/* Module parameter and sysfs export */
module_param    (htb_hysteresis, int, 0640);
MODULE_PARM_DESC(htb_hysteresis, "Hysteresis mode, less CPU load, less accurate");

static int htb_rate_est = 0; /* htb classes have a default rate estimator */
module_param(htb_rate_est, int, 0640);
MODULE_PARM_DESC(htb_rate_est, "setup a default rate estimator (4sec 16sec) for htb classes");

/* used internaly to keep status of single class */
enum htb_cmode {
	HTB_CANT_SEND,		/* class can't send and can't borrow */
	HTB_MAY_BORROW,		/* class can't send but may borrow */
	HTB_CAN_SEND		/* class can send */
};

struct htb_prio {
	union {
		struct rb_root	row;
		struct rb_root	feed;
	};
	struct rb_node	*ptr;
	/* When class changes from state 1->2 and disconnects from
	 * parent's feed then we lost ptr value and start from the
	 * first child again. Here we store classid of the
	 * last valid ptr (used when ptr is NULL).
	 */
	u32		last_ptr_id;
};

/* interior & leaf nodes; props specific to leaves are marked L:
 * To reduce false sharing, place mostly read fields at beginning,
 * and mostly written ones at the end.
 */
struct htb_class {
	struct Qdisc_class_common common;
	struct psched_ratecfg	rate;
	struct psched_ratecfg	ceil;
	s64			buffer, cbuffer;/* token bucket depth/rate */
	s64			mbuffer;	/* max wait time */
	u32			prio;		/* these two are used only by leaves... */
	int			quantum;	/* but stored for parent-to-leaf return */

	struct tcf_proto __rcu	*filter_list;	/* class attached filters */
	struct tcf_block	*block;

	int			level;		/* our level (see above) */
	unsigned int		children;
	struct htb_class	*parent;	/* parent class */

	struct net_rate_estimator __rcu *rate_est;

	/*
	 * Written often fields
	 */
	struct gnet_stats_basic_sync bstats;
	struct gnet_stats_basic_sync bstats_bias;
	struct tc_htb_xstats	xstats;	/* our special stats */

	/* token bucket parameters */
	s64			tokens, ctokens;/* current number of tokens */
	s64			t_c;		/* checkpoint time */

	union {
		struct htb_class_leaf {
			int		deficit[TC_HTB_MAXDEPTH];
			struct Qdisc	*q;
			struct netdev_queue *offload_queue;
		} leaf;
		struct htb_class_inner {
			struct htb_prio clprio[TC_HTB_NUMPRIO];
		} inner;
	};
	s64			pq_key;

	int			prio_activity;	/* for which prios are we active */
	enum htb_cmode		cmode;		/* current mode of the class */
	struct rb_node		pq_node;	/* node for event queue */
	struct rb_node		node[TC_HTB_NUMPRIO];	/* node for self or feed tree */

	unsigned int drops ____cacheline_aligned_in_smp;
	unsigned int		overlimits;
};

struct htb_level {
	struct rb_root	wait_pq;
	struct htb_prio hprio[TC_HTB_NUMPRIO];
};

struct htb_sched {
	struct Qdisc_class_hash clhash;
	int			defcls;		/* class where unclassified flows go to */
	int			rate2quantum;	/* quant = rate / rate2quantum */

	/* filters for qdisc itself */
	struct tcf_proto __rcu	*filter_list;
	struct tcf_block	*block;

#define HTB_WARN_TOOMANYEVENTS	0x1
	unsigned int		warned;	/* only one warning */
	int			direct_qlen;
	struct work_struct	work;

	/* non shaped skbs; let them go directly thru */
	struct qdisc_skb_head	direct_queue;
	u32			direct_pkts;
	u32			overlimits;

	struct qdisc_watchdog	watchdog;

	s64			now;	/* cached dequeue time */

	/* time of nearest event per level (row) */
	s64			near_ev_cache[TC_HTB_MAXDEPTH];

	int			row_mask[TC_HTB_MAXDEPTH];

	struct htb_level	hlevel[TC_HTB_MAXDEPTH];

	struct Qdisc		**direct_qdiscs;
	unsigned int            num_direct_qdiscs;

	bool			offload;
};

/* find class in global hash table using given handle */
static inline struct htb_class *htb_find(u32 handle, struct Qdisc *sch)
{
	struct htb_sched *q = qdisc_priv(sch);
	struct Qdisc_class_common *clc;

	clc = qdisc_class_find(&q->clhash, handle);
	if (clc == NULL)
		return NULL;
	return container_of(clc, struct htb_class, common);
}

static unsigned long htb_search(struct Qdisc *sch, u32 handle)
{
	return (unsigned long)htb_find(handle, sch);
}

#define HTB_DIRECT ((struct htb_class *)-1L)

/**
 * htb_classify - classify a packet into class
 * @skb: the socket buffer
 * @sch: the active queue discipline
 * @qerr: pointer for returned status code
 *
 * It returns NULL if the packet should be dropped or -1 if the packet
 * should be passed directly thru. In all other cases leaf class is returned.
 * We allow direct class selection by classid in priority. The we examine
 * filters in qdisc and in inner nodes (if higher filter points to the inner
 * node). If we end up with classid MAJOR:0 we enqueue the skb into special
 * internal fifo (direct). These packets then go directly thru. If we still
 * have no valid leaf we try to use MAJOR:default leaf. It still unsuccessful
 * then finish and return direct queue.
 */
static struct htb_class *htb_classify(struct sk_buff *skb, struct Qdisc *sch,
				      int *qerr)
{
	struct htb_sched *q = qdisc_priv(sch);
	struct htb_class *cl;
	struct tcf_result res;
	struct tcf_proto *tcf;
	int result;

	/* allow to select class by setting skb->priority to valid classid;
	 * note that nfmark can be used too by attaching filter fw with no
	 * rules in it
	 */
	if (skb->priority == sch->handle)
		return HTB_DIRECT;	/* X:0 (direct flow) selected */
	cl = htb_find(skb->priority, sch);
	if (cl) {
		if (cl->level == 0)
			return cl;
		/* Start with inner filter chain if a non-leaf class is selected */
		tcf = rcu_dereference_bh(cl->filter_list);
	} else {
		tcf = rcu_dereference_bh(q->filter_list);
	}

	*qerr = NET_XMIT_SUCCESS | __NET_XMIT_BYPASS;
	while (tcf && (result = tcf_classify(skb, NULL, tcf, &res, false)) >= 0) {
#ifdef CONFIG_NET_CLS_ACT
		switch (result) {
		case TC_ACT_QUEUED:
		case TC_ACT_STOLEN:
		case TC_ACT_TRAP:
			*qerr = NET_XMIT_SUCCESS | __NET_XMIT_STOLEN;
			fallthrough;
		case TC_ACT_SHOT:
			return NULL;
		}
#endif
		cl = (void *)res.class;
		if (!cl) {
			if (res.classid == sch->handle)
				return HTB_DIRECT;	/* X:0 (direct flow) */
			cl = htb_find(res.classid, sch);
			if (!cl)
				break;	/* filter selected invalid classid */
		}
		if (!cl->level)
			return cl;	/* we hit leaf; return it */

		/* we have got inner class; apply inner filter chain */
		tcf = rcu_dereference_bh(cl->filter_list);
	}
	/* classification failed; try to use default class */
	cl = htb_find(TC_H_MAKE(TC_H_MAJ(sch->handle), q->defcls), sch);
	if (!cl || cl->level)
		return HTB_DIRECT;	/* bad default .. this is safe bet */
	return cl;
}

/**
 * htb_add_to_id_tree - adds class to the round robin list
 * @root: the root of the tree
 * @cl: the class to add
 * @prio: the give prio in class
 *
 * Routine adds class to the list (actually tree) sorted by classid.
 * Make sure that class is not already on such list for given prio.
 */
static void htb_add_to_id_tree(struct rb_root *root,
			       struct htb_class *cl, int prio)
{
	struct rb_node **p = &root->rb_node, *parent = NULL;

	while (*p) {
		struct htb_class *c;
		parent = *p;
		c = rb_entry(parent, struct htb_class, node[prio]);

		if (cl->common.classid > c->common.classid)
			p = &parent->rb_right;
		else
			p = &parent->rb_left;
	}
	rb_link_node(&cl->node[prio], parent, p);
	rb_insert_color(&cl->node[prio], root);
}

/**
 * htb_add_to_wait_tree - adds class to the event queue with delay
 * @q: the priority event queue
 * @cl: the class to add
 * @delay: delay in microseconds
 *
 * The class is added to priority event queue to indicate that class will
 * change its mode in cl->pq_key microseconds. Make sure that class is not
 * already in the queue.
 */
static void htb_add_to_wait_tree(struct htb_sched *q,
				 struct htb_class *cl, s64 delay)
{
	struct rb_node **p = &q->hlevel[cl->level].wait_pq.rb_node, *parent = NULL;

	cl->pq_key = q->now + delay;
	if (cl->pq_key == q->now)
		cl->pq_key++;

	/* update the nearest event cache */
	if (q->near_ev_cache[cl->level] > cl->pq_key)
		q->near_ev_cache[cl->level] = cl->pq_key;

	while (*p) {
		struct htb_class *c;
		parent = *p;
		c = rb_entry(parent, struct htb_class, pq_node);
		if (cl->pq_key >= c->pq_key)
			p = &parent->rb_right;
		else
			p = &parent->rb_left;
	}
	rb_link_node(&cl->pq_node, parent, p);
	rb_insert_color(&cl->pq_node, &q->hlevel[cl->level].wait_pq);
}

/**
 * htb_next_rb_node - finds next node in binary tree
 * @n: the current node in binary tree
 *
 * When we are past last key we return NULL.
 * Average complexity is 2 steps per call.
 */
static inline void htb_next_rb_node(struct rb_node **n)
{
	*n = rb_next(*n);
}

/**
 * htb_add_class_to_row - add class to its row
 * @q: the priority event queue
 * @cl: the class to add
 * @mask: the given priorities in class in bitmap
 *
 * The class is added to row at priorities marked in mask.
 * It does nothing if mask == 0.
 */
static inline void htb_add_class_to_row(struct htb_sched *q,
					struct htb_class *cl, int mask)
{
	q->row_mask[cl->level] |= mask;
	while (mask) {
		int prio = ffz(~mask);
		mask &= ~(1 << prio);
		htb_add_to_id_tree(&q->hlevel[cl->level].hprio[prio].row, cl, prio);
	}
}

/* If this triggers, it is a bug in this code, but it need not be fatal */
static void htb_safe_rb_erase(struct rb_node *rb, struct rb_root *root)
{
	if (RB_EMPTY_NODE(rb)) {
		WARN_ON(1);
	} else {
		rb_erase(rb, root);
		RB_CLEAR_NODE(rb);
	}
}


/**
 * htb_remove_class_from_row - removes class from its row
 * @q: the priority event queue
 * @cl: the class to add
 * @mask: the given priorities in class in bitmap
 *
 * The class is removed from row at priorities marked in mask.
 * It does nothing if mask == 0.
 */
static inline void htb_remove_class_from_row(struct htb_sched *q,
						 struct htb_class *cl, int mask)
{
	int m = 0;
	struct htb_level *hlevel = &q->hlevel[cl->level];

	while (mask) {
		int prio = ffz(~mask);
		struct htb_prio *hprio = &hlevel->hprio[prio];

		mask &= ~(1 << prio);
		if (hprio->ptr == cl->node + prio)
			htb_next_rb_node(&hprio->ptr);

		htb_safe_rb_erase(cl->node + prio, &hprio->row);
		if (!hprio->row.rb_node)
			m |= 1 << prio;
	}
	q->row_mask[cl->level] &= ~m;
}

/**
 * htb_activate_prios - creates active classe's feed chain
 * @q: the priority event queue
 * @cl: the class to activate
 *
 * The class is connected to ancestors and/or appropriate rows
 * for priorities it is participating on. cl->cmode must be new
 * (activated) mode. It does nothing if cl->prio_activity == 0.
 */
static void htb_activate_prios(struct htb_sched *q, struct htb_class *cl)
{
	struct htb_class *p = cl->parent;
	long m, mask = cl->prio_activity;

	while (cl->cmode == HTB_MAY_BORROW && p && mask) {
		m = mask;
		while (m) {
			unsigned int prio = ffz(~m);

			if (WARN_ON_ONCE(prio >= ARRAY_SIZE(p->inner.clprio)))
				break;
			m &= ~(1 << prio);

			if (p->inner.clprio[prio].feed.rb_node)
				/* parent already has its feed in use so that
				 * reset bit in mask as parent is already ok
				 */
				mask &= ~(1 << prio);

			htb_add_to_id_tree(&p->inner.clprio[prio].feed, cl, prio);
		}
		p->prio_activity |= mask;
		cl = p;
		p = cl->parent;

	}
	if (cl->cmode == HTB_CAN_SEND && mask)
		htb_add_class_to_row(q, cl, mask);
}

/**
 * htb_deactivate_prios - remove class from feed chain
 * @q: the priority event queue
 * @cl: the class to deactivate
 *
 * cl->cmode must represent old mode (before deactivation). It does
 * nothing if cl->prio_activity == 0. Class is removed from all feed
 * chains and rows.
 */
static void htb_deactivate_prios(struct htb_sched *q, struct htb_class *cl)
{
	struct htb_class *p = cl->parent;
	long m, mask = cl->prio_activity;

	while (cl->cmode == HTB_MAY_BORROW && p && mask) {
		m = mask;
		mask = 0;
		while (m) {
			int prio = ffz(~m);
			m &= ~(1 << prio);

			if (p->inner.clprio[prio].ptr == cl->node + prio) {
				/* we are removing child which is pointed to from
				 * parent feed - forget the pointer but remember
				 * classid
				 */
				p->inner.clprio[prio].last_ptr_id = cl->common.classid;
				p->inner.clprio[prio].ptr = NULL;
			}

			htb_safe_rb_erase(cl->node + prio,
					  &p->inner.clprio[prio].feed);

			if (!p->inner.clprio[prio].feed.rb_node)
				mask |= 1 << prio;
		}

		p->prio_activity &= ~mask;
		cl = p;
		p = cl->parent;

	}
	if (cl->cmode == HTB_CAN_SEND && mask)
		htb_remove_class_from_row(q, cl, mask);
}

static inline s64 htb_lowater(const struct htb_class *cl)
{
	if (htb_hysteresis)
		return cl->cmode != HTB_CANT_SEND ? -cl->cbuffer : 0;
	else
		return 0;
}
static inline s64 htb_hiwater(const struct htb_class *cl)
{
	if (htb_hysteresis)
		return cl->cmode == HTB_CAN_SEND ? -cl->buffer : 0;
	else
		return 0;
}


/**
 * htb_class_mode - computes and returns current class mode
 * @cl: the target class
 * @diff: diff time in microseconds
 *
 * It computes cl's mode at time cl->t_c+diff and returns it. If mode
 * is not HTB_CAN_SEND then cl->pq_key is updated to time difference
 * from now to time when cl will change its state.
 * Also it is worth to note that class mode doesn't change simply
 * at cl->{c,}tokens == 0 but there can rather be hysteresis of
 * 0 .. -cl->{c,}buffer range. It is meant to limit number of
 * mode transitions per time unit. The speed gain is about 1/6.
 */
static inline enum htb_cmode
htb_class_mode(struct htb_class *cl, s64 *diff)
{
	s64 toks;

	if ((toks = (cl->ctokens + *diff)) < htb_lowater(cl)) {
		*diff = -toks;
		return HTB_CANT_SEND;
	}

	if ((toks = (cl->tokens + *diff)) >= htb_hiwater(cl))
		return HTB_CAN_SEND;

	*diff = -toks;
	return HTB_MAY_BORROW;
}

/**
 * htb_change_class_mode - changes classe's mode
 * @q: the priority event queue
 * @cl: the target class
 * @diff: diff time in microseconds
 *
 * This should be the only way how to change classe's mode under normal
 * circumstances. Routine will update feed lists linkage, change mode
 * and add class to the wait event queue if appropriate. New mode should
 * be different from old one and cl->pq_key has to be valid if changing
 * to mode other than HTB_CAN_SEND (see htb_add_to_wait_tree).
 */
static void
htb_change_class_mode(struct htb_sched *q, struct htb_class *cl, s64 *diff)
{
	enum htb_cmode new_mode = htb_class_mode(cl, diff);

	if (new_mode == cl->cmode)
		return;

	if (new_mode == HTB_CANT_SEND) {
		cl->overlimits++;
		q->overlimits++;
	}

	if (cl->prio_activity) {	/* not necessary: speed optimization */
		if (cl->cmode != HTB_CANT_SEND)
			htb_deactivate_prios(q, cl);
		cl->cmode = new_mode;
		if (new_mode != HTB_CANT_SEND)
			htb_activate_prios(q, cl);
	} else
		cl->cmode = new_mode;
}

/**
 * htb_activate - inserts leaf cl into appropriate active feeds
 * @q: the priority event queue
 * @cl: the target class
 *
 * Routine learns (new) priority of leaf and activates feed chain
 * for the prio. It can be called on already active leaf safely.
 * It also adds leaf into droplist.
 */
static inline void htb_activate(struct htb_sched *q, struct htb_class *cl)
{
	WARN_ON(cl->level || !cl->leaf.q || !cl->leaf.q->q.qlen);

	if (!cl->prio_activity) {
		cl->prio_activity = 1 << cl->prio;
		htb_activate_prios(q, cl);
	}
}

/**
 * htb_deactivate - remove leaf cl from active feeds
 * @q: the priority event queue
 * @cl: the target class
 *
 * Make sure that leaf is active. In the other words it can't be called
 * with non-active leaf. It also removes class from the drop list.
 */
static inline void htb_deactivate(struct htb_sched *q, struct htb_class *cl)
{
	WARN_ON(!cl->prio_activity);

	htb_deactivate_prios(q, cl);
	cl->prio_activity = 0;
}

static int htb_enqueue(struct sk_buff *skb, struct Qdisc *sch,
		       struct sk_buff **to_free)
{
	int ret;
	unsigned int len = qdisc_pkt_len(skb);
	struct htb_sched *q = qdisc_priv(sch);
	struct htb_class *cl = htb_classify(skb, sch, &ret);

	if (cl == HTB_DIRECT) {
		/* enqueue to helper queue */
		if (q->direct_queue.qlen < q->direct_qlen) {
			__qdisc_enqueue_tail(skb, &q->direct_queue);
			q->direct_pkts++;
		} else {
			return qdisc_drop(skb, sch, to_free);
		}
#ifdef CONFIG_NET_CLS_ACT
	} else if (!cl) {
		if (ret & __NET_XMIT_BYPASS)
			qdisc_qstats_drop(sch);
		__qdisc_drop(skb, to_free);
		return ret;
#endif
	} else if ((ret = qdisc_enqueue(skb, cl->leaf.q,
					to_free)) != NET_XMIT_SUCCESS) {
		if (net_xmit_drop_count(ret)) {
			qdisc_qstats_drop(sch);
			cl->drops++;
		}
		return ret;
	} else {
		htb_activate(q, cl);
	}

	sch->qstats.backlog += len;
	sch->q.qlen++;
	return NET_XMIT_SUCCESS;
}

static inline void htb_accnt_tokens(struct htb_class *cl, int bytes, s64 diff)
{
	s64 toks = diff + cl->tokens;

	if (toks > cl->buffer)
		toks = cl->buffer;
	toks -= (s64) psched_l2t_ns(&cl->rate, bytes);
	if (toks <= -cl->mbuffer)
		toks = 1 - cl->mbuffer;

	cl->tokens = toks;
}

static inline void htb_accnt_ctokens(struct htb_class *cl, int bytes, s64 diff)
{
	s64 toks = diff + cl->ctokens;

	if (toks > cl->cbuffer)
		toks = cl->cbuffer;
	toks -= (s64) psched_l2t_ns(&cl->ceil, bytes);
	if (toks <= -cl->mbuffer)
		toks = 1 - cl->mbuffer;

	cl->ctokens = toks;
}

/**
 * htb_charge_class - charges amount "bytes" to leaf and ancestors
 * @q: the priority event queue
 * @cl: the class to start iterate
 * @level: the minimum level to account
 * @skb: the socket buffer
 *
 * Routine assumes that packet "bytes" long was dequeued from leaf cl
 * borrowing from "level". It accounts bytes to ceil leaky bucket for
 * leaf and all ancestors and to rate bucket for ancestors at levels
 * "level" and higher. It also handles possible change of mode resulting
 * from the update. Note that mode can also increase here (MAY_BORROW to
 * CAN_SEND) because we can use more precise clock that event queue here.
 * In such case we remove class from event queue first.
 */
static void htb_charge_class(struct htb_sched *q, struct htb_class *cl,
			     int level, struct sk_buff *skb)
{
	int bytes = qdisc_pkt_len(skb);
	enum htb_cmode old_mode;
	s64 diff;

	while (cl) {
		diff = min_t(s64, q->now - cl->t_c, cl->mbuffer);
		if (cl->level >= level) {
			if (cl->level == level)
				cl->xstats.lends++;
			htb_accnt_tokens(cl, bytes, diff);
		} else {
			cl->xstats.borrows++;
			cl->tokens += diff;	/* we moved t_c; update tokens */
		}
		htb_accnt_ctokens(cl, bytes, diff);
		cl->t_c = q->now;

		old_mode = cl->cmode;
		diff = 0;
		htb_change_class_mode(q, cl, &diff);
		if (old_mode != cl->cmode) {
			if (old_mode != HTB_CAN_SEND)
				htb_safe_rb_erase(&cl->pq_node, &q->hlevel[cl->level].wait_pq);
			if (cl->cmode != HTB_CAN_SEND)
				htb_add_to_wait_tree(q, cl, diff);
		}

		/* update basic stats except for leaves which are already updated */
		if (cl->level)
			bstats_update(&cl->bstats, skb);

		cl = cl->parent;
	}
}

/**
 * htb_do_events - make mode changes to classes at the level
 * @q: the priority event queue
 * @level: which wait_pq in 'q->hlevel'
 * @start: start jiffies
 *
 * Scans event queue for pending events and applies them. Returns time of
 * next pending event (0 for no event in pq, q->now for too many events).
 * Note: Applied are events whose have cl->pq_key <= q->now.
 */
static s64 htb_do_events(struct htb_sched *q, const int level,
			 unsigned long start)
{
	/* don't run for longer than 2 jiffies; 2 is used instead of
	 * 1 to simplify things when jiffy is going to be incremented
	 * too soon
	 */
	unsigned long stop_at = start + 2;
	struct rb_root *wait_pq = &q->hlevel[level].wait_pq;

	while (time_before(jiffies, stop_at)) {
		struct htb_class *cl;
		s64 diff;
		struct rb_node *p = rb_first(wait_pq);

		if (!p)
			return 0;

		cl = rb_entry(p, struct htb_class, pq_node);
		if (cl->pq_key > q->now)
			return cl->pq_key;

		htb_safe_rb_erase(p, wait_pq);
		diff = min_t(s64, q->now - cl->t_c, cl->mbuffer);
		htb_change_class_mode(q, cl, &diff);
		if (cl->cmode != HTB_CAN_SEND)
			htb_add_to_wait_tree(q, cl, diff);
	}

	/* too much load - let's continue after a break for scheduling */
	if (!(q->warned & HTB_WARN_TOOMANYEVENTS)) {
		pr_warn("htb: too many events!\n");
		q->warned |= HTB_WARN_TOOMANYEVENTS;
	}

	return q->now;
}

/* Returns class->node+prio from id-tree where classe's id is >= id. NULL
 * is no such one exists.
 */
static struct rb_node *htb_id_find_next_upper(int prio, struct rb_node *n,
					      u32 id)
{
	struct rb_node *r = NULL;
	while (n) {
		struct htb_class *cl =
		    rb_entry(n, struct htb_class, node[prio]);

		if (id > cl->common.classid) {
			n = n->rb_right;
		} else if (id < cl->common.classid) {
			r = n;
			n = n->rb_left;
		} else {
			return n;
		}
	}
	return r;
}

/**
 * htb_lookup_leaf - returns next leaf class in DRR order
 * @hprio: the current one
 * @prio: which prio in class
 *
 * Find leaf where current feed pointers points to.
 */
static struct htb_class *htb_lookup_leaf(struct htb_prio *hprio, const int prio)
{
	int i;
	struct {
		struct rb_node *root;
		struct rb_node **pptr;
		u32 *pid;
	} stk[TC_HTB_MAXDEPTH], *sp = stk;

	BUG_ON(!hprio->row.rb_node);
	sp->root = hprio->row.rb_node;
	sp->pptr = &hprio->ptr;
	sp->pid = &hprio->last_ptr_id;

	for (i = 0; i < 65535; i++) {
		if (!*sp->pptr && *sp->pid) {
			/* ptr was invalidated but id is valid - try to recover
			 * the original or next ptr
			 */
			*sp->pptr =
			    htb_id_find_next_upper(prio, sp->root, *sp->pid);
		}
		*sp->pid = 0;	/* ptr is valid now so that remove this hint as it
				 * can become out of date quickly
				 */
		if (!*sp->pptr) {	/* we are at right end; rewind & go up */
			*sp->pptr = sp->root;
			while ((*sp->pptr)->rb_left)
				*sp->pptr = (*sp->pptr)->rb_left;
			if (sp > stk) {
				sp--;
				if (!*sp->pptr) {
					WARN_ON(1);
					return NULL;
				}
				htb_next_rb_node(sp->pptr);
			}
		} else {
			struct htb_class *cl;
			struct htb_prio *clp;

			cl = rb_entry(*sp->pptr, struct htb_class, node[prio]);
			if (!cl->level)
				return cl;
			clp = &cl->inner.clprio[prio];
			(++sp)->root = clp->feed.rb_node;
			sp->pptr = &clp->ptr;
			sp->pid = &clp->last_ptr_id;
		}
	}
	WARN_ON(1);
	return NULL;
}

/* dequeues packet at given priority and level; call only if
 * you are sure that there is active class at prio/level
 */
static struct sk_buff *htb_dequeue_tree(struct htb_sched *q, const int prio,
					const int level)
{
	struct sk_buff *skb = NULL;
	struct htb_class *cl, *start;
	struct htb_level *hlevel = &q->hlevel[level];
	struct htb_prio *hprio = &hlevel->hprio[prio];

	/* look initial class up in the row */
	start = cl = htb_lookup_leaf(hprio, prio);

	do {
next:
		if (unlikely(!cl))
			return NULL;

		/* class can be empty - it is unlikely but can be true if leaf
		 * qdisc drops packets in enqueue routine or if someone used
		 * graft operation on the leaf since last dequeue;
		 * simply deactivate and skip such class
		 */
		if (unlikely(cl->leaf.q->q.qlen == 0)) {
			struct htb_class *next;
			htb_deactivate(q, cl);

			/* row/level might become empty */
			if ((q->row_mask[level] & (1 << prio)) == 0)
				return NULL;

			next = htb_lookup_leaf(hprio, prio);

			if (cl == start)	/* fix start if we just deleted it */
				start = next;
			cl = next;
			goto next;
		}

		skb = cl->leaf.q->dequeue(cl->leaf.q);
		if (likely(skb != NULL))
			break;

		qdisc_warn_nonwc("htb", cl->leaf.q);
		htb_next_rb_node(level ? &cl->parent->inner.clprio[prio].ptr:
					 &q->hlevel[0].hprio[prio].ptr);
		cl = htb_lookup_leaf(hprio, prio);

	} while (cl != start);

	if (likely(skb != NULL)) {
		bstats_update(&cl->bstats, skb);
		cl->leaf.deficit[level] -= qdisc_pkt_len(skb);
		if (cl->leaf.deficit[level] < 0) {
			cl->leaf.deficit[level] += cl->quantum;
			htb_next_rb_node(level ? &cl->parent->inner.clprio[prio].ptr :
						 &q->hlevel[0].hprio[prio].ptr);
		}
		/* this used to be after charge_class but this constelation
		 * gives us slightly better performance
		 */
		if (!cl->leaf.q->q.qlen)
			htb_deactivate(q, cl);
		htb_charge_class(q, cl, level, skb);
	}
	return skb;
}

static struct sk_buff *htb_dequeue(struct Qdisc *sch)
{
	struct sk_buff *skb;
	struct htb_sched *q = qdisc_priv(sch);
	int level;
	s64 next_event;
	unsigned long start_at;

	/* try to dequeue direct packets as high prio (!) to minimize cpu work */
	skb = __qdisc_dequeue_head(&q->direct_queue);
	if (skb != NULL) {
ok:
		qdisc_bstats_update(sch, skb);
		qdisc_qstats_backlog_dec(sch, skb);
		sch->q.qlen--;
		return skb;
	}

	if (!sch->q.qlen)
		goto fin;
	q->now = ktime_get_ns();
	start_at = jiffies;

	next_event = q->now + 5LLU * NSEC_PER_SEC;

	for (level = 0; level < TC_HTB_MAXDEPTH; level++) {
		/* common case optimization - skip event handler quickly */
		int m;
		s64 event = q->near_ev_cache[level];

		if (q->now >= event) {
			event = htb_do_events(q, level, start_at);
			if (!event)
				event = q->now + NSEC_PER_SEC;
			q->near_ev_cache[level] = event;
		}

		if (next_event > event)
			next_event = event;

		m = ~q->row_mask[level];
		while (m != (int)(-1)) {
			int prio = ffz(m);

			m |= 1 << prio;
			skb = htb_dequeue_tree(q, prio, level);
			if (likely(skb != NULL))
				goto ok;
		}
	}
	if (likely(next_event > q->now))
		qdisc_watchdog_schedule_ns(&q->watchdog, next_event);
	else
		schedule_work(&q->work);
fin:
	return skb;
}

/* reset all classes */
/* always caled under BH & queue lock */
static void htb_reset(struct Qdisc *sch)
{
	struct htb_sched *q = qdisc_priv(sch);
	struct htb_class *cl;
	unsigned int i;

	for (i = 0; i < q->clhash.hashsize; i++) {
		hlist_for_each_entry(cl, &q->clhash.hash[i], common.hnode) {
			if (cl->level)
				memset(&cl->inner, 0, sizeof(cl->inner));
			else {
				if (cl->leaf.q && !q->offload)
					qdisc_reset(cl->leaf.q);
			}
			cl->prio_activity = 0;
			cl->cmode = HTB_CAN_SEND;
		}
	}
	qdisc_watchdog_cancel(&q->watchdog);
	__qdisc_reset_queue(&q->direct_queue);
	memset(q->hlevel, 0, sizeof(q->hlevel));
	memset(q->row_mask, 0, sizeof(q->row_mask));
}

static const struct nla_policy htb_policy[TCA_HTB_MAX + 1] = {
	[TCA_HTB_PARMS]	= { .len = sizeof(struct tc_htb_opt) },
	[TCA_HTB_INIT]	= { .len = sizeof(struct tc_htb_glob) },
	[TCA_HTB_CTAB]	= { .type = NLA_BINARY, .len = TC_RTAB_SIZE },
	[TCA_HTB_RTAB]	= { .type = NLA_BINARY, .len = TC_RTAB_SIZE },
	[TCA_HTB_DIRECT_QLEN] = { .type = NLA_U32 },
	[TCA_HTB_RATE64] = { .type = NLA_U64 },
	[TCA_HTB_CEIL64] = { .type = NLA_U64 },
	[TCA_HTB_OFFLOAD] = { .type = NLA_FLAG },
};

static void htb_work_func(struct work_struct *work)
{
	struct htb_sched *q = container_of(work, struct htb_sched, work);
	struct Qdisc *sch = q->watchdog.qdisc;

	rcu_read_lock();
	__netif_schedule(qdisc_root(sch));
	rcu_read_unlock();
}

static int htb_offload(struct net_device *dev, struct tc_htb_qopt_offload *opt)
{
	return dev->netdev_ops->ndo_setup_tc(dev, TC_SETUP_QDISC_HTB, opt);
}

static int htb_init(struct Qdisc *sch, struct nlattr *opt,
		    struct netlink_ext_ack *extack)
{
	struct net_device *dev = qdisc_dev(sch);
	struct tc_htb_qopt_offload offload_opt;
	struct htb_sched *q = qdisc_priv(sch);
	struct nlattr *tb[TCA_HTB_MAX + 1];
	struct tc_htb_glob *gopt;
	unsigned int ntx;
	bool offload;
	int err;

	qdisc_watchdog_init(&q->watchdog, sch);
	INIT_WORK(&q->work, htb_work_func);

	if (!opt)
		return -EINVAL;

	err = tcf_block_get(&q->block, &q->filter_list, sch, extack);
	if (err)
		return err;

	err = nla_parse_nested_deprecated(tb, TCA_HTB_MAX, opt, htb_policy,
					  NULL);
	if (err < 0)
		return err;

	if (!tb[TCA_HTB_INIT])
		return -EINVAL;

	gopt = nla_data(tb[TCA_HTB_INIT]);
	if (gopt->version != HTB_VER >> 16)
		return -EINVAL;

	offload = nla_get_flag(tb[TCA_HTB_OFFLOAD]);

	if (offload) {
		if (sch->parent != TC_H_ROOT) {
			NL_SET_ERR_MSG(extack, "HTB must be the root qdisc to use offload");
			return -EOPNOTSUPP;
		}

		if (!tc_can_offload(dev) || !dev->netdev_ops->ndo_setup_tc) {
			NL_SET_ERR_MSG(extack, "hw-tc-offload ethtool feature flag must be on");
			return -EOPNOTSUPP;
		}

		q->num_direct_qdiscs = dev->real_num_tx_queues;
		q->direct_qdiscs = kcalloc(q->num_direct_qdiscs,
					   sizeof(*q->direct_qdiscs),
					   GFP_KERNEL);
		if (!q->direct_qdiscs)
			return -ENOMEM;
	}

	err = qdisc_class_hash_init(&q->clhash);
	if (err < 0)
		return err;

	if (tb[TCA_HTB_DIRECT_QLEN])
		q->direct_qlen = nla_get_u32(tb[TCA_HTB_DIRECT_QLEN]);
	else
		q->direct_qlen = qdisc_dev(sch)->tx_queue_len;

	if ((q->rate2quantum = gopt->rate2quantum) < 1)
		q->rate2quantum = 1;
	q->defcls = gopt->defcls;

	if (!offload)
		return 0;

	for (ntx = 0; ntx < q->num_direct_qdiscs; ntx++) {
		struct netdev_queue *dev_queue = netdev_get_tx_queue(dev, ntx);
		struct Qdisc *qdisc;

		qdisc = qdisc_create_dflt(dev_queue, &pfifo_qdisc_ops,
					  TC_H_MAKE(sch->handle, 0), extack);
		if (!qdisc) {
			return -ENOMEM;
		}

		q->direct_qdiscs[ntx] = qdisc;
		qdisc->flags |= TCQ_F_ONETXQUEUE | TCQ_F_NOPARENT;
	}

	sch->flags |= TCQ_F_MQROOT;

	offload_opt = (struct tc_htb_qopt_offload) {
		.command = TC_HTB_CREATE,
		.parent_classid = TC_H_MAJ(sch->handle) >> 16,
		.classid = TC_H_MIN(q->defcls),
		.extack = extack,
	};
	err = htb_offload(dev, &offload_opt);
	if (err)
		return err;

	/* Defer this assignment, so that htb_destroy skips offload-related
	 * parts (especially calling ndo_setup_tc) on errors.
	 */
	q->offload = true;

	return 0;
}

static void htb_attach_offload(struct Qdisc *sch)
{
	struct net_device *dev = qdisc_dev(sch);
	struct htb_sched *q = qdisc_priv(sch);
	unsigned int ntx;

	for (ntx = 0; ntx < q->num_direct_qdiscs; ntx++) {
		struct Qdisc *old, *qdisc = q->direct_qdiscs[ntx];

		old = dev_graft_qdisc(qdisc->dev_queue, qdisc);
		qdisc_put(old);
		qdisc_hash_add(qdisc, false);
	}
	for (ntx = q->num_direct_qdiscs; ntx < dev->num_tx_queues; ntx++) {
		struct netdev_queue *dev_queue = netdev_get_tx_queue(dev, ntx);
		struct Qdisc *old = dev_graft_qdisc(dev_queue, NULL);

		qdisc_put(old);
	}

	kfree(q->direct_qdiscs);
	q->direct_qdiscs = NULL;
}

static void htb_attach_software(struct Qdisc *sch)
{
	struct net_device *dev = qdisc_dev(sch);
	unsigned int ntx;

	/* Resemble qdisc_graft behavior. */
	for (ntx = 0; ntx < dev->num_tx_queues; ntx++) {
		struct netdev_queue *dev_queue = netdev_get_tx_queue(dev, ntx);
		struct Qdisc *old = dev_graft_qdisc(dev_queue, sch);

		qdisc_refcount_inc(sch);

		qdisc_put(old);
	}
}

static void htb_attach(struct Qdisc *sch)
{
	struct htb_sched *q = qdisc_priv(sch);

	if (q->offload)
		htb_attach_offload(sch);
	else
		htb_attach_software(sch);
}

static int htb_dump(struct Qdisc *sch, struct sk_buff *skb)
{
	struct htb_sched *q = qdisc_priv(sch);
	struct nlattr *nest;
	struct tc_htb_glob gopt;

	if (q->offload)
		sch->flags |= TCQ_F_OFFLOADED;
	else
		sch->flags &= ~TCQ_F_OFFLOADED;

	sch->qstats.overlimits = q->overlimits;
	/* Its safe to not acquire qdisc lock. As we hold RTNL,
	 * no change can happen on the qdisc parameters.
	 */

	gopt.direct_pkts = q->direct_pkts;
	gopt.version = HTB_VER;
	gopt.rate2quantum = q->rate2quantum;
	gopt.defcls = q->defcls;
	gopt.debug = 0;

	nest = nla_nest_start_noflag(skb, TCA_OPTIONS);
	if (nest == NULL)
		goto nla_put_failure;
	if (nla_put(skb, TCA_HTB_INIT, sizeof(gopt), &gopt) ||
	    nla_put_u32(skb, TCA_HTB_DIRECT_QLEN, q->direct_qlen))
		goto nla_put_failure;
	if (q->offload && nla_put_flag(skb, TCA_HTB_OFFLOAD))
		goto nla_put_failure;

	return nla_nest_end(skb, nest);

nla_put_failure:
	nla_nest_cancel(skb, nest);
	return -1;
}

static int htb_dump_class(struct Qdisc *sch, unsigned long arg,
			  struct sk_buff *skb, struct tcmsg *tcm)
{
	struct htb_class *cl = (struct htb_class *)arg;
	struct htb_sched *q = qdisc_priv(sch);
	struct nlattr *nest;
	struct tc_htb_opt opt;

	/* Its safe to not acquire qdisc lock. As we hold RTNL,
	 * no change can happen on the class parameters.
	 */
	tcm->tcm_parent = cl->parent ? cl->parent->common.classid : TC_H_ROOT;
	tcm->tcm_handle = cl->common.classid;
	if (!cl->level && cl->leaf.q)
		tcm->tcm_info = cl->leaf.q->handle;

	nest = nla_nest_start_noflag(skb, TCA_OPTIONS);
	if (nest == NULL)
		goto nla_put_failure;

	memset(&opt, 0, sizeof(opt));

	psched_ratecfg_getrate(&opt.rate, &cl->rate);
	opt.buffer = PSCHED_NS2TICKS(cl->buffer);
	psched_ratecfg_getrate(&opt.ceil, &cl->ceil);
	opt.cbuffer = PSCHED_NS2TICKS(cl->cbuffer);
	opt.quantum = cl->quantum;
	opt.prio = cl->prio;
	opt.level = cl->level;
	if (nla_put(skb, TCA_HTB_PARMS, sizeof(opt), &opt))
		goto nla_put_failure;
	if (q->offload && nla_put_flag(skb, TCA_HTB_OFFLOAD))
		goto nla_put_failure;
	if ((cl->rate.rate_bytes_ps >= (1ULL << 32)) &&
	    nla_put_u64_64bit(skb, TCA_HTB_RATE64, cl->rate.rate_bytes_ps,
			      TCA_HTB_PAD))
		goto nla_put_failure;
	if ((cl->ceil.rate_bytes_ps >= (1ULL << 32)) &&
	    nla_put_u64_64bit(skb, TCA_HTB_CEIL64, cl->ceil.rate_bytes_ps,
			      TCA_HTB_PAD))
		goto nla_put_failure;

	return nla_nest_end(skb, nest);

nla_put_failure:
	nla_nest_cancel(skb, nest);
	return -1;
}

static void htb_offload_aggregate_stats(struct htb_sched *q,
					struct htb_class *cl)
{
	u64 bytes = 0, packets = 0;
	struct htb_class *c;
	unsigned int i;

	gnet_stats_basic_sync_init(&cl->bstats);

	for (i = 0; i < q->clhash.hashsize; i++) {
		hlist_for_each_entry(c, &q->clhash.hash[i], common.hnode) {
			struct htb_class *p = c;

			while (p && p->level < cl->level)
				p = p->parent;

			if (p != cl)
				continue;

			bytes += u64_stats_read(&c->bstats_bias.bytes);
			packets += u64_stats_read(&c->bstats_bias.packets);
			if (c->level == 0) {
				bytes += u64_stats_read(&c->leaf.q->bstats.bytes);
				packets += u64_stats_read(&c->leaf.q->bstats.packets);
			}
		}
	}
	_bstats_update(&cl->bstats, bytes, packets);
}

static int
htb_dump_class_stats(struct Qdisc *sch, unsigned long arg, struct gnet_dump *d)
{
	struct htb_class *cl = (struct htb_class *)arg;
	struct htb_sched *q = qdisc_priv(sch);
	struct gnet_stats_queue qs = {
		.drops = cl->drops,
		.overlimits = cl->overlimits,
	};
	__u32 qlen = 0;

	if (!cl->level && cl->leaf.q)
		qdisc_qstats_qlen_backlog(cl->leaf.q, &qlen, &qs.backlog);

	cl->xstats.tokens = clamp_t(s64, PSCHED_NS2TICKS(cl->tokens),
				    INT_MIN, INT_MAX);
	cl->xstats.ctokens = clamp_t(s64, PSCHED_NS2TICKS(cl->ctokens),
				     INT_MIN, INT_MAX);

	if (q->offload) {
		if (!cl->level) {
			if (cl->leaf.q)
				cl->bstats = cl->leaf.q->bstats;
			else
				gnet_stats_basic_sync_init(&cl->bstats);
			_bstats_update(&cl->bstats,
				       u64_stats_read(&cl->bstats_bias.bytes),
				       u64_stats_read(&cl->bstats_bias.packets));
		} else {
			htb_offload_aggregate_stats(q, cl);
		}
	}

	if (gnet_stats_copy_basic(d, NULL, &cl->bstats, true) < 0 ||
	    gnet_stats_copy_rate_est(d, &cl->rate_est) < 0 ||
	    gnet_stats_copy_queue(d, NULL, &qs, qlen) < 0)
		return -1;

	return gnet_stats_copy_app(d, &cl->xstats, sizeof(cl->xstats));
}

static struct netdev_queue *
htb_select_queue(struct Qdisc *sch, struct tcmsg *tcm)
{
	struct net_device *dev = qdisc_dev(sch);
	struct tc_htb_qopt_offload offload_opt;
	struct htb_sched *q = qdisc_priv(sch);
	int err;

	if (!q->offload)
		return sch->dev_queue;

	offload_opt = (struct tc_htb_qopt_offload) {
		.command = TC_HTB_LEAF_QUERY_QUEUE,
		.classid = TC_H_MIN(tcm->tcm_parent),
	};
	err = htb_offload(dev, &offload_opt);
	if (err || offload_opt.qid >= dev->num_tx_queues)
		return NULL;
	return netdev_get_tx_queue(dev, offload_opt.qid);
}

static struct Qdisc *
htb_graft_helper(struct netdev_queue *dev_queue, struct Qdisc *new_q)
{
	struct net_device *dev = dev_queue->dev;
	struct Qdisc *old_q;

	if (dev->flags & IFF_UP)
		dev_deactivate(dev);
	old_q = dev_graft_qdisc(dev_queue, new_q);
	if (new_q)
		new_q->flags |= TCQ_F_ONETXQUEUE | TCQ_F_NOPARENT;
	if (dev->flags & IFF_UP)
		dev_activate(dev);

	return old_q;
}

static struct netdev_queue *htb_offload_get_queue(struct htb_class *cl)
{
	struct netdev_queue *queue;

	queue = cl->leaf.offload_queue;
	if (!(cl->leaf.q->flags & TCQ_F_BUILTIN))
		WARN_ON(cl->leaf.q->dev_queue != queue);

	return queue;
}

static void htb_offload_move_qdisc(struct Qdisc *sch, struct htb_class *cl_old,
				   struct htb_class *cl_new, bool destroying)
{
	struct netdev_queue *queue_old, *queue_new;
	struct net_device *dev = qdisc_dev(sch);

	queue_old = htb_offload_get_queue(cl_old);
	queue_new = htb_offload_get_queue(cl_new);

	if (!destroying) {
		struct Qdisc *qdisc;

		if (dev->flags & IFF_UP)
			dev_deactivate(dev);
		qdisc = dev_graft_qdisc(queue_old, NULL);
		WARN_ON(qdisc != cl_old->leaf.q);
	}

	if (!(cl_old->leaf.q->flags & TCQ_F_BUILTIN))
		cl_old->leaf.q->dev_queue = queue_new;
	cl_old->leaf.offload_queue = queue_new;

	if (!destroying) {
		struct Qdisc *qdisc;

		qdisc = dev_graft_qdisc(queue_new, cl_old->leaf.q);
		if (dev->flags & IFF_UP)
			dev_activate(dev);
		WARN_ON(!(qdisc->flags & TCQ_F_BUILTIN));
	}
}

static int htb_graft(struct Qdisc *sch, unsigned long arg, struct Qdisc *new,
		     struct Qdisc **old, struct netlink_ext_ack *extack)
{
	struct netdev_queue *dev_queue = sch->dev_queue;
	struct htb_class *cl = (struct htb_class *)arg;
	struct htb_sched *q = qdisc_priv(sch);
	struct Qdisc *old_q;

	if (cl->level)
		return -EINVAL;

	if (q->offload)
		dev_queue = htb_offload_get_queue(cl);

	if (!new) {
		new = qdisc_create_dflt(dev_queue, &pfifo_qdisc_ops,
					cl->common.classid, extack);
		if (!new)
			return -ENOBUFS;
	}

	if (q->offload) {
		/* One ref for cl->leaf.q, the other for dev_queue->qdisc. */
		qdisc_refcount_inc(new);
		old_q = htb_graft_helper(dev_queue, new);
	}

	*old = qdisc_replace(sch, new, &cl->leaf.q);

	if (q->offload) {
		WARN_ON(old_q != *old);
		qdisc_put(old_q);
	}

	return 0;
}

static struct Qdisc *htb_leaf(struct Qdisc *sch, unsigned long arg)
{
	struct htb_class *cl = (struct htb_class *)arg;
	return !cl->level ? cl->leaf.q : NULL;
}

static void htb_qlen_notify(struct Qdisc *sch, unsigned long arg)
{
	struct htb_class *cl = (struct htb_class *)arg;

	if (!cl->prio_activity)
		return;
	htb_deactivate(qdisc_priv(sch), cl);
}

static inline int htb_parent_last_child(struct htb_class *cl)
{
	if (!cl->parent)
		/* the root class */
		return 0;
	if (cl->parent->children > 1)
		/* not the last child */
		return 0;
	return 1;
}

static void htb_parent_to_leaf(struct Qdisc *sch, struct htb_class *cl,
			       struct Qdisc *new_q)
{
	struct htb_sched *q = qdisc_priv(sch);
	struct htb_class *parent = cl->parent;

	WARN_ON(cl->level || !cl->leaf.q || cl->prio_activity);

	if (parent->cmode != HTB_CAN_SEND)
		htb_safe_rb_erase(&parent->pq_node,
				  &q->hlevel[parent->level].wait_pq);

	parent->level = 0;
	memset(&parent->inner, 0, sizeof(parent->inner));
	parent->leaf.q = new_q ? new_q : &noop_qdisc;
	parent->tokens = parent->buffer;
	parent->ctokens = parent->cbuffer;
	parent->t_c = ktime_get_ns();
	parent->cmode = HTB_CAN_SEND;
	if (q->offload)
		parent->leaf.offload_queue = cl->leaf.offload_queue;
}

static void htb_parent_to_leaf_offload(struct Qdisc *sch,
				       struct netdev_queue *dev_queue,
				       struct Qdisc *new_q)
{
	struct Qdisc *old_q;

	/* One ref for cl->leaf.q, the other for dev_queue->qdisc. */
	if (new_q)
		qdisc_refcount_inc(new_q);
	old_q = htb_graft_helper(dev_queue, new_q);
	WARN_ON(!(old_q->flags & TCQ_F_BUILTIN));
}

static int htb_destroy_class_offload(struct Qdisc *sch, struct htb_class *cl,
				     bool last_child, bool destroying,
				     struct netlink_ext_ack *extack)
{
	struct tc_htb_qopt_offload offload_opt;
	struct netdev_queue *dev_queue;
	struct Qdisc *q = cl->leaf.q;
	struct Qdisc *old;
	int err;

	if (cl->level)
		return -EINVAL;

	WARN_ON(!q);
	dev_queue = htb_offload_get_queue(cl);
	/* When destroying, caller qdisc_graft grafts the new qdisc and invokes
	 * qdisc_put for the qdisc being destroyed. htb_destroy_class_offload
	 * does not need to graft or qdisc_put the qdisc being destroyed.
	 */
	if (!destroying) {
		old = htb_graft_helper(dev_queue, NULL);
		/* Last qdisc grafted should be the same as cl->leaf.q when
		 * calling htb_delete.
		 */
		WARN_ON(old != q);
	}

	if (cl->parent) {
		_bstats_update(&cl->parent->bstats_bias,
			       u64_stats_read(&q->bstats.bytes),
			       u64_stats_read(&q->bstats.packets));
	}

	offload_opt = (struct tc_htb_qopt_offload) {
		.command = !last_child ? TC_HTB_LEAF_DEL :
			   destroying ? TC_HTB_LEAF_DEL_LAST_FORCE :
			   TC_HTB_LEAF_DEL_LAST,
		.classid = cl->common.classid,
		.extack = extack,
	};
	err = htb_offload(qdisc_dev(sch), &offload_opt);

	if (!destroying) {
		if (!err)
			qdisc_put(old);
		else
			htb_graft_helper(dev_queue, old);
	}

	if (last_child)
		return err;

	if (!err && offload_opt.classid != TC_H_MIN(cl->common.classid)) {
		u32 classid = TC_H_MAJ(sch->handle) |
			      TC_H_MIN(offload_opt.classid);
		struct htb_class *moved_cl = htb_find(classid, sch);

		htb_offload_move_qdisc(sch, moved_cl, cl, destroying);
	}

	return err;
}

static void htb_destroy_class(struct Qdisc *sch, struct htb_class *cl)
{
	if (!cl->level) {
		WARN_ON(!cl->leaf.q);
		qdisc_put(cl->leaf.q);
	}
	gen_kill_estimator(&cl->rate_est);
	tcf_block_put(cl->block);
	kfree(cl);
}

static void htb_destroy(struct Qdisc *sch)
{
	struct net_device *dev = qdisc_dev(sch);
	struct tc_htb_qopt_offload offload_opt;
	struct htb_sched *q = qdisc_priv(sch);
	struct hlist_node *next;
	bool nonempty, changed;
	struct htb_class *cl;
	unsigned int i;

	cancel_work_sync(&q->work);
	qdisc_watchdog_cancel(&q->watchdog);
	/* This line used to be after htb_destroy_class call below
	 * and surprisingly it worked in 2.4. But it must precede it
	 * because filter need its target class alive to be able to call
	 * unbind_filter on it (without Oops).
	 */
	tcf_block_put(q->block);

	for (i = 0; i < q->clhash.hashsize; i++) {
		hlist_for_each_entry(cl, &q->clhash.hash[i], common.hnode) {
			tcf_block_put(cl->block);
			cl->block = NULL;
		}
	}

	do {
		nonempty = false;
		changed = false;
		for (i = 0; i < q->clhash.hashsize; i++) {
			hlist_for_each_entry_safe(cl, next, &q->clhash.hash[i],
						  common.hnode) {
				bool last_child;

				if (!q->offload) {
					htb_destroy_class(sch, cl);
					continue;
				}

				nonempty = true;

				if (cl->level)
					continue;

				changed = true;

				last_child = htb_parent_last_child(cl);
				htb_destroy_class_offload(sch, cl, last_child,
							  true, NULL);
				qdisc_class_hash_remove(&q->clhash,
							&cl->common);
				if (cl->parent)
					cl->parent->children--;
				if (last_child)
					htb_parent_to_leaf(sch, cl, NULL);
				htb_destroy_class(sch, cl);
			}
		}
	} while (changed);
	WARN_ON(nonempty);

	qdisc_class_hash_destroy(&q->clhash);
	__qdisc_reset_queue(&q->direct_queue);

	if (q->offload) {
		offload_opt = (struct tc_htb_qopt_offload) {
			.command = TC_HTB_DESTROY,
		};
		htb_offload(dev, &offload_opt);
	}

	if (!q->direct_qdiscs)
		return;
	for (i = 0; i < q->num_direct_qdiscs && q->direct_qdiscs[i]; i++)
		qdisc_put(q->direct_qdiscs[i]);
	kfree(q->direct_qdiscs);
}

static int htb_delete(struct Qdisc *sch, unsigned long arg,
		      struct netlink_ext_ack *extack)
{
	struct htb_sched *q = qdisc_priv(sch);
	struct htb_class *cl = (struct htb_class *)arg;
	struct Qdisc *new_q = NULL;
	int last_child = 0;
	int err;

	/* TODO: why don't allow to delete subtree ? references ? does
	 * tc subsys guarantee us that in htb_destroy it holds no class
	 * refs so that we can remove children safely there ?
	 */
	if (cl->children || qdisc_class_in_use(&cl->common)) {
		NL_SET_ERR_MSG(extack, "HTB class in use");
		return -EBUSY;
	}

	if (!cl->level && htb_parent_last_child(cl))
		last_child = 1;

	if (q->offload) {
		err = htb_destroy_class_offload(sch, cl, last_child, false,
						extack);
		if (err)
			return err;
	}

	if (last_child) {
		struct netdev_queue *dev_queue = sch->dev_queue;

		if (q->offload)
			dev_queue = htb_offload_get_queue(cl);

		new_q = qdisc_create_dflt(dev_queue, &pfifo_qdisc_ops,
					  cl->parent->common.classid,
					  NULL);
		if (q->offload)
			htb_parent_to_leaf_offload(sch, dev_queue, new_q);
	}

	sch_tree_lock(sch);

	if (!cl->level)
		qdisc_purge_queue(cl->leaf.q);

	/* delete from hash and active; remainder in destroy_class */
	qdisc_class_hash_remove(&q->clhash, &cl->common);
	if (cl->parent)
		cl->parent->children--;

	if (cl->prio_activity)
		htb_deactivate(q, cl);

	if (cl->cmode != HTB_CAN_SEND)
		htb_safe_rb_erase(&cl->pq_node,
				  &q->hlevel[cl->level].wait_pq);

	if (last_child)
		htb_parent_to_leaf(sch, cl, new_q);

	sch_tree_unlock(sch);

	htb_destroy_class(sch, cl);
	return 0;
}

static int htb_change_class(struct Qdisc *sch, u32 classid,
			    u32 parentid, struct nlattr **tca,
			    unsigned long *arg, struct netlink_ext_ack *extack)
{
	int err = -EINVAL;
	struct htb_sched *q = qdisc_priv(sch);
	struct htb_class *cl = (struct htb_class *)*arg, *parent;
	struct tc_htb_qopt_offload offload_opt;
	struct nlattr *opt = tca[TCA_OPTIONS];
	struct nlattr *tb[TCA_HTB_MAX + 1];
	struct Qdisc *parent_qdisc = NULL;
	struct netdev_queue *dev_queue;
	struct tc_htb_opt *hopt;
	u64 rate64, ceil64;
	int warn = 0;

	/* extract all subattrs from opt attr */
	if (!opt)
		goto failure;

	err = nla_parse_nested_deprecated(tb, TCA_HTB_MAX, opt, htb_policy,
					  extack);
	if (err < 0)
		goto failure;

	err = -EINVAL;
	if (tb[TCA_HTB_PARMS] == NULL)
		goto failure;

	parent = parentid == TC_H_ROOT ? NULL : htb_find(parentid, sch);

	hopt = nla_data(tb[TCA_HTB_PARMS]);
	if (!hopt->rate.rate || !hopt->ceil.rate)
		goto failure;

	if (q->offload) {
		/* Options not supported by the offload. */
		if (hopt->rate.overhead || hopt->ceil.overhead) {
			NL_SET_ERR_MSG(extack, "HTB offload doesn't support the overhead parameter");
			goto failure;
		}
		if (hopt->rate.mpu || hopt->ceil.mpu) {
			NL_SET_ERR_MSG(extack, "HTB offload doesn't support the mpu parameter");
			goto failure;
		}
	}

	/* Keeping backward compatible with rate_table based iproute2 tc */
	if (hopt->rate.linklayer == TC_LINKLAYER_UNAWARE)
		qdisc_put_rtab(qdisc_get_rtab(&hopt->rate, tb[TCA_HTB_RTAB],
					      NULL));

	if (hopt->ceil.linklayer == TC_LINKLAYER_UNAWARE)
		qdisc_put_rtab(qdisc_get_rtab(&hopt->ceil, tb[TCA_HTB_CTAB],
					      NULL));

	rate64 = nla_get_u64_default(tb[TCA_HTB_RATE64], 0);
	ceil64 = nla_get_u64_default(tb[TCA_HTB_CEIL64], 0);

	if (!cl) {		/* new class */
		struct net_device *dev = qdisc_dev(sch);
		struct Qdisc *new_q, *old_q;
		int prio;
		struct {
			struct nlattr		nla;
			struct gnet_estimator	opt;
		} est = {
			.nla = {
				.nla_len	= nla_attr_size(sizeof(est.opt)),
				.nla_type	= TCA_RATE,
			},
			.opt = {
				/* 4s interval, 16s averaging constant */
				.interval	= 2,
				.ewma_log	= 2,
			},
		};

		/* check for valid classid */
		if (!classid || TC_H_MAJ(classid ^ sch->handle) ||
		    htb_find(classid, sch))
			goto failure;

		/* check maximal depth */
		if (parent && parent->parent && parent->parent->level < 2) {
			NL_SET_ERR_MSG_MOD(extack, "tree is too deep");
			goto failure;
		}
		err = -ENOBUFS;
		cl = kzalloc(sizeof(*cl), GFP_KERNEL);
		if (!cl)
			goto failure;

		gnet_stats_basic_sync_init(&cl->bstats);
		gnet_stats_basic_sync_init(&cl->bstats_bias);

		err = tcf_block_get(&cl->block, &cl->filter_list, sch, extack);
		if (err) {
			kfree(cl);
			goto failure;
		}
		if (htb_rate_est || tca[TCA_RATE]) {
			err = gen_new_estimator(&cl->bstats, NULL,
						&cl->rate_est,
						NULL,
						true,
						tca[TCA_RATE] ? : &est.nla);
			if (err)
				goto err_block_put;
		}

		cl->children = 0;
		RB_CLEAR_NODE(&cl->pq_node);

		for (prio = 0; prio < TC_HTB_NUMPRIO; prio++)
			RB_CLEAR_NODE(&cl->node[prio]);

		cl->common.classid = classid;

		/* Make sure nothing interrupts us in between of two
		 * ndo_setup_tc calls.
		 */
		ASSERT_RTNL();

		/* create leaf qdisc early because it uses kmalloc(GFP_KERNEL)
		 * so that can't be used inside of sch_tree_lock
		 * -- thanks to Karlis Peisenieks
		 */
		if (!q->offload) {
			dev_queue = sch->dev_queue;
		} else if (!(parent && !parent->level)) {
			/* Assign a dev_queue to this classid. */
			offload_opt = (struct tc_htb_qopt_offload) {
				.command = TC_HTB_LEAF_ALLOC_QUEUE,
				.classid = cl->common.classid,
				.parent_classid = parent ?
					TC_H_MIN(parent->common.classid) :
					TC_HTB_CLASSID_ROOT,
				.rate = max_t(u64, hopt->rate.rate, rate64),
				.ceil = max_t(u64, hopt->ceil.rate, ceil64),
				.prio = hopt->prio,
				.quantum = hopt->quantum,
				.extack = extack,
			};
			err = htb_offload(dev, &offload_opt);
			if (err) {
				NL_SET_ERR_MSG_WEAK(extack,
						    "Failed to offload TC_HTB_LEAF_ALLOC_QUEUE");
				goto err_kill_estimator;
			}
			dev_queue = netdev_get_tx_queue(dev, offload_opt.qid);
		} else { /* First child. */
			dev_queue = htb_offload_get_queue(parent);
			old_q = htb_graft_helper(dev_queue, NULL);
			WARN_ON(old_q != parent->leaf.q);
			offload_opt = (struct tc_htb_qopt_offload) {
				.command = TC_HTB_LEAF_TO_INNER,
				.classid = cl->common.classid,
				.parent_classid =
					TC_H_MIN(parent->common.classid),
				.rate = max_t(u64, hopt->rate.rate, rate64),
				.ceil = max_t(u64, hopt->ceil.rate, ceil64),
				.prio = hopt->prio,
				.quantum = hopt->quantum,
				.extack = extack,
			};
			err = htb_offload(dev, &offload_opt);
			if (err) {
				NL_SET_ERR_MSG_WEAK(extack,
						    "Failed to offload TC_HTB_LEAF_TO_INNER");
				htb_graft_helper(dev_queue, old_q);
				goto err_kill_estimator;
			}
			_bstats_update(&parent->bstats_bias,
				       u64_stats_read(&old_q->bstats.bytes),
				       u64_stats_read(&old_q->bstats.packets));
			qdisc_put(old_q);
		}
		new_q = qdisc_create_dflt(dev_queue, &pfifo_qdisc_ops,
					  classid, NULL);
		if (q->offload) {
			/* One ref for cl->leaf.q, the other for dev_queue->qdisc. */
			if (new_q)
				qdisc_refcount_inc(new_q);
			old_q = htb_graft_helper(dev_queue, new_q);
			/* No qdisc_put needed. */
			WARN_ON(!(old_q->flags & TCQ_F_BUILTIN));
		}
		sch_tree_lock(sch);
		if (parent && !parent->level) {
			/* turn parent into inner node */
			qdisc_purge_queue(parent->leaf.q);
			parent_qdisc = parent->leaf.q;
			if (parent->prio_activity)
				htb_deactivate(q, parent);

			/* remove from evt list because of level change */
			if (parent->cmode != HTB_CAN_SEND) {
				htb_safe_rb_erase(&parent->pq_node, &q->hlevel[0].wait_pq);
				parent->cmode = HTB_CAN_SEND;
			}
			parent->level = (parent->parent ? parent->parent->level
					 : TC_HTB_MAXDEPTH) - 1;
			memset(&parent->inner, 0, sizeof(parent->inner));
		}

		/* leaf (we) needs elementary qdisc */
		cl->leaf.q = new_q ? new_q : &noop_qdisc;
		if (q->offload)
			cl->leaf.offload_queue = dev_queue;

		cl->parent = parent;

		/* set class to be in HTB_CAN_SEND state */
		cl->tokens = PSCHED_TICKS2NS(hopt->buffer);
		cl->ctokens = PSCHED_TICKS2NS(hopt->cbuffer);
		cl->mbuffer = 60ULL * NSEC_PER_SEC;	/* 1min */
		cl->t_c = ktime_get_ns();
		cl->cmode = HTB_CAN_SEND;

		/* attach to the hash list and parent's family */
		qdisc_class_hash_insert(&q->clhash, &cl->common);
		if (parent)
			parent->children++;
		if (cl->leaf.q != &noop_qdisc)
			qdisc_hash_add(cl->leaf.q, true);
	} else {
		if (tca[TCA_RATE]) {
			err = gen_replace_estimator(&cl->bstats, NULL,
						    &cl->rate_est,
						    NULL,
						    true,
						    tca[TCA_RATE]);
			if (err)
				return err;
		}

		if (q->offload) {
			struct net_device *dev = qdisc_dev(sch);

			offload_opt = (struct tc_htb_qopt_offload) {
				.command = TC_HTB_NODE_MODIFY,
				.classid = cl->common.classid,
				.rate = max_t(u64, hopt->rate.rate, rate64),
				.ceil = max_t(u64, hopt->ceil.rate, ceil64),
				.prio = hopt->prio,
				.quantum = hopt->quantum,
				.extack = extack,
			};
			err = htb_offload(dev, &offload_opt);
			if (err)
				/* Estimator was replaced, and rollback may fail
				 * as well, so we don't try to recover it, and
				 * the estimator won't work property with the
				 * offload anyway, because bstats are updated
				 * only when the stats are queried.
				 */
				return err;
		}

		sch_tree_lock(sch);
	}

	psched_ratecfg_precompute(&cl->rate, &hopt->rate, rate64);
	psched_ratecfg_precompute(&cl->ceil, &hopt->ceil, ceil64);

	/* it used to be a nasty bug here, we have to check that node
	 * is really leaf before changing cl->leaf !
	 */
	if (!cl->level) {
		u64 quantum = cl->rate.rate_bytes_ps;

		do_div(quantum, q->rate2quantum);
		cl->quantum = min_t(u64, quantum, INT_MAX);

		if (!hopt->quantum && cl->quantum < 1000) {
			warn = -1;
			cl->quantum = 1000;
		}
		if (!hopt->quantum && cl->quantum > 200000) {
			warn = 1;
			cl->quantum = 200000;
		}
		if (hopt->quantum)
			cl->quantum = hopt->quantum;
		if ((cl->prio = hopt->prio) >= TC_HTB_NUMPRIO)
			cl->prio = TC_HTB_NUMPRIO - 1;
	}

	cl->buffer = PSCHED_TICKS2NS(hopt->buffer);
	cl->cbuffer = PSCHED_TICKS2NS(hopt->cbuffer);

	sch_tree_unlock(sch);
	qdisc_put(parent_qdisc);

	if (warn)
		NL_SET_ERR_MSG_FMT_MOD(extack,
				       "quantum of class %X is %s. Consider r2q change.",
				       cl->common.classid, (warn == -1 ? "small" : "big"));

	qdisc_class_hash_grow(sch, &q->clhash);

	*arg = (unsigned long)cl;
	return 0;

err_kill_estimator:
	gen_kill_estimator(&cl->rate_est);
err_block_put:
	tcf_block_put(cl->block);
	kfree(cl);
failure:
	return err;
}

static struct tcf_block *htb_tcf_block(struct Qdisc *sch, unsigned long arg,
				       struct netlink_ext_ack *extack)
{
	struct htb_sched *q = qdisc_priv(sch);
	struct htb_class *cl = (struct htb_class *)arg;

	return cl ? cl->block : q->block;
}

static unsigned long htb_bind_filter(struct Qdisc *sch, unsigned long parent,
				     u32 classid)
{
	struct htb_class *cl = htb_find(classid, sch);

	/*if (cl && !cl->level) return 0;
	 * The line above used to be there to prevent attaching filters to
	 * leaves. But at least tc_index filter uses this just to get class
	 * for other reasons so that we have to allow for it.
	 * ----
	 * 19.6.2002 As Werner explained it is ok - bind filter is just
	 * another way to "lock" the class - unlike "get" this lock can
	 * be broken by class during destroy IIUC.
	 */
	if (cl)
		qdisc_class_get(&cl->common);
	return (unsigned long)cl;
}

static void htb_unbind_filter(struct Qdisc *sch, unsigned long arg)
{
	struct htb_class *cl = (struct htb_class *)arg;

	qdisc_class_put(&cl->common);
}

static void htb_walk(struct Qdisc *sch, struct qdisc_walker *arg)
{
	struct htb_sched *q = qdisc_priv(sch);
	struct htb_class *cl;
	unsigned int i;

	if (arg->stop)
		return;

	for (i = 0; i < q->clhash.hashsize; i++) {
		hlist_for_each_entry(cl, &q->clhash.hash[i], common.hnode) {
			if (!tc_qdisc_stats_dump(sch, (unsigned long)cl, arg))
				return;
		}
	}
}

static const struct Qdisc_class_ops htb_class_ops = {
	.select_queue	=	htb_select_queue,
	.graft		=	htb_graft,
	.leaf		=	htb_leaf,
	.qlen_notify	=	htb_qlen_notify,
	.find		=	htb_search,
	.change		=	htb_change_class,
	.delete		=	htb_delete,
	.walk		=	htb_walk,
	.tcf_block	=	htb_tcf_block,
	.bind_tcf	=	htb_bind_filter,
	.unbind_tcf	=	htb_unbind_filter,
	.dump		=	htb_dump_class,
	.dump_stats	=	htb_dump_class_stats,
};

static struct Qdisc_ops htb_qdisc_ops __read_mostly = {
	.cl_ops		=	&htb_class_ops,
	.id		=	"htb",
	.priv_size	=	sizeof(struct htb_sched),
	.enqueue	=	htb_enqueue,
	.dequeue	=	htb_dequeue,
	.peek		=	qdisc_peek_dequeued,
	.init		=	htb_init,
	.attach		=	htb_attach,
	.reset		=	htb_reset,
	.destroy	=	htb_destroy,
	.dump		=	htb_dump,
	.owner		=	THIS_MODULE,
};
MODULE_ALIAS_NET_SCH("htb");

static int __init htb_module_init(void)
{
	return register_qdisc(&htb_qdisc_ops);
}
static void __exit htb_module_exit(void)
{
	unregister_qdisc(&htb_qdisc_ops);
}

module_init(htb_module_init)
module_exit(htb_module_exit)
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Hierarchical Token Bucket scheduler");
