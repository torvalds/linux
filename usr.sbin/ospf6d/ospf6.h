/*	$OpenBSD: ospf6.h,v 1.22 2023/06/21 09:47:03 sthen Exp $ */

/*
 * Copyright (c) 2004, 2005, 2007 Esben Norby <norby@openbsd.org>
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

/* OSPF protocol definitions */

#ifndef _OSPF_H_
#define _OSPF_H_

#include <netinet/in.h>
#include <stddef.h>

/* misc */
#define OSPF6_VERSION		3
#define IPPROTO_OSPF		89
#define AllSPFRouters		"ff02::5"
#define AllDRouters		"ff02::6"

#define PACKET_HDR		100 /* XXX used to calculate the IP payload */

#define DEFAULT_METRIC		10
#define DEFAULT_REDIST_METRIC	100
#define MIN_METRIC		1
#define MAX_METRIC		65535	/* sum & as-ext lsa use 24bit metrics */

#define DEFAULT_PRIORITY	1
#define MIN_PRIORITY		0
#define MAX_PRIORITY		255

#define DEFAULT_HELLO_INTERVAL	10
#define MIN_HELLO_INTERVAL	1
#define MAX_HELLO_INTERVAL	65535

#define DEFAULT_RTR_DEAD_TIME	40
#define MIN_RTR_DEAD_TIME	2
#define MAX_RTR_DEAD_TIME	65535

#define DEFAULT_RXMT_INTERVAL	5
#define MIN_RXMT_INTERVAL	5
#define MAX_RXMT_INTERVAL	3600

#define DEFAULT_TRANSMIT_DELAY	1
#define MIN_TRANSMIT_DELAY	1
#define MAX_TRANSMIT_DELAY	3600

#define DEFAULT_ADJ_TMOUT	120

#define DEFAULT_NBR_TMOUT	86400	/* 24 hours */

#define DEFAULT_SPF_DELAY	1
#define MIN_SPF_DELAY		1
#define MAX_SPF_DELAY		10

#define DEFAULT_SPF_HOLDTIME	5
#define MIN_SPF_HOLDTIME	1
#define MAX_SPF_HOLDTIME	5

/* msec */
#define KR_RELOAD_TIMER		250
#define KR_RELOAD_HOLD_TIMER	5000

#define MIN_MD_ID		0
#define MAX_MD_ID		255

#define DEFAULT_INSTANCE_ID	0
#define MIN_INSTANCE_ID	0
#define MAX_INSTANCE_ID	0

/* OSPF compatibility flags */
#define OSPF_OPTION_V6		0x01
#define OSPF_OPTION_E		0x02
#define OSPF_OPTION_x		0x04	/* ignored, should be 0 */
#define OSPF_OPTION_N		0x08
#define OSPF_OPTION_R		0x10
#define OSPF_OPTION_DC		0x20

/* OSPF packet types */
#define PACKET_TYPE_HELLO	1
#define PACKET_TYPE_DD		2
#define PACKET_TYPE_LS_REQUEST	3
#define PACKET_TYPE_LS_UPDATE	4
#define PACKET_TYPE_LS_ACK	5

/* LSA */
#define LS_REFRESH_TIME		1800
#define MIN_LS_INTERVAL		5
#define MIN_LS_ARRIVAL		1
#define DEFAULT_AGE		0
#define MAX_AGE			3600
#define CHECK_AGE		300
#define MAX_AGE_DIFF		900
#define LS_INFINITY		0xffffff
#define RESV_SEQ_NUM		0x80000000U	/* reserved and "unused" */
#define INIT_SEQ_NUM		0x80000001U
#define MAX_SEQ_NUM		0x7fffffffU

/* OSPF header */
struct ospf_hdr {
	u_int8_t		version;
	u_int8_t		type;
	u_int16_t		len;
	u_int32_t		rtr_id;
	u_int32_t		area_id;
	u_int16_t		chksum;
	u_int8_t		instance;
	u_int8_t		zero;		/* must be zero */
};

/* Hello header (type 1) */
struct hello_hdr {
	u_int32_t		iface_id;
	u_int32_t		opts;	/* 8bit rtr_priority + 24bits options */
	u_int16_t		hello_interval;
	u_int16_t		rtr_dead_interval;
	u_int32_t		d_rtr;
	u_int32_t		bd_rtr;
};

/* Database Description header (type 2) */
struct db_dscrp_hdr {
	u_int32_t		opts;
	u_int16_t		iface_mtu;
	u_int8_t		zero;		/* must be zero */
	u_int8_t		bits;
	u_int32_t		dd_seq_num;
};

#define OSPF_DBD_MS		0x01
#define OSPF_DBD_M		0x02
#define OSPF_DBD_I		0x04

/*  Link State Request header (type 3) */
struct ls_req_hdr {
	u_int16_t		zero;
	u_int16_t		type;
	u_int32_t		ls_id;
	u_int32_t		adv_rtr;
};

/* Link State Update header (type 4) */
struct ls_upd_hdr {
	u_int32_t		num_lsa;
};

#define LSA_TYPE_LINK		0x0008
#define	LSA_TYPE_ROUTER		0x2001
#define LSA_TYPE_NETWORK	0x2002
#define LSA_TYPE_INTER_A_PREFIX	0x2003
#define LSA_TYPE_INTER_A_ROUTER	0x2004
#define LSA_TYPE_INTRA_A_PREFIX	0x2009
#define	LSA_TYPE_EXTERNAL	0x4005

