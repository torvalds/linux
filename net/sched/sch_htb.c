/*
 * net/sched/sch_htb.c	Hierarchical token bucket, feed tree version
 *
 *		This program is free software; you can redistribute it and/or
 *		modify it under the terms of the GNU General Public License
 *		as published by the Free Software Foundation; either version
 *		2 of the License, or (at your option) any later version.
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
#define HTB_VER 0x30011		/* major must be matched with number suplied by TC as version */

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
	int			filter_cnt;
	int			refcnt;		/* usage count of this class */

	int			level;		/* our level (see above) */
	unsigned int		children;
	struct htb_class	*parent;	/* parent class */

	struct gnet_stats_rate_est64 rate_est;

	/*
	 * Written often fields
	 */
	struct gnet_stats_basic_packed bstats;
	struct gnet_stats_queue	qstats;
	struct tc_htb_xstats	xstats;	/* our special stats */

	/* token bucket parameters */
	s64			tokens, ctokens;/* current number of tokens */
	s64			t_c;		/* checkpoint time */

	union {
		struct htb_class_leaf {
			struct list_head drop_list;
			int		deficit[TC_HTB_MAXDEPTH];
			struct Qdisc	*q;
		} leaf;
		struct htb_class_inner {
			struct htb_prio clprio[TC_HTB_NUMPRIO];
		} inner;
	} un;
	s64			pq_key;

	int			prio_activity;	/* for which prios are we active */
	enum htb_cmode		cmode;		/* current mode of the class */
	struct rb_node		pq_node;	/* node for event queue */
	struct rb_node		node[TC_HTB_NUMPRIO];	/* node for self or feed tree */
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

#define HTB_WARN_TOOMANYEVENTS	0x1
	unsigned int		warned;	/* only one warning */
	int			direct_qlen;
	struct work_struct	work;

	/* non shaped skbs; let them go directly thru */
	struct sk_buff_head	direct_queue;
	long			direct_pkts;

	struct qdisc_watchdog	watchdog;

	s64			now;	/* cached dequeue time */
	struct list_head	drops[TC_HTB_NUMPRIO];/* active leaves (for drops) */

	/* time of nearest event per level (row) */
	s64			near_ev_cache[TC_HTB_MAXDEPTH];

	int			row_mask[TC_HTB_MAXDEPTH];

	struct htb_level	hlevel[TC_HTB_MAXDEPTH];
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

/**
 * htb_classify - classify a packet into class
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
#define HTB_DIRECT ((struct htb_class *)-1L)

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
	while (tcf && (result = tc_classify(skb, tcf, &res, false)) >= 0) {
#ifdef CONFIG_NET_CLS_ACT
		switch (result) {
		case TC_ACT_QUEUED:
		case TC_ACT_STOLEN:
			*qerr = NET_XMIT_SUCCESS | __NET_XMIT_STOLEN;
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
			int prio = ffz(~m);
			m &= ~(1 << prio);

			if (p->un.inner.clprio[prio].feed.rb_node)
				/* parent already has its feed in use so that
				 * reset bit in mask as parent is already ok
				 */
				mask &= ~(1 << prio);

			htb_add_to_id_tree(&p->un.inner.clprio[prio].feed, cl, prio);
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

			if (p->un.inner.clprio[prio].ptr == cl->node + prio) {
				/* we are removing child which is pointed to from
				 * parent feed - forget the pointer but remember
				 * classid
				 */
				p->un.inner.clprio[prio].last_ptr_id = cl->common.classid;
				p->un.inner.clprio[prio].ptr = NULL;
			}

			htb_safe_rb_erase(cl->node + prio,
					  &p->un.inner.clprio[prio].feed);

			if (!p->un.inner.clprio[prio].feed.rb_node)
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
 *
 * This should be the only way how to change classe's mode under normal
 * cirsumstances. Routine will update feed lists linkage, change mode
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
 *
 * Routine learns (new) priority of leaf and activates feed chain
 * for the prio. It can be called on already active leaf safely.
 * It also adds leaf into droplist.
 */
static inline void htb_activate(struct htb_sched *q, struct htb_class *cl)
{
	WARN_ON(cl->level || !cl->un.leaf.q || !cl->un.leaf.q->q.qlen);

	if (!cl->prio_activity) {
		cl->prio_activity = 1 << cl->prio;
		htb_activate_prios(q, cl);
		list_add_tail(&cl->un.leaf.drop_list,
			      q->drops + cl->prio);
	}
}

