/*
 * Copyright (c) 2003 Patrick McHardy, <kaber@trash.net>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * 2003-10-17 - Ported from altq
 */
/*
 * Copyright (c) 1997-1999 Carnegie Mellon University. All Rights Reserved.
 *
 * Permission to use, copy, modify, and distribute this software and
 * its documentation is hereby granted (including for commercial or
 * for-profit use), provided that both the copyright notice and this
 * permission notice appear in all copies of the software, derivative
 * works, or modified versions, and any portions thereof.
 *
 * THIS SOFTWARE IS EXPERIMENTAL AND IS KNOWN TO HAVE BUGS, SOME OF
 * WHICH MAY HAVE SERIOUS CONSEQUENCES.  CARNEGIE MELLON PROVIDES THIS
 * SOFTWARE IN ITS ``AS IS'' CONDITION, AND ANY EXPRESS OR IMPLIED
 * WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED.  IN NO EVENT SHALL CARNEGIE MELLON UNIVERSITY BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT
 * OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
 * BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
 * LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE
 * USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH
 * DAMAGE.
 *
 * Carnegie Mellon encourages (but does not require) users of this
 * software to return any improvements or extensions that they make,
 * and to grant Carnegie Mellon the rights to redistribute these
 * changes without encumbrance.
 */
/*
 * H-FSC is described in Proceedings of SIGCOMM'97,
 * "A Hierarchical Fair Service Curve Algorithm for Link-Sharing,
 * Real-Time and Priority Service"
 * by Ion Stoica, Hui Zhang, and T. S. Eugene Ng.
 *
 * Oleg Cherevko <olwi@aq.ml.com.ua> added the upperlimit for link-sharing.
 * when a class has an upperlimit, the fit-time is computed from the
 * upperlimit service curve.  the link-sharing scheduler does not schedule
 * a class whose fit-time exceeds the current time.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/types.h>
#include <linux/errno.h>
#include <linux/compiler.h>
#include <linux/spinlock.h>
#include <linux/skbuff.h>
#include <linux/string.h>
#include <linux/slab.h>
#include <linux/list.h>
#include <linux/rbtree.h>
#include <linux/init.h>
#include <linux/rtnetlink.h>
#include <linux/pkt_sched.h>
#include <net/netlink.h>
#include <net/pkt_sched.h>
#include <net/pkt_cls.h>
#include <asm/div64.h>

/*
 * kernel internal service curve representation:
 *   coordinates are given by 64 bit unsigned integers.
 *   x-axis: unit is clock count.
 *   y-axis: unit is byte.
 *
 *   The service curve parameters are converted to the internal
 *   representation. The slope values are scaled to avoid overflow.
 *   the inverse slope values as well as the y-projection of the 1st
 *   segment are kept in order to avoid 64-bit divide operations
 *   that are expensive on 32-bit architectures.
 */

struct internal_sc {
	u64	sm1;	/* scaled slope of the 1st segment */
	u64	ism1;	/* scaled inverse-slope of the 1st segment */
	u64	dx;	/* the x-projection of the 1st segment */
	u64	dy;	/* the y-projection of the 1st segment */
	u64	sm2;	/* scaled slope of the 2nd segment */
	u64	ism2;	/* scaled inverse-slope of the 2nd segment */
};

/* runtime service curve */
struct runtime_sc {
	u64	x;	/* current starting position on x-axis */
	u64	y;	/* current starting position on y-axis */
	u64	sm1;	/* scaled slope of the 1st segment */
	u64	ism1;	/* scaled inverse-slope of the 1st segment */
	u64	dx;	/* the x-projection of the 1st segment */
	u64	dy;	/* the y-projection of the 1st segment */
	u64	sm2;	/* scaled slope of the 2nd segment */
	u64	ism2;	/* scaled inverse-slope of the 2nd segment */
};

enum hfsc_class_flags {
	HFSC_RSC = 0x1,
	HFSC_FSC = 0x2,
	HFSC_USC = 0x4
};

struct hfsc_class {
	struct Qdisc_class_common cl_common;

	struct gnet_stats_basic_sync bstats;
	struct gnet_stats_queue qstats;
	struct net_rate_estimator __rcu *rate_est;
	struct tcf_proto __rcu *filter_list; /* filter list */
	struct tcf_block *block;
	unsigned int	level;		/* class level in hierarchy */

	struct hfsc_sched *sched;	/* scheduler data */
	struct hfsc_class *cl_parent;	/* parent class */
	struct list_head siblings;	/* sibling classes */
	struct list_head children;	/* child classes */
	struct Qdisc	*qdisc;		/* leaf qdisc */

	struct rb_node el_node;		/* qdisc's eligible tree member */
	struct rb_root vt_tree;		/* active children sorted by cl_vt */
	struct rb_node vt_node;		/* parent's vt_tree member */
	struct rb_root cf_tree;		/* active children sorted by cl_f */
	struct rb_node cf_node;		/* parent's cf_heap member */

	u64	cl_total;		/* total work in bytes */
	u64	cl_cumul;		/* cumulative work in bytes done by
					   real-time criteria */

	u64	cl_d;			/* deadline*/
	u64	cl_e;			/* eligible time */
	u64	cl_vt;			/* virtual time */
	u64	cl_f;			/* time when this class will fit for
					   link-sharing, max(myf, cfmin) */
	u64	cl_myf;			/* my fit-time (calculated from this
					   class's own upperlimit curve) */
	u64	cl_cfmin;		/* earliest children's fit-time (used
					   with cl_myf to obtain cl_f) */
	u64	cl_cvtmin;		/* minimal virtual time among the
					   children fit for link-sharing
					   (monotonic within a period) */
	u64	cl_vtadj;		/* intra-period cumulative vt
					   adjustment */
	u64	cl_cvtoff;		/* largest virtual time seen among
					   the children */

	struct internal_sc cl_rsc;	/* internal real-time service curve */
	struct internal_sc cl_fsc;	/* internal fair service curve */
	struct internal_sc cl_usc;	/* internal upperlimit service curve */
	struct runtime_sc cl_deadline;	/* deadline curve */
	struct runtime_sc cl_eligible;	/* eligible curve */
	struct runtime_sc cl_virtual;	/* virtual curve */
	struct runtime_sc cl_ulimit;	/* upperlimit curve */

	u8		cl_flags;	/* which curves are valid */
	u32		cl_vtperiod;	/* vt period sequence number */
	u32		cl_parentperiod;/* parent's vt period sequence number*/
	u32		cl_nactive;	/* number of active children */
};

struct hfsc_sched {
	u16	defcls;				/* default class id */
	struct hfsc_class root;			/* root class */
	struct Qdisc_class_hash clhash;		/* class hash */
	struct rb_root eligible;		/* eligible tree */
	struct qdisc_watchdog watchdog;		/* watchdog timer */
};

#define	HT_INFINITY	0xffffffffffffffffULL	/* infinite time value */

