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

#define DN_SCHED_PRIO	5 //XXX

#if !defined(_KERNEL) || !defined(__linux__)
#define test_bit(ix, pData)	((*pData) & (1<<(ix)))
#define __set_bit(ix, pData)	(*pData) |= (1<<(ix))
#define __clear_bit(ix, pData)	(*pData) &= ~(1<<(ix))
#endif

#ifdef __MIPSEL__
#define __clear_bit(ix, pData)	(*pData) &= ~(1<<(ix))
#endif

/* Size of the array of queues pointers. */
#define BITMAP_T	unsigned long
#define MAXPRIO		(sizeof(BITMAP_T) * 8)

/*
 * The scheduler instance contains an array of pointers to queues,
 * one for each priority, and a bitmap listing backlogged queues.
 */
struct prio_si {
	BITMAP_T bitmap;			/* array bitmap */
	struct dn_queue *q_array[MAXPRIO];	/* Array of queues pointers */
};

/*
 * If a queue with the same priority is already backlogged, use
 * that one instead of the queue passed as argument.
 */
static int 
prio_enqueue(struct dn_sch_inst *_si, struct dn_queue *q, struct mbuf *m)
{
	struct prio_si *si = (struct prio_si *)(_si + 1);
	int prio = q->fs->fs.par[0];

	if (test_bit(prio, &si->bitmap) == 0) {
		/* No queue with this priority, insert */
		__set_bit(prio, &si->bitmap);
		si->q_array[prio] = q;
	} else { /* use the existing queue */
		q = si->q_array[prio];
	}
	if (dn_enqueue(q, m, 0))
		return 1;
	return 0;
}

/*
 * Packets are dequeued only from the highest priority queue.
 * The function ffs() return the lowest bit in the bitmap that rapresent
 * the array index (-1) which contains the pointer to the highest priority
 * queue.
 * After the dequeue, if this queue become empty, it is index is removed
 * from the bitmap.
 * Scheduler is idle if the bitmap is empty
 *
 * NOTE: highest priority is 0, lowest is sched->max_prio_q
 */
static struct mbuf *
prio_dequeue(struct dn_sch_inst *_si)
{
	struct prio_si *si = (struct prio_si *)(_si + 1);
	struct mbuf *m;
	struct dn_queue *q;
	int prio;

	if (si->bitmap == 0) /* scheduler idle */
		return NULL;

	prio = ffs(si->bitmap) - 1;

	/* Take the highest priority queue in the scheduler */
	q = si->q_array[prio];
	// assert(q)

	m = dn_dequeue(q);
	if (q->mq.head == NULL) {
		/* Queue is now empty, remove from scheduler
		 * and mark it
		 */
		si->q_array[prio] = NULL;
		__clear_bit(prio, &si->bitmap);
	}
	return m;
}

static int
prio_new_sched(struct dn_sch_inst *_si)
{
	struct prio_si *si = (struct prio_si *)(_si + 1);

	bzero(si->q_array, sizeof(si->q_array));
	si->bitmap = 0;

	return 0;
}

static int
prio_new_fsk(struct dn_fsk *fs)
{
	/* Check if the prioritiy is between 0 and MAXPRIO-1 */
	ipdn_bound_var(&fs->fs.par[0], 0, 0, MAXPRIO - 1, "PRIO priority");
	return 0;
}

static int
prio_new_queue(struct dn_queue *q)
{
	struct prio_si *si = (struct prio_si *)(q->_si + 1);
	int prio = q->fs->fs.par[0];
	struct dn_queue *oldq;

	q->ni.oid.subtype = DN_SCHED_PRIO;

	if (q->mq.head == NULL)
		return 0;

	/* Queue already full, must insert in the scheduler or append
	 * mbufs to existing queue. This partly duplicates prio_enqueue
	 */
	if (test_bit(prio, &si->bitmap) == 0) {
		/* No queue with this priority, insert */
		__set_bit(prio, &si->bitmap);
		si->q_array[prio] = q;
	} else if ( (oldq = si->q_array[prio]) != q) {
		/* must append to the existing queue.
		 * can simply append q->mq.head to q2->...
		 * and add the counters to those of q2
		 */
		oldq->mq.tail->m_nextpkt = q->mq.head;
		oldq->mq.tail = q->mq.tail;
		oldq->ni.length += q->ni.length;
		q->ni.length = 0;
		oldq->ni.len_bytes += q->ni.len_bytes;
		q->ni.len_bytes = 0;
		q->mq.tail = q->mq.head = NULL;
	}
	return 0;
}

static int
prio_free_queue(struct dn_queue *q)
{
	int prio = q->fs->fs.par[0];
	struct prio_si *si = (struct prio_si *)(q->_si + 1);

	if (si->q_array[prio] == q) {
		si->q_array[prio] = NULL;
		__clear_bit(prio, &si->bitmap);
	}
	return 0;
}


static struct dn_alg prio_desc = {
	_SI( .type = ) DN_SCHED_PRIO,
	_SI( .name = ) "PRIO",
	_SI( .flags = ) DN_MULTIQUEUE,

	/* we need extra space in the si and the queue */
	_SI( .schk_datalen = ) 0,
	_SI( .si_datalen = ) sizeof(struct prio_si),
	_SI( .q_datalen = ) 0,

	_SI( .enqueue = ) prio_enqueue,
	_SI( .dequeue = ) prio_dequeue,

	_SI( .config = )  NULL,
	_SI( .destroy = )  NULL,
	_SI( .new_sched = ) prio_new_sched,
	_SI( .free_sched = ) NULL,

	_SI( .new_fsk = ) prio_new_fsk,
	_SI( .free_fsk = )  NULL,

	_SI( .new_queue = ) prio_new_queue,
	_SI( .free_queue = ) prio_free_queue,
#ifdef NEW_AQM
	_SI( .getconfig = )  NULL,
#endif
};


DECLARE_DNSCHED_MODULE(dn_prio, &prio_desc);
