/*-
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright (c) 2001 Daniel Hartmeier
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 *    - Redistributions of source code must retain the above copyright
 *      notice, this list of conditions and the following disclaimer.
 *    - Redistributions in binary form must reproduce the above
 *      copyright notice, this list of conditions and the following
 *      disclaimer in the documentation and/or other materials provided
 *      with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
 * COPYRIGHT HOLDERS OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 * CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
 * ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 *
 *	$OpenBSD: pfvar.h,v 1.282 2009/01/29 15:12:28 pyr Exp $
 *	$FreeBSD$
 */

#ifndef	_NET_PF_ALTQ_H_
#define	_NET_PF_ALTQ_H_

struct cbq_opts {
	u_int		minburst;
	u_int		maxburst;
	u_int		pktsize;
	u_int		maxpktsize;
	u_int		ns_per_byte;
	u_int		maxidle;
	int		minidle;
	u_int		offtime;
	int		flags;
};

struct codel_opts {
	u_int		target;
	u_int		interval;
	int		ecn;
};

struct priq_opts {
	int		flags;
};

struct hfsc_opts_v0 {
	/* real-time service curve */
	u_int		rtsc_m1;	/* slope of the 1st segment in bps */
	u_int		rtsc_d;		/* the x-projection of m1 in msec */
	u_int		rtsc_m2;	/* slope of the 2nd segment in bps */
	/* link-sharing service curve */
	u_int		lssc_m1;
	u_int		lssc_d;
	u_int		lssc_m2;
	/* upper-limit service curve */
	u_int		ulsc_m1;
	u_int		ulsc_d;
	u_int		ulsc_m2;
	int		flags;
};

struct hfsc_opts_v1 {
	/* real-time service curve */
	u_int64_t	rtsc_m1;	/* slope of the 1st segment in bps */
	u_int		rtsc_d;		/* the x-projection of m1 in msec */
	u_int64_t	rtsc_m2;	/* slope of the 2nd segment in bps */
	/* link-sharing service curve */
	u_int64_t	lssc_m1;
	u_int		lssc_d;
	u_int64_t	lssc_m2;
	/* upper-limit service curve */
	u_int64_t	ulsc_m1;
	u_int		ulsc_d;
	u_int64_t	ulsc_m2;
	int		flags;
};

/*
 * struct hfsc_opts doesn't have a version indicator macro or
 * backwards-compat and convenience macros because both in the kernel and
 * the pfctl parser, there are struct hfsc_opts instances named 'hfsc_opts'.
 * It is believed that only in-tree code uses struct hfsc_opts, so
 * backwards-compat macros are not necessary.  The few in-tree uses can just
 * be updated to the latest versioned struct tag.
 */

/*
 * XXX this needs some work
 */
struct fairq_opts {
	u_int           nbuckets;
	u_int           hogs_m1;
	int             flags;

	/* link sharing service curve */
	u_int           lssc_m1;
	u_int           lssc_d;
	u_int           lssc_m2;
};

/*
 * struct pf_altq_v0, struct pf_altq_v1, etc. are the ioctl argument
 * structures corresponding to struct pfioc_altq_v0, struct pfioc_altq_v1,
 * etc.
 *
 */
struct pf_altq_v0 {
	char			 ifname[IFNAMSIZ];

	/*
	 * This member is a holdover from when the kernel state structure
	 * was reused as the ioctl argument structure, and remains to
	 * preserve the size and layout of this struct for backwards compat.
	 */
	void			*unused1;
	TAILQ_ENTRY(pf_altq_v0)	 entries;

	/* scheduler spec */
	uint8_t			 scheduler;	/* scheduler type */
	uint16_t		 tbrsize;	/* tokenbucket regulator size */
	uint32_t		 ifbandwidth;	/* interface bandwidth */

	/* queue spec */
	char			 qname[PF_QNAME_SIZE];	/* queue name */
	char			 parent[PF_QNAME_SIZE];	/* parent name */
	uint32_t		 parent_qid;	/* parent queue id */
	uint32_t		 bandwidth;	/* queue bandwidth */
	uint8_t			 priority;	/* priority */
	uint8_t			 local_flags;	/* dynamic interface */
#define	PFALTQ_FLAG_IF_REMOVED		0x01