static bool cl_in_el_or_vttree(struct hfsc_class *cl)
{
	return ((cl->cl_flags & HFSC_FSC) && cl->cl_nactive) ||
		((cl->cl_flags & HFSC_RSC) && !RB_EMPTY_NODE(&cl->el_node));
}

/*
 * eligible tree holds backlogged classes being sorted by their eligible times.
 * there is one eligible tree per hfsc instance.
 */

static void
eltree_insert(struct hfsc_class *cl)
{
	struct rb_node **p = &cl->sched->eligible.rb_node;
	struct rb_node *parent = NULL;
	struct hfsc_class *cl1;

	while (*p != NULL) {
		parent = *p;
		cl1 = rb_entry(parent, struct hfsc_class, el_node);
		if (cl->cl_e >= cl1->cl_e)
			p = &parent->rb_right;
		else
			p = &parent->rb_left;
	}
	rb_link_node(&cl->el_node, parent, p);
	rb_insert_color(&cl->el_node, &cl->sched->eligible);
}

static inline void
eltree_remove(struct hfsc_class *cl)
{
	if (!RB_EMPTY_NODE(&cl->el_node)) {
		rb_erase(&cl->el_node, &cl->sched->eligible);
		RB_CLEAR_NODE(&cl->el_node);
	}
}

static inline void
eltree_update(struct hfsc_class *cl)
{
	eltree_remove(cl);
	eltree_insert(cl);
}

/* find the class with the minimum deadline among the eligible classes */
static inline struct hfsc_class *
eltree_get_mindl(struct hfsc_sched *q, u64 cur_time)
{
	struct hfsc_class *p, *cl = NULL;
	struct rb_node *n;

	for (n = rb_first(&q->eligible); n != NULL; n = rb_next(n)) {
		p = rb_entry(n, struct hfsc_class, el_node);
		if (p->cl_e > cur_time)
			break;
		if (cl == NULL || p->cl_d < cl->cl_d)
			cl = p;
	}
	return cl;
}

/* find the class with minimum eligible time among the eligible classes */
static inline struct hfsc_class *
eltree_get_minel(struct hfsc_sched *q)
{
	struct rb_node *n;

	n = rb_first(&q->eligible);
	if (n == NULL)
		return NULL;
	return rb_entry(n, struct hfsc_class, el_node);
}

/*
 * vttree holds holds backlogged child classes being sorted by their virtual
 * time. each intermediate class has one vttree.
 */
static void
vttree_insert(struct hfsc_class *cl)
{
	struct rb_node **p = &cl->cl_parent->vt_tree.rb_node;
	struct rb_node *parent = NULL;
	struct hfsc_class *cl1;

	while (*p != NULL) {
		parent = *p;
		cl1 = rb_entry(parent, struct hfsc_class, vt_node);
		if (cl->cl_vt >= cl1->cl_vt)
			p = &parent->rb_right;
		else
			p = &parent->rb_left;
	}
	rb_link_node(&cl->vt_node, parent, p);
	rb_insert_color(&cl->vt_node, &cl->cl_parent->vt_tree);
}

static inline void
vttree_remove(struct hfsc_class *cl)
{
	rb_erase(&cl->vt_node, &cl->cl_parent->vt_tree);
}

static inline void
vttree_update(struct hfsc_class *cl)
{
	vttree_remove(cl);
	vttree_insert(cl);
}

static inline struct hfsc_class *
vttree_firstfit(struct hfsc_class *cl, u64 cur_time)
{
	struct hfsc_class *p;
	struct rb_node *n;

	for (n = rb_first(&cl->vt_tree); n != NULL; n = rb_next(n)) {
		p = rb_entry(n, struct hfsc_class, vt_node);
		if (p->cl_f <= cur_time)
			return p;
	}
	return NULL;
}

/*
 * get the leaf class with the minimum vt in the hierarchy
 */
static struct hfsc_class *
vttree_get_minvt(struct hfsc_class *cl, u64 cur_time)
{
	/* if root-class's cfmin is bigger than cur_time nothing to do */
	if (cl->cl_cfmin > cur_time)
		return NULL;

	while (cl->level > 0) {
		cl = vttree_firstfit(cl, cur_time);
		if (cl == NULL)
			return NULL;
		/*
		 * update parent's cl_cvtmin.
		 */
		if (cl->cl_parent->cl_cvtmin < cl->cl_vt)
			cl->cl_parent->cl_cvtmin = cl->cl_vt;
	}
	return cl;
}

static void
cftree_insert(struct hfsc_class *cl)
{
	struct rb_node **p = &cl->cl_parent->cf_tree.rb_node;
	struct rb_node *parent = NULL;
	struct hfsc_class *cl1;

	while (*p != NULL) {
		parent = *p;
		cl1 = rb_entry(parent, struct hfsc_class, cf_node);
		if (cl->cl_f >= cl1->cl_f)
			p = &parent->rb_right;
		else
			p = &parent->rb_left;
	}
	rb_link_node(&cl->cf_node, parent, p);
	rb_insert_color(&cl->cf_node, &cl->cl_parent->cf_tree);
}

static inline void
cftree_remove(struct hfsc_class *cl)
{
	rb_erase(&cl->cf_node, &cl->cl_parent->cf_tree);
}

static inline void
cftree_update(struct hfsc_class *cl)
{
	cftree_remove(cl);
	cftree_insert(cl);
}

/*
 * service curve support functions
 *
 *  external service curve parameters
 *	m: bps
 *	d: us
 *  internal service curve parameters
 *	sm: (bytes/psched_us) << SM_SHIFT
 *	ism: (psched_us/byte) << ISM_SHIFT
 *	dx: psched_us
 *
 * The clock source resolution with ktime and PSCHED_SHIFT 10 is 1.024us.
 *
 * sm and ism are scaled in order to keep effective digits.
 * SM_SHIFT and ISM_SHIFT are selected to keep at least 4 effective
 * digits in decimal using the following table.
 *
 *  bits/sec      100Kbps     1Mbps     10Mbps     100Mbps    1Gbps
 *  ------------+-------------------------------------------------------
 *  bytes/1.024us 12.8e-3    128e-3     1280e-3    12800e-3   128000e-3
 *
 *  1.024us/byte  78.125     7.8125     0.78125    0.078125   0.0078125
 *
 * So, for PSCHED_SHIFT 10 we need: SM_SHIFT 20, ISM_SHIFT 18.
 */
#define	SM_SHIFT	(30 - PSCHED_SHIFT)
#define	ISM_SHIFT	(8 + PSCHED_SHIFT)

#define	SM_MASK		((1ULL << SM_SHIFT) - 1)
#define	ISM_MASK	((1ULL << ISM_SHIFT) - 1)

static inline u64
seg_x2y(u64 x, u64 sm)
{
	u64 y;

	/*
	 * compute
	 *	y = x * sm >> SM_SHIFT
	 * but divide it for the upper and lower bits to avoid overflow
	 */
	y = (x >> SM_SHIFT) * sm + (((x & SM_MASK) * sm) >> SM_SHIFT);
	return y;
}

