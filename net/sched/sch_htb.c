/* vim: ts=8 sw=8
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
 *
 * $Id: sch_htb.c,v 1.25 2003/12/07 11:08:25 devik Exp devik $
 */
#include <linux/config.h>
#include <linux/module.h>
#include <asm/uaccess.h>
#include <asm/system.h>
#include <linux/bitops.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/string.h>
#include <linux/mm.h>
#include <linux/socket.h>
#include <linux/sockios.h>
#include <linux/in.h>
#include <linux/errno.h>
#include <linux/interrupt.h>
#include <linux/if_ether.h>
#include <linux/inet.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/notifier.h>
#include <net/ip.h>
#include <net/route.h>
#include <linux/skbuff.h>
#include <linux/list.h>
#include <linux/compiler.h>
#include <net/sock.h>
#include <net/pkt_sched.h>
#include <linux/rbtree.h>

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

#define HTB_HSIZE 16	/* classid hash size */
#define HTB_EWMAC 2	/* rate average over HTB_EWMAC*HTB_HSIZE sec */
#undef HTB_DEBUG	/* compile debugging support (activated by tc tool) */
#define HTB_RATECM 1    /* whether to use rate computer */
#define HTB_HYSTERESIS 1/* whether to use mode hysteresis for speedup */
#define HTB_QLOCK(S) spin_lock_bh(&(S)->dev->queue_lock)
#define HTB_QUNLOCK(S) spin_unlock_bh(&(S)->dev->queue_lock)
#define HTB_VER 0x30011	/* major must be matched with number suplied by TC as version */

#if HTB_VER >> 16 != TC_HTB_PROTOVER
#error "Mismatched sch_htb.c and pkt_sch.h"
#endif

/* debugging support; S is subsystem, these are defined:
  0 - netlink messages
  1 - enqueue
  2 - drop & requeue
  3 - dequeue main
  4 - dequeue one prio DRR part
  5 - dequeue class accounting
  6 - class overlimit status computation
  7 - hint tree
  8 - event queue
 10 - rate estimator
 11 - classifier 
 12 - fast dequeue cache

 L is level; 0 = none, 1 = basic info, 2 = detailed, 3 = full
 q->debug uint32 contains 16 2-bit fields one for subsystem starting
 from LSB
 */
#ifdef HTB_DEBUG
#define HTB_DBG_COND(S,L) (((q->debug>>(2*S))&3) >= L)
#define HTB_DBG(S,L,FMT,ARG...) if (HTB_DBG_COND(S,L)) \
	printk(KERN_DEBUG FMT,##ARG)
#define HTB_CHCL(cl) BUG_TRAP((cl)->magic == HTB_CMAGIC)
#define HTB_PASSQ q,
#define HTB_ARGQ struct htb_sched *q,
#define static
#undef __inline__
#define __inline__
#undef inline
#define inline
#define HTB_CMAGIC 0xFEFAFEF1
#define htb_safe_rb_erase(N,R) do { BUG_TRAP((N)->rb_color != -1); \
		if ((N)->rb_color == -1) break; \
		rb_erase(N,R); \
		(N)->rb_color = -1; } while (0)
#else
#define HTB_DBG_COND(S,L) (0)
#define HTB_DBG(S,L,FMT,ARG...)
#define HTB_PASSQ
#define HTB_ARGQ
#define HTB_CHCL(cl)
#define htb_safe_rb_erase(N,R) rb_erase(N,R)
#endif


/* used internaly to keep status of single class */
enum htb_cmode {
    HTB_CANT_SEND,		/* class can't send and can't borrow */
    HTB_MAY_BORROW,		/* class can't send but may borrow */
    HTB_CAN_SEND		/* class can send */
};

/* interior & leaf nodes; props specific to leaves are marked L: */
struct htb_class
{
#ifdef HTB_DEBUG
	unsigned magic;
#endif
    /* general class parameters */
    u32 classid;
    struct gnet_stats_basic bstats;
    struct gnet_stats_queue qstats;
    struct gnet_stats_rate_est rate_est;
    struct tc_htb_xstats xstats;/* our special stats */
    int refcnt;			/* usage count of this class */

#ifdef HTB_RATECM
    /* rate measurement counters */
    unsigned long rate_bytes,sum_bytes;
    unsigned long rate_packets,sum_packets;
#endif

    /* topology */
    int level;			/* our level (see above) */
    struct htb_class *parent;	/* parent class */
    struct list_head hlist;	/* classid hash list item */
    struct list_head sibling;	/* sibling list item */
    struct list_head children;	/* children list */

    union {
	    struct htb_class_leaf {
		    struct Qdisc *q;
		    int prio;
		    int aprio;	
		    int quantum;
		    int deficit[TC_HTB_MAXDEPTH];
		    struct list_head drop_list;
	    } leaf;
	    struct htb_class_inner {
		    struct rb_root feed[TC_HTB_NUMPRIO]; /* feed trees */
		    struct rb_node *ptr[TC_HTB_NUMPRIO]; /* current class ptr */
            /* When class changes from state 1->2 and disconnects from 
               parent's feed then we lost ptr value and start from the
              first child again. Here we store classid of the
              last valid ptr (used when ptr is NULL). */
              u32 last_ptr_id[TC_HTB_NUMPRIO];
	    } inner;
    } un;
    struct rb_node node[TC_HTB_NUMPRIO]; /* node for self or feed tree */
    struct rb_node pq_node;		 /* node for event queue */
    unsigned long pq_key;	/* the same type as jiffies global */
    
    int prio_activity;		/* for which prios are we active */
    enum htb_cmode cmode;	/* current mode of the class */

    /* class attached filters */
    struct tcf_proto *filter_list;
    int filter_cnt;

    int warned;		/* only one warning about non work conserving .. */

    /* token bucket parameters */
    struct qdisc_rate_table *rate;	/* rate table of the class itself */
    struct qdisc_rate_table *ceil;	/* ceiling rate (limits borrows too) */
    long buffer,cbuffer;		/* token bucket depth/rate */
    long mbuffer;			/* max wait time */
    long tokens,ctokens;		/* current number of tokens */
    psched_time_t t_c;			/* checkpoint time */
};

/* TODO: maybe compute rate when size is too large .. or drop ? */
static __inline__ long L2T(struct htb_class *cl,struct qdisc_rate_table *rate,
	int size)
{ 
    int slot = size >> rate->rate.cell_log;
    if (slot > 255) {
	cl->xstats.giants++;
	slot = 255;
    }
    return rate->data[slot];
}

struct htb_sched
{
    struct list_head root;			/* root classes list */
    struct list_head hash[HTB_HSIZE];		/* hashed by classid */
    struct list_head drops[TC_HTB_NUMPRIO];	/* active leaves (for drops) */
    
    /* self list - roots of self generating tree */
    struct rb_root row[TC_HTB_MAXDEPTH][TC_HTB_NUMPRIO];
    int row_mask[TC_HTB_MAXDEPTH];
    struct rb_node *ptr[TC_HTB_MAXDEPTH][TC_HTB_NUMPRIO];
    u32 last_ptr_id[TC_HTB_MAXDEPTH][TC_HTB_NUMPRIO];

    /* self wait list - roots of wait PQs per row */
    struct rb_root wait_pq[TC_HTB_MAXDEPTH];

    /* time of nearest event per level (row) */
    unsigned long near_ev_cache[TC_HTB_MAXDEPTH];

    /* cached value of jiffies in dequeue */
    unsigned long jiffies;

    /* whether we hit non-work conserving class during this dequeue; we use */
    int nwc_hit;	/* this to disable mindelay complaint in dequeue */

    int defcls;		/* class where unclassified flows go to */
    u32 debug;		/* subsystem debug levels */

    /* filters for qdisc itself */
    struct tcf_proto *filter_list;
    int filter_cnt;

    int rate2quantum;		/* quant = rate / rate2quantum */
    psched_time_t now;		/* cached dequeue time */
    struct timer_list timer;	/* send delay timer */
#ifdef HTB_RATECM
    struct timer_list rttim;	/* rate computer timer */
    int recmp_bucket;		/* which hash bucket to recompute next */
#endif
    