	uint16_t		 qlimit;	/* queue size limit */
	uint16_t		 flags;		/* misc flags */
	union {
		struct cbq_opts		 cbq_opts;
		struct codel_opts	 codel_opts;
		struct priq_opts	 priq_opts;
		struct hfsc_opts_v0	 hfsc_opts;
		struct fairq_opts        fairq_opts;
	} pq_u;

	uint32_t		 qid;		/* return value */
};

struct pf_altq_v1 {
	char			 ifname[IFNAMSIZ];

	TAILQ_ENTRY(pf_altq_v1)	 entries;

	/* scheduler spec */
	uint8_t			 scheduler;	/* scheduler type */
	uint32_t		 tbrsize;	/* tokenbucket regulator size */
	uint64_t		 ifbandwidth;	/* interface bandwidth */

	/* queue spec */
	char			 qname[PF_QNAME_SIZE];	/* queue name */
	char			 parent[PF_QNAME_SIZE];	/* parent name */
	uint32_t		 parent_qid;	/* parent queue id */
	uint64_t		 bandwidth;	/* queue bandwidth */
	uint8_t			 priority;	/* priority */
	uint8_t			 local_flags;	/* dynamic interface, see _v0 */

	uint16_t		 qlimit;	/* queue size limit */
	uint16_t		 flags;		/* misc flags */
	union {
		struct cbq_opts		 cbq_opts;
		struct codel_opts	 codel_opts;
		struct priq_opts	 priq_opts;
		struct hfsc_opts_v1	 hfsc_opts;
		struct fairq_opts        fairq_opts;
	} pq_u;

	uint32_t		 qid;		/* return value */
};

/* Latest version of struct pf_altq_vX */
#define PF_ALTQ_VERSION	1

#ifdef _KERNEL
struct pf_kaltq {
	char			 ifname[IFNAMSIZ];

	void			*altq_disc;	/* discipline-specific state */
	TAILQ_ENTRY(pf_kaltq)	 entries;

	/* scheduler spec */
	uint8_t			 scheduler;	/* scheduler type */
	uint32_t		 tbrsize;	/* tokenbucket regulator size */
	uint64_t		 ifbandwidth;	/* interface bandwidth */

	/* queue spec */
	char			 qname[PF_QNAME_SIZE];	/* queue name */
	char			 parent[PF_QNAME_SIZE];	/* parent name */
	uint32_t		 parent_qid;	/* parent queue id */
	uint64_t		 bandwidth;	/* queue bandwidth */
	uint8_t			 priority;	/* priority */
	uint8_t			 local_flags;	/* dynamic interface, see _v0 */

	uint16_t		 qlimit;	/* queue size limit */
	uint16_t		 flags;		/* misc flags */
	union {
		struct cbq_opts		 cbq_opts;
		struct codel_opts	 codel_opts;
		struct priq_opts	 priq_opts;
		struct hfsc_opts_v1	 hfsc_opts;
		struct fairq_opts        fairq_opts;
	} pq_u;

	uint32_t		 qid;		/* return value */
};
#endif /* _KERNEL */

/*
 * Compatibility and convenience macros
 */
#ifdef _KERNEL
/*
 * Avoid a patch with 100+ lines of name substitution.
 */
#define pf_altq pf_kaltq

#else /* _KERNEL */

#ifdef PFIOC_USE_LATEST
/*
 * Maintaining in-tree consumers of the ioctl interface is easier when that
 * code can be written in terms old names that refer to the latest interface
 * version as that reduces the required changes in the consumers to those
 * that are functionally necessary to accommodate a new interface version.
 */
#define	pf_altq		__CONCAT(pf_altq_v, PF_ALTQ_VERSION)

#else /* PFIOC_USE_LATEST */
/*
 * When building out-of-tree code that is written for the old interface,
 * such as may exist in ports for example, resolve the old pf_altq struct
 * tag to the v0 version.
 */
#define	pf_altq		__CONCAT(pf_altq_v, 0)

#endif /* PFIOC_USE_LATEST */
#endif /* _KERNEL */

#endif	/* _NET_PF_ALTQ_H_ */
