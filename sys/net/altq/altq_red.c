/*-
 * Copyright (C) 1997-2003
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
 *
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
 * $KAME: altq_red.c,v 1.18 2003/09/05 22:40:36 itojun Exp $
 * $FreeBSD$	
 */

#include "opt_altq.h"
#include "opt_inet.h"
#include "opt_inet6.h"
#ifdef ALTQ_RED	/* red is enabled by ALTQ_RED option in opt_altq.h */

#include <sys/param.h>
#include <sys/malloc.h>
#include <sys/mbuf.h>
#include <sys/socket.h>
#include <sys/systm.h>
#include <sys/errno.h>
#if 1 /* ALTQ3_COMPAT */
#include <sys/sockio.h>
#include <sys/proc.h>
#include <sys/kernel.h>
#ifdef ALTQ_FLOWVALVE
#include <sys/queue.h>
#include <sys/time.h>
#endif
#endif /* ALTQ3_COMPAT */

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
#include <netpfil/pf/pf_mtag.h>
#include <net/altq/altq.h>
#include <net/altq/altq_red.h>

/*
 * ALTQ/RED (Random Early Detection) implementation using 32-bit
 * fixed-point calculation.
 *
 * written by kjc using the ns code as a reference.
 * you can learn more about red and ns from Sally's home page at
 * http://www-nrg.ee.lbl.gov/floyd/
 *
 * most of the red parameter values are fixed in this implementation
 * to prevent fixed-point overflow/underflow.
 * if you change the parameters, watch out for overflow/underflow!
 *
 * the parameters used are recommended values by Sally.
 * the corresponding ns config looks:
 *	q_weight=0.00195
 *	minthresh=5 maxthresh=15 queue-size=60
 *	linterm=30
 *	dropmech=drop-tail
 *	bytes=false (can't be handled by 32-bit fixed-point)
 *	doubleq=false dqthresh=false
 *	wait=true
 */
/*
 * alternative red parameters for a slow link.
 *
 * assume the queue length becomes from zero to L and keeps L, it takes
 * N packets for q_avg to reach 63% of L.
 * when q_weight is 0.002, N is about 500 packets.
 * for a slow link like dial-up, 500 packets takes more than 1 minute!
 * when q_weight is 0.008, N is about 127 packets.
 * when q_weight is 0.016, N is about 63 packets.
 * bursts of 50 packets are allowed for 0.002, bursts of 25 packets
 * are allowed for 0.016.
 * see Sally's paper for more details.
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
#define	TH_MIN		5	/* min threshold */
#define	TH_MAX		15	/* max threshold */

#define	RED_LIMIT	60	/* default max queue length */
#define	RED_STATS		/* collect statistics */

/*
 * our default policy for forced-drop is drop-tail.
 * (in altq-1.1.2 or earlier, the default was random-drop.
 * but it makes more sense to punish the cause of the surge.)
 * to switch to the random-drop policy, define "RED_RANDOM_DROP".
 */


/* default red parameter values */
static int default_th_min = TH_MIN;
static int default_th_max = TH_MAX;
static int default_inv_pmax = INV_P_MAX;


/*
 * red support routines
 */
