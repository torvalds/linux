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
 *
 * $KAME: altq.h,v 1.10 2003/07/10 12:07:47 kjc Exp $
 * $FreeBSD$
 */
#ifndef _ALTQ_ALTQ_H_
#define	_ALTQ_ALTQ_H_

#if 0
/*
 * allow altq-3 (altqd(8) and /dev/altq) to coexist with the new pf-based altq.
 * altq3 is mainly for research experiments. pf-based altq is for daily use.
 */
#define ALTQ3_COMPAT		/* for compatibility with altq-3 */
#define ALTQ3_CLFIER_COMPAT	/* for compatibility with altq-3 classifier */
#endif


/* altq discipline type */
#define	ALTQT_NONE		0	/* reserved */
#define	ALTQT_CBQ		1	/* cbq */
#define	ALTQT_WFQ		2	/* wfq */
#define	ALTQT_AFMAP		3	/* afmap */
#define	ALTQT_FIFOQ		4	/* fifoq */
#define	ALTQT_RED		5	/* red */
#define	ALTQT_RIO		6	/* rio */
#define	ALTQT_LOCALQ		7	/* local use */
#define	ALTQT_HFSC		8	/* hfsc */
#define	ALTQT_CDNR		9	/* traffic conditioner */
#define	ALTQT_BLUE		10	/* blue */
#define	ALTQT_PRIQ		11	/* priority queue */
#define	ALTQT_JOBS		12	/* JoBS */
#define	ALTQT_FAIRQ		13	/* fairq */
#define	ALTQT_CODEL		14      /* CoDel */
#define	ALTQT_MAX		15	/* should be max discipline type + 1 */


/* simple token backet meter profile */
struct	tb_profile {
	u_int64_t	rate;	/* rate in bit-per-sec */
	u_int32_t	depth;	/* depth in bytes */
};


/*
 * generic packet counter
 */
struct pktcntr {
	u_int64_t	packets;
	u_int64_t	bytes;
};

#define	PKTCNTR_ADD(cntr, len)	\
	do { (cntr)->packets++; (cntr)->bytes += len; } while (/*CONSTCOND*/ 0)


#ifdef _KERNEL
#include <net/altq/altq_var.h>
#endif

/*
 * Can't put these versions in the scheduler-specific headers and include
 * them all here as that will cause build failure due to cross-including
 * each other scheduler's private bits into each scheduler's
 * implementation.
 */
#define CBQ_STATS_VERSION	0	/* Latest version of class_stats_t */
#define CODEL_STATS_VERSION	0	/* Latest version of codel_ifstats */
#define FAIRQ_STATS_VERSION	0	/* Latest version of fairq_classstats */
#define HFSC_STATS_VERSION	1	/* Latest version of hfsc_classstats */
#define PRIQ_STATS_VERSION	0	/* Latest version of priq_classstats */

/* Return the latest stats version for the given scheduler. */
static inline int altq_stats_version(int scheduler)
{
	switch (scheduler) {
	case ALTQT_CBQ:   return (CBQ_STATS_VERSION);
	case ALTQT_CODEL: return (CODEL_STATS_VERSION);
	case ALTQT_FAIRQ: return (FAIRQ_STATS_VERSION);
	case ALTQT_HFSC:  return (HFSC_STATS_VERSION);
	case ALTQT_PRIQ:  return (PRIQ_STATS_VERSION);
	default: return (0);
	}
}
	
#endif /* _ALTQ_ALTQ_H_ */
