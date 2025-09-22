/*	$OpenBSD: mpls.h,v 1.47 2025/03/02 21:28:32 bluhm Exp $	*/

/*
 * Copyright (C) 1999, 2000 and 2001 AYAME Project, WIDE Project.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
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
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULARPURPOSE 
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE PROJECT OR CONTRIBUTORS BE LIABLE 
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, ORCONSEQUENTIAL 
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS 
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) 
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT 
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY 
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF 
 * SUCH DAMAGE.
 */

#ifndef _NETMPLS_MPLS_H_
#define _NETMPLS_MPLS_H_

/*
 * Structure of a SHIM header.
 */
#define MPLS_LABEL_MAX		((1 << 20) - 1)

struct shim_hdr {
	u_int32_t shim_label;	/* 20 bit label, 4 bit exp & BoS, 8 bit TTL */
};

#define MPLS_HDRLEN	sizeof(struct shim_hdr)

/*
 * By byte-swapping the constants, we avoid ever having to byte-swap IP
 * addresses inside the kernel.  Unfortunately, user-level programs rely
 * on these macros not doing byte-swapping.
 */

#ifdef _KERNEL
#define __MADDR(x)     ((u_int32_t)htonl((u_int32_t)(x)))
#else
#define __MADDR(x)     ((u_int32_t)(x))
#endif

#define MPLS_LABEL_MASK		__MADDR(0xfffff000U)
#define MPLS_LABEL_OFFSET	12
#define MPLS_EXP_MASK		__MADDR(0x00000e00U)
#define MPLS_EXP_OFFSET		9
#define MPLS_BOS_MASK		__MADDR(0x00000100U)
#define MPLS_BOS_OFFSET		8
#define MPLS_TTL_MASK		__MADDR(0x000000ffU)

#define CW_ZERO_MASK		__MADDR(0xf0000000U)
#define CW_FRAG_MASK		__MADDR(0x00300000U)

#define MPLS_BOS_ISSET(l)	(((l) & MPLS_BOS_MASK) == MPLS_BOS_MASK)

/* Reserved label values (RFC3032) */
#define MPLS_LABEL_IPV4NULL	0               /* IPv4 Explicit NULL Label */
#define MPLS_LABEL_RTALERT	1               /* Router Alert Label       */
#define MPLS_LABEL_IPV6NULL	2               /* IPv6 Explicit NULL Label */
#define MPLS_LABEL_IMPLNULL	3               /* Implicit NULL Label      */
/*      MPLS_LABEL_RESERVED	4-15 */		/* Values 4-15 are reserved */
#define MPLS_LABEL_RESERVED_MAX 15

/*
 * Socket address
 */

struct sockaddr_mpls {
	u_int8_t	smpls_len;		/* length */
	u_int8_t	smpls_family;		/* AF_MPLS */
	u_int16_t	smpls_pad0;
	u_int32_t	smpls_label;		/* MPLS label */
	u_int32_t	smpls_pad1[2];
};

struct rt_mpls {
	u_int32_t	mpls_label;
	u_int8_t	mpls_operation;
	u_int8_t	mpls_exp;
};

#define MPLS_OP_LOCAL		0x0
#define MPLS_OP_POP		0x1
#define MPLS_OP_PUSH		0x2
#define MPLS_OP_SWAP		0x4

#define MPLS_INKERNEL_LOOP_MAX	16

#define satosmpls(sa)		((struct sockaddr_mpls *)(sa))
#define smplstosa(smpls)	((struct sockaddr *)(smpls))

/*
 * Names for MPLS sysctl objects
 */
#define MPLSCTL_ENABLE			1
#define	MPLSCTL_DEFTTL			2
#define MPLSCTL_MAPTTL_IP		5
#define MPLSCTL_MAPTTL_IP6		6
#define MPLSCTL_MAXID			7	

#define MPLSCTL_NAMES { \
	{ NULL, 0 }, \
	{ NULL, 0 }, \
	{ "ttl", CTLTYPE_INT }, \
	{ "ifq", CTLTYPE_NODE },\
	{ NULL, 0 }, \
	{ "mapttl_ip", CTLTYPE_INT }, \
	{ "mapttl_ip6", CTLTYPE_INT } \
}

#define IMR_TYPE_NONE			0
#define IMR_TYPE_ETHERNET		1
#define IMR_TYPE_ETHERNET_TAGGED	2

#define IMR_FLAG_CONTROLWORD		0x1

struct ifmpwreq {
	uint32_t	imr_flags;
	uint32_t	imr_type; /* pseudowire type */
	struct		shim_hdr imr_lshim; /* local label */
	struct		shim_hdr imr_rshim; /* remote label */
	struct		sockaddr_storage imr_nexthop;
};

#endif

#ifdef _KERNEL

#define MPLS_LABEL2SHIM(_l)	(htonl((_l) << MPLS_LABEL_OFFSET))
#define MPLS_SHIM2LABEL(_s)	(ntohl((_s)) >> MPLS_LABEL_OFFSET)

extern int		mpls_defttl;
extern int		mpls_mapttl_ip;
extern int		mpls_mapttl_ip6;


struct mbuf	*mpls_shim_pop(struct mbuf *);
struct mbuf	*mpls_shim_swap(struct mbuf *, struct rt_mpls *);
struct mbuf	*mpls_shim_push(struct mbuf *, struct rt_mpls *);

struct mbuf	*mpls_ip_adjttl(struct mbuf *, u_int8_t);
#ifdef INET6
struct mbuf	*mpls_ip6_adjttl(struct mbuf *, u_int8_t);
#endif

int		 mpls_output(struct ifnet *, struct mbuf *, struct sockaddr *,
		    struct rtentry *);
void		 mpls_input(struct ifnet *, struct mbuf *, struct netstack *);

#endif /* _KERNEL */
