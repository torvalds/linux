/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2010 Riccardo Panicucci, Universita` di Pisa
 * Copyright (c) 2000-2002 Luigi Rizzo, Universita` di Pisa
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

#ifndef MAX64
#define MAX64(x,y)  (( (int64_t) ( (y)-(x) )) > 0 ) ? (y) : (x)
#endif

/*
 * timestamps are computed on 64 bit using fixed point arithmetic.
 * LMAX_BITS, WMAX_BITS are the max number of bits for the packet len
 * and sum of weights, respectively. FRAC_BITS is the number of
 * fractional bits. We want FRAC_BITS >> WMAX_BITS to avoid too large
 * errors when computing the inverse, FRAC_BITS < 32 so we can do 1/w
 * using an unsigned 32-bit division, and to avoid wraparounds we need
 * LMAX_BITS + WMAX_BITS + FRAC_BITS << 64
 * As an example
 * FRAC_BITS = 26, LMAX_BITS=14, WMAX_BITS = 19
 */
#ifndef FRAC_BITS
#define FRAC_BITS    28 /* shift for fixed point arithmetic */
#define	ONE_FP	(1UL << FRAC_BITS)
#endif

/*
 * Private information for the scheduler instance:
 * sch_heap (key is Finish time) returns the next queue to serve
 * ne_heap (key is Start time) stores not-eligible queues
 * idle_heap (key=start/finish time) stores idle flows. It must
 *	support extract-from-middle.
 * A flow is only in 1 of the three heaps.
 * XXX todo: use a more efficient data structure, e.g. a tree sorted
 * by F with min_subtree(S) in each node
 */
struct wf2qp_si {
    struct dn_heap sch_heap;	/* top extract - key Finish  time */
    struct dn_heap ne_heap;	/* top extract - key Start   time */
    struct dn_heap idle_heap;	/* random extract - key Start=Finish time */
    uint64_t V;			/* virtual time */
    uint32_t inv_wsum;		/* inverse of sum of weights */
    uint32_t wsum;		/* sum of weights */
};

struct wf2qp_queue {
    struct dn_queue _q;
    uint64_t S, F;		/* start time, finish time */
    uint32_t inv_w;		/* ONE_FP / weight */
    int32_t heap_pos;		/* position (index) of struct in heap */
};

/*
 * This file implements a WF2Q+ scheduler as it has been in dummynet
 * since 2000.
 * The scheduler supports per-flow queues and has O(log N) complexity.
 *
 * WF2Q+ needs to drain entries from the idle heap so that we
 * can keep the sum of weights up to date. We can do it whenever
 * we get a chance, or periodically, or following some other
 * strategy. The function idle_check() drains at most N elements
 * from the idle heap.
 */
static void
idle_check(struct wf2qp_si *si, int n, int force)
{
    struct dn_heap *h = &si->idle_heap;
    while (n-- > 0 && h->elements > 0 &&
		(force || DN_KEY_LT(HEAP_TOP(h)->key, si->V))) {
	struct dn_queue *q = HEAP_TOP(h)->object;
        struct wf2qp_queue *alg_fq = (struct wf2qp_queue *)q;

        heap_extract(h, NULL);
        /* XXX to let the flowset delete the queue we should
	 * mark it as 'unused' by the scheduler.
	 */
        alg_fq->S = alg_fq->F + 1; /* Mark timestamp as invalid. */
        si->wsum -= q->fs->fs.par[0];	/* adjust sum of weights */
	if (si->wsum > 0)
		si->inv_wsum = ONE_FP/si->wsum;
    }
}

