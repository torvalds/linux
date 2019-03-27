/*-
 * Copyright (C) 1998-2003
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
/*-
 * Copyright (c) 1990-1994 Regents of the University of California.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *	This product includes software developed by the Computer Systems
 *	Engineering Group at Lawrence Berkeley Laboratory.
 * 4. Neither the name of the University nor of the Laboratory may be used
 *    to endorse or promote products derived from this software without
 *    specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $KAME: altq_rio.c,v 1.17 2003/07/10 12:07:49 kjc Exp $
 * $FreeBSD$
 */

#include "opt_altq.h"
#include "opt_inet.h"
#include "opt_inet6.h"
#ifdef ALTQ_RIO	/* rio is enabled by ALTQ_RIO option in opt_altq.h */

#include <sys/param.h>
#include <sys/malloc.h>
#include <sys/mbuf.h>
#include <sys/socket.h>
#include <sys/systm.h>
#include <sys/errno.h>
#if 1 /* ALTQ3_COMPAT */
#include <sys/proc.h>
#include <sys/sockio.h>
#include <sys/kernel.h>
#endif

#include <net/if.h>
#include <net/if_var.h>

#include <netinet/in.h>
#include <netinet/in_systm.h>
#include <netinet/ip.h>
#ifdef INET6
#include <netinet/ip6.h>
#endif

#include <netpfil/pf/pf.h>
#include <netpfil/pf/pf_altq.h>
#include <net/altq/altq.h>
#include <net/altq/altq_cdnr.h>
#include <net/altq/altq_red.h>
#include <net/altq/altq_rio.h>

/*
 * RIO: RED with IN/OUT bit
 *   described in
 *	"Explicit Allocation of Best Effort Packet Delivery Service"
 *	David D. Clark and Wenjia Fang, MIT Lab for Computer Science
 *	http://diffserv.lcs.mit.edu/Papers/exp-alloc-ddc-wf.{ps,pdf}
 *
 * this implementation is extended to support more than 2 drop precedence
 * values as described in RFC2597 (Assured Forwarding PHB Group).
 *
 */
/*
 * AF DS (differentiated service) codepoints.
 * (classes can be mapped to CBQ or H-FSC classes.)
 *
 *      0   1   2   3   4   5   6   7
 *    +---+---+---+---+---+---+---+---+
 *    |   CLASS   |DropPre| 0 |  CU   |
 *    +---+---+---+---+---+---+---+---+
 *
 *    class 1: 001
 *    class 2: 010
 *    class 3: 011
 *    class 4: 100
 *
 *    low drop prec:    01
 *    medium drop prec: 10
 *    high drop prec:   01
 */

/* normal red parameters */
#define	W_WEIGHT	512	/* inverse of weight of EWMA (511/512) */
				/* q_weight = 0.00195 */

/* red parameters for a slow link */
#define	W_WEIGHT_1	128	/* inverse of weight of EWMA (127/128) */
				/* q_weight = 0.0078125 */

/* red parameters for a very slow link (e.g., dialup) */
#define	W_WEIGHT_2	64	/* inverse of weight of EWMA (63/64) */
				/* q_weight = 0.015625 */

/* fixed-point uses 12-bit decimal places */
#define	FP_SHIFT	12	/* fixed-point shift */

/* red parameters for drop probability */
#define	INV_P_MAX	10	/* inverse of max drop probability */
#define	TH_MIN		 5	/* min threshold */
#define	TH_MAX		15	/* max threshold */

#define	RIO_LIMIT	60	/* default max queue length */
#define	RIO_STATS		/* collect statistics */

#define	TV_DELTA(a, b, delta) {					\
	int	xxs;						\
								\
	delta = (a)->tv_usec - (b)->tv_usec; 			\
	if ((xxs = (a)->tv_sec - (b)->tv_sec) != 0) { 		\
		if (xxs < 0) { 					\
			delta = 60000000;			\
		} else if (xxs > 4)  {				\
			if (xxs > 60)				\
				delta = 60000000;		\
			else					\
				delta += xxs * 1000000;		\
		} else while (xxs > 0) {			\
			delta += 1000000;			\
			xxs--;					\
		}						\
	}							\
}

/* default rio parameter values */
static struct redparams default_rio_params[RIO_NDROPPREC] = {
  /* th_min,		 th_max,     inv_pmax */
  { TH_MAX * 2 + TH_MIN, TH_MAX * 3, INV_P_MAX }, /* low drop precedence */
  { TH_MAX + TH_MIN,	 TH_MAX * 2, INV_P_MAX }, /* medium drop precedence */
  { TH_MIN,		 TH_MAX,     INV_P_MAX }  /* high drop precedence */
};

