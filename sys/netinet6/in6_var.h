/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (C) 1995, 1996, 1997, and 1998 WIDE Project.
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
 *	$KAME: in6_var.h,v 1.56 2001/03/29 05:34:31 itojun Exp $
 */

/*-
 * Copyright (c) 1985, 1986, 1993
 *	The Regents of the University of California.  All rights reserved.
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
 *	@(#)in_var.h	8.1 (Berkeley) 6/10/93
 * $FreeBSD$
 */

#ifndef _NETINET6_IN6_VAR_H_
#define _NETINET6_IN6_VAR_H_

#include <sys/tree.h>
#include <sys/counter.h>

#ifdef _KERNEL
#include <sys/fnv_hash.h>
#include <sys/libkern.h>
#endif

/*
 * Interface address, Internet version.  One of these structures
 * is allocated for each interface with an Internet address.
 * The ifaddr structure contains the protocol-independent part
 * of the structure and is assumed to be first.
 */

/*
 * pltime/vltime are just for future reference (required to implements 2
 * hour rule for hosts).  they should never be modified by nd6_timeout or
 * anywhere else.
 *	userland -> kernel: accept pltime/vltime
 *	kernel -> userland: throw up everything
 *	in kernel: modify preferred/expire only
 */
struct in6_addrlifetime {
	time_t ia6t_expire;	/* valid lifetime expiration time */
	time_t ia6t_preferred;	/* preferred lifetime expiration time */
	u_int32_t ia6t_vltime;	/* valid lifetime */
	u_int32_t ia6t_pltime;	/* prefix lifetime */
};

struct nd_ifinfo;
struct scope6_id;
struct lltable;
struct mld_ifsoftc;
struct in6_multi;

struct in6_ifextra {
	counter_u64_t *in6_ifstat;
	counter_u64_t *icmp6_ifstat;
	struct nd_ifinfo *nd_ifinfo;
	struct scope6_id *scope6_id;
	struct lltable *lltable;
	struct mld_ifsoftc *mld_ifinfo;
};

#define	LLTABLE6(ifp)	(((struct in6_ifextra *)(ifp)->if_afdata[AF_INET6])->lltable)

#ifdef _KERNEL

SLIST_HEAD(in6_multi_head, in6_multi);
MALLOC_DECLARE(M_IP6MADDR);

struct	in6_ifaddr {
	struct	ifaddr ia_ifa;		/* protocol-independent info */
#define	ia_ifp		ia_ifa.ifa_ifp
#define ia_flags	ia_ifa.ifa_flags
	struct	sockaddr_in6 ia_addr;	/* interface address */
	struct	sockaddr_in6 ia_net;	/* network number of interface */
	struct	sockaddr_in6 ia_dstaddr; /* space for destination addr */
	struct	sockaddr_in6 ia_prefixmask; /* prefix mask */
	u_int32_t ia_plen;		/* prefix length */
	CK_STAILQ_ENTRY(in6_ifaddr)	ia_link;	/* list of IPv6 addresses */
	int	ia6_flags;

	struct in6_addrlifetime ia6_lifetime;
	time_t	ia6_createtime; /* the creation time of this address, which is
				 * currently used for temporary addresses only.
				 */
	time_t	ia6_updatetime;

	/* back pointer to the ND prefix (for autoconfigured addresses only) */
	struct nd_prefix *ia6_ndpr;

	/* multicast addresses joined from the kernel */
	LIST_HEAD(, in6_multi_mship) ia6_memberships;
	/* entry in bucket of inet6 addresses */
	CK_LIST_ENTRY(in6_ifaddr) ia6_hash;
};

/* List of in6_ifaddr's. */
CK_STAILQ_HEAD(in6_ifaddrhead, in6_ifaddr);
CK_LIST_HEAD(in6_ifaddrlisthead, in6_ifaddr);
#endif	/* _KERNEL */

/* control structure to manage address selection policy */
struct in6_addrpolicy {
	struct sockaddr_in6 addr; /* prefix address */
	struct sockaddr_in6 addrmask; /* prefix mask */
	int preced;		/* precedence */
	int label;		/* matching label */
	u_quad_t use;		/* statistics */
};

/*
 * IPv6 interface statistics, as defined in RFC2465 Ipv6IfStatsEntry (p12).
 */