static int
wf2qp_enqueue(struct dn_sch_inst *_si, struct dn_queue *q, struct mbuf *m)
{
    struct dn_fsk *fs = q->fs;
    struct wf2qp_si *si = (struct wf2qp_si *)(_si + 1);
    struct wf2qp_queue *alg_fq;
    uint64_t len = m->m_pkthdr.len;

    if (m != q->mq.head) {
	if (dn_enqueue(q, m, 0)) /* packet was dropped */
	    return 1;
	if (m != q->mq.head)	/* queue was already busy */
	    return 0;
    }

    /* If reach this point, queue q was idle */
    alg_fq = (struct wf2qp_queue *)q;

    if (DN_KEY_LT(alg_fq->F, alg_fq->S)) {
        /* F<S means timestamps are invalid ->brand new queue. */
        alg_fq->S = si->V;		/* init start time */
        si->wsum += fs->fs.par[0];	/* add weight of new queue. */
	si->inv_wsum = ONE_FP/si->wsum;
    } else { /* if it was idle then it was in the idle heap */
        heap_extract(&si->idle_heap, q);
        alg_fq->S = MAX64(alg_fq->F, si->V);	/* compute new S */
    }
    alg_fq->F = alg_fq->S + len * alg_fq->inv_w;

    /* if nothing is backlogged, make sure this flow is eligible */
    if (si->ne_heap.elements == 0 && si->sch_heap.elements == 0)
        si->V = MAX64(alg_fq->S, si->V);

    /*
     * Look at eligibility. A flow is not eligibile if S>V (when
     * this happens, it means that there is some other flow already
     * scheduled for the same pipe, so the sch_heap cannot be
     * empty). If the flow is not eligible we just store it in the
     * ne_heap. Otherwise, we store in the sch_heap.
     * Note that for all flows in sch_heap (SCH), S_i <= V,
     * and for all flows in ne_heap (NEH), S_i > V.
     * So when we need to compute max(V, min(S_i)) forall i in
     * SCH+NEH, we only need to look into NEH.
     */
    if (DN_KEY_LT(si->V, alg_fq->S)) {
        /* S>V means flow Not eligible. */
        if (si->sch_heap.elements == 0)
            D("++ ouch! not eligible but empty scheduler!");
        heap_insert(&si->ne_heap, alg_fq->S, q);
    } else {
        heap_insert(&si->sch_heap, alg_fq->F, q);
    }
    return 0;
}

/* XXX invariant: sch > 0 || V >= min(S in neh) */
static struct mbuf *
wf2qp_dequeue(struct dn_sch_inst *_si)
{
	/* Access scheduler instance private data */
	struct wf2qp_si *si = (struct wf2qp_si *)(_si + 1);
	struct mbuf *m;
	struct dn_queue *q;
	struct dn_heap *sch = &si->sch_heap;
	struct dn_heap *neh = &si->ne_heap;
	struct wf2qp_queue *alg_fq;

	if (sch->elements == 0 && neh->elements == 0) {
		/* we have nothing to do. We could kill the idle heap
		 * altogether and reset V
		 */
		idle_check(si, 0x7fffffff, 1);
		si->V = 0;
		si->wsum = 0;	/* should be set already */
		return NULL;	/* quick return if nothing to do */
	}
	idle_check(si, 1, 0);	/* drain something from the idle heap */

	/* make sure at least one element is eligible, bumping V
	 * and moving entries that have become eligible.
	 * We need to repeat the first part twice, before and
	 * after extracting the candidate, or enqueue() will
	 * find the data structure in a wrong state.
	 */
  m = NULL;
  for(;;) {
	/*
	 * Compute V = max(V, min(S_i)). Remember that all elements
	 * in sch have by definition S_i <= V so if sch is not empty,
	 * V is surely the max and we must not update it. Conversely,
	 * if sch is empty we only need to look at neh.
	 * We don't need to move the queues, as it will be done at the
	 * next enqueue
	 */
	if (sch->elements == 0 && neh->elements > 0) {
		si->V = MAX64(si->V, HEAP_TOP(neh)->key);
	}
	while (neh->elements > 0 &&
		    DN_KEY_LEQ(HEAP_TOP(neh)->key, si->V)) {
		q = HEAP_TOP(neh)->object;
		alg_fq = (struct wf2qp_queue *)q;
		heap_extract(neh, NULL);
		heap_insert(sch, alg_fq->F, q);
	}
	if (m) /* pkt found in previous iteration */
		break;
	/* ok we have at least one eligible pkt */
	q = HEAP_TOP(sch)->object;
	alg_fq = (struct wf2qp_queue *)q;
	m = dn_dequeue(q);
	heap_extract(sch, NULL); /* Remove queue from heap. */
	si->V += (uint64_t)(m->m_pkthdr.len) * si->inv_wsum;
	alg_fq->S = alg_fq->F;  /* Update start time. */
	if (q->mq.head == 0) {	/* not backlogged any more. */
		heap_insert(&si->idle_heap, alg_fq->F, q);
	} else {			/* Still backlogged. */
		/* Update F, store in neh or sch */
		uint64_t len = q->mq.head->m_pkthdr.len;
		alg_fq->F += len * alg_fq->inv_w;
		if (DN_KEY_LEQ(alg_fq->S, si->V)) {
			heap_insert(sch, alg_fq->F, q);
		} else {
			heap_insert(neh, alg_fq->S, q);
		}
	}
    }
	return m;
}

