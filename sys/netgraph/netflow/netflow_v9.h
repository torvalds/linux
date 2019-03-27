/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2010 Alexander V. Chernikov <melifaro@ipfw.ru>
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
 *	 $FreeBSD$
 */

#ifndef	_NETFLOW_V9_H_
#define	_NETFLOW_V9_H_

#ifdef COUNTERS_64
#define CNTR		uint64_t
#define CNTR_MAX	UINT64_MAX
#else
#define CNTR		uint32_t
#define CNTR_MAX	UINT_MAX
#endif

struct netflow_v9_template
{
	int	field_id;
	int	field_length;
};

/* Template ID for tcp/udp v4 streams ID:257 (0x100 + NETFLOW_V9_FLOW_V4_L4) */
struct netflow_v9_record_ipv4_tcp
{
	uint32_t	src_addr;	/* Source IPv4 address (IPV4_SRC_ADDR) */
	uint32_t	dst_addr;	/* Destination IPv4 address (IPV4_DST_ADDR) */
	uint32_t	next_hop;	/* Next hop IPv4 address (IPV4_NEXT_HOP) */
	uint16_t	i_ifx;	/* Source interface index (INPUT_SNMP) */
	uint16_t	o_ifx;	/* Destination interface index (OUTPUT_SNMP) */
	CNTR		i_packets;	/* Number of incoming packets in a flow (IN_PKTS) */
	CNTR		i_octets;	/* Number of incoming octets in a flow (IN_BYTES) */
	CNTR		o_packets;	/* Number of outgoing packets in a flow (OUT_PKTS) */
	CNTR		o_octets;	/* Number of outgoing octets in a flow (OUT_BYTES) */
	uint32_t	first;	/* System uptime at start of a flow (FIRST_SWITCHED) */
	uint32_t	last;	/* System uptime at end of a flow (LAST_SWITCHED) */
	uint16_t	s_port;	/* Source port (L4_SRC_PORT) */
	uint16_t	d_port;	/* Destination port (L4_DST_PORT) */
	uint8_t		flags;	/* Cumulative OR of tcp flags (TCP_FLAGS) */
	uint8_t		prot;		/* IP protocol */
	uint8_t		tos;		/* IP type of service IN (or OUT) (TOS) */
	uint32_t	src_as;	/* Src peer/origin Autonomous System (SRC_AS) */
	uint32_t	dst_as;	/* Dst peer/origin Autonomous System (DST_AS) */
	uint8_t		src_mask;	/* Source route's mask bits (SRC_MASK) */
	uint8_t		dst_mask; 	/* Destination route's mask bits (DST_MASK) */
} __attribute__((__packed__));

/* Template ID for tcp/udp v6 streams ID: 260 (0x100 + NETFLOW_V9_FLOW_V6_L4) */
struct netflow_v9_record_ipv6_tcp
{
	struct in6_addr	src_addr;	/* Source IPv6 address (IPV6_SRC_ADDR) */
	struct in6_addr	dst_addr;	/* Destination IPv6 address (IPV6_DST_ADDR) */
	struct in6_addr	next_hop;	/* Next hop IPv6 address (IPV6_NEXT_HOP) */
	uint16_t	i_ifx;	/* Source interface index (INPUT_SNMP) */
	uint16_t	o_ifx;	/* Destination interface index (OUTPUT_SNMP) */
	CNTR		i_packets;	/* Number of incoming packets in a flow (IN_PKTS) */
	CNTR		i_octets;	/* Number of incoming octets in a flow (IN_BYTES) */
	CNTR		o_packets;	/* Number of outgoing packets in a flow (OUT_PKTS) */
	CNTR		o_octets;	/* Number of outgoing octets in a flow (OUT_BYTES) */
	uint32_t	first;	/* System uptime at start of a flow (FIRST_SWITCHED) */
	uint32_t	last;	/* System uptime at end of a flow (LAST_SWITCHED) */
	uint16_t	s_port;	/* Source port (L4_SRC_PORT) */
	uint16_t	d_port;	/* Destination port (L4_DST_PORT) */
	uint8_t		flags;	/* Cumulative OR of tcp flags (TCP_FLAGS) */
	uint8_t		prot;		/* IP protocol */
	uint8_t		tos;		/* IP type of service IN (or OUT) (TOS) */
	uint32_t	src_as;	/* Src peer/origin Autonomous System (SRC_AS) */
	uint32_t	dst_as;	/* Dst peer/origin Autonomous System (DST_AS) */
	uint8_t		src_mask;	/* Source route's mask bits (SRC_MASK) */
	uint8_t		dst_mask; 	/* Destination route's mask bits (DST_MASK) */
} __attribute__((__packed__));

/* Used in export9_add to determine max record size */
struct netflow_v9_record_general
{
	union {
		struct netflow_v9_record_ipv4_tcp v4_tcp;
		struct netflow_v9_record_ipv6_tcp v6_tcp;
	} rec;
};

#define BASE_MTU	1500
#define MIN_MTU		sizeof(struct netflow_v5_header)
#define MAX_MTU		16384
#define NETFLOW_V9_MAX_SIZE	_NETFLOW_V9_MAX_SIZE(BASE_MTU)
/* Decrease MSS by 16 since there can be some IPv[46] header options */
#define _NETFLOW_V9_MAX_SIZE(x)	(x) - sizeof(struct ip6_hdr) - sizeof(struct udphdr) - 16

/* #define NETFLOW_V9_MAX_FLOWSETS	2 */

#define NETFLOW_V9_MAX_RECORD_SIZE	sizeof(struct netflow_v9_record_ipv6_tcp)
#define NETFLOW_V9_MAX_PACKETS_TEMPL	500	/* Send data templates every ... packets */
#define NETFLOW_V9_MAX_TIME_TEMPL	600	/* Send data templates every ... seconds */
#define NETFLOW_V9_MAX_TEMPLATES	16	/* Not a real value */
#define _NETFLOW_V9_TEMPLATE_SIZE(x)	(sizeof(x) / sizeof(struct netflow_v9_template)) * 4
//#define _NETFLOW_V9_TEMPLATE_SIZE(x)	((x) + 1) * 4

/* Flow Templates */
#define NETFLOW_V9_FLOW_V4_L4	1 /* IPv4 TCP/UDP packet */
#define NETFLOW_V9_FLOW_V4_ICMP	2 /* IPv4 ICMP packet, currently unused */
#define NETFLOW_V9_FLOW_V4_L3	3 /* IPv4 IP packet */
#define NETFLOW_V9_FLOW_V6_L4	4 /* IPv6 TCP/UDP packet */
#define NETFLOW_V9_FLOW_V6_ICMP	5 /* IPv6 ICMP packet, currently unused */
#define NETFLOW_V9_FLOW_V6_L3	6 /* IPv6 IP packet */

#define NETFLOW_V9_FLOW_FAKE	65535 /* Not uset used in real flowsets! */

struct netflow_v9_export_dgram {
	struct netflow_v9_header	header;
	char				*data; /* MTU can change, record length is dynamic */
};

struct netflow_v9_flowset_header {
	uint16_t	id; /* FlowSet id */
	uint16_t	length; /* FlowSet length */
} __attribute__((__packed__));

struct netflow_v9_packet_opt {
	uint16_t	length; /* current packet length */
	uint16_t	count; /* current records count */
	uint16_t	mtu; /* max MTU shapshot */
	uint16_t	flow_type; /* current flowset */
	uint16_t	flow_header; /* offset pointing to current flow header */
};
#endif