struct in6_ifstat {
	uint64_t ifs6_in_receive;	/* # of total input datagram */
	uint64_t ifs6_in_hdrerr;	/* # of datagrams with invalid hdr */
	uint64_t ifs6_in_toobig;	/* # of datagrams exceeded MTU */
	uint64_t ifs6_in_noroute;	/* # of datagrams with no route */
	uint64_t ifs6_in_addrerr;	/* # of datagrams with invalid dst */
	uint64_t ifs6_in_protounknown;	/* # of datagrams with unknown proto */
					/* NOTE: increment on final dst if */
	uint64_t ifs6_in_truncated;	/* # of truncated datagrams */
	uint64_t ifs6_in_discard;	/* # of discarded datagrams */
					/* NOTE: fragment timeout is not here */
	uint64_t ifs6_in_deliver;	/* # of datagrams delivered to ULP */
					/* NOTE: increment on final dst if */
	uint64_t ifs6_out_forward;	/* # of datagrams forwarded */
					/* NOTE: increment on outgoing if */
	uint64_t ifs6_out_request;	/* # of outgoing datagrams from ULP */
					/* NOTE: does not include forwrads */
	uint64_t ifs6_out_discard;	/* # of discarded datagrams */
	uint64_t ifs6_out_fragok;	/* # of datagrams fragmented */
	uint64_t ifs6_out_fragfail;	/* # of datagrams failed on fragment */
	uint64_t ifs6_out_fragcreat;	/* # of fragment datagrams */
					/* NOTE: this is # after fragment */
	uint64_t ifs6_reass_reqd;	/* # of incoming fragmented packets */
					/* NOTE: increment on final dst if */
	uint64_t ifs6_reass_ok;		/* # of reassembled packets */
					/* NOTE: this is # after reass */
					/* NOTE: increment on final dst if */
	uint64_t ifs6_reass_fail;	/* # of reass failures */
					/* NOTE: may not be packet count */
					/* NOTE: increment on final dst if */
	uint64_t ifs6_in_mcast;		/* # of inbound multicast datagrams */
	uint64_t ifs6_out_mcast;	/* # of outbound multicast datagrams */
};

/*
 * ICMPv6 interface statistics, as defined in RFC2466 Ipv6IfIcmpEntry.
 * XXX: I'm not sure if this file is the right place for this structure...
 */
struct icmp6_ifstat {
	/*
	 * Input statistics
	 */
	/* ipv6IfIcmpInMsgs, total # of input messages */
	uint64_t ifs6_in_msg;
	/* ipv6IfIcmpInErrors, # of input error messages */
	uint64_t ifs6_in_error;
	/* ipv6IfIcmpInDestUnreachs, # of input dest unreach errors */
	uint64_t ifs6_in_dstunreach;
	/* ipv6IfIcmpInAdminProhibs, # of input administratively prohibited errs */
	uint64_t ifs6_in_adminprohib;
	/* ipv6IfIcmpInTimeExcds, # of input time exceeded errors */
	uint64_t ifs6_in_timeexceed;
	/* ipv6IfIcmpInParmProblems, # of input parameter problem errors */
	uint64_t ifs6_in_paramprob;
	/* ipv6IfIcmpInPktTooBigs, # of input packet too big errors */
	uint64_t ifs6_in_pkttoobig;
	/* ipv6IfIcmpInEchos, # of input echo requests */
	uint64_t ifs6_in_echo;
	/* ipv6IfIcmpInEchoReplies, # of input echo replies */
	uint64_t ifs6_in_echoreply;
	/* ipv6IfIcmpInRouterSolicits, # of input router solicitations */
	uint64_t ifs6_in_routersolicit;
	/* ipv6IfIcmpInRouterAdvertisements, # of input router advertisements */
	uint64_t ifs6_in_routeradvert;
	/* ipv6IfIcmpInNeighborSolicits, # of input neighbor solicitations */
	uint64_t ifs6_in_neighborsolicit;
	/* ipv6IfIcmpInNeighborAdvertisements, # of input neighbor advertisements */
	uint64_t ifs6_in_neighboradvert;
	/* ipv6IfIcmpInRedirects, # of input redirects */
	uint64_t ifs6_in_redirect;
	/* ipv6IfIcmpInGroupMembQueries, # of input MLD queries */
	uint64_t ifs6_in_mldquery;
	/* ipv6IfIcmpInGroupMembResponses, # of input MLD reports */
	uint64_t ifs6_in_mldreport;
	/* ipv6IfIcmpInGroupMembReductions, # of input MLD done */
	uint64_t ifs6_in_mlddone;

	/*
	 * Output statistics. We should solve unresolved routing problem...
	 */
	/* ipv6IfIcmpOutMsgs, total # of output messages */
	uint64_t ifs6_out_msg;
	/* ipv6IfIcmpOutErrors, # of output error messages */
	uint64_t ifs6_out_error;
	/* ipv6IfIcmpOutDestUnreachs, # of output dest unreach errors */
	uint64_t ifs6_out_dstunreach;
	/* ipv6IfIcmpOutAdminProhibs, # of output administratively prohibited errs */
	uint64_t ifs6_out_adminprohib;
	/* ipv6IfIcmpOutTimeExcds, # of output time exceeded errors */
	uint64_t ifs6_out_timeexceed;
	/* ipv6IfIcmpOutParmProblems, # of output parameter problem errors */
	uint64_t ifs6_out_paramprob;
	/* ipv6IfIcmpOutPktTooBigs, # of output packet too big errors */
	uint64_t ifs6_out_pkttoobig;
	/* ipv6IfIcmpOutEchos, # of output echo requests */
	uint64_t ifs6_out_echo;
	/* ipv6IfIcmpOutEchoReplies, # of output echo replies */
	uint64_t ifs6_out_echoreply;
	/* ipv6IfIcmpOutRouterSolicits, # of output router solicitations */
	uint64_t ifs6_out_routersolicit;
	/* ipv6IfIcmpOutRouterAdvertisements, # of output router advertisements */
	uint64_t ifs6_out_routeradvert;
	/* ipv6IfIcmpOutNeighborSolicits, # of output neighbor solicitations */
	uint64_t ifs6_out_neighborsolicit;
	/* ipv6IfIcmpOutNeighborAdvertisements, # of output neighbor advertisements */
	uint64_t ifs6_out_neighboradvert;
	/* ipv6IfIcmpOutRedirects, # of output redirects */
	uint64_t ifs6_out_redirect;
	/* ipv6IfIcmpOutGroupMembQueries, # of output MLD queries */
	uint64_t ifs6_out_mldquery;
	/* ipv6IfIcmpOutGroupMembResponses, # of output MLD reports */
	uint64_t ifs6_out_mldreport;
	/* ipv6IfIcmpOutGroupMembReductions, # of output MLD done */
	uint64_t ifs6_out_mlddone;
};