/* internal function prototypes */
static int dscp2index(u_int8_t);

rio_t *
rio_alloc(int weight, struct redparams *params, int flags, int pkttime)
{
	rio_t	*rp;
	int	 w, i;
	int	 npkts_per_sec;

	rp = malloc(sizeof(rio_t), M_DEVBUF, M_NOWAIT | M_ZERO);
	if (rp == NULL)
		return (NULL);

	rp->rio_flags = flags;
	if (pkttime == 0)
		/* default packet time: 1000 bytes / 10Mbps * 8 * 1000000 */
		rp->rio_pkttime = 800;
	else
		rp->rio_pkttime = pkttime;

	if (weight != 0)
		rp->rio_weight = weight;
	else {
		/* use default */
		rp->rio_weight = W_WEIGHT;

		/* when the link is very slow, adjust red parameters */
		npkts_per_sec = 1000000 / rp->rio_pkttime;
		if (npkts_per_sec < 50) {
			/* up to about 400Kbps */
			rp->rio_weight = W_WEIGHT_2;
		} else if (npkts_per_sec < 300) {
			/* up to about 2.4Mbps */
			rp->rio_weight = W_WEIGHT_1;
		}
	}

	/* calculate wshift.  weight must be power of 2 */
	w = rp->rio_weight;
	for (i = 0; w > 1; i++)
		w = w >> 1;
	rp->rio_wshift = i;
	w = 1 << rp->rio_wshift;
	if (w != rp->rio_weight) {
		printf("invalid weight value %d for red! use %d\n",
		       rp->rio_weight, w);
		rp->rio_weight = w;
	}

	/* allocate weight table */
	rp->rio_wtab = wtab_alloc(rp->rio_weight);

	for (i = 0; i < RIO_NDROPPREC; i++) {
		struct dropprec_state *prec = &rp->rio_precstate[i];

		prec->avg = 0;
		prec->idle = 1;

		if (params == NULL || params[i].inv_pmax == 0)
			prec->inv_pmax = default_rio_params[i].inv_pmax;
		else
			prec->inv_pmax = params[i].inv_pmax;
		if (params == NULL || params[i].th_min == 0)
			prec->th_min = default_rio_params[i].th_min;
		else
			prec->th_min = params[i].th_min;
		if (params == NULL || params[i].th_max == 0)
			prec->th_max = default_rio_params[i].th_max;
		else
			prec->th_max = params[i].th_max;

		/*
		 * th_min_s and th_max_s are scaled versions of th_min
		 * and th_max to be compared with avg.
		 */
		prec->th_min_s = prec->th_min << (rp->rio_wshift + FP_SHIFT);
		prec->th_max_s = prec->th_max << (rp->rio_wshift + FP_SHIFT);

		/*
		 * precompute probability denominator
		 *  probd = (2 * (TH_MAX-TH_MIN) / pmax) in fixed-point
		 */
		prec->probd = (2 * (prec->th_max - prec->th_min)
			       * prec->inv_pmax) << FP_SHIFT;

		microtime(&prec->last);
	}

	return (rp);
}

void
rio_destroy(rio_t *rp)
{
	wtab_destroy(rp->rio_wtab);
	free(rp, M_DEVBUF);
}

void
rio_getstats(rio_t *rp, struct redstats *sp)
{
	int	i;

	for (i = 0; i < RIO_NDROPPREC; i++) {
		bcopy(&rp->q_stats[i], sp, sizeof(struct redstats));
		sp->q_avg = rp->rio_precstate[i].avg >> rp->rio_wshift;
		sp++;
	}
}

#if (RIO_NDROPPREC == 3)
/*
 * internally, a drop precedence value is converted to an index
 * starting from 0.
 */
static int
dscp2index(u_int8_t dscp)
{
	int	dpindex = dscp & AF_DROPPRECMASK;

	if (dpindex == 0)
		return (0);
	return ((dpindex >> 3) - 1);
}
#endif

#if 1
/*
 * kludge: when a packet is dequeued, we need to know its drop precedence
 * in order to keep the queue length of each drop precedence.
 * use m_pkthdr.rcvif to pass this info.
 */
#define	RIOM_SET_PRECINDEX(m, idx)	\
	do { (m)->m_pkthdr.rcvif = (void *)((long)(idx)); } while (0)
#define	RIOM_GET_PRECINDEX(m)	\
	({ long idx; idx = (long)((m)->m_pkthdr.rcvif); \
	(m)->m_pkthdr.rcvif = NULL; idx; })
