/*	$OpenBSD: ip6.h,v 1.23 2025/05/27 07:52:49 bluhm Exp $	*/
/*	$KAME: ip6.h,v 1.45 2003/06/05 04:46:38 keiichi Exp $	*/

/*
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
 */

/*
 * Copyright (c) 1982, 1986, 1993
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
 *	@(#)ip.h	8.1 (Berkeley) 6/10/93
 */

#ifndef _NETINET_IP6_H_
#define _NETINET_IP6_H_

/*
 * Definition for internet protocol version 6.
 * RFC 2460
 */

struct ip6_hdr {
	union {
		struct ip6_hdrctl {
			u_int32_t ip6_un1_flow;	/* 20 bits of flow-ID */
			u_int16_t ip6_un1_plen;	/* payload length */
			u_int8_t  ip6_un1_nxt;	/* next header */
			u_int8_t  ip6_un1_hlim;	/* hop limit */
		} ip6_un1;
		u_int8_t ip6_un2_vfc;	/* 4 bits version, top 4 bits class */
	} ip6_ctlun;
	struct in6_addr ip6_src;	/* source address */
	struct in6_addr ip6_dst;	/* destination address */
} __packed;

#define ip6_vfc		ip6_ctlun.ip6_un2_vfc
#define ip6_flow	ip6_ctlun.ip6_un1.ip6_un1_flow
#define ip6_plen	ip6_ctlun.ip6_un1.ip6_un1_plen
#define ip6_nxt		ip6_ctlun.ip6_un1.ip6_un1_nxt
#define ip6_hlim	ip6_ctlun.ip6_un1.ip6_un1_hlim
#define ip6_hops	ip6_ctlun.ip6_un1.ip6_un1_hlim

#define IPV6_VERSION		0x60
#define IPV6_VERSION_MASK	0xf0

#if _BYTE_ORDER == _BIG_ENDIAN
#define IPV6_FLOWINFO_MASK	0x0fffffff	/* flow info (28 bits) */
#define IPV6_FLOWLABEL_MASK	0x000fffff	/* flow label (20 bits) */
#else
#if _BYTE_ORDER == _LITTLE_ENDIAN
#define IPV6_FLOWINFO_MASK	0xffffff0f	/* flow info (28 bits) */
#define IPV6_FLOWLABEL_MASK	0xffff0f00	/* flow label (20 bits) */
#endif /* _LITTLE_ENDIAN */
#endif
#if 1
/* ECN bits proposed by Sally Floyd */
#define IP6TOS_CE		0x01	/* congestion experienced */
#define IP6TOS_ECT		0x02	/* ECN-capable transport */
#endif

#ifdef _KERNEL
/*
 * for IPv6 pseudo header checksum
 * XXX nonstandard
 */
struct ip6_hdr_pseudo {
	struct in6_addr ip6ph_src;
	struct in6_addr ip6ph_dst;
	u_int32_t	ip6ph_len;
	u_int8_t	ip6ph_zero[3];
	u_int8_t	ip6ph_nxt;
} __packed;
#endif

/*
 * Extension Headers
 */
struct	ip6_ext {
	u_int8_t ip6e_nxt;
	u_int8_t ip6e_len;
} __packed;

/* Hop-by-Hop options header */
/* XXX should we pad it to force alignment on an 8-byte boundary? */
struct ip6_hbh {
	u_int8_t ip6h_nxt;	/* next header */
	u_int8_t ip6h_len;	/* length in units of 8 octets */
	/* followed by options */
} __packed;

/* Destination options header */
/* XXX should we pad it to force alignment on an 8-byte boundary? */
struct ip6_dest {
	u_int8_t ip6d_nxt;	/* next header */
	u_int8_t ip6d_len;	/* length in units of 8 octets */
	/* followed by options */
} __packed;

/* Option types and related macros */
#define IP6OPT_PAD1		0x00	/* 00 0 00000 */
#define IP6OPT_PADN		0x01	/* 00 0 00001 */
#define IP6OPT_JUMBO		0xC2	/* 11 0 00010 = 194 */
#define IP6OPT_NSAP_ADDR	0xC3	/* 11 0 00011 */
#define IP6OPT_TUNNEL_LIMIT	0x04	/* 00 0 00100 */
#define IP6OPT_ROUTER_ALERT	0x05	/* 00 0 00101 (RFC3542, recommended) */

#define IP6OPT_RTALERT_LEN	4
#define IP6OPT_RTALERT_MLD	0	/* Datagram contains an MLD message */
#define IP6OPT_RTALERT_RSVP	1	/* Datagram contains an RSVP message */
#define IP6OPT_RTALERT_ACTNET	2	/* contains an Active Networks msg */
#define IP6OPT_MINLEN		2

#define IP6OPT_TYPE(o)		((o) & 0xC0)
#define IP6OPT_TYPE_SKIP	0x00
#define IP6OPT_TYPE_DISCARD	0x40
#define IP6OPT_TYPE_FORCEICMP	0x80
#define IP6OPT_TYPE_ICMP	0xC0