struct	in6_ifreq {
	char	ifr_name[IFNAMSIZ];
	union {
		struct	sockaddr_in6 ifru_addr;
		struct	sockaddr_in6 ifru_dstaddr;
		int	ifru_flags;
		int	ifru_flags6;
		int	ifru_metric;
		caddr_t	ifru_data;
		struct in6_addrlifetime ifru_lifetime;
		struct in6_ifstat ifru_stat;
		struct icmp6_ifstat ifru_icmp6stat;
		u_int32_t ifru_scope_id[16];
	} ifr_ifru;
};

struct	in6_aliasreq {
	char	ifra_name[IFNAMSIZ];
	struct	sockaddr_in6 ifra_addr;
	struct	sockaddr_in6 ifra_dstaddr;
	struct	sockaddr_in6 ifra_prefixmask;
	int	ifra_flags;
	struct in6_addrlifetime ifra_lifetime;
	int	ifra_vhid;
};

/* pre-10.x compat */
struct	oin6_aliasreq {
	char	ifra_name[IFNAMSIZ];
	struct	sockaddr_in6 ifra_addr;
	struct	sockaddr_in6 ifra_dstaddr;
	struct	sockaddr_in6 ifra_prefixmask;
	int	ifra_flags;
	struct in6_addrlifetime ifra_lifetime;
};

/* prefix type macro */
#define IN6_PREFIX_ND	1
#define IN6_PREFIX_RR	2

/*
 * prefix related flags passed between kernel(NDP related part) and
 * user land command(ifconfig) and daemon(rtadvd).
 */
struct in6_prflags {
	struct prf_ra {
		u_char onlink : 1;
		u_char autonomous : 1;
		u_char reserved : 6;
	} prf_ra;
	u_char prf_reserved1;
	u_short prf_reserved2;
	/* want to put this on 4byte offset */
	struct prf_rr {
		u_char decrvalid : 1;
		u_char decrprefd : 1;
		u_char reserved : 6;
	} prf_rr;
	u_char prf_reserved3;
	u_short prf_reserved4;
};

struct  in6_prefixreq {
	char	ipr_name[IFNAMSIZ];
	u_char	ipr_origin;
	u_char	ipr_plen;
	u_int32_t ipr_vltime;
	u_int32_t ipr_pltime;
	struct in6_prflags ipr_flags;
	struct	sockaddr_in6 ipr_prefix;
};

#define PR_ORIG_RA	0
#define PR_ORIG_RR	1
#define PR_ORIG_STATIC	2
#define PR_ORIG_KERNEL	3

#define ipr_raf_onlink		ipr_flags.prf_ra.onlink
#define ipr_raf_auto		ipr_flags.prf_ra.autonomous

#define ipr_statef_onlink	ipr_flags.prf_state.onlink

#define ipr_rrf_decrvalid	ipr_flags.prf_rr.decrvalid
#define ipr_rrf_decrprefd	ipr_flags.prf_rr.decrprefd

struct	in6_rrenumreq {
	char	irr_name[IFNAMSIZ];
	u_char	irr_origin;
	u_char	irr_m_len;	/* match len for matchprefix */
	u_char	irr_m_minlen;	/* minlen for matching prefix */
	u_char	irr_m_maxlen;	/* maxlen for matching prefix */
	u_char	irr_u_uselen;	/* uselen for adding prefix */
	u_char	irr_u_keeplen;	/* keeplen from matching prefix */
	struct irr_raflagmask {
		u_char onlink : 1;
		u_char autonomous : 1;
		u_char reserved : 6;
	} irr_raflagmask;
	u_int32_t irr_vltime;
	u_int32_t irr_pltime;
	struct in6_prflags irr_flags;
	struct	sockaddr_in6 irr_matchprefix;
	struct	sockaddr_in6 irr_useprefix;
};

#define irr_raf_mask_onlink	irr_raflagmask.onlink
#define irr_raf_mask_auto	irr_raflagmask.autonomous
#define irr_raf_mask_reserved	irr_raflagmask.reserved

