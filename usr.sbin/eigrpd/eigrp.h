/*	$OpenBSD: eigrp.h,v 1.6 2016/09/02 16:34:20 renato Exp $ */

/*
 * Copyright (c) 2015 Renato Westphal <renato@openbsd.org>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

/* EIGRP protocol definitions */

#ifndef _EIGRP_H_
#define _EIGRP_H_

/* misc */
#define EIGRP_VERSION		2
#define IPPROTO_EIGRP		88
#define EIGRP_IP_TTL		2

/* EIGRP multicast group addresses */
#define AllEIGRPRouters_v4		"224.0.0.10"
#define AllEIGRPRouters_v6		"ff02::a"

#define EIGRP_INFINITE_METRIC	((uint32_t )(~0))

#define RTP_RTRNS_INTERVAL	5
#define RTP_RTRNS_MAX_ATTEMPTS	16

#define RTP_ACK_TIMEOUT		100000

#define EIGRP_VERSION_MAJOR	1
#define EIGRP_VERSION_MINOR	2

#define EIGRP_MIN_AS		1
#define EIGRP_MAX_AS		65535

#define DEFAULT_HELLO_INTERVAL	5
#define MIN_HELLO_INTERVAL	1
#define MAX_HELLO_INTERVAL	65535

#define DEFAULT_HELLO_HOLDTIME	15
#define MIN_HELLO_HOLDTIME	1
#define MAX_HELLO_HOLDTIME	65535

#define EIGRP_SCALING_FACTOR	256

#define DEFAULT_DELAY		10
#define MIN_DELAY		1
#define MAX_DELAY		16777215

#define DEFAULT_BANDWIDTH	100000
#define MIN_BANDWIDTH		1
#define MAX_BANDWIDTH		10000000

#define DEFAULT_RELIABILITY	255
#define MIN_RELIABILITY		1
#define MAX_RELIABILITY		255

#define DEFAULT_LOAD		1
#define MIN_LOAD		1
#define MAX_LOAD		255

#define MIN_MTU			1
#define MAX_MTU			65535

#define MIN_KVALUE		0
#define MAX_KVALUE		254

#define DEFAULT_ACTIVE_TIMEOUT	3
#define MIN_ACTIVE_TIMEOUT	0
#define MAX_ACTIVE_TIMEOUT	65535

#define DEFAULT_MAXIMUM_HOPS	100
#define MIN_MAXIMUM_HOPS	1
#define MAX_MAXIMUM_HOPS	255

#define DEFAULT_MAXIMUM_PATHS	4
#define MIN_MAXIMUM_PATHS	1
#define MAX_MAXIMUM_PATHS	32

#define DEFAULT_VARIANCE	1
#define MIN_VARIANCE		1
#define MAX_VARIANCE		128

#define EIGRP_HEADER_VERSION	2

#define EIGRP_VRID_UNICAST_AF	0x0000
#define EIGRP_VRID_MULTICAST_AF	0x0001
#define EIGRP_VRID_UNICAST_SF	0x8000

/* EIGRP packet types */
#define EIGRP_OPC_UPDATE	1
#define EIGRP_OPC_REQUEST	2
#define EIGRP_OPC_QUERY		3
#define EIGRP_OPC_REPLY		4
#define EIGRP_OPC_HELLO		5
#define EIGRP_OPC_PROBE		7
#define EIGRP_OPC_SIAQUERY	10
#define EIGRP_OPC_SIAREPLY	11

struct eigrp_hdr {
	uint8_t			version;
	uint8_t			opcode;
	uint16_t		chksum;
	uint32_t		flags;
	uint32_t		seq_num;
	uint32_t		ack_num;
	uint16_t		vrid;
	uint16_t		as;
};
/* EIGRP header flags */
#define EIGRP_HDR_FLAG_INIT	0x01
#define EIGRP_HDR_FLAG_CR	0x02
#define EIGRP_HDR_FLAG_RS	0x04
#define EIGRP_HDR_FLAG_EOT	0x08

/* TLV record */
struct tlv {
	uint16_t		type;
	uint16_t		length;
};
#define	TLV_HDR_LEN		4

struct tlv_parameter {
	uint16_t		type;
	uint16_t		length;
	uint8_t			kvalues[6];
	uint16_t		holdtime;
};

