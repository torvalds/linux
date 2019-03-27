/*
 * CoDel - The Controlled-Delay Active Queue Management algorithm
 *
 *  Copyright (C) 2013 Ermal Luçi <eri@FreeBSD.org>
 *  Copyright (C) 2011-2012 Kathleen Nichols <nichols@pollere.com>
 *  Copyright (C) 2011-2012 Van Jacobson <van@pollere.net>
 *  Copyright (C) 2012 Michael D. Taht <dave.taht@bufferbloat.net>
 *  Copyright (C) 2012 Eric Dumazet <edumazet@google.com>
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions, and the following disclaimer,
 *    without modification.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. The names of the authors may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * Alternatively, provided that this notice is retained in full, this
 * software may be distributed under the terms of the GNU General
 * Public License ("GPL") version 2, in which case the provisions of the
 * GPL apply INSTEAD OF those given above.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH
 * DAMAGE.
 *
 * $FreeBSD$
 */
#include "opt_altq.h"
#include "opt_inet.h"
#include "opt_inet6.h"

#ifdef ALTQ_CODEL  /* CoDel is enabled by ALTQ_CODEL option in opt_altq.h */

#include <sys/param.h>
#include <sys/malloc.h>
#include <sys/mbuf.h>
#include <sys/socket.h>
#include <sys/systm.h>

#include <net/if.h>
#include <net/if_var.h>
#include <netinet/in.h>

#include <netpfil/pf/pf.h>
#include <netpfil/pf/pf_altq.h>
#include <net/altq/if_altq.h>
#include <net/altq/altq.h>
#include <net/altq/altq_codel.h>

static int		 codel_should_drop(struct codel *, class_queue_t *,
			    struct mbuf *, u_int64_t);
static void		 codel_Newton_step(struct codel_vars *);
static u_int64_t	 codel_control_law(u_int64_t t, u_int64_t, u_int32_t);

#define	codel_time_after(a, b)		((int64_t)(a) - (int64_t)(b) > 0)
#define	codel_time_after_eq(a, b)	((int64_t)(a) - (int64_t)(b) >= 0)
#define	codel_time_before(a, b)		((int64_t)(a) - (int64_t)(b) < 0)
#define	codel_time_before_eq(a, b)	((int64_t)(a) - (int64_t)(b) <= 0)

static int codel_request(struct ifaltq *, int, void *);

static int codel_enqueue(struct ifaltq *, struct mbuf *, struct altq_pktattr *);
static struct mbuf *codel_dequeue(struct ifaltq *, int);

int
codel_pfattach(struct pf_altq *a)
{
	struct ifnet *ifp;

	if ((ifp = ifunit(a->ifname)) == NULL || a->altq_disc == NULL)
		return (EINVAL);

	return (altq_attach(&ifp->if_snd, ALTQT_CODEL, a->altq_disc,
	    codel_enqueue, codel_dequeue, codel_request, NULL, NULL));
}

int
codel_add_altq(struct ifnet *ifp, struct pf_altq *a)
{
	struct codel_if	*cif;
	struct codel_opts	*opts;

	if (ifp == NULL)
		return (EINVAL);
	if (!ALTQ_IS_READY(&ifp->if_snd))
		return (ENODEV);

	opts = &a->pq_u.codel_opts;

	cif = malloc(sizeof(struct codel_if), M_DEVBUF, M_NOWAIT | M_ZERO);
	if (cif == NULL)
		return (ENOMEM);
	cif->cif_bandwidth = a->ifbandwidth;
	cif->cif_ifq = &ifp->if_snd;

	cif->cl_q = malloc(sizeof(class_queue_t), M_DEVBUF, M_NOWAIT | M_ZERO);
	if (cif->cl_q == NULL) {
		free(cif, M_DEVBUF);
		return (ENOMEM);
	}

	if (a->qlimit == 0)
		a->qlimit = 50;	/* use default. */
	qlimit(cif->cl_q) = a->qlimit;
	qtype(cif->cl_q) = Q_CODEL;
	qlen(cif->cl_q) = 0;
	qsize(cif->cl_q) = 0;

	if (opts->target == 0)
		opts->target = 5;
	if (opts->interval == 0)
		opts->interval = 100;
	cif->codel.params.target = machclk_freq * opts->target / 1000;
	cif->codel.params.interval = machclk_freq * opts->interval / 1000;
	cif->codel.params.ecn = opts->ecn;
	cif->codel.stats.maxpacket = 256;

	cif->cl_stats.qlength = qlen(cif->cl_q);
	cif->cl_stats.qlimit = qlimit(cif->cl_q);

	/* keep the state in pf_altq */
	a->altq_disc = cif;

	return (0);
}

int
codel_remove_altq(struct pf_altq *a)
{
	struct codel_if *cif;

	if ((cif = a->altq_disc) == NULL)
		return (EINVAL);
	a->altq_disc = NULL;

	if (cif->cl_q)
		free(cif->cl_q, M_DEVBUF);
	free(cif, M_DEVBUF);

	return (0);
}