#define IP6OPT_MUTABLE		0x20

/* IPv6 options: common part */
struct ip6_opt {
	u_int8_t ip6o_type;
	u_int8_t ip6o_len;
} __packed;

/* Jumbo Payload Option */
struct ip6_opt_jumbo {
	u_int8_t ip6oj_type;
	u_int8_t ip6oj_len;
	u_int8_t ip6oj_jumbo_len[4];
} __packed;
#define IP6OPT_JUMBO_LEN 6

/* NSAP Address Option */
struct ip6_opt_nsap {
	u_int8_t ip6on_type;
	u_int8_t ip6on_len;
	u_int8_t ip6on_src_nsap_len;
	u_int8_t ip6on_dst_nsap_len;
	/* followed by source NSAP */
	/* followed by destination NSAP */
} __packed;

/* Tunnel Limit Option */
struct ip6_opt_tunnel {
	u_int8_t ip6ot_type;
	u_int8_t ip6ot_len;
	u_int8_t ip6ot_encap_limit;
} __packed;

/* Router Alert Option */
struct ip6_opt_router {
	u_int8_t ip6or_type;
	u_int8_t ip6or_len;
	u_int8_t ip6or_value[2];
} __packed;
/* Router alert values (in network byte order) */
#if _BYTE_ORDER == _BIG_ENDIAN
#define IP6_ALERT_MLD	0x0000
#define IP6_ALERT_RSVP	0x0001
#define IP6_ALERT_AN	0x0002
#else
#if _BYTE_ORDER == _LITTLE_ENDIAN
#define IP6_ALERT_MLD	0x0000
#define IP6_ALERT_RSVP	0x0100
#define IP6_ALERT_AN	0x0200
#endif /* _LITTLE_ENDIAN */
#endif

/* Routing header */
struct ip6_rthdr {
	u_int8_t  ip6r_nxt;	/* next header */
	u_int8_t  ip6r_len;	/* length in units of 8 octets */
	u_int8_t  ip6r_type;	/* routing type */
	u_int8_t  ip6r_segleft;	/* segments left */
	/* followed by routing type specific data */
} __packed;

/* Type 0 Routing header */
struct ip6_rthdr0 {
	u_int8_t  ip6r0_nxt;		/* next header */
	u_int8_t  ip6r0_len;		/* length in units of 8 octets */
	u_int8_t  ip6r0_type;		/* always zero */
	u_int8_t  ip6r0_segleft;	/* segments left */
	u_int32_t ip6r0_reserved;	/* reserved field */
} __packed;

/* Fragment header */
struct ip6_frag {
	u_int8_t  ip6f_nxt;		/* next header */
	u_int8_t  ip6f_reserved;	/* reserved field */
	u_int16_t ip6f_offlg;		/* offset, reserved, and flag */
	u_int32_t ip6f_ident;		/* identification */
} __packed;

#if _BYTE_ORDER == _BIG_ENDIAN
#define IP6F_OFF_MASK		0xfff8	/* mask out offset from _offlg */
#define IP6F_RESERVED_MASK	0x0006	/* reserved bits in ip6f_offlg */
#define IP6F_MORE_FRAG		0x0001	/* more-fragments flag */
#else /* _BYTE_ORDER == _LITTLE_ENDIAN */
#define IP6F_OFF_MASK		0xf8ff	/* mask out offset from _offlg */
#define IP6F_RESERVED_MASK	0x0600	/* reserved bits in ip6f_offlg */
#define IP6F_MORE_FRAG		0x0100	/* more-fragments flag */
#endif /* _BYTE_ORDER == _LITTLE_ENDIAN */

/*
 * Internet implementation parameters.
 */
#define IPV6_MAXHLIM	255	/* maximum hoplimit */
#define IPV6_DEFHLIM	64	/* default hlim */
#define IPV6_FRAGTTL	120	/* ttl for fragment packets, in slowtimo tick */
#define IPV6_HLIMDEC	1	/* subtracted when forwarding */

#define IPV6_MMTU	1280	/* minimal MTU and reassembly. 1024 + 256 */
#define IPV6_MAXPACKET	65535	/* ip6 max packet size without Jumbo payload*/

#ifdef _KERNEL
/*
 * ip6_exthdr_get() ensures that intermediate protocol header (from "off"
 * to "len") is located in single mbuf, on contiguous memory region.
 * The pointer to the region will be returned to pointer variable "val",
 * with type "typ".
 */

static inline void *
ip6_exthdr_get(struct mbuf **mp, int off, int len)
{
	struct mbuf *t;
	int toff;

	if ((*mp)->m_len >= off + len)
		return (mtod(*mp, caddr_t) + off);

	t = m_pulldown(*mp, off, len, &toff);
	if (t == NULL) {
		*mp = NULL;
		return (NULL);
	}
	return (mtod(t, caddr_t) + toff);
}
#endif /* _KERNEL */
#endif /* _NETINET_IP6_H_ */
