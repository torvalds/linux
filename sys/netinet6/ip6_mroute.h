/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (C) 1998 WIDE Project.
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
 * 3. Neither the name of the project nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE PROJECT AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE PROJECT OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 *	$KAME: ip6_mroute.h,v 1.19 2001/06/14 06:12:55 suz Exp $
 * $FreeBSD$
 */

/*	BSDI ip_mroute.h,v 2.5 1996/10/11 16:01:48 pjd Exp	*/

/*
 * Definitions for IP multicast forwarding.
 *
 * Written by David Waitzman, BBN Labs, August 1988.
 * Modified by Steve Deering, Stanford, February 1989.
 * Modified by Ajit Thyagarajan, PARC, August 1993.
 * Modified by Ajit Thyagarajan, PARC, August 1994.
 * Modified by Ahmed Helmy, USC, September 1996.
 *
 * MROUTING Revision: 1.2
 */

#ifndef _NETINET6_IP6_MROUTE_H_
#define _NETINET6_IP6_MROUTE_H_

/*
 * Multicast Routing set/getsockopt commands.
 */
#ifdef _KERNEL
#define MRT6_OINIT		100	/* initialize forwarder (omrt6msg) */
#endif
#define MRT6_DONE		101	/* shut down forwarder */
#define MRT6_ADD_MIF		102	/* add multicast interface */
#define MRT6_DEL_MIF		103	/* delete multicast interface */
#define MRT6_ADD_MFC		104	/* insert forwarding cache entry */
#define MRT6_DEL_MFC		105	/* delete forwarding cache entry */
#define MRT6_PIM                107     /* enable pim code */
#define MRT6_INIT		108	/* initialize forwarder (mrt6msg) */

#if BSD >= 199103
#define GET_TIME(t)	microtime(&t)
#elif defined(sun)
#define GET_TIME(t)	uniqtime(&t)
#else
#define GET_TIME(t)	((t) = time)
#endif

/*
 * Types and macros for handling bitmaps with one bit per multicast interface.
 */
typedef u_short mifi_t;		/* type of a mif index */
#define MAXMIFS		64

#ifndef	IF_SETSIZE
#define	IF_SETSIZE	256
#endif

typedef	u_int32_t	if_mask;
#define	NIFBITS	(sizeof(if_mask) * NBBY)	/* bits per mask */

#ifndef howmany
#define	howmany(x, y)	(((x) + ((y) - 1)) / (y))
#endif

typedef	struct if_set {
	if_mask	ifs_bits[howmany(IF_SETSIZE, NIFBITS)];
} if_set;

#define	IF_SET(n, p)	((p)->ifs_bits[(n)/NIFBITS] |= (1 << ((n) % NIFBITS)))
#define	IF_CLR(n, p)	((p)->ifs_bits[(n)/NIFBITS] &= ~(1 << ((n) % NIFBITS)))
#define	IF_ISSET(n, p)	((p)->ifs_bits[(n)/NIFBITS] & (1 << ((n) % NIFBITS)))
#define	IF_COPY(f, t)	bcopy(f, t, sizeof(*(f)))
#define	IF_ZERO(p)	bzero(p, sizeof(*(p)))

/*
 * Argument structure for MRT6_ADD_IF.
 */
struct mif6ctl {
	mifi_t	    mif6c_mifi;		/* the index of the mif to be added  */
	u_char	    mif6c_flags;	/* MIFF_ flags defined below         */
	u_short	    mif6c_pifi;		/* the index of the physical IF */
};

#define	MIFF_REGISTER	0x1	/* mif represents a register end-point */

/*
 * Argument structure for MRT6_ADD_MFC and MRT6_DEL_MFC
 */
struct mf6cctl {
	struct sockaddr_in6 mf6cc_origin;	/* IPv6 origin of mcasts */
	struct sockaddr_in6 mf6cc_mcastgrp; /* multicast group associated */
	mifi_t		mf6cc_parent;	/* incoming ifindex */
	struct if_set	mf6cc_ifset;	/* set of forwarding ifs */
};

/*
 * The kernel's multicast routing statistics.
 */
struct mrt6stat {
	uint64_t mrt6s_mfc_lookups;	/* # forw. cache hash table hits   */
	uint64_t mrt6s_mfc_misses;	/* # forw. cache hash table misses */
	uint64_t mrt6s_upcalls;		/* # calls to multicast routing daemon */
	uint64_t mrt6s_no_route;	/* no route for packet's origin    */
	uint64_t mrt6s_bad_tunnel;	/* malformed tunnel options        */
	uint64_t mrt6s_cant_tunnel;	/* no room for tunnel options      */
	uint64_t mrt6s_wrong_if;	/* arrived on wrong interface	   */
	uint64_t mrt6s_upq_ovflw;	/* upcall Q overflow		   */
	uint64_t mrt6s_cache_cleanups;	/* # entries with no upcalls	   */
	uint64_t mrt6s_drop_sel;	/* pkts dropped selectively        */
	uint64_t mrt6s_q_overflow;	/* pkts dropped - Q overflow       */
	uint64_t mrt6s_pkt2large;	/* pkts dropped - size > BKT SIZE  */
	uint64_t mrt6s_upq_sockfull;	/* upcalls dropped - socket full   */
};

#ifdef MRT6_OINIT
/*
 * Struct used to communicate from kernel to multicast router
 * note the convenient similarity to an IPv6 header.
 * XXX old version, superseded by mrt6msg.
 */
struct omrt6msg {
	u_long	    unused1;
	u_char	    im6_msgtype;		/* what type of message	    */
	u_char	    im6_mbz;			/* must be zero		    */
	u_char	    im6_mif;			/* mif rec'd on		    */
	u_char	    unused2;
	struct in6_addr  im6_src, im6_dst;
};
#endif