static inline u64
seg_y2x(u64 y, u64 ism)
{
	u64 x;

	if (y == 0)
		x = 0;
	else if (ism == HT_INFINITY)
		x = HT_INFINITY;
	else {
		x = (y >> ISM_SHIFT) * ism
		    + (((y & ISM_MASK) * ism) >> ISM_SHIFT);
	}
	return x;
}

/* Convert m (bps) into sm (bytes/psched us) */
static u64
m2sm(u32 m)
{
	u64 sm;

	sm = ((u64)m << SM_SHIFT);
	sm += PSCHED_TICKS_PER_SEC - 1;
	do_div(sm, PSCHED_TICKS_PER_SEC);
	return sm;
}

/* convert m (bps) into ism (psched us/byte) */
static u64
m2ism(u32 m)
{
	u64 ism;

	if (m == 0)
		ism = HT_INFINITY;
	else {
		ism = ((u64)PSCHED_TICKS_PER_SEC << ISM_SHIFT);
		ism += m - 1;
		do_div(ism, m);
	}
	return ism;
}

/* convert d (us) into dx (psched us) */
static u64
d2dx(u32 d)
{
	u64 dx;

	dx = ((u64)d * PSCHED_TICKS_PER_SEC);
	dx += USEC_PER_SEC - 1;
	do_div(dx, USEC_PER_SEC);
	return dx;
}

/* convert sm (bytes/psched us) into m (bps) */
static u32
sm2m(u64 sm)
{
	u64 m;

	m = (sm * PSCHED_TICKS_PER_SEC) >> SM_SHIFT;
	return (u32)m;
}

/* convert dx (psched us) into d (us) */
static u32
dx2d(u64 dx)
{
	u64 d;

	d = dx * USEC_PER_SEC;
	do_div(d, PSCHED_TICKS_PER_SEC);
	return (u32)d;
}

static void
sc2isc(struct tc_service_curve *sc, struct internal_sc *isc)
{
	isc->sm1  = m2sm(sc->m1);
	isc->ism1 = m2ism(sc->m1);
	isc->dx   = d2dx(sc->d);
	isc->dy   = seg_x2y(isc->dx, isc->sm1);
	isc->sm2  = m2sm(sc->m2);
	isc->ism2 = m2ism(sc->m2);
}

/*
 * initialize the runtime service curve with the given internal
 * service curve starting at (x, y).
 */
static void
rtsc_init(struct runtime_sc *rtsc, struct internal_sc *isc, u64 x, u64 y)
{
	rtsc->x	   = x;
	rtsc->y    = y;
	rtsc->sm1  = isc->sm1;
	rtsc->ism1 = isc->ism1;
	rtsc->dx   = isc->dx;
	rtsc->dy   = isc->dy;
	rtsc->sm2  = isc->sm2;
	rtsc->ism2 = isc->ism2;
}

/*
 * calculate the y-projection of the runtime service curve by the
 * given x-projection value
 */
static u64
rtsc_y2x(struct runtime_sc *rtsc, u64 y)
{
	u64 x;

	if (y < rtsc->y)
		x = rtsc->x;
	else if (y <= rtsc->y + rtsc->dy) {
		/* x belongs to the 1st segment */
		if (rtsc->dy == 0)
			x = rtsc->x + rtsc->dx;
		else
			x = rtsc->x + seg_y2x(y - rtsc->y, rtsc->ism1);
	} else {
		/* x belongs to the 2nd segment */
		x = rtsc->x + rtsc->dx
		    + seg_y2x(y - rtsc->y - rtsc->dy, rtsc->ism2);
	}
	return x;
}

static u64
rtsc_x2y(struct runtime_sc *rtsc, u64 x)
{
	u64 y;

	if (x <= rtsc->x)
		y = rtsc->y;
	else if (x <= rtsc->x + rtsc->dx)
		/* y belongs to the 1st segment */
		y = rtsc->y + seg_x2y(x - rtsc->x, rtsc->sm1);
	else
		/* y belongs to the 2nd segment */
		y = rtsc->y + rtsc->dy
		    + seg_x2y(x - rtsc->x - rtsc->dx, rtsc->sm2);
	return y;
}

/*
 * update the runtime service curve by taking the minimum of the current
 * runtime service curve and the service curve starting at (x, y).
 */
static void
rtsc_min(struct runtime_sc *rtsc, struct internal_sc *isc, u64 x, u64 y)
{
	u64 y1, y2, dx, dy;
	u32 dsm;

	if (isc->sm1 <= isc->sm2) {
		/* service curve is convex */
		y1 = rtsc_x2y(rtsc, x);
		if (y1 < y)
			/* the current rtsc is smaller */
			return;
		rtsc->x = x;
		rtsc->y = y;
		return;
	}

	/*
	 * service curve is concave
	 * compute the two y values of the current rtsc
	 *	y1: at x
	 *	y2: at (x + dx)
	 */
	y1 = rtsc_x2y(rtsc, x);
	if (y1 <= y) {
		/* rtsc is below isc, no change to rtsc */
		return;
	}

	y2 = rtsc_x2y(rtsc, x + isc->dx);
	if (y2 >= y + isc->dy) {
		/* rtsc is above isc, replace rtsc by isc */
		rtsc->x = x;
		rtsc->y = y;
		rtsc->dx = isc->dx;
		rtsc->dy = isc->dy;
		return;
	}

	/*
	 * the two curves intersect
	 * compute the offsets (dx, dy) using the reverse
	 * function of seg_x2y()
	 *	seg_x2y(dx, sm1) == seg_x2y(dx, sm2) + (y1 - y)
	 */
	dx = (y1 - y) << SM_SHIFT;
	dsm = isc->sm1 - isc->sm2;
	do_div(dx, dsm);
	/*
	 * check if (x, y1) belongs to the 1st segment of rtsc.
	 * if so, add the offset.
	 */
	if (rtsc->x + rtsc->dx > x)
		dx += rtsc->x + rtsc->dx - x;
	dy = seg_x2y(dx, isc->sm1);

	rtsc->x = x;
	rtsc->y = y;
	rtsc->dx = dx;
	rtsc->dy = dy;
}

static void
init_ed(struct hfsc_class *cl, unsigned int next_len)
{
	u64 cur_time = psched_get_time();

	/* update the deadline curve */
	rtsc_min(&cl->cl_deadline, &cl->cl_rsc, cur_time, cl->cl_cumul);

	/*
	 * update the eligible curve.
	 * for concave, it is equal to the deadline curve.
	 * for convex, it is a linear curve with slope m2.
	 */
	cl->cl_eligible = cl->cl_deadline;
	if (cl->cl_rsc.sm1 <= cl->cl_rsc.sm2) {
		cl->cl_eligible.dx = 0;
		cl->cl_eligible.dy = 0;
	}

	/* compute e and d */
	cl->cl_e = rtsc_y2x(&cl->cl_eligible, cl->cl_cumul);
	cl->cl_d = rtsc_y2x(&cl->cl_deadline, cl->cl_cumul + next_len);

	eltree_insert(cl);
}