#define irr_raf_onlink		irr_flags.prf_ra.onlink
#define irr_raf_auto		irr_flags.prf_ra.autonomous

#define irr_statef_onlink	irr_flags.prf_state.onlink

#define irr_rrf			irr_flags.prf_rr
#define irr_rrf_decrvalid	irr_flags.prf_rr.decrvalid
#define irr_rrf_decrprefd	irr_flags.prf_rr.decrprefd

/*
 * Given a pointer to an in6_ifaddr (ifaddr),
 * return a pointer to the addr as a sockaddr_in6
 */
#define IA6_IN6(ia)	(&((ia)->ia_addr.sin6_addr))
#define IA6_DSTIN6(ia)	(&((ia)->ia_dstaddr.sin6_addr))
#define IA6_MASKIN6(ia)	(&((ia)->ia_prefixmask.sin6_addr))
#define IA6_SIN6(ia)	(&((ia)->ia_addr))
#define IA6_DSTSIN6(ia)	(&((ia)->ia_dstaddr))
#define IFA_IN6(x)	(&((struct sockaddr_in6 *)((x)->ifa_addr))->sin6_addr)
#define IFA_DSTIN6(x)	(&((struct sockaddr_in6 *)((x)->ifa_dstaddr))->sin6_addr)

#define IFPR_IN6(x)	(&((struct sockaddr_in6 *)((x)->ifpr_prefix))->sin6_addr)

#ifdef _KERNEL
#define IN6_ARE_MASKED_ADDR_EQUAL(d, a, m)	(	\
	(((d)->s6_addr32[0] ^ (a)->s6_addr32[0]) & (m)->s6_addr32[0]) == 0 && \
	(((d)->s6_addr32[1] ^ (a)->s6_addr32[1]) & (m)->s6_addr32[1]) == 0 && \
	(((d)->s6_addr32[2] ^ (a)->s6_addr32[2]) & (m)->s6_addr32[2]) == 0 && \
	(((d)->s6_addr32[3] ^ (a)->s6_addr32[3]) & (m)->s6_addr32[3]) == 0 )
#define IN6_MASK_ADDR(a, m)	do { \
	(a)->s6_addr32[0] &= (m)->s6_addr32[0]; \
	(a)->s6_addr32[1] &= (m)->s6_addr32[1]; \
	(a)->s6_addr32[2] &= (m)->s6_addr32[2]; \
	(a)->s6_addr32[3] &= (m)->s6_addr32[3]; \
} while (0)
#endif

#define SIOCSIFADDR_IN6		 _IOW('i', 12, struct in6_ifreq)
#define SIOCGIFADDR_IN6		_IOWR('i', 33, struct in6_ifreq)

#ifdef _KERNEL
/*
 * SIOCSxxx ioctls should be unused (see comments in in6.c), but
 * we do not shift numbers for binary compatibility.
 */
#define SIOCSIFDSTADDR_IN6	 _IOW('i', 14, struct in6_ifreq)
#define SIOCSIFNETMASK_IN6	 _IOW('i', 22, struct in6_ifreq)
#endif

#define SIOCGIFDSTADDR_IN6	_IOWR('i', 34, struct in6_ifreq)
#define SIOCGIFNETMASK_IN6	_IOWR('i', 37, struct in6_ifreq)

#define SIOCDIFADDR_IN6		 _IOW('i', 25, struct in6_ifreq)
#define OSIOCAIFADDR_IN6	 _IOW('i', 26, struct oin6_aliasreq)
#define SIOCAIFADDR_IN6		 _IOW('i', 27, struct in6_aliasreq)

#define SIOCSIFPHYADDR_IN6       _IOW('i', 70, struct in6_aliasreq)
#define	SIOCGIFPSRCADDR_IN6	_IOWR('i', 71, struct in6_ifreq)
#define	SIOCGIFPDSTADDR_IN6	_IOWR('i', 72, struct in6_ifreq)

#define SIOCGIFAFLAG_IN6	_IOWR('i', 73, struct in6_ifreq)

#ifdef _KERNEL
#define OSIOCGIFINFO_IN6	_IOWR('i', 76, struct in6_ondireq)
#endif
#define SIOCGIFINFO_IN6		_IOWR('i', 108, struct in6_ndireq)
#define SIOCSIFINFO_IN6		_IOWR('i', 109, struct in6_ndireq)
#define SIOCSNDFLUSH_IN6	_IOWR('i', 77, struct in6_ifreq)
#define SIOCGNBRINFO_IN6	_IOWR('i', 78, struct in6_nbrinfo)
#define SIOCSPFXFLUSH_IN6	_IOWR('i', 79, struct in6_ifreq)
#define SIOCSRTRFLUSH_IN6	_IOWR('i', 80, struct in6_ifreq)

#define SIOCGIFALIFETIME_IN6	_IOWR('i', 81, struct in6_ifreq)
#define SIOCGIFSTAT_IN6		_IOWR('i', 83, struct in6_ifreq)
#define SIOCGIFSTAT_ICMP6	_IOWR('i', 84, struct in6_ifreq)

