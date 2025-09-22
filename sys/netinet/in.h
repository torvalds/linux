/*	$OpenBSD: in.h,v 1.149 2025/03/02 21:28:32 bluhm Exp $	*/
/*	$NetBSD: in.h,v 1.20 1996/02/13 23:41:47 christos Exp $	*/

/*
 * Copyright (c) 1982, 1986, 1990, 1993
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
 */

/*
 * Constants and structures defined by the internet system,
 * Per RFC 790, September 1981, and numerous additions.
 */

#ifndef _NETINET_IN_H_
#define	_NETINET_IN_H_

#include <sys/cdefs.h>

#ifndef _KERNEL
#include <sys/types.h>

/* <sys/_endian.h> is pulled in by <sys/types.h> */
#ifndef htons
#define htons(x)	__htobe16(x)
#define htonl(x)	__htobe32(x)
#define ntohs(x)	__htobe16(x)
#define ntohl(x)	__htobe32(x)
#endif

#endif /* _KERNEL */

#ifndef	_SA_FAMILY_T_DEFINED_
#define	_SA_FAMILY_T_DEFINED_
typedef	__sa_family_t	sa_family_t;	/* sockaddr address family type */
#endif /* _SA_FAMILY_T_DEFINED_ */

#ifndef _IN_TYPES_DEFINED_
#define _IN_TYPES_DEFINED_
typedef __in_addr_t	in_addr_t;	/* base type for internet address */
typedef __in_port_t	in_port_t;	/* IP port type */
#endif

/*
 * Protocols
 */
#define	IPPROTO_IP		0		/* dummy for IP */
#define	IPPROTO_HOPOPTS		IPPROTO_IP	/* Hop-by-hop option header */
#define	IPPROTO_ICMP		1		/* control message protocol */
#define	IPPROTO_IGMP		2		/* group mgmt protocol */
#define	IPPROTO_GGP		3		/* gateway^2 (deprecated) */
#define	IPPROTO_IPIP		4		/* IP inside IP */
#define	IPPROTO_IPV4		IPPROTO_IPIP	/* IP inside IP */
#define	IPPROTO_TCP		6		/* tcp */
#define	IPPROTO_EGP		8		/* exterior gateway protocol */
#define	IPPROTO_PUP		12		/* pup */
#define	IPPROTO_UDP		17		/* user datagram protocol */
#define	IPPROTO_IDP		22		/* xns idp */
#define	IPPROTO_TP		29		/* tp-4 w/ class negotiation */
#define	IPPROTO_IPV6		41		/* IPv6 in IPv6 */
#define	IPPROTO_ROUTING		43		/* Routing header */
#define	IPPROTO_FRAGMENT	44		/* Fragmentation/reassembly header */
#define	IPPROTO_RSVP		46		/* resource reservation */
#define	IPPROTO_GRE		47		/* GRE encap, RFCs 1701/1702 */
#define	IPPROTO_ESP		50		/* Encap. Security Payload */
#define	IPPROTO_AH		51		/* Authentication header */
#define	IPPROTO_MOBILE		55		/* IP Mobility, RFC 2004 */
#define	IPPROTO_ICMPV6		58		/* ICMP for IPv6 */
#define	IPPROTO_NONE		59		/* No next header */
#define	IPPROTO_DSTOPTS		60		/* Destination options header */
#define	IPPROTO_EON		80		/* ISO cnlp */
#define	IPPROTO_ETHERIP		97		/* Ethernet in IPv4 */
#define	IPPROTO_ENCAP		98		/* encapsulation header */
#define	IPPROTO_PIM		103		/* Protocol indep. multicast */
#define	IPPROTO_IPCOMP		108		/* IP Payload Comp. Protocol */
#define	IPPROTO_CARP		112		/* CARP */
#define	IPPROTO_SCTP		132		/* SCTP, RFC 4960 */
#define	IPPROTO_UDPLITE		136		/* UDP-Lite, RFC 3828 */
#define	IPPROTO_MPLS		137		/* unicast MPLS packet */
#define	IPPROTO_PFSYNC		240		/* PFSYNC */
#define	IPPROTO_RAW		255		/* raw IP packet */

#define	IPPROTO_MAX		256

/* Only used internally, so it can be outside the range of valid IP protocols */
#define	IPPROTO_DIVERT		258		/* Divert sockets */

