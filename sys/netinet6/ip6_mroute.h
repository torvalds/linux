/*	$OpenBSD: ip6_mroute.h,v 1.25 2025/05/19 04:54:04 jan Exp $	*/
/*	$KAME: ip6_mroute.h,v 1.17 2001/02/10 02:05:52 itojun Exp $	*/

/*
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
 */

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
#define MRT6_DONE		101	/* shut down forwarder */
#define MRT6_ADD_MIF		102	/* add multicast interface */
#define MRT6_DEL_MIF		103	/* delete multicast interface */
#define MRT6_ADD_MFC		104	/* insert forwarding cache entry */
#define MRT6_DEL_MFC		105	/* delete forwarding cache entry */
#define MRT6_INIT		108	/* initialize forwarder (mrt6msg) */

/*
 * Types and macros for handling bitmaps with one bit per multicast interface.
 */
typedef u_short mifi_t;		/* type of a mif index */
#define MAXMIFS		64

/*
 * Assume inclusion of sys/param.h or sys/select.h for howmany()
 */
#define	__NIFBITS	(sizeof(uint32_t) * 8)	/* bits per mask */
typedef	struct if_set {
	uint32_t	ifs_bits[howmany(256, __NIFBITS)];
} if_set;

#define	IF_SET(n, p) \
	((p)->ifs_bits[(n)/__NIFBITS] |= (1U << ((n) % __NIFBITS)))
#define	IF_CLR(n, p) \
	((p)->ifs_bits[(n)/__NIFBITS] &= ~(1U << ((n) % __NIFBITS)))
#define	IF_ISSET(n, p) \
	((p)->ifs_bits[(n)/__NIFBITS] & (1U << ((n) % __NIFBITS)))
#define	IF_COPY(f, t)	memcpy(t, f, sizeof(*(f)))
#define	IF_ZERO(p)	memset(p, 0, sizeof(*(p)))

/*
 * Argument structure for MRT6_ADD_IF.
 */
struct mif6ctl {
	mifi_t	    mif6c_mifi;		/* the index of the mif to be added  */
	u_char	    mif6c_flags;	/* MIFF_ flags defined below         */
	u_short	    mif6c_pifi;		/* the index of the physical IF */
#ifdef notyet
	u_int	    mif6c_rate_limit;    /* max rate			     */
#endif
};

#define	MIFF_REGISTER	0x1	/* mif represents a register end-point */

#define MF6C_INCOMPLETE_PARENT ((mifi_t)-1)

/*
 * Argument structure for MRT6_ADD_MFC and MRT6_DEL_MFC
 */
struct mf6cctl {
	struct sockaddr_in6 mf6cc_origin;	/* IPv6 origin of mcasts */
	struct sockaddr_in6 mf6cc_mcastgrp; /* multicast group associated */
	mifi_t		mf6cc_parent;	/* incoming ifindex */
	struct if_set	mf6cc_ifset;	/* set of forwarding ifs */
};

/* structure used to get all the mif entries via sysctl */
struct mif6info {
	struct in6_addr	m6_lcl_addr;	/* local interface address           */
	u_int16_t	m6_ifindex;	/* interface index                   */
	u_int64_t	m6_pkt_in;	/* # pkts in on interface            */
	u_int64_t	m6_pkt_out;	/* # pkts out on interface           */
	u_int64_t	m6_bytes_in;	/* # bytes in on interface	     */
	u_int64_t	m6_bytes_out;	/* # bytes out on interface	     */
	u_int		m6_rate_limit;	/* max rate			     */
	mifi_t		m6_mifi;
	u_char		m6_flags;	/* MIFF_ flags defined above         */
};

/* structure used to get all the mf6c entries via sysctl */
struct mf6cinfo {
	struct sockaddr_in6  mf6c_origin;	/* IPv6 origin of mcasts     */
	struct sockaddr_in6  mf6c_mcastgrp;	/* multicast group associated*/
	mifi_t		 mf6c_parent;		/* incoming IF               */
	struct if_set	 mf6c_ifset;		/* set of outgoing IFs */

	u_int64_t	mf6c_pkt_cnt;		/* pkt count for src-grp     */
	u_int64_t	mf6c_byte_cnt;		/* byte count for src-grp    */
	u_int64_t	mf6c_stall_cnt;		/* pkt-cnt waiting for route */
};

/*
 * The kernel's multicast routing statistics.
 */
