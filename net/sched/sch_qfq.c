/*
 * net/sched/sch_qfq.c         Quick Fair Queueing Scheduler.
 *
 * Copyright (c) 2009 Fabio Checconi, Luigi Rizzo, and Paolo Valente.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * version 2 as published by the Free Software Foundation.
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/bitops.h>
#include <linux/errno.h>
#include <linux/netdevice.h>
#include <linux/pkt_sched.h>
#include <net/sch_generic.h>
#include <net/pkt_sched.h>
#include <net/pkt_cls.h>


/*  Quick Fair Queueing
    ===================

    Sources:

    Fabio Checconi, Luigi Rizzo, and Paolo Valente: "QFQ: Efficient
    Packet Scheduling with Tight Bandwidth Distribution Guarantees."

    See also:
    http://retis.sssup.it/~fabio/linux/qfq/
 */

/*

  Virtual time computations.

  S, F and V are all computed in fixed point arithmetic with
  FRAC_BITS decimal bits.

  QFQ_MAX_INDEX is the maximum index allowed for a group. We need
	one bit per index.
  QFQ_MAX_WSHIFT is the maximum power of two supported as a weight.

  The layout of the bits is as below:

                   [ MTU_SHIFT ][      FRAC_BITS    ]
                   [ MAX_INDEX    ][ MIN_SLOT_SHIFT ]
				 ^.__grp->index = 0
				 *.__grp->slot_shift

  where MIN_SLOT_SHIFT is derived by difference from the others.

  The max group index corresponds to Lmax/w_min, where
  Lmax=1<<MTU_SHIFT, w_min = 1 .
  From this, and knowing how many groups (MAX_INDEX) we want,
  we can derive the shift corresponding to each group.

  Because we often need to compute
	F = S + len/w_i  and V = V + len/wsum
  instead of storing w_i store the value
	inv_w = (1<<FRAC_BITS)/w_i
  so we can do F = S + len * inv_w * wsum.
  We use W_TOT in the formulas so we can easily move between
  static and adaptive weight sum.

  The per-scheduler-instance data contain all the data structures
  for the scheduler: bitmaps and bucket lists.

 */

/*
 * Maximum number of consecutive slots occupied by backlogged classes
 * inside a group.
 */
#define QFQ_MAX_SLOTS	32

/*
 * Shifts used for class<->group mapping.  We allow class weights that are
 * in the range [1, 2^MAX_WSHIFT], and we try to map each class i to the
 * group with the smallest index that can support the L_i / r_i configured
 * for the class.
 *
 * grp->index is the index of the group; and grp->slot_shift
 * is the shift for the corresponding (scaled) sigma_i.
 */
#define QFQ_MAX_INDEX		24
#define QFQ_MAX_WSHIFT		12

#define	QFQ_MAX_WEIGHT		(1<<QFQ_MAX_WSHIFT)
#define QFQ_MAX_WSUM		(16*QFQ_MAX_WEIGHT)

#define FRAC_BITS		30	/* fixed point arithmetic */
#define ONE_FP			(1UL << FRAC_BITS)
#define IWSUM			(ONE_FP/QFQ_MAX_WSUM)

#define QFQ_MTU_SHIFT		16	/* to support TSO/GSO */
#define QFQ_MIN_SLOT_SHIFT	(FRAC_BITS + QFQ_MTU_SHIFT - QFQ_MAX_INDEX)
#define QFQ_MIN_LMAX		256	/* min possible lmax for a class */

/*
 * Possible group states.  These values are used as indexes for the bitmaps
 * array of struct qfq_queue.
 */
enum qfq_state { ER, IR, EB, IB, QFQ_MAX_STATE };

struct qfq_group;

struct qfq_class {
	struct Qdisc_class_common common;

	unsigned int refcnt;
	unsigned int filter_cnt;

	struct gnet_stats_basic_packed bstats;
	struct gnet_stats_queue qstats;
	struct gnet_stats_rate_est rate_est;
	struct Qdisc *qdisc;

	struct hlist_node next;	/* Link for the slot list. */
	u64 S, F;		/* flow timestamps (exact) */

	/* group we belong to. In principle we would need the index,
	 * which is log_2(lmax/weight), but we never reference it
	 * directly, only the group.
	 */
	struct qfq_group *grp;

	/* these are copied from the flowset. */
	u32	inv_w;		/* ONE_FP/weight */
	u32	lmax;		/* Max packet size for this flow. */
};

struct qfq_group {
	u64 S, F;			/* group timestamps (approx). */
	unsigned int slot_shift;	/* Slot shift. */
	unsigned int index;		/* Group index. */
	unsigned int front;		/* Index of the front slot. */
	unsigned long full_slots;	/* non-empty slots */

	/* Array of RR lists of active classes. */
	struct hlist_head slots[QFQ_MAX_SLOTS];
};

struct qfq_sched {
	struct tcf_proto *filter_list;
	struct Qdisc_class_hash clhash;

	u64		V;		/* Precise virtual time. */
	u32		wsum;		/* weight sum */

	unsigned long bitmaps[QFQ_MAX_STATE];	    /* Group bitmaps. */
	struct qfq_group groups[QFQ_MAX_INDEX + 1]; /* The groups. */
};