/*
 * Structure used to communicate from kernel to multicast router.
 * We'll overlay the structure onto an MLD header (not an IPv6 header
 * like igmpmsg{} used for IPv4 implementation). This is because this
 * structure will be passed via an IPv6 raw socket, on which an application
 * will only receive the payload i.e. the data after the IPv6 header and all
 * the extension headers. (see Section 3 of RFC3542)
 */
struct mrt6msg {
#define MRT6MSG_NOCACHE		1
#define MRT6MSG_WRONGMIF	2
#define MRT6MSG_WHOLEPKT	3		/* used for user level encap*/
	u_char	    im6_mbz;			/* must be zero		    */
	u_char	    im6_msgtype;		/* what type of message	    */
	u_int16_t   im6_mif;			/* mif rec'd on		    */
	u_int32_t   im6_pad;			/* padding for 64bit arch   */
	struct in6_addr  im6_src, im6_dst;
};

/*
 * Argument structure used by multicast routing daemon to get src-grp
 * packet counts
 */
struct sioc_sg_req6 {
	struct sockaddr_in6 src;
	struct sockaddr_in6 grp;
	u_quad_t pktcnt;
	u_quad_t bytecnt;
	u_quad_t wrong_if;
};

/*
 * Argument structure used by mrouted to get mif pkt counts
 */
struct sioc_mif_req6 {
	mifi_t mifi;		/* mif number				*/
	u_quad_t icount;	/* Input packet count on mif		*/
	u_quad_t ocount;	/* Output packet count on mif		*/
	u_quad_t ibytes;	/* Input byte count on mif		*/
	u_quad_t obytes;	/* Output byte count on mif		*/
};

/*
 * Structure to export 'struct mif6' to userland via sysctl.
 */
struct mif6_sctl {
	u_char		m6_flags;	/* MIFF_ flags defined above         */
	u_int		m6_rate_limit;	/* max rate			     */
	struct in6_addr	m6_lcl_addr;	/* local interface address           */
	uint32_t	m6_ifp;		/* interface index	             */
	u_quad_t	m6_pkt_in;	/* # pkts in on interface            */
	u_quad_t	m6_pkt_out;	/* # pkts out on interface           */
	u_quad_t	m6_bytes_in;	/* # bytes in on interface	     */
	u_quad_t	m6_bytes_out;	/* # bytes out on interface	     */
};

#if defined(_KERNEL) || defined(KERNEL)
/*
 * The kernel's multicast-interface structure.
 */
struct mif6 {
        u_char		m6_flags;	/* MIFF_ flags defined above         */
	u_int		m6_rate_limit;	/* max rate			     */
	struct in6_addr	m6_lcl_addr;	/* local interface address           */
	struct ifnet    *m6_ifp;	/* pointer to interface              */
	u_quad_t	m6_pkt_in;	/* # pkts in on interface            */
	u_quad_t	m6_pkt_out;	/* # pkts out on interface           */
	u_quad_t	m6_bytes_in;	/* # bytes in on interface	     */
	u_quad_t	m6_bytes_out;	/* # bytes out on interface	     */
#ifdef notyet
	u_int		m6_rsvp_on;	/* RSVP listening on this vif */
	struct socket   *m6_rsvpd;	/* RSVP daemon socket */
#endif
};

/*
 * The kernel's multicast forwarding cache entry structure
 */
struct mf6c {
	struct sockaddr_in6  mf6c_origin;	/* IPv6 origin of mcasts     */
	struct sockaddr_in6  mf6c_mcastgrp;	/* multicast group associated*/
	mifi_t		 mf6c_parent;		/* incoming IF               */
	struct if_set	 mf6c_ifset;		/* set of outgoing IFs */

	u_quad_t	mf6c_pkt_cnt;		/* pkt count for src-grp     */
	u_quad_t	mf6c_byte_cnt;		/* byte count for src-grp    */
	u_quad_t	mf6c_wrong_if;		/* wrong if for src-grp	     */
	int		mf6c_expire;		/* time to clean entry up    */
	struct timeval  mf6c_last_assert;	/* last time I sent an assert*/
	struct rtdetq  *mf6c_stall;		/* pkts waiting for route */
	struct mf6c    *mf6c_next;		/* hash table linkage */
};

#define MF6C_INCOMPLETE_PARENT ((mifi_t)-1)

/*
 * Argument structure used for pkt info. while upcall is made
 */
#ifndef _NETINET_IP_MROUTE_H_
struct rtdetq {		/* XXX: rtdetq is also defined in ip_mroute.h */
    struct mbuf		*m;		/* A copy of the packet		    */
    struct ifnet	*ifp;		/* Interface pkt came in on	    */
#ifdef UPCALL_TIMING
    struct timeval	t;		/* Timestamp */
#endif /* UPCALL_TIMING */
    struct rtdetq	*next;
};
#endif /* _NETINET_IP_MROUTE_H_ */

#define MF6CTBLSIZ	256
#if (MF6CTBLSIZ & (MF6CTBLSIZ - 1)) == 0	  /* from sys:route.h */
#define MF6CHASHMOD(h)	((h) & (MF6CTBLSIZ - 1))
#else
#define MF6CHASHMOD(h)	((h) % MF6CTBLSIZ)
#endif

#define MAX_UPQ6	4		/* max. no of pkts in upcall Q */

extern int	(*ip6_mrouter_set)(struct socket *so, struct sockopt *sopt);
extern int	(*ip6_mrouter_get)(struct socket *so, struct sockopt *sopt);
extern int	(*ip6_mrouter_done)(void);
extern int	(*mrt6_ioctl)(u_long, caddr_t);
#endif /* _KERNEL */

#endif /* !_NETINET6_IP6_MROUTE_H_ */
