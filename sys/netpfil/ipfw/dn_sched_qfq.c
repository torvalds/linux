/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2010 Fabio Checconi, Luigi Rizzo, Paolo Valente
 * All rights reserved
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

/*
 * $FreeBSD$
 */

#ifdef _KERNEL
#include <sys/malloc.h>
#include <sys/socket.h>
#include <sys/socketvar.h>
#include <sys/kernel.h>
#include <sys/lock.h>
#include <sys/mbuf.h>
#include <sys/module.h>
#include <sys/rwlock.h>
#include <net/if.h>	/* IFNAMSIZ */
#include <netinet/in.h>
#include <netinet/ip_var.h>		/* ipfw_rule_ref */
#include <netinet/ip_fw.h>	/* flow_id */
#include <netinet/ip_dummynet.h>
#include <netpfil/ipfw/ip_fw_private.h>
#include <netpfil/ipfw/dn_heap.h>
#include <netpfil/ipfw/ip_dn_private.h>
#ifdef NEW_AQM
#include <netpfil/ipfw/dn_aqm.h>
#endif
#include <netpfil/ipfw/dn_sched.h>
#else
#include <dn_test.h>
#endif

#ifdef QFQ_DEBUG
#define _P64	unsigned long long	/* cast for printing uint64_t */
struct qfq_sched;
static void dump_sched(struct qfq_sched *q, const char *msg);
#define	NO(x)	x
#else
#define NO(x)
#endif
#define DN_SCHED_QFQ	4 // XXX Where?
typedef	unsigned long	bitmap;

/*
 * bitmaps ops are critical. Some linux versions have __fls
 * and the bitmap ops. Some machines have ffs
 * NOTE: fls() returns 1 for the least significant bit,
 *       __fls() returns 0 for the same case.
 * We use the base-0 version __fls() to match the description in
 * the ToN QFQ paper
 */
#if defined(_WIN32) || (defined(__MIPSEL__) && defined(LINUX_24))
int fls(unsigned int n)
{
	int i = 0;
	for (i = 0; n > 0; n >>= 1, i++)
		;
	return i;
}
#endif

#if !defined(_KERNEL) || defined( __FreeBSD__ ) || defined(_WIN32) || (defined(__MIPSEL__) && defined(LINUX_24))
static inline unsigned long __fls(unsigned long word)
{
	return fls(word) - 1;
}
#endif

#if !defined(_KERNEL) || !defined(__linux__)
#ifdef QFQ_DEBUG
static int test_bit(int ix, bitmap *p)
{
	if (ix < 0 || ix > 31)
		D("bad index %d", ix);
	return *p & (1<<ix);
}
static void __set_bit(int ix, bitmap *p)
{
	if (ix < 0 || ix > 31)
		D("bad index %d", ix);
	*p |= (1<<ix);
}
static void __clear_bit(int ix, bitmap *p)
{
	if (ix < 0 || ix > 31)
		D("bad index %d", ix);
	*p &= ~(1<<ix);
}
#else /* !QFQ_DEBUG */
/* XXX do we have fast version, or leave it to the compiler ? */
#define test_bit(ix, pData)	((*pData) & (1<<(ix)))
#define __set_bit(ix, pData)	(*pData) |= (1<<(ix))
#define __clear_bit(ix, pData)	(*pData) &= ~(1<<(ix))
#endif /* !QFQ_DEBUG */
#endif /* !__linux__ */

#ifdef __MIPSEL__
#define __clear_bit(ix, pData)	(*pData) &= ~(1<<(ix))
#endif

/*-------------------------------------------*/
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
 * inside a group. This is approx lmax/lmin + 5.
 * XXX check because it poses constraints on MAX_INDEX
 */
#define QFQ_MAX_SLOTS	32
/*
 * Shifts used for class<->group mapping. Class weights are
 * in the range [1, QFQ_MAX_WEIGHT], we to map each class i to the
 * group with the smallest index that can support the L_i / r_i
 * configured for the class.
 *
 * grp->index is the index of the group; and grp->slot_shift
 * is the shift for the corresponding (scaled) sigma_i.
 *
 * When computing the group index, we do (len<<FP_SHIFT)/weight,
 * then compute an FLS (which is like a log2()), and if the result
 * is below the MAX_INDEX region we use 0 (which is the same as
 * using a larger len).
 */