struct mrt6stat {
	u_int64_t mrt6s_mfc_lookups;	/* # forw. cache hash table hits   */
	u_int64_t mrt6s_mfc_misses;	/* # forw. cache hash table misses */
	u_int64_t mrt6s_upcalls;	/* # calls to mrouted              */
	u_int64_t mrt6s_no_route;	/* no route for packet's origin    */
	u_int64_t mrt6s_bad_tunnel;	/* malformed tunnel options        */
	u_int64_t mrt6s_cant_tunnel;	/* no room for tunnel options      */
	u_int64_t mrt6s_wrong_if;	/* arrived on wrong interface	   */
	u_int64_t mrt6s_upq_ovflw;	/* upcall Q overflow		   */
	u_int64_t mrt6s_cache_cleanups;	/* # entries with no upcalls	   */
	u_int64_t mrt6s_drop_sel;	/* pkts dropped selectively        */
	u_int64_t mrt6s_q_overflow;	/* pkts dropped - Q overflow       */
	u_int64_t mrt6s_pkt2large;	/* pkts dropped - size > BKT SIZE  */
	u_int64_t mrt6s_upq_sockfull;	/* upcalls dropped - socket full   */
};

/*
 * Structure used to communicate from kernel to multicast router.
 * We'll overlay the structure onto an MLD header (not an IPv6 header
 * like igmpmsg{} used for IPv4 implementation). This is because this
 * structure will be passed via an IPv6 raw socket, on which an application
 * will only receive the payload i.e. the data after the IPv6 header and all
 * the extension headers. (see Section 3 of draft-ietf-ipngwg-2292bis-01)
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
	u_int64_t pktcnt;
	u_int64_t bytecnt;
	u_int64_t wrong_if;
};

/*
 * Argument structure used by mrouted to get mif pkt counts
 */
struct sioc_mif_req6 {
	mifi_t mifi;		/* mif number				*/
	u_int64_t icount;	/* Input packet count on mif		*/
	u_int64_t ocount;	/* Output packet count on mif		*/
	u_int64_t ibytes;	/* Input byte count on mif		*/
	u_int64_t obytes;	/* Output byte count on mif		*/
};

#if defined(_KERNEL)

enum mrt6stat_counters {
	mrt6s_mfc_lookups,
	mrt6s_mfc_misses,
	mrt6s_upcalls,
	mrt6s_no_route,
	mrt6s_bad_tunnel,
	mrt6s_cant_tunnel,
	mrt6s_wrong_if,
	mrt6s_upq_ovflw,
	mrt6s_cache_cleanups,
	mrt6s_drop_sel,
	mrt6s_q_overflow,
	mrt6s_pkt2large,
	mrt6s_upq_sockfull,
	mrt6s_ncounters
};

extern struct cpumem *mrt6counters;

static inline void
mrt6stat_inc(enum mrt6stat_counters c)
{
	counters_inc(mrt6counters, c);
}

/* How frequent should we look for expired entries (in seconds). */
#define	MCAST_EXPIRE_TIMEOUT	30

/*
 * The kernel's multicast-interface structure.
 */
struct mif6 {
	mifi_t		m6_mifi;	/* mif index			     */
	u_char		m6_flags;	/* MIFF_ flags defined above         */
	u_int		m6_rate_limit;	/* max rate			     */
#ifdef notyet
	struct tbf      *m6_tbf;	/* token bucket structure at intf.   */
#endif
	struct in6_addr	m6_lcl_addr;	/* local interface address           */
	u_int64_t	m6_pkt_in;	/* # pkts in on interface            */
	u_int64_t	m6_pkt_out;	/* # pkts out on interface           */
	u_int64_t	m6_bytes_in;	/* # bytes in on interface	     */
	u_int64_t	m6_bytes_out;	/* # bytes out on interface	     */
};

/*
 * The kernel's multicast forwarding cache entry structure
 */
struct mf6c {
	struct sockaddr_in6  mf6c_origin;	/* IPv6 origin of mcasts     */
	struct sockaddr_in6  mf6c_mcastgrp;	/* multicast group associated*/
	mifi_t		 mf6c_parent;		/* incoming IF               */

	u_int64_t	mf6c_pkt_cnt;		/* pkt count for src-grp     */
	u_int64_t	mf6c_byte_cnt;		/* byte count for src-grp    */
	u_int64_t	mf6c_wrong_if;		/* wrong if for src-grp	     */
	int		mf6c_expire;		/* time to clean entry up    */
};

int	ip6_mrouter_set(int, struct socket *, struct mbuf *);
int	ip6_mrouter_get(int, struct socket *, struct mbuf *);
int	ip6_mrouter_done(struct socket *);
void	ip6_mrouter_detach(struct ifnet *);
void	mrt6_init(void);
int	mrt6_ioctl(struct socket *, u_long, caddr_t);
int	mrt6_sysctl_mif(void *, size_t *);
int	mrt6_sysctl_mrt6stat(void *, size_t *, void *);
int	mrt6_sysctl_mfc(void *, size_t *);
#endif /* _KERNEL */

#endif /* !_NETINET6_IP6_MROUTE_H_ */
