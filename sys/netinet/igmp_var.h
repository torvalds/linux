/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 1988 Stephen Deering.
 * Copyright (c) 1992, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * Stephen Deering of Stanford University.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
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
 *	from: @(#)igmp_var.h	8.1 (Berkeley) 7/19/93
 * $FreeBSD$
 */

#ifndef _NETINET_IGMP_VAR_H_
#define _NETINET_IGMP_VAR_H_

/*
 * Internet Group Management Protocol (IGMP),
 * implementation-specific definitions.
 *
 * Written by Steve Deering, Stanford, May 1988.
 *
 * MULTICAST Revision: 3.5.1.3
 */

/*
 * IGMPv3 protocol statistics.
 */
struct igmpstat {
	/*
	 * Structure header (to insulate ABI changes).
	 */
	uint32_t igps_version;		/* version of this structure */
	uint32_t igps_len;		/* length of this structure */
	/*
	 * Message statistics.
	 */
	uint64_t igps_rcv_total;	/* total IGMP messages received */
	uint64_t igps_rcv_tooshort;	/* received with too few bytes */
	uint64_t igps_rcv_badttl;	/* received with ttl other than 1 */
	uint64_t igps_rcv_badsum;	/* received with bad checksum */
	/*
	 * Query statistics.
	 */
	uint64_t igps_rcv_v1v2_queries;	/* received IGMPv1/IGMPv2 queries */
	uint64_t igps_rcv_v3_queries;	/* received IGMPv3 queries */
	uint64_t igps_rcv_badqueries;	/* received invalid queries */
	uint64_t igps_rcv_gen_queries;	/* received general queries */
	uint64_t igps_rcv_group_queries;/* received group queries */
	uint64_t igps_rcv_gsr_queries;	/* received group-source queries */
	uint64_t igps_drop_gsr_queries;	/* dropped group-source queries */
	/*
	 * Report statistics.
	 */
	uint64_t igps_rcv_reports;	/* received membership reports */
	uint64_t igps_rcv_badreports;	/* received invalid reports */
	uint64_t igps_rcv_ourreports;	/* received reports for our groups */
	uint64_t igps_rcv_nora;		/* received w/o Router Alert option */
	uint64_t igps_snd_reports;	/* sent membership reports */
	/*
	 * Padding for future additions.
	 */
	uint64_t __igps_pad[4];
};
#define IGPS_VERSION_3	3		/* as of FreeBSD 8.x */
#define IGPS_VERSION3_LEN		168
#ifdef CTASSERT
CTASSERT(sizeof(struct igmpstat) == IGPS_VERSION3_LEN);
#endif

/*
 * Identifiers for IGMP sysctl nodes
 */
#define IGMPCTL_STATS		1	/* statistics (read-only) */

#define IGMP_RANDOM_DELAY(X) (random() % (X) + 1)
#define IGMP_MAX_STATE_CHANGES		24 /* Max pending changes per group */

/*
 * IGMP per-group states.
 */
#define IGMP_NOT_MEMBER			0 /* Can garbage collect in_multi */
#define IGMP_SILENT_MEMBER		1 /* Do not perform IGMP for group */
#define IGMP_REPORTING_MEMBER		2 /* IGMPv1/2/3 we are reporter */
#define IGMP_IDLE_MEMBER		3 /* IGMPv1/2 we reported last */
#define IGMP_LAZY_MEMBER		4 /* IGMPv1/2 other member reporting */
#define IGMP_SLEEPING_MEMBER		5 /* IGMPv1/2 start query response */
#define IGMP_AWAKENING_MEMBER		6 /* IGMPv1/2 group timer will start */
#define IGMP_G_QUERY_PENDING_MEMBER	7 /* IGMPv3 group query pending */
#define IGMP_SG_QUERY_PENDING_MEMBER	8 /* IGMPv3 source query pending */
#define IGMP_LEAVING_MEMBER		9 /* IGMPv3 dying gasp (pending last */
					  /* retransmission of INCLUDE {}) */

/*
 * IGMP version tag.
 */
#define IGMP_VERSION_NONE		0 /* Invalid */
#define IGMP_VERSION_1			1
#define IGMP_VERSION_2			2
#define IGMP_VERSION_3			3 /* Default */

/*
 * IGMPv3 protocol control variables.
 */
#define IGMP_RV_INIT		2	/* Robustness Variable */
#define IGMP_RV_MIN		1
#define IGMP_RV_MAX		7

#define IGMP_QI_INIT		125	/* Query Interval (s) */
#define IGMP_QI_MIN		1
#define IGMP_QI_MAX		255

#define IGMP_QRI_INIT		10	/* Query Response Interval (s) */
#define IGMP_QRI_MIN		1
#define IGMP_QRI_MAX		255

#define IGMP_URI_INIT		3	/* Unsolicited Report Interval (s) */
#define IGMP_URI_MIN		0
#define IGMP_URI_MAX		10

#define IGMP_MAX_G_GS_PACKETS		8 /* # of packets to answer G/GS */
#define IGMP_MAX_STATE_CHANGE_PACKETS	8 /* # of packets per state change */
#define IGMP_MAX_RESPONSE_PACKETS	16 /* # of packets for general query */
#define IGMP_MAX_RESPONSE_BURST		4 /* # of responses to send at once */
#define IGMP_RESPONSE_BURST_INTERVAL	(PR_FASTHZ / 2)	/* 500ms */

/*
 * IGMP-specific mbuf flags.
 */
#define M_IGMPV2	M_PROTO1	/* Packet is IGMPv2 */
#define M_IGMPV3_HDR	M_PROTO2	/* Packet has IGMPv3 headers */
#define M_GROUPREC	M_PROTO3	/* mbuf chain is a group record */
#define M_IGMP_LOOP	M_PROTO4	/* transmit on loif, not real ifp */

/*
 * Default amount of leading space for IGMPv3 to allocate at the
 * beginning of its mbuf packet chains, to avoid fragmentation and
 * unnecessary allocation of leading mbufs.
 */
#define RAOPT_LEN	4		/* Length of IP Router Alert option */
#define	IGMP_LEADINGSPACE		\
	(sizeof(struct ip) + RAOPT_LEN + sizeof(struct igmp_report))

/*
 * Structure returned by net.inet.igmp.ifinfo sysctl.
 */
struct igmp_ifinfo {
	uint32_t igi_version;	/* IGMPv3 Host Compatibility Mode */
	uint32_t igi_v1_timer;	/* IGMPv1 Querier Present timer (s) */
	uint32_t igi_v2_timer;	/* IGMPv2 Querier Present timer (s) */
	uint32_t igi_v3_timer;	/* IGMPv3 General Query (interface) timer (s)*/
	uint32_t igi_flags;	/* IGMP per-interface flags */
#define IGIF_SILENT	0x00000001	/* Do not use IGMP on this ifp */
#define IGIF_LOOPBACK	0x00000002	/* Send IGMP reports to loopback */
	uint32_t igi_rv;	/* IGMPv3 Robustness Variable */
	uint32_t igi_qi;	/* IGMPv3 Query Interval (s) */
	uint32_t igi_qri;	/* IGMPv3 Query Response Interval (s) */
	uint32_t igi_uri;	/* IGMPv3 Unsolicited Report Interval (s) */
};

#ifdef _KERNEL
#define	IGMPSTAT_ADD(name, val)		V_igmpstat.name += (val)
#define	IGMPSTAT_INC(name)		IGMPSTAT_ADD(name, 1)

/*
 * Subsystem lock macros.
 * The IGMP lock is only taken with IGMP. Currently it is system-wide.
 * VIMAGE: The lock could be pushed to per-VIMAGE granularity in future.
 */
#define	IGMP_LOCK_INIT()	mtx_init(&igmp_mtx, "igmp_mtx", NULL, MTX_DEF)
#define	IGMP_LOCK_DESTROY()	mtx_destroy(&igmp_mtx)
#define	IGMP_LOCK()		mtx_lock(&igmp_mtx)
#define	IGMP_LOCK_ASSERT()	mtx_assert(&igmp_mtx, MA_OWNED)
#define	IGMP_UNLOCK()		mtx_unlock(&igmp_mtx)
#define	IGMP_UNLOCK_ASSERT()	mtx_assert(&igmp_mtx, MA_NOTOWNED)

/*
 * Per-interface IGMP router version information.
 */
struct igmp_ifsoftc {
	LIST_ENTRY(igmp_ifsoftc) igi_link;
	struct ifnet *igi_ifp;	/* pointer back to interface */
	uint32_t igi_version;	/* IGMPv3 Host Compatibility Mode */
	uint32_t igi_v1_timer;	/* IGMPv1 Querier Present timer (s) */
	uint32_t igi_v2_timer;	/* IGMPv2 Querier Present timer (s) */
	uint32_t igi_v3_timer;	/* IGMPv3 General Query (interface) timer (s)*/
	uint32_t igi_flags;	/* IGMP per-interface flags */
	uint32_t igi_rv;	/* IGMPv3 Robustness Variable */
	uint32_t igi_qi;	/* IGMPv3 Query Interval (s) */
	uint32_t igi_qri;	/* IGMPv3 Query Response Interval (s) */
	uint32_t igi_uri;	/* IGMPv3 Unsolicited Report Interval (s) */
	struct mbufq	igi_gq;		/* general query responses queue */
};

int	igmp_change_state(struct in_multi *);
void	igmp_fasttimo(void);
struct igmp_ifsoftc *
	igmp_domifattach(struct ifnet *);
void	igmp_domifdetach(struct ifnet *);
void	igmp_ifdetach(struct ifnet *);
int	igmp_input(struct mbuf **, int *, int);
void	igmp_slowtimo(void);

SYSCTL_DECL(_net_inet_igmp);

#endif /* _KERNEL */
#endif