#define QFQ_MAX_INDEX		19
#define QFQ_MAX_WSHIFT		16	/* log2(max_weight) */

#define	QFQ_MAX_WEIGHT		(1<<QFQ_MAX_WSHIFT)
#define QFQ_MAX_WSUM		(2*QFQ_MAX_WEIGHT)

#define FRAC_BITS		30	/* fixed point arithmetic */
#define ONE_FP			(1UL << FRAC_BITS)

#define QFQ_MTU_SHIFT		11	/* log2(max_len) */
#define QFQ_MIN_SLOT_SHIFT	(FRAC_BITS + QFQ_MTU_SHIFT - QFQ_MAX_INDEX)

/*
 * Possible group states, also indexes for the bitmaps array in
 * struct qfq_queue. We rely on ER, IR, EB, IB being numbered 0..3
 */
enum qfq_state { ER, IR, EB, IB, QFQ_MAX_STATE };

struct qfq_group;
/*
 * additional queue info. Some of this info should come from
 * the flowset, we copy them here for faster processing.
 * This is an overlay of the struct dn_queue
 */
struct qfq_class {
	struct dn_queue _q;
	uint64_t S, F;		/* flow timestamps (exact) */
	struct qfq_class *next; /* Link for the slot list. */

	/* group we belong to. In principle we would need the index,
	 * which is log_2(lmax/weight), but we never reference it
	 * directly, only the group.
	 */
	struct qfq_group *grp;

	/* these are copied from the flowset. */
	uint32_t	inv_w;	/* ONE_FP/weight */
	uint32_t 	lmax;	/* Max packet size for this flow. */
};

/* Group descriptor, see the paper for details.
 * Basically this contains the bucket lists
 */
struct qfq_group {
	uint64_t S, F;			/* group timestamps (approx). */
	unsigned int slot_shift;	/* Slot shift. */
	unsigned int index;		/* Group index. */
	unsigned int front;		/* Index of the front slot. */
	bitmap full_slots;		/* non-empty slots */

	/* Array of lists of active classes. */
	struct qfq_class *slots[QFQ_MAX_SLOTS];
};

/* scheduler instance descriptor. */
struct qfq_sched {
	uint64_t	V;		/* Precise virtual time. */
	uint32_t	wsum;		/* weight sum */
	uint32_t	iwsum;		/* inverse weight sum */
	NO(uint32_t	i_wsum;)	/* ONE_FP/w_sum */
	NO(uint32_t	queued;)	/* debugging */
	NO(uint32_t	loops;)		/* debugging */
	bitmap bitmaps[QFQ_MAX_STATE];	/* Group bitmaps. */
	struct qfq_group groups[QFQ_MAX_INDEX + 1]; /* The groups. */
};

/*---- support functions ----------------------------*/

/* Generic comparison function, handling wraparound. */
static inline int qfq_gt(uint64_t a, uint64_t b)
{
	return (int64_t)(a - b) > 0;
}

/* Round a precise timestamp to its slotted value. */
static inline uint64_t qfq_round_down(uint64_t ts, unsigned int shift)
{
	return ts & ~((1ULL << shift) - 1);
}

/* return the pointer to the group with lowest index in the bitmap */
static inline struct qfq_group *qfq_ffs(struct qfq_sched *q,
					unsigned long bitmap)
{
	int index = ffs(bitmap) - 1; // zero-based
	return &q->groups[index];
}

/*
 * Calculate a flow index, given its weight and maximum packet length.
 * index = log_2(maxlen/weight) but we need to apply the scaling.
 * This is used only once at flow creation.
 */
static int qfq_calc_index(uint32_t inv_w, unsigned int maxlen)
{
	uint64_t slot_size = (uint64_t)maxlen *inv_w;
	unsigned long size_map;
	int index = 0;

	size_map = (unsigned long)(slot_size >> QFQ_MIN_SLOT_SHIFT);
	if (!size_map)
		goto out;

	index = __fls(size_map) + 1;	// basically a log_2()
	index -= !(slot_size - (1ULL << (index + QFQ_MIN_SLOT_SHIFT - 1)));

	if (index < 0)
		index = 0;

out:
	ND("W = %d, L = %d, I = %d\n", ONE_FP/inv_w, maxlen, index);
	return index;
}
/*---- end support functions ----*/