/*
 * From FreeBSD:
 *
 * Local port number conventions:
 *
 * When a user does a bind(2) or connect(2) with a port number of zero,
 * a non-conflicting local port address is chosen.
 * The default range is IPPORT_RESERVED through
 * IPPORT_USERRESERVED, although that is settable by sysctl.
 *
 * A user may set the IPPROTO_IP option IP_PORTRANGE to change this
 * default assignment range.
 *
 * The value IP_PORTRANGE_DEFAULT causes the default behavior.
 *
 * The value IP_PORTRANGE_HIGH changes the range of candidate port numbers
 * into the "high" range.  These are reserved for client outbound connections
 * which do not want to be filtered by any firewalls.
 *
 * The value IP_PORTRANGE_LOW changes the range to the "low" are
 * that is (by convention) restricted to privileged processes.  This
 * convention is based on "vouchsafe" principles only.  It is only secure
 * if you trust the remote host to restrict these ports.
 *
 * The default range of ports and the high range can be changed by
 * sysctl(3).  (net.inet.ip.port{hi}{first,last})
 *
 * Changing those values has bad security implications if you are
 * using a stateless firewall that is allowing packets outside of that
 * range in order to allow transparent outgoing connections.
 *
 * Such a firewall configuration will generally depend on the use of these
 * default values.  If you change them, you may find your Security
 * Administrator looking for you with a heavy object.
 */

/*
 * Ports < IPPORT_RESERVED are reserved for
 * privileged processes (e.g. root).
 * Ports > IPPORT_USERRESERVED are reserved
 * for servers, not necessarily privileged.
 */
#define	IPPORT_RESERVED		1024
#define	IPPORT_USERRESERVED	49151

/*
 * Default local port range to use by setting IP_PORTRANGE_HIGH
 */
#define IPPORT_HIFIRSTAUTO	49152
#define IPPORT_HILASTAUTO	65535

#ifndef _IN_ADDR_DECLARED
#define _IN_ADDR_DECLARED
/*
 * IP Version 4 Internet address (a structure for historical reasons)
 */
struct in_addr {
	in_addr_t s_addr;
};
#endif /* _IN_ADDR_DECLARED */

/* last return value of *_input(), meaning "all job for this pkt is done".  */
#define	IPPROTO_DONE		257

/*
 * Definitions of bits in internet address integers.
 * On subnets, the decomposition of addresses to host and net parts
 * is done according to subnet mask, not the masks here.
 *
 * By byte-swapping the constants, we avoid ever having to byte-swap IP
 * addresses inside the kernel.  Unfortunately, user-level programs rely
 * on these macros not doing byte-swapping.
 */
#ifdef _KERNEL
#define	__IPADDR(x)	((u_int32_t) htonl((u_int32_t)(x)))
#else
#define	__IPADDR(x)	((u_int32_t)(x))
#endif /* _KERNEL */

#define	IN_CLASSA(i)		(((u_int32_t)(i) & __IPADDR(0x80000000)) == \
				 __IPADDR(0x00000000))
#define	IN_CLASSA_NET		__IPADDR(0xff000000)
#define	IN_CLASSA_NSHIFT	24
#define	IN_CLASSA_HOST		__IPADDR(0x00ffffff)
#define	IN_CLASSA_MAX		128

#define	IN_CLASSB(i)		(((u_int32_t)(i) & __IPADDR(0xc0000000)) == \
				 __IPADDR(0x80000000))
#define	IN_CLASSB_NET		__IPADDR(0xffff0000)
#define	IN_CLASSB_NSHIFT	16
#define	IN_CLASSB_HOST		__IPADDR(0x0000ffff)
#define	IN_CLASSB_MAX		65536

#define	IN_CLASSC(i)		(((u_int32_t)(i) & __IPADDR(0xe0000000)) == \
				 __IPADDR(0xc0000000))
#define	IN_CLASSC_NET		__IPADDR(0xffffff00)
#define	IN_CLASSC_NSHIFT	8
#define	IN_CLASSC_HOST		__IPADDR(0x000000ff)

#define	IN_CLASSD(i)		(((u_int32_t)(i) & __IPADDR(0xf0000000)) == \
				 __IPADDR(0xe0000000))
/* These ones aren't really net and host fields, but routing needn't know. */
#define	IN_CLASSD_NET		__IPADDR(0xf0000000)
#define	IN_CLASSD_NSHIFT	28
#define	IN_CLASSD_HOST		__IPADDR(0x0fffffff)
#define	IN_MULTICAST(i)		IN_CLASSD(i)

