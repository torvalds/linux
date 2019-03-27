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
 * $KAME: altq_red.h,v 1.8 2003/07/10 12:07:49 kjc Exp $
 * $FreeBSD$
 */

#ifndef _ALTQ_ALTQ_RED_H_
#define	_ALTQ_ALTQ_RED_H_

#include <net/altq/altq_classq.h>


/* red flags */
#define	REDF_ECN4	0x01	/* use packet marking for IPv4 packets */
#define	REDF_ECN6	0x02	/* use packet marking for IPv6 packets */
#define	REDF_ECN	(REDF_ECN4 | REDF_ECN6)
#define	REDF_FLOWVALVE	0x04	/* use flowvalve (aka penalty-box) */

/*
 * simpler versions of red parameters and statistics used by other
 * disciplines (e.g., CBQ)
 */
struct redparams {
	int th_min;		/* red min threshold */
	int th_max;		/* red max threshold */
	int inv_pmax;		/* inverse of max drop probability */
};

struct redstats {
	int		q_avg;
	struct pktcntr	xmit_cnt;
	struct pktcntr	drop_cnt;
	u_int		drop_forced;
	u_int		drop_unforced;
	u_int		marked_packets;
};


#ifdef _KERNEL


/* weight table structure for idle time calibration */
struct wtab {
	struct wtab	*w_next;
	int		 w_weight;
	int		 w_param_max;
	int		 w_refcount;
	int32_t		 w_tab[32];
};

typedef struct red {
	int		red_pkttime;	/* average packet time in micro sec
					   used for idle calibration */
	int		red_flags;	/* red flags */

	/* red parameters */
	int		red_weight;	/* weight for EWMA */
	int		red_inv_pmax;	/* inverse of max drop probability */
	int		red_thmin;	/* red min threshold */
	int		red_thmax;	/* red max threshold */

	/* variables for internal use */
	int		red_wshift;	/* log(red_weight) */
	int		red_thmin_s;	/* th_min scaled by avgshift */
	int		red_thmax_s;	/* th_max scaled by avgshift */
	int		red_probd;	/* drop probability denominator */

	int		red_avg;	/* queue len avg scaled by avgshift */
	int		red_count;	/* packet count since last dropped/
					   marked packet */
	int		red_idle;	/* queue was empty */
	int		red_old;	/* avg is above th_min */
	struct wtab	*red_wtab;	/* weight table */
	struct timeval	 red_last;	/* time when the queue becomes idle */


	struct {
		struct pktcntr	xmit_cnt;
		struct pktcntr	drop_cnt;
		u_int		drop_forced;
		u_int		drop_unforced;
		u_int		marked_packets;
	} red_stats;
} red_t;


/* red drop types */
#define	DTYPE_NODROP	0	/* no drop */
#define	DTYPE_FORCED	1	/* a "forced" drop */
#define	DTYPE_EARLY	2	/* an "unforced" (early) drop */

extern red_t		*red_alloc(int, int, int, int, int, int);
extern void		 red_destroy(red_t *);
extern void		 red_getstats(red_t *, struct redstats *);
extern int		 red_addq(red_t *, class_queue_t *, struct mbuf *,
			     struct altq_pktattr *);
extern struct mbuf	*red_getq(red_t *, class_queue_t *);
extern int		 drop_early(int, int, int);
extern int		 mark_ecn(struct mbuf *, struct altq_pktattr *, int);
extern struct wtab	*wtab_alloc(int);
extern int		 wtab_destroy(struct wtab *);
extern int32_t		 pow_w(struct wtab *, int);

#endif /* _KERNEL */

#endif /* _ALTQ_ALTQ_RED_H_ */