static void
update_ed(struct hfsc_class *cl, unsigned int next_len)
{
	cl->cl_e = rtsc_y2x(&cl->cl_eligible, cl->cl_cumul);
	cl->cl_d = rtsc_y2x(&cl->cl_deadline, cl->cl_cumul + next_len);

	eltree_update(cl);
}

static inline void
update_d(struct hfsc_class *cl, unsigned int next_len)
{
	cl->cl_d = rtsc_y2x(&cl->cl_deadline, cl->cl_cumul + next_len);
}

static inline void
update_cfmin(struct hfsc_class *cl)
{
	struct rb_node *n = rb_first(&cl->cf_tree);
	struct hfsc_class *p;

	if (n == NULL) {
		cl->cl_cfmin = 0;
		return;
	}
	p = rb_entry(n, struct hfsc_class, cf_node);
	cl->cl_cfmin = p->cl_f;
}

static void
init_vf(struct hfsc_class *cl, unsigned int len)
{
	struct hfsc_class *max_cl;
	struct rb_node *n;
	u64 vt, f, cur_time;
	int go_active;

	cur_time = 0;
	go_active = 1;
	for (; cl->cl_parent != NULL; cl = cl->cl_parent) {
		if (go_active && cl->cl_nactive++ == 0)
			go_active = 1;
		else
			go_active = 0;

		if (go_active) {
			n = rb_last(&cl->cl_parent->vt_tree);
			if (n != NULL) {
				max_cl = rb_entry(n, struct hfsc_class, vt_node);
				/*
				 * set vt to the average of the min and max
				 * classes.  if the parent's period didn't
				 * change, don't decrease vt of the class.
				 */
				vt = max_cl->cl_vt;
				if (cl->cl_parent->cl_cvtmin != 0)
					vt = (cl->cl_parent->cl_cvtmin + vt)/2;

				if (cl->cl_parent->cl_vtperiod !=
				    cl->cl_parentperiod || vt > cl->cl_vt)
					cl->cl_vt = vt;
			} else {
				/*
				 * first child for a new parent backlog period.
				 * initialize cl_vt to the highest value seen
				 * among the siblings. this is analogous to
				 * what cur_time would provide in realtime case.
				 */
				cl->cl_vt = cl->cl_parent->cl_cvtoff;
				cl->cl_parent->cl_cvtmin = 0;
			}

			/* update the virtual curve */
			rtsc_min(&cl->cl_virtual, &cl->cl_fsc, cl->cl_vt, cl->cl_total);
			cl->cl_vtadj = 0;

			cl->cl_vtperiod++;  /* increment vt period */
			cl->cl_parentperiod = cl->cl_parent->cl_vtperiod;
			if (cl->cl_parent->cl_nactive == 0)
				cl->cl_parentperiod++;
			cl->cl_f = 0;

			vttree_insert(cl);
			cftree_insert(cl);

			if (cl->cl_flags & HFSC_USC) {
				/* class has upper limit curve */
				if (cur_time == 0)
					cur_time = psched_get_time();

				/* update the ulimit curve */
				rtsc_min(&cl->cl_ulimit, &cl->cl_usc, cur_time,
					 cl->cl_total);
				/* compute myf */
				cl->cl_myf = rtsc_y2x(&cl->cl_ulimit,
						      cl->cl_total);
			}
		}

		f = max(cl->cl_myf, cl->cl_cfmin);
		if (f != cl->cl_f) {
			cl->cl_f = f;
			cftree_update(cl);
		}
		update_cfmin(cl->cl_parent);
	}
}

static void
update_vf(struct hfsc_class *cl, unsigned int len, u64 cur_time)
{
	u64 f; /* , myf_bound, delta; */
	int go_passive = 0;

	if (cl->qdisc->q.qlen == 0 && cl->cl_flags & HFSC_FSC)
		go_passive = 1;

	for (; cl->cl_parent != NULL; cl = cl->cl_parent) {
		cl->cl_total += len;

		if (!(cl->cl_flags & HFSC_FSC) || cl->cl_nactive == 0)
			continue;

		if (go_passive && --cl->cl_nactive == 0)
			go_passive = 1;
		else
			go_passive = 0;

		/* update vt */
		cl->cl_vt = rtsc_y2x(&cl->cl_virtual, cl->cl_total) + cl->cl_vtadj;

		/*
		 * if vt of the class is smaller than cvtmin,
		 * the class was skipped in the past due to non-fit.
		 * if so, we need to adjust vtadj.
		 */
		if (cl->cl_vt < cl->cl_parent->cl_cvtmin) {
			cl->cl_vtadj += cl->cl_parent->cl_cvtmin - cl->cl_vt;
			cl->cl_vt = cl->cl_parent->cl_cvtmin;
		}

		if (go_passive) {
			/* no more active child, going passive */

			/* update cvtoff of the parent class */
			if (cl->cl_vt > cl->cl_parent->cl_cvtoff)
				cl->cl_parent->cl_cvtoff = cl->cl_vt;

			/* remove this class from the vt tree */
			vttree_remove(cl);

			cftree_remove(cl);
			update_cfmin(cl->cl_parent);

			continue;
		}

		/* update the vt tree */
		vttree_update(cl);

		/* update f */
		if (cl->cl_flags & HFSC_USC) {
			cl->cl_myf = rtsc_y2x(&cl->cl_ulimit, cl->cl_total);
#if 0
			cl->cl_myf = cl->cl_myfadj + rtsc_y2x(&cl->cl_ulimit,
							      cl->cl_total);
			/*
			 * This code causes classes to stay way under their
			 * limit when multiple classes are used at gigabit
			 * speed. needs investigation. -kaber
			 */
			/*
			 * if myf lags behind by more than one clock tick
			 * from the current time, adjust myfadj to prevent
			 * a rate-limited class from going greedy.
			 * in a steady state under rate-limiting, myf
			 * fluctuates within one clock tick.
			 */
			myf_bound = cur_time - PSCHED_JIFFIE2US(1);
			if (cl->cl_myf < myf_bound) {
				delta = cur_time - cl->cl_myf;
				cl->cl_myfadj += delta;
				cl->cl_myf += delta;
			}
#endif
		}

		f = max(cl->cl_myf, cl->cl_cfmin);
		if (f != cl->cl_f) {
			cl->cl_f = f;
			cftree_update(cl);
			update_cfmin(cl->cl_parent);
		}
	}
}

static unsigned int
qdisc_peek_len(struct Qdisc *sch)
{
	struct sk_buff *skb;
	unsigned int len;

	skb = sch->ops->peek(sch);
	if (unlikely(skb == NULL)) {
		qdisc_warn_nonwc("qdisc_peek_len", sch);
		return 0;
	}
	len = qdisc_pkt_len(skb);

	return len;
}

static void
hfsc_adjust_levels(struct hfsc_class *cl)
{
	struct hfsc_class *p;
	unsigned int level;

	do {
		level = 0;
		list_for_each_entry(p, &cl->children, siblings) {
			if (p->level >= level)
				level = p->level + 1;
		}
		cl->level = level;
	} while ((cl = cl->cl_parent) != NULL);
}