red_t *
red_alloc(int weight, int inv_pmax, int th_min, int th_max, int flags,
   int pkttime)
{
	red_t	*rp;
	int	 w, i;
	int	 npkts_per_sec;

	rp = malloc(sizeof(red_t), M_DEVBUF, M_NOWAIT | M_ZERO);
	if (rp == NULL)
		return (NULL);

	if (weight == 0)
		rp->red_weight = W_WEIGHT;
	else
		rp->red_weight = weight;

	/* allocate weight table */
	rp->red_wtab = wtab_alloc(rp->red_weight);
	if (rp->red_wtab == NULL) {
		free(rp, M_DEVBUF);
		return (NULL);
	}

	rp->red_avg = 0;
	rp->red_idle = 1;

	if (inv_pmax == 0)
		rp->red_inv_pmax = default_inv_pmax;
	else
		rp->red_inv_pmax = inv_pmax;
	if (th_min == 0)
		rp->red_thmin = default_th_min;
	else
		rp->red_thmin = th_min;
	if (th_max == 0)
		rp->red_thmax = default_th_max;
	else
		rp->red_thmax = th_max;

	rp->red_flags = flags;

	if (pkttime == 0)
		/* default packet time: 1000 bytes / 10Mbps * 8 * 1000000 */
		rp->red_pkttime = 800;
	else
		rp->red_pkttime = pkttime;

	if (weight == 0) {
		/* when the link is very slow, adjust red parameters */
		npkts_per_sec = 1000000 / rp->red_pkttime;
		if (npkts_per_sec < 50) {
			/* up to about 400Kbps */
			rp->red_weight = W_WEIGHT_2;
		} else if (npkts_per_sec < 300) {
			/* up to about 2.4Mbps */
			rp->red_weight = W_WEIGHT_1;
		}
	}

	/* calculate wshift.  weight must be power of 2 */
	w = rp->red_weight;
	for (i = 0; w > 1; i++)
		w = w >> 1;
	rp->red_wshift = i;
	w = 1 << rp->red_wshift;
	if (w != rp->red_weight) {
		printf("invalid weight value %d for red! use %d\n",
		       rp->red_weight, w);
		rp->red_weight = w;
	}

	/*
	 * thmin_s and thmax_s are scaled versions of th_min and th_max
	 * to be compared with avg.
	 */
	rp->red_thmin_s = rp->red_thmin << (rp->red_wshift + FP_SHIFT);
	rp->red_thmax_s = rp->red_thmax << (rp->red_wshift + FP_SHIFT);

	/*
	 * precompute probability denominator
	 *  probd = (2 * (TH_MAX-TH_MIN) / pmax) in fixed-point
	 */
	rp->red_probd = (2 * (rp->red_thmax - rp->red_thmin)
			 * rp->red_inv_pmax) << FP_SHIFT;

	microtime(&rp->red_last);
	return (rp);
}

void
red_destroy(red_t *rp)
{
	wtab_destroy(rp->red_wtab);
	free(rp, M_DEVBUF);
}

void
red_getstats(red_t *rp, struct redstats *sp)
{
	sp->q_avg		= rp->red_avg >> rp->red_wshift;
	sp->xmit_cnt		= rp->red_stats.xmit_cnt;
	sp->drop_cnt		= rp->red_stats.drop_cnt;
	sp->drop_forced		= rp->red_stats.drop_forced;
	sp->drop_unforced	= rp->red_stats.drop_unforced;
	sp->marked_packets	= rp->red_stats.marked_packets;
}

int
red_addq(red_t *rp, class_queue_t *q, struct mbuf *m,
    struct altq_pktattr *pktattr)
{
	int avg, droptype;
	int n;

	avg = rp->red_avg;

	/*
	 * if we were idle, we pretend that n packets arrived during
	 * the idle period.
	 */
	if (rp->red_idle) {
		struct timeval now;
		int t;

		rp->red_idle = 0;
		microtime(&now);
		t = (now.tv_sec - rp->red_last.tv_sec);
		if (t > 60) {
			/*
			 * being idle for more than 1 minute, set avg to zero.
			 * this prevents t from overflow.
			 */
			avg = 0;
		} else {
			t = t * 1000000 + (now.tv_usec - rp->red_last.tv_usec);
			n = t / rp->red_pkttime - 1;

			/* the following line does (avg = (1 - Wq)^n * avg) */
			if (n > 0)
				avg = (avg >> FP_SHIFT) *
				    pow_w(rp->red_wtab, n);
		}
	}

	/* run estimator. (note: avg is scaled by WEIGHT in fixed-point) */
	avg += (qlen(q) << FP_SHIFT) - (avg >> rp->red_wshift);
	rp->red_avg = avg;		/* save the new value */

	/*
	 * red_count keeps a tally of arriving traffic that has not
	 * been dropped.
	 */
	rp->red_count++;