#define SIOCSDEFIFACE_IN6	_IOWR('i', 85, struct in6_ndifreq)
#define SIOCGDEFIFACE_IN6	_IOWR('i', 86, struct in6_ndifreq)

#define SIOCSIFINFO_FLAGS	_IOWR('i', 87, struct in6_ndireq) /* XXX */

#define SIOCSSCOPE6		_IOW('i', 88, struct in6_ifreq)
#define SIOCGSCOPE6		_IOWR('i', 89, struct in6_ifreq)
#define SIOCGSCOPE6DEF		_IOWR('i', 90, struct in6_ifreq)

#define SIOCSIFPREFIX_IN6	_IOW('i', 100, struct in6_prefixreq) /* set */
#define SIOCGIFPREFIX_IN6	_IOWR('i', 101, struct in6_prefixreq) /* get */
#define SIOCDIFPREFIX_IN6	_IOW('i', 102, struct in6_prefixreq) /* del */
#define SIOCAIFPREFIX_IN6	_IOW('i', 103, struct in6_rrenumreq) /* add */
#define SIOCCIFPREFIX_IN6	_IOW('i', 104, \
				     struct in6_rrenumreq) /* change */
#define SIOCSGIFPREFIX_IN6	_IOW('i', 105, \
				     struct in6_rrenumreq) /* set global */

#define SIOCGETSGCNT_IN6	_IOWR('u', 106, \
				      struct sioc_sg_req6) /* get s,g pkt cnt */
#define SIOCGETMIFCNT_IN6	_IOWR('u', 107, \
				      struct sioc_mif_req6) /* get pkt cnt per if */

#define SIOCAADDRCTL_POLICY	_IOW('u', 108, struct in6_addrpolicy)
#define SIOCDADDRCTL_POLICY	_IOW('u', 109, struct in6_addrpolicy)

#define IN6_IFF_ANYCAST		0x01	/* anycast address */
#define IN6_IFF_TENTATIVE	0x02	/* tentative address */
#define IN6_IFF_DUPLICATED	0x04	/* DAD detected duplicate */
#define IN6_IFF_DETACHED	0x08	/* may be detached from the link */
#define IN6_IFF_DEPRECATED	0x10	/* deprecated address */
#define IN6_IFF_NODAD		0x20	/* don't perform DAD on this address
					 * (obsolete)
					 */
#define IN6_IFF_AUTOCONF	0x40	/* autoconfigurable address. */
#define IN6_IFF_TEMPORARY	0x80	/* temporary (anonymous) address. */
#define	IN6_IFF_PREFER_SOURCE	0x0100	/* preferred address for SAS */

/* do not input/output */
#define IN6_IFF_NOTREADY (IN6_IFF_TENTATIVE|IN6_IFF_DUPLICATED)

#ifdef _KERNEL
#define IN6_ARE_SCOPE_CMP(a,b) ((a)-(b))
#define IN6_ARE_SCOPE_EQUAL(a,b) ((a)==(b))
#endif

#ifdef _KERNEL
VNET_DECLARE(struct in6_ifaddrhead, in6_ifaddrhead);
VNET_DECLARE(struct in6_ifaddrlisthead *, in6_ifaddrhashtbl);
VNET_DECLARE(u_long, in6_ifaddrhmask);
#define	V_in6_ifaddrhead		VNET(in6_ifaddrhead)
#define	V_in6_ifaddrhashtbl		VNET(in6_ifaddrhashtbl)
#define	V_in6_ifaddrhmask		VNET(in6_ifaddrhmask)

#define	IN6ADDR_NHASH_LOG2		8
#define	IN6ADDR_NHASH			(1 << IN6ADDR_NHASH_LOG2)
#define	IN6ADDR_HASHVAL(x)		(in6_addrhash(x))
#define	IN6ADDR_HASH(x) \
    (&V_in6_ifaddrhashtbl[IN6ADDR_HASHVAL(x) & V_in6_ifaddrhmask])

static __inline uint32_t
in6_addrhash(const struct in6_addr *in6)
{
	uint32_t x;

	x = in6->s6_addr32[0] ^ in6->s6_addr32[1] ^ in6->s6_addr32[2] ^
	    in6->s6_addr32[3];
	return (fnv_32_buf(&x, sizeof(x), FNV1_32_INIT));
}

extern struct rmlock in6_ifaddr_lock;
#define	IN6_IFADDR_LOCK_ASSERT()	rm_assert(&in6_ifaddr_lock, RA_LOCKED)
#define	IN6_IFADDR_RLOCK(t)		rm_rlock(&in6_ifaddr_lock, (t))
#define	IN6_IFADDR_RLOCK_ASSERT()	rm_assert(&in6_ifaddr_lock, RA_RLOCKED)
#define	IN6_IFADDR_RUNLOCK(t)		rm_runlock(&in6_ifaddr_lock, (t))
#define	IN6_IFADDR_WLOCK()		rm_wlock(&in6_ifaddr_lock)
#define	IN6_IFADDR_WLOCK_ASSERT()	rm_assert(&in6_ifaddr_lock, RA_WLOCKED)
#define	IN6_IFADDR_WUNLOCK()		rm_wunlock(&in6_ifaddr_lock)