/**
 * htb_deactivate - remove leaf cl from active feeds
 *
 * Make sure that leaf is active. In the other words it can't be called
 * with non-active leaf. It also removes class from the drop list.
 */
static inline void htb_deactivate(struct htb_sched *q, struct htb_class *cl)
{
	WARN_ON(!cl->prio_activity);

	htb_deactivate_prios(q, cl);
	cl->prio_activity = 0;
	list_del_init(&cl->un.leaf.drop_list);
}

static int htb_enqueue(struct sk_buff *skb, struct Qdisc *sch)
{
	int uninitialized_var(ret);
	struct htb_sched *q = qdisc_priv(sch);
	struct htb_class *cl = htb_classify(skb, sch, &ret);

	if (cl == HTB_DIRECT) {
		/* enqueue to helper queue */
		if (q->direct_queue.qlen < q->direct_qlen) {
			__skb_queue_tail(&q->direct_queue, skb);
			q->direct_pkts++;
		} else {
			return qdisc_drop(skb, sch);
		}
#ifdef CONFIG_NET_CLS_ACT
	} else if (!cl) {
		if (ret & __NET_XMIT_BYPASS)
			qdisc_qstats_drop(sch);
		kfree_skb(skb);
		return ret;
#endif
	} else if ((ret = qdisc_enqueue(skb, cl->un.leaf.q)) != NET_XMIT_SUCCESS) {
		if (net_xmit_drop_count(ret)) {
			qdisc_qstats_drop(sch);
			cl->qstats.drops++;
		}
		return ret;
	} else {
		htb_activate(q, cl);
	}

	qdisc_qstats_backlog_inc(sch, skb);
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
			clp = &cl->un.inner.clprio[prio];
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
		if (unlikely(cl->un.leaf.q->q.qlen == 0)) {
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

		skb = cl->un.leaf.q->dequeue(cl->un.leaf.q);
		if (likely(skb != NULL))
			break;

		qdisc_warn_nonwc("htb", cl->un.leaf.q);
		htb_next_rb_node(level ? &cl->parent->un.inner.clprio[prio].ptr:
					 &q->hlevel[0].hprio[prio].ptr);
		cl = htb_lookup_leaf(hprio, prio);

	} while (cl != start);

	if (likely(skb != NULL)) {
		bstats_update(&cl->bstats, skb);
		cl->un.leaf.deficit[level] -= qdisc_pkt_len(skb);
		if (cl->un.leaf.deficit[level] < 0) {
			cl->un.leaf.deficit[level] += cl->quantum;
			htb_next_rb_node(level ? &cl->parent->un.inner.clprio[prio].ptr :
						 &q->hlevel[0].hprio[prio].ptr);
		}
		/* this used to be after charge_class but this constelation
		 * gives us slightly better performance
		 */
		if (!cl->un.leaf.q->q.qlen)
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
	skb = __skb_dequeue(&q->direct_queue);
	if (skb != NULL) {
ok:
		qdisc_bstats_update(sch, skb);
		qdisc_unthrottled(sch);
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
	qdisc_qstats_overlimit(sch);
	if (likely(next_event > q->now))
		qdisc_watchdog_schedule_ns(&q->watchdog, next_event, true);
	else
		schedule_work(&q->work);
fin:
	return skb;
}

/* try to drop from each class (by prio) until one succeed */
static unsigned int htb_drop(struct Qdisc *sch)
{
	struct htb_sched *q = qdisc_priv(sch);
	int prio;

	for (prio = TC_HTB_NUMPRIO - 1; prio >= 0; prio--) {
		struct list_head *p;
		list_for_each(p, q->drops + prio) {
			struct htb_class *cl = list_entry(p, struct htb_class,
							  un.leaf.drop_list);
			unsigned int len;
			if (cl->un.leaf.q->ops->drop &&
			    (len = cl->un.leaf.q->ops->drop(cl->un.leaf.q))) {
				sch->qstats.backlog -= len;
				sch->q.qlen--;
				if (!cl->un.leaf.q->q.qlen)
					htb_deactivate(q, cl);
				return len;
			}
		}
	}
	return 0;
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
				memset(&cl->un.inner, 0, sizeof(cl->un.inner));
			else {
				if (cl->un.leaf.q)
					qdisc_reset(cl->un.leaf.q);
				INIT_LIST_HEAD(&cl->un.leaf.drop_list);
			}
			cl->prio_activity = 0;
			cl->cmode = HTB_CAN_SEND;
		}
	}
	qdisc_watchdog_cancel(&q->watchdog);
	__skb_queue_purge(&q->direct_queue);
	sch->q.qlen = 0;
	sch->qstats.backlog = 0;
	memset(q->hlevel, 0, sizeof(q->hlevel));
	memset(q->row_mask, 0, sizeof(q->row_mask));
	for (i = 0; i < TC_HTB_NUMPRIO; i++)
		INIT_LIST_HEAD(q->drops + i);
}