int
codel_getqstats(struct pf_altq *a, void *ubuf, int *nbytes, int version)
{
	struct codel_if *cif;
	struct codel_ifstats stats;
	int error = 0;

	if ((cif = altq_lookup(a->ifname, ALTQT_CODEL)) == NULL)
		return (EBADF);

	if (*nbytes < sizeof(stats))
		return (EINVAL);

	stats = cif->cl_stats;
	stats.stats = cif->codel.stats;

	if ((error = copyout((caddr_t)&stats, ubuf, sizeof(stats))) != 0)
		return (error);
	*nbytes = sizeof(stats);

	return (0);
}

static int
codel_request(struct ifaltq *ifq, int req, void *arg)
{
	struct codel_if	*cif = (struct codel_if *)ifq->altq_disc;
	struct mbuf *m;

	IFQ_LOCK_ASSERT(ifq);

	switch (req) {
	case ALTRQ_PURGE:
		if (!ALTQ_IS_ENABLED(cif->cif_ifq))
			break;

		if (qempty(cif->cl_q))
			break;

		while ((m = _getq(cif->cl_q)) != NULL) {
			PKTCNTR_ADD(&cif->cl_stats.cl_dropcnt, m_pktlen(m));
			m_freem(m);
			IFQ_DEC_LEN(cif->cif_ifq);
		}
		cif->cif_ifq->ifq_len = 0;
		break;
	}

	return (0);
}

static int
codel_enqueue(struct ifaltq *ifq, struct mbuf *m, struct altq_pktattr *pktattr)
{

	struct codel_if *cif = (struct codel_if *) ifq->altq_disc;

	IFQ_LOCK_ASSERT(ifq);

	/* grab class set by classifier */
	if ((m->m_flags & M_PKTHDR) == 0) {
		/* should not happen */
		printf("altq: packet for %s does not have pkthdr\n",
		   ifq->altq_ifp->if_xname);
		m_freem(m);
		PKTCNTR_ADD(&cif->cl_stats.cl_dropcnt, m_pktlen(m));
		return (ENOBUFS);
	}

	if (codel_addq(&cif->codel, cif->cl_q, m)) {
		PKTCNTR_ADD(&cif->cl_stats.cl_dropcnt, m_pktlen(m));
		return (ENOBUFS);
	}
	IFQ_INC_LEN(ifq);

	return (0);
}

static struct mbuf *
codel_dequeue(struct ifaltq *ifq, int op)
{
	struct codel_if *cif = (struct codel_if *)ifq->altq_disc;
	struct mbuf *m;

	IFQ_LOCK_ASSERT(ifq);

	if (IFQ_IS_EMPTY(ifq))
		return (NULL);

	if (op == ALTDQ_POLL)
		return (qhead(cif->cl_q));


	m = codel_getq(&cif->codel, cif->cl_q);
	if (m != NULL) {
		IFQ_DEC_LEN(ifq);
		PKTCNTR_ADD(&cif->cl_stats.cl_xmitcnt, m_pktlen(m));
		return (m);
	}

	return (NULL);
}

struct codel *
codel_alloc(int target, int interval, int ecn)
{
	struct codel *c;

	c = malloc(sizeof(*c), M_DEVBUF, M_NOWAIT | M_ZERO);
	if (c != NULL) {
		c->params.target = machclk_freq * target / 1000;
		c->params.interval = machclk_freq * interval / 1000;
		c->params.ecn = ecn;
		c->stats.maxpacket = 256;
	}

	return (c);
}

void
codel_destroy(struct codel *c)
{

	free(c, M_DEVBUF);
}

#define	MTAG_CODEL	1438031249
int
codel_addq(struct codel *c, class_queue_t *q, struct mbuf *m)
{
	struct m_tag *mtag;
	uint64_t *enqueue_time;

	if (qlen(q) < qlimit(q)) {
		mtag = m_tag_locate(m, MTAG_CODEL, 0, NULL);
		if (mtag == NULL)
			mtag = m_tag_alloc(MTAG_CODEL, 0, sizeof(uint64_t),
			    M_NOWAIT);
		if (mtag == NULL) {
			m_freem(m);
			return (-1);
		}
		enqueue_time = (uint64_t *)(mtag + 1);
		*enqueue_time = read_machclk();
		m_tag_prepend(m, mtag);
		_addq(q, m);
		return (0);
	}
	c->drop_overlimit++;
	m_freem(m);

	return (-1);
}