static int
wf2qp_new_sched(struct dn_sch_inst *_si)
{
	struct wf2qp_si *si = (struct wf2qp_si *)(_si + 1);
	int ofs = offsetof(struct wf2qp_queue, heap_pos);

	/* all heaps support extract from middle */
	if (heap_init(&si->idle_heap, 16, ofs) ||
	    heap_init(&si->sch_heap, 16, ofs) ||
	    heap_init(&si->ne_heap, 16, ofs)) {
		heap_free(&si->ne_heap);
		heap_free(&si->sch_heap);
		heap_free(&si->idle_heap);
		return ENOMEM;
	}
	return 0;
}

static int
wf2qp_free_sched(struct dn_sch_inst *_si)
{
	struct wf2qp_si *si = (struct wf2qp_si *)(_si + 1);

	heap_free(&si->sch_heap);
	heap_free(&si->ne_heap);
	heap_free(&si->idle_heap);

	return 0;
}

static int
wf2qp_new_fsk(struct dn_fsk *fs)
{
	ipdn_bound_var(&fs->fs.par[0], 1,
		1, 100, "WF2Q+ weight");
	return 0;
}

static int
wf2qp_new_queue(struct dn_queue *_q)
{
	struct wf2qp_queue *q = (struct wf2qp_queue *)_q;

	_q->ni.oid.subtype = DN_SCHED_WF2QP;
	q->F = 0;	/* not strictly necessary */
	q->S = q->F + 1;    /* mark timestamp as invalid. */
        q->inv_w = ONE_FP / _q->fs->fs.par[0];
	if (_q->mq.head != NULL) {
		wf2qp_enqueue(_q->_si, _q, _q->mq.head);
	}
	return 0;
}

/*
 * Called when the infrastructure removes a queue (e.g. flowset
 * is reconfigured). Nothing to do if we did not 'own' the queue,
 * otherwise remove it from the right heap and adjust the sum
 * of weights.
 */
static int
wf2qp_free_queue(struct dn_queue *q)
{
	struct wf2qp_queue *alg_fq = (struct wf2qp_queue *)q;
	struct wf2qp_si *si = (struct wf2qp_si *)(q->_si + 1);

	if (alg_fq->S >= alg_fq->F + 1)
		return 0;	/* nothing to do, not in any heap */
	si->wsum -= q->fs->fs.par[0];
	if (si->wsum > 0)
		si->inv_wsum = ONE_FP/si->wsum;

	/* extract from the heap. XXX TODO we may need to adjust V
	 * to make sure the invariants hold.
	 */
	if (q->mq.head == NULL) {
		heap_extract(&si->idle_heap, q);
	} else if (DN_KEY_LT(si->V, alg_fq->S)) {
		heap_extract(&si->ne_heap, q);
	} else {
		heap_extract(&si->sch_heap, q);
	}
	return 0;
}

/*
 * WF2Q+ scheduler descriptor
 * contains the type of the scheduler, the name, the size of the
 * structures and function pointers.
 */
static struct dn_alg wf2qp_desc = {
	_SI( .type = ) DN_SCHED_WF2QP,
	_SI( .name = ) "WF2Q+",
	_SI( .flags = ) DN_MULTIQUEUE,

	/* we need extra space in the si and the queue */
	_SI( .schk_datalen = ) 0,
	_SI( .si_datalen = ) sizeof(struct wf2qp_si),
	_SI( .q_datalen = ) sizeof(struct wf2qp_queue) -
				sizeof(struct dn_queue),

	_SI( .enqueue = ) wf2qp_enqueue,
	_SI( .dequeue = ) wf2qp_dequeue,

	_SI( .config = )  NULL,
	_SI( .destroy = )  NULL,
	_SI( .new_sched = ) wf2qp_new_sched,
	_SI( .free_sched = ) wf2qp_free_sched,

	_SI( .new_fsk = ) wf2qp_new_fsk,
	_SI( .free_fsk = )  NULL,

	_SI( .new_queue = ) wf2qp_new_queue,
	_SI( .free_queue = ) wf2qp_free_queue,
#ifdef NEW_AQM
	_SI( .getconfig = )  NULL,
#endif

};


DECLARE_DNSCHED_MODULE(dn_wf2qp, &wf2qp_desc);
