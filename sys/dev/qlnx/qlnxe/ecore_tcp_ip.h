/*
 * Copyright (c) 2018-2019 Cavium, Inc.
 * All rights reserved.
 *
 *  Redistribution and use in source and binary forms, with or without
 *  modification, are permitted provided that the following conditions
 *  are met:
 *
 *  1. Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 *  2. Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in the
 *     documentation and/or other materials provided with the distribution.
 *
 *  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 *  AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 *  IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 *  ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 *  LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 *  CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 *  SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 *  INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 *  CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 *  ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 *  POSSIBILITY OF SUCH DAMAGE.
 *
 * $FreeBSD$
 */

#ifndef __ECORE_TCP_IP_H
#define __ECORE_TCP_IP_H

#define VLAN_VID_MASK	0x0fff /* VLAN Identifier */
#define ETH_P_8021Q	0x8100          /* 802.1Q VLAN Extended Header  */
#define ETH_P_8021AD	0x88A8          /* 802.1ad Service VLAN		*/
#define ETH_P_IPV6	0x86DD		/* IPv6 over bluebook		*/
#define ETH_P_IP	0x0800		/* Internet Protocol packet	*/
#define ETH_HLEN	14		/* Total octets in header.	 */
#define VLAN_HLEN       4               /* additional bytes required by VLAN */
#define MAX_VLAN_PRIO	7	/* Max vlan priority value in 801.1Q tag */

#define MAX_DSCP		63 /* Max DSCP value in IP header */
#define IPPROTO_TCP	6

#ifndef htonl
#define htonl(val) OSAL_CPU_TO_BE32(val)
#endif

#ifndef ntohl
#define ntohl(val) OSAL_BE32_TO_CPU(val)
#endif

#ifndef htons
#define htons(val) OSAL_CPU_TO_BE16(val)
#endif

#ifndef ntohs
#define ntohs(val) OSAL_BE16_TO_CPU(val)
#endif


struct ecore_ethhdr {
	unsigned char	h_dest[ETH_ALEN];	/* destination eth addr	*/
	unsigned char	h_source[ETH_ALEN];	/* source ether addr	*/
	u16		h_proto;		/* packet type ID field	*/
};

struct ecore_iphdr {
	u8	ihl:4,
		version:4;
	u8	tos;
	u16	tot_len;
	u16	id;
	u16	frag_off;
	u8	ttl;
	u8	protocol;
	u16	check;
	u32	saddr;
	u32	daddr;
	/*The options start here. */
};

struct ecore_vlan_ethhdr {
	unsigned char	h_dest[ETH_ALEN];
	unsigned char	h_source[ETH_ALEN];
	u16		h_vlan_proto;
	u16		h_vlan_TCI;
	u16		h_vlan_encapsulated_proto;
};

struct ecore_in6_addr {
	union {
		u8		u6_addr8[16];
		u16		u6_addr16[8];
		u32		u6_addr32[4];
	} in6_u;
};

struct ecore_ipv6hdr {
	u8			priority:4,
				version:4;
	u8			flow_lbl[3];

	u16			payload_len;
	u8			nexthdr;
	u8			hop_limit;

	struct	ecore_in6_addr	saddr;
	struct	ecore_in6_addr	daddr;
};

struct ecore_tcphdr {
	u16	source;
	u16	dest;
	u32	seq;
	u32	ack_seq;
	u16	res1:4,
		doff:4,
		fin:1,
		syn:1,
		rst:1,
		psh:1,
		ack:1,
		urg:1,
		ece:1,
		cwr:1;
	u16	window;
	u16	check;
	u16	urg_ptr;
};

enum {
	INET_ECN_NOT_ECT = 0,
	INET_ECN_ECT_1 = 1,
	INET_ECN_ECT_0 = 2,
	INET_ECN_CE = 3,
	INET_ECN_MASK = 3,
};

#endif
