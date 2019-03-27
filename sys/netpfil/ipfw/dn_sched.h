/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2010 Riccardo Panicucci, Luigi Rizzo, Universita` di Pisa
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
 * The API to write a packet scheduling algorithm for dummynet.
 *
 * $FreeBSD$
 */

#ifndef _DN_SCHED_H
#define _DN_SCHED_H

#define	DN_MULTIQUEUE	0x01
/*
 * Descriptor for a scheduling algorithm.
 * Contains all function pointers for a given scheduler
 * This is typically created when a module is loaded, and stored
 * in a global list of schedulers.
 */
struct dn_alg {
	uint32_t type;           /* the scheduler type */
	const char *name;   /* scheduler name */
	uint32_t flags;	/* DN_MULTIQUEUE if supports multiple queues */

	/*
	 * The following define the size of 3 optional data structures
	 * that may need to be allocated at runtime, and are appended
	 * to each of the base data structures: scheduler, sched.inst,
	 * and queue. We don't have a per-flowset structure.
	 */
	/*    + parameters attached to the template, e.g.
	 *	default queue sizes, weights, quantum size, and so on;
	 */
	size_t schk_datalen;

	/*    + per-instance parameters, such as timestamps,
	 *	containers for queues, etc;
	 */
	size_t si_datalen;

	size_t q_datalen;	/* per-queue parameters (e.g. S,F) */

	/*
	 * Methods implemented by the scheduler:
	 * enqueue	enqueue packet 'm' on scheduler 's', queue 'q'.
	 *	q is NULL for !MULTIQUEUE.
	 *	Return 0 on success, 1 on drop (packet consumed anyways).
	 *	Note that q should be interpreted only as a hint
	 *	on the flow that the mbuf belongs to: while a
	 *	scheduler will normally enqueue m into q, it is ok
	 *	to leave q alone and put the mbuf elsewhere.
	 *	This function is called in two cases:
	 *	 - when a new packet arrives to the scheduler;
	 *	 - when a scheduler is reconfigured. In this case the
	 *	   call is issued by the new_queue callback, with a 
	 *	   non empty queue (q) and m pointing to the first
	 *	   mbuf in the queue. For this reason, the function
	 *	   should internally check for (m != q->mq.head)
	 *	   before calling dn_enqueue().
	 *
	 * dequeue	Called when scheduler instance 's' can
	 *	dequeue a packet. Return NULL if none are available.
	 *	XXX what about non work-conserving ?
	 *
	 * config	called on 'sched X config ...', normally writes
	 *	in the area of size sch_arg
	 *
	 * destroy	called on 'sched delete', frees everything
	 *	in sch_arg (other parts are handled by more specific
	 *	functions)
	 *
	 * new_sched    called when a new instance is created, e.g.
	 *	to create the local queue for !MULTIQUEUE, set V or
	 *	copy parameters for WFQ, and so on.
	 *
	 * free_sched	called when deleting an instance, cleans
	 *	extra data in the per-instance area.
	 *
	 * new_fsk	called when a flowset is linked to a scheduler,
	 *	e.g. to validate parameters such as weights etc.
	 * free_fsk	when a flowset is unlinked from a scheduler.
	 *	(probably unnecessary)
	 *
	 * new_queue	called to set the per-queue parameters,
	 *	e.g. S and F, adjust sum of weights in the parent, etc.
	 *
	 *	The new_queue callback is normally called from when
	 *	creating a new queue. In some cases (such as a
	 *	scheduler change or reconfiguration) it can be called
	 *	with a non empty queue. In this case, the queue
	 *	In case of non empty queue, the new_queue callback could
	 *	need to call the enqueue function. In this case,
	 *	the callback should eventually call enqueue() passing
	 *	as m the first element in the queue.
	 *
	 * free_queue	actions related to a queue removal, e.g. undo
	 *	all the above. If the queue has data in it, also remove
	 *	from the scheduler. This can e.g. happen during a reconfigure.
	 */
	int (*enqueue)(struct dn_sch_inst *, struct dn_queue *,
		struct mbuf *);
	struct mbuf * (*dequeue)(struct dn_sch_inst *);

	int (*config)(struct dn_schk *);
	int (*destroy)(struct dn_schk*);
	int (*new_sched)(struct dn_sch_inst *);
	int (*free_sched)(struct dn_sch_inst *);
	int (*new_fsk)(struct dn_fsk *f);
	int (*free_fsk)(struct dn_fsk *f);
	int (*new_queue)(struct dn_queue *q);
	int (*free_queue)(struct dn_queue *q);
#ifdef NEW_AQM
	/* Getting scheduler extra parameters */
	int (*getconfig)(struct dn_schk *, struct dn_extra_parms *);
#endif

	/* run-time fields */
	int ref_count;      /* XXX number of instances in the system */
	SLIST_ENTRY(dn_alg) next; /* Next scheduler in the list */
};

/* MSVC does not support initializers so we need this ugly macro */
#ifdef _WIN32
#define _SI(fld)
#else
#define _SI(fld)	fld
#endif

/*
 * Additionally, dummynet exports some functions and macros
 * to be used by schedulers:
 */

void dn_free_pkts(struct mbuf *mnext);
int dn_enqueue(struct dn_queue *q, struct mbuf* m, int drop);
/* bound a variable between min and max */
int ipdn_bound_var(int *v, int dflt, int lo, int hi, const char *msg);

/*
 * Extract the head of a queue, update stats. Must be the very last
 * thing done on a dequeue as the queue itself may go away.
 */
static __inline struct mbuf*
dn_dequeue(struct dn_queue *q)
{
	struct mbuf *m = q->mq.head;
	if (m == NULL)
		return NULL;
#ifdef NEW_AQM
	/* Call AQM dequeue function  */
	if (q->fs->aqmfp && q->fs->aqmfp->dequeue )
		return q->fs->aqmfp->dequeue(q);
#endif
	q->mq.head = m->m_nextpkt;
	q->mq.count--;

	/* Update stats for the queue */
	q->ni.length--;
	q->ni.len_bytes -= m->m_pkthdr.len;
	if (q->_si) {
		q->_si->ni.length--;
		q->_si->ni.len_bytes -= m->m_pkthdr.len;
	}
	if (q->ni.length == 0) /* queue is now idle */
		q->q_time = dn_cfg.curr_time;
	return m;
}

int dn_sched_modevent(module_t mod, int cmd, void *arg);

#define DECLARE_DNSCHED_MODULE(name, dnsched)			\
	static moduledata_t name##_mod = {			\
		#name, dn_sched_modevent, dnsched		\
	};							\
	DECLARE_MODULE(name, name##_mod, 			\
		SI_SUB_PROTO_FIREWALL, SI_ORDER_ANY); 		\
        MODULE_DEPEND(name, dummynet, 3, 3, 3)
#endif /* _DN_SCHED_H */
