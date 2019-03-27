/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2009 Bruce Simpson.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. The name of the author may not be used to endorse or promote
 *    products derived from this software without specific prior written
 *    permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $FreeBSD$
 */
#ifndef _NETINET6_MLD6_VAR_H_
#define _NETINET6_MLD6_VAR_H_

/*
 * Multicast Listener Discovery (MLD)
 * implementation-specific definitions.
 */

/*
 * MLD per-group states.
 */
#define MLD_NOT_MEMBER			0 /* Can garbage collect group */
#define MLD_SILENT_MEMBER		1 /* Do not perform MLD for group */
#define MLD_REPORTING_MEMBER		2 /* MLDv1 we are reporter */
#define MLD_IDLE_MEMBER			3 /* MLDv1 we reported last */
#define MLD_LAZY_MEMBER			4 /* MLDv1 other member reporting */
#define MLD_SLEEPING_MEMBER		5 /* MLDv1 start query response */
#define MLD_AWAKENING_MEMBER		6 /* MLDv1 group timer will start */
#define MLD_G_QUERY_PENDING_MEMBER	7 /* MLDv2 group query pending */
#define MLD_SG_QUERY_PENDING_MEMBER	8 /* MLDv2 source query pending */
#define MLD_LEAVING_MEMBER		9 /* MLDv2 dying gasp (pending last */
					  /* retransmission of INCLUDE {}) */

/*
 * MLD version tag.
 */
#define MLD_VERSION_NONE		0 /* Invalid */
#define MLD_VERSION_1			1
#define MLD_VERSION_2			2 /* Default */

/*
 * MLDv2 protocol control variables.
 */
#define MLD_RV_INIT		2	/* Robustness Variable */
#define MLD_RV_MIN		1
#define MLD_RV_MAX		7

#define MLD_QI_INIT		125	/* Query Interval (s) */
#define MLD_QI_MIN		1
#define MLD_QI_MAX		255

#define MLD_QRI_INIT		10	/* Query Response Interval (s) */
#define MLD_QRI_MIN		1
#define MLD_QRI_MAX		255

#define MLD_URI_INIT		3	/* Unsolicited Report Interval (s) */
#define MLD_URI_MIN		0
#define MLD_URI_MAX		10

#define MLD_MAX_GS_SOURCES		256 /* # of sources in rx GS query */
#define MLD_MAX_G_GS_PACKETS		8 /* # of packets to answer G/GS */
#define MLD_MAX_STATE_CHANGE_PACKETS	8 /* # of packets per state change */
#define MLD_MAX_RESPONSE_PACKETS	16 /* # of packets for general query */
#define MLD_MAX_RESPONSE_BURST		4 /* # of responses to send at once */
#define MLD_RESPONSE_BURST_INTERVAL	(PR_FASTHZ / 2)	/* 500ms */

/*
 * MLD-specific mbuf flags.
 */
#define M_MLDV1		M_PROTO1	/* Packet is MLDv1 */
#define M_GROUPREC	M_PROTO3	/* mbuf chain is a group record */

/*
 * Leading space for MLDv2 reports inside MTU.
 *
 * NOTE: This differs from IGMPv3 significantly. KAME IPv6 requires
 * that a fully formed mbuf chain *without* the Router Alert option
 * is passed to ip6_output(), however we must account for it in the
 * MTU if we need to split an MLDv2 report into several packets.
 *
 * We now put the MLDv2 report header in the initial mbuf containing
 * the IPv6 header.
 */
#define	MLD_MTUSPACE	(sizeof(struct ip6_hdr) + sizeof(struct mld_raopt) + \
			 sizeof(struct icmp6_hdr))

/*
 * Structure returned by net.inet6.mld.ifinfo.
 */
struct mld_ifinfo {
	uint32_t mli_version;	/* MLDv1 Host Compatibility Mode */
	uint32_t mli_v1_timer;	/* MLDv1 Querier Present timer (s) */
	uint32_t mli_v2_timer;	/* MLDv2 General Query (interface) timer (s)*/
	uint32_t mli_flags;	/* MLD per-interface flags */
#define MLIF_SILENT	0x00000001	/* Do not use MLD on this ifp */
#define MLIF_USEALLOW	0x00000002	/* Use ALLOW/BLOCK for joins/leaves */
	uint32_t mli_rv;	/* MLDv2 Robustness Variable */
	uint32_t mli_qi;	/* MLDv2 Query Interval (s) */
	uint32_t mli_qri;	/* MLDv2 Query Response Interval (s) */
	uint32_t mli_uri;	/* MLDv2 Unsolicited Report Interval (s) */
};

#ifdef _KERNEL
/*
 * Per-link MLD state.
 */
struct mld_ifsoftc {
	LIST_ENTRY(mld_ifsoftc) mli_link;
	struct ifnet *mli_ifp;	/* interface this instance belongs to */
	uint32_t mli_version;	/* MLDv1 Host Compatibility Mode */
	uint32_t mli_v1_timer;	/* MLDv1 Querier Present timer (s) */
	uint32_t mli_v2_timer;	/* MLDv2 General Query (interface) timer (s)*/
	uint32_t mli_flags;	/* MLD per-interface flags */
	uint32_t mli_rv;	/* MLDv2 Robustness Variable */
	uint32_t mli_qi;	/* MLDv2 Query Interval (s) */
	uint32_t mli_qri;	/* MLDv2 Query Response Interval (s) */
	uint32_t mli_uri;	/* MLDv2 Unsolicited Report Interval (s) */
	struct mbufq	 mli_gq;	/* queue of general query responses */
};

#define MLD_RANDOM_DELAY(X)		(arc4random() % (X) + 1)
#define MLD_MAX_STATE_CHANGES		24 /* Max pending changes per group */

/*
 * Subsystem lock macros.
 * The MLD lock is only taken with MLD. Currently it is system-wide.
 * VIMAGE: The lock could be pushed to per-VIMAGE granularity in future.
 */
#define	MLD_LOCK_INIT()	mtx_init(&mld_mtx, "mld_mtx", NULL, MTX_DEF)
#define	MLD_LOCK_DESTROY()	mtx_destroy(&mld_mtx)
#define	MLD_LOCK()		mtx_lock(&mld_mtx)
#define	MLD_LOCK_ASSERT()	mtx_assert(&mld_mtx, MA_OWNED)
#define	MLD_UNLOCK()		mtx_unlock(&mld_mtx)
#define	MLD_UNLOCK_ASSERT()	mtx_assert(&mld_mtx, MA_NOTOWNED)

/*
 * Per-link MLD context.
 */
#define MLD_IFINFO(ifp) \
	(((struct in6_ifextra *)(ifp)->if_afdata[AF_INET6])->mld_ifinfo)

struct in6_multi_head;
int	mld_change_state(struct in6_multi *, const int);
struct mld_ifsoftc *
	mld_domifattach(struct ifnet *);
void	mld_domifdetach(struct ifnet *);
void	mld_fasttimo(void);
void	mld_ifdetach(struct ifnet *, struct in6_multi_head *);
int	mld_input(struct mbuf *, int, int);
void	mld_slowtimo(void);

#ifdef SYSCTL_DECL
SYSCTL_DECL(_net_inet6_mld);
#endif

#endif /* _KERNEL */

#endif /* _NETINET6_MLD6_VAR_H_ */
