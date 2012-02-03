/*
 * Copyright (C) 1999-2011, Broadcom Corporation
 * 
 *         Unless you and Broadcom execute a separate written software license
 * agreement governing use of this software, this software is licensed to you
 * under the terms of the GNU General Public License version 2 (the "GPL"),
 * available at http://www.broadcom.com/licenses/GPLv2.php, with the
 * following added to such license:
 * 
 *      As a special exception, the copyright holders of this software give you
 * permission to link this software with independent modules, and to copy and
 * distribute the resulting executable under terms of your choice, provided that
 * you also meet, for each linked independent module, the terms and conditions of
 * the license of that module.  An independent module is a module which is not
 * derived from this software.  The special exception does not apply to any
 * modifications of the software.
 * 
 *      Notwithstanding the above, under no circumstances may you combine this
 * software in any way with any other Broadcom software provided under a license
 * other than the GPL, without Broadcom's express prior written consent.
 *
 * Fundamental constants relating to IP Protocol
 *
 * $Id: bcmip.h,v 9.19 2009/11/10 20:08:33 Exp $
 */

#ifndef _bcmip_h_
#define _bcmip_h_

#ifndef _TYPEDEFS_H_
#include <typedefs.h>
#endif

/* This marks the start of a packed structure section. */
#include <packed_section_start.h>


/* IPV4 and IPV6 common */
#define IP_VER_OFFSET		0x0	/* offset to version field */
#define IP_VER_MASK		0xf0	/* version mask */
#define IP_VER_SHIFT		4	/* version shift */
#define IP_VER_4		4	/* version number for IPV4 */
#define IP_VER_6		6	/* version number for IPV6 */

#define IP_VER(ip_body) \
	((((uint8 *)(ip_body))[IP_VER_OFFSET] & IP_VER_MASK) >> IP_VER_SHIFT)

#define IP_PROT_ICMP		0x1	/* ICMP protocol */
#define IP_PROT_TCP		0x6	/* TCP protocol */
#define IP_PROT_UDP		0x11	/* UDP protocol type */

/* IPV4 field offsets */
#define IPV4_VER_HL_OFFSET	0	/* version and ihl byte offset */
#define IPV4_TOS_OFFSET		1	/* type of service offset */
#define IPV4_PKTLEN_OFFSET	2	/* packet length offset */
#define IPV4_PKTFLAG_OFFSET	6	/* more-frag,dont-frag flag offset */
#define IPV4_PROT_OFFSET	9	/* protocol type offset */
#define IPV4_CHKSUM_OFFSET	10	/* IP header checksum offset */
#define IPV4_SRC_IP_OFFSET	12	/* src IP addr offset */
#define IPV4_DEST_IP_OFFSET	16	/* dest IP addr offset */
#define IPV4_OPTIONS_OFFSET	20	/* IP options offset */

/* IPV4 field decodes */
#define IPV4_VER_MASK		0xf0	/* IPV4 version mask */
#define IPV4_VER_SHIFT		4	/* IPV4 version shift */

#define IPV4_HLEN_MASK		0x0f	/* IPV4 header length mask */
#define IPV4_HLEN(ipv4_body)	(4 * (((uint8 *)(ipv4_body))[IPV4_VER_HL_OFFSET] & IPV4_HLEN_MASK))

#define IPV4_ADDR_LEN		4	/* IPV4 address length */

#define IPV4_ADDR_NULL(a)	((((uint8 *)(a))[0] | ((uint8 *)(a))[1] | \
				  ((uint8 *)(a))[2] | ((uint8 *)(a))[3]) == 0)

#define IPV4_ADDR_BCAST(a)	((((uint8 *)(a))[0] & ((uint8 *)(a))[1] & \
				  ((uint8 *)(a))[2] & ((uint8 *)(a))[3]) == 0xff)

#define	IPV4_TOS_DSCP_MASK	0xfc	/* DiffServ codepoint mask */
#define	IPV4_TOS_DSCP_SHIFT	2	/* DiffServ codepoint shift */

#define	IPV4_TOS(ipv4_body)	(((uint8 *)(ipv4_body))[IPV4_TOS_OFFSET])

