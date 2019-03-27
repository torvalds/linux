/*
 * Copyright (c) 2008 The DragonFly Project.  All rights reserved.
 * 
 * This code is derived from software contributed to The DragonFly Project
 * by Matthew Dillon <dillon@backplane.com>
 * 
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 * 3. Neither the name of The DragonFly Project nor the names of its
 *    contributors may be used to endorse or promote products derived
 *    from this software without specific, prior written permission.
 * 
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE
 * COPYRIGHT HOLDERS OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED
 * AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT
 * OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 * 
 * $DragonFly: src/sys/net/altq/altq_fairq.c,v 1.1 2008/04/06 18:58:15 dillon Exp $
 * $FreeBSD$
 */
/*
 * Matt: I gutted altq_priq.c and used it as a skeleton on which to build
 * fairq.  The fairq algorithm is completely different then priq, of course,
 * but because I used priq's skeleton I believe I should include priq's
 * copyright.
 *
 * Copyright (C) 2000-2003
 *	Sony Computer Science Laboratories Inc.  All rights reserved.
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
 * THIS SOFTWARE IS PROVIDED BY SONY CSL AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL SONY CSL OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

/*
 * FAIRQ - take traffic classified by keep state (hashed into
 * mbuf->m_pkthdr.altq_state_hash) and bucketize it.  Fairly extract
 * the first packet from each bucket in a round-robin fashion.
 *
 * TODO - better overall qlimit support (right now it is per-bucket).
 *	- NOTE: red etc is per bucket, not overall.
 *	- better service curve support.
 *
 * EXAMPLE:
 *
 *  altq on em0 fairq bandwidth 650Kb queue { std, bulk }
 *  queue std  priority 3 bandwidth 400Kb \
 *	fairq (buckets 64, default, hogs 1Kb) qlimit 50
 *  queue bulk priority 2 bandwidth 100Kb \
 *	fairq (buckets 64, hogs 1Kb) qlimit 50
 *
 *  pass out on em0 from any to any keep state queue std
 *  pass out on em0 inet proto tcp ..... port ... keep state queue bulk
 */
#include "opt_altq.h"
#include "opt_inet.h"
#include "opt_inet6.h"

#ifdef ALTQ_FAIRQ  /* fairq is enabled in the kernel conf */

#include <sys/param.h>
#include <sys/malloc.h>
#include <sys/mbuf.h>
#include <sys/socket.h>
#include <sys/sockio.h>
#include <sys/systm.h>
#include <sys/proc.h>
#include <sys/errno.h>
#include <sys/kernel.h>
#include <sys/queue.h>

#include <net/if.h>
#include <net/if_var.h>
#include <netinet/in.h>

#include <netpfil/pf/pf.h>
#include <netpfil/pf/pf_altq.h>
#include <netpfil/pf/pf_mtag.h>
#include <net/altq/altq.h>
#include <net/altq/altq_fairq.h>

/*
 * function prototypes
 */
static int	fairq_clear_interface(struct fairq_if *);
static int	fairq_request(struct ifaltq *, int, void *);
static void	fairq_purge(struct fairq_if *);
static struct fairq_class *fairq_class_create(struct fairq_if *, int, int, u_int, struct fairq_opts *, int);
static int	fairq_class_destroy(struct fairq_class *);
static int	fairq_enqueue(struct ifaltq *, struct mbuf *, struct altq_pktattr *);
static struct mbuf *fairq_dequeue(struct ifaltq *, int);

static int	fairq_addq(struct fairq_class *, struct mbuf *, u_int32_t);
static struct mbuf *fairq_getq(struct fairq_class *, uint64_t);
static struct mbuf *fairq_pollq(struct fairq_class *, uint64_t, int *);
static fairq_bucket_t *fairq_selectq(struct fairq_class *, int);
static void	fairq_purgeq(struct fairq_class *);

static void	get_class_stats(struct fairq_classstats *, struct fairq_class *);
static struct fairq_class *clh_to_clp(struct fairq_if *, uint32_t);