	/* see if we drop early */
	droptype = DTYPE_NODROP;
	if (avg >= rp->red_thmin_s && qlen(q) > 1) {
		if (avg >= rp->red_thmax_s) {
			/* avg >= th_max: forced drop */
			droptype = DTYPE_FORCED;
		} else if (rp->red_old == 0) {
			/* first exceeds th_min */
			rp->red_count = 1;
			rp->red_old = 1;
		} else if (drop_early((avg - rp->red_thmin_s) >> rp->red_wshift,
				      rp->red_probd, rp->red_count)) {
			/* mark or drop by red */
			if ((rp->red_flags & REDF_ECN) &&
			    mark_ecn(m, pktattr, rp->red_flags)) {
				/* successfully marked.  do not drop. */
				rp->red_count = 0;
#ifdef RED_STATS
				rp->red_stats.marked_packets++;
#endif
			} else {
				/* unforced drop by red */
				droptype = DTYPE_EARLY;
			}
		}
	} else {
		/* avg < th_min */
		rp->red_old = 0;
	}

	/*
	 * if the queue length hits the hard limit, it's a forced drop.
	 */
	if (droptype == DTYPE_NODROP && qlen(q) >= qlimit(q))
		droptype = DTYPE_FORCED;

#ifdef RED_RANDOM_DROP
	/* if successful or forced drop, enqueue this packet. */
	if (droptype != DTYPE_EARLY)
		_addq(q, m);
#else
	/* if successful, enqueue this packet. */
	if (droptype == DTYPE_NODROP)
		_addq(q, m);
#endif
	if (droptype != DTYPE_NODROP) {
		if (droptype == DTYPE_EARLY) {
			/* drop the incoming packet */
#ifdef RED_STATS
			rp->red_stats.drop_unforced++;
#endif
		} else {
			/* forced drop, select a victim packet in the queue. */
#ifdef RED_RANDOM_DROP
			m = _getq_random(q);
#endif
#ifdef RED_STATS
			rp->red_stats.drop_forced++;
#endif
		}
#ifdef RED_STATS
		PKTCNTR_ADD(&rp->red_stats.drop_cnt, m_pktlen(m));
#endif
		rp->red_count = 0;
		m_freem(m);
		return (-1);
	}
	/* successfully queued */
#ifdef RED_STATS
	PKTCNTR_ADD(&rp->red_stats.xmit_cnt, m_pktlen(m));
#endif
	return (0);
}

/*
 * early-drop probability is calculated as follows:
 *   prob = p_max * (avg - th_min) / (th_max - th_min)
 *   prob_a = prob / (2 - count*prob)
 *	    = (avg-th_min) / (2*(th_max-th_min)*inv_p_max - count*(avg-th_min))
 * here prob_a increases as successive undrop count increases.
 * (prob_a starts from prob/2, becomes prob when (count == (1 / prob)),
 * becomes 1 when (count >= (2 / prob))).
 */
int
drop_early(int fp_len, int fp_probd, int count)
{
	int	d;		/* denominator of drop-probability */

	d = fp_probd - count * fp_len;
	if (d <= 0)
		/* count exceeds the hard limit: drop or mark */
		return (1);

	/*
	 * now the range of d is [1..600] in fixed-point. (when
	 * th_max-th_min=10 and p_max=1/30)
	 * drop probability = (avg - TH_MIN) / d
	 */

	if ((arc4random() % d) < fp_len) {
		/* drop or mark */
		return (1);
	}
	/* no drop/mark */
	return (0);
}

/*
 * try to mark CE bit to the packet.
 *    returns 1 if successfully marked, 0 otherwise.
 */
