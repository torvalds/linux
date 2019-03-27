/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2010-2011 Alexander V. Chernikov <melifaro@ipfw.ru>
 * Copyright (c) 2004 Gleb Smirnoff <glebius@FreeBSD.org>
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
 *	 $SourceForge: netflow.h,v 1.8 2004/09/16 17:05:11 glebius Exp $
 *	 $FreeBSD$
 */

/* netflow timeouts in seconds */

#define	ACTIVE_TIMEOUT		(30*60)	/* maximum flow lifetime is 30 min */
#define	INACTIVE_TIMEOUT	15

/*
 * More info can be found in these Cisco documents:
 *
 * Cisco IOS NetFlow, White Papers.
 * http://www.cisco.com/en/US/products/ps6601/prod_white_papers_list.html
 *
 * Cisco CNS NetFlow Collection Engine User Guide, 5.0.2, NetFlow Export
 * Datagram Formats.
 * http://www.cisco.com/en/US/products/sw/netmgtsw/ps1964/products_user_guide_chapter09186a00803f3147.html#wp26453
 *
 * Cisco Systems NetFlow Services Export Version 9
 * http://www.ietf.org/rfc/rfc3954.txt
 *
 */

#define NETFLOW_V1 1
#define NETFLOW_V5 5
#define NETFLOW_V9 9

struct netflow_v1_header
{
  uint16_t version;	/* NetFlow version */
  uint16_t count;	/* Number of records in flow */
  uint32_t sys_uptime;	/* System uptime */
  uint32_t unix_secs;	/* Current seconds since 0000 UTC 1970 */
  uint32_t unix_nsecs;	/* Remaining nanoseconds since 0000 UTC 1970 */
} __attribute__((__packed__));

struct netflow_v5_header
{
  uint16_t version;	/* NetFlow version */
  uint16_t count;	/* Number of records in flow */
  uint32_t sys_uptime;	/* System uptime */
  uint32_t unix_secs;	/* Current seconds since 0000 UTC 1970 */
  uint32_t unix_nsecs;	/* Remaining nanoseconds since 0000 UTC 1970 */
  uint32_t flow_seq;	/* Sequence number of the first record */
  uint8_t engine_type;	/* Type of flow switching engine (RP,VIP,etc.) */
  uint8_t engine_id;	/* Slot number of the flow switching engine */
  uint16_t pad;		/* Pad to word boundary */
} __attribute__((__packed__));

struct netflow_v9_header
{
  uint16_t version;	/* NetFlow version */
  uint16_t count;	/* Total number of records in packet */
  uint32_t sys_uptime;	/* System uptime */
  uint32_t unix_secs;	/* Current seconds since 0000 UTC 1970 */
  uint32_t seq_num;	/* Sequence number */
  uint32_t source_id;	/* Observation Domain id */
} __attribute__((__packed__));

struct netflow_v1_record
{
  uint32_t src_addr;	/* Source IP address */
  uint32_t dst_addr;	/* Destination IP address */
  uint32_t next_hop;	/* Next hop IP address */
  uint16_t in_ifx;	/* Source interface index */
  uint16_t out_ifx;	/* Destination interface index */
  uint32_t packets;	/* Number of packets in a flow */
  uint32_t octets;	/* Number of octets in a flow */
  uint32_t first;	/* System uptime at start of a flow */
  uint32_t last;	/* System uptime at end of a flow */
  uint16_t s_port;	/* Source port */
  uint16_t d_port;	/* Destination port */
  uint16_t pad1;	/* Pad to word boundary */
  uint8_t prot;		/* IP protocol */
  uint8_t tos;		/* IP type of service */
  uint8_t flags;	/* Cumulative OR of tcp flags */
  uint8_t pad2;		/* Pad to word boundary */
  uint16_t pad3;	/* Pad to word boundary */
  uint8_t reserved[5];	/* Reserved for future use */
} __attribute__((__packed__));

struct netflow_v5_record
{
  uint32_t src_addr;	/* Source IP address */
  uint32_t dst_addr;	/* Destination IP address */
  uint32_t next_hop;	/* Next hop IP address */
  uint16_t i_ifx;	/* Source interface index */
  uint16_t o_ifx;	/* Destination interface index */
  uint32_t packets;	/* Number of packets in a flow */
  uint32_t octets;	/* Number of octets in a flow */
  uint32_t first;	/* System uptime at start of a flow */
  uint32_t last;	/* System uptime at end of a flow */
  uint16_t s_port;	/* Source port */
  uint16_t d_port;	/* Destination port */
  uint8_t pad1;		/* Pad to word boundary */
  uint8_t flags;	/* Cumulative OR of tcp flags */
  uint8_t prot;		/* IP protocol */
  uint8_t tos;		/* IP type of service */
  uint16_t src_as;	/* Src peer/origin Autonomous System */
  uint16_t dst_as;	/* Dst peer/origin Autonomous System */
  uint8_t src_mask;	/* Source route's mask bits */
  uint8_t dst_mask;	/* Destination route's mask bits */
  uint16_t pad2;	/* Pad to word boundary */
} __attribute__((__packed__));