int
fairq_pfattach(struct pf_altq *a)
{
	struct ifnet *ifp;
	int error;

	if ((ifp = ifunit(a->ifname)) == NULL || a->altq_disc == NULL)
		return (EINVAL);

	error = altq_attach(&ifp->if_snd, ALTQT_FAIRQ, a->altq_disc,
	    fairq_enqueue, fairq_dequeue, fairq_request, NULL, NULL);

	return (error);
}

int
fairq_add_altq(struct ifnet *ifp, struct pf_altq *a)
{
	struct fairq_if *pif;

	if (ifp == NULL)
		return (EINVAL);
	if (!ALTQ_IS_READY(&ifp->if_snd))
		return (ENODEV);


	pif = malloc(sizeof(struct fairq_if),
			M_DEVBUF, M_WAITOK | M_ZERO);
	pif->pif_bandwidth = a->ifbandwidth;
	pif->pif_maxpri = -1;
	pif->pif_ifq = &ifp->if_snd;

	/* keep the state in pf_altq */
	a->altq_disc = pif;

	return (0);
}

int
fairq_remove_altq(struct pf_altq *a)
{
	struct fairq_if *pif;

	if ((pif = a->altq_disc) == NULL)
		return (EINVAL);
	a->altq_disc = NULL;

	fairq_clear_interface(pif);

	free(pif, M_DEVBUF);
	return (0);
}

int
fairq_add_queue(struct pf_altq *a)
{
	struct fairq_if *pif;
	struct fairq_class *cl;

	if ((pif = a->altq_disc) == NULL)
		return (EINVAL);

	/* check parameters */
	if (a->priority >= FAIRQ_MAXPRI)
		return (EINVAL);
	if (a->qid == 0)
		return (EINVAL);
	if (pif->pif_classes[a->priority] != NULL)
		return (EBUSY);
	if (clh_to_clp(pif, a->qid) != NULL)
		return (EBUSY);

	cl = fairq_class_create(pif, a->priority, a->qlimit, a->bandwidth,
			       &a->pq_u.fairq_opts, a->qid);
	if (cl == NULL)
		return (ENOMEM);

	return (0);
}

int
fairq_remove_queue(struct pf_altq *a)
{
	struct fairq_if *pif;
	struct fairq_class *cl;

	if ((pif = a->altq_disc) == NULL)
		return (EINVAL);

	if ((cl = clh_to_clp(pif, a->qid)) == NULL)
		return (EINVAL);

	return (fairq_class_destroy(cl));
}

int
fairq_getqstats(struct pf_altq *a, void *ubuf, int *nbytes, int version)
{
	struct fairq_if *pif;
	struct fairq_class *cl;
	struct fairq_classstats stats;
	int error = 0;

	if ((pif = altq_lookup(a->ifname, ALTQT_FAIRQ)) == NULL)
		return (EBADF);

	if ((cl = clh_to_clp(pif, a->qid)) == NULL)
		return (EINVAL);

	if (*nbytes < sizeof(stats))
		return (EINVAL);

	get_class_stats(&stats, cl);

	if ((error = copyout((caddr_t)&stats, ubuf, sizeof(stats))) != 0)
		return (error);
	*nbytes = sizeof(stats);
	return (0);
}

/*
 * bring the interface back to the initial state by discarding
 * all the filters and classes.
 */
static int
fairq_clear_interface(struct fairq_if *pif)
{
	struct fairq_class *cl;
	int pri;

	/* clear out the classes */
	for (pri = 0; pri <= pif->pif_maxpri; pri++) {
		if ((cl = pif->pif_classes[pri]) != NULL)
			fairq_class_destroy(cl);
	}

	return (0);
}

static int
fairq_request(struct ifaltq *ifq, int req, void *arg)
{
	struct fairq_if *pif = (struct fairq_if *)ifq->altq_disc;

	IFQ_LOCK_ASSERT(ifq);

	switch (req) {
	case ALTRQ_PURGE:
		fairq_purge(pif);
		break;
	}
	return (0);
}