    /* non shaped skbs; let them go directly thru */
    struct sk_buff_head direct_queue;
    int direct_qlen;  /* max qlen of above */

    long direct_pkts;
};

/* compute hash of size HTB_HSIZE for given handle */
static __inline__ int htb_hash(u32 h) 
{
#if HTB_HSIZE != 16
 #error "Declare new hash for your HTB_HSIZE"
#endif
    h ^= h>>8;	/* stolen from cbq_hash */
    h ^= h>>4;
    return h & 0xf;
}

/* find class in global hash table using given handle */
static __inline__ struct htb_class *htb_find(u32 handle, struct Qdisc *sch)
{
	struct htb_sched *q = qdisc_priv(sch);
	struct list_head *p;
	if (TC_H_MAJ(handle) != sch->handle) 
		return NULL;
	
	list_for_each (p,q->hash+htb_hash(handle)) {
		struct htb_class *cl = list_entry(p,struct htb_class,hlist);
		if (cl->classid == handle)
			return cl;
	}
	return NULL;
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
 * have no valid leaf we try to use MAJOR:default leaf. It still unsuccessfull
 * then finish and return direct queue.
 */
#define HTB_DIRECT (struct htb_class*)-1
static inline u32 htb_classid(struct htb_class *cl)
{
	return (cl && cl != HTB_DIRECT) ? cl->classid : TC_H_UNSPEC;
}

static struct htb_class *htb_classify(struct sk_buff *skb, struct Qdisc *sch, int *qerr)
{
	struct htb_sched *q = qdisc_priv(sch);
	struct htb_class *cl;
	struct tcf_result res;
	struct tcf_proto *tcf;
	int result;

	/* allow to select class by setting skb->priority to valid classid;
	   note that nfmark can be used too by attaching filter fw with no
	   rules in it */
	if (skb->priority == sch->handle)
		return HTB_DIRECT;  /* X:0 (direct flow) selected */
	if ((cl = htb_find(skb->priority,sch)) != NULL && cl->level == 0) 
		return cl;

	*qerr = NET_XMIT_BYPASS;
	tcf = q->filter_list;
	while (tcf && (result = tc_classify(skb, tcf, &res)) >= 0) {
#ifdef CONFIG_NET_CLS_ACT
		switch (result) {
		case TC_ACT_QUEUED:
		case TC_ACT_STOLEN: 
			*qerr = NET_XMIT_SUCCESS;
		case TC_ACT_SHOT:
			return NULL;
		}
#elif defined(CONFIG_NET_CLS_POLICE)
		if (result == TC_POLICE_SHOT)
			return HTB_DIRECT;
#endif
		if ((cl = (void*)res.class) == NULL) {
			if (res.classid == sch->handle)
				return HTB_DIRECT;  /* X:0 (direct flow) */
			if ((cl = htb_find(res.classid,sch)) == NULL)
				break; /* filter selected invalid classid */
		}
		if (!cl->level)
			return cl; /* we hit leaf; return it */

		/* we have got inner class; apply inner filter chain */
		tcf = cl->filter_list;
	}
	/* classification failed; try to use default class */
	cl = htb_find(TC_H_MAKE(TC_H_MAJ(sch->handle),q->defcls),sch);
	if (!cl || cl->level)
		return HTB_DIRECT; /* bad default .. this is safe bet */
	return cl;
}

#ifdef HTB_DEBUG
static void htb_next_rb_node(struct rb_node **n);
#define HTB_DUMTREE(root,memb) if(root) { \
	struct rb_node *n = (root)->rb_node; \
	while (n->rb_left) n = n->rb_left; \
	while (n) { \
		struct htb_class *cl = rb_entry(n, struct htb_class, memb); \
		printk(" %x",cl->classid); htb_next_rb_node (&n); \
	} }

static void htb_debug_dump (struct htb_sched *q)
{
	int i,p;
	printk(KERN_DEBUG "htb*g j=%lu lj=%lu\n",jiffies,q->jiffies);
	/* rows */
	for (i=TC_HTB_MAXDEPTH-1;i>=0;i--) {
		printk(KERN_DEBUG "htb*r%d m=%x",i,q->row_mask[i]);
		for (p=0;p<TC_HTB_NUMPRIO;p++) {
			if (!q->row[i][p].rb_node) continue;
			printk(" p%d:",p);
			HTB_DUMTREE(q->row[i]+p,node[p]);
		}
		printk("\n");
	}
	/* classes */
	for (i = 0; i < HTB_HSIZE; i++) {
		struct list_head *l;
		list_for_each (l,q->hash+i) {
			struct htb_class *cl = list_entry(l,struct htb_class,hlist);
			long diff = PSCHED_TDIFF_SAFE(q->now, cl->t_c, (u32)cl->mbuffer);
			printk(KERN_DEBUG "htb*c%x m=%d t=%ld c=%ld pq=%lu df=%ld ql=%d "
					"pa=%x f:",
				cl->classid,cl->cmode,cl->tokens,cl->ctokens,
				cl->pq_node.rb_color==-1?0:cl->pq_key,diff,
				cl->level?0:cl->un.leaf.q->q.qlen,cl->prio_activity);
			if (cl->level)
			for (p=0;p<TC_HTB_NUMPRIO;p++) {
				if (!cl->un.inner.feed[p].rb_node) continue;
				printk(" p%d a=%x:",p,cl->un.inner.ptr[p]?rb_entry(cl->un.inner.ptr[p], struct htb_class,node[p])->classid:0);
				HTB_DUMTREE(cl->un.inner.feed+p,node[p]);
			}
			printk("\n");
		}
	}
}
#endif
/**
 * htb_add_to_id_tree - adds class to the round robin list
 *
 * Routine adds class to the list (actually tree) sorted by classid.
 * Make sure that class is not already on such list for given prio.
 */
