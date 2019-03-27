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
 * $DragonFly: src/sys/net/altq/altq_fairq.h,v 1.1 2008/04/06 18:58:15 dillon Exp $
 * $FreeBSD$
 */

#ifndef _ALTQ_ALTQ_FAIRQ_H_
#define	_ALTQ_ALTQ_FAIRQ_H_

#include <net/altq/altq.h>
#include <net/altq/altq_classq.h>
#include <net/altq/altq_codel.h>
#include <net/altq/altq_red.h>
#include <net/altq/altq_rio.h>
#include <net/altq/altq_rmclass.h>

#define	FAIRQ_MAX_BUCKETS	2048	/* maximum number of sorting buckets */
#define	FAIRQ_MAXPRI		RM_MAXPRIO
#define FAIRQ_BITMAP_WIDTH	(sizeof(fairq_bitmap_t)*8)
#define FAIRQ_BITMAP_MASK	(FAIRQ_BITMAP_WIDTH - 1)

/* fairq class flags */
#define	FARF_RED		0x0001	/* use RED */
#define	FARF_ECN		0x0002  /* use RED/ECN */
#define	FARF_RIO		0x0004  /* use RIO */
#define	FARF_CODEL		0x0008	/* use CoDel */
#define	FARF_CLEARDSCP		0x0010  /* clear diffserv codepoint */
#define	FARF_DEFAULTCLASS	0x1000	/* default class */

#define FARF_HAS_PACKETS	0x2000	/* might have queued packets */

#define FARF_USERFLAGS		(FARF_RED|FARF_ECN|FARF_RIO|FARF_CLEARDSCP| \
				 FARF_DEFAULTCLASS)

/* special class handles */
#define	FAIRQ_NULLCLASS_HANDLE	0

typedef u_int	fairq_bitmap_t;

struct fairq_classstats {
	uint32_t		class_handle;

	u_int			qlength;
	u_int			qlimit;
	struct pktcntr		xmit_cnt;  /* transmitted packet counter */
	struct pktcntr		drop_cnt;  /* dropped packet counter */

	/* codel, red and rio related info */
	int			qtype;
	struct redstats		red[3];	/* rio has 3 red stats */
	struct codel_stats	codel;
};

/*
 * FAIRQ_STATS_VERSION is defined in altq.h to work around issues stemming
 * from mixing of public-API and internal bits in each scheduler-specific
 * header.
 */

#ifdef _KERNEL

typedef struct fairq_bucket {
	struct fairq_bucket *next;	/* circular list */
	struct fairq_bucket *prev;	/* circular list */
	class_queue_t	queue;		/* the actual queue */
	uint64_t	bw_bytes;	/* statistics used to calculate bw */
	uint64_t	bw_delta;	/* statistics used to calculate bw */
	uint64_t	last_time;
	int		in_use;
} fairq_bucket_t;

struct fairq_class {
	uint32_t	cl_handle;	/* class handle */
	u_int		cl_nbuckets;	/* (power of 2) */
	u_int		cl_nbucket_mask; /* bucket mask */
	fairq_bucket_t	*cl_buckets;
	fairq_bucket_t	*cl_head;	/* head of circular bucket list */
	fairq_bucket_t	*cl_polled;
	union {
		struct red	*cl_red;	/* RED state */
		struct codel	*cl_codel;	/* CoDel state */
	} cl_aqm;
#define	cl_red		cl_aqm.cl_red
#define	cl_codel	cl_aqm.cl_codel
	u_int		cl_hogs_m1;
	u_int		cl_lssc_m1;
	u_int		cl_bandwidth;
	uint64_t	cl_bw_bytes;
	uint64_t	cl_bw_delta;
	uint64_t	cl_last_time;
	int		cl_qtype;	/* rollup */
	int		cl_qlimit;
	int		cl_pri;		/* priority */
	int		cl_flags;	/* class flags */
	struct fairq_if	*cl_pif;	/* back pointer to pif */
	struct altq_pktattr *cl_pktattr; /* saved header used by ECN */

	/* round robin index */

	/* statistics */
	struct pktcntr  cl_xmitcnt;	/* transmitted packet counter */
	struct pktcntr  cl_dropcnt;	/* dropped packet counter */
};

/*
 * fairq interface state
 */
struct fairq_if {
	struct fairq_if		*pif_next;	/* interface state list */
	struct ifaltq		*pif_ifq;	/* backpointer to ifaltq */
	u_int			pif_bandwidth;	/* link bandwidth in bps */
	int			pif_maxpri;	/* max priority in use */
	struct fairq_class	*pif_poll_cache;/* cached poll */
	struct fairq_class	*pif_default;	/* default class */
	struct fairq_class	*pif_classes[FAIRQ_MAXPRI]; /* classes */
};

#endif /* _KERNEL */

#endif /* _ALTQ_ALTQ_FAIRQ_H_ */