/* discard all the queued packets on the interface */
static void
fairq_purge(struct fairq_if *pif)
{
	struct fairq_class *cl;
	int pri;

	for (pri = 0; pri <= pif->pif_maxpri; pri++) {
		if ((cl = pif->pif_classes[pri]) != NULL && cl->cl_head)
			fairq_purgeq(cl);
	}
	if (ALTQ_IS_ENABLED(pif->pif_ifq))
		pif->pif_ifq->ifq_len = 0;
}

static struct fairq_class *
fairq_class_create(struct fairq_if *pif, int pri, int qlimit,
		   u_int bandwidth, struct fairq_opts *opts, int qid)
{
	struct fairq_class *cl;
	int flags = opts->flags;
	u_int nbuckets = opts->nbuckets;
	int i;

#ifndef ALTQ_RED
	if (flags & FARF_RED) {
#ifdef ALTQ_DEBUG
		printf("fairq_class_create: RED not configured for FAIRQ!\n");
#endif
		return (NULL);
	}
#endif
#ifndef ALTQ_CODEL
	if (flags & FARF_CODEL) {
#ifdef ALTQ_DEBUG
		printf("fairq_class_create: CODEL not configured for FAIRQ!\n");
#endif
		return (NULL);
	}
#endif
	if (nbuckets == 0)
		nbuckets = 256;
	if (nbuckets > FAIRQ_MAX_BUCKETS)
		nbuckets = FAIRQ_MAX_BUCKETS;
	/* enforce power-of-2 size */
	while ((nbuckets ^ (nbuckets - 1)) != ((nbuckets << 1) - 1))
		++nbuckets;

	if ((cl = pif->pif_classes[pri]) != NULL) {
		/* modify the class instead of creating a new one */
		IFQ_LOCK(cl->cl_pif->pif_ifq);
		if (cl->cl_head)
			fairq_purgeq(cl);
		IFQ_UNLOCK(cl->cl_pif->pif_ifq);
#ifdef ALTQ_RIO
		if (cl->cl_qtype == Q_RIO)
			rio_destroy((rio_t *)cl->cl_red);
#endif
#ifdef ALTQ_RED
		if (cl->cl_qtype == Q_RED)
			red_destroy(cl->cl_red);
#endif
#ifdef ALTQ_CODEL
		if (cl->cl_qtype == Q_CODEL)
			codel_destroy(cl->cl_codel);
#endif
	} else {
		cl = malloc(sizeof(struct fairq_class),
				M_DEVBUF, M_WAITOK | M_ZERO);
		cl->cl_nbuckets = nbuckets;
		cl->cl_nbucket_mask = nbuckets - 1;

		cl->cl_buckets = malloc(
			sizeof(struct fairq_bucket) * cl->cl_nbuckets,
			M_DEVBUF, M_WAITOK | M_ZERO);
		cl->cl_head = NULL;
	}

	pif->pif_classes[pri] = cl;
	if (flags & FARF_DEFAULTCLASS)
		pif->pif_default = cl;
	if (qlimit == 0)
		qlimit = 50;  /* use default */
	cl->cl_qlimit = qlimit;
	for (i = 0; i < cl->cl_nbuckets; ++i) {
		qlimit(&cl->cl_buckets[i].queue) = qlimit;
	}
	cl->cl_bandwidth = bandwidth / 8;
	cl->cl_qtype = Q_DROPTAIL;
	cl->cl_flags = flags & FARF_USERFLAGS;
	cl->cl_pri = pri;
	if (pri > pif->pif_maxpri)
		pif->pif_maxpri = pri;
	cl->cl_pif = pif;
	cl->cl_handle = qid;
	cl->cl_hogs_m1 = opts->hogs_m1 / 8;
	cl->cl_lssc_m1 = opts->lssc_m1 / 8;	/* NOT YET USED */

#ifdef ALTQ_RED
	if (flags & (FARF_RED|FARF_RIO)) {
		int red_flags, red_pkttime;

		red_flags = 0;
		if (flags & FARF_ECN)
			red_flags |= REDF_ECN;
#ifdef ALTQ_RIO
		if (flags & FARF_CLEARDSCP)
			red_flags |= RIOF_CLEARDSCP;
#endif
		if (pif->pif_bandwidth < 8)
			red_pkttime = 1000 * 1000 * 1000; /* 1 sec */
		else
			red_pkttime = (int64_t)pif->pif_ifq->altq_ifp->if_mtu
			  * 1000 * 1000 * 1000 / (pif->pif_bandwidth / 8);
#ifdef ALTQ_RIO
		if (flags & FARF_RIO) {
			cl->cl_red = (red_t *)rio_alloc(0, NULL,
						red_flags, red_pkttime);
			if (cl->cl_red != NULL)
				cl->cl_qtype = Q_RIO;
		} else
#endif
		if (flags & FARF_RED) {
			cl->cl_red = red_alloc(0, 0,
			    cl->cl_qlimit * 10/100,
			    cl->cl_qlimit * 30/100,
			    red_flags, red_pkttime);
			if (cl->cl_red != NULL)
				cl->cl_qtype = Q_RED;
		}
	}
#endif /* ALTQ_RED */
#ifdef ALTQ_CODEL
	if (flags & FARF_CODEL) {
		cl->cl_codel = codel_alloc(5, 100, 0);
		if (cl->cl_codel != NULL)
			cl->cl_qtype = Q_CODEL;
	}
#endif

	return (cl);
}