#define LSA_TYPE_FLAG_U		0x8000
#define LSA_TYPE_FLAG_S2	0x4000
#define LSA_TYPE_FLAG_S1	0x2000
#define LSA_TYPE_SCOPE_MASK	0x6000

#define LSA_IS_SCOPE_LLOCAL(x)	\
    (((x) & LSA_TYPE_SCOPE_MASK) == 0)
#define LSA_IS_SCOPE_AREA(x)	\
    (((x) & LSA_TYPE_SCOPE_MASK) == LSA_TYPE_FLAG_S1)
#define LSA_IS_SCOPE_AS(x)	\
    (((x) & LSA_TYPE_SCOPE_MASK) == LSA_TYPE_FLAG_S2)

#define LINK_TYPE_POINTTOPOINT	1
#define LINK_TYPE_TRANSIT_NET	2
#define LINK_TYPE_RESERVED	3
#define LINK_TYPE_VIRTUAL	4

/* for some reason they thought 24bit types are fun, make them less a hazard */
#define LSA_24_MASK 0xffffff
#define LSA_24_GETHI(x)		\
	((x) >> 24)
#define LSA_24_GETLO(x)		\
	((x) & LSA_24_MASK)
#define LSA_24_SETHI(x, y)	\
	((x) = ((x) & LSA_24_MASK) | (((y) & 0xff) << 24))
#define LSA_24_SETLO(x, y)	\
	((x) = ((y) & LSA_24_MASK) | ((x) & ~LSA_24_MASK))

/* LSA headers */
#define LSA_METRIC_MASK		0x00ffffff	/* only for sum & as-ext */
#define LSA_ASEXT_E_FLAG	0x04000000
#define LSA_ASEXT_F_FLAG	0x02000000
#define LSA_ASEXT_T_FLAG	0x01000000

#define OSPF_RTR_B		0x01
#define OSPF_RTR_E		0x02
#define OSPF_RTR_V		0x04
#define OSPF_RTR_x		0x08	/* ignored, should be 0 */
#define OSPF_RTR_Nt		0x10

#define OSPF_PREFIX_NU		0x01
#define OSPF_PREFIX_LA		0x02
#define OSPF_PREFIX_x		0x04	/* ignored, should be 0 */
#define OSPF_PREFIX_P		0x08
#define OSPF_PREFIX_DN		0x10

/* return encoded IPv6 prefix size in bytes depending on the prefixlen */
#define LSA_PREFIXSIZE(x)	(((x) + 31)/32 * 4)

struct lsa_prefix {
	u_int8_t		prefixlen;
	u_int8_t		options;
	u_int16_t		metric;	/* Ref. LS type in AS-Ext-LSA */
	/* + an IPv6 prefix encoded in (prefixlen + 31)/32 words */
};

struct lsa_rtr {
	u_int32_t		opts;	/* 8bit flags + 24bits options */
};

struct lsa_rtr_link {
	u_int8_t		type;
	u_int8_t		dummy;
	u_int16_t		metric;
	u_int32_t		iface_id;
	u_int32_t		nbr_iface_id;
	u_int32_t		nbr_rtr_id;
};

struct lsa_net {
	u_int32_t		opts;	/* 24bits options */
};

struct lsa_net_link {
	u_int32_t		att_rtr;
};

struct lsa_prefix_sum {
	u_int32_t		metric;		/* only lower 24 bit */
	/* + one prefix */
};

struct lsa_rtr_sum {
	u_int32_t		opts;		/* lower 24bit options */
	u_int32_t		metric;		/* only lower 24 bit */
	u_int32_t		dest_rtr_id;
};

struct lsa_asext {
	u_int32_t		metric;		/* lower 24 bit plus EFT bits */
	struct lsa_prefix	prefix;
	/* + if F bit is set, an optional forwarding address (128bit) */
	/* + if T bit is set, an optional external route tag (32bit) */
	/* + if referenced LS type (i.e. prefix.metric) is non-zero, an
	 *   optional referenced LS ID (32bit) -- not used by OSPF itself */
};

struct lsa_link {
	u_int32_t		opts;		/* rtr pri & 24bit options */
	struct in6_addr		lladdr;
	u_int32_t		numprefix;
	/* + numprefix * lsa_prefix */
};

struct lsa_intra_prefix {
	u_int16_t		numprefix;
	u_int16_t		ref_type;
	u_int32_t		ref_ls_id;
	u_int32_t		ref_adv_rtr;
	/* + numprefix * lsa_prefix */
};

struct lsa_hdr {
	u_int16_t		age;
	u_int16_t		type;
	u_int32_t		ls_id;
	u_int32_t		adv_rtr;
	u_int32_t		seq_num;
	u_int16_t		ls_chksum;
	u_int16_t		len;
};

#define LS_CKSUM_OFFSET	offsetof(struct lsa_hdr, ls_chksum)

struct lsa {
	struct lsa_hdr		hdr;
	union {
		struct lsa_rtr		rtr;
		struct lsa_net		net;
		struct lsa_prefix_sum	pref_sum;
		struct lsa_rtr_sum	rtr_sum;
		struct lsa_asext	asext;
		struct lsa_link		link;
		struct lsa_intra_prefix pref_intra;
	}			data;
};

#endif /* !_OSPF_H_ */