#endif

int
rio_addq(rio_t *rp, class_queue_t *q, struct mbuf *m,
    struct altq_pktattr *pktattr)
{
	int			 avg, droptype;
	u_int8_t		 dsfield, odsfield;
	int			 dpindex, i, n, t;
	struct timeval		 now;
	struct dropprec_state	*prec;

	dsfield = odsfield = read_dsfield(m, pktattr);
	dpindex = dscp2index(dsfield);

	/*
	 * update avg of the precedence states whose drop precedence
	 * is larger than or equal to the drop precedence of the packet
	 */
	now.tv_sec = 0;
	for (i = dpindex; i < RIO_NDROPPREC; i++) {
		prec = &rp->rio_precstate[i];
		avg = prec->avg;
		if (prec->idle) {
			prec->idle = 0;
			if (now.tv_sec == 0)
				microtime(&now);
			t = (now.tv_sec - prec->last.tv_sec);
			if (t > 60)
				avg = 0;
			else {
				t = t * 1000000 +
					(now.tv_usec - prec->last.tv_usec);
				n = t / rp->rio_pkttime;
				/* calculate (avg = (1 - Wq)^n * avg) */
				if (n > 0)
					avg = (avg >> FP_SHIFT) *
						pow_w(rp->rio_wtab, n);
			}
		}

		/* run estimator. (avg is scaled by WEIGHT in fixed-point) */
		avg += (prec->qlen << FP_SHIFT) - (avg >> rp->rio_wshift);
		prec->avg = avg;		/* save the new value */
		/*
		 * count keeps a tally of arriving traffic that has not
		 * been dropped.
		 */
		prec->count++;
	}

	prec = &rp->rio_precstate[dpindex];
	avg = prec->avg;

	/* see if we drop early */
	droptype = DTYPE_NODROP;
	if (avg >= prec->th_min_s && prec->qlen > 1) {
		if (avg >= prec->th_max_s) {
			/* avg >= th_max: forced drop */
			droptype = DTYPE_FORCED;
		} else if (prec->old == 0) {
			/* first exceeds th_min */
			prec->count = 1;
			prec->old = 1;
		} else if (drop_early((avg - prec->th_min_s) >> rp->rio_wshift,
				      prec->probd, prec->count)) {
			/* unforced drop by red */
			droptype = DTYPE_EARLY;
		}
	} else {
		/* avg < th_min */
		prec->old = 0;
	}

	/*
	 * if the queue length hits the hard limit, it's a forced drop.
	 */
	if (droptype == DTYPE_NODROP && qlen(q) >= qlimit(q))
		droptype = DTYPE_FORCED;

	if (droptype != DTYPE_NODROP) {
		/* always drop incoming packet (as opposed to randomdrop) */
		for (i = dpindex; i < RIO_NDROPPREC; i++)
			rp->rio_precstate[i].count = 0;
#ifdef RIO_STATS
		if (droptype == DTYPE_EARLY)
			rp->q_stats[dpindex].drop_unforced++;
		else
			rp->q_stats[dpindex].drop_forced++;
		PKTCNTR_ADD(&rp->q_stats[dpindex].drop_cnt, m_pktlen(m));
#endif
		m_freem(m);
		return (-1);
	}

	for (i = dpindex; i < RIO_NDROPPREC; i++)
		rp->rio_precstate[i].qlen++;

	/* save drop precedence index in mbuf hdr */
	RIOM_SET_PRECINDEX(m, dpindex);

	if (rp->rio_flags & RIOF_CLEARDSCP)
		dsfield &= ~DSCP_MASK;

	if (dsfield != odsfield)
		write_dsfield(m, pktattr, dsfield);

	_addq(q, m);

#ifdef RIO_STATS
	PKTCNTR_ADD(&rp->q_stats[dpindex].xmit_cnt, m_pktlen(m));
#endif
	return (0);
}

struct mbuf *
rio_getq(rio_t *rp, class_queue_t *q)
{
	struct mbuf	*m;
	int		 dpindex, i;

	if ((m = _getq(q)) == NULL)
		return NULL;

	dpindex = RIOM_GET_PRECINDEX(m);
	for (i = dpindex; i < RIO_NDROPPREC; i++) {
		if (--rp->rio_precstate[i].qlen == 0) {
			if (rp->rio_precstate[i].idle == 0) {
				rp->rio_precstate[i].idle = 1;
				microtime(&rp->rio_precstate[i].last);
			}
		}
	}
	return (m);
}


#endif /* ALTQ_RIO */