static int
fairq_class_destroy(struct fairq_class *cl)
{
	struct fairq_if *pif;
	int pri;

	IFQ_LOCK(cl->cl_pif->pif_ifq);

	if (cl->cl_head)
		fairq_purgeq(cl);

	pif = cl->cl_pif;
	pif->pif_classes[cl->cl_pri] = NULL;
	if (pif->pif_poll_cache == cl)
		pif->pif_poll_cache = NULL;
	if (pif->pif_maxpri == cl->cl_pri) {
		for (pri = cl->cl_pri; pri >= 0; pri--)
			if (pif->pif_classes[pri] != NULL) {
				pif->pif_maxpri = pri;
				break;
			}
		if (pri < 0)
			pif->pif_maxpri = -1;
	}
	IFQ_UNLOCK(cl->cl_pif->pif_ifq);

	if (cl->cl_red != NULL) {
#ifdef ALTQ_RIO
		if (cl->cl_qtype == Q_RIO)
			rio_destroy((rio_t *)cl->cl_red);
#endif
#ifdef ALTQ_RED
		if (cl->cl_qtype == Q_RED)
			red_destroy(cl->cl_red);
#endif
#ifdef ALTQ_CODEL
		if (cl->cl_qtype == Q_CODEL)
			codel_destroy(cl->cl_codel);
#endif
	}
	free(cl->cl_buckets, M_DEVBUF);
	free(cl, M_DEVBUF);

	return (0);
}

/*
 * fairq_enqueue is an enqueue function to be registered to
 * (*altq_enqueue) in struct ifaltq.
 */
static int
fairq_enqueue(struct ifaltq *ifq, struct mbuf *m, struct altq_pktattr *pktattr)
{
	struct fairq_if *pif = (struct fairq_if *)ifq->altq_disc;
	struct fairq_class *cl = NULL; /* Make compiler happy */
	struct pf_mtag *t;
	u_int32_t qid_hash = 0;
	int len;

	IFQ_LOCK_ASSERT(ifq);

	/* grab class set by classifier */
	if ((m->m_flags & M_PKTHDR) == 0) {
		/* should not happen */
		printf("altq: packet for %s does not have pkthdr\n",
			ifq->altq_ifp->if_xname);
		m_freem(m);
		return (ENOBUFS);
	}

	if ((t = pf_find_mtag(m)) != NULL) {
		cl = clh_to_clp(pif, t->qid);
		qid_hash = t->qid_hash;
	}
	if (cl == NULL) {
		cl = pif->pif_default;
		if (cl == NULL) {
			m_freem(m);
			return (ENOBUFS);
		}
	}
	cl->cl_flags |= FARF_HAS_PACKETS;
	cl->cl_pktattr = NULL;
	len = m_pktlen(m);
	if (fairq_addq(cl, m, qid_hash) != 0) {
		/* drop occurred.  mbuf was freed in fairq_addq. */
		PKTCNTR_ADD(&cl->cl_dropcnt, len);
		return (ENOBUFS);
	}
	IFQ_INC_LEN(ifq);

	return (0);
}