#define	IN_RFC3021_NET		__IPADDR(0xfffffffe)
#define	IN_RFC3021_NSHIFT	31
#define	IN_RFC3021_HOST		__IPADDR(0x00000001)
#define	IN_RFC3021_SUBNET(n)	(((u_int32_t)(n) & IN_RFC3021_NET) == \
				 IN_RFC3021_NET)

#define	IN_EXPERIMENTAL(i)	(((u_int32_t)(i) & __IPADDR(0xf0000000)) == \
				 __IPADDR(0xf0000000))
#define	IN_BADCLASS(i)		(((u_int32_t)(i) & __IPADDR(0xf0000000)) == \
				 __IPADDR(0xf0000000))

#define	IN_LOCAL_GROUP(i)	(((u_int32_t)(i) & __IPADDR(0xffffff00)) == \
				 __IPADDR(0xe0000000))

#ifdef _KERNEL
#define IN_CLASSFULBROADCAST(i, b) \
				((IN_CLASSC(b) && (b | IN_CLASSC_HOST) == i) ||	\
				 (IN_CLASSB(b) && (b | IN_CLASSB_HOST) == i) ||	\
				 (IN_CLASSA(b) && (b | IN_CLASSA_HOST) == i))
#endif	/* _KERNEL */

#define	INADDR_ANY		__IPADDR(0x00000000)
#define	INADDR_LOOPBACK		__IPADDR(0x7f000001)
#define	INADDR_BROADCAST	__IPADDR(0xffffffff)	/* must be masked */
#ifndef _KERNEL
#define	INADDR_NONE		__IPADDR(0xffffffff)	/* -1 return */
#endif /* _KERNEL */

#define	INADDR_UNSPEC_GROUP	__IPADDR(0xe0000000)	/* 224.0.0.0 */
#define	INADDR_ALLHOSTS_GROUP	__IPADDR(0xe0000001)	/* 224.0.0.1 */
#define	INADDR_ALLROUTERS_GROUP __IPADDR(0xe0000002)	/* 224.0.0.2 */
#define	INADDR_CARP_GROUP	__IPADDR(0xe0000012)	/* 224.0.0.18 */
#define	INADDR_PFSYNC_GROUP	__IPADDR(0xe00000f0)	/* 224.0.0.240 */
#define INADDR_MAX_LOCAL_GROUP	__IPADDR(0xe00000ff)	/* 224.0.0.255 */

#define	IN_LOOPBACKNET		127			/* official! */

/*
 * IP Version 4 socket address.
 */
struct sockaddr_in {
	u_int8_t    sin_len;
	sa_family_t sin_family;
	in_port_t   sin_port;
	struct	    in_addr sin_addr;
	int8_t	    sin_zero[8];
};

/*
 * Structure used to describe IP options.
 * Used to store options internally, to pass them to a process,
 * or to restore options retrieved earlier.
 * The ip_dst is used for the first-hop gateway when using a source route
 * (this gets put into the header proper).
 */
struct ip_opts {
	struct in_addr	ip_dst;		/* first hop, 0 w/o src rt */
#if defined(__cplusplus)
	int8_t		Ip_opts[40];	/* cannot have same name as class */
#else
	int8_t		ip_opts[40];	/* actually variable in size */
#endif /* defined(__cplusplus) */
};

/*
 * Options for use with [gs]etsockopt at the IP level.
 * First word of comment is data type; bool is stored in int.
 */