#define in6_ifstat_inc(ifp, tag) \
do {								\
	if (ifp)						\
		counter_u64_add(((struct in6_ifextra *)		\
		    ((ifp)->if_afdata[AF_INET6]))->in6_ifstat[	\
		    offsetof(struct in6_ifstat, tag) / sizeof(uint64_t)], 1);\
} while (/*CONSTCOND*/ 0)

extern u_char inet6ctlerrmap[];
VNET_DECLARE(unsigned long, in6_maxmtu);
#define	V_in6_maxmtu			VNET(in6_maxmtu)
#endif /* _KERNEL */

/*
 * IPv6 multicast MLD-layer source entry.
 */
struct ip6_msource {
	RB_ENTRY(ip6_msource)	im6s_link;	/* RB tree links */
	struct in6_addr		im6s_addr;
	struct im6s_st {
		uint16_t	ex;		/* # of exclusive members */
		uint16_t	in;		/* # of inclusive members */
	}			im6s_st[2];	/* state at t0, t1 */
	uint8_t			im6s_stp;	/* pending query */
};
RB_HEAD(ip6_msource_tree, ip6_msource);

/*
 * IPv6 multicast PCB-layer source entry.
 *
 * NOTE: overlapping use of struct ip6_msource fields at start.
 */
struct in6_msource {
	RB_ENTRY(ip6_msource)	im6s_link;	/* Common field */
	struct in6_addr		im6s_addr;	/* Common field */
	uint8_t			im6sl_st[2];	/* state before/at commit */
};

#ifdef _KERNEL
/*
 * IPv6 source tree comparison function.
 *
 * An ordered predicate is necessary; bcmp() is not documented to return
 * an indication of order, memcmp() is, and is an ISO C99 requirement.
 */
static __inline int
ip6_msource_cmp(const struct ip6_msource *a, const struct ip6_msource *b)
{

	return (memcmp(&a->im6s_addr, &b->im6s_addr, sizeof(struct in6_addr)));
}
RB_PROTOTYPE(ip6_msource_tree, ip6_msource, im6s_link, ip6_msource_cmp);

/*
 * IPv6 multicast PCB-layer group filter descriptor.
 */
struct in6_mfilter {
	struct ip6_msource_tree	im6f_sources; /* source list for (S,G) */
	u_long			im6f_nsrc;    /* # of source entries */
	uint8_t			im6f_st[2];   /* state before/at commit */
};

/*
 * Legacy KAME IPv6 multicast membership descriptor.
 */
struct in6_multi_mship {
	struct	in6_multi *i6mm_maddr;
	LIST_ENTRY(in6_multi_mship) i6mm_chain;
};

/*
 * IPv6 group descriptor.
 *
 * For every entry on an ifnet's if_multiaddrs list which represents
 * an IP multicast group, there is one of these structures.
 *
 * If any source filters are present, then a node will exist in the RB-tree
 * to permit fast lookup by source whenever an operation takes place.
 * This permits pre-order traversal when we issue reports.
 * Source filter trees are kept separately from the socket layer to
 * greatly simplify locking.
 *
 * When MLDv2 is active, in6m_timer is the response to group query timer.
 * The state-change timer in6m_sctimer is separate; whenever state changes
 * for the group the state change record is generated and transmitted,
 * and kept if retransmissions are necessary.
 *
 * FUTURE: in6m_link is now only used when groups are being purged
 * on a detaching ifnet. It could be demoted to a SLIST_ENTRY, but
 * because it is at the very start of the struct, we can't do this
 * w/o breaking the ABI for ifmcstat.
 */
struct in6_multi {
	struct	in6_addr in6m_addr;	/* IPv6 multicast address */
	struct	ifnet *in6m_ifp;	/* back pointer to ifnet */
	struct	ifmultiaddr *in6m_ifma;	/* back pointer to ifmultiaddr */
	u_int	in6m_refcount;		/* reference count */
	u_int	in6m_state;		/* state of the membership */
	u_int	in6m_timer;		/* MLD6 listener report timer */

	/* New fields for MLDv2 follow. */
	struct mld_ifsoftc	*in6m_mli;	/* MLD info */
	SLIST_ENTRY(in6_multi)	 in6m_nrele;	/* to-be-released by MLD */
	SLIST_ENTRY(in6_multi)	 in6m_defer;	/* deferred MLDv1 */
	struct ip6_msource_tree	 in6m_srcs;	/* tree of sources */
	u_long			 in6m_nsrc;	/* # of tree entries */

	struct mbufq		 in6m_scq;	/* queue of pending
						 * state-change packets */
	struct timeval		 in6m_lastgsrtv;	/* last G-S-R query */
	uint16_t		 in6m_sctimer;	/* state-change timer */
	uint16_t		 in6m_scrv;	/* state-change rexmit count */