/*
 * fairq_dequeue is a dequeue function to be registered to
 * (*altq_dequeue) in struct ifaltq.
 *
 * note: ALTDQ_POLL returns the next packet without removing the packet
 *	from the queue.  ALTDQ_REMOVE is a normal dequeue operation.
 *	ALTDQ_REMOVE must return the same packet if called immediately
 *	after ALTDQ_POLL.
 */
static struct mbuf *
fairq_dequeue(struct ifaltq *ifq, int op)
{
	struct fairq_if *pif = (struct fairq_if *)ifq->altq_disc;
	struct fairq_class *cl;
	struct fairq_class *best_cl;
	struct mbuf *best_m;
	struct mbuf *m = NULL;
	uint64_t cur_time = read_machclk();
	int pri;
	int hit_limit;

	IFQ_LOCK_ASSERT(ifq);

	if (IFQ_IS_EMPTY(ifq)) {
		return (NULL);
	}

	if (pif->pif_poll_cache && op == ALTDQ_REMOVE) {
		best_cl = pif->pif_poll_cache;
		m = fairq_getq(best_cl, cur_time);
		pif->pif_poll_cache = NULL;
		if (m) {
			IFQ_DEC_LEN(ifq);
			PKTCNTR_ADD(&best_cl->cl_xmitcnt, m_pktlen(m));
			return (m);
		}
	} else {
		best_cl = NULL;
		best_m = NULL;

		for (pri = pif->pif_maxpri;  pri >= 0; pri--) {
			if ((cl = pif->pif_classes[pri]) == NULL)
				continue;
			if ((cl->cl_flags & FARF_HAS_PACKETS) == 0)
				continue;
			m = fairq_pollq(cl, cur_time, &hit_limit);
			if (m == NULL) {
				cl->cl_flags &= ~FARF_HAS_PACKETS;
				continue;
			}

			/*
			 * Only override the best choice if we are under
			 * the BW limit.
			 */
			if (hit_limit == 0 || best_cl == NULL) {
				best_cl = cl;
				best_m = m;
			}

			/*
			 * Remember the highest priority mbuf in case we
			 * do not find any lower priority mbufs.
			 */
			if (hit_limit)
				continue;
			break;
		}
		if (op == ALTDQ_POLL) {
			pif->pif_poll_cache = best_cl;
			m = best_m;
		} else if (best_cl) {
			m = fairq_getq(best_cl, cur_time);
			if (m != NULL) {
				IFQ_DEC_LEN(ifq);
				PKTCNTR_ADD(&best_cl->cl_xmitcnt, m_pktlen(m));
			}
		} 
		return (m);
	}
	return (NULL);
}

