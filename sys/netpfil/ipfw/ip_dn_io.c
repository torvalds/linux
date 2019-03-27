/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2010 Luigi Rizzo, Riccardo Panicucci, Universita` di Pisa
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
 * Dummynet portions related to packet handling.
 */
#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include "opt_inet6.h"

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/malloc.h>
#include <sys/mbuf.h>
#include <sys/kernel.h>
#include <sys/lock.h>
#include <sys/module.h>
#include <sys/mutex.h>
#include <sys/priv.h>
#include <sys/proc.h>
#include <sys/rwlock.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/sysctl.h>

#include <net/if.h>	/* IFNAMSIZ, struct ifaddr, ifq head, lock.h mutex.h */
#include <net/netisr.h>
#include <net/vnet.h>

#include <netinet/in.h>
#include <netinet/ip.h>		/* ip_len, ip_off */
#include <netinet/ip_var.h>	/* ip_output(), IP_FORWARDING */
#include <netinet/ip_fw.h>
#include <netinet/ip_dummynet.h>
#include <netinet/if_ether.h> /* various ether_* routines */
#include <netinet/ip6.h>       /* for ip6_input, ip6_output prototypes */
#include <netinet6/ip6_var.h>

#include <netpfil/ipfw/ip_fw_private.h>
#include <netpfil/ipfw/dn_heap.h>
#include <netpfil/ipfw/ip_dn_private.h>
#ifdef NEW_AQM
#include <netpfil/ipfw/dn_aqm.h>
#endif
#include <netpfil/ipfw/dn_sched.h>

/*
 * We keep a private variable for the simulation time, but we could
 * probably use an existing one ("softticks" in sys/kern/kern_timeout.c)
 * instead of dn_cfg.curr_time
 */

struct dn_parms dn_cfg;
//VNET_DEFINE(struct dn_parms, _base_dn_cfg);

static long tick_last;		/* Last tick duration (usec). */
static long tick_delta;		/* Last vs standard tick diff (usec). */
static long tick_delta_sum;	/* Accumulated tick difference (usec).*/
static long tick_adjustment;	/* Tick adjustments done. */
static long tick_lost;		/* Lost(coalesced) ticks number. */
/* Adjusted vs non-adjusted curr_time difference (ticks). */
static long tick_diff;

static unsigned long	io_pkt;
static unsigned long	io_pkt_fast;

#ifdef NEW_AQM
unsigned long	io_pkt_drop;
#else
static unsigned long	io_pkt_drop;
#endif
/*
 * We use a heap to store entities for which we have pending timer events.
 * The heap is checked at every tick and all entities with expired events
 * are extracted.
 */
  
MALLOC_DEFINE(M_DUMMYNET, "dummynet", "dummynet heap");

extern	void (*bridge_dn_p)(struct mbuf *, struct ifnet *);

#ifdef SYSCTL_NODE

/*
 * Because of the way the SYSBEGIN/SYSEND macros work on other
 * platforms, there should not be functions between them.
 * So keep the handlers outside the block.
 */
static int
sysctl_hash_size(SYSCTL_HANDLER_ARGS)
{
	int error, value;

	value = dn_cfg.hash_size;
	error = sysctl_handle_int(oidp, &value, 0, req);
	if (error != 0 || req->newptr == NULL)
		return (error);
	if (value < 16 || value > 65536)
		return (EINVAL);
	dn_cfg.hash_size = value;
	return (0);
}

static int
sysctl_limits(SYSCTL_HANDLER_ARGS)
{
	int error;
	long value;

	if (arg2 != 0)
		value = dn_cfg.slot_limit;
	else
		value = dn_cfg.byte_limit;
	error = sysctl_handle_long(oidp, &value, 0, req);

	if (error != 0 || req->newptr == NULL)
		return (error);
	if (arg2 != 0) {
		if (value < 1)
			return (EINVAL);
		dn_cfg.slot_limit = value;
	} else {
		if (value < 1500)
			return (EINVAL);
		dn_cfg.byte_limit = value;
	}
	return (0);
}

SYSBEGIN(f4)

SYSCTL_DECL(_net_inet);
SYSCTL_DECL(_net_inet_ip);
#ifdef NEW_AQM
SYSCTL_NODE(_net_inet_ip, OID_AUTO, dummynet, CTLFLAG_RW, 0, "Dummynet");
#else
static SYSCTL_NODE(_net_inet_ip, OID_AUTO, dummynet, CTLFLAG_RW, 0, "Dummynet");
#endif

/* wrapper to pass dn_cfg fields to SYSCTL_* */
//#define DC(x)	(&(VNET_NAME(_base_dn_cfg).x))
#define DC(x)	(&(dn_cfg.x))
/* parameters */


SYSCTL_PROC(_net_inet_ip_dummynet, OID_AUTO, hash_size,
    CTLTYPE_INT | CTLFLAG_RW, 0, 0, sysctl_hash_size,
    "I", "Default hash table size");


SYSCTL_PROC(_net_inet_ip_dummynet, OID_AUTO, pipe_slot_limit,
    CTLTYPE_LONG | CTLFLAG_RW, 0, 1, sysctl_limits,
    "L", "Upper limit in slots for pipe queue.");
SYSCTL_PROC(_net_inet_ip_dummynet, OID_AUTO, pipe_byte_limit,
    CTLTYPE_LONG | CTLFLAG_RW, 0, 0, sysctl_limits,
    "L", "Upper limit in bytes for pipe queue.");
SYSCTL_INT(_net_inet_ip_dummynet, OID_AUTO, io_fast,
    CTLFLAG_RW, DC(io_fast), 0, "Enable fast dummynet io.");
SYSCTL_INT(_net_inet_ip_dummynet, OID_AUTO, debug,
    CTLFLAG_RW, DC(debug), 0, "Dummynet debug level");

/* RED parameters */
SYSCTL_INT(_net_inet_ip_dummynet, OID_AUTO, red_lookup_depth,
    CTLFLAG_RD, DC(red_lookup_depth), 0, "Depth of RED lookup table");
SYSCTL_INT(_net_inet_ip_dummynet, OID_AUTO, red_avg_pkt_size,
    CTLFLAG_RD, DC(red_avg_pkt_size), 0, "RED Medium packet size");
SYSCTL_INT(_net_inet_ip_dummynet, OID_AUTO, red_max_pkt_size,
    CTLFLAG_RD, DC(red_max_pkt_size), 0, "RED Max packet size");

/* time adjustment */
SYSCTL_LONG(_net_inet_ip_dummynet, OID_AUTO, tick_delta,
    CTLFLAG_RD, &tick_delta, 0, "Last vs standard tick difference (usec).");
SYSCTL_LONG(_net_inet_ip_dummynet, OID_AUTO, tick_delta_sum,
    CTLFLAG_RD, &tick_delta_sum, 0, "Accumulated tick difference (usec).");
SYSCTL_LONG(_net_inet_ip_dummynet, OID_AUTO, tick_adjustment,
    CTLFLAG_RD, &tick_adjustment, 0, "Tick adjustments done.");
SYSCTL_LONG(_net_inet_ip_dummynet, OID_AUTO, tick_diff,
    CTLFLAG_RD, &tick_diff, 0,
    "Adjusted vs non-adjusted curr_time difference (ticks).");
SYSCTL_LONG(_net_inet_ip_dummynet, OID_AUTO, tick_lost,
    CTLFLAG_RD, &tick_lost, 0,
    "Number of ticks coalesced by dummynet taskqueue.");

/* Drain parameters */
SYSCTL_UINT(_net_inet_ip_dummynet, OID_AUTO, expire,
    CTLFLAG_RW, DC(expire), 0, "Expire empty queues/pipes");
SYSCTL_UINT(_net_inet_ip_dummynet, OID_AUTO, expire_cycle,
    CTLFLAG_RD, DC(expire_cycle), 0, "Expire cycle for queues/pipes");

/* statistics */
SYSCTL_INT(_net_inet_ip_dummynet, OID_AUTO, schk_count,
    CTLFLAG_RD, DC(schk_count), 0, "Number of schedulers");
SYSCTL_INT(_net_inet_ip_dummynet, OID_AUTO, si_count,
    CTLFLAG_RD, DC(si_count), 0, "Number of scheduler instances");
SYSCTL_INT(_net_inet_ip_dummynet, OID_AUTO, fsk_count,
    CTLFLAG_RD, DC(fsk_count), 0, "Number of flowsets");
SYSCTL_INT(_net_inet_ip_dummynet, OID_AUTO, queue_count,
    CTLFLAG_RD, DC(queue_count), 0, "Number of queues");
SYSCTL_ULONG(_net_inet_ip_dummynet, OID_AUTO, io_pkt,
    CTLFLAG_RD, &io_pkt, 0,
    "Number of packets passed to dummynet.");
SYSCTL_ULONG(_net_inet_ip_dummynet, OID_AUTO, io_pkt_fast,
    CTLFLAG_RD, &io_pkt_fast, 0,
    "Number of packets bypassed dummynet scheduler.");
SYSCTL_ULONG(_net_inet_ip_dummynet, OID_AUTO, io_pkt_drop,
    CTLFLAG_RD, &io_pkt_drop, 0,
    "Number of packets dropped by dummynet.");
#undef DC
SYSEND

#endif

static void	dummynet_send(struct mbuf *);

/*
 * Return the mbuf tag holding the dummynet state (it should
 * be the first one on the list).
 */
struct dn_pkt_tag *
dn_tag_get(struct mbuf *m)
{
	struct m_tag *mtag = m_tag_first(m);
#ifdef NEW_AQM
	/* XXX: to skip ts m_tag. For Debugging only*/
	if (mtag != NULL && mtag->m_tag_id == DN_AQM_MTAG_TS) {
		m_tag_delete(m,mtag); 
		mtag = m_tag_first(m);
		D("skip TS tag");
	}
#endif
	KASSERT(mtag != NULL &&
	    mtag->m_tag_cookie == MTAG_ABI_COMPAT &&
	    mtag->m_tag_id == PACKET_TAG_DUMMYNET,
	    ("packet on dummynet queue w/o dummynet tag!"));
	return (struct dn_pkt_tag *)(mtag+1);
}

#ifndef NEW_AQM
static inline void
mq_append(struct mq *q, struct mbuf *m)
{
#ifdef USERSPACE
	// buffers from netmap need to be copied
	// XXX note that the routine is not expected to fail
	ND("append %p to %p", m, q);
	if (m->m_flags & M_STACK) {
		struct mbuf *m_new;
		void *p;
		int l, ofs;

		ofs = m->m_data - m->__m_extbuf;
		// XXX allocate
		MGETHDR(m_new, M_NOWAIT, MT_DATA);
		ND("*** WARNING, volatile buf %p ext %p %d dofs %d m_new %p",
			m, m->__m_extbuf, m->__m_extlen, ofs, m_new);
		p = m_new->__m_extbuf;	/* new pointer */
		l = m_new->__m_extlen;	/* new len */
		if (l <= m->__m_extlen) {
			panic("extlen too large");
		}

		*m_new = *m;	// copy
		m_new->m_flags &= ~M_STACK;
		m_new->__m_extbuf = p; // point to new buffer
		_pkt_copy(m->__m_extbuf, p, m->__m_extlen);
		m_new->m_data = p + ofs;
		m = m_new;
	}
#endif /* USERSPACE */
	if (q->head == NULL)
		q->head = m;
	else
		q->tail->m_nextpkt = m;
	q->count++;
	q->tail = m;
	m->m_nextpkt = NULL;
}
#endif

/*
 * Dispose a list of packet. Use a functions so if we need to do
 * more work, this is a central point to do it.
 */
void dn_free_pkts(struct mbuf *mnext)
{
        struct mbuf *m;
    
        while ((m = mnext) != NULL) {
                mnext = m->m_nextpkt;
                FREE_PKT(m);
        }
}

static int
red_drops (struct dn_queue *q, int len)
{
	/*
	 * RED algorithm
	 *
	 * RED calculates the average queue size (avg) using a low-pass filter
	 * with an exponential weighted (w_q) moving average:
	 * 	avg  <-  (1-w_q) * avg + w_q * q_size
	 * where q_size is the queue length (measured in bytes or * packets).
	 *
	 * If q_size == 0, we compute the idle time for the link, and set
	 *	avg = (1 - w_q)^(idle/s)
	 * where s is the time needed for transmitting a medium-sized packet.
	 *
	 * Now, if avg < min_th the packet is enqueued.
	 * If avg > max_th the packet is dropped. Otherwise, the packet is
	 * dropped with probability P function of avg.
	 */

	struct dn_fsk *fs = q->fs;
	int64_t p_b = 0;

	/* Queue in bytes or packets? */
	uint32_t q_size = (fs->fs.flags & DN_QSIZE_BYTES) ?
	    q->ni.len_bytes : q->ni.length;

	/* Average queue size estimation. */
	if (q_size != 0) {
		/* Queue is not empty, avg <- avg + (q_size - avg) * w_q */
		int diff = SCALE(q_size) - q->avg;
		int64_t v = SCALE_MUL((int64_t)diff, (int64_t)fs->w_q);

		q->avg += (int)v;
	} else {
		/*
		 * Queue is empty, find for how long the queue has been
		 * empty and use a lookup table for computing
		 * (1 - * w_q)^(idle_time/s) where s is the time to send a
		 * (small) packet.
		 * XXX check wraps...
		 */
		if (q->avg) {
			u_int t = div64((dn_cfg.curr_time - q->q_time), fs->lookup_step);

			q->avg = (t < fs->lookup_depth) ?
			    SCALE_MUL(q->avg, fs->w_q_lookup[t]) : 0;
		}
	}

	/* Should i drop? */
	if (q->avg < fs->min_th) {
		q->count = -1;
		return (0);	/* accept packet */
	}
	if (q->avg >= fs->max_th) {	/* average queue >=  max threshold */
		if (fs->fs.flags & DN_IS_ECN)
			return (1);
		if (fs->fs.flags & DN_IS_GENTLE_RED) {
			/*
			 * According to Gentle-RED, if avg is greater than
			 * max_th the packet is dropped with a probability
			 *	 p_b = c_3 * avg - c_4
			 * where c_3 = (1 - max_p) / max_th
			 *       c_4 = 1 - 2 * max_p
			 */
			p_b = SCALE_MUL((int64_t)fs->c_3, (int64_t)q->avg) -
			    fs->c_4;
		} else {
			q->count = -1;
			return (1);
		}
	} else if (q->avg > fs->min_th) {
		if (fs->fs.flags & DN_IS_ECN)
			return (1);
		/*
		 * We compute p_b using the linear dropping function
		 *	 p_b = c_1 * avg - c_2
		 * where c_1 = max_p / (max_th - min_th)
		 * 	 c_2 = max_p * min_th / (max_th - min_th)
		 */
		p_b = SCALE_MUL((int64_t)fs->c_1, (int64_t)q->avg) - fs->c_2;
	}

	if (fs->fs.flags & DN_QSIZE_BYTES)
		p_b = div64((p_b * len) , fs->max_pkt_size);
	if (++q->count == 0)
		q->random = random() & 0xffff;
	else {
		/*
		 * q->count counts packets arrived since last drop, so a greater
		 * value of q->count means a greater packet drop probability.
		 */
		if (SCALE_MUL(p_b, SCALE((int64_t)q->count)) > q->random) {
			q->count = 0;
			/* After a drop we calculate a new random value. */
			q->random = random() & 0xffff;
			return (1);	/* drop */
		}
	}
	/* End of RED algorithm. */

	return (0);	/* accept */

}

/*
 * ECN/ECT Processing (partially adopted from altq)
 */
#ifndef NEW_AQM
static
#endif
int
ecn_mark(struct mbuf* m)
{
	struct ip *ip;
	ip = (struct ip *)mtodo(m, dn_tag_get(m)->iphdr_off);

	switch (ip->ip_v) {
	case IPVERSION:
	{
		uint16_t old;

		if ((ip->ip_tos & IPTOS_ECN_MASK) == IPTOS_ECN_NOTECT)
			return (0);	/* not-ECT */
		if ((ip->ip_tos & IPTOS_ECN_MASK) == IPTOS_ECN_CE)
			return (1);	/* already marked */

		/*
		 * ecn-capable but not marked,
		 * mark CE and update checksum
		 */
		old = *(uint16_t *)ip;
		ip->ip_tos |= IPTOS_ECN_CE;
		ip->ip_sum = cksum_adjust(ip->ip_sum, old, *(uint16_t *)ip);
		return (1);
	}
#ifdef INET6
	case (IPV6_VERSION >> 4):
	{
		struct ip6_hdr *ip6 = (struct ip6_hdr *)ip;
		u_int32_t flowlabel;

		flowlabel = ntohl(ip6->ip6_flow);
		if ((flowlabel >> 28) != 6)
			return (0);	/* version mismatch! */
		if ((flowlabel & (IPTOS_ECN_MASK << 20)) ==
		    (IPTOS_ECN_NOTECT << 20))
			return (0);	/* not-ECT */
		if ((flowlabel & (IPTOS_ECN_MASK << 20)) ==
		    (IPTOS_ECN_CE << 20))
			return (1);	/* already marked */
		/*
		 * ecn-capable but not marked, mark CE
		 */
		flowlabel |= (IPTOS_ECN_CE << 20);
		ip6->ip6_flow = htonl(flowlabel);
		return (1);
	}
#endif
	}
	return (0);
}

/*
 * Enqueue a packet in q, subject to space and queue management policy
 * (whose parameters are in q->fs).
 * Update stats for the queue and the scheduler.
 * Return 0 on success, 1 on drop. The packet is consumed anyways.
 */
int
dn_enqueue(struct dn_queue *q, struct mbuf* m, int drop)
{   
	struct dn_fs *f;
	struct dn_flow *ni;	/* stats for scheduler instance */
	uint64_t len;

	if (q->fs == NULL || q->_si == NULL) {
		printf("%s fs %p si %p, dropping\n",
			__FUNCTION__, q->fs, q->_si);
		FREE_PKT(m);
		return 1;
	}
	f = &(q->fs->fs);
	ni = &q->_si->ni;
	len = m->m_pkthdr.len;
	/* Update statistics, then check reasons to drop pkt. */
	q->ni.tot_bytes += len;
	q->ni.tot_pkts++;
	ni->tot_bytes += len;
	ni->tot_pkts++;
	if (drop)
		goto drop;
	if (f->plr && random() < f->plr)
		goto drop;
#ifdef NEW_AQM
	/* Call AQM enqueue function */
	if (q->fs->aqmfp)
		return q->fs->aqmfp->enqueue(q ,m);
#endif
	if (f->flags & DN_IS_RED && red_drops(q, m->m_pkthdr.len)) {
		if (!(f->flags & DN_IS_ECN) || !ecn_mark(m))
			goto drop;
	}
	if (f->flags & DN_QSIZE_BYTES) {
		if (q->ni.len_bytes > f->qsize)
			goto drop;
	} else if (q->ni.length >= f->qsize) {
		goto drop;
	}
	mq_append(&q->mq, m);
	q->ni.length++;
	q->ni.len_bytes += len;
	ni->length++;
	ni->len_bytes += len;
	return (0);

drop:
	io_pkt_drop++;
	q->ni.drops++;
	ni->drops++;
	FREE_PKT(m);
	return (1);
}

/*
 * Fetch packets from the delay line which are due now. If there are
 * leftover packets, reinsert the delay line in the heap.
 * Runs under scheduler lock.
 */
static void
transmit_event(struct mq *q, struct delay_line *dline, uint64_t now)
{
	struct mbuf *m;
	struct dn_pkt_tag *pkt = NULL;

	dline->oid.subtype = 0; /* not in heap */
	while ((m = dline->mq.head) != NULL) {
		pkt = dn_tag_get(m);
		if (!DN_KEY_LEQ(pkt->output_time, now))
			break;
		dline->mq.head = m->m_nextpkt;
		dline->mq.count--;
		mq_append(q, m);
	}
	if (m != NULL) {
		dline->oid.subtype = 1; /* in heap */
		heap_insert(&dn_cfg.evheap, pkt->output_time, dline);
	}
}

/*
 * Convert the additional MAC overheads/delays into an equivalent
 * number of bits for the given data rate. The samples are
 * in milliseconds so we need to divide by 1000.
 */
static uint64_t
extra_bits(struct mbuf *m, struct dn_schk *s)
{
	int index;
	uint64_t bits;
	struct dn_profile *pf = s->profile;

	if (!pf || pf->samples_no == 0)
		return 0;
	index  = random() % pf->samples_no;
	bits = div64((uint64_t)pf->samples[index] * s->link.bandwidth, 1000);
	if (index >= pf->loss_level) {
		struct dn_pkt_tag *dt = dn_tag_get(m);
		if (dt)
			dt->dn_dir = DIR_DROP;
	}
	return bits;
}

/*
 * Send traffic from a scheduler instance due by 'now'.
 * Return a pointer to the head of the queue.
 */
static struct mbuf *
serve_sched(struct mq *q, struct dn_sch_inst *si, uint64_t now)
{
	struct mq def_q;
	struct dn_schk *s = si->sched;
	struct mbuf *m = NULL;
	int delay_line_idle = (si->dline.mq.head == NULL);
	int done, bw;

	if (q == NULL) {
		q = &def_q;
		q->head = NULL;
	}

	bw = s->link.bandwidth;
	si->kflags &= ~DN_ACTIVE;

	if (bw > 0)
		si->credit += (now - si->sched_time) * bw;
	else
		si->credit = 0;
	si->sched_time = now;
	done = 0;
	while (si->credit >= 0 && (m = s->fp->dequeue(si)) != NULL) {
		uint64_t len_scaled;

		done++;
		len_scaled = (bw == 0) ? 0 : hz *
			(m->m_pkthdr.len * 8 + extra_bits(m, s));
		si->credit -= len_scaled;
		/* Move packet in the delay line */
		dn_tag_get(m)->output_time = dn_cfg.curr_time + s->link.delay ;
		mq_append(&si->dline.mq, m);
	}

	/*
	 * If credit >= 0 the instance is idle, mark time.
	 * Otherwise put back in the heap, and adjust the output
	 * time of the last inserted packet, m, which was too early.
	 */
	if (si->credit >= 0) {
		si->idle_time = now;
	} else {
		uint64_t t;
		KASSERT (bw > 0, ("bw=0 and credit<0 ?"));
		t = div64(bw - 1 - si->credit, bw);
		if (m)
			dn_tag_get(m)->output_time += t;
		si->kflags |= DN_ACTIVE;
		heap_insert(&dn_cfg.evheap, now + t, si);
	}
	if (delay_line_idle && done)
		transmit_event(q, &si->dline, now);
	return q->head;
}

/*
 * The timer handler for dummynet. Time is computed in ticks, but
 * but the code is tolerant to the actual rate at which this is called.
 * Once complete, the function reschedules itself for the next tick.
 */
void
dummynet_task(void *context, int pending)
{
	struct timeval t;
	struct mq q = { NULL, NULL }; /* queue to accumulate results */

	CURVNET_SET((struct vnet *)context);

	DN_BH_WLOCK();

	/* Update number of lost(coalesced) ticks. */
	tick_lost += pending - 1;

	getmicrouptime(&t);
	/* Last tick duration (usec). */
	tick_last = (t.tv_sec - dn_cfg.prev_t.tv_sec) * 1000000 +
	(t.tv_usec - dn_cfg.prev_t.tv_usec);
	/* Last tick vs standard tick difference (usec). */
	tick_delta = (tick_last * hz - 1000000) / hz;
	/* Accumulated tick difference (usec). */
	tick_delta_sum += tick_delta;

	dn_cfg.prev_t = t;

	/*
	* Adjust curr_time if the accumulated tick difference is
	* greater than the 'standard' tick. Since curr_time should
	* be monotonically increasing, we do positive adjustments
	* as required, and throttle curr_time in case of negative
	* adjustment.
	*/
	dn_cfg.curr_time++;
	if (tick_delta_sum - tick >= 0) {
		int diff = tick_delta_sum / tick;

		dn_cfg.curr_time += diff;
		tick_diff += diff;
		tick_delta_sum %= tick;
		tick_adjustment++;
	} else if (tick_delta_sum + tick <= 0) {
		dn_cfg.curr_time--;
		tick_diff--;
		tick_delta_sum += tick;
		tick_adjustment++;
	}

	/* serve pending events, accumulate in q */
	for (;;) {
		struct dn_id *p;    /* generic parameter to handler */

		if (dn_cfg.evheap.elements == 0 ||
		    DN_KEY_LT(dn_cfg.curr_time, HEAP_TOP(&dn_cfg.evheap)->key))
			break;
		p = HEAP_TOP(&dn_cfg.evheap)->object;
		heap_extract(&dn_cfg.evheap, NULL);

		if (p->type == DN_SCH_I) {
			serve_sched(&q, (struct dn_sch_inst *)p, dn_cfg.curr_time);
		} else { /* extracted a delay line */
			transmit_event(&q, (struct delay_line *)p, dn_cfg.curr_time);
		}
	}
	if (dn_cfg.expire && ++dn_cfg.expire_cycle >= dn_cfg.expire) {
		dn_cfg.expire_cycle = 0;
		dn_drain_scheduler();
		dn_drain_queue();
	}

	dn_reschedule();
	DN_BH_WUNLOCK();
	if (q.head != NULL)
		dummynet_send(q.head);
	CURVNET_RESTORE();
}

/*
 * forward a chain of packets to the proper destination.
 * This runs outside the dummynet lock.
 */
static void
dummynet_send(struct mbuf *m)
{
	struct mbuf *n;

	for (; m != NULL; m = n) {
		struct ifnet *ifp = NULL;	/* gcc 3.4.6 complains */
        	struct m_tag *tag;
		int dst;

		n = m->m_nextpkt;
		m->m_nextpkt = NULL;
		tag = m_tag_first(m);
		if (tag == NULL) { /* should not happen */
			dst = DIR_DROP;
		} else {
			struct dn_pkt_tag *pkt = dn_tag_get(m);
			/* extract the dummynet info, rename the tag
			 * to carry reinject info.
			 */
			if (pkt->dn_dir == (DIR_OUT | PROTO_LAYER2) &&
				pkt->ifp == NULL) {
				dst = DIR_DROP;
			} else {
				dst = pkt->dn_dir;
				ifp = pkt->ifp;
				tag->m_tag_cookie = MTAG_IPFW_RULE;
				tag->m_tag_id = 0;
			}
		}

		switch (dst) {
		case DIR_OUT:
			ip_output(m, NULL, NULL, IP_FORWARDING, NULL, NULL);
			break ;

		case DIR_IN :
			netisr_dispatch(NETISR_IP, m);
			break;

#ifdef INET6
		case DIR_IN | PROTO_IPV6:
			netisr_dispatch(NETISR_IPV6, m);
			break;

		case DIR_OUT | PROTO_IPV6:
			ip6_output(m, NULL, NULL, IPV6_FORWARDING, NULL, NULL, NULL);
			break;
#endif

		case DIR_FWD | PROTO_IFB: /* DN_TO_IFB_FWD: */
			if (bridge_dn_p != NULL)
				((*bridge_dn_p)(m, ifp));
			else
				printf("dummynet: if_bridge not loaded\n");

			break;

		case DIR_IN | PROTO_LAYER2: /* DN_TO_ETH_DEMUX: */
			/*
			 * The Ethernet code assumes the Ethernet header is
			 * contiguous in the first mbuf header.
			 * Insure this is true.
			 */
			if (m->m_len < ETHER_HDR_LEN &&
			    (m = m_pullup(m, ETHER_HDR_LEN)) == NULL) {
				printf("dummynet/ether: pullup failed, "
				    "dropping packet\n");
				break;
			}
			ether_demux(m->m_pkthdr.rcvif, m);
			break;

		case DIR_OUT | PROTO_LAYER2: /* DN_TO_ETH_OUT: */
			ether_output_frame(ifp, m);
			break;

		case DIR_DROP:
			/* drop the packet after some time */
			FREE_PKT(m);
			break;

		default:
			printf("dummynet: bad switch %d!\n", dst);
			FREE_PKT(m);
			break;
		}
	}
}

static inline int
tag_mbuf(struct mbuf *m, int dir, struct ip_fw_args *fwa)
{
	struct dn_pkt_tag *dt;
	struct m_tag *mtag;

	mtag = m_tag_get(PACKET_TAG_DUMMYNET,
		    sizeof(*dt), M_NOWAIT | M_ZERO);
	if (mtag == NULL)
		return 1;		/* Cannot allocate packet header. */
	m_tag_prepend(m, mtag);		/* Attach to mbuf chain. */
	dt = (struct dn_pkt_tag *)(mtag + 1);
	dt->rule = fwa->rule;
	dt->rule.info &= IPFW_ONEPASS;	/* only keep this info */
	dt->dn_dir = dir;
	dt->ifp = fwa->flags & IPFW_ARGS_OUT ? fwa->ifp : NULL;
	/* dt->output tame is updated as we move through */
	dt->output_time = dn_cfg.curr_time;
	dt->iphdr_off = (dir & PROTO_LAYER2) ? ETHER_HDR_LEN : 0;
	return 0;
}


/*
 * dummynet hook for packets.
 * We use the argument to locate the flowset fs and the sched_set sch
 * associated to it. The we apply flow_mask and sched_mask to
 * determine the queue and scheduler instances.
 */
int
dummynet_io(struct mbuf **m0, struct ip_fw_args *fwa)
{
	struct mbuf *m = *m0;
	struct dn_fsk *fs = NULL;
	struct dn_sch_inst *si;
	struct dn_queue *q = NULL;	/* default */
	int fs_id, dir;

	fs_id = (fwa->rule.info & IPFW_INFO_MASK) +
		((fwa->rule.info & IPFW_IS_PIPE) ? 2*DN_MAX_ID : 0);
	/* XXXGL: convert args to dir */
	if (fwa->flags & IPFW_ARGS_IN)
		dir = DIR_IN;
	else
		dir = DIR_OUT;
	if (fwa->flags & IPFW_ARGS_ETHER)
		dir |= PROTO_LAYER2;
	else if (fwa->flags & IPFW_ARGS_IP6)
		dir |= PROTO_IPV6;
	DN_BH_WLOCK();
	io_pkt++;
	/* we could actually tag outside the lock, but who cares... */
	if (tag_mbuf(m, dir, fwa))
		goto dropit;
	if (dn_cfg.busy) {
		/* if the upper half is busy doing something expensive,
		 * lets queue the packet and move forward
		 */
		mq_append(&dn_cfg.pending, m);
		m = *m0 = NULL; /* consumed */
		goto done; /* already active, nothing to do */
	}
	/* XXX locate_flowset could be optimised with a direct ref. */
	fs = dn_ht_find(dn_cfg.fshash, fs_id, 0, NULL);
	if (fs == NULL)
		goto dropit;	/* This queue/pipe does not exist! */
	if (fs->sched == NULL)	/* should not happen */
		goto dropit;
	/* find scheduler instance, possibly applying sched_mask */
	si = ipdn_si_find(fs->sched, &(fwa->f_id));
	if (si == NULL)
		goto dropit;
	/*
	 * If the scheduler supports multiple queues, find the right one
	 * (otherwise it will be ignored by enqueue).
	 */
	if (fs->sched->fp->flags & DN_MULTIQUEUE) {
		q = ipdn_q_find(fs, si, &(fwa->f_id));
		if (q == NULL)
			goto dropit;
	}
	if (fs->sched->fp->enqueue(si, q, m)) {
		/* packet was dropped by enqueue() */
		m = *m0 = NULL;

		/* dn_enqueue already increases io_pkt_drop */
		io_pkt_drop--;

		goto dropit;
	}

	if (si->kflags & DN_ACTIVE) {
		m = *m0 = NULL; /* consumed */
		goto done; /* already active, nothing to do */
	}

	/* compute the initial allowance */
	if (si->idle_time < dn_cfg.curr_time) {
	    /* Do this only on the first packet on an idle pipe */
	    struct dn_link *p = &fs->sched->link;

	    si->sched_time = dn_cfg.curr_time;
	    si->credit = dn_cfg.io_fast ? p->bandwidth : 0;
	    if (p->burst) {
		uint64_t burst = (dn_cfg.curr_time - si->idle_time) * p->bandwidth;
		if (burst > p->burst)
			burst = p->burst;
		si->credit += burst;
	    }
	}
	/* pass through scheduler and delay line */
	m = serve_sched(NULL, si, dn_cfg.curr_time);

	/* optimization -- pass it back to ipfw for immediate send */
	/* XXX Don't call dummynet_send() if scheduler return the packet
	 *     just enqueued. This avoid a lock order reversal.
	 *     
	 */
	if (/*dn_cfg.io_fast &&*/ m == *m0 && (dir & PROTO_LAYER2) == 0 ) {
		/* fast io, rename the tag * to carry reinject info. */
		struct m_tag *tag = m_tag_first(m);

		tag->m_tag_cookie = MTAG_IPFW_RULE;
		tag->m_tag_id = 0;
		io_pkt_fast++;
		if (m->m_nextpkt != NULL) {
			printf("dummynet: fast io: pkt chain detected!\n");
			m->m_nextpkt = NULL;
		}
		m = NULL;
	} else {
		*m0 = NULL;
	}
done:
	DN_BH_WUNLOCK();
	if (m)
		dummynet_send(m);
	return 0;

dropit:
	io_pkt_drop++;
	DN_BH_WUNLOCK();
	if (m)
		FREE_PKT(m);
	*m0 = NULL;
	return (fs && (fs->fs.flags & DN_NOERROR)) ? 0 : ENOBUFS;
}