static void htb_add_to_id_tree (HTB_ARGQ struct rb_root *root,
		struct htb_class *cl,int prio)
{
	struct rb_node **p = &root->rb_node, *parent = NULL;
	HTB_DBG(7,3,"htb_add_id_tree cl=%X prio=%d\n",cl->classid,prio);
#ifdef HTB_DEBUG
	if (cl->node[prio].rb_color != -1) { BUG_TRAP(0); return; }
	HTB_CHCL(cl);
	if (*p) {
		struct htb_class *x = rb_entry(*p,struct htb_class,node[prio]);
		HTB_CHCL(x);
	}
#endif
	while (*p) {
		struct htb_class *c; parent = *p;
		c = rb_entry(parent, struct htb_class, node[prio]);
		HTB_CHCL(c);
		if (cl->classid > c->classid)
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
static void htb_add_to_wait_tree (struct htb_sched *q,
		struct htb_class *cl,long delay,int debug_hint)
{
	struct rb_node **p = &q->wait_pq[cl->level].rb_node, *parent = NULL;
	HTB_DBG(7,3,"htb_add_wt cl=%X key=%lu\n",cl->classid,cl->pq_key);
#ifdef HTB_DEBUG
	if (cl->pq_node.rb_color != -1) { BUG_TRAP(0); return; }
	HTB_CHCL(cl);
	if ((delay <= 0 || delay > cl->mbuffer) && net_ratelimit())
		printk(KERN_ERR "HTB: suspicious delay in wait_tree d=%ld cl=%X h=%d\n",delay,cl->classid,debug_hint);
#endif
	cl->pq_key = q->jiffies + PSCHED_US2JIFFIE(delay);
	if (cl->pq_key == q->jiffies)
		cl->pq_key++;

	/* update the nearest event cache */
	if (time_after(q->near_ev_cache[cl->level], cl->pq_key))
		q->near_ev_cache[cl->level] = cl->pq_key;
	
	while (*p) {
		struct htb_class *c; parent = *p;
		c = rb_entry(parent, struct htb_class, pq_node);
		if (time_after_eq(cl->pq_key, c->pq_key))
			p = &parent->rb_right;
		else 
			p = &parent->rb_left;
	}
	rb_link_node(&cl->pq_node, parent, p);
	rb_insert_color(&cl->pq_node, &q->wait_pq[cl->level]);
}

/**
 * htb_next_rb_node - finds next node in binary tree
 *
 * When we are past last key we return NULL.
 * Average complexity is 2 steps per call.
 */
static void htb_next_rb_node(struct rb_node **n)
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
		struct htb_class *cl,int mask)
{
	HTB_DBG(7,2,"htb_addrow cl=%X mask=%X rmask=%X\n",
			cl->classid,mask,q->row_mask[cl->level]);
	HTB_CHCL(cl);
	q->row_mask[cl->level] |= mask;
	while (mask) {
		int prio = ffz(~mask);
		mask &= ~(1 << prio);
		htb_add_to_id_tree(HTB_PASSQ q->row[cl->level]+prio,cl,prio);
	}
}

/**
 * htb_remove_class_from_row - removes class from its row
 *
 * The class is removed from row at priorities marked in mask.
 * It does nothing if mask == 0.
 */
static __inline__ void htb_remove_class_from_row(struct htb_sched *q,
		struct htb_class *cl,int mask)
{
	int m = 0;
	HTB_CHCL(cl);
	while (mask) {
		int prio = ffz(~mask);
		mask &= ~(1 << prio);
		if (q->ptr[cl->level][prio] == cl->node+prio)
			htb_next_rb_node(q->ptr[cl->level]+prio);
		htb_safe_rb_erase(cl->node + prio,q->row[cl->level]+prio);
		if (!q->row[cl->level][prio].rb_node) 
			m |= 1 << prio;
	}
	HTB_DBG(7,2,"htb_delrow cl=%X mask=%X rmask=%X maskdel=%X\n",
			cl->classid,mask,q->row_mask[cl->level],m);
	q->row_mask[cl->level] &= ~m;
}

/**
 * htb_activate_prios - creates active classe's feed chain
 *
 * The class is connected to ancestors and/or appropriate rows
 * for priorities it is participating on. cl->cmode must be new 
 * (activated) mode. It does nothing if cl->prio_activity == 0.
 */
static void htb_activate_prios(struct htb_sched *q,struct htb_class *cl)
{
	struct htb_class *p = cl->parent;
	long m,mask = cl->prio_activity;
	HTB_DBG(7,2,"htb_act_prios cl=%X mask=%lX cmode=%d\n",cl->classid,mask,cl->cmode);
	HTB_CHCL(cl);

	while (cl->cmode == HTB_MAY_BORROW && p && mask) {
		HTB_CHCL(p);
		m = mask; while (m) {
			int prio = ffz(~m);
			m &= ~(1 << prio);
			
			if (p->un.inner.feed[prio].rb_node)
				/* parent already has its feed in use so that
				   reset bit in mask as parent is already ok */
				mask &= ~(1 << prio);
			
			htb_add_to_id_tree(HTB_PASSQ p->un.inner.feed+prio,cl,prio);
		}
		HTB_DBG(7,3,"htb_act_pr_aft p=%X pact=%X mask=%lX pmode=%d\n",
				p->classid,p->prio_activity,mask,p->cmode);
		p->prio_activity |= mask;
		cl = p; p = cl->parent;
		HTB_CHCL(cl);
	}
	if (cl->cmode == HTB_CAN_SEND && mask)
		htb_add_class_to_row(q,cl,mask);
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
	long m,mask = cl->prio_activity;
	HTB_DBG(7,2,"htb_deact_prios cl=%X mask=%lX cmode=%d\n",cl->classid,mask,cl->cmode);
	HTB_CHCL(cl);

	while (cl->cmode == HTB_MAY_BORROW && p && mask) {
		m = mask; mask = 0; 
		while (m) {
			int prio = ffz(~m);
			m &= ~(1 << prio);
			
			if (p->un.inner.ptr[prio] == cl->node+prio) {
				/* we are removing child which is pointed to from
				   parent feed - forget the pointer but remember
				   classid */
				p->un.inner.last_ptr_id[prio] = cl->classid;
				p->un.inner.ptr[prio] = NULL;
			}
			
			htb_safe_rb_erase(cl->node + prio,p->un.inner.feed + prio);
			
			if (!p->un.inner.feed[prio].rb_node) 
				mask |= 1 << prio;
		}
		HTB_DBG(7,3,"htb_deact_pr_aft p=%X pact=%X mask=%lX pmode=%d\n",
				p->classid,p->prio_activity,mask,p->cmode);
		p->prio_activity &= ~mask;
		cl = p; p = cl->parent;
		HTB_CHCL(cl);
	}
	if (cl->cmode == HTB_CAN_SEND && mask) 
		htb_remove_class_from_row(q,cl,mask);
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
static __inline__ enum htb_cmode 
htb_class_mode(struct htb_class *cl,long *diff)
{
    long toks;

    if ((toks = (cl->ctokens + *diff)) < (
#if HTB_HYSTERESIS
	    cl->cmode != HTB_CANT_SEND ? -cl->cbuffer :
#endif
       	    0)) {
	    *diff = -toks;
	    return HTB_CANT_SEND;
    }
    if ((toks = (cl->tokens + *diff)) >= (
#if HTB_HYSTERESIS
	    cl->cmode == HTB_CAN_SEND ? -cl->buffer :
#endif
	    0))
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
htb_change_class_mode(struct htb_sched *q, struct htb_class *cl, long *diff)
{ 
	enum htb_cmode new_mode = htb_class_mode(cl,diff);
	
	HTB_CHCL(cl);
	HTB_DBG(7,1,"htb_chging_clmode %d->%d cl=%X\n",cl->cmode,new_mode,cl->classid);

	if (new_mode == cl->cmode)
		return;	
	
	if (cl->prio_activity) { /* not necessary: speed optimization */
		if (cl->cmode != HTB_CANT_SEND) 
			htb_deactivate_prios(q,cl);
		cl->cmode = new_mode;
		if (new_mode != HTB_CANT_SEND) 
			htb_activate_prios(q,cl);
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
static __inline__ void htb_activate(struct htb_sched *q,struct htb_class *cl)
{
	BUG_TRAP(!cl->level && cl->un.leaf.q && cl->un.leaf.q->q.qlen);
	HTB_CHCL(cl);
	if (!cl->prio_activity) {
		cl->prio_activity = 1 << (cl->un.leaf.aprio = cl->un.leaf.prio);
		htb_activate_prios(q,cl);
		list_add_tail(&cl->un.leaf.drop_list,q->drops+cl->un.leaf.aprio);
	}
}

/**
 * htb_deactivate - remove leaf cl from active feeds 
 *
 * Make sure that leaf is active. In the other words it can't be called
 * with non-active leaf. It also removes class from the drop list.
 */
static __inline__ void 
htb_deactivate(struct htb_sched *q,struct htb_class *cl)
{
	BUG_TRAP(cl->prio_activity);
	HTB_CHCL(cl);
	htb_deactivate_prios(q,cl);
	cl->prio_activity = 0;
	list_del_init(&cl->un.leaf.drop_list);
}

static int htb_enqueue(struct sk_buff *skb, struct Qdisc *sch)
{
    int ret;
    struct htb_sched *q = qdisc_priv(sch);
    struct htb_class *cl = htb_classify(skb,sch,&ret);

    if (cl == HTB_DIRECT) {
	/* enqueue to helper queue */
	if (q->direct_queue.qlen < q->direct_qlen) {
	    __skb_queue_tail(&q->direct_queue, skb);
	    q->direct_pkts++;
	} else {
	    kfree_skb(skb);
	    sch->qstats.drops++;
	    return NET_XMIT_DROP;
	}
#ifdef CONFIG_NET_CLS_ACT
    } else if (!cl) {
	if (ret == NET_XMIT_BYPASS)
		sch->qstats.drops++;
	kfree_skb (skb);
	return ret;
#endif
    } else if (cl->un.leaf.q->enqueue(skb, cl->un.leaf.q) != NET_XMIT_SUCCESS) {
	sch->qstats.drops++;
	cl->qstats.drops++;
	return NET_XMIT_DROP;
    } else {
	cl->bstats.packets++; cl->bstats.bytes += skb->len;
	htb_activate (q,cl);
    }

    sch->q.qlen++;
    sch->bstats.packets++; sch->bstats.bytes += skb->len;
    HTB_DBG(1,1,"htb_enq_ok cl=%X skb=%p\n",(cl && cl != HTB_DIRECT)?cl->classid:0,skb);
    return NET_XMIT_SUCCESS;
}

/* TODO: requeuing packet charges it to policers again !! */
static int htb_requeue(struct sk_buff *skb, struct Qdisc *sch)
{
    struct htb_sched *q = qdisc_priv(sch);
    int ret =  NET_XMIT_SUCCESS;
    struct htb_class *cl = htb_classify(skb,sch, &ret);
    struct sk_buff *tskb;

    if (cl == HTB_DIRECT || !cl) {
	/* enqueue to helper queue */
	if (q->direct_queue.qlen < q->direct_qlen && cl) {
	    __skb_queue_head(&q->direct_queue, skb);
	} else {
            __skb_queue_head(&q->direct_queue, skb);
            tskb = __skb_dequeue_tail(&q->direct_queue);
            kfree_skb (tskb);
            sch->qstats.drops++;
            return NET_XMIT_CN;	
	}
    } else if (cl->un.leaf.q->ops->requeue(skb, cl->un.leaf.q) != NET_XMIT_SUCCESS) {
	sch->qstats.drops++;
	cl->qstats.drops++;
	return NET_XMIT_DROP;
    } else 
	    htb_activate (q,cl);

    sch->q.qlen++;
    sch->qstats.requeues++;
    HTB_DBG(1,1,"htb_req_ok cl=%X skb=%p\n",(cl && cl != HTB_DIRECT)?cl->classid:0,skb);
    return NET_XMIT_SUCCESS;
}

static void htb_timer(unsigned long arg)
{
    struct Qdisc *sch = (struct Qdisc*)arg;
    sch->flags &= ~TCQ_F_THROTTLED;
    wmb();
    netif_schedule(sch->dev);
}

#ifdef HTB_RATECM
#define RT_GEN(D,R) R+=D-(R/HTB_EWMAC);D=0
static void htb_rate_timer(unsigned long arg)
{
	struct Qdisc *sch = (struct Qdisc*)arg;
	struct htb_sched *q = qdisc_priv(sch);
	struct list_head *p;

	/* lock queue so that we can muck with it */
	HTB_QLOCK(sch);
	HTB_DBG(10,1,"htb_rttmr j=%ld\n",jiffies);

	q->rttim.expires = jiffies + HZ;
	add_timer(&q->rttim);

	/* scan and recompute one bucket at time */
	if (++q->recmp_bucket >= HTB_HSIZE) 
		q->recmp_bucket = 0;
	list_for_each (p,q->hash+q->recmp_bucket) {
		struct htb_class *cl = list_entry(p,struct htb_class,hlist);
		HTB_DBG(10,2,"htb_rttmr_cl cl=%X sbyte=%lu spkt=%lu\n",
				cl->classid,cl->sum_bytes,cl->sum_packets);
		RT_GEN (cl->sum_bytes,cl->rate_bytes);
		RT_GEN (cl->sum_packets,cl->rate_packets);
	}
	HTB_QUNLOCK(sch);
}
#endif

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
static void htb_charge_class(struct htb_sched *q,struct htb_class *cl,
		int level,int bytes)
{	
	long toks,diff;
	enum htb_cmode old_mode;
	HTB_DBG(5,1,"htb_chrg_cl cl=%X lev=%d len=%d\n",cl->classid,level,bytes);

#define HTB_ACCNT(T,B,R) toks = diff + cl->T; \
	if (toks > cl->B) toks = cl->B; \
	toks -= L2T(cl, cl->R, bytes); \
	if (toks <= -cl->mbuffer) toks = 1-cl->mbuffer; \
	cl->T = toks

	while (cl) {
		HTB_CHCL(cl);
		diff = PSCHED_TDIFF_SAFE(q->now, cl->t_c, (u32)cl->mbuffer);
#ifdef HTB_DEBUG
		if (diff > cl->mbuffer || diff < 0 || PSCHED_TLESS(q->now, cl->t_c)) {
			if (net_ratelimit())
				printk(KERN_ERR "HTB: bad diff in charge, cl=%X diff=%lX now=%Lu then=%Lu j=%lu\n",
				       cl->classid, diff,
#ifdef CONFIG_NET_SCH_CLK_GETTIMEOFDAY
				       q->now.tv_sec * 1000000ULL + q->now.tv_usec,
				       cl->t_c.tv_sec * 1000000ULL + cl->t_c.tv_usec,
#else
				       (unsigned long long) q->now,
				       (unsigned long long) cl->t_c,
#endif
				       q->jiffies);
			diff = 1000;
		}
#endif
		if (cl->level >= level) {
			if (cl->level == level) cl->xstats.lends++;
			HTB_ACCNT (tokens,buffer,rate);
		} else {
			cl->xstats.borrows++;
			cl->tokens += diff; /* we moved t_c; update tokens */
		}
		HTB_ACCNT (ctokens,cbuffer,ceil);
		cl->t_c = q->now;
		HTB_DBG(5,2,"htb_chrg_clp cl=%X diff=%ld tok=%ld ctok=%ld\n",cl->classid,diff,cl->tokens,cl->ctokens);

		old_mode = cl->cmode; diff = 0;
		htb_change_class_mode(q,cl,&diff);
		if (old_mode != cl->cmode) {
			if (old_mode != HTB_CAN_SEND)
				htb_safe_rb_erase(&cl->pq_node,q->wait_pq+cl->level);
			if (cl->cmode != HTB_CAN_SEND)
				htb_add_to_wait_tree (q,cl,diff,1);
		}
		
#ifdef HTB_RATECM
		/* update rate counters */
		cl->sum_bytes += bytes; cl->sum_packets++;
#endif

		/* update byte stats except for leaves which are already updated */
		if (cl->level) {
			cl->bstats.bytes += bytes;
			cl->bstats.packets++;
		}
		cl = cl->parent;
	}
}

/**
 * htb_do_events - make mode changes to classes at the level
 *
 * Scans event queue for pending events and applies them. Returns jiffies to
 * next pending event (0 for no event in pq).
 * Note: Aplied are events whose have cl->pq_key <= jiffies.
 */
static long htb_do_events(struct htb_sched *q,int level)
{
	int i;
	HTB_DBG(8,1,"htb_do_events l=%d root=%p rmask=%X\n",
			level,q->wait_pq[level].rb_node,q->row_mask[level]);
	for (i = 0; i < 500; i++) {
		struct htb_class *cl;
		long diff;
		struct rb_node *p = q->wait_pq[level].rb_node;
		if (!p) return 0;
		while (p->rb_left) p = p->rb_left;

		cl = rb_entry(p, struct htb_class, pq_node);
		if (time_after(cl->pq_key, q->jiffies)) {
			HTB_DBG(8,3,"htb_do_ev_ret delay=%ld\n",cl->pq_key - q->jiffies);
			return cl->pq_key - q->jiffies;
		}
		htb_safe_rb_erase(p,q->wait_pq+level);
		diff = PSCHED_TDIFF_SAFE(q->now, cl->t_c, (u32)cl->mbuffer);
#ifdef HTB_DEBUG
		if (diff > cl->mbuffer || diff < 0 || PSCHED_TLESS(q->now, cl->t_c)) {
			if (net_ratelimit())
				printk(KERN_ERR "HTB: bad diff in events, cl=%X diff=%lX now=%Lu then=%Lu j=%lu\n",
				       cl->classid, diff,
#ifdef CONFIG_NET_SCH_CLK_GETTIMEOFDAY
				       q->now.tv_sec * 1000000ULL + q->now.tv_usec,
				       cl->t_c.tv_sec * 1000000ULL + cl->t_c.tv_usec,
#else
				       (unsigned long long) q->now,
				       (unsigned long long) cl->t_c,
#endif
				       q->jiffies);
			diff = 1000;
		}
#endif
		htb_change_class_mode(q,cl,&diff);
		if (cl->cmode != HTB_CAN_SEND)
			htb_add_to_wait_tree (q,cl,diff,2);
	}
	if (net_ratelimit())
		printk(KERN_WARNING "htb: too many events !\n");
	return HZ/10;
}

/* Returns class->node+prio from id-tree where classe's id is >= id. NULL
   is no such one exists. */
static struct rb_node *
htb_id_find_next_upper(int prio,struct rb_node *n,u32 id)
{
	struct rb_node *r = NULL;
	while (n) {
		struct htb_class *cl = rb_entry(n,struct htb_class,node[prio]);
		if (id == cl->classid) return n;
		
		if (id > cl->classid) {
			n = n->rb_right;
		} else {
			r = n;
			n = n->rb_left;
		}
	}
	return r;
}

/**
 * htb_lookup_leaf - returns next leaf class in DRR order
 *
 * Find leaf where current feed pointers points to.
 */
static struct htb_class *
htb_lookup_leaf(HTB_ARGQ struct rb_root *tree,int prio,struct rb_node **pptr,u32 *pid)
{
	int i;
	struct {
		struct rb_node *root;
		struct rb_node **pptr;
		u32 *pid;
	} stk[TC_HTB_MAXDEPTH],*sp = stk;
	
	BUG_TRAP(tree->rb_node);
	sp->root = tree->rb_node;
	sp->pptr = pptr;
	sp->pid = pid;

	for (i = 0; i < 65535; i++) {
		HTB_DBG(4,2,"htb_lleaf ptr=%p pid=%X\n",*sp->pptr,*sp->pid);
		
		if (!*sp->pptr && *sp->pid) { 
			/* ptr was invalidated but id is valid - try to recover 
			   the original or next ptr */
			*sp->pptr = htb_id_find_next_upper(prio,sp->root,*sp->pid);
		}
		*sp->pid = 0; /* ptr is valid now so that remove this hint as it
			         can become out of date quickly */
		if (!*sp->pptr) { /* we are at right end; rewind & go up */
			*sp->pptr = sp->root;
			while ((*sp->pptr)->rb_left) 
				*sp->pptr = (*sp->pptr)->rb_left;
			if (sp > stk) {
				sp--;
				BUG_TRAP(*sp->pptr); if(!*sp->pptr) return NULL;
				htb_next_rb_node (sp->pptr);
			}
		} else {
			struct htb_class *cl;
			cl = rb_entry(*sp->pptr,struct htb_class,node[prio]);
			HTB_CHCL(cl);
			if (!cl->level) 
				return cl;
			(++sp)->root = cl->un.inner.feed[prio].rb_node;
			sp->pptr = cl->un.inner.ptr+prio;
			sp->pid = cl->un.inner.last_ptr_id+prio;
		}
	}
	BUG_TRAP(0);
	return NULL;
}

/* dequeues packet at given priority and level; call only if
   you are sure that there is active class at prio/level */
static struct sk_buff *
htb_dequeue_tree(struct htb_sched *q,int prio,int level)
{
	struct sk_buff *skb = NULL;
	struct htb_class *cl,*start;
	/* look initial class up in the row */
	start = cl = htb_lookup_leaf (HTB_PASSQ q->row[level]+prio,prio,
			q->ptr[level]+prio,q->last_ptr_id[level]+prio);
	
	do {
next:
		BUG_TRAP(cl); 
		if (!cl) return NULL;
		HTB_DBG(4,1,"htb_deq_tr prio=%d lev=%d cl=%X defic=%d\n",
				prio,level,cl->classid,cl->un.leaf.deficit[level]);

		/* class can be empty - it is unlikely but can be true if leaf
		   qdisc drops packets in enqueue routine or if someone used
		   graft operation on the leaf since last dequeue; 
		   simply deactivate and skip such class */
		if (unlikely(cl->un.leaf.q->q.qlen == 0)) {
			struct htb_class *next;
			htb_deactivate(q,cl);

			/* row/level might become empty */
			if ((q->row_mask[level] & (1 << prio)) == 0)
				return NULL; 
			
			next = htb_lookup_leaf (HTB_PASSQ q->row[level]+prio,
					prio,q->ptr[level]+prio,q->last_ptr_id[level]+prio);

			if (cl == start) /* fix start if we just deleted it */
				start = next;
			cl = next;
			goto next;
		}
	
		if (likely((skb = cl->un.leaf.q->dequeue(cl->un.leaf.q)) != NULL)) 
			break;
		if (!cl->warned) {
			printk(KERN_WARNING "htb: class %X isn't work conserving ?!\n",cl->classid);
			cl->warned = 1;
		}
		q->nwc_hit++;
		htb_next_rb_node((level?cl->parent->un.inner.ptr:q->ptr[0])+prio);
		cl = htb_lookup_leaf (HTB_PASSQ q->row[level]+prio,prio,q->ptr[level]+prio,
				q->last_ptr_id[level]+prio);

	} while (cl != start);

	if (likely(skb != NULL)) {
		if ((cl->un.leaf.deficit[level] -= skb->len) < 0) {
			HTB_DBG(4,2,"htb_next_cl oldptr=%p quant_add=%d\n",
				level?cl->parent->un.inner.ptr[prio]:q->ptr[0][prio],cl->un.leaf.quantum);
			cl->un.leaf.deficit[level] += cl->un.leaf.quantum;
			htb_next_rb_node((level?cl->parent->un.inner.ptr:q->ptr[0])+prio);
		}
		/* this used to be after charge_class but this constelation
		   gives us slightly better performance */
		if (!cl->un.leaf.q->q.qlen)
			htb_deactivate (q,cl);
		htb_charge_class (q,cl,level,skb->len);
	}
	return skb;
}

static void htb_delay_by(struct Qdisc *sch,long delay)
{
	struct htb_sched *q = qdisc_priv(sch);
	if (delay <= 0) delay = 1;
	if (unlikely(delay > 5*HZ)) {
		if (net_ratelimit())
			printk(KERN_INFO "HTB delay %ld > 5sec\n", delay);
		delay = 5*HZ;
	}
	/* why don't use jiffies here ? because expires can be in past */
	mod_timer(&q->timer, q->jiffies + delay);
	sch->flags |= TCQ_F_THROTTLED;
	sch->qstats.overlimits++;
	HTB_DBG(3,1,"htb_deq t_delay=%ld\n",delay);
}

static struct sk_buff *htb_dequeue(struct Qdisc *sch)
{
	struct sk_buff *skb = NULL;
	struct htb_sched *q = qdisc_priv(sch);
	int level;
	long min_delay;
#ifdef HTB_DEBUG
	int evs_used = 0;
#endif

	q->jiffies = jiffies;
	HTB_DBG(3,1,"htb_deq dircnt=%d qlen=%d\n",skb_queue_len(&q->direct_queue),
			sch->q.qlen);

	/* try to dequeue direct packets as high prio (!) to minimize cpu work */
	if ((skb = __skb_dequeue(&q->direct_queue)) != NULL) {
		sch->flags &= ~TCQ_F_THROTTLED;
		sch->q.qlen--;
		return skb;
	}

	if (!sch->q.qlen) goto fin;
	PSCHED_GET_TIME(q->now);

	min_delay = LONG_MAX;
	q->nwc_hit = 0;
	for (level = 0; level < TC_HTB_MAXDEPTH; level++) {
		/* common case optimization - skip event handler quickly */
		int m;
		long delay;
		if (time_after_eq(q->jiffies, q->near_ev_cache[level])) {
			delay = htb_do_events(q,level);
			q->near_ev_cache[level] = q->jiffies + (delay ? delay : HZ);
#ifdef HTB_DEBUG
			evs_used++;
#endif
		} else
			delay = q->near_ev_cache[level] - q->jiffies;	
		
		if (delay && min_delay > delay) 
			min_delay = delay;
		m = ~q->row_mask[level];
		while (m != (int)(-1)) {
			int prio = ffz (m);
			m |= 1 << prio;
			skb = htb_dequeue_tree(q,prio,level);
			if (likely(skb != NULL)) {
				sch->q.qlen--;
				sch->flags &= ~TCQ_F_THROTTLED;
				goto fin;
			}
		}
	}
#ifdef HTB_DEBUG
	if (!q->nwc_hit && min_delay >= 10*HZ && net_ratelimit()) {
		if (min_delay == LONG_MAX) {
			printk(KERN_ERR "HTB: dequeue bug (%d,%lu,%lu), report it please !\n",
					evs_used,q->jiffies,jiffies);
			htb_debug_dump(q);
		} else 
			printk(KERN_WARNING "HTB: mindelay=%ld, some class has "
					"too small rate\n",min_delay);
	}
#endif
	htb_delay_by (sch,min_delay > 5*HZ ? 5*HZ : min_delay);
fin:
	HTB_DBG(3,1,"htb_deq_end %s j=%lu skb=%p\n",sch->dev->name,q->jiffies,skb);
	return skb;
}

/* try to drop from each class (by prio) until one succeed */
static unsigned int htb_drop(struct Qdisc* sch)
{
	struct htb_sched *q = qdisc_priv(sch);
	int prio;

	for (prio = TC_HTB_NUMPRIO - 1; prio >= 0; prio--) {
		struct list_head *p;
		list_for_each (p,q->drops+prio) {
			struct htb_class *cl = list_entry(p, struct htb_class,
							  un.leaf.drop_list);
			unsigned int len;
			if (cl->un.leaf.q->ops->drop && 
				(len = cl->un.leaf.q->ops->drop(cl->un.leaf.q))) {
				sch->q.qlen--;
				if (!cl->un.leaf.q->q.qlen)
					htb_deactivate (q,cl);
				return len;
			}
		}
	}
	return 0;
}

/* reset all classes */
/* always caled under BH & queue lock */
static void htb_reset(struct Qdisc* sch)
{
	struct htb_sched *q = qdisc_priv(sch);
	int i;
	HTB_DBG(0,1,"htb_reset sch=%p, handle=%X\n",sch,sch->handle);

	for (i = 0; i < HTB_HSIZE; i++) {
		struct list_head *p;
		list_for_each (p,q->hash+i) {
			struct htb_class *cl = list_entry(p,struct htb_class,hlist);
			if (cl->level)
				memset(&cl->un.inner,0,sizeof(cl->un.inner));
			else {
				if (cl->un.leaf.q) 
					qdisc_reset(cl->un.leaf.q);
				INIT_LIST_HEAD(&cl->un.leaf.drop_list);
			}
			cl->prio_activity = 0;
			cl->cmode = HTB_CAN_SEND;
#ifdef HTB_DEBUG
			cl->pq_node.rb_color = -1;
			memset(cl->node,255,sizeof(cl->node));
#endif

		}
	}
	sch->flags &= ~TCQ_F_THROTTLED;
	del_timer(&q->timer);
	__skb_queue_purge(&q->direct_queue);
	sch->q.qlen = 0;
	memset(q->row,0,sizeof(q->row));
	memset(q->row_mask,0,sizeof(q->row_mask));
	memset(q->wait_pq,0,sizeof(q->wait_pq));
	memset(q->ptr,0,sizeof(q->ptr));
	for (i = 0; i < TC_HTB_NUMPRIO; i++)
		INIT_LIST_HEAD(q->drops+i);
}

static int htb_init(struct Qdisc *sch, struct rtattr *opt)
{
	struct htb_sched *q = qdisc_priv(sch);
	struct rtattr *tb[TCA_HTB_INIT];
	struct tc_htb_glob *gopt;
	int i;
#ifdef HTB_DEBUG
	printk(KERN_INFO "HTB init, kernel part version %d.%d\n",
			  HTB_VER >> 16,HTB_VER & 0xffff);
#endif
	if (!opt || rtattr_parse_nested(tb, TCA_HTB_INIT, opt) ||
			tb[TCA_HTB_INIT-1] == NULL ||
			RTA_PAYLOAD(tb[TCA_HTB_INIT-1]) < sizeof(*gopt)) {
		printk(KERN_ERR "HTB: hey probably you have bad tc tool ?\n");
		return -EINVAL;
	}
	gopt = RTA_DATA(tb[TCA_HTB_INIT-1]);
	if (gopt->version != HTB_VER >> 16) {
		printk(KERN_ERR "HTB: need tc/htb version %d (minor is %d), you have %d\n",
				HTB_VER >> 16,HTB_VER & 0xffff,gopt->version);
		return -EINVAL;
	}
	q->debug = gopt->debug;
	HTB_DBG(0,1,"htb_init sch=%p handle=%X r2q=%d\n",sch,sch->handle,gopt->rate2quantum);

	INIT_LIST_HEAD(&q->root);
	for (i = 0; i < HTB_HSIZE; i++)
		INIT_LIST_HEAD(q->hash+i);
	for (i = 0; i < TC_HTB_NUMPRIO; i++)
		INIT_LIST_HEAD(q->drops+i);

	init_timer(&q->timer);
	skb_queue_head_init(&q->direct_queue);

	q->direct_qlen = sch->dev->tx_queue_len;
	if (q->direct_qlen < 2) /* some devices have zero tx_queue_len */
		q->direct_qlen = 2;
	q->timer.function = htb_timer;
	q->timer.data = (unsigned long)sch;

#ifdef HTB_RATECM
	init_timer(&q->rttim);
	q->rttim.function = htb_rate_timer;
	q->rttim.data = (unsigned long)sch;
	q->rttim.expires = jiffies + HZ;
	add_timer(&q->rttim);
#endif
	if ((q->rate2quantum = gopt->rate2quantum) < 1)
		q->rate2quantum = 1;
	q->defcls = gopt->defcls;

	return 0;
}

static int htb_dump(struct Qdisc *sch, struct sk_buff *skb)
{
	struct htb_sched *q = qdisc_priv(sch);
	unsigned char	 *b = skb->tail;
	struct rtattr *rta;
	struct tc_htb_glob gopt;
	HTB_DBG(0,1,"htb_dump sch=%p, handle=%X\n",sch,sch->handle);
	HTB_QLOCK(sch);
	gopt.direct_pkts = q->direct_pkts;

#ifdef HTB_DEBUG
	if (HTB_DBG_COND(0,2))
		htb_debug_dump(q);
#endif
	gopt.version = HTB_VER;
	gopt.rate2quantum = q->rate2quantum;
	gopt.defcls = q->defcls;
	gopt.debug = q->debug;
	rta = (struct rtattr*)b;
	RTA_PUT(skb, TCA_OPTIONS, 0, NULL);
	RTA_PUT(skb, TCA_HTB_INIT, sizeof(gopt), &gopt);
	rta->rta_len = skb->tail - b;
	HTB_QUNLOCK(sch);
	return skb->len;
rtattr_failure:
	HTB_QUNLOCK(sch);
	skb_trim(skb, skb->tail - skb->data);
	return -1;
}

static int htb_dump_class(struct Qdisc *sch, unsigned long arg,
	struct sk_buff *skb, struct tcmsg *tcm)
{
#ifdef HTB_DEBUG
	struct htb_sched *q = qdisc_priv(sch);
#endif
	struct htb_class *cl = (struct htb_class*)arg;
	unsigned char	 *b = skb->tail;
	struct rtattr *rta;
	struct tc_htb_opt opt;

	HTB_DBG(0,1,"htb_dump_class handle=%X clid=%X\n",sch->handle,cl->classid);

	HTB_QLOCK(sch);
	tcm->tcm_parent = cl->parent ? cl->parent->classid : TC_H_ROOT;
	tcm->tcm_handle = cl->classid;
	if (!cl->level && cl->un.leaf.q)
		tcm->tcm_info = cl->un.leaf.q->handle;

	rta = (struct rtattr*)b;
	RTA_PUT(skb, TCA_OPTIONS, 0, NULL);

	memset (&opt,0,sizeof(opt));

	opt.rate = cl->rate->rate; opt.buffer = cl->buffer;
	opt.ceil = cl->ceil->rate; opt.cbuffer = cl->cbuffer;
	opt.quantum = cl->un.leaf.quantum; opt.prio = cl->un.leaf.prio;
	opt.level = cl->level; 
	RTA_PUT(skb, TCA_HTB_PARMS, sizeof(opt), &opt);
	rta->rta_len = skb->tail - b;
	HTB_QUNLOCK(sch);
	return skb->len;
rtattr_failure:
	HTB_QUNLOCK(sch);
	skb_trim(skb, b - skb->data);
	return -1;
}

static int
htb_dump_class_stats(struct Qdisc *sch, unsigned long arg,
	struct gnet_dump *d)
{
	struct htb_class *cl = (struct htb_class*)arg;

#ifdef HTB_RATECM
	cl->rate_est.bps = cl->rate_bytes/(HTB_EWMAC*HTB_HSIZE);
	cl->rate_est.pps = cl->rate_packets/(HTB_EWMAC*HTB_HSIZE);
#endif

	if (!cl->level && cl->un.leaf.q)
		cl->qstats.qlen = cl->un.leaf.q->q.qlen;
	cl->xstats.tokens = cl->tokens;
	cl->xstats.ctokens = cl->ctokens;

	if (gnet_stats_copy_basic(d, &cl->bstats) < 0 ||
	    gnet_stats_copy_rate_est(d, &cl->rate_est) < 0 ||
	    gnet_stats_copy_queue(d, &cl->qstats) < 0)
		return -1;

	return gnet_stats_copy_app(d, &cl->xstats, sizeof(cl->xstats));
}

static int htb_graft(struct Qdisc *sch, unsigned long arg, struct Qdisc *new,
	struct Qdisc **old)
{
	struct htb_class *cl = (struct htb_class*)arg;

	if (cl && !cl->level) {
		if (new == NULL && (new = qdisc_create_dflt(sch->dev, 
					&pfifo_qdisc_ops)) == NULL)
					return -ENOBUFS;
		sch_tree_lock(sch);
		if ((*old = xchg(&cl->un.leaf.q, new)) != NULL) {
			if (cl->prio_activity)
				htb_deactivate (qdisc_priv(sch),cl);

			/* TODO: is it correct ? Why CBQ doesn't do it ? */
			sch->q.qlen -= (*old)->q.qlen;	
			qdisc_reset(*old);
		}
		sch_tree_unlock(sch);
		return 0;
	}
	return -ENOENT;
}

static struct Qdisc * htb_leaf(struct Qdisc *sch, unsigned long arg)
{
	struct htb_class *cl = (struct htb_class*)arg;
	return (cl && !cl->level) ? cl->un.leaf.q : NULL;
}

static unsigned long htb_get(struct Qdisc *sch, u32 classid)
{
#ifdef HTB_DEBUG
	struct htb_sched *q = qdisc_priv(sch);
#endif
	struct htb_class *cl = htb_find(classid,sch);
	HTB_DBG(0,1,"htb_get clid=%X q=%p cl=%p ref=%d\n",classid,q,cl,cl?cl->refcnt:0);
	if (cl) 
		cl->refcnt++;
	return (unsigned long)cl;
}

static void htb_destroy_filters(struct tcf_proto **fl)
{
	struct tcf_proto *tp;

	while ((tp = *fl) != NULL) {
		*fl = tp->next;
		tcf_destroy(tp);
	}
}

static void htb_destroy_class(struct Qdisc* sch,struct htb_class *cl)
{
	struct htb_sched *q = qdisc_priv(sch);
	HTB_DBG(0,1,"htb_destrycls clid=%X ref=%d\n", cl?cl->classid:0,cl?cl->refcnt:0);
	if (!cl->level) {
		BUG_TRAP(cl->un.leaf.q);
		sch->q.qlen -= cl->un.leaf.q->q.qlen;
		qdisc_destroy(cl->un.leaf.q);
	}
	qdisc_put_rtab(cl->rate);
	qdisc_put_rtab(cl->ceil);
	
	htb_destroy_filters (&cl->filter_list);
	
	while (!list_empty(&cl->children)) 
		htb_destroy_class (sch,list_entry(cl->children.next,
					struct htb_class,sibling));

	/* note: this delete may happen twice (see htb_delete) */
	list_del(&cl->hlist);
	list_del(&cl->sibling);
	
	if (cl->prio_activity)
		htb_deactivate (q,cl);
	
	if (cl->cmode != HTB_CAN_SEND)
		htb_safe_rb_erase(&cl->pq_node,q->wait_pq+cl->level);
	
	kfree(cl);
}

/* always caled under BH & queue lock */
static void htb_destroy(struct Qdisc* sch)
{
	struct htb_sched *q = qdisc_priv(sch);
	HTB_DBG(0,1,"htb_destroy q=%p\n",q);

	del_timer_sync (&q->timer);
#ifdef HTB_RATECM
	del_timer_sync (&q->rttim);
#endif
	/* This line used to be after htb_destroy_class call below
	   and surprisingly it worked in 2.4. But it must precede it 
	   because filter need its target class alive to be able to call
	   unbind_filter on it (without Oops). */
	htb_destroy_filters(&q->filter_list);
	
	while (!list_empty(&q->root)) 
		htb_destroy_class (sch,list_entry(q->root.next,
					struct htb_class,sibling));

	__skb_queue_purge(&q->direct_queue);
}

static int htb_delete(struct Qdisc *sch, unsigned long arg)
{
	struct htb_sched *q = qdisc_priv(sch);
	struct htb_class *cl = (struct htb_class*)arg;
	HTB_DBG(0,1,"htb_delete q=%p cl=%X ref=%d\n",q,cl?cl->classid:0,cl?cl->refcnt:0);

	// TODO: why don't allow to delete subtree ? references ? does
	// tc subsys quarantee us that in htb_destroy it holds no class
	// refs so that we can remove children safely there ?
	if (!list_empty(&cl->children) || cl->filter_cnt)
		return -EBUSY;
	
	sch_tree_lock(sch);
	
	/* delete from hash and active; remainder in destroy_class */
	list_del_init(&cl->hlist);
	if (cl->prio_activity)
		htb_deactivate (q,cl);

	if (--cl->refcnt == 0)
		htb_destroy_class(sch,cl);

	sch_tree_unlock(sch);
	return 0;
}

static void htb_put(struct Qdisc *sch, unsigned long arg)
{
#ifdef HTB_DEBUG
	struct htb_sched *q = qdisc_priv(sch);
#endif
	struct htb_class *cl = (struct htb_class*)arg;
	HTB_DBG(0,1,"htb_put q=%p cl=%X ref=%d\n",q,cl?cl->classid:0,cl?cl->refcnt:0);

	if (--cl->refcnt == 0)
		htb_destroy_class(sch,cl);
}

static int htb_change_class(struct Qdisc *sch, u32 classid, 
		u32 parentid, struct rtattr **tca, unsigned long *arg)
{
	int err = -EINVAL;
	struct htb_sched *q = qdisc_priv(sch);
	struct htb_class *cl = (struct htb_class*)*arg,*parent;
	struct rtattr *opt = tca[TCA_OPTIONS-1];
	struct qdisc_rate_table *rtab = NULL, *ctab = NULL;
	struct rtattr *tb[TCA_HTB_RTAB];
	struct tc_htb_opt *hopt;

	/* extract all subattrs from opt attr */
	if (!opt || rtattr_parse_nested(tb, TCA_HTB_RTAB, opt) ||
			tb[TCA_HTB_PARMS-1] == NULL ||
			RTA_PAYLOAD(tb[TCA_HTB_PARMS-1]) < sizeof(*hopt))
		goto failure;
	
	parent = parentid == TC_H_ROOT ? NULL : htb_find (parentid,sch);

	hopt = RTA_DATA(tb[TCA_HTB_PARMS-1]);
	HTB_DBG(0,1,"htb_chg cl=%p(%X), clid=%X, parid=%X, opt/prio=%d, rate=%u, buff=%d, quant=%d\n", cl,cl?cl->classid:0,classid,parentid,(int)hopt->prio,hopt->rate.rate,hopt->buffer,hopt->quantum);
	rtab = qdisc_get_rtab(&hopt->rate, tb[TCA_HTB_RTAB-1]);
	ctab = qdisc_get_rtab(&hopt->ceil, tb[TCA_HTB_CTAB-1]);
	if (!rtab || !ctab) goto failure;

	if (!cl) { /* new class */
		struct Qdisc *new_q;
		/* check for valid classid */
		if (!classid || TC_H_MAJ(classid^sch->handle) || htb_find(classid,sch))
			goto failure;

		/* check maximal depth */
		if (parent && parent->parent && parent->parent->level < 2) {
			printk(KERN_ERR "htb: tree is too deep\n");
			goto failure;
		}
		err = -ENOBUFS;
		if ((cl = kmalloc(sizeof(*cl), GFP_KERNEL)) == NULL)
			goto failure;
		
		memset(cl, 0, sizeof(*cl));
		cl->refcnt = 1;
		INIT_LIST_HEAD(&cl->sibling);
		INIT_LIST_HEAD(&cl->hlist);
		INIT_LIST_HEAD(&cl->children);
		INIT_LIST_HEAD(&cl->un.leaf.drop_list);
#ifdef HTB_DEBUG
		cl->magic = HTB_CMAGIC;
#endif

		/* create leaf qdisc early because it uses kmalloc(GFP_KERNEL)
		   so that can't be used inside of sch_tree_lock
		   -- thanks to Karlis Peisenieks */
		new_q = qdisc_create_dflt(sch->dev, &pfifo_qdisc_ops);
		sch_tree_lock(sch);
		if (parent && !parent->level) {
			/* turn parent into inner node */
			sch->q.qlen -= parent->un.leaf.q->q.qlen;
			qdisc_destroy (parent->un.leaf.q);
			if (parent->prio_activity) 
				htb_deactivate (q,parent);

			/* remove from evt list because of level change */
			if (parent->cmode != HTB_CAN_SEND) {
				htb_safe_rb_erase(&parent->pq_node,q->wait_pq /*+0*/);
				parent->cmode = HTB_CAN_SEND;
			}
			parent->level = (parent->parent ? parent->parent->level
					: TC_HTB_MAXDEPTH) - 1;
			memset (&parent->un.inner,0,sizeof(parent->un.inner));
		}
		/* leaf (we) needs elementary qdisc */
		cl->un.leaf.q = new_q ? new_q : &noop_qdisc;

		cl->classid = classid; cl->parent = parent;

		/* set class to be in HTB_CAN_SEND state */
		cl->tokens = hopt->buffer;
		cl->ctokens = hopt->cbuffer;
		cl->mbuffer = 60000000; /* 1min */
		PSCHED_GET_TIME(cl->t_c);
		cl->cmode = HTB_CAN_SEND;

		/* attach to the hash list and parent's family */
		list_add_tail(&cl->hlist, q->hash+htb_hash(classid));
		list_add_tail(&cl->sibling, parent ? &parent->children : &q->root);
#ifdef HTB_DEBUG
		{ 
			int i;
			for (i = 0; i < TC_HTB_NUMPRIO; i++) cl->node[i].rb_color = -1;
			cl->pq_node.rb_color = -1;
		}
#endif
	} else sch_tree_lock(sch);

	/* it used to be a nasty bug here, we have to check that node
           is really leaf before changing cl->un.leaf ! */
	if (!cl->level) {
		cl->un.leaf.quantum = rtab->rate.rate / q->rate2quantum;
		if (!hopt->quantum && cl->un.leaf.quantum < 1000) {
			printk(KERN_WARNING "HTB: quantum of class %X is small. Consider r2q change.\n", cl->classid);
			cl->un.leaf.quantum = 1000;
		}
		if (!hopt->quantum && cl->un.leaf.quantum > 200000) {
			printk(KERN_WARNING "HTB: quantum of class %X is big. Consider r2q change.\n", cl->classid);
			cl->un.leaf.quantum = 200000;
		}
		if (hopt->quantum)
			cl->un.leaf.quantum = hopt->quantum;
		if ((cl->un.leaf.prio = hopt->prio) >= TC_HTB_NUMPRIO)
			cl->un.leaf.prio = TC_HTB_NUMPRIO - 1;
	}

	cl->buffer = hopt->buffer;
	cl->cbuffer = hopt->cbuffer;
	if (cl->rate) qdisc_put_rtab(cl->rate); cl->rate = rtab;
	if (cl->ceil) qdisc_put_rtab(cl->ceil); cl->ceil = ctab;
	sch_tree_unlock(sch);

	*arg = (unsigned long)cl;
	return 0;

failure:
	if (rtab) qdisc_put_rtab(rtab);
	if (ctab) qdisc_put_rtab(ctab);
	return err;
}

static struct tcf_proto **htb_find_tcf(struct Qdisc *sch, unsigned long arg)
{
	struct htb_sched *q = qdisc_priv(sch);
	struct htb_class *cl = (struct htb_class *)arg;
	struct tcf_proto **fl = cl ? &cl->filter_list : &q->filter_list;
	HTB_DBG(0,2,"htb_tcf q=%p clid=%X fref=%d fl=%p\n",q,cl?cl->classid:0,cl?cl->filter_cnt:q->filter_cnt,*fl);
	return fl;
}

static unsigned long htb_bind_filter(struct Qdisc *sch, unsigned long parent,
	u32 classid)
{
	struct htb_sched *q = qdisc_priv(sch);
	struct htb_class *cl = htb_find (classid,sch);
	HTB_DBG(0,2,"htb_bind q=%p clid=%X cl=%p fref=%d\n",q,classid,cl,cl?cl->filter_cnt:q->filter_cnt);
	/*if (cl && !cl->level) return 0;
	  The line above used to be there to prevent attaching filters to 
	  leaves. But at least tc_index filter uses this just to get class 
	  for other reasons so that we have to allow for it.
	  ----
	  19.6.2002 As Werner explained it is ok - bind filter is just
	  another way to "lock" the class - unlike "get" this lock can
	  be broken by class during destroy IIUC.
	 */
	if (cl) 
		cl->filter_cnt++; 
	else 
		q->filter_cnt++;
	return (unsigned long)cl;
}

static void htb_unbind_filter(struct Qdisc *sch, unsigned long arg)
{
	struct htb_sched *q = qdisc_priv(sch);
	struct htb_class *cl = (struct htb_class *)arg;
	HTB_DBG(0,2,"htb_unbind q=%p cl=%p fref=%d\n",q,cl,cl?cl->filter_cnt:q->filter_cnt);
	if (cl) 
		cl->filter_cnt--; 
	else 
		q->filter_cnt--;
}

static void htb_walk(struct Qdisc *sch, struct qdisc_walker *arg)
{
	struct htb_sched *q = qdisc_priv(sch);
	int i;

	if (arg->stop)
		return;

	for (i = 0; i < HTB_HSIZE; i++) {
		struct list_head *p;
		list_for_each (p,q->hash+i) {
			struct htb_class *cl = list_entry(p,struct htb_class,hlist);
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

static struct Qdisc_class_ops htb_class_ops = {
	.graft		=	htb_graft,
	.leaf		=	htb_leaf,
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

static struct Qdisc_ops htb_qdisc_ops = {
	.next		=	NULL,
	.cl_ops		=	&htb_class_ops,
	.id		=	"htb",
	.priv_size	=	sizeof(struct htb_sched),
	.enqueue	=	htb_enqueue,
	.dequeue	=	htb_dequeue,
	.requeue	=	htb_requeue,
	.drop		=	htb_drop,
	.init		=	htb_init,
	.reset		=	htb_reset,
	.destroy	=	htb_destroy,
	.change		=	NULL /* htb_change */,
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