#define	IP_OPTIONS		1    /* buf/ip_opts; set/get IP options */
#define	IP_HDRINCL		2    /* int; header is included with data */
#define	IP_TOS			3    /* int; IP type of service and preced. */
#define	IP_TTL			4    /* int; IP time to live */
#define	IP_RECVOPTS		5    /* bool; receive all IP opts w/dgram */
#define	IP_RECVRETOPTS		6    /* bool; receive IP opts for response */
#define	IP_RECVDSTADDR		7    /* bool; receive IP dst addr w/dgram */
#define	IP_RETOPTS		8    /* ip_opts; set/get IP options */
#define	IP_MULTICAST_IF		9    /* in_addr; set/get IP multicast i/f  */
#define	IP_MULTICAST_TTL	10   /* u_char; set/get IP multicast ttl */
#define	IP_MULTICAST_LOOP	11   /* u_char; set/get IP multicast loopback */
#define	IP_ADD_MEMBERSHIP	12   /* ip_mreq; add an IP group membership */
#define	IP_DROP_MEMBERSHIP	13   /* ip_mreq; drop an IP group membership */
#define IP_PORTRANGE		19   /* int; range to choose for unspec port */
#define IP_AUTH_LEVEL		20   /* int; authentication used */
#define IP_ESP_TRANS_LEVEL	21   /* int; transport encryption */
#define IP_ESP_NETWORK_LEVEL	22   /* int; full-packet encryption */
#define IP_IPSEC_LOCAL_ID	23   /* buf; IPsec local ID */
#define IP_IPSEC_REMOTE_ID	24   /* buf; IPsec remote ID */
#define IP_IPSEC_LOCAL_CRED	25   /* buf; was: IPsec local credentials */
#define IP_IPSEC_REMOTE_CRED	26   /* buf; was: IPsec remote credentials */
#define IP_IPSEC_LOCAL_AUTH	27   /* buf; was: IPsec local auth material */
#define IP_IPSEC_REMOTE_AUTH	28   /* buf; was: IPsec remote auth material */
#define IP_IPCOMP_LEVEL		29   /* int; compression used */
#define IP_RECVIF		30   /* bool; receive reception if w/dgram */
#define IP_RECVTTL		31   /* bool; receive IP TTL w/dgram */
#define IP_MINTTL		32   /* minimum TTL for packet or drop */
#define IP_RECVDSTPORT		33   /* bool; receive IP dst port w/dgram */
#define IP_PIPEX		34   /* bool; using PIPEX */
#define IP_RECVRTABLE		35   /* bool; receive rdomain w/dgram */
#define IP_IPSECFLOWINFO	36   /* bool; IPsec flow info for dgram */
#define IP_IPDEFTTL		37   /* int; IP TTL system default */
#define IP_SENDSRCADDR		IP_RECVDSTADDR  /* struct in_addr; */
						/* source address to use */

#define IP_RTABLE		0x1021	/* int; routing table, see SO_RTABLE */

#if __BSD_VISIBLE
/*
 * Security levels - IPsec, not IPSO
 */

#define IPSEC_LEVEL_BYPASS      0x00    /* Bypass policy altogether */
#define IPSEC_LEVEL_NONE        0x00    /* Send clear, accept any */
#define IPSEC_LEVEL_AVAIL       0x01    /* Send secure if SA available */
#define IPSEC_LEVEL_USE         0x02    /* Send secure, accept any */
#define IPSEC_LEVEL_REQUIRE     0x03    /* Require secure inbound, also use */
#define IPSEC_LEVEL_UNIQUE      0x04    /* Use outbound SA that is unique */
#define IPSEC_LEVEL_DEFAULT     IPSEC_LEVEL_AVAIL

#define IPSEC_AUTH_LEVEL_DEFAULT IPSEC_LEVEL_DEFAULT
#define IPSEC_ESP_TRANS_LEVEL_DEFAULT IPSEC_LEVEL_DEFAULT
#define IPSEC_ESP_NETWORK_LEVEL_DEFAULT IPSEC_LEVEL_DEFAULT
#define IPSEC_IPCOMP_LEVEL_DEFAULT IPSEC_LEVEL_DEFAULT

#endif /* __BSD_VISIBLE */

/*
 * Defaults and limits for options
 */
#define	IP_DEFAULT_MULTICAST_TTL  1	/* normally limit m'casts to 1 hop  */
#define	IP_DEFAULT_MULTICAST_LOOP 1	/* normally hear sends if a member  */
/*
 * The imo_membership vector for each socket starts at IP_MIN_MEMBERSHIPS
 * and is dynamically allocated at run-time, bounded by IP_MAX_MEMBERSHIPS,
 * and is reallocated when needed, sized according to a power-of-two increment.
 */
#define	IP_MIN_MEMBERSHIPS	15
#define	IP_MAX_MEMBERSHIPS	4095

/*
 * Argument structure for IP_ADD_MEMBERSHIP and IP_DROP_MEMBERSHIP.
 */
struct ip_mreq {
	struct	in_addr imr_multiaddr;	/* IP multicast address of group */
	struct	in_addr imr_interface;	/* local IP address of interface */
};

struct ip_mreqn {
	struct	in_addr imr_multiaddr;	/* IP multicast address of group */
	struct	in_addr imr_address;	/* local IP address of interface */
	int		imr_ifindex;	/* interface index */
};

/*
 * Argument for IP_PORTRANGE:
 * - which range to search when port is unspecified at bind() or connect()
 */
#define IP_PORTRANGE_DEFAULT	0	/* default range */
#define IP_PORTRANGE_HIGH	1	/* "high" - request firewall bypass */
#define IP_PORTRANGE_LOW	2	/* "low" - vouchsafe security */