static inline struct hfsc_class *
hfsc_find_class(u32 classid, struct Qdisc *sch)
{
	struct hfsc_sched *q = qdisc_priv(sch);
	struct Qdisc_class_common *clc;

	clc = qdisc_class_find(&q->clhash, classid);
	if (clc == NULL)
		return NULL;
	return container_of(clc, struct hfsc_class, cl_common);
}

static void
hfsc_change_rsc(struct hfsc_class *cl, struct tc_service_curve *rsc,
		u64 cur_time)
{
	sc2isc(rsc, &cl->cl_rsc);
	rtsc_init(&cl->cl_deadline, &cl->cl_rsc, cur_time, cl->cl_cumul);
	cl->cl_eligible = cl->cl_deadline;
	if (cl->cl_rsc.sm1 <= cl->cl_rsc.sm2) {
		cl->cl_eligible.dx = 0;
		cl->cl_eligible.dy = 0;
	}
	cl->cl_flags |= HFSC_RSC;
}

static void
hfsc_change_fsc(struct hfsc_class *cl, struct tc_service_curve *fsc)
{
	sc2isc(fsc, &cl->cl_fsc);
	rtsc_init(&cl->cl_virtual, &cl->cl_fsc, cl->cl_vt, cl->cl_total);
	cl->cl_flags |= HFSC_FSC;
}

static void
hfsc_change_usc(struct hfsc_class *cl, struct tc_service_curve *usc,
		u64 cur_time)
{
	sc2isc(usc, &cl->cl_usc);
	rtsc_init(&cl->cl_ulimit, &cl->cl_usc, cur_time, cl->cl_total);
	cl->cl_flags |= HFSC_USC;
}

static void
hfsc_upgrade_rt(struct hfsc_class *cl)
{
	cl->cl_fsc = cl->cl_rsc;
	rtsc_init(&cl->cl_virtual, &cl->cl_fsc, cl->cl_vt, cl->cl_total);
	cl->cl_flags |= HFSC_FSC;
}

static const struct nla_policy hfsc_policy[TCA_HFSC_MAX + 1] = {
	[TCA_HFSC_RSC]	= { .len = sizeof(struct tc_service_curve) },
	[TCA_HFSC_FSC]	= { .len = sizeof(struct tc_service_curve) },
	[TCA_HFSC_USC]	= { .len = sizeof(struct tc_service_curve) },
};

static int
hfsc_change_class(struct Qdisc *sch, u32 classid, u32 parentid,
		  struct nlattr **tca, unsigned long *arg,
		  struct netlink_ext_ack *extack)
{
	struct hfsc_sched *q = qdisc_priv(sch);
	struct hfsc_class *cl = (struct hfsc_class *)*arg;
	struct hfsc_class *parent = NULL;
	struct nlattr *opt = tca[TCA_OPTIONS];
	struct nlattr *tb[TCA_HFSC_MAX + 1];
	struct tc_service_curve *rsc = NULL, *fsc = NULL, *usc = NULL;
	u64 cur_time;
	int err;

	if (opt == NULL)
		return -EINVAL;

	err = nla_parse_nested_deprecated(tb, TCA_HFSC_MAX, opt, hfsc_policy,
					  NULL);
	if (err < 0)
		return err;

	if (tb[TCA_HFSC_RSC]) {
		rsc = nla_data(tb[TCA_HFSC_RSC]);
		if (rsc->m1 == 0 && rsc->m2 == 0)
			rsc = NULL;
	}

	if (tb[TCA_HFSC_FSC]) {
		fsc = nla_data(tb[TCA_HFSC_FSC]);
		if (fsc->m1 == 0 && fsc->m2 == 0)
			fsc = NULL;
	}

	if (tb[TCA_HFSC_USC]) {
		usc = nla_data(tb[TCA_HFSC_USC]);
		if (usc->m1 == 0 && usc->m2 == 0)
			usc = NULL;
	}

	if (cl != NULL) {
		int old_flags;
		int len = 0;

		if (parentid) {
			if (cl->cl_parent &&
			    cl->cl_parent->cl_common.classid != parentid)
				return -EINVAL;
			if (cl->cl_parent == NULL && parentid != TC_H_ROOT)
				return -EINVAL;
		}
		cur_time = psched_get_time();

		if (tca[TCA_RATE]) {
			err = gen_replace_estimator(&cl->bstats, NULL,
						    &cl->rate_est,
						    NULL,
						    true,
						    tca[TCA_RATE]);
			if (err)
				return err;
		}

		sch_tree_lock(sch);
		old_flags = cl->cl_flags;

		if (rsc != NULL)
			hfsc_change_rsc(cl, rsc, cur_time);
		if (fsc != NULL)
			hfsc_change_fsc(cl, fsc);
		if (usc != NULL)
			hfsc_change_usc(cl, usc, cur_time);

		if (cl->qdisc->q.qlen != 0)
			len = qdisc_peek_len(cl->qdisc);
		/* Check queue length again since some qdisc implementations
		 * (e.g., netem/codel) might empty the queue during the peek
		 * operation.
		 */
		if (cl->qdisc->q.qlen != 0) {
			if (cl->cl_flags & HFSC_RSC) {
				if (old_flags & HFSC_RSC)
					update_ed(cl, len);
				else
					init_ed(cl, len);
			}

			if (cl->cl_flags & HFSC_FSC) {
				if (old_flags & HFSC_FSC)
					update_vf(cl, 0, cur_time);
				else
					init_vf(cl, len);
			}
		}
		sch_tree_unlock(sch);

		return 0;
	}

	if (parentid == TC_H_ROOT)
		return -EEXIST;

	parent = &q->root;
	if (parentid) {
		parent = hfsc_find_class(parentid, sch);
		if (parent == NULL)
			return -ENOENT;
	}

	if (classid == 0 || TC_H_MAJ(classid ^ sch->handle) != 0)
		return -EINVAL;
	if (hfsc_find_class(classid, sch))
		return -EEXIST;

	if (rsc == NULL && fsc == NULL)
		return -EINVAL;

	cl = kzalloc(sizeof(struct hfsc_class), GFP_KERNEL);
	if (cl == NULL)
		return -ENOBUFS;

	RB_CLEAR_NODE(&cl->el_node);

	err = tcf_block_get(&cl->block, &cl->filter_list, sch, extack);
	if (err) {
		kfree(cl);
		return err;
	}

	if (tca[TCA_RATE]) {
		err = gen_new_estimator(&cl->bstats, NULL, &cl->rate_est,
					NULL, true, tca[TCA_RATE]);
		if (err) {
			tcf_block_put(cl->block);
			kfree(cl);
			return err;
		}
	}

	if (rsc != NULL)
		hfsc_change_rsc(cl, rsc, 0);
	if (fsc != NULL)
		hfsc_change_fsc(cl, fsc);
	if (usc != NULL)
		hfsc_change_usc(cl, usc, 0);