int
mark_ecn(struct mbuf *m, struct altq_pktattr *pktattr, int flags)
{
	struct mbuf	*m0;
	struct pf_mtag	*at;
	void		*hdr;

	at = pf_find_mtag(m);
	if (at != NULL) {
		hdr = at->hdr;
	} else
		return (0);

	/* verify that pattr_hdr is within the mbuf data */
	for (m0 = m; m0 != NULL; m0 = m0->m_next)
		if (((caddr_t)hdr >= m0->m_data) &&
		    ((caddr_t)hdr < m0->m_data + m0->m_len))
			break;
	if (m0 == NULL) {
		/* ick, tag info is stale */
		return (0);
	}

	switch (((struct ip *)hdr)->ip_v) {
	case IPVERSION:
		if (flags & REDF_ECN4) {
			struct ip *ip = hdr;
			u_int8_t otos;
			int sum;

			if (ip->ip_v != 4)
				return (0);	/* version mismatch! */

			if ((ip->ip_tos & IPTOS_ECN_MASK) == IPTOS_ECN_NOTECT)
				return (0);	/* not-ECT */
			if ((ip->ip_tos & IPTOS_ECN_MASK) == IPTOS_ECN_CE)
				return (1);	/* already marked */

			/*
			 * ecn-capable but not marked,
			 * mark CE and update checksum
			 */
			otos = ip->ip_tos;
			ip->ip_tos |= IPTOS_ECN_CE;
			/*
			 * update checksum (from RFC1624)
			 *	   HC' = ~(~HC + ~m + m')
			 */
			sum = ~ntohs(ip->ip_sum) & 0xffff;
			sum += (~otos & 0xffff) + ip->ip_tos;
			sum = (sum >> 16) + (sum & 0xffff);
			sum += (sum >> 16);  /* add carry */
			ip->ip_sum = htons(~sum & 0xffff);
			return (1);
		}
		break;
#ifdef INET6
	case (IPV6_VERSION >> 4):
		if (flags & REDF_ECN6) {
			struct ip6_hdr *ip6 = hdr;
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
			 * ecn-capable but not marked,  mark CE
			 */
			flowlabel |= (IPTOS_ECN_CE << 20);
			ip6->ip6_flow = htonl(flowlabel);
			return (1);
		}
		break;
#endif  /* INET6 */
	}

	/* not marked */
	return (0);
}

struct mbuf *
red_getq(rp, q)
	red_t *rp;
	class_queue_t *q;
{
	struct mbuf *m;

	if ((m = _getq(q)) == NULL) {
		if (rp->red_idle == 0) {
			rp->red_idle = 1;
			microtime(&rp->red_last);
		}
		return NULL;
	}

	rp->red_idle = 0;
	return (m);
}

/*
 * helper routine to calibrate avg during idle.
 * pow_w(wtab, n) returns (1 - Wq)^n in fixed-point
 * here Wq = 1/weight and the code assumes Wq is close to zero.
 *
 * w_tab[n] holds ((1 - Wq)^(2^n)) in fixed-point.
 */
static struct wtab *wtab_list = NULL;	/* pointer to wtab list */

struct wtab *
wtab_alloc(int weight)
{
	struct wtab	*w;
	int		 i;

	for (w = wtab_list; w != NULL; w = w->w_next)
		if (w->w_weight == weight) {
			w->w_refcount++;
			return (w);
		}

	w = malloc(sizeof(struct wtab), M_DEVBUF, M_NOWAIT | M_ZERO);
	if (w == NULL)
		return (NULL);
	w->w_weight = weight;
	w->w_refcount = 1;
	w->w_next = wtab_list;
	wtab_list = w;

	/* initialize the weight table */
	w->w_tab[0] = ((weight - 1) << FP_SHIFT) / weight;
	for (i = 1; i < 32; i++) {
		w->w_tab[i] = (w->w_tab[i-1] * w->w_tab[i-1]) >> FP_SHIFT;
		if (w->w_tab[i] == 0 && w->w_param_max == 0)
			w->w_param_max = 1 << i;
	}

	return (w);
}

int
wtab_destroy(struct wtab *w)
{
	struct wtab	*prev;

	if (--w->w_refcount > 0)
		return (0);

	if (wtab_list == w)
		wtab_list = w->w_next;
	else for (prev = wtab_list; prev->w_next != NULL; prev = prev->w_next)
		if (prev->w_next == w) {
			prev->w_next = w->w_next;
			break;
		}

	free(w, M_DEVBUF);
	return (0);
}

int32_t
pow_w(struct wtab *w, int n)
{
	int	i, bit;
	int32_t	val;

	if (n >= w->w_param_max)
		return (0);

	val = 1 << FP_SHIFT;
	if (n <= 0)
		return (val);

	bit = 1;
	i = 0;
	while (n) {
		if (n & bit) {
			val = (val * w->w_tab[i]) >> FP_SHIFT;
			n &= ~bit;
		}
		i++;
		bit <<=  1;
	}
	return (val);
}


#endif /* ALTQ_RED */