/*-------- API calls --------------------------------*/
/*
 * Validate and copy parameters from flowset.
 */
static int
qfq_new_queue(struct dn_queue *_q)
{
	struct qfq_sched *q = (struct qfq_sched *)(_q->_si + 1);
	struct qfq_class *cl = (struct qfq_class *)_q;
	int i;
	uint32_t w;	/* approximated weight */

	/* import parameters from the flowset. They should be correct
	 * already.
	 */
	w = _q->fs->fs.par[0];
	cl->lmax = _q->fs->fs.par[1];
	if (!w || w > QFQ_MAX_WEIGHT) {
		w = 1;
		D("rounding weight to 1");
	}
	cl->inv_w = ONE_FP/w;
	w = ONE_FP/cl->inv_w;	
	if (q->wsum + w > QFQ_MAX_WSUM)
		return EINVAL;

	i = qfq_calc_index(cl->inv_w, cl->lmax);
	cl->grp = &q->groups[i];
	q->wsum += w;
	q->iwsum = ONE_FP / q->wsum; /* XXX note theory */
	// XXX cl->S = q->V; ?
	return 0;
}

/* remove an empty queue */
static int
qfq_free_queue(struct dn_queue *_q)
{
	struct qfq_sched *q = (struct qfq_sched *)(_q->_si + 1);
	struct qfq_class *cl = (struct qfq_class *)_q;
	if (cl->inv_w) {
		q->wsum -= ONE_FP/cl->inv_w;
		if (q->wsum != 0)
			q->iwsum = ONE_FP / q->wsum;
		cl->inv_w = 0; /* reset weight to avoid run twice */
	}
	return 0;
}

/* Calculate a mask to mimic what would be ffs_from(). */
static inline unsigned long
mask_from(unsigned long bitmap, int from)
{
	return bitmap & ~((1UL << from) - 1);
}

/*
 * The state computation relies on ER=0, IR=1, EB=2, IB=3
 * First compute eligibility comparing grp->S, q->V,
 * then check if someone is blocking us and possibly add EB
 */
static inline unsigned int
qfq_calc_state(struct qfq_sched *q, struct qfq_group *grp)
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
static inline void
qfq_move_groups(struct qfq_sched *q, unsigned long mask, int src, int dst)
{
	q->bitmaps[dst] |= q->bitmaps[src] & mask;
	q->bitmaps[src] &= ~mask;
}