	cl->cl_common.classid = classid;
	cl->sched     = q;
	cl->cl_parent = parent;
	cl->qdisc = qdisc_create_dflt(sch->dev_queue, &pfifo_qdisc_ops,
				      classid, NULL);
	if (cl->qdisc == NULL)
		cl->qdisc = &noop_qdisc;
	else
		qdisc_hash_add(cl->qdisc, true);
	INIT_LIST_HEAD(&cl->children);
	cl->vt_tree = RB_ROOT;
	cl->cf_tree = RB_ROOT;

	sch_tree_lock(sch);
	/* Check if the inner class is a misconfigured 'rt' */
	if (!(parent->cl_flags & HFSC_FSC) && parent != &q->root) {
		NL_SET_ERR_MSG(extack,
			       "Forced curve change on parent 'rt' to 'sc'");
		hfsc_upgrade_rt(parent);
	}
	qdisc_class_hash_insert(&q->clhash, &cl->cl_common);
	list_add_tail(&cl->siblings, &parent->children);
	if (parent->level == 0)
		qdisc_purge_queue(parent->qdisc);
	hfsc_adjust_levels(parent);
	sch_tree_unlock(sch);

	qdisc_class_hash_grow(sch, &q->clhash);

	*arg = (unsigned long)cl;
	return 0;
}

static void
hfsc_destroy_class(struct Qdisc *sch, struct hfsc_class *cl)
{
	struct hfsc_sched *q = qdisc_priv(sch);

	tcf_block_put(cl->block);
	qdisc_put(cl->qdisc);
	gen_kill_estimator(&cl->rate_est);
	if (cl != &q->root)
		kfree(cl);
}

static int
hfsc_delete_class(struct Qdisc *sch, unsigned long arg,
		  struct netlink_ext_ack *extack)
{
	struct hfsc_sched *q = qdisc_priv(sch);
	struct hfsc_class *cl = (struct hfsc_class *)arg;

	if (cl->level > 0 || qdisc_class_in_use(&cl->cl_common) ||
	    cl == &q->root) {
		NL_SET_ERR_MSG(extack, "HFSC class in use");
		return -EBUSY;
	}

	sch_tree_lock(sch);

	list_del(&cl->siblings);
	hfsc_adjust_levels(cl->cl_parent);

	qdisc_purge_queue(cl->qdisc);
	qdisc_class_hash_remove(&q->clhash, &cl->cl_common);

	sch_tree_unlock(sch);

	hfsc_destroy_class(sch, cl);
	return 0;
}

static struct hfsc_class *
hfsc_classify(struct sk_buff *skb, struct Qdisc *sch, int *qerr)
{
	struct hfsc_sched *q = qdisc_priv(sch);
	struct hfsc_class *head, *cl;
	struct tcf_result res;
	struct tcf_proto *tcf;
	int result;

	if (TC_H_MAJ(skb->priority ^ sch->handle) == 0 &&
	    (cl = hfsc_find_class(skb->priority, sch)) != NULL)
		if (cl->level == 0)
			return cl;

	*qerr = NET_XMIT_SUCCESS | __NET_XMIT_BYPASS;
	head = &q->root;
	tcf = rcu_dereference_bh(q->root.filter_list);
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
		cl = (struct hfsc_class *)res.class;
		if (!cl) {
			cl = hfsc_find_class(res.classid, sch);
			if (!cl)
				break; /* filter selected invalid classid */
			if (cl->level >= head->level)
				break; /* filter may only point downwards */
		}

		if (cl->level == 0)
			return cl; /* hit leaf class */

		/* apply inner filter chain */
		tcf = rcu_dereference_bh(cl->filter_list);
		head = cl;
	}

	/* classification failed, try default class */
	cl = hfsc_find_class(TC_H_MAKE(TC_H_MAJ(sch->handle),
				       READ_ONCE(q->defcls)), sch);
	if (cl == NULL || cl->level > 0)
		return NULL;

	return cl;
}

static int
hfsc_graft_class(struct Qdisc *sch, unsigned long arg, struct Qdisc *new,
		 struct Qdisc **old, struct netlink_ext_ack *extack)
{
	struct hfsc_class *cl = (struct hfsc_class *)arg;

	if (cl->level > 0)
		return -EINVAL;
	if (new == NULL) {
		new = qdisc_create_dflt(sch->dev_queue, &pfifo_qdisc_ops,
					cl->cl_common.classid, NULL);
		if (new == NULL)
			new = &noop_qdisc;
	}

	*old = qdisc_replace(sch, new, &cl->qdisc);
	return 0;
}

static struct Qdisc *
hfsc_class_leaf(struct Qdisc *sch, unsigned long arg)
{
	struct hfsc_class *cl = (struct hfsc_class *)arg;

	if (cl->level == 0)
		return cl->qdisc;

	return NULL;
}

static void
hfsc_qlen_notify(struct Qdisc *sch, unsigned long arg)
{
	struct hfsc_class *cl = (struct hfsc_class *)arg;

	/* vttree is now handled in update_vf() so that update_vf(cl, 0, 0)
	 * needs to be called explicitly to remove a class from vttree.
	 */
	if (cl->cl_nactive)
		update_vf(cl, 0, 0);
	if (cl->cl_flags & HFSC_RSC)
		eltree_remove(cl);
}

static unsigned long
hfsc_search_class(struct Qdisc *sch, u32 classid)
{
	return (unsigned long)hfsc_find_class(classid, sch);
}

static unsigned long
hfsc_bind_tcf(struct Qdisc *sch, unsigned long parent, u32 classid)
{
	struct hfsc_class *p = (struct hfsc_class *)parent;
	struct hfsc_class *cl = hfsc_find_class(classid, sch);

	if (cl != NULL) {
		if (p != NULL && p->level <= cl->level)
			return 0;
		qdisc_class_get(&cl->cl_common);
	}

	return (unsigned long)cl;
}

static void
hfsc_unbind_tcf(struct Qdisc *sch, unsigned long arg)
{
	struct hfsc_class *cl = (struct hfsc_class *)arg;

	qdisc_class_put(&cl->cl_common);
}

static struct tcf_block *hfsc_tcf_block(struct Qdisc *sch, unsigned long arg,
					struct netlink_ext_ack *extack)
{
	struct hfsc_sched *q = qdisc_priv(sch);
	struct hfsc_class *cl = (struct hfsc_class *)arg;

	if (cl == NULL)
		cl = &q->root;

	return cl->block;
}

static int
hfsc_dump_sc(struct sk_buff *skb, int attr, struct internal_sc *sc)
{
	struct tc_service_curve tsc;

	tsc.m1 = sm2m(sc->sm1);
	tsc.d  = dx2d(sc->dx);
	tsc.m2 = sm2m(sc->sm2);
	if (nla_put(skb, attr, sizeof(tsc), &tsc))
		goto nla_put_failure;

	return skb->len;

 nla_put_failure:
	return -1;
}

