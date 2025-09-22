/*	$OpenBSD: ospf.h,v 1.23 2013/01/17 09:14:15 markus Exp $ */

/*
 * Copyright (c) 2004, 2005 Esben Norby <norby@openbsd.org>
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
#define OSPF_VERSION		2
#define IPPROTO_OSPF		89
#define AllSPFRouters		"224.0.0.5"
#define AllDRouters		"224.0.0.6"

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

/* msec */
#define DEFAULT_FAST_INTERVAL	333
#define MIN_FAST_INTERVAL	50
#define MAX_FAST_INTERVAL	333

#define DEFAULT_RTR_DEAD_TIME	40
#define FAST_RTR_DEAD_TIME	1
#define MIN_RTR_DEAD_TIME	2
#define MAX_RTR_DEAD_TIME	2147483647

#define DEFAULT_RXMT_INTERVAL	5
#define MIN_RXMT_INTERVAL	5
#define MAX_RXMT_INTERVAL	3600

#define DEFAULT_TRANSMIT_DELAY	1
#define MIN_TRANSMIT_DELAY	1
#define MAX_TRANSMIT_DELAY	3600

#define DEFAULT_ADJ_TMOUT	120

#define DEFAULT_NBR_TMOUT	86400	/* 24 hours */

/* msec */
#define DEFAULT_SPF_DELAY	1000
#define MIN_SPF_DELAY		10
#define MAX_SPF_DELAY		10000

/* msec */
#define DEFAULT_SPF_HOLDTIME	5000
#define MIN_SPF_HOLDTIME	10
#define MAX_SPF_HOLDTIME	5000

/* msec */
#define KR_RELOAD_TIMER		250
#define KR_RELOAD_HOLD_TIMER	5000

#define MIN_MD_ID		0
#define MAX_MD_ID		255

#define MAX_SIMPLE_AUTH_LEN	8

/* OSPF compatibility flags */
#define OSPF_OPTION_MT		0x01
#define OSPF_OPTION_E		0x02
#define OSPF_OPTION_MC		0x04
#define OSPF_OPTION_NP		0x08
#define OSPF_OPTION_EA		0x10
#define OSPF_OPTION_DC		0x20
#define OSPF_OPTION_O		0x40	/* only on DD options */
#define OSPF_OPTION_DN		0x80	/* only on LSA options */

/* OSPF packet types */
#define PACKET_TYPE_HELLO	1
#define PACKET_TYPE_DD		2
#define PACKET_TYPE_LS_REQUEST	3
#define PACKET_TYPE_LS_UPDATE	4
#define PACKET_TYPE_LS_ACK	5

/* OSPF auth types */
#define	AUTH_TYPE_NONE		0
#define AUTH_TYPE_SIMPLE	1
#define	AUTH_TYPE_CRYPT		2

#define MIN_AUTHTYPE		0
#define MAX_AUTHTYPE		2

/* LSA */
#define LS_REFRESH_TIME		1800
#define MIN_LS_INTERVAL		5
#define MIN_LS_ARRIVAL		1
#define DEFAULT_AGE		0
#define MAX_AGE			3600
#define CHECK_AGE		300
#define MAX_AGE_DIFF		900
#define LS_INFINITY		0xffffff
#define RESV_SEQ_NUM		0x80000000	/* reserved and "unused" */
#define INIT_SEQ_NUM		0x80000001
#define MAX_SEQ_NUM		0x7fffffff

/* OSPF header */
struct crypt {
	u_int16_t		dummy;
	u_int8_t		keyid;
	u_int8_t		len;
	u_int32_t		seq_num;
};

struct ospf_hdr {
	u_int8_t		version;
	u_int8_t		type;
	u_int16_t		len;
	u_int32_t		rtr_id;
	u_int32_t		area_id;
	u_int16_t		chksum;
	u_int16_t		auth_type;
	union {
		char		simple[MAX_SIMPLE_AUTH_LEN];
		struct crypt	crypt;
	} auth_key;
};

/* Hello header (type 1) */
struct hello_hdr {
	u_int32_t		mask;
	u_int16_t		hello_interval;
	u_int8_t		opts;
	u_int8_t		rtr_priority;
	u_int32_t		rtr_dead_interval;
	u_int32_t		d_rtr;
	u_int32_t		bd_rtr;
};

/* Database Description header (type 2) */
struct db_dscrp_hdr {
	u_int16_t		iface_mtu;
	u_int8_t		opts;
	u_int8_t		bits;
	u_int32_t		dd_seq_num;
};

#define OSPF_DBD_MS		0x01
#define OSPF_DBD_M		0x02
#define OSPF_DBD_I		0x04

/*  Link State Request header (type 3) */
struct ls_req_hdr {
	u_int32_t		type;
	u_int32_t		ls_id;
	u_int32_t		adv_rtr;
};

/* Link State Update header (type 4) */
struct ls_upd_hdr {
	u_int32_t		num_lsa;
};

#define	LSA_TYPE_ROUTER		1
#define LSA_TYPE_NETWORK	2
#define LSA_TYPE_SUM_NETWORK	3
#define LSA_TYPE_SUM_ROUTER	4
#define	LSA_TYPE_EXTERNAL	5
#define	LSA_TYPE_LINK_OPAQ	9
#define	LSA_TYPE_AREA_OPAQ	10
#define	LSA_TYPE_AS_OPAQ	11

#define LINK_TYPE_POINTTOPOINT	1
#define LINK_TYPE_TRANSIT_NET	2
#define LINK_TYPE_STUB_NET	3
#define LINK_TYPE_VIRTUAL	4

/* LSA headers */
#define LSA_METRIC_MASK		0x00ffffff	/* only for sum & as-ext */
#define LSA_ASEXT_E_FLAG	0x80000000

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


#define OSPF_RTR_B		0x01
#define OSPF_RTR_E		0x02
#define OSPF_RTR_V		0x04

struct lsa_rtr {
	u_int8_t		flags;
	u_int8_t		dummy;
	u_int16_t		nlinks;
};

struct lsa_rtr_link {
	u_int32_t		id;
	u_int32_t		data;
	u_int8_t		type;
	u_int8_t		num_tos;
	u_int16_t		metric;
};

struct lsa_net {
	u_int32_t		mask;
	u_int32_t		att_rtr[1];
};

struct lsa_net_link {
	u_int32_t		att_rtr;
};

struct lsa_sum {
	u_int32_t		mask;
	u_int32_t		metric;		/* only lower 24 bit */
};

struct lsa_asext {
	u_int32_t		mask;
	u_int32_t		metric;		/* lower 24 bit plus E bit */
	u_int32_t		fw_addr;
	u_int32_t		ext_tag;
};

struct lsa_hdr {
	u_int16_t		age;
	u_int8_t		opts;
	u_int8_t		type;
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
		struct lsa_sum		sum;
		struct lsa_asext	asext;
	}			data;
};

#endif /* !_OSPF_H_ */