struct tlv_sw_version {
	uint16_t		type;
	uint16_t		length;
	uint8_t			vendor_os_major;
	uint8_t			vendor_os_minor;
	uint8_t			eigrp_major;
	uint8_t			eigrp_minor;
};

struct tlv_mcast_seq {
	uint16_t		type;
	uint16_t		length;
	uint32_t		seq;
};

struct classic_metric {
	uint32_t		delay;
	uint32_t		bandwidth;
	uint8_t			mtu[3]; /* 3 bytes, yeah... */
	uint8_t			hop_count;
	uint8_t			reliability;
	uint8_t			load;
	uint8_t			tag;
	uint8_t			flags;
};
#define F_METRIC_SRC_WITHDRAW	0x01
#define F_METRIC_C_DEFAULT	0x02
#define F_METRIC_ACTIVE		0x04

struct classic_emetric {
	uint32_t		routerid;
	uint32_t		as;
	uint32_t		tag;
	uint32_t		metric;
	uint16_t		reserved;
	uint8_t			protocol;
	uint8_t			flags;
};

#define EIGRP_EXT_PROTO_IGRP	1
#define EIGRP_EXT_PROTO_EIGRP	2
#define EIGRP_EXT_PROTO_STATIC	3
#define EIGRP_EXT_PROTO_RIP	4
#define EIGRP_EXT_PROTO_HELLO	5
#define EIGRP_EXT_PROTO_OSPF	6
#define EIGRP_EXT_PROTO_ISIS	7
#define EIGRP_EXT_PROTO_EGP	8
#define EIGRP_EXT_PROTO_BGP	9
#define EIGRP_EXT_PROTO_IDRP	10
#define EIGRP_EXT_PROTO_CONN	11

/* EIGRP TLV types (v1.2) */
#define TLV_PROTO_GENERAL	0x0000
#define TLV_PROTO_IPV4		0x0100
#define TLV_PROTO_IPV6		0x0400
#define TLV_PROTO_MASK		0xff00

#define TLV_ROUTE_REQUEST	0x0001
#define TLV_ROUTE_INTERNAL	0x0002
#define TLV_ROUTE_EXTERNAL	0x0003
#define TLV_ROUTE_COMMUNITY	0x0004

#define TLV_TYPE_PARAMETER	(TLV_PROTO_GENERAL | 0x0001)
#define TLV_TYPE_AUTH		(TLV_PROTO_GENERAL | 0x0002)
#define TLV_TYPE_SEQ		(TLV_PROTO_GENERAL | 0x0003)
#define TLV_TYPE_SW_VERSION	(TLV_PROTO_GENERAL | 0x0004)
#define TLV_TYPE_MCAST_SEQ	(TLV_PROTO_GENERAL | 0x0005)
#define TLV_TYPE_PEER_TERM	(TLV_PROTO_GENERAL | 0x0007)
#define TLV_TYPE_IPV4_REQUEST	(TLV_PROTO_IPV4 | TLV_ROUTE_REQUEST)
#define TLV_TYPE_IPV4_INTERNAL	(TLV_PROTO_IPV4 | TLV_ROUTE_INTERNAL)
#define TLV_TYPE_IPV4_EXTERNAL	(TLV_PROTO_IPV4 | TLV_ROUTE_EXTERNAL)
#define TLV_TYPE_IPV4_COMMUNITY	(TLV_PROTO_IPV4 | TLV_ROUTE_COMMUNITY)
#define TLV_TYPE_IPV6_REQUEST	(TLV_PROTO_IPV6 | TLV_ROUTE_REQUEST)
#define TLV_TYPE_IPV6_INTERNAL	(TLV_PROTO_IPV6 | TLV_ROUTE_INTERNAL)
#define TLV_TYPE_IPV6_EXTERNAL	(TLV_PROTO_IPV6 | TLV_ROUTE_EXTERNAL)
#define TLV_TYPE_IPV6_COMMUNITY	(TLV_PROTO_IPV6 | TLV_ROUTE_COMMUNITY)
#define TLV_TYPE_MASK		0x00ff

/* TLV lengths */
#define TLV_TYPE_PARAMETER_LEN		0x000C
#define TLV_TYPE_SW_VERSION_LEN		0x0008
#define TLV_TYPE_MCAST_SEQ_LEN		0x0008
#define TLV_TYPE_IPV4_INT_MIN_LEN	0x0019
#define TLV_TYPE_IPV6_INT_MIN_LEN	0x0025

#endif /* _EIGRP_H_ */