static int
hfsc_dump_curves(struct sk_buff *skb, struct hfsc_class *cl)
{
	if ((cl->cl_flags & HFSC_RSC) &&
	    (hfsc_dump_sc(skb, TCA_HFSC_RSC, &cl->cl_rsc) < 0))
		goto nla_put_failure;

	if ((cl->cl_flags & HFSC_FSC) &&
	    (hfsc_dump_sc(skb, TCA_HFSC_FSC, &cl->cl_fsc) < 0))
		goto nla_put_failure;

	if ((cl->cl_flags & HFSC_USC) &&
	    (hfsc_dump_sc(skb, TCA_HFSC_USC, &cl->cl_usc) < 0))
		goto nla_put_failure;

	return skb->len;

 nla_put_failure:
	return -1;
}

static int
hfsc_dump_class(struct Qdisc *sch, unsigned long arg, struct sk_buff *skb,
		struct tcmsg *tcm)
{
	struct hfsc_class *cl = (struct hfsc_class *)arg;
	struct nlattr *nest;

	tcm->tcm_parent = cl->cl_parent ? cl->cl_parent->cl_common.classid :
					  TC_H_ROOT;
	tcm->tcm_handle = cl->cl_common.classid;
	if (cl->level == 0)
		tcm->tcm_info = cl->qdisc->handle;

	nest = nla_nest_start_noflag(skb, TCA_OPTIONS);
	if (nest == NULL)
		goto nla_put_failure;
	if (hfsc_dump_curves(skb, cl) < 0)
		goto nla_put_failure;
	return nla_nest_end(skb, nest);

 nla_put_failure:
	nla_nest_cancel(skb, nest);
	return -EMSGSIZE;
}

static int
hfsc_dump_class_stats(struct Qdisc *sch, unsigned long arg,
	struct gnet_dump *d)
{
	struct hfsc_class *cl = (struct hfsc_class *)arg;
	struct tc_hfsc_stats xstats;
	__u32 qlen;

	qdisc_qstats_qlen_backlog(cl->qdisc, &qlen, &cl->qstats.backlog);
	xstats.level   = cl->level;
	xstats.period  = cl->cl_vtperiod;
	xstats.work    = cl->cl_total;
	xstats.rtwork  = cl->cl_cumul;

	if (gnet_stats_copy_basic(d, NULL, &cl->bstats, true) < 0 ||
	    gnet_stats_copy_rate_est(d, &cl->rate_est) < 0 ||
	    gnet_stats_copy_queue(d, NULL, &cl->qstats, qlen) < 0)
		return -1;

	return gnet_stats_copy_app(d, &xstats, sizeof(xstats));
}



static void
hfsc_walk(struct Qdisc *sch, struct qdisc_walker *arg)
{
	struct hfsc_sched *q = qdisc_priv(sch);
	struct hfsc_class *cl;
	unsigned int i;

	if (arg->stop)
		return;

	for (i = 0; i < q->clhash.hashsize; i++) {
		hlist_for_each_entry(cl, &q->clhash.hash[i],
				     cl_common.hnode) {
			if (!tc_qdisc_stats_dump(sch, (unsigned long)cl, arg))
				return;
		}
	}
}

static void
hfsc_schedule_watchdog(struct Qdisc *sch)
{
	struct hfsc_sched *q = qdisc_priv(sch);
	struct hfsc_class *cl;
	u64 next_time = 0;

	cl = eltree_get_minel(q);
	if (cl)
		next_time = cl->cl_e;
	if (q->root.cl_cfmin != 0) {
		if (next_time == 0 || next_time > q->root.cl_cfmin)
			next_time = q->root.cl_cfmin;
	}
	if (next_time)
		qdisc_watchdog_schedule(&q->watchdog, next_time);
}

static int
hfsc_init_qdisc(struct Qdisc *sch, struct nlattr *opt,
		struct netlink_ext_ack *extack)
{
	struct hfsc_sched *q = qdisc_priv(sch);
	struct tc_hfsc_qopt *qopt;
	int err;

	qdisc_watchdog_init(&q->watchdog, sch);

	if (!opt || nla_len(opt) < sizeof(*qopt))
		return -EINVAL;
	qopt = nla_data(opt);

	q->defcls = qopt->defcls;
	err = qdisc_class_hash_init(&q->clhash);
	if (err < 0)
		return err;
	q->eligible = RB_ROOT;

	err = tcf_block_get(&q->root.block, &q->root.filter_list, sch, extack);
	if (err)
		return err;

	gnet_stats_basic_sync_init(&q->root.bstats);
	q->root.cl_common.classid = sch->handle;
	q->root.sched   = q;
	q->root.qdisc = qdisc_create_dflt(sch->dev_queue, &pfifo_qdisc_ops,
					  sch->handle, NULL);
	if (q->root.qdisc == NULL)
		q->root.qdisc = &noop_qdisc;
	else
		qdisc_hash_add(q->root.qdisc, true);
	INIT_LIST_HEAD(&q->root.children);
	q->root.vt_tree = RB_ROOT;
	q->root.cf_tree = RB_ROOT;

	qdisc_class_hash_insert(&q->clhash, &q->root.cl_common);
	qdisc_class_hash_grow(sch, &q->clhash);

	return 0;
}

static int
hfsc_change_qdisc(struct Qdisc *sch, struct nlattr *opt,
		  struct netlink_ext_ack *extack)
{
	struct hfsc_sched *q = qdisc_priv(sch);
	struct tc_hfsc_qopt *qopt;

	if (nla_len(opt) < sizeof(*qopt))
		return -EINVAL;
	qopt = nla_data(opt);

	WRITE_ONCE(q->defcls, qopt->defcls);

	return 0;
}

static void
hfsc_reset_class(struct hfsc_class *cl)
{
	cl->cl_total        = 0;
	cl->cl_cumul        = 0;
	cl->cl_d            = 0;
	cl->cl_e            = 0;
	cl->cl_vt           = 0;
	cl->cl_vtadj        = 0;
	cl->cl_cvtmin       = 0;
	cl->cl_cvtoff       = 0;
	cl->cl_vtperiod     = 0;
	cl->cl_parentperiod = 0;
	cl->cl_f            = 0;
	cl->cl_myf          = 0;
	cl->cl_cfmin        = 0;
	cl->cl_nactive      = 0;

	cl->vt_tree = RB_ROOT;
	cl->cf_tree = RB_ROOT;
	qdisc_reset(cl->qdisc);

	if (cl->cl_flags & HFSC_RSC)
		rtsc_init(&cl->cl_deadline, &cl->cl_rsc, 0, 0);
	if (cl->cl_flags & HFSC_FSC)
		rtsc_init(&cl->cl_virtual, &cl->cl_fsc, 0, 0);
	if (cl->cl_flags & HFSC_USC)
		rtsc_init(&cl->cl_ulimit, &cl->cl_usc, 0, 0);
}