/*
 * Buffer lengths for strings containing printable IP addresses
 */
#ifndef INET_ADDRSTRLEN
#define INET_ADDRSTRLEN		16
#endif /* INET_ADDRSTRLEN */


#if __BSD_VISIBLE
/*
 * Definitions for inet sysctl operations.
 *
 * Third level is protocol number.
 * Fourth level is desired variable within that protocol.
 */
#define	IPPROTO_MAXID	(IPPROTO_DIVERT + 1)	/* don't list to IPPROTO_MAX */

#define	CTL_IPPROTO_NAMES { \
	{ "ip", CTLTYPE_NODE }, \
	{ "icmp", CTLTYPE_NODE }, \
	{ "igmp", CTLTYPE_NODE }, \
	{ "ggp", CTLTYPE_NODE }, \
	{ "ipip", CTLTYPE_NODE }, \
	{ 0, 0 }, \
	{ "tcp", CTLTYPE_NODE }, \
	{ 0, 0 }, \
	{ "egp", CTLTYPE_NODE }, \
	{ 0, 0 }, \
	{ 0, 0 }, \
	{ 0, 0 }, \
	{ "pup", CTLTYPE_NODE }, \
	{ 0, 0 }, \
	{ 0, 0 }, \
	{ 0, 0 }, \
	{ 0, 0 }, \
	{ "udp", CTLTYPE_NODE }, \
	{ 0, 0 }, \
	{ 0, 0 }, \
	{ 0, 0 }, \
	{ 0, 0 }, \
	{ 0, 0 }, \
	{ 0, 0 }, \
	{ 0, 0 }, \
	{ 0, 0 }, \
	{ 0, 0 }, \
	{ 0, 0 }, \
	{ 0, 0 }, \
	{ 0, 0 }, \
	{ 0, 0 }, \
	{ 0, 0 }, \
	{ 0, 0 }, \
	{ 0, 0 }, \
	{ 0, 0 }, \
	{ 0, 0 }, \
	{ 0, 0 }, \
	{ 0, 0 }, \
	{ 0, 0 }, \
	{ 0, 0 }, \
	{ 0, 0 }, \
	{ 0, 0 }, \
	{ 0, 0 }, \
	{ 0, 0 }, \
	{ 0, 0 }, \
	{ 0, 0 }, \
	{ 0, 0 }, \
	{ "gre", CTLTYPE_NODE }, \
	{ 0, 0 }, \
	{ 0, 0 }, \
	{ "esp", CTLTYPE_NODE }, \
	{ "ah", CTLTYPE_NODE }, \
	{ 0, 0 }, \
	{ 0, 0 }, \
	{ 0, 0 }, \
	{ 0, 0 }, \
	{ 0, 0 }, \
	{ 0, 0 }, \
	{ 0, 0 }, \
	{ 0, 0 }, \
	{ 0, 0 }, \
	{ 0, 0 }, \
	{ 0, 0 }, \
	{ 0, 0 }, \
	{ 0, 0 }, \
	{ 0, 0 }, \
	{ 0, 0 }, \
	{ 0, 0 }, \
	{ 0, 0 }, \
	{ 0, 0 }, \
	{ 0, 0 }, \
	{ 0, 0 }, \
	{ 0, 0 }, \
	{ 0, 0 }, \
	{ 0, 0 }, \
	{ 0, 0 }, \
	{ 0, 0 }, \
	{ 0, 0 }, \
	{ 0, 0 }, \
	{ 0, 0 }, \
	{ 0, 0 }, \
	{ 0, 0 }, \
	{ 0, 0 }, \
	{ 0, 0 }, \
	{ 0, 0 }, \
	{ 0, 0 }, \
	{ 0, 0 }, \
	{ 0, 0 }, \
	{ 0, 0 }, \
	{ 0, 0 }, \
	{ 0, 0 }, \
	{ 0, 0 }, \
	{ 0, 0 }, \
	{ 0, 0 }, \
	{ 0, 0 }, \
	{ 0, 0 }, \
	{ 0, 0 }, \
	{ "etherip", CTLTYPE_NODE }, \
	{ 0, 0 }, \
	{ 0, 0 }, \
	{ 0, 0 }, \
	{ 0, 0 }, \
	{ 0, 0 }, \
	{ 0, 0 }, \
	{ 0, 0 }, \
	{ 0, 0 }, \
	{ 0, 0 }, \
	{ 0, 0 }, \
	{ "ipcomp", CTLTYPE_NODE }, \
	{ 0, 0 }, \
	{ 0, 0 }, \
	{ 0, 0 }, \
	{ "carp", CTLTYPE_NODE }, \
	{ 0, 0 }, \
	{ 0, 0 }, \
	{ 0, 0 }, \
	{ 0, 0 }, \
	{ 0, 0 }, \
	{ 0, 0 }, \
	{ 0, 0 }, \
	{ 0, 0 }, \
	{ 0, 0 }, \
	{ 0, 0 }, \
	{ 0, 0 }, \
	{ 0, 0 }, \
	{ 0, 0 }, \
	{ 0, 0 }, \
	{ 0, 0 }, \
	{ 0, 0 }, \
	{ 0, 0 }, \
	{ 0, 0 }, \
	{ 0, 0 }, \
	{ 0, 0 }, \
	{ 0, 0 }, \
	{ 0, 0 }, \
	{ 0, 0 }, \
	{ 0, 0 }, \
	{ 0, 0 }, \
	{ 0, 0 }, \
	{ 0, 0 }, \
	{ 0, 0 }, \
	{ 0, 0 }, \
	{ 0, 0 }, \
	{ 0, 0 }, \
	{ 0, 0 }, \
	{ 0, 0 }, \
	{ 0, 0 }, \
	{ 0, 0 }, \
	{ 0, 0 }, \
	{ 0, 0 }, \
	{ 0, 0 }, \
	{ 0, 0 }, \
	{ 0, 0 }, \
	{ 0, 0 }, \
	{ 0, 0 }, \
	{ 0, 0 }, \
	{ 0, 0 }, \
	{ 0, 0 }, \
	{ 0, 0 }, \
	{ 0, 0 }, \
	{ 0, 0 }, \
	{ 0, 0 }, \
	{ 0, 0 }, \
	{ 0, 0 }, \
	{ 0, 0 }, \
	{ 0, 0 }, \
	{ 0, 0 }, \
	{ 0, 0 }, \
	{ 0, 0 }, \
	{ 0, 0 }, \
	{ 0, 0 }, \
	{ 0, 0 }, \
	{ 0, 0 }, \
	{ 0, 0 }, \
	{ 0, 0 }, \
	{ 0, 0 }, \
	{ 0, 0 }, \
	{ 0, 0 }, \
	{ 0, 0 }, \
	{ 0, 0 }, \
	{ 0, 0 }, \
	{ 0, 0 }, \
	{ 0, 0 }, \
	{ 0, 0 }, \
	{ 0, 0 }, \
	{ 0, 0 }, \
	{ 0, 0 }, \
	{ 0, 0 }, \
	{ 0, 0 }, \
	{ 0, 0 }, \
	{ 0, 0 }, \
	{ 0, 0 }, \
	{ 0, 0 }, \
	{ 0, 0 }, \
	{ 0, 0 }, \
	{ 0, 0 }, \
	{ 0, 0 }, \
	{ 0, 0 }, \
	{ 0, 0 }, \
	{ 0, 0 }, \
	{ 0, 0 }, \
	{ 0, 0 }, \
	{ 0, 0 }, \
	{ 0, 0 }, \
	{ 0, 0 }, \
	{ 0, 0 }, \
	{ 0, 0 }, \
	{ 0, 0 }, \
	{ 0, 0 }, \
	{ 0, 0 }, \
	{ 0, 0 }, \
	{ 0, 0 }, \
	{ 0, 0 }, \
	{ 0, 0 }, \
	{ 0, 0 }, \
	{ 0, 0 }, \
	{ 0, 0 }, \
	{ 0, 0 }, \
	{ 0, 0 }, \
	{ 0, 0 }, \
	{ 0, 0 }, \
	{ 0, 0 }, \
	{ 0, 0 }, \
	{ 0, 0 }, \
	{ 0, 0 }, \
	{ 0, 0 }, \
	{ 0, 0 }, \
	{ 0, 0 }, \
	{ 0, 0 }, \
	{ 0, 0 }, \
	{ 0, 0 }, \
	{ 0, 0 }, \
	{ 0, 0 }, \
	{ 0, 0 }, \
	{ 0, 0 }, \
	{ 0, 0 }, \
	{ 0, 0 }, \
	{ 0, 0 }, \
	{ 0, 0 }, \
	{ 0, 0 }, \
	{ "pfsync", CTLTYPE_NODE }, \
	{ 0, 0 }, \
	{ 0, 0 }, \
	{ 0, 0 }, \
	{ 0, 0 }, \
	{ 0, 0 }, \
	{ 0, 0 }, \
	{ 0, 0 }, \
	{ 0, 0 }, \
	{ 0, 0 }, \
	{ 0, 0 }, \
	{ 0, 0 }, \
	{ 0, 0 }, \
	{ 0, 0 }, \
	{ 0, 0 }, \
	{ 0, 0 }, \
	{ 0, 0 }, \
	{ 0, 0 }, \
	{ "divert", CTLTYPE_NODE }, \
}