#define	IPV4_TOS_PREC_MASK	0xe0	/* Historical precedence mask */
#define	IPV4_TOS_PREC_SHIFT	5	/* Historical precedence shift */

#define IPV4_TOS_LOWDELAY	0x10	/* Lowest delay requested */
#define IPV4_TOS_THROUGHPUT	0x8	/* Best throughput requested */
#define IPV4_TOS_RELIABILITY	0x4	/* Most reliable delivery requested */

#define IPV4_PROT(ipv4_body)	(((uint8 *)(ipv4_body))[IPV4_PROT_OFFSET])

#define IPV4_FRAG_RESV		0x8000	/* Reserved */
#define IPV4_FRAG_DONT		0x4000	/* Don't fragment */
#define IPV4_FRAG_MORE		0x2000	/* More fragments */
#define IPV4_FRAG_OFFSET_MASK	0x1fff	/* Fragment offset */

#define IPV4_ADDR_STR_LEN	16	/* Max IP address length in string format */

/* IPV4 packet formats */
BWL_PRE_PACKED_STRUCT struct ipv4_addr {
	uint8	addr[IPV4_ADDR_LEN];
} BWL_POST_PACKED_STRUCT;

BWL_PRE_PACKED_STRUCT struct ipv4_hdr {
	uint8	version_ihl;		/* Version and Internet Header Length */
	uint8	tos;			/* Type Of Service */
	uint16	tot_len;		/* Number of bytes in packet (max 65535) */
	uint16	id;
	uint16	frag;			/* 3 flag bits and fragment offset */
	uint8	ttl;			/* Time To Live */
	uint8	prot;			/* Protocol */
	uint16	hdr_chksum;		/* IP header checksum */
	uint8	src_ip[IPV4_ADDR_LEN];	/* Source IP Address */
	uint8	dst_ip[IPV4_ADDR_LEN];	/* Destination IP Address */
} BWL_POST_PACKED_STRUCT;

/* IPV6 field offsets */
#define IPV6_PAYLOAD_LEN_OFFSET	4	/* payload length offset */
#define IPV6_NEXT_HDR_OFFSET	6	/* next header/protocol offset */
#define IPV6_HOP_LIMIT_OFFSET	7	/* hop limit offset */
#define IPV6_SRC_IP_OFFSET	8	/* src IP addr offset */
#define IPV6_DEST_IP_OFFSET	24	/* dst IP addr offset */

/* IPV6 field decodes */
#define IPV6_TRAFFIC_CLASS(ipv6_body) \
	(((((uint8 *)(ipv6_body))[0] & 0x0f) << 4) | \
	 ((((uint8 *)(ipv6_body))[1] & 0xf0) >> 4))

#define IPV6_FLOW_LABEL(ipv6_body) \
	(((((uint8 *)(ipv6_body))[1] & 0x0f) << 16) | \
	 (((uint8 *)(ipv6_body))[2] << 8) | \
	 (((uint8 *)(ipv6_body))[3]))

#define IPV6_PAYLOAD_LEN(ipv6_body) \
	((((uint8 *)(ipv6_body))[IPV6_PAYLOAD_LEN_OFFSET + 0] << 8) | \
	 ((uint8 *)(ipv6_body))[IPV6_PAYLOAD_LEN_OFFSET + 1])

#define IPV6_NEXT_HDR(ipv6_body) \
	(((uint8 *)(ipv6_body))[IPV6_NEXT_HDR_OFFSET])

#define IPV6_PROT(ipv6_body)	IPV6_NEXT_HDR(ipv6_body)

#define IPV6_ADDR_LEN		16	/* IPV6 address length */

/* IPV4 TOS or IPV6 Traffic Classifier or 0 */
#define IP_TOS46(ip_body) \
	(IP_VER(ip_body) == IP_VER_4 ? IPV4_TOS(ip_body) : \
	 IP_VER(ip_body) == IP_VER_6 ? IPV6_TRAFFIC_CLASS(ip_body) : 0)

/* This marks the end of a packed structure section. */
#include <packed_section_end.h>

#endif	/* _bcmip_h_ */