static const struct nla_policy htb_policy[TCA_HTB_MAX + 1] = {
	[TCA_HTB_PARMS]	= { .len = sizeof(struct tc_htb_opt) },
	[TCA_HTB_INIT]	= { .len = sizeof(struct tc_htb_glob) },
	[TCA_HTB_CTAB]	= { .type = NLA_BINARY, .len = TC_RTAB_SIZE },
	[TCA_HTB_RTAB]	= { .type = NLA_BINARY, .len = TC_RTAB_SIZE },
	[TCA_HTB_DIRECT_QLEN] = { .type = NLA_U32 },
	[TCA_HTB_RATE64] = { .type = NLA_U64 },
	[TCA_HTB_CEIL64] = { .type = NLA_U64 },
};

static void htb_work_func(struct work_struct *work)
{
	struct htb_sched *q = container_of(work, struct htb_sched, work);
	struct Qdisc *sch = q->watchdog.qdisc;

	rcu_read_lock();
	__netif_schedule(qdisc_root(sch));
	rcu_read_unlock();
}

static int htb_init(struct Qdisc *sch, struct nlattr *opt)
{
	struct htb_sched *q = qdisc_priv(sch);
	struct nlattr *tb[TCA_HTB_MAX + 1];
	struct tc_htb_glob *gopt;
	int err;
	int i;

	if (!opt)
		return -EINVAL;

	err = nla_parse_nested(tb, TCA_HTB_MAX, opt, htb_policy);
	if (err < 0)
		return err;

	if (!tb[TCA_HTB_INIT])
		return -EINVAL;

	gopt = nla_data(tb[TCA_HTB_INIT]);
	if (gopt->version != HTB_VER >> 16)
		return -EINVAL;

	err = qdisc_class_hash_init(&q->clhash);
	if (err < 0)
		return err;
	for (i = 0; i < TC_HTB_NUMPRIO; i++)
		INIT_LIST_HEAD(q->drops + i);

	qdisc_watchdog_init(&q->watchdog, sch);
	INIT_WORK(&q->work, htb_work_func);
	__skb_queue_head_init(&q->direct_queue);

	if (tb[TCA_HTB_DIRECT_QLEN])
		q->direct_qlen = nla_get_u32(tb[TCA_HTB_DIRECT_QLEN]);
	else
		q->direct_qlen = qdisc_dev(sch)->tx_queue_len;

	if ((q->rate2quantum = gopt->rate2quantum) < 1)
		q->rate2quantum = 1;
	q->defcls = gopt->defcls;

	return 0;
}