/*
 * Names for IP sysctl objects
 */
#define	IPCTL_FORWARDING	1	/* act as router */
#define	IPCTL_SENDREDIRECTS	2	/* may send redirects when forwarding */
#define	IPCTL_DEFTTL		3	/* default TTL */
#define	IPCTL_SOURCEROUTE	5	/* may perform source routes */
#define	IPCTL_DIRECTEDBCAST	6	/* default broadcast behavior */
#define IPCTL_IPPORT_FIRSTAUTO	7
#define IPCTL_IPPORT_LASTAUTO	8
#define IPCTL_IPPORT_HIFIRSTAUTO 9
#define IPCTL_IPPORT_HILASTAUTO	10
#define	IPCTL_IPPORT_MAXQUEUE	11
#define	IPCTL_ENCDEBUG		12
#define IPCTL_IPSEC_STATS	13
#define IPCTL_IPSEC_EXPIRE_ACQUIRE 14   /* How long to wait for key mgmt. */
#define IPCTL_IPSEC_EMBRYONIC_SA_TIMEOUT	15 /* new SA lifetime */
#define IPCTL_IPSEC_REQUIRE_PFS 16
#define IPCTL_IPSEC_SOFT_ALLOCATIONS            17
#define IPCTL_IPSEC_ALLOCATIONS 18
#define IPCTL_IPSEC_SOFT_BYTES  19
#define IPCTL_IPSEC_BYTES       20
#define IPCTL_IPSEC_TIMEOUT     21
#define IPCTL_IPSEC_SOFT_TIMEOUT 22
#define IPCTL_IPSEC_SOFT_FIRSTUSE 23
#define IPCTL_IPSEC_FIRSTUSE    24
#define IPCTL_IPSEC_ENC_ALGORITHM 25
#define IPCTL_IPSEC_AUTH_ALGORITHM 26
#define	IPCTL_MTUDISC		27	/* allow path MTU discovery */
#define	IPCTL_MTUDISCTIMEOUT	28	/* allow path MTU discovery */
#define	IPCTL_IPSEC_IPCOMP_ALGORITHM	29
#define	IPCTL_IFQUEUE		30
#define	IPCTL_MFORWARDING	31
#define	IPCTL_MULTIPATH		32
#define	IPCTL_STATS		33	/* IP statistics */
#define	IPCTL_MRTPROTO		34	/* type of multicast */
#define	IPCTL_MRTSTATS		35
#define	IPCTL_ARPQUEUED		36
#define	IPCTL_MRTMFC		37
#define	IPCTL_MRTVIF		38
#define	IPCTL_ARPTIMEOUT	39
#define	IPCTL_ARPDOWN		40
#define	IPCTL_ARPQUEUE		41
#define	IPCTL_MAXID		42