static int
fairq_addq(struct fairq_class *cl, struct mbuf *m, u_int32_t bucketid)
{
	fairq_bucket_t *b;
	u_int hindex;
	uint64_t bw;

	/*
	 * If the packet doesn't have any keep state put it on the end of
	 * our queue.  XXX this can result in out of order delivery.
	 */
	if (bucketid == 0) {
		if (cl->cl_head)
			b = cl->cl_head->prev;
		else
			b = &cl->cl_buckets[0];
	} else {
		hindex = bucketid & cl->cl_nbucket_mask;
		b = &cl->cl_buckets[hindex];
	}

	/*
	 * Add the bucket to the end of the circular list of active buckets.
	 *
	 * As a special case we add the bucket to the beginning of the list
	 * instead of the end if it was not previously on the list and if
	 * its traffic is less then the hog level.
	 */
	if (b->in_use == 0) {
		b->in_use = 1;
		if (cl->cl_head == NULL) {
			cl->cl_head = b;
			b->next = b;
			b->prev = b;
		} else {
			b->next = cl->cl_head;
			b->prev = cl->cl_head->prev;
			b->prev->next = b;
			b->next->prev = b;

			if (b->bw_delta && cl->cl_hogs_m1) {
				bw = b->bw_bytes * machclk_freq / b->bw_delta;
				if (bw < cl->cl_hogs_m1)
					cl->cl_head = b;
			}
		}
	}

#ifdef ALTQ_RIO
	if (cl->cl_qtype == Q_RIO)
		return rio_addq((rio_t *)cl->cl_red, &b->queue, m, cl->cl_pktattr);
#endif
#ifdef ALTQ_RED
	if (cl->cl_qtype == Q_RED)
		return red_addq(cl->cl_red, &b->queue, m, cl->cl_pktattr);
#endif
#ifdef ALTQ_CODEL
	if (cl->cl_qtype == Q_CODEL)
		return codel_addq(cl->cl_codel, &b->queue, m);
#endif
	if (qlen(&b->queue) >= qlimit(&b->queue)) {
		m_freem(m);
		return (-1);
	}

	if (cl->cl_flags & FARF_CLEARDSCP)
		write_dsfield(m, cl->cl_pktattr, 0);

	_addq(&b->queue, m);

	return (0);
}

static struct mbuf *
fairq_getq(struct fairq_class *cl, uint64_t cur_time)
{
	fairq_bucket_t *b;
	struct mbuf *m;

	b = fairq_selectq(cl, 0);
	if (b == NULL)
		m = NULL;
#ifdef ALTQ_RIO
	else if (cl->cl_qtype == Q_RIO)
		m = rio_getq((rio_t *)cl->cl_red, &b->queue);
#endif
#ifdef ALTQ_RED
	else if (cl->cl_qtype == Q_RED)
		m = red_getq(cl->cl_red, &b->queue);
#endif
#ifdef ALTQ_CODEL
	else if (cl->cl_qtype == Q_CODEL)
		m = codel_getq(cl->cl_codel, &b->queue);
#endif
	else
		m = _getq(&b->queue);

	/*
	 * Calculate the BW change
	 */
	if (m != NULL) {
		uint64_t delta;

		/*
		 * Per-class bandwidth calculation
		 */
		delta = (cur_time - cl->cl_last_time);
		if (delta > machclk_freq * 8)
			delta = machclk_freq * 8;
		cl->cl_bw_delta += delta;
		cl->cl_bw_bytes += m->m_pkthdr.len;
		cl->cl_last_time = cur_time;
		cl->cl_bw_delta -= cl->cl_bw_delta >> 3;
		cl->cl_bw_bytes -= cl->cl_bw_bytes >> 3;

		/*
		 * Per-bucket bandwidth calculation
		 */
		delta = (cur_time - b->last_time);
		if (delta > machclk_freq * 8)
			delta = machclk_freq * 8;
		b->bw_delta += delta;
		b->bw_bytes += m->m_pkthdr.len;
		b->last_time = cur_time;
		b->bw_delta -= b->bw_delta >> 3;
		b->bw_bytes -= b->bw_bytes >> 3;
	}
	return(m);
}

/*
 * Figure out what the next packet would be if there were no limits.  If
 * this class hits its bandwidth limit *hit_limit is set to no-zero, otherwise
 * it is set to 0.  A non-NULL mbuf is returned either way.
 */
static struct mbuf *
fairq_pollq(struct fairq_class *cl, uint64_t cur_time, int *hit_limit)
{
	fairq_bucket_t *b;
	struct mbuf *m;
	uint64_t delta;
	uint64_t bw;

	*hit_limit = 0;
	b = fairq_selectq(cl, 1);
	if (b == NULL)
		return(NULL);
	m = qhead(&b->queue);

	/*
	 * Did this packet exceed the class bandwidth?  Calculate the
	 * bandwidth component of the packet.
	 *
	 * - Calculate bytes per second
	 */
	delta = cur_time - cl->cl_last_time;
	if (delta > machclk_freq * 8)
		delta = machclk_freq * 8;
	cl->cl_bw_delta += delta;
	cl->cl_last_time = cur_time;
	if (cl->cl_bw_delta) {
		bw = cl->cl_bw_bytes * machclk_freq / cl->cl_bw_delta;

		if (bw > cl->cl_bandwidth)
			*hit_limit = 1;
#ifdef ALTQ_DEBUG
		printf("BW %6ju relative to %6u %d queue %p\n",
			(uintmax_t)bw, cl->cl_bandwidth, *hit_limit, b);
#endif
	}
	return(m);
}