static void
hfsc_reset_qdisc(struct Qdisc *sch)
{
	struct hfsc_sched *q = qdisc_priv(sch);
	struct hfsc_class *cl;
	unsigned int i;

	for (i = 0; i < q->clhash.hashsize; i++) {
		hlist_for_each_entry(cl, &q->clhash.hash[i], cl_common.hnode)
			hfsc_reset_class(cl);
	}
	q->eligible = RB_ROOT;
	qdisc_watchdog_cancel(&q->watchdog);
}

static void
hfsc_destroy_qdisc(struct Qdisc *sch)
{
	struct hfsc_sched *q = qdisc_priv(sch);
	struct hlist_node *next;
	struct hfsc_class *cl;
	unsigned int i;

	for (i = 0; i < q->clhash.hashsize; i++) {
		hlist_for_each_entry(cl, &q->clhash.hash[i], cl_common.hnode) {
			tcf_block_put(cl->block);
			cl->block = NULL;
		}
	}
	for (i = 0; i < q->clhash.hashsize; i++) {
		hlist_for_each_entry_safe(cl, next, &q->clhash.hash[i],
					  cl_common.hnode)
			hfsc_destroy_class(sch, cl);
	}
	qdisc_class_hash_destroy(&q->clhash);
	qdisc_watchdog_cancel(&q->watchdog);
}

static int
hfsc_dump_qdisc(struct Qdisc *sch, struct sk_buff *skb)
{
	struct hfsc_sched *q = qdisc_priv(sch);
	unsigned char *b = skb_tail_pointer(skb);
	struct tc_hfsc_qopt qopt;

	qopt.defcls = READ_ONCE(q->defcls);
	if (nla_put(skb, TCA_OPTIONS, sizeof(qopt), &qopt))
		goto nla_put_failure;
	return skb->len;

 nla_put_failure:
	nlmsg_trim(skb, b);
	return -1;
}

static int
hfsc_enqueue(struct sk_buff *skb, struct Qdisc *sch, struct sk_buff **to_free)
{
	unsigned int len = qdisc_pkt_len(skb);
	struct hfsc_class *cl;
	int err;
	bool first;

	cl = hfsc_classify(skb, sch, &err);
	if (cl == NULL) {
		if (err & __NET_XMIT_BYPASS)
			qdisc_qstats_drop(sch);
		__qdisc_drop(skb, to_free);
		return err;
	}

	first = !cl->qdisc->q.qlen;
	err = qdisc_enqueue(skb, cl->qdisc, to_free);
	if (unlikely(err != NET_XMIT_SUCCESS)) {
		if (net_xmit_drop_count(err)) {
			cl->qstats.drops++;
			qdisc_qstats_drop(sch);
		}
		return err;
	}

	sch->qstats.backlog += len;
	sch->q.qlen++;

	if (first && !cl_in_el_or_vttree(cl)) {
		if (cl->cl_flags & HFSC_RSC)
			init_ed(cl, len);
		if (cl->cl_flags & HFSC_FSC)
			init_vf(cl, len);
		/*
		 * If this is the first packet, isolate the head so an eventual
		 * head drop before the first dequeue operation has no chance
		 * to invalidate the deadline.
		 */
		if (cl->cl_flags & HFSC_RSC)
			cl->qdisc->ops->peek(cl->qdisc);

	}

	return NET_XMIT_SUCCESS;
}

static struct sk_buff *
hfsc_dequeue(struct Qdisc *sch)
{
	struct hfsc_sched *q = qdisc_priv(sch);
	struct hfsc_class *cl;
	struct sk_buff *skb;
	u64 cur_time;
	unsigned int next_len;
	int realtime = 0;

	if (sch->q.qlen == 0)
		return NULL;

	cur_time = psched_get_time();

	/*
	 * if there are eligible classes, use real-time criteria.
	 * find the class with the minimum deadline among
	 * the eligible classes.
	 */
	cl = eltree_get_mindl(q, cur_time);
	if (cl) {
		realtime = 1;
	} else {
		/*
		 * use link-sharing criteria
		 * get the class with the minimum vt in the hierarchy
		 */
		cl = vttree_get_minvt(&q->root, cur_time);
		if (cl == NULL) {
			qdisc_qstats_overlimit(sch);
			hfsc_schedule_watchdog(sch);
			return NULL;
		}
	}

	skb = qdisc_dequeue_peeked(cl->qdisc);
	if (skb == NULL) {
		qdisc_warn_nonwc("HFSC", cl->qdisc);
		return NULL;
	}

	bstats_update(&cl->bstats, skb);
	update_vf(cl, qdisc_pkt_len(skb), cur_time);
	if (realtime)
		cl->cl_cumul += qdisc_pkt_len(skb);

	if (cl->cl_flags & HFSC_RSC) {
		if (cl->qdisc->q.qlen != 0) {
			/* update ed */
			next_len = qdisc_peek_len(cl->qdisc);
			/* Check queue length again since some qdisc implementations
			 * (e.g., netem/codel) might empty the queue during the peek
			 * operation.
			 */
			if (cl->qdisc->q.qlen != 0) {
				if (realtime)
					update_ed(cl, next_len);
				else
					update_d(cl, next_len);
			}
		} else {
			/* the class becomes passive */
			eltree_remove(cl);
		}
	}

	qdisc_bstats_update(sch, skb);
	qdisc_qstats_backlog_dec(sch, skb);
	sch->q.qlen--;

	return skb;
}

static const struct Qdisc_class_ops hfsc_class_ops = {
	.change		= hfsc_change_class,
	.delete		= hfsc_delete_class,
	.graft		= hfsc_graft_class,
	.leaf		= hfsc_class_leaf,
	.qlen_notify	= hfsc_qlen_notify,
	.find		= hfsc_search_class,
	.bind_tcf	= hfsc_bind_tcf,
	.unbind_tcf	= hfsc_unbind_tcf,
	.tcf_block	= hfsc_tcf_block,
	.dump		= hfsc_dump_class,
	.dump_stats	= hfsc_dump_class_stats,
	.walk		= hfsc_walk
};

static struct Qdisc_ops hfsc_qdisc_ops __read_mostly = {
	.id		= "hfsc",
	.init		= hfsc_init_qdisc,
	.change		= hfsc_change_qdisc,
	.reset		= hfsc_reset_qdisc,
	.destroy	= hfsc_destroy_qdisc,
	.dump		= hfsc_dump_qdisc,
	.enqueue	= hfsc_enqueue,
	.dequeue	= hfsc_dequeue,
	.peek		= qdisc_peek_dequeued,
	.cl_ops		= &hfsc_class_ops,
	.priv_size	= sizeof(struct hfsc_sched),
	.owner		= THIS_MODULE
};
MODULE_ALIAS_NET_SCH("hfsc");

static int __init
hfsc_init(void)
{
	return register_qdisc(&hfsc_qdisc_ops);
}

static void __exit
hfsc_cleanup(void)
{
	unregister_qdisc(&hfsc_qdisc_ops);
}

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Hierarchical Fair Service Curve scheduler");
module_init(hfsc_init);
module_exit(hfsc_cleanup);