#define	IPCTL_NAMES { \
	{ 0, 0 }, \
	{ "forwarding", CTLTYPE_INT }, \
	{ "redirect", CTLTYPE_INT }, \
	{ "ttl", CTLTYPE_INT }, \
	/* { "mtu", CTLTYPE_INT }, */ { 0, 0 }, \
	{ "sourceroute", CTLTYPE_INT }, \
	{ "directed-broadcast", CTLTYPE_INT }, \
	{ "portfirst", CTLTYPE_INT }, \
	{ "portlast", CTLTYPE_INT }, \
	{ "porthifirst", CTLTYPE_INT }, \
	{ "porthilast", CTLTYPE_INT }, \
	{ "maxqueue", CTLTYPE_INT }, \
	{ "encdebug", CTLTYPE_INT }, \
	{ 0, 0 /* ipsecstat */ }, \
	{ "ipsec-expire-acquire", CTLTYPE_INT }, \
	{ "ipsec-invalid-life", CTLTYPE_INT }, \
	{ "ipsec-pfs", CTLTYPE_INT }, \
	{ "ipsec-soft-allocs", CTLTYPE_INT }, \
	{ "ipsec-allocs", CTLTYPE_INT }, \
	{ "ipsec-soft-bytes", CTLTYPE_INT }, \
	{ "ipsec-bytes", CTLTYPE_INT }, \
	{ "ipsec-timeout", CTLTYPE_INT }, \
	{ "ipsec-soft-timeout", CTLTYPE_INT }, \
	{ "ipsec-soft-firstuse", CTLTYPE_INT }, \
	{ "ipsec-firstuse", CTLTYPE_INT }, \
	{ "ipsec-enc-alg", CTLTYPE_STRING }, \
	{ "ipsec-auth-alg", CTLTYPE_STRING }, \
	{ "mtudisc", CTLTYPE_INT }, \
	{ "mtudisctimeout", CTLTYPE_INT }, \
	{ "ipsec-comp-alg", CTLTYPE_STRING }, \
	{ "ifq", CTLTYPE_NODE }, \
	{ "mforwarding", CTLTYPE_INT }, \
	{ "multipath", CTLTYPE_INT }, \
	{ "stats", CTLTYPE_STRUCT }, \
	{ "mrtproto", CTLTYPE_INT }, \
	{ "mrtstats", CTLTYPE_STRUCT }, \
	{ "arpqueued", CTLTYPE_INT }, \
	{ "mrtmfc", CTLTYPE_STRUCT }, \
	{ "mrtvif", CTLTYPE_STRUCT }, \
	{ "arptimeout", CTLTYPE_INT }, \
	{ "arpdown", CTLTYPE_INT }, \
	{ "arpq", CTLTYPE_NODE }, \
}