/*
 * Locate the next queue we want to pull a packet out of.  This code
 * is also responsible for removing empty buckets from the circular list.
 */
static
fairq_bucket_t *
fairq_selectq(struct fairq_class *cl, int ispoll)
{
	fairq_bucket_t *b;
	uint64_t bw;

	if (ispoll == 0 && cl->cl_polled) {
		b = cl->cl_polled;
		cl->cl_polled = NULL;
		return(b);
	}

	while ((b = cl->cl_head) != NULL) {
		/*
		 * Remove empty queues from consideration
		 */
		if (qempty(&b->queue)) {
			b->in_use = 0;
			cl->cl_head = b->next;
			if (cl->cl_head == b) {
				cl->cl_head = NULL;
			} else {
				b->next->prev = b->prev;
				b->prev->next = b->next;
			}
			continue;
		}

		/*
		 * Advance the round robin.  Queues with bandwidths less
		 * then the hog bandwidth are allowed to burst.
		 */
		if (cl->cl_hogs_m1 == 0) {
			cl->cl_head = b->next;
		} else if (b->bw_delta) {
			bw = b->bw_bytes * machclk_freq / b->bw_delta;
			if (bw >= cl->cl_hogs_m1) {
				cl->cl_head = b->next;
			}
			/*
			 * XXX TODO - 
			 */
		}

		/*
		 * Return bucket b.
		 */
		break;
	}
	if (ispoll)
		cl->cl_polled = b;
	return(b);
}

static void
fairq_purgeq(struct fairq_class *cl)
{
	fairq_bucket_t *b;
	struct mbuf *m;

	while ((b = fairq_selectq(cl, 0)) != NULL) {
		while ((m = _getq(&b->queue)) != NULL) {
			PKTCNTR_ADD(&cl->cl_dropcnt, m_pktlen(m));
			m_freem(m);
		}
		ASSERT(qlen(&b->queue) == 0);
	}
}

static void
get_class_stats(struct fairq_classstats *sp, struct fairq_class *cl)
{
	fairq_bucket_t *b;

	sp->class_handle = cl->cl_handle;
	sp->qlimit = cl->cl_qlimit;
	sp->xmit_cnt = cl->cl_xmitcnt;
	sp->drop_cnt = cl->cl_dropcnt;
	sp->qtype = cl->cl_qtype;
	sp->qlength = 0;

	if (cl->cl_head) {
		b = cl->cl_head;
		do {
			sp->qlength += qlen(&b->queue);
			b = b->next;
		} while (b != cl->cl_head);
	}

#ifdef ALTQ_RED
	if (cl->cl_qtype == Q_RED)
		red_getstats(cl->cl_red, &sp->red[0]);
#endif
#ifdef ALTQ_RIO
	if (cl->cl_qtype == Q_RIO)
		rio_getstats((rio_t *)cl->cl_red, &sp->red[0]);
#endif
#ifdef ALTQ_CODEL
	if (cl->cl_qtype == Q_CODEL)
		codel_getstats(cl->cl_codel, &sp->codel);
#endif
}

/* convert a class handle to the corresponding class pointer */
static struct fairq_class *
clh_to_clp(struct fairq_if *pif, uint32_t chandle)
{
	struct fairq_class *cl;
	int idx;

	if (chandle == 0)
		return (NULL);

	for (idx = pif->pif_maxpri; idx >= 0; idx--)
		if ((cl = pif->pif_classes[idx]) != NULL &&
		    cl->cl_handle == chandle)
			return (cl);

	return (NULL);
}

#endif /* ALTQ_FAIRQ */