	/*
	 * SSM state counters which track state at T0 (the time the last
	 * state-change report's RV timer went to zero) and T1
	 * (time of pending report, i.e. now).
	 * Used for computing MLDv2 state-change reports. Several refcounts
	 * are maintained here to optimize for common use-cases.
	 */
	struct in6m_st {
		uint16_t	iss_fmode;	/* MLD filter mode */
		uint16_t	iss_asm;	/* # of ASM listeners */
		uint16_t	iss_ex;		/* # of exclusive members */
		uint16_t	iss_in;		/* # of inclusive members */
		uint16_t	iss_rec;	/* # of recorded sources */
	}			in6m_st[2];	/* state at t0, t1 */
};

void in6m_disconnect_locked(struct in6_multi_head *inmh, struct in6_multi *inm);

/*
 * Helper function to derive the filter mode on a source entry
 * from its internal counters. Predicates are:
 *  A source is only excluded if all listeners exclude it.
 *  A source is only included if no listeners exclude it,
 *  and at least one listener includes it.
 * May be used by ifmcstat(8).
 */
static __inline uint8_t
im6s_get_mode(const struct in6_multi *inm, const struct ip6_msource *ims,
    uint8_t t)
{

	t = !!t;
	if (inm->in6m_st[t].iss_ex > 0 &&
	    inm->in6m_st[t].iss_ex == ims->im6s_st[t].ex)
		return (MCAST_EXCLUDE);
	else if (ims->im6s_st[t].in > 0 && ims->im6s_st[t].ex == 0)
		return (MCAST_INCLUDE);
	return (MCAST_UNDEFINED);
}

/*
 * Lock macros for IPv6 layer multicast address lists.  IPv6 lock goes
 * before link layer multicast locks in the lock order.  In most cases,
 * consumers of IN_*_MULTI() macros should acquire the locks before
 * calling them; users of the in_{add,del}multi() functions should not.
 */
extern struct mtx in6_multi_list_mtx;
extern struct sx in6_multi_sx;

#define	IN6_MULTI_LIST_LOCK()		mtx_lock(&in6_multi_list_mtx)
#define	IN6_MULTI_LIST_UNLOCK()	mtx_unlock(&in6_multi_list_mtx)
#define	IN6_MULTI_LIST_LOCK_ASSERT()	mtx_assert(&in6_multi_list_mtx, MA_OWNED)
#define	IN6_MULTI_LIST_UNLOCK_ASSERT() mtx_assert(&in6_multi_list_mtx, MA_NOTOWNED)

#define	IN6_MULTI_LOCK()		sx_xlock(&in6_multi_sx)
#define	IN6_MULTI_UNLOCK()	sx_xunlock(&in6_multi_sx)
#define	IN6_MULTI_LOCK_ASSERT()	sx_assert(&in6_multi_sx, SA_XLOCKED)
#define	IN6_MULTI_UNLOCK_ASSERT() sx_assert(&in6_multi_sx, SA_XUNLOCKED)

/*
 * Get the in6_multi pointer from a ifmultiaddr.
 * Returns NULL if ifmultiaddr is no longer valid.
 */
static __inline struct in6_multi *
in6m_ifmultiaddr_get_inm(struct ifmultiaddr *ifma)
{

	NET_EPOCH_ASSERT();

	return ((ifma->ifma_addr->sa_family != AF_INET6 ||	
	    (ifma->ifma_flags & IFMA_F_ENQUEUED) == 0) ? NULL :
	    ifma->ifma_protospec);
}

/*
 * Look up an in6_multi record for an IPv6 multicast address
 * on the interface ifp.
 * If no record found, return NULL.
 */
static __inline struct in6_multi *
in6m_lookup_locked(struct ifnet *ifp, const struct in6_addr *mcaddr)
{
	struct ifmultiaddr *ifma;
	struct in6_multi *inm;

	CK_STAILQ_FOREACH(ifma, &ifp->if_multiaddrs, ifma_link) {
		inm = in6m_ifmultiaddr_get_inm(ifma);
		if (inm == NULL)
			continue;
		if (IN6_ARE_ADDR_EQUAL(&inm->in6m_addr, mcaddr))
			return (inm);
	}
	return (NULL);
}

/*
 * Wrapper for in6m_lookup_locked().
 *
 * SMPng: Assumes that neithr the IN6_MULTI_LOCK() or IF_ADDR_LOCK() are held.
 */
static __inline struct in6_multi *
in6m_lookup(struct ifnet *ifp, const struct in6_addr *mcaddr)
{
	struct epoch_tracker et;
	struct in6_multi *inm;

	IN6_MULTI_LIST_LOCK();
	NET_EPOCH_ENTER(et);
	inm = in6m_lookup_locked(ifp, mcaddr);
	NET_EPOCH_EXIT(et);
	IN6_MULTI_LIST_UNLOCK();

	return (inm);
}

/* Acquire an in6_multi record. */
static __inline void
in6m_acquire_locked(struct in6_multi *inm)
{

	IN6_MULTI_LIST_LOCK_ASSERT();
	++inm->in6m_refcount;
}