static struct qfq_class *qfq_find_class(struct Qdisc *sch, u32 classid)
{
	struct qfq_sched *q = qdisc_priv(sch);
	struct Qdisc_class_common *clc;

	clc = qdisc_class_find(&q->clhash, classid);
	if (clc == NULL)
		return NULL;
	return container_of(clc, struct qfq_class, common);
}

static void qfq_purge_queue(struct qfq_class *cl)
{
	unsigned int len = cl->qdisc->q.qlen;

	qdisc_reset(cl->qdisc);
	qdisc_tree_decrease_qlen(cl->qdisc, len);
}

static const struct nla_policy qfq_policy[TCA_QFQ_MAX + 1] = {
	[TCA_QFQ_WEIGHT] = { .type = NLA_U32 },
	[TCA_QFQ_LMAX] = { .type = NLA_U32 },
};

/*
 * Calculate a flow index, given its weight and maximum packet length.
 * index = log_2(maxlen/weight) but we need to apply the scaling.
 * This is used only once at flow creation.
 */
static int qfq_calc_index(u32 inv_w, unsigned int maxlen)
{
	u64 slot_size = (u64)maxlen * inv_w;
	unsigned long size_map;
	int index = 0;

	size_map = slot_size >> QFQ_MIN_SLOT_SHIFT;
	if (!size_map)
		goto out;

	index = __fls(size_map) + 1;	/* basically a log_2 */
	index -= !(slot_size - (1ULL << (index + QFQ_MIN_SLOT_SHIFT - 1)));

	if (index < 0)
		index = 0;
out:
	pr_debug("qfq calc_index: W = %lu, L = %u, I = %d\n",
		 (unsigned long) ONE_FP/inv_w, maxlen, index);

	return index;
}

/* Length of the next packet (0 if the queue is empty). */
static unsigned int qdisc_peek_len(struct Qdisc *sch)
{
	struct sk_buff *skb;

	skb = sch->ops->peek(sch);
	return skb ? qdisc_pkt_len(skb) : 0;
}

static void qfq_deactivate_class(struct qfq_sched *, struct qfq_class *);
static void qfq_activate_class(struct qfq_sched *q, struct qfq_class *cl,
			       unsigned int len);

static void qfq_update_class_params(struct qfq_sched *q, struct qfq_class *cl,
				    u32 lmax, u32 inv_w, int delta_w)
{
	int i;

	/* update qfq-specific data */
	cl->lmax = lmax;
	cl->inv_w = inv_w;
	i = qfq_calc_index(cl->inv_w, cl->lmax);

	cl->grp = &q->groups[i];

	q->wsum += delta_w;
}

static void qfq_update_reactivate_class(struct qfq_sched *q,
					struct qfq_class *cl,
					u32 inv_w, u32 lmax, int delta_w)
{
	bool need_reactivation = false;
	int i = qfq_calc_index(inv_w, lmax);

	if (&q->groups[i] != cl->grp && cl->qdisc->q.qlen > 0) {
		/*
		 * shift cl->F back, to not charge the
		 * class for the not-yet-served head
		 * packet
		 */
		cl->F = cl->S;
		/* remove class from its slot in the old group */
		qfq_deactivate_class(q, cl);
		need_reactivation = true;
	}

	qfq_update_class_params(q, cl, lmax, inv_w, delta_w);

	if (need_reactivation) /* activate in new group */
		qfq_activate_class(q, cl, qdisc_peek_len(cl->qdisc));
}


static int qfq_change_class(struct Qdisc *sch, u32 classid, u32 parentid,
			    struct nlattr **tca, unsigned long *arg)
{
	struct qfq_sched *q = qdisc_priv(sch);
	struct qfq_class *cl = (struct qfq_class *)*arg;
	struct nlattr *tb[TCA_QFQ_MAX + 1];
	u32 weight, lmax, inv_w;
	int err;
	int delta_w;

	if (tca[TCA_OPTIONS] == NULL) {
		pr_notice("qfq: no options\n");
		return -EINVAL;
	}

	err = nla_parse_nested(tb, TCA_QFQ_MAX, tca[TCA_OPTIONS], qfq_policy);
	if (err < 0)
		return err;

	if (tb[TCA_QFQ_WEIGHT]) {
		weight = nla_get_u32(tb[TCA_QFQ_WEIGHT]);
		if (!weight || weight > (1UL << QFQ_MAX_WSHIFT)) {
			pr_notice("qfq: invalid weight %u\n", weight);
			return -EINVAL;
		}
	} else
		weight = 1;

	inv_w = ONE_FP / weight;
	weight = ONE_FP / inv_w;
	delta_w = weight - (cl ? ONE_FP / cl->inv_w : 0);
	if (q->wsum + delta_w > QFQ_MAX_WSUM) {
		pr_notice("qfq: total weight out of range (%u + %u)\n",
			  delta_w, q->wsum);
		return -EINVAL;
	}

	if (tb[TCA_QFQ_LMAX]) {
		lmax = nla_get_u32(tb[TCA_QFQ_LMAX]);
		if (lmax < QFQ_MIN_LMAX || lmax > (1UL << QFQ_MTU_SHIFT)) {
			pr_notice("qfq: invalid max length %u\n", lmax);
			return -EINVAL;
		}
	} else
		lmax = psched_mtu(qdisc_dev(sch));