static int htb_dump(struct Qdisc *sch, struct sk_buff *skb)
{
	struct htb_sched *q = qdisc_priv(sch);
	struct nlattr *nest;
	struct tc_htb_glob gopt;

	/* Its safe to not acquire qdisc lock. As we hold RTNL,
	 * no change can happen on the qdisc parameters.
	 */

	gopt.direct_pkts = q->direct_pkts;
	gopt.version = HTB_VER;
	gopt.rate2quantum = q->rate2quantum;
	gopt.defcls = q->defcls;
	gopt.debug = 0;

	nest = nla_nest_start(skb, TCA_OPTIONS);
	if (nest == NULL)
		goto nla_put_failure;
	if (nla_put(skb, TCA_HTB_INIT, sizeof(gopt), &gopt) ||
	    nla_put_u32(skb, TCA_HTB_DIRECT_QLEN, q->direct_qlen))
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
	struct nlattr *nest;
	struct tc_htb_opt opt;

	/* Its safe to not acquire qdisc lock. As we hold RTNL,
	 * no change can happen on the class parameters.
	 */
	tcm->tcm_parent = cl->parent ? cl->parent->common.classid : TC_H_ROOT;
	tcm->tcm_handle = cl->common.classid;
	if (!cl->level && cl->un.leaf.q)
		tcm->tcm_info = cl->un.leaf.q->handle;

	nest = nla_nest_start(skb, TCA_OPTIONS);
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

static int
htb_dump_class_stats(struct Qdisc *sch, unsigned long arg, struct gnet_dump *d)
{
	struct htb_class *cl = (struct htb_class *)arg;
	__u32 qlen = 0;

	if (!cl->level && cl->un.leaf.q)
		qlen = cl->un.leaf.q->q.qlen;
	cl->xstats.tokens = clamp_t(s64, PSCHED_NS2TICKS(cl->tokens),
				    INT_MIN, INT_MAX);
	cl->xstats.ctokens = clamp_t(s64, PSCHED_NS2TICKS(cl->ctokens),
				     INT_MIN, INT_MAX);

	if (gnet_stats_copy_basic(d, NULL, &cl->bstats) < 0 ||
	    gnet_stats_copy_rate_est(d, NULL, &cl->rate_est) < 0 ||
	    gnet_stats_copy_queue(d, NULL, &cl->qstats, qlen) < 0)
		return -1;

	return gnet_stats_copy_app(d, &cl->xstats, sizeof(cl->xstats));
}

static int htb_graft(struct Qdisc *sch, unsigned long arg, struct Qdisc *new,
		     struct Qdisc **old)
{
	struct htb_class *cl = (struct htb_class *)arg;

	if (cl->level)
		return -EINVAL;
	if (new == NULL &&
	    (new = qdisc_create_dflt(sch->dev_queue, &pfifo_qdisc_ops,
				     cl->common.classid)) == NULL)
		return -ENOBUFS;

	*old = qdisc_replace(sch, new, &cl->un.leaf.q);
	return 0;
}

static struct Qdisc *htb_leaf(struct Qdisc *sch, unsigned long arg)
{
	struct htb_class *cl = (struct htb_class *)arg;
	return !cl->level ? cl->un.leaf.q : NULL;
}

static void htb_qlen_notify(struct Qdisc *sch, unsigned long arg)
{
	struct htb_class *cl = (struct htb_class *)arg;

	if (cl->un.leaf.q->q.qlen == 0)
		htb_deactivate(qdisc_priv(sch), cl);
}

static unsigned long htb_get(struct Qdisc *sch, u32 classid)
{
	struct htb_class *cl = htb_find(classid, sch);
	if (cl)
		cl->refcnt++;
	return (unsigned long)cl;
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

static void htb_parent_to_leaf(struct htb_sched *q, struct htb_class *cl,
			       struct Qdisc *new_q)
{
	struct htb_class *parent = cl->parent;

	WARN_ON(cl->level || !cl->un.leaf.q || cl->prio_activity);

	if (parent->cmode != HTB_CAN_SEND)
		htb_safe_rb_erase(&parent->pq_node,
				  &q->hlevel[parent->level].wait_pq);

	parent->level = 0;
	memset(&parent->un.inner, 0, sizeof(parent->un.inner));
	INIT_LIST_HEAD(&parent->un.leaf.drop_list);
	parent->un.leaf.q = new_q ? new_q : &noop_qdisc;
	parent->tokens = parent->buffer;
	parent->ctokens = parent->cbuffer;
	parent->t_c = ktime_get_ns();
	parent->cmode = HTB_CAN_SEND;
}

static void htb_destroy_class(struct Qdisc *sch, struct htb_class *cl)
{
	if (!cl->level) {
		WARN_ON(!cl->un.leaf.q);
		qdisc_destroy(cl->un.leaf.q);
	}
	gen_kill_estimator(&cl->bstats, &cl->rate_est);
	tcf_destroy_chain(&cl->filter_list);
	kfree(cl);
}

static void htb_destroy(struct Qdisc *sch)
{
	struct htb_sched *q = qdisc_priv(sch);
	struct hlist_node *next;
	struct htb_class *cl;
	unsigned int i;

	cancel_work_sync(&q->work);
	qdisc_watchdog_cancel(&q->watchdog);
	/* This line used to be after htb_destroy_class call below
	 * and surprisingly it worked in 2.4. But it must precede it
	 * because filter need its target class alive to be able to call
	 * unbind_filter on it (without Oops).
	 */
	tcf_destroy_chain(&q->filter_list);

	for (i = 0; i < q->clhash.hashsize; i++) {
		hlist_for_each_entry(cl, &q->clhash.hash[i], common.hnode)
			tcf_destroy_chain(&cl->filter_list);
	}
	for (i = 0; i < q->clhash.hashsize; i++) {
		hlist_for_each_entry_safe(cl, next, &q->clhash.hash[i],
					  common.hnode)
			htb_destroy_class(sch, cl);
	}
	qdisc_class_hash_destroy(&q->clhash);
	__skb_queue_purge(&q->direct_queue);
}

static int htb_delete(struct Qdisc *sch, unsigned long arg)
{
	struct htb_sched *q = qdisc_priv(sch);
	struct htb_class *cl = (struct htb_class *)arg;
	struct Qdisc *new_q = NULL;
	int last_child = 0;

	/* TODO: why don't allow to delete subtree ? references ? does
	 * tc subsys guarantee us that in htb_destroy it holds no class
	 * refs so that we can remove children safely there ?
	 */
	if (cl->children || cl->filter_cnt)
		return -EBUSY;

	if (!cl->level && htb_parent_last_child(cl)) {
		new_q = qdisc_create_dflt(sch->dev_queue, &pfifo_qdisc_ops,
					  cl->parent->common.classid);
		last_child = 1;
	}

	sch_tree_lock(sch);

	if (!cl->level) {
		unsigned int qlen = cl->un.leaf.q->q.qlen;
		unsigned int backlog = cl->un.leaf.q->qstats.backlog;

		qdisc_reset(cl->un.leaf.q);
		qdisc_tree_reduce_backlog(cl->un.leaf.q, qlen, backlog);
	}

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
		htb_parent_to_leaf(q, cl, new_q);

	BUG_ON(--cl->refcnt == 0);
	/*
	 * This shouldn't happen: we "hold" one cops->get() when called
	 * from tc_ctl_tclass; the destroy method is done from cops->put().
	 */

	sch_tree_unlock(sch);
	return 0;
}

static void htb_put(struct Qdisc *sch, unsigned long arg)
{
	struct htb_class *cl = (struct htb_class *)arg;

	if (--cl->refcnt == 0)
		htb_destroy_class(sch, cl);
}

static int htb_change_class(struct Qdisc *sch, u32 classid,
			    u32 parentid, struct nlattr **tca,
			    unsigned long *arg)
{
	int err = -EINVAL;
	struct htb_sched *q = qdisc_priv(sch);
	struct htb_class *cl = (struct htb_class *)*arg, *parent;
	struct nlattr *opt = tca[TCA_OPTIONS];
	struct nlattr *tb[TCA_HTB_MAX + 1];
	struct tc_htb_opt *hopt;
	u64 rate64, ceil64;

	/* extract all subattrs from opt attr */
	if (!opt)
		goto failure;

	err = nla_parse_nested(tb, TCA_HTB_MAX, opt, htb_policy);
	if (err < 0)
		goto failure;

	err = -EINVAL;
	if (tb[TCA_HTB_PARMS] == NULL)
		goto failure;

	parent = parentid == TC_H_ROOT ? NULL : htb_find(parentid, sch);

	hopt = nla_data(tb[TCA_HTB_PARMS]);
	if (!hopt->rate.rate || !hopt->ceil.rate)
		goto failure;

	/* Keeping backward compatible with rate_table based iproute2 tc */
	if (hopt->rate.linklayer == TC_LINKLAYER_UNAWARE)
		qdisc_put_rtab(qdisc_get_rtab(&hopt->rate, tb[TCA_HTB_RTAB]));

	if (hopt->ceil.linklayer == TC_LINKLAYER_UNAWARE)
		qdisc_put_rtab(qdisc_get_rtab(&hopt->ceil, tb[TCA_HTB_CTAB]));

	if (!cl) {		/* new class */
		struct Qdisc *new_q;
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
			pr_err("htb: tree is too deep\n");
			goto failure;
		}
		err = -ENOBUFS;
		cl = kzalloc(sizeof(*cl), GFP_KERNEL);
		if (!cl)
			goto failure;

		if (htb_rate_est || tca[TCA_RATE]) {
			err = gen_new_estimator(&cl->bstats, NULL,
						&cl->rate_est,
						qdisc_root_sleeping_lock(sch),
						tca[TCA_RATE] ? : &est.nla);
			if (err) {
				kfree(cl);
				goto failure;
			}
		}

		cl->refcnt = 1;
		cl->children = 0;
		INIT_LIST_HEAD(&cl->un.leaf.drop_list);
		RB_CLEAR_NODE(&cl->pq_node);

		for (prio = 0; prio < TC_HTB_NUMPRIO; prio++)
			RB_CLEAR_NODE(&cl->node[prio]);

		/* create leaf qdisc early because it uses kmalloc(GFP_KERNEL)
		 * so that can't be used inside of sch_tree_lock
		 * -- thanks to Karlis Peisenieks
		 */
		new_q = qdisc_create_dflt(sch->dev_queue,
					  &pfifo_qdisc_ops, classid);
		sch_tree_lock(sch);
		if (parent && !parent->level) {
			unsigned int qlen = parent->un.leaf.q->q.qlen;
			unsigned int backlog = parent->un.leaf.q->qstats.backlog;

			/* turn parent into inner node */
			qdisc_reset(parent->un.leaf.q);
			qdisc_tree_reduce_backlog(parent->un.leaf.q, qlen, backlog);
			qdisc_destroy(parent->un.leaf.q);
			if (parent->prio_activity)
				htb_deactivate(q, parent);

			/* remove from evt list because of level change */
			if (parent->cmode != HTB_CAN_SEND) {
				htb_safe_rb_erase(&parent->pq_node, &q->hlevel[0].wait_pq);
				parent->cmode = HTB_CAN_SEND;
			}
			parent->level = (parent->parent ? parent->parent->level
					 : TC_HTB_MAXDEPTH) - 1;
			memset(&parent->un.inner, 0, sizeof(parent->un.inner));
		}
		/* leaf (we) needs elementary qdisc */
		cl->un.leaf.q = new_q ? new_q : &noop_qdisc;

		cl->common.classid = classid;
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
	} else {
		if (tca[TCA_RATE]) {
			spinlock_t *lock = qdisc_root_sleeping_lock(sch);

			err = gen_replace_estimator(&cl->bstats, NULL,
						    &cl->rate_est,
						    lock,
						    tca[TCA_RATE]);
			if (err)
				return err;
		}
		sch_tree_lock(sch);
	}

	rate64 = tb[TCA_HTB_RATE64] ? nla_get_u64(tb[TCA_HTB_RATE64]) : 0;

	ceil64 = tb[TCA_HTB_CEIL64] ? nla_get_u64(tb[TCA_HTB_CEIL64]) : 0;

	psched_ratecfg_precompute(&cl->rate, &hopt->rate, rate64);
	psched_ratecfg_precompute(&cl->ceil, &hopt->ceil, ceil64);

	/* it used to be a nasty bug here, we have to check that node
	 * is really leaf before changing cl->un.leaf !
	 */
	if (!cl->level) {
		u64 quantum = cl->rate.rate_bytes_ps;

		do_div(quantum, q->rate2quantum);
		cl->quantum = min_t(u64, quantum, INT_MAX);

		if (!hopt->quantum && cl->quantum < 1000) {
			pr_warn("HTB: quantum of class %X is small. Consider r2q change.\n",
				cl->common.classid);
			cl->quantum = 1000;
		}
		if (!hopt->quantum && cl->quantum > 200000) {
			pr_warn("HTB: quantum of class %X is big. Consider r2q change.\n",
				cl->common.classid);
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

	qdisc_class_hash_grow(sch, &q->clhash);

	*arg = (unsigned long)cl;
	return 0;

failure:
	return err;
}

static struct tcf_proto __rcu **htb_find_tcf(struct Qdisc *sch,
					     unsigned long arg)
{
	struct htb_sched *q = qdisc_priv(sch);
	struct htb_class *cl = (struct htb_class *)arg;
	struct tcf_proto __rcu **fl = cl ? &cl->filter_list : &q->filter_list;

	return fl;
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
		cl->filter_cnt++;
	return (unsigned long)cl;
}

static void htb_unbind_filter(struct Qdisc *sch, unsigned long arg)
{
	struct htb_class *cl = (struct htb_class *)arg;

	if (cl)
		cl->filter_cnt--;
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
			if (arg->count < arg->skip) {
				arg->count++;
				continue;
			}
			if (arg->fn(sch, (unsigned long)cl, arg) < 0) {
				arg->stop = 1;
				return;
			}
			arg->count++;
		}
	}
}

static const struct Qdisc_class_ops htb_class_ops = {
	.graft		=	htb_graft,
	.leaf		=	htb_leaf,
	.qlen_notify	=	htb_qlen_notify,
	.get		=	htb_get,
	.put		=	htb_put,
	.change		=	htb_change_class,
	.delete		=	htb_delete,
	.walk		=	htb_walk,
	.tcf_chain	=	htb_find_tcf,
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
	.drop		=	htb_drop,
	.init		=	htb_init,
	.reset		=	htb_reset,
	.destroy	=	htb_destroy,
	.dump		=	htb_dump,
	.owner		=	THIS_MODULE,
};

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