static __inline void
in6m_acquire(struct in6_multi *inm)
{
	IN6_MULTI_LIST_LOCK();
	in6m_acquire_locked(inm);
	IN6_MULTI_LIST_UNLOCK();
}

static __inline void
in6m_rele_locked(struct in6_multi_head *inmh, struct in6_multi *inm)
{
	KASSERT(inm->in6m_refcount > 0, ("refcount == %d inm: %p", inm->in6m_refcount, inm));
	IN6_MULTI_LIST_LOCK_ASSERT();

	if (--inm->in6m_refcount == 0) {
		MPASS(inm->in6m_ifp == NULL);
		inm->in6m_ifma->ifma_protospec = NULL;
		MPASS(inm->in6m_ifma->ifma_llifma == NULL);
		SLIST_INSERT_HEAD(inmh, inm, in6m_nrele);
	}
}

struct ip6_moptions;
struct sockopt;
struct inpcbinfo;

/* Multicast KPIs. */
int	im6o_mc_filter(const struct ip6_moptions *, const struct ifnet *,
	    const struct sockaddr *, const struct sockaddr *);
int in6_joingroup(struct ifnet *, const struct in6_addr *,
	    struct in6_mfilter *, struct in6_multi **, int);
int	in6_joingroup_locked(struct ifnet *, const struct in6_addr *,
	    struct in6_mfilter *, struct in6_multi **, int);
int	in6_leavegroup(struct in6_multi *, struct in6_mfilter *);
int	in6_leavegroup_locked(struct in6_multi *, struct in6_mfilter *);
void	in6m_clear_recorded(struct in6_multi *);
void	in6m_commit(struct in6_multi *);
void	in6m_print(const struct in6_multi *);
int	in6m_record_source(struct in6_multi *, const struct in6_addr *);
void	in6m_release_list_deferred(struct in6_multi_head *);
void	in6m_release_wait(void);
void	ip6_freemoptions(struct ip6_moptions *);
int	ip6_getmoptions(struct inpcb *, struct sockopt *);
int	ip6_setmoptions(struct inpcb *, struct sockopt *);

/* flags to in6_update_ifa */
#define IN6_IFAUPDATE_DADDELAY	0x1 /* first time to configure an address */

int	in6_mask2len(struct in6_addr *, u_char *);
int	in6_control(struct socket *, u_long, caddr_t, struct ifnet *,
	struct thread *);
int	in6_update_ifa(struct ifnet *, struct in6_aliasreq *,
	struct in6_ifaddr *, int);
void	in6_prepare_ifra(struct in6_aliasreq *, const struct in6_addr *,
	const struct in6_addr *);
void	in6_purgeaddr(struct ifaddr *);
int	in6if_do_dad(struct ifnet *);
void	in6_savemkludge(struct in6_ifaddr *);
void	*in6_domifattach(struct ifnet *);
void	in6_domifdetach(struct ifnet *, void *);
int	in6_domifmtu(struct ifnet *);
void	in6_setmaxmtu(void);
int	in6_if2idlen(struct ifnet *);
struct in6_ifaddr *in6ifa_ifpforlinklocal(struct ifnet *, int);
struct in6_ifaddr *in6ifa_ifpwithaddr(struct ifnet *, const struct in6_addr *);
struct in6_ifaddr *in6ifa_ifwithaddr(const struct in6_addr *, uint32_t);
struct in6_ifaddr *in6ifa_llaonifp(struct ifnet *);
int	in6_addr2zoneid(struct ifnet *, struct in6_addr *, u_int32_t *);
int	in6_matchlen(struct in6_addr *, struct in6_addr *);
int	in6_are_prefix_equal(struct in6_addr *, struct in6_addr *, int);
void	in6_prefixlen2mask(struct in6_addr *, int);
int	in6_prefix_ioctl(struct socket *, u_long, caddr_t,
	struct ifnet *);
int	in6_prefix_add_ifid(int, struct in6_ifaddr *);
void	in6_prefix_remove_ifid(int, struct in6_ifaddr *);
void	in6_purgeprefix(struct ifnet *);

int	in6_is_addr_deprecated(struct sockaddr_in6 *);
int	in6_src_ioctl(u_long, caddr_t);

void	in6_newaddrmsg(struct in6_ifaddr *, int);
/*
 * Extended API for IPv6 FIB support.
 */
struct mbuf *ip6_tryforward(struct mbuf *);
void	in6_rtredirect(struct sockaddr *, struct sockaddr *, struct sockaddr *,
	    int, struct sockaddr *, u_int);
int	in6_rtrequest(int, struct sockaddr *, struct sockaddr *,
	    struct sockaddr *, int, struct rtentry **, u_int);
void	in6_rtalloc(struct route_in6 *, u_int);
void	in6_rtalloc_ign(struct route_in6 *, u_long, u_int);
struct rtentry *in6_rtalloc1(struct sockaddr *, int, u_long, u_int);
#endif /* _KERNEL */

#endif /* _NETINET6_IN6_VAR_H_ */