	if (cl != NULL) {
		if (tca[TCA_RATE]) {
			err = gen_replace_estimator(&cl->bstats, &cl->rate_est,
						    qdisc_root_sleeping_lock(sch),
						    tca[TCA_RATE]);
			if (err)
				return err;
		}

		if (lmax == cl->lmax && inv_w == cl->inv_w)
			return 0; /* nothing to update */

		sch_tree_lock(sch);
		qfq_update_reactivate_class(q, cl, inv_w, lmax, delta_w);
		sch_tree_unlock(sch);

		return 0;
	}

	cl = kzalloc(sizeof(struct qfq_class), GFP_KERNEL);
	if (cl == NULL)
		return -ENOBUFS;

	cl->refcnt = 1;
	cl->common.classid = classid;

	qfq_update_class_params(q, cl, lmax, inv_w, delta_w);

	cl->qdisc = qdisc_create_dflt(sch->dev_queue,
				      &pfifo_qdisc_ops, classid);
	if (cl->qdisc == NULL)
		cl->qdisc = &noop_qdisc;

	if (tca[TCA_RATE]) {
		err = gen_new_estimator(&cl->bstats, &cl->rate_est,
					qdisc_root_sleeping_lock(sch),
					tca[TCA_RATE]);
		if (err) {
			qdisc_destroy(cl->qdisc);
			kfree(cl);
			return err;
		}
	}

	sch_tree_lock(sch);
	qdisc_class_hash_insert(&q->clhash, &cl->common);
	sch_tree_unlock(sch);

	qdisc_class_hash_grow(sch, &q->clhash);

	*arg = (unsigned long)cl;
	return 0;
}

static void qfq_destroy_class(struct Qdisc *sch, struct qfq_class *cl)
{
	struct qfq_sched *q = qdisc_priv(sch);

	if (cl->inv_w) {
		q->wsum -= ONE_FP / cl->inv_w;
		cl->inv_w = 0;
	}

	gen_kill_estimator(&cl->bstats, &cl->rate_est);
	qdisc_destroy(cl->qdisc);
	kfree(cl);
}

static int qfq_delete_class(struct Qdisc *sch, unsigned long arg)
{
	struct qfq_sched *q = qdisc_priv(sch);
	struct qfq_class *cl = (struct qfq_class *)arg;

	if (cl->filter_cnt > 0)
		return -EBUSY;

	sch_tree_lock(sch);

	qfq_purge_queue(cl);
	qdisc_class_hash_remove(&q->clhash, &cl->common);

	BUG_ON(--cl->refcnt == 0);
	/*
	 * This shouldn't happen: we "hold" one cops->get() when called
	 * from tc_ctl_tclass; the destroy method is done from cops->put().
	 */

	sch_tree_unlock(sch);
	return 0;
}

static unsigned long qfq_get_class(struct Qdisc *sch, u32 classid)
{
	struct qfq_class *cl = qfq_find_class(sch, classid);

	if (cl != NULL)
		cl->refcnt++;

	return (unsigned long)cl;
}

static void qfq_put_class(struct Qdisc *sch, unsigned long arg)
{
	struct qfq_class *cl = (struct qfq_class *)arg;

	if (--cl->refcnt == 0)
		qfq_destroy_class(sch, cl);
}

static struct tcf_proto **qfq_tcf_chain(struct Qdisc *sch, unsigned long cl)
{
	struct qfq_sched *q = qdisc_priv(sch);

	if (cl)
		return NULL;

	return &q->filter_list;
}

static unsigned long qfq_bind_tcf(struct Qdisc *sch, unsigned long parent,
				  u32 classid)
{
	struct qfq_class *cl = qfq_find_class(sch, classid);

	if (cl != NULL)
		cl->filter_cnt++;

	return (unsigned long)cl;
}

static void qfq_unbind_tcf(struct Qdisc *sch, unsigned long arg)
{
	struct qfq_class *cl = (struct qfq_class *)arg;

	cl->filter_cnt--;
}

static int qfq_graft_class(struct Qdisc *sch, unsigned long arg,
			   struct Qdisc *new, struct Qdisc **old)
{
	struct qfq_class *cl = (struct qfq_class *)arg;

	if (new == NULL) {
		new = qdisc_create_dflt(sch->dev_queue,
					&pfifo_qdisc_ops, cl->common.classid);
		if (new == NULL)
			new = &noop_qdisc;
	}

	sch_tree_lock(sch);
	qfq_purge_queue(cl);
	*old = cl->qdisc;
	cl->qdisc = new;
	sch_tree_unlock(sch);
	return 0;
}

static struct Qdisc *qfq_class_leaf(struct Qdisc *sch, unsigned long arg)
{
	struct qfq_class *cl = (struct qfq_class *)arg;

	return cl->qdisc;
}

static int qfq_dump_class(struct Qdisc *sch, unsigned long arg,
			  struct sk_buff *skb, struct tcmsg *tcm)
{
	struct qfq_class *cl = (struct qfq_class *)arg;
	struct nlattr *nest;