static int
codel_should_drop(struct codel *c, class_queue_t *q, struct mbuf *m,
    u_int64_t now)
{
	struct m_tag *mtag;
	uint64_t *enqueue_time;

	if (m == NULL) {
		c->vars.first_above_time = 0;
		return (0);
	}

	mtag = m_tag_locate(m, MTAG_CODEL, 0, NULL);
	if (mtag == NULL) {
		/* Only one warning per second. */
		if (ppsratecheck(&c->last_log, &c->last_pps, 1))
			printf("%s: could not found the packet mtag!\n",
			    __func__);
		c->vars.first_above_time = 0;
		return (0);
	}
	enqueue_time = (uint64_t *)(mtag + 1);
	c->vars.ldelay = now - *enqueue_time;
	c->stats.maxpacket = MAX(c->stats.maxpacket, m_pktlen(m));

	if (codel_time_before(c->vars.ldelay, c->params.target) ||
	    qsize(q) <= c->stats.maxpacket) {
		/* went below - stay below for at least interval */
		c->vars.first_above_time = 0;
		return (0);
	}
	if (c->vars.first_above_time == 0) {
		/* just went above from below. If we stay above
		 * for at least interval we'll say it's ok to drop
		 */
		c->vars.first_above_time = now + c->params.interval;
		return (0);
	}
	if (codel_time_after(now, c->vars.first_above_time))
		return (1);

	return (0);
}

/*
 * Run a Newton method step:
 * new_invsqrt = (invsqrt / 2) * (3 - count * invsqrt^2)
 *
 * Here, invsqrt is a fixed point number (< 1.0), 32bit mantissa, aka Q0.32
 */
static void
codel_Newton_step(struct codel_vars *vars)
{
	uint32_t invsqrt, invsqrt2;
	uint64_t val;

/* sizeof_in_bits(rec_inv_sqrt) */
#define	REC_INV_SQRT_BITS (8 * sizeof(u_int16_t))
/* needed shift to get a Q0.32 number from rec_inv_sqrt */
#define	REC_INV_SQRT_SHIFT (32 - REC_INV_SQRT_BITS)

	invsqrt = ((u_int32_t)vars->rec_inv_sqrt) << REC_INV_SQRT_SHIFT;
	invsqrt2 = ((u_int64_t)invsqrt * invsqrt) >> 32;
	val = (3LL << 32) - ((u_int64_t)vars->count * invsqrt2);
	val >>= 2; /* avoid overflow in following multiply */
	val = (val * invsqrt) >> (32 - 2 + 1);

	vars->rec_inv_sqrt = val >> REC_INV_SQRT_SHIFT;
}

static u_int64_t
codel_control_law(u_int64_t t, u_int64_t interval, u_int32_t rec_inv_sqrt)
{

	return (t + (u_int32_t)(((u_int64_t)interval *
	    (rec_inv_sqrt << REC_INV_SQRT_SHIFT)) >> 32));
}

struct mbuf *
codel_getq(struct codel *c, class_queue_t *q)
{
	struct mbuf	*m;
	u_int64_t	 now;
	int		 drop;

	if ((m = _getq(q)) == NULL) {
		c->vars.dropping = 0;
		return (m);
	}

	now = read_machclk();
	drop = codel_should_drop(c, q, m, now);
	if (c->vars.dropping) {
		if (!drop) {
			/* sojourn time below target - leave dropping state */
			c->vars.dropping = 0;
		} else if (codel_time_after_eq(now, c->vars.drop_next)) {
			/* It's time for the next drop. Drop the current
			 * packet and dequeue the next. The dequeue might
			 * take us out of dropping state.
			 * If not, schedule the next drop.
			 * A large backlog might result in drop rates so high
			 * that the next drop should happen now,
			 * hence the while loop.
			 */
			while (c->vars.dropping &&
			    codel_time_after_eq(now, c->vars.drop_next)) {
				c->vars.count++; /* don't care of possible wrap
						  * since there is no more
						  * divide */
				codel_Newton_step(&c->vars);
				/* TODO ECN */
				PKTCNTR_ADD(&c->stats.drop_cnt, m_pktlen(m));
				m_freem(m);
				m = _getq(q);
				if (!codel_should_drop(c, q, m, now))
					/* leave dropping state */
					c->vars.dropping = 0;
				else
					/* and schedule the next drop */
					c->vars.drop_next =
					    codel_control_law(c->vars.drop_next,
						c->params.interval,
						c->vars.rec_inv_sqrt);
			}
		}
	} else if (drop) {
		/* TODO ECN */
		PKTCNTR_ADD(&c->stats.drop_cnt, m_pktlen(m));
		m_freem(m);

		m = _getq(q);
		drop = codel_should_drop(c, q, m, now);

		c->vars.dropping = 1;
		/* if min went above target close to when we last went below it
		 * assume that the drop rate that controlled the queue on the
		 * last cycle is a good starting point to control it now.
		 */
		if (codel_time_before(now - c->vars.drop_next,
		    16 * c->params.interval)) {
			c->vars.count = (c->vars.count - c->vars.lastcount) | 1;
			/* we dont care if rec_inv_sqrt approximation
			 * is not very precise :
			 * Next Newton steps will correct it quadratically.
			 */
			codel_Newton_step(&c->vars);
		} else {
			c->vars.count = 1;
			c->vars.rec_inv_sqrt = ~0U >> REC_INV_SQRT_SHIFT;
		}
		c->vars.lastcount = c->vars.count;
		c->vars.drop_next = codel_control_law(now, c->params.interval,
		    c->vars.rec_inv_sqrt);
	}

	return (m);
}

void
codel_getstats(struct codel *c, struct codel_stats *s)
{
	*s = c->stats;
}

#endif /* ALTQ_CODEL */
