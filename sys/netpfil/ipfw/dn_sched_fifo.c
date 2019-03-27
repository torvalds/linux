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

/*
 * This file implements a FIFO scheduler for a single queue.
 * The queue is allocated as part of the scheduler instance,
 * and there is a single flowset is in the template which stores
 * queue size and policy.
 * Enqueue and dequeue use the default library functions.
 */
static int 
fifo_enqueue(struct dn_sch_inst *si, struct dn_queue *q, struct mbuf *m)
{
	/* XXX if called with q != NULL and m=NULL, this is a
	 * re-enqueue from an existing scheduler, which we should
	 * handle.
	 */
	(void)q;
	return dn_enqueue((struct dn_queue *)(si+1), m, 0);
}

static struct mbuf *
fifo_dequeue(struct dn_sch_inst *si)
{
	return dn_dequeue((struct dn_queue *)(si + 1));
}

static int
fifo_new_sched(struct dn_sch_inst *si)
{
	/* This scheduler instance contains the queue */
	struct dn_queue *q = (struct dn_queue *)(si + 1);

        set_oid(&q->ni.oid, DN_QUEUE, sizeof(*q));
	q->_si = si;
	q->fs = si->sched->fs;
	return 0;
}

static int
fifo_free_sched(struct dn_sch_inst *si)
{
	struct dn_queue *q = (struct dn_queue *)(si + 1);
	dn_free_pkts(q->mq.head);
	bzero(q, sizeof(*q));
	return 0;
}

/*
 * FIFO scheduler descriptor
 * contains the type of the scheduler, the name, the size of extra
 * data structures, and function pointers.
 */
static struct dn_alg fifo_desc = {
	_SI( .type = )  DN_SCHED_FIFO,
	_SI( .name = )  "FIFO",
	_SI( .flags = ) 0,

	_SI( .schk_datalen = ) 0,
	_SI( .si_datalen = )  sizeof(struct dn_queue),
	_SI( .q_datalen = )  0,

	_SI( .enqueue = )  fifo_enqueue,
	_SI( .dequeue = )  fifo_dequeue,
	_SI( .config = )  NULL,
	_SI( .destroy = )  NULL,
	_SI( .new_sched = )  fifo_new_sched,
	_SI( .free_sched = )  fifo_free_sched,
	_SI( .new_fsk = )  NULL,
	_SI( .free_fsk = )  NULL,
	_SI( .new_queue = )  NULL,
	_SI( .free_queue = )  NULL,
#ifdef NEW_AQM
	_SI( .getconfig = )  NULL,
#endif
};

DECLARE_DNSCHED_MODULE(dn_fifo, &fifo_desc);