	tcm->tcm_parent	= TC_H_ROOT;
	tcm->tcm_handle	= cl->common.classid;
	tcm->tcm_info	= cl->qdisc->handle;

	nest = nla_nest_start(skb, TCA_OPTIONS);
	if (nest == NULL)
		goto nla_put_failure;
	if (nla_put_u32(skb, TCA_QFQ_WEIGHT, ONE_FP/cl->inv_w) ||
	    nla_put_u32(skb, TCA_QFQ_LMAX, cl->lmax))
		goto nla_put_failure;
	return nla_nest_end(skb, nest);

nla_put_failure:
	nla_nest_cancel(skb, nest);
	return -EMSGSIZE;
}

static int qfq_dump_class_stats(struct Qdisc *sch, unsigned long arg,
				struct gnet_dump *d)
{
	struct qfq_class *cl = (struct qfq_class *)arg;
	struct tc_qfq_stats xstats;

	memset(&xstats, 0, sizeof(xstats));
	cl->qdisc->qstats.qlen = cl->qdisc->q.qlen;

	xstats.weight = ONE_FP/cl->inv_w;
	xstats.lmax = cl->lmax;

	if (gnet_stats_copy_basic(d, &cl->bstats) < 0 ||
	    gnet_stats_copy_rate_est(d, &cl->bstats, &cl->rate_est) < 0 ||
	    gnet_stats_copy_queue(d, &cl->qdisc->qstats) < 0)
		return -1;

	return gnet_stats_copy_app(d, &xstats, sizeof(xstats));
}