#endif /* __BSD_VISIBLE */

/* INET6 stuff */
#define __KAME_NETINET_IN_H_INCLUDED_
#include <netinet6/in6.h>
#undef __KAME_NETINET_IN_H_INCLUDED_

#ifndef _KERNEL
#if __BSD_VISIBLE
__BEGIN_DECLS
int	   bindresvport(int, struct sockaddr_in *);
struct sockaddr;
int	   bindresvport_sa(int, struct sockaddr *);
__END_DECLS
#endif /* __BSD_VISIBLE */
#endif /* !_KERNEL */

#ifdef _KERNEL
extern const u_char inetctlerrmap[];
extern const struct in_addr zeroin_addr;

struct mbuf;
struct sockaddr;
struct sockaddr_in;
struct ifaddr;
struct in_ifaddr;
struct route;
struct netstack;

void	   ipv4_input(struct ifnet *, struct mbuf *, struct netstack *);
struct mbuf *
	   ipv4_check(struct ifnet *, struct mbuf *);

int	   in_broadcast(struct in_addr, u_int);
int	   in_canforward(struct in_addr);
int	   in_cksum(struct mbuf *, int);
int	   in4_cksum(struct mbuf *, u_int8_t, int, int);
void	   in_hdr_cksum_out(struct mbuf *, struct ifnet *);
void	   in_proto_cksum_out(struct mbuf *, struct ifnet *);
int	   in_ifcap_cksum(struct mbuf *, struct ifnet *, int);
void	   in_ifdetach(struct ifnet *);
int	   in_mask2len(struct in_addr *);
void	   in_len2mask(struct in_addr *, int);
int	   in_nam2sin(const struct mbuf *, struct sockaddr_in **);
int	   in_sa2sin(struct sockaddr *, struct sockaddr_in **);

char	  *inet_ntoa(struct in_addr);
int	   inet_nat64(int, const void *, void *, const void *, u_int8_t);
int	   inet_nat46(int, const void *, void *, const void *, u_int8_t);

const char *inet_ntop(int, const void *, char *, socklen_t);
const char *sockaddr_ntop(struct sockaddr *, char *, size_t);

#define	in_hosteq(s,t)	((s).s_addr == (t).s_addr)
#define	in_nullhost(x)	((x).s_addr == INADDR_ANY)

/*
 * Convert between address family specific and general structs.
 * Inline functions check the source type and are stricter than
 * casts or defines.
 */

static inline struct sockaddr_in *
satosin(struct sockaddr *sa)
{
	return ((struct sockaddr_in *)(sa));
}

static inline const struct sockaddr_in *
satosin_const(const struct sockaddr *sa)
{
	return ((const struct sockaddr_in *)(sa));
}

static inline struct sockaddr *
sintosa(struct sockaddr_in *sin)
{
	return ((struct sockaddr *)(sin));
}

static inline struct in_ifaddr *
ifatoia(struct ifaddr *ifa)
{
	return ((struct in_ifaddr *)(ifa));
}
#endif /* _KERNEL */
#endif /* _NETINET_IN_H_ */