static inline void
qfq_unblock_groups(struct qfq_sched *q, int index, uint64_t old_finish)
{
	unsigned long mask = mask_from(q->bitmaps[ER], index + 1);
	struct qfq_group *next;

	if (mask) {
		next = qfq_ffs(q, mask);
		if (!qfq_gt(next->F, old_finish))
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
static inline void
qfq_make_eligible(struct qfq_sched *q, uint64_t old_V)
{
	unsigned long mask, vslot, old_vslot;

	vslot = q->V >> QFQ_MIN_SLOT_SHIFT;
	old_vslot = old_V >> QFQ_MIN_SLOT_SHIFT;

	if (vslot != old_vslot) {
		/* must be 2ULL, see ToN QFQ article fig.5, we use base-0 fls */
		mask = (2ULL << (__fls(vslot ^ old_vslot))) - 1;
		qfq_move_groups(q, mask, IR, ER);
		qfq_move_groups(q, mask, IB, EB);
	}
}

/*
 * XXX we should make sure that slot becomes less than 32.
 * This is guaranteed by the input values.
 * roundedS is always cl->S rounded on grp->slot_shift bits.
 */
static inline void
qfq_slot_insert(struct qfq_group *grp, struct qfq_class *cl, uint64_t roundedS)
{
	uint64_t slot = (roundedS - grp->S) >> grp->slot_shift;
	unsigned int i = (grp->front + slot) % QFQ_MAX_SLOTS;

	cl->next = grp->slots[i];
	grp->slots[i] = cl;
	__set_bit(slot, &grp->full_slots);
}

/*
 * remove the entry from the slot
 */
static inline void
qfq_front_slot_remove(struct qfq_group *grp)
{
	struct qfq_class **h = &grp->slots[grp->front];

	*h = (*h)->next;
	if (!*h)
		__clear_bit(0, &grp->full_slots);
}

/*
 * Returns the first full queue in a group. As a side effect,
 * adjust the bucket list so the first non-empty bucket is at
 * position 0 in full_slots.
 */
static inline struct qfq_class *
qfq_slot_scan(struct qfq_group *grp)
{
	int i;

	ND("grp %d full %x", grp->index, grp->full_slots);
	if (!grp->full_slots)
		return NULL;

	i = ffs(grp->full_slots) - 1; // zero-based
	if (i > 0) {
		grp->front = (grp->front + i) % QFQ_MAX_SLOTS;
		grp->full_slots >>= i;
	}

	return grp->slots[grp->front];
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
static inline void
qfq_slot_rotate(struct qfq_sched *q, struct qfq_group *grp, uint64_t roundedS)
{
	unsigned int i = (grp->S - roundedS) >> grp->slot_shift;

	(void)q;
	grp->full_slots <<= i;
	grp->front = (grp->front - i) % QFQ_MAX_SLOTS;
}


static inline void
qfq_update_eligible(struct qfq_sched *q, uint64_t old_V)
{
	bitmap ineligible;

	ineligible = q->bitmaps[IR] | q->bitmaps[IB];
	if (ineligible) {
		if (!q->bitmaps[ER]) {
			struct qfq_group *grp;
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
static inline int
qfq_update_class(struct qfq_sched *q, struct qfq_group *grp,
	    struct qfq_class *cl)
{

	(void)q;
	cl->S = cl->F;
	if (cl->_q.mq.head == NULL)  {
		qfq_front_slot_remove(grp);
	} else {
		unsigned int len;
		uint64_t roundedS;

		len = cl->_q.mq.head->m_pkthdr.len;
		cl->F = cl->S + (uint64_t)len * cl->inv_w;
		roundedS = qfq_round_down(cl->S, grp->slot_shift);
		if (roundedS == grp->S)
			return 0;

		qfq_front_slot_remove(grp);
		qfq_slot_insert(grp, cl, roundedS);
	}
	return 1;
}

static struct mbuf *
qfq_dequeue(struct dn_sch_inst *si)
{
	struct qfq_sched *q = (struct qfq_sched *)(si + 1);
	struct qfq_group *grp;
	struct qfq_class *cl;
	struct mbuf *m;
	uint64_t old_V;

	NO(q->loops++;)
	if (!q->bitmaps[ER]) {
		NO(if (q->queued)
			dump_sched(q, "start dequeue");)
		return NULL;
	}

	grp = qfq_ffs(q, q->bitmaps[ER]);

	cl = grp->slots[grp->front];
	/* extract from the first bucket in the bucket list */
	m = dn_dequeue(&cl->_q);

	if (!m) {
		D("BUG/* non-workconserving leaf */");
		return NULL;
	}
	NO(q->queued--;)
	old_V = q->V;
	q->V += (uint64_t)m->m_pkthdr.len * q->iwsum;
	ND("m is %p F 0x%llx V now 0x%llx", m, cl->F, q->V);

	if (qfq_update_class(q, grp, cl)) {
		uint64_t old_F = grp->F;
		cl = qfq_slot_scan(grp);
		if (!cl) { /* group gone, remove from ER */
			__clear_bit(grp->index, &q->bitmaps[ER]);
			// grp->S = grp->F + 1; // XXX debugging only
		} else {
			uint64_t roundedS = qfq_round_down(cl->S, grp->slot_shift);
			unsigned int s;

			if (grp->S == roundedS)
				goto skip_unblock;
			grp->S = roundedS;
			grp->F = roundedS + (2ULL << grp->slot_shift);
			/* remove from ER and put in the new set */
			__clear_bit(grp->index, &q->bitmaps[ER]);
			s = qfq_calc_state(q, grp);
			__set_bit(grp->index, &q->bitmaps[s]);
		}
		/* we need to unblock even if the group has gone away */
		qfq_unblock_groups(q, grp->index, old_F);
	}

skip_unblock:
	qfq_update_eligible(q, old_V);
	NO(if (!q->bitmaps[ER] && q->queued)
		dump_sched(q, "end dequeue");)

	return m;
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
static inline void
qfq_update_start(struct qfq_sched *q, struct qfq_class *cl)
{
	unsigned long mask;
	uint64_t limit, roundedF;
	int slot_shift = cl->grp->slot_shift;

	roundedF = qfq_round_down(cl->F, slot_shift);
	limit = qfq_round_down(q->V, slot_shift) + (1ULL << slot_shift);

	if (!qfq_gt(cl->F, q->V) || qfq_gt(roundedF, limit)) {
		/* timestamp was stale */
		mask = mask_from(q->bitmaps[ER], cl->grp->index);
		if (mask) {
			struct qfq_group *next = qfq_ffs(q, mask);
			if (qfq_gt(roundedF, next->F)) {
				/* from pv 71261956973ba9e0637848a5adb4a5819b4bae83 */
				if (qfq_gt(limit, next->F))
					cl->S = next->F;
				else /* preserve timestamp correctness */
					cl->S = limit;
				return;
			}
		}
		cl->S = q->V;
	} else { /* timestamp is not stale */
		cl->S = cl->F;
	}
}

static int
qfq_enqueue(struct dn_sch_inst *si, struct dn_queue *_q, struct mbuf *m)
{
	struct qfq_sched *q = (struct qfq_sched *)(si + 1);
	struct qfq_group *grp;
	struct qfq_class *cl = (struct qfq_class *)_q;
	uint64_t roundedS;
	int s;

	NO(q->loops++;)
	DX(4, "len %d flow %p inv_w 0x%x grp %d", m->m_pkthdr.len,
		_q, cl->inv_w, cl->grp->index);
	/* XXX verify that the packet obeys the parameters */
	if (m != _q->mq.head) {
		if (dn_enqueue(_q, m, 0)) /* packet was dropped */
			return 1;
		NO(q->queued++;)
		if (m != _q->mq.head)
			return 0;
	}
	/* If reach this point, queue q was idle */
	grp = cl->grp;
	qfq_update_start(q, cl); /* adjust start time */
	/* compute new finish time and rounded start. */
	cl->F = cl->S + (uint64_t)(m->m_pkthdr.len) * cl->inv_w;
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
		qfq_slot_rotate(q, grp, roundedS);
		/* group was surely ineligible, remove */
		__clear_bit(grp->index, &q->bitmaps[IR]);
		__clear_bit(grp->index, &q->bitmaps[IB]);
	} else if (!q->bitmaps[ER] && qfq_gt(roundedS, q->V))
		q->V = roundedS;

	grp->S = roundedS;
	grp->F = roundedS + (2ULL << grp->slot_shift); // i.e. 2\sigma_i
	s = qfq_calc_state(q, grp);
	__set_bit(grp->index, &q->bitmaps[s]);
	ND("new state %d 0x%x", s, q->bitmaps[s]);
	ND("S %llx F %llx V %llx", cl->S, cl->F, q->V);
skip_update:
	qfq_slot_insert(grp, cl, roundedS);

	return 0;
}


#if 0
static inline void
qfq_slot_remove(struct qfq_sched *q, struct qfq_group *grp,
	struct qfq_class *cl, struct qfq_class **pprev)
{
	unsigned int i, offset;
	uint64_t roundedS;

	roundedS = qfq_round_down(cl->S, grp->slot_shift);
	offset = (roundedS - grp->S) >> grp->slot_shift;
	i = (grp->front + offset) % QFQ_MAX_SLOTS;

#ifdef notyet
	if (!pprev) {
		pprev = &grp->slots[i];
		while (*pprev && *pprev != cl)
			pprev = &(*pprev)->next;
	}
#endif

	*pprev = cl->next;
	if (!grp->slots[i])
		__clear_bit(offset, &grp->full_slots);
}

/*
 * called to forcibly destroy a queue.
 * If the queue is not in the front bucket, or if it has
 * other queues in the front bucket, we can simply remove
 * the queue with no other side effects.
 * Otherwise we must propagate the event up.
 * XXX description to be completed.
 */
static void
qfq_deactivate_class(struct qfq_sched *q, struct qfq_class *cl,
				 struct qfq_class **pprev)
{
	struct qfq_group *grp = &q->groups[cl->index];
	unsigned long mask;
	uint64_t roundedS;
	int s;

	cl->F = cl->S;	// not needed if the class goes away.
	qfq_slot_remove(q, grp, cl, pprev);

	if (!grp->full_slots) {
		/* nothing left in the group, remove from all sets.
		 * Do ER last because if we were blocking other groups
		 * we must unblock them.
		 */
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
	} else if (!grp->slots[grp->front]) {
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
#endif

static int
qfq_new_fsk(struct dn_fsk *f)
{
	ipdn_bound_var(&f->fs.par[0], 1, 1, QFQ_MAX_WEIGHT, "qfq weight");
	ipdn_bound_var(&f->fs.par[1], 1500, 1, 2000, "qfq maxlen");
	ND("weight %d len %d\n", f->fs.par[0], f->fs.par[1]);
	return 0;
}

/*
 * initialize a new scheduler instance
 */
static int
qfq_new_sched(struct dn_sch_inst *si)
{
	struct qfq_sched *q = (struct qfq_sched *)(si + 1);
	struct qfq_group *grp;
	int i;

	for (i = 0; i <= QFQ_MAX_INDEX; i++) {
		grp = &q->groups[i];
		grp->index = i;
		grp->slot_shift = QFQ_MTU_SHIFT + FRAC_BITS -
					(QFQ_MAX_INDEX - i);
	}
	return 0;
}

/*
 * QFQ scheduler descriptor
 */
static struct dn_alg qfq_desc = {
	_SI( .type = ) DN_SCHED_QFQ,
	_SI( .name = ) "QFQ",
	_SI( .flags = ) DN_MULTIQUEUE,

	_SI( .schk_datalen = ) 0,
	_SI( .si_datalen = ) sizeof(struct qfq_sched),
	_SI( .q_datalen = ) sizeof(struct qfq_class) - sizeof(struct dn_queue),

	_SI( .enqueue = ) qfq_enqueue,
	_SI( .dequeue = ) qfq_dequeue,

	_SI( .config = )  NULL,
	_SI( .destroy = )  NULL,
	_SI( .new_sched = ) qfq_new_sched,
	_SI( .free_sched = )  NULL,
	_SI( .new_fsk = ) qfq_new_fsk,
	_SI( .free_fsk = )  NULL,
	_SI( .new_queue = ) qfq_new_queue,
	_SI( .free_queue = ) qfq_free_queue,
#ifdef NEW_AQM
	_SI( .getconfig = )  NULL,
#endif
};

DECLARE_DNSCHED_MODULE(dn_qfq, &qfq_desc);

#ifdef QFQ_DEBUG
static void
dump_groups(struct qfq_sched *q, uint32_t mask)
{
	int i, j;

	for (i = 0; i < QFQ_MAX_INDEX + 1; i++) {
		struct qfq_group *g = &q->groups[i];

		if (0 == (mask & (1<<i)))
			continue;
		for (j = 0; j < QFQ_MAX_SLOTS; j++) {
			if (g->slots[j])
				D("    bucket %d %p", j, g->slots[j]);
		}
		D("full_slots 0x%llx", (_P64)g->full_slots);
		D("        %2d S 0x%20llx F 0x%llx %c", i,
			(_P64)g->S, (_P64)g->F,
			mask & (1<<i) ? '1' : '0');
	}
}

static void
dump_sched(struct qfq_sched *q, const char *msg)
{
	D("--- in %s: ---", msg);
	D("loops %d queued %d V 0x%llx", q->loops, q->queued, (_P64)q->V);
	D("    ER 0x%08x", (unsigned)q->bitmaps[ER]);
	D("    EB 0x%08x", (unsigned)q->bitmaps[EB]);
	D("    IR 0x%08x", (unsigned)q->bitmaps[IR]);
	D("    IB 0x%08x", (unsigned)q->bitmaps[IB]);
	dump_groups(q, 0xffffffff);
};
#endif /* QFQ_DEBUG */
