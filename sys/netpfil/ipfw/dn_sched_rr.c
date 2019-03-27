/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2010 Riccardo Panicucci, Universita` di Pisa
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

#define DN_SCHED_RR	3 // XXX Where?

struct rr_queue {
	struct dn_queue q;		/* Standard queue */
	int status;			/* 1: queue is in the list */
	uint32_t credit;		/* max bytes we can transmit */
	uint32_t quantum;		/* quantum * weight */
	struct rr_queue *qnext;		/* */
};

/* struct rr_schk contains global config parameters
 * and is right after dn_schk
 */
struct rr_schk {
	uint32_t min_q;		/* Min quantum */
	uint32_t max_q;		/* Max quantum */
	uint32_t q_bytes;	/* default quantum in bytes */
};

/* per-instance round robin list, right after dn_sch_inst */
struct rr_si {
	struct rr_queue *head, *tail;	/* Pointer to current queue */
};

/* Append a queue to the rr list */
static inline void
rr_append(struct rr_queue *q, struct rr_si *si)
{
	q->status = 1;		/* mark as in-rr_list */
	q->credit = q->quantum;	/* initialize credit */

	/* append to the tail */
	if (si->head == NULL)
		si->head = q;
	else
		si->tail->qnext = q;
	si->tail = q;		/* advance the tail pointer */
	q->qnext = si->head;	/* make it circular */
}

/* Remove the head queue from circular list. */
static inline void
rr_remove_head(struct rr_si *si)
{
	if (si->head == NULL)
		return; /* empty queue */
	si->head->status = 0;

	if (si->head == si->tail) {
		si->head = si->tail = NULL;
		return;
	}

	si->head = si->head->qnext;
	si->tail->qnext = si->head;
}

/* Remove a queue from circular list.
 * XXX see if ti can be merge with remove_queue()
 */
static inline void
remove_queue_q(struct rr_queue *q, struct rr_si *si)
{
	struct rr_queue *prev;

	if (q->status != 1)
		return;
	if (q == si->head) {
		rr_remove_head(si);
		return;
	}

	for (prev = si->head; prev; prev = prev->qnext) {
		if (prev->qnext != q)
			continue;
		prev->qnext = q->qnext;
		if (q == si->tail)
			si->tail = prev;
		q->status = 0;
		break;
	}
}


static inline void
next_pointer(struct rr_si *si)
{
	if (si->head == NULL)
		return; /* empty queue */

	si->head = si->head->qnext;
	si->tail = si->tail->qnext;
}

static int
rr_enqueue(struct dn_sch_inst *_si, struct dn_queue *q, struct mbuf *m)
{
	struct rr_si *si;
	struct rr_queue *rrq;

	if (m != q->mq.head) {
		if (dn_enqueue(q, m, 0)) /* packet was dropped */
			return 1;
		if (m != q->mq.head)
			return 0;
	}

	/* If reach this point, queue q was idle */
	si = (struct rr_si *)(_si + 1);
	rrq = (struct rr_queue *)q;

	if (rrq->status == 1) /* Queue is already in the queue list */
		return 0;

	/* Insert the queue in the queue list */
	rr_append(rrq, si);

	return 0;
}

static struct mbuf *
rr_dequeue(struct dn_sch_inst *_si)
{
	/* Access scheduler instance private data */
	struct rr_si *si = (struct rr_si *)(_si + 1);
	struct rr_queue *rrq;
	uint64_t len;

	while ( (rrq = si->head) ) {
		struct mbuf *m = rrq->q.mq.head;
		if ( m == NULL) {
			/* empty queue, remove from list */
			rr_remove_head(si);
			continue;
		}
		len = m->m_pkthdr.len;

		if (len > rrq->credit) {
			/* Packet too big */
			rrq->credit += rrq->quantum;
			/* Try next queue */
			next_pointer(si);
		} else {
			rrq->credit -= len;
			return dn_dequeue(&rrq->q);
		}
	}

	/* no packet to dequeue*/
	return NULL;
}

static int
rr_config(struct dn_schk *_schk)
{
	struct rr_schk *schk = (struct rr_schk *)(_schk + 1);
	ND("called");

	/* use reasonable quantums (64..2k bytes, default 1500) */
	schk->min_q = 64;
	schk->max_q = 2048;
	schk->q_bytes = 1500;	/* quantum */

	return 0;
}

static int
rr_new_sched(struct dn_sch_inst *_si)
{
	struct rr_si *si = (struct rr_si *)(_si + 1);

	ND("called");
	si->head = si->tail = NULL;

	return 0;
}

static int
rr_free_sched(struct dn_sch_inst *_si)
{
	(void)_si;
	ND("called");
	/* Nothing to do? */
	return 0;
}

static int
rr_new_fsk(struct dn_fsk *fs)
{
	struct rr_schk *schk = (struct rr_schk *)(fs->sched + 1);
	/* par[0] is the weight, par[1] is the quantum step */
	/* make sure the product fits an uint32_t */
	ipdn_bound_var(&fs->fs.par[0], 1,
		1, 65536, "RR weight");
	ipdn_bound_var(&fs->fs.par[1], schk->q_bytes,
		schk->min_q, schk->max_q, "RR quantum");
	return 0;
}

static int
rr_new_queue(struct dn_queue *_q)
{
	struct rr_queue *q = (struct rr_queue *)_q;
	uint64_t quantum;

	_q->ni.oid.subtype = DN_SCHED_RR;

	quantum = (uint64_t)_q->fs->fs.par[0] * _q->fs->fs.par[1];
	if (quantum >= (1ULL<< 32)) {
		D("quantum too large, truncating to 4G - 1");
		quantum = (1ULL<< 32) - 1;
	}
	q->quantum = quantum;
	ND("called, q->quantum %d", q->quantum);
	q->credit = q->quantum;
	q->status = 0;

	if (_q->mq.head != NULL) {
		/* Queue NOT empty, insert in the queue list */
		rr_append(q, (struct rr_si *)(_q->_si + 1));
	}
	return 0;
}

static int
rr_free_queue(struct dn_queue *_q)
{
	struct rr_queue *q = (struct rr_queue *)_q;

	ND("called");
	if (q->status == 1) {
		struct rr_si *si = (struct rr_si *)(_q->_si + 1);
		remove_queue_q(q, si);
	}
	return 0;
}

/*
 * RR scheduler descriptor
 * contains the type of the scheduler, the name, the size of the
 * structures and function pointers.
 */
static struct dn_alg rr_desc = {
	_SI( .type = ) DN_SCHED_RR,
	_SI( .name = ) "RR",
	_SI( .flags = ) DN_MULTIQUEUE,

	_SI( .schk_datalen = ) sizeof(struct rr_schk),
	_SI( .si_datalen = ) sizeof(struct rr_si),
	_SI( .q_datalen = ) sizeof(struct rr_queue) - sizeof(struct dn_queue),

	_SI( .enqueue = ) rr_enqueue,
	_SI( .dequeue = ) rr_dequeue,

	_SI( .config = ) rr_config,
	_SI( .destroy = ) NULL,
	_SI( .new_sched = ) rr_new_sched,
	_SI( .free_sched = ) rr_free_sched,
	_SI( .new_fsk = ) rr_new_fsk,
	_SI( .free_fsk = ) NULL,
	_SI( .new_queue = ) rr_new_queue,
	_SI( .free_queue = ) rr_free_queue,
#ifdef NEW_AQM
	_SI( .getconfig = )  NULL,
#endif
};


DECLARE_DNSCHED_MODULE(dn_rr, &rr_desc);
