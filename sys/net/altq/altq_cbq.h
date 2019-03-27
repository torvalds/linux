/*-
 * Copyright (c) Sun Microsystems, Inc. 1993-1998 All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *      This product includes software developed by the SMCC Technology
 *      Development Group at Sun Microsystems, Inc.
 *
 * 4. The name of the Sun Microsystems, Inc nor may not be used to endorse or
 *      promote products derived from this software without specific prior
 *      written permission.
 *
 * SUN MICROSYSTEMS DOES NOT CLAIM MERCHANTABILITY OF THIS SOFTWARE OR THE
 * SUITABILITY OF THIS SOFTWARE FOR ANY PARTICULAR PURPOSE.  The software is
 * provided "as is" without express or implied warranty of any kind.
 *
 * These notices must be retained in any copies of any part of this software.
 *
 * $KAME: altq_cbq.h,v 1.12 2003/10/03 05:05:15 kjc Exp $
 * $FreeBSD$
 */

#ifndef _ALTQ_ALTQ_CBQ_H_
#define	_ALTQ_ALTQ_CBQ_H_

#include <net/altq/altq.h>
#include <net/altq/altq_rmclass.h>
#include <net/altq/altq_codel.h>
#include <net/altq/altq_red.h>
#include <net/altq/altq_rio.h>

#ifdef __cplusplus
extern "C" {
#endif

#define	NULL_CLASS_HANDLE	0

/* class flags must be same as class flags in altq_rmclass.h */
#define	CBQCLF_RED		0x0001	/* use RED */
#define	CBQCLF_ECN		0x0002  /* use RED/ECN */
#define	CBQCLF_RIO		0x0004  /* use RIO */
#define	CBQCLF_FLOWVALVE	0x0008	/* use flowvalve (aka penalty-box) */
#define	CBQCLF_CLEARDSCP	0x0010  /* clear diffserv codepoint */
#define	CBQCLF_BORROW		0x0020  /* borrow from parent */
#define	CBQCLF_CODEL		0x0040	/* use CoDel */

#ifdef _KERNEL
CTASSERT(CBQCLF_RED == RMCF_RED);
CTASSERT(CBQCLF_ECN == RMCF_ECN);
CTASSERT(CBQCLF_RIO == RMCF_RIO);
CTASSERT(CBQCLF_FLOWVALVE == RMCF_FLOWVALVE);
CTASSERT(CBQCLF_CLEARDSCP == RMCF_CLEARDSCP);
CTASSERT(CBQCLF_CODEL == RMCF_CODEL);
#endif

/* class flags only for root class */
#define	CBQCLF_WRR		0x0100	/* weighted-round robin */
#define	CBQCLF_EFFICIENT	0x0200  /* work-conserving */

/* class flags for special classes */
#define	CBQCLF_ROOTCLASS	0x1000	/* root class */
#define	CBQCLF_DEFCLASS		0x2000	/* default class */
#define	CBQCLF_CLASSMASK	0xf000	/* class mask */

#define	CBQ_MAXQSIZE		200
#define	CBQ_MAXPRI		RM_MAXPRIO

typedef struct _cbq_class_stats_ {
	u_int32_t	handle;
	u_int		depth;

	struct pktcntr	xmit_cnt;	/* packets sent in this class */
	struct pktcntr	drop_cnt;	/* dropped packets */
	u_int		over;		/* # times went over limit */
	u_int		borrows;	/* # times tried to borrow */
	u_int		overactions;	/* # times invoked overlimit action */
	u_int		delays;		/* # times invoked delay actions */

	/* other static class parameters useful for debugging */
	int		priority;
	int		maxidle;
	int		minidle;
	int		offtime;
	int		qmax;
	int		ns_per_byte;
	int		wrr_allot;

	int		qcnt;		/* # packets in queue */
	int		avgidle;

	/* codel, red and rio related info */
	int		qtype;
	struct redstats	red[3];
	struct codel_stats codel;
} class_stats_t;

/*
 * CBQ_STATS_VERSION is defined in altq.h to work around issues stemming
 * from mixing of public-API and internal bits in each scheduler-specific
 * header.
 */


#ifdef _KERNEL
/*
 * Define macros only good for kernel drivers and modules.
 */
#define	CBQ_WATCHDOG		(hz / 20)
#define	CBQ_TIMEOUT		10
#define	CBQ_LS_TIMEOUT		(20 * hz / 1000)

#define	CBQ_MAX_CLASSES	256


/*
 * Define State structures.
 */
typedef struct cbqstate {
	int			 cbq_qlen;	/* # of packets in cbq */
	struct rm_class		*cbq_class_tbl[CBQ_MAX_CLASSES];

	struct rm_ifdat		 ifnp;
	struct callout		 cbq_callout;	/* for timeouts */
#ifdef ALTQ3_CLFIER_COMPAT
	struct acc_classifier	cbq_classifier;
#endif
} cbq_state_t;

#endif /* _KERNEL */

#ifdef __cplusplus
}
#endif

#endif /* !_ALTQ_ALTQ_CBQ_H_ */