static void qfq_walk(struct Qdisc *sch, struct qdisc_walker *arg)
{
	struct qfq_sched *q = qdisc_priv(sch);
	struct qfq_class *cl;
	struct hlist_node *n;
	unsigned int i;

	if (arg->stop)
		return;

	for (i = 0; i < q->clhash.hashsize; i++) {
		hlist_for_each_entry(cl, n, &q->clhash.hash[i], common.hnode) {
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

static struct qfq_class *qfq_classify(struct sk_buff *skb, struct Qdisc *sch,
				      int *qerr)
{
	struct qfq_sched *q = qdisc_priv(sch);
	struct qfq_class *cl;
	struct tcf_result res;
	int result;

	if (TC_H_MAJ(skb->priority ^ sch->handle) == 0) {
		pr_debug("qfq_classify: found %d\n", skb->priority);
		cl = qfq_find_class(sch, skb->priority);
		if (cl != NULL)
			return cl;
	}

	*qerr = NET_XMIT_SUCCESS | __NET_XMIT_BYPASS;
	result = tc_classify(skb, q->filter_list, &res);
	if (result >= 0) {
#ifdef CONFIG_NET_CLS_ACT
		switch (result) {
		case TC_ACT_QUEUED:
		case TC_ACT_STOLEN:
			*qerr = NET_XMIT_SUCCESS | __NET_XMIT_STOLEN;
		case TC_ACT_SHOT:
			return NULL;
		}
#endif
		cl = (struct qfq_class *)res.class;
		if (cl == NULL)
			cl = qfq_find_class(sch, res.classid);
		return cl;
	}

	return NULL;
}

/* Generic comparison function, handling wraparound. */
static inline int qfq_gt(u64 a, u64 b)
{
	return (s64)(a - b) > 0;
}

/* Round a precise timestamp to its slotted value. */
static inline u64 qfq_round_down(u64 ts, unsigned int shift)
{
	return ts & ~((1ULL << shift) - 1);
}

/* return the pointer to the group with lowest index in the bitmap */
static inline struct qfq_group *qfq_ffs(struct qfq_sched *q,
					unsigned long bitmap)
{
	int index = __ffs(bitmap);
	return &q->groups[index];
}
/* Calculate a mask to mimic what would be ffs_from(). */
static inline unsigned long mask_from(unsigned long bitmap, int from)
{
	return bitmap & ~((1UL << from) - 1);
}

/*
 * The state computation relies on ER=0, IR=1, EB=2, IB=3
 * First compute eligibility comparing grp->S, q->V,
 * then check if someone is blocking us and possibly add EB
 */
static int qfq_calc_state(struct qfq_sched *q, const struct qfq_group *grp)
{
	/* if S > V we are not eligible */
	unsigned int state = qfq_gt(grp->S, q->V);
	unsigned long mask = mask_from(q->bitmaps[ER], grp->index);
	struct qfq_group *next;

	if (mask) {
		next = qfq_ffs(q, mask);
		if (qfq_gt(grp->F, next->F))
			state |= EB;
	}

	return state;
}


/*
 * In principle
 *	q->bitmaps[dst] |= q->bitmaps[src] & mask;
 *	q->bitmaps[src] &= ~mask;
 * but we should make sure that src != dst
 */
static inline void qfq_move_groups(struct qfq_sched *q, unsigned long mask,
				   int src, int dst)
{
	q->bitmaps[dst] |= q->bitmaps[src] & mask;
	q->bitmaps[src] &= ~mask;
}

static void qfq_unblock_groups(struct qfq_sched *q, int index, u64 old_F)
{
	unsigned long mask = mask_from(q->bitmaps[ER], index + 1);
	struct qfq_group *next;

	if (mask) {
		next = qfq_ffs(q, mask);
		if (!qfq_gt(next->F, old_F))
			return;
	}

	mask = (1UL << index) - 1;
	qfq_move_groups(q, mask, EB, ER);
	qfq_move_groups(q, mask, IB, IR);
}

/*
 * perhaps
 *
	old_V ^= q->V;
	old_V >>= QFQ_MIN_SLOT_SHIFT;
	if (old_V) {
		...
	}
 *
 */
static void qfq_make_eligible(struct qfq_sched *q, u64 old_V)
{
	unsigned long vslot = q->V >> QFQ_MIN_SLOT_SHIFT;
	unsigned long old_vslot = old_V >> QFQ_MIN_SLOT_SHIFT;

	if (vslot != old_vslot) {
		unsigned long mask = (1UL << fls(vslot ^ old_vslot)) - 1;
		qfq_move_groups(q, mask, IR, ER);
		qfq_move_groups(q, mask, IB, EB);
	}
}


/*
 * If the weight and lmax (max_pkt_size) of the classes do not change,
 * then QFQ guarantees that the slot index is never higher than
 * 2 + ((1<<QFQ_MTU_SHIFT)/QFQ_MIN_LMAX) * (QFQ_MAX_WEIGHT/QFQ_MAX_WSUM).
 *
 * With the current values of the above constants, the index is
 * then guaranteed to never be higher than 2 + 256 * (1 / 16) = 18.
 *
 * When the weight of a class is increased or the lmax of the class is
 * decreased, a new class with smaller slot size may happen to be
 * activated. The activation of this class should be properly delayed
 * to when the service of the class has finished in the ideal system
 * tracked by QFQ. If the activation of the class is not delayed to
 * this reference time instant, then this class may be unjustly served
 * before other classes waiting for service. This may cause
 * (unfrequently) the above bound to the slot index to be violated for
 * some of these unlucky classes.
 *
 * Instead of delaying the activation of the new class, which is quite
 * complex, the following inaccurate but simple solution is used: if
 * the slot index is higher than QFQ_MAX_SLOTS-2, then the timestamps
 * of the class are shifted backward so as to let the slot index
 * become equal to QFQ_MAX_SLOTS-2. This threshold is used because, if
 * the slot index is above it, then the data structure implementing
 * the bucket list either gets immediately corrupted or may get
 * corrupted on a possible next packet arrival that causes the start
 * time of the group to be shifted backward.
 */
static void qfq_slot_insert(struct qfq_group *grp, struct qfq_class *cl,
			    u64 roundedS)
{
	u64 slot = (roundedS - grp->S) >> grp->slot_shift;
	unsigned int i; /* slot index in the bucket list */

	if (unlikely(slot > QFQ_MAX_SLOTS - 2)) {
		u64 deltaS = roundedS - grp->S -
			((u64)(QFQ_MAX_SLOTS - 2)<<grp->slot_shift);
		cl->S -= deltaS;
		cl->F -= deltaS;
		slot = QFQ_MAX_SLOTS - 2;
	}

	i = (grp->front + slot) % QFQ_MAX_SLOTS;

	hlist_add_head(&cl->next, &grp->slots[i]);
	__set_bit(slot, &grp->full_slots);
}

/* Maybe introduce hlist_first_entry?? */
static struct qfq_class *qfq_slot_head(struct qfq_group *grp)
{
	return hlist_entry(grp->slots[grp->front].first,
			   struct qfq_class, next);
}

/*
 * remove the entry from the slot
 */
static void qfq_front_slot_remove(struct qfq_group *grp)
{
	struct qfq_class *cl = qfq_slot_head(grp);

	BUG_ON(!cl);
	hlist_del(&cl->next);
	if (hlist_empty(&grp->slots[grp->front]))
		__clear_bit(0, &grp->full_slots);
}

/*
 * Returns the first full queue in a group. As a side effect,
 * adjust the bucket list so the first non-empty bucket is at
 * position 0 in full_slots.
 */
static struct qfq_class *qfq_slot_scan(struct qfq_group *grp)
{
	unsigned int i;

	pr_debug("qfq slot_scan: grp %u full %#lx\n",
		 grp->index, grp->full_slots);

	if (grp->full_slots == 0)
		return NULL;

	i = __ffs(grp->full_slots);  /* zero based */
	if (i > 0) {
		grp->front = (grp->front + i) % QFQ_MAX_SLOTS;
		grp->full_slots >>= i;
	}

	return qfq_slot_head(grp);
}

/*
 * adjust the bucket list. When the start time of a group decreases,
 * we move the index down (modulo QFQ_MAX_SLOTS) so we don't need to
 * move the objects. The mask of occupied slots must be shifted
 * because we use ffs() to find the first non-empty slot.
 * This covers decreases in the group's start time, but what about
 * increases of the start time ?
 * Here too we should make sure that i is less than 32
 */
static void qfq_slot_rotate(struct qfq_group *grp, u64 roundedS)
{
	unsigned int i = (grp->S - roundedS) >> grp->slot_shift;

	grp->full_slots <<= i;
	grp->front = (grp->front - i) % QFQ_MAX_SLOTS;
}

static void qfq_update_eligible(struct qfq_sched *q, u64 old_V)
{
	struct qfq_group *grp;
	unsigned long ineligible;

	ineligible = q->bitmaps[IR] | q->bitmaps[IB];
	if (ineligible) {
		if (!q->bitmaps[ER]) {
			grp = qfq_ffs(q, ineligible);
			if (qfq_gt(grp->S, q->V))
				q->V = grp->S;
		}
		qfq_make_eligible(q, old_V);
	}
}

/*
 * Updates the class, returns true if also the group needs to be updated.
 */
static bool qfq_update_class(struct qfq_group *grp, struct qfq_class *cl)
{
	unsigned int len = qdisc_peek_len(cl->qdisc);

	cl->S = cl->F;
	if (!len)
		qfq_front_slot_remove(grp);	/* queue is empty */
	else {
		u64 roundedS;

		cl->F = cl->S + (u64)len * cl->inv_w;
		roundedS = qfq_round_down(cl->S, grp->slot_shift);
		if (roundedS == grp->S)
			return false;

		qfq_front_slot_remove(grp);
		qfq_slot_insert(grp, cl, roundedS);
	}

	return true;
}

static struct sk_buff *qfq_dequeue(struct Qdisc *sch)
{
	struct qfq_sched *q = qdisc_priv(sch);
	struct qfq_group *grp;
	struct qfq_class *cl;
	struct sk_buff *skb;
	unsigned int len;
	u64 old_V;

	if (!q->bitmaps[ER])
		return NULL;

	grp = qfq_ffs(q, q->bitmaps[ER]);

	cl = qfq_slot_head(grp);
	skb = qdisc_dequeue_peeked(cl->qdisc);
	if (!skb) {
		WARN_ONCE(1, "qfq_dequeue: non-workconserving leaf\n");
		return NULL;
	}

	sch->q.qlen--;
	qdisc_bstats_update(sch, skb);

	old_V = q->V;
	len = qdisc_pkt_len(skb);
	q->V += (u64)len * IWSUM;
	pr_debug("qfq dequeue: len %u F %lld now %lld\n",
		 len, (unsigned long long) cl->F, (unsigned long long) q->V);

	if (qfq_update_class(grp, cl)) {
		u64 old_F = grp->F;

		cl = qfq_slot_scan(grp);
		if (!cl)
			__clear_bit(grp->index, &q->bitmaps[ER]);
		else {
			u64 roundedS = qfq_round_down(cl->S, grp->slot_shift);
			unsigned int s;

			if (grp->S == roundedS)
				goto skip_unblock;
			grp->S = roundedS;
			grp->F = roundedS + (2ULL << grp->slot_shift);
			__clear_bit(grp->index, &q->bitmaps[ER]);
			s = qfq_calc_state(q, grp);
			__set_bit(grp->index, &q->bitmaps[s]);
		}

		qfq_unblock_groups(q, grp->index, old_F);
	}

skip_unblock:
	qfq_update_eligible(q, old_V);

	return skb;
}

/*
 * Assign a reasonable start time for a new flow k in group i.
 * Admissible values for \hat(F) are multiples of \sigma_i
 * no greater than V+\sigma_i . Larger values mean that
 * we had a wraparound so we consider the timestamp to be stale.
 *
 * If F is not stale and F >= V then we set S = F.
 * Otherwise we should assign S = V, but this may violate
 * the ordering in ER. So, if we have groups in ER, set S to
 * the F_j of the first group j which would be blocking us.
 * We are guaranteed not to move S backward because
 * otherwise our group i would still be blocked.
 */
static void qfq_update_start(struct qfq_sched *q, struct qfq_class *cl)
{
	unsigned long mask;
	u64 limit, roundedF;
	int slot_shift = cl->grp->slot_shift;

	roundedF = qfq_round_down(cl->F, slot_shift);
	limit = qfq_round_down(q->V, slot_shift) + (1ULL << slot_shift);

	if (!qfq_gt(cl->F, q->V) || qfq_gt(roundedF, limit)) {
		/* timestamp was stale */
		mask = mask_from(q->bitmaps[ER], cl->grp->index);
		if (mask) {
			struct qfq_group *next = qfq_ffs(q, mask);
			if (qfq_gt(roundedF, next->F)) {
				if (qfq_gt(limit, next->F))
					cl->S = next->F;
				else /* preserve timestamp correctness */
					cl->S = limit;
				return;
			}
		}
		cl->S = q->V;
	} else  /* timestamp is not stale */
		cl->S = cl->F;
}

static int qfq_enqueue(struct sk_buff *skb, struct Qdisc *sch)
{
	struct qfq_sched *q = qdisc_priv(sch);
	struct qfq_class *cl;
	int err = 0;

	cl = qfq_classify(skb, sch, &err);
	if (cl == NULL) {
		if (err & __NET_XMIT_BYPASS)
			sch->qstats.drops++;
		kfree_skb(skb);
		return err;
	}
	pr_debug("qfq_enqueue: cl = %x\n", cl->common.classid);

	if (unlikely(cl->lmax < qdisc_pkt_len(skb))) {
		pr_debug("qfq: increasing maxpkt from %u to %u for class %u",
			  cl->lmax, qdisc_pkt_len(skb), cl->common.classid);
		qfq_update_reactivate_class(q, cl, cl->inv_w,
					    qdisc_pkt_len(skb), 0);
	}

	err = qdisc_enqueue(skb, cl->qdisc);
	if (unlikely(err != NET_XMIT_SUCCESS)) {
		pr_debug("qfq_enqueue: enqueue failed %d\n", err);
		if (net_xmit_drop_count(err)) {
			cl->qstats.drops++;
			sch->qstats.drops++;
		}
		return err;
	}

	bstats_update(&cl->bstats, skb);
	++sch->q.qlen;

	/* If the new skb is not the head of queue, then done here. */
	if (cl->qdisc->q.qlen != 1)
		return err;

	/* If reach this point, queue q was idle */
	qfq_activate_class(q, cl, qdisc_pkt_len(skb));

	return err;
}

/*
 * Handle class switch from idle to backlogged.
 */
static void qfq_activate_class(struct qfq_sched *q, struct qfq_class *cl,
			       unsigned int pkt_len)
{
	struct qfq_group *grp = cl->grp;
	u64 roundedS;
	int s;

	qfq_update_start(q, cl);

	/* compute new finish time and rounded start. */
	cl->F = cl->S + (u64)pkt_len * cl->inv_w;
	roundedS = qfq_round_down(cl->S, grp->slot_shift);

	/*
	 * insert cl in the correct bucket.
	 * If cl->S >= grp->S we don't need to adjust the
	 * bucket list and simply go to the insertion phase.
	 * Otherwise grp->S is decreasing, we must make room
	 * in the bucket list, and also recompute the group state.
	 * Finally, if there were no flows in this group and nobody
	 * was in ER make sure to adjust V.
	 */
	if (grp->full_slots) {
		if (!qfq_gt(grp->S, cl->S))
			goto skip_update;

		/* create a slot for this cl->S */
		qfq_slot_rotate(grp, roundedS);
		/* group was surely ineligible, remove */
		__clear_bit(grp->index, &q->bitmaps[IR]);
		__clear_bit(grp->index, &q->bitmaps[IB]);
	} else if (!q->bitmaps[ER] && qfq_gt(roundedS, q->V))
		q->V = roundedS;

	grp->S = roundedS;
	grp->F = roundedS + (2ULL << grp->slot_shift);
	s = qfq_calc_state(q, grp);
	__set_bit(grp->index, &q->bitmaps[s]);

	pr_debug("qfq enqueue: new state %d %#lx S %lld F %lld V %lld\n",
		 s, q->bitmaps[s],
		 (unsigned long long) cl->S,
		 (unsigned long long) cl->F,
		 (unsigned long long) q->V);

skip_update:
	qfq_slot_insert(grp, cl, roundedS);
}


static void qfq_slot_remove(struct qfq_sched *q, struct qfq_group *grp,
			    struct qfq_class *cl)
{
	unsigned int i, offset;
	u64 roundedS;

	roundedS = qfq_round_down(cl->S, grp->slot_shift);
	offset = (roundedS - grp->S) >> grp->slot_shift;
	i = (grp->front + offset) % QFQ_MAX_SLOTS;

	hlist_del(&cl->next);
	if (hlist_empty(&grp->slots[i]))
		__clear_bit(offset, &grp->full_slots);
}

/*
 * called to forcibly destroy a queue.
 * If the queue is not in the front bucket, or if it has
 * other queues in the front bucket, we can simply remove
 * the queue with no other side effects.
 * Otherwise we must propagate the event up.
 */
static void qfq_deactivate_class(struct qfq_sched *q, struct qfq_class *cl)
{
	struct qfq_group *grp = cl->grp;
	unsigned long mask;
	u64 roundedS;
	int s;

	cl->F = cl->S;
	qfq_slot_remove(q, grp, cl);

	if (!grp->full_slots) {
		__clear_bit(grp->index, &q->bitmaps[IR]);
		__clear_bit(grp->index, &q->bitmaps[EB]);
		__clear_bit(grp->index, &q->bitmaps[IB]);

		if (test_bit(grp->index, &q->bitmaps[ER]) &&
		    !(q->bitmaps[ER] & ~((1UL << grp->index) - 1))) {
			mask = q->bitmaps[ER] & ((1UL << grp->index) - 1);
			if (mask)
				mask = ~((1UL << __fls(mask)) - 1);
			else
				mask = ~0UL;
			qfq_move_groups(q, mask, EB, ER);
			qfq_move_groups(q, mask, IB, IR);
		}
		__clear_bit(grp->index, &q->bitmaps[ER]);
	} else if (hlist_empty(&grp->slots[grp->front])) {
		cl = qfq_slot_scan(grp);
		roundedS = qfq_round_down(cl->S, grp->slot_shift);
		if (grp->S != roundedS) {
			__clear_bit(grp->index, &q->bitmaps[ER]);
			__clear_bit(grp->index, &q->bitmaps[IR]);
			__clear_bit(grp->index, &q->bitmaps[EB]);
			__clear_bit(grp->index, &q->bitmaps[IB]);
			grp->S = roundedS;
			grp->F = roundedS + (2ULL << grp->slot_shift);
			s = qfq_calc_state(q, grp);
			__set_bit(grp->index, &q->bitmaps[s]);
		}
	}

	qfq_update_eligible(q, q->V);
}

static void qfq_qlen_notify(struct Qdisc *sch, unsigned long arg)
{
	struct qfq_sched *q = qdisc_priv(sch);
	struct qfq_class *cl = (struct qfq_class *)arg;

	if (cl->qdisc->q.qlen == 0)
		qfq_deactivate_class(q, cl);
}

static unsigned int qfq_drop(struct Qdisc *sch)
{
	struct qfq_sched *q = qdisc_priv(sch);
	struct qfq_group *grp;
	unsigned int i, j, len;

	for (i = 0; i <= QFQ_MAX_INDEX; i++) {
		grp = &q->groups[i];
		for (j = 0; j < QFQ_MAX_SLOTS; j++) {
			struct qfq_class *cl;
			struct hlist_node *n;

			hlist_for_each_entry(cl, n, &grp->slots[j], next) {

				if (!cl->qdisc->ops->drop)
					continue;

				len = cl->qdisc->ops->drop(cl->qdisc);
				if (len > 0) {
					sch->q.qlen--;
					if (!cl->qdisc->q.qlen)
						qfq_deactivate_class(q, cl);

					return len;
				}
			}
		}
	}

	return 0;
}

static int qfq_init_qdisc(struct Qdisc *sch, struct nlattr *opt)
{
	struct qfq_sched *q = qdisc_priv(sch);
	struct qfq_group *grp;
	int i, j, err;

	err = qdisc_class_hash_init(&q->clhash);
	if (err < 0)
		return err;

	for (i = 0; i <= QFQ_MAX_INDEX; i++) {
		grp = &q->groups[i];
		grp->index = i;
		grp->slot_shift = QFQ_MTU_SHIFT + FRAC_BITS
				   - (QFQ_MAX_INDEX - i);
		for (j = 0; j < QFQ_MAX_SLOTS; j++)
			INIT_HLIST_HEAD(&grp->slots[j]);
	}

	return 0;
}

static void qfq_reset_qdisc(struct Qdisc *sch)
{
	struct qfq_sched *q = qdisc_priv(sch);
	struct qfq_group *grp;
	struct qfq_class *cl;
	struct hlist_node *n, *tmp;
	unsigned int i, j;

	for (i = 0; i <= QFQ_MAX_INDEX; i++) {
		grp = &q->groups[i];
		for (j = 0; j < QFQ_MAX_SLOTS; j++) {
			hlist_for_each_entry_safe(cl, n, tmp,
						  &grp->slots[j], next) {
				qfq_deactivate_class(q, cl);
			}
		}
	}

	for (i = 0; i < q->clhash.hashsize; i++) {
		hlist_for_each_entry(cl, n, &q->clhash.hash[i], common.hnode)
			qdisc_reset(cl->qdisc);
	}
	sch->q.qlen = 0;
}

static void qfq_destroy_qdisc(struct Qdisc *sch)
{
	struct qfq_sched *q = qdisc_priv(sch);
	struct qfq_class *cl;
	struct hlist_node *n, *next;
	unsigned int i;

	tcf_destroy_chain(&q->filter_list);

	for (i = 0; i < q->clhash.hashsize; i++) {
		hlist_for_each_entry_safe(cl, n, next, &q->clhash.hash[i],
					  common.hnode) {
			qfq_destroy_class(sch, cl);
		}
	}
	qdisc_class_hash_destroy(&q->clhash);
}

static const struct Qdisc_class_ops qfq_class_ops = {
	.change		= qfq_change_class,
	.delete		= qfq_delete_class,
	.get		= qfq_get_class,
	.put		= qfq_put_class,
	.tcf_chain	= qfq_tcf_chain,
	.bind_tcf	= qfq_bind_tcf,
	.unbind_tcf	= qfq_unbind_tcf,
	.graft		= qfq_graft_class,
	.leaf		= qfq_class_leaf,
	.qlen_notify	= qfq_qlen_notify,
	.dump		= qfq_dump_class,
	.dump_stats	= qfq_dump_class_stats,
	.walk		= qfq_walk,
};

static struct Qdisc_ops qfq_qdisc_ops __read_mostly = {
	.cl_ops		= &qfq_class_ops,
	.id		= "qfq",
	.priv_size	= sizeof(struct qfq_sched),
	.enqueue	= qfq_enqueue,
	.dequeue	= qfq_dequeue,
	.peek		= qdisc_peek_dequeued,
	.drop		= qfq_drop,
	.init		= qfq_init_qdisc,
	.reset		= qfq_reset_qdisc,
	.destroy	= qfq_destroy_qdisc,
	.owner		= THIS_MODULE,
};

static int __init qfq_init(void)
{
	return register_qdisc(&qfq_qdisc_ops);
}

static void __exit qfq_exit(void)
{
	unregister_qdisc(&qfq_qdisc_ops);
}

module_init(qfq_init);
module_exit(qfq_exit);
MODULE_LICENSE("GPL");