#define NETFLOW_V1_MAX_RECORDS 24
#define NETFLOW_V5_MAX_RECORDS 30

#define NETFLOW_V1_MAX_SIZE (sizeof(netflow_v1_header)+ \
			     sizeof(netflow_v1_record)*NETFLOW_V1_MAX_RECORDS)
#define NETFLOW_V5_MAX_SIZE (sizeof(netflow_v5_header)+ \
			     sizeof(netflow_v5_record)*NETFLOW_V5_MAX_RECORDS)

struct netflow_v5_export_dgram {
	struct netflow_v5_header	header;
	struct netflow_v5_record	r[NETFLOW_V5_MAX_RECORDS];
} __attribute__((__packed__));


/* RFC3954 field definitions */
#define NETFLOW_V9_FIELD_IN_BYTES		1	/* Input bytes count for a flow. Default 4, can be 8 */
#define NETFLOW_V9_FIELD_IN_PKTS		2	/* Incoming counter with number of packets associated with an IP Flow. Default 4 */
#define NETFLOW_V9_FIELD_FLOWS			3	/* Number of Flows that were aggregated. Default 4 */
#define NETFLOW_V9_FIELD_PROTOCOL		4	/* IP protocol byte. 1 */
#define NETFLOW_V9_FIELD_TOS			5	/* Type of service byte setting when entering the incoming interface. 1 */
#define NETFLOW_V9_FIELD_TCP_FLAGS		6	/* TCP flags; cumulative of all the TCP flags seen in this Flow. 1 */
#define NETFLOW_V9_FIELD_L4_SRC_PORT		7	/* TCP/UDP source port number. 2 */
#define NETFLOW_V9_FIELD_IPV4_SRC_ADDR		8	/* IPv4 source address. 4 */
#define NETFLOW_V9_FIELD_SRC_MASK		9	/* The number of contiguous bits in the source subnet mask (i.e., the mask in slash notation). 1 */
#define NETFLOW_V9_FIELD_INPUT_SNMP		10	/* Input interface index. Default 2 */
#define NETFLOW_V9_FIELD_L4_DST_PORT		11	/* TCP/UDP destination port number. 2 */
#define NETFLOW_V9_FIELD_IPV4_DST_ADDR		12	/* IPv4 destination address. 4 */
#define NETFLOW_V9_FIELD_DST_MASK		13	/* The number of contiguous bits in the destination subnet mask (i.e., the mask in slash notation). 1 */
#define NETFLOW_V9_FIELD_OUTPUT_SNMP		14	/* Output interface index. Default 2 */
#define NETFLOW_V9_FIELD_IPV4_NEXT_HOP		15	/* IPv4 address of the next-hop router. 4 */
#define NETFLOW_V9_FIELD_SRC_AS			16	/* Source BGP autonomous system number. Default 2, can be 4 */
#define NETFLOW_V9_FIELD_DST_AS			17	/* Destination BGP autonomous system number. Default 2, can be 4 */
#define NETFLOW_V9_FIELD_BGP_IPV4_NEXT_HOP	18	/* Next-hop router's IP address in the BGP domain. 4 */
#define NETFLOW_V9_FIELD_MUL_DST_PKTS		19	/* IP multicast outgoing packet counter for packets associated with IP flow. Default 4 */
#define NETFLOW_V9_FIELD_MUL_DST_BYTES		20	/* IP multicast outgoing Octet (byte) counter for the number of bytes associated with IP flow. Default 4 */
#define NETFLOW_V9_FIELD_LAST_SWITCHED		21	/* sysUptime in msec at which the last packet of this Flow was switched. 4 */
#define NETFLOW_V9_FIELD_FIRST_SWITCHED		22	/* sysUptime in msec at which the first packet of this Flow was switched. 4 */
#define NETFLOW_V9_FIELD_OUT_BYTES		23	/* Outgoing counter for the number of bytes associated with an IP Flow. Default 4 */
#define NETFLOW_V9_FIELD_OUT_PKTS		24	/* Outgoing counter for the number of packets associated with an IP Flow. Default 4 */
#define NETFLOW_V9_FIELD_IPV6_SRC_ADDR		27	/* IPv6 source address. 16 */
#define NETFLOW_V9_FIELD_IPV6_DST_ADDR		28	/* IPv6 destination address. 16 */
#define NETFLOW_V9_FIELD_IPV6_SRC_MASK		29	/* Length of the IPv6 source mask in contiguous bits. 1 */
#define NETFLOW_V9_FIELD_IPV6_DST_MASK		30	/* Length of the IPv6 destination mask in contiguous bits. 1 */
#define NETFLOW_V9_FIELD_IPV6_FLOW_LABEL	31	/* IPv6 flow label as per RFC 2460 definition. 3 */
#define NETFLOW_V9_FIELD_ICMP_TYPE		32	/* Internet Control Message Protocol (ICMP) packet type; reported as ICMP Type * 256 + ICMP code. 2 */
#define NETFLOW_V9_FIELD_MUL_IGMP_TYPE		33	/* Internet Group Management Protocol (IGMP) packet type. 1 */
#define NETFLOW_V9_FIELD_SAMPLING_INTERVAL	34	/* When using sampled NetFlow, the rate at which packets are sampled; for example, a value of 100 indicates that one of every hundred packets is sampled. 4 */
#define NETFLOW_V9_FIELD_SAMPLING_ALGORITHM	35	/* For sampled NetFlow platform-wide: 0x01 deterministic sampling 0x02 random sampling. 1 */
#define NETFLOW_V9_FIELD_FLOW_ACTIVE_TIMEOUT	36	/* Timeout value (in seconds) for active flow entries in the NetFlow cache. 2 */
#define NETFLOW_V9_FIELD_FLOW_INACTIVE_TIMEOUT	37	/* Timeout value (in seconds) for inactive Flow entries in the NetFlow cache. 2 */
#define NETFLOW_V9_FIELD_ENGINE_TYPE		38	/* Type of Flow switching engine (route processor, linecard, etc...). 1 */
#define NETFLOW_V9_FIELD_ENGINE_ID		39	/* ID number of the Flow switching engine. 1 */
#define NETFLOW_V9_FIELD_TOTAL_BYTES_EXP	40	/* Counter with for the number of bytes exported by the Observation Domain. Default 4 */
#define NETFLOW_V9_FIELD_TOTAL_PKTS_EXP		41	/* Counter with for the number of packets exported by the Observation Domain. Default 4 */
#define NETFLOW_V9_FIELD_TOTAL_FLOWS_EXP	42	/* Counter with for the number of flows exported by the Observation Domain. Default 4 */
#define NETFLOW_V9_FIELD_MPLS_TOP_LABEL_TYPE	46	/* MPLS Top Label Type. 1 */
#define NETFLOW_V9_FIELD_MPLS_TOP_LABEL_IP_ADDR	47	/* Forwarding Equivalent Class corresponding to the MPLS Top Label. 4 */
#define NETFLOW_V9_FIELD_FLOW_SAMPLER_ID	48	/* Identifier shown in "show flow-sampler". 1 */
#define NETFLOW_V9_FIELD_FLOW_SAMPLER_MODE	49	/* The type of algorithm used for sampling data. 2 */
#define NETFLOW_V9_FIELD_FLOW_SAMPLER_RANDOM_INTERVAL		50	/* Packet interval at which to sample. 4. */
#define NETFLOW_V9_FIELD_DST_TOS		55	/* Type of Service byte setting when exiting outgoing interface. 1. */
#define NETFLOW_V9_FIELD_SRC_MAC		56	/* Source MAC Address. 6 */
#define NETFLOW_V9_FIELD_DST_MAC		57	/* Destination MAC Address. 6 */
#define NETFLOW_V9_FIELD_SRC_VLAN		58	/* Virtual LAN identifier associated with ingress interface. 2 */
#define NETFLOW_V9_FIELD_DST_VLAN		59	/* Virtual LAN identifier associated with egress interface. 2 */
#define NETFLOW_V9_FIELD_IP_PROTOCOL_VERSION	60	/* Internet Protocol Version. Set to 4 for IPv4, set to 6 for IPv6. If not present in the template, then version 4 is assumed. 1. */
#define NETFLOW_V9_FIELD_DIRECTION		61	/* Flow direction: 0 - ingress flow 1 - egress flow. 1 */
#define NETFLOW_V9_FIELD_IPV6_NEXT_HOP		62	/* IPv6 address of the next-hop router. 16 */
#define NETFLOW_V9_FIELD_BGP_IPV6_NEXT_HOP	63	/* Next-hop router in the BGP domain. 16 */
#define NETFLOW_V9_FIELD_IPV6_OPTION_HEADERS	64	/* Bit-encoded field identifying IPv6 option headers found in the flow */
#define NETFLOW_V9_FIELD_MPLS_LABEL_1		70	/* MPLS label at position 1 in the stack. 3 */
#define NETFLOW_V9_FIELD_MPLS_LABEL_2		71	/* MPLS label at position 2 in the stack. 3 */
#define NETFLOW_V9_FIELD_MPLS_LABEL_3		72	/* MPLS label at position 3 in the stack. 3 */
#define NETFLOW_V9_FIELD_MPLS_LABEL_4		73	/* MPLS label at position 4 in the stack. 3 */
#define NETFLOW_V9_FIELD_MPLS_LABEL_5		74	/* MPLS label at position 5 in the stack. 3 */
#define NETFLOW_V9_FIELD_MPLS_LABEL_6		75	/* MPLS label at position 6 in the stack. 3 */
#define NETFLOW_V9_FIELD_MPLS_LABEL_7		76	/* MPLS label at position 7 in the stack. 3 */
#define NETFLOW_V9_FIELD_MPLS_LABEL_8		77	/* MPLS label at position 8 in the stack. 3 */
#define NETFLOW_V9_FIELD_MPLS_LABEL_9		78	/* MPLS label at position 9 in the stack. 3 */
#define NETFLOW_V9_FIELD_MPLS_LABEL_10		79	/* MPLS label at position 10 in the stack. 3 */

#define NETFLOW_V9_MAX_RESERVED_FLOWSET		0xFF	/* Clause 5.2 */
