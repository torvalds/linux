/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2015-2019 Yandex LLC
 * Copyright (c) 2015 Alexander V. Chernikov <melifaro@FreeBSD.org>
 * Copyright (c) 2015-2019 Andrey V. Elsukov <ae@FreeBSD.org>
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
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * $FreeBSD$
 */

#ifndef	_NETINET6_IP_FW_NAT64_H_
#define	_NETINET6_IP_FW_NAT64_H_

struct ipfw_nat64stl_stats {
	uint64_t	opcnt64;	/* 6to4 of packets translated */
	uint64_t	opcnt46;	/* 4to6 of packets translated */
	uint64_t	ofrags;		/* number of fragments generated */
	uint64_t	ifrags;		/* number of fragments received */
	uint64_t	oerrors;	/* number of output errors */
	uint64_t	noroute4;
	uint64_t	noroute6;
	uint64_t	noproto;	/* Protocol not supported */
	uint64_t	nomem;		/* mbuf allocation failed */
	uint64_t	dropped;	/* dropped due to some errors */
};

struct ipfw_nat64clat_stats {
	uint64_t	opcnt64;	/* 6to4 of packets translated */
	uint64_t	opcnt46;	/* 4to6 of packets translated */
	uint64_t	ofrags;		/* number of fragments generated */
	uint64_t	ifrags;		/* number of fragments received */
	uint64_t	oerrors;	/* number of output errors */
	uint64_t	noroute4;
	uint64_t	noroute6;
	uint64_t	noproto;	/* Protocol not supported */
	uint64_t	nomem;		/* mbuf allocation failed */
	uint64_t	dropped;	/* dropped due to some errors */
};

struct ipfw_nat64lsn_stats {
	uint64_t	opcnt64;	/* 6to4 of packets translated */
	uint64_t	opcnt46;	/* 4to6 of packets translated */
	uint64_t	ofrags;		/* number of fragments generated */
	uint64_t	ifrags;		/* number of fragments received */
	uint64_t	oerrors;	/* number of output errors */
	uint64_t	noroute4;
	uint64_t	noroute6;
	uint64_t	noproto;	/* Protocol not supported */
	uint64_t	nomem;		/* mbuf allocation failed */
	uint64_t	dropped;	/* dropped due to some errors */

	uint64_t	nomatch4;	/* No addr/port match */
	uint64_t	jcalls;		/* Number of job handler calls */
	uint64_t	jrequests;	/* Number of job requests */
	uint64_t	jhostsreq;	/* Number of job host requests */
	uint64_t	jportreq;	/* Number of portgroup requests */
	uint64_t	jhostfails;	/* Number of failed host allocs */
	uint64_t	jportfails;	/* Number of failed portgroup allocs */
	uint64_t	jreinjected;	/* Number of packets reinjected to q */
	uint64_t	jmaxlen;	/* Max queue length reached */
	uint64_t	jnomem;		/* No memory to alloc queue item */

	uint64_t	screated;	/* Number of states created */
	uint64_t	sdeleted;	/* Number of states deleted */
	uint64_t	spgcreated;	/* Number of portgroups created */
	uint64_t	spgdeleted;	/* Number of portgroups deleted */
	uint64_t	hostcount;	/* Number of hosts  */
	uint64_t	tcpchunks;	/* Number of TCP chunks */
	uint64_t	udpchunks;	/* Number of UDP chunks */
	uint64_t	icmpchunks;	/* Number of ICMP chunks */

	uint64_t	_reserved[4];
};

#define	NAT64_LOG		0x0001	/* Enable logging via BPF */
#define	NAT64_ALLOW_PRIVATE	0x0002	/* Allow private IPv4 address
					 * translation
					 */
typedef struct _ipfw_nat64stl_cfg {
	char		name[64];	/* NAT name			*/
	ipfw_obj_ntlv	ntlv6;		/* object name tlv		*/
	ipfw_obj_ntlv	ntlv4;		/* object name tlv		*/
	struct in6_addr	prefix6;	/* NAT64 prefix */
	uint8_t		plen6;		/* Prefix length */
	uint8_t		set;		/* Named instance set [0..31] */
	uint8_t		spare[2];
	uint32_t	flags;
} ipfw_nat64stl_cfg;

typedef struct _ipfw_nat64clat_cfg {
	char		name[64];	/* NAT name			*/
	struct in6_addr	plat_prefix;	/* NAT64 (PLAT) prefix */
	struct in6_addr	clat_prefix;	/* Client (CLAT) prefix */
	uint8_t		plat_plen;	/* PLAT Prefix length */
	uint8_t		clat_plen;	/* CLAT Prefix length */
	uint8_t		set;		/* Named instance set [0..31] */
	uint8_t		spare;
	uint32_t	flags;
} ipfw_nat64clat_cfg;

/*
 * NAT64LSN default configuration values
 */
#define	NAT64LSN_MAX_PORTS	2048	/* Unused */
#define	NAT64LSN_JMAXLEN	2048	/* Max outstanding requests. */
#define	NAT64LSN_TCP_SYN_AGE	10	/* State's TTL after SYN received. */
#define	NAT64LSN_TCP_EST_AGE	(2 * 3600) /* TTL for established connection */
#define	NAT64LSN_TCP_FIN_AGE	180	/* State's TTL after FIN/RST received */
#define	NAT64LSN_UDP_AGE	120	/* TTL for UDP states */
#define	NAT64LSN_ICMP_AGE	60	/* TTL for ICMP states */
#define	NAT64LSN_HOST_AGE	3600	/* TTL for stale host entry */
#define	NAT64LSN_PG_AGE		900	/* TTL for stale ports groups */

typedef struct _ipfw_nat64lsn_cfg {
	char		name[64];	/* NAT name			*/
	uint32_t	flags;

	uint32_t	max_ports;      /* Unused */
	uint32_t	agg_prefix_len; /* Unused */
	uint32_t	agg_prefix_max; /* Unused */

	struct in_addr	prefix4;
	uint16_t	plen4;		/* Prefix length */
	uint16_t	plen6;		/* Prefix length */
	struct in6_addr	prefix6;	/* NAT64 prefix */
	uint32_t	jmaxlen;	/* Max jobqueue length */

	uint16_t	min_port;	/* Unused */
	uint16_t	max_port;	/* Unused */

	uint16_t	nh_delete_delay;/* Stale host delete delay */
	uint16_t	pg_delete_delay;/* Stale portgroup delete delay */
	uint16_t	st_syn_ttl;	/* TCP syn expire */
	uint16_t	st_close_ttl;	/* TCP fin expire */
	uint16_t	st_estab_ttl;	/* TCP established expire */
	uint16_t	st_udp_ttl;	/* UDP expire */
	uint16_t	st_icmp_ttl;	/* ICMP expire */
	uint8_t		set;		/* Named instance set [0..31] */
	uint8_t		states_chunks;	/* Number of states chunks per PG */
} ipfw_nat64lsn_cfg;

typedef struct _ipfw_nat64lsn_state {
	struct in_addr	daddr;		/* Remote IPv4 address */
	uint16_t	dport;		/* Remote destination port */
	uint16_t	aport;		/* Local alias port */
	uint16_t	sport;		/* Source port */
	uint8_t		flags;		/* State flags */
	uint8_t		spare[3];
	uint16_t	idle;		/* Last used time */
} ipfw_nat64lsn_state;

typedef struct _ipfw_nat64lsn_stg {
	uint64_t	next_idx;	/* next state index */
	struct in_addr	alias4;		/* IPv4 alias address */
	uint8_t		proto;		/* protocol */
	uint8_t		flags;
	uint16_t	spare;
	struct in6_addr	host6;		/* Bound IPv6 host */
	uint32_t	count;		/* Number of states */
	uint32_t	spare2;
} ipfw_nat64lsn_stg;

typedef struct _ipfw_nat64lsn_state_v1 {
	struct in6_addr	host6;		/* Bound IPv6 host */
	struct in_addr	daddr;		/* Remote IPv4 address */
	uint16_t	dport;		/* Remote destination port */
	uint16_t	aport;		/* Local alias port */
	uint16_t	sport;		/* Source port */
	uint16_t	spare;
	uint16_t	idle;		/* Last used time */
	uint8_t		flags;		/* State flags */
	uint8_t		proto;		/* protocol */
} ipfw_nat64lsn_state_v1;

typedef struct _ipfw_nat64lsn_stg_v1 {
	union nat64lsn_pgidx {
		uint64_t	index;
		struct {
			uint8_t		chunk;	/* states chunk */
			uint8_t		proto;	/* protocol */
			uint16_t	port;	/* base port */
			in_addr_t	addr;	/* alias address */
		};
	} next;				/* next state index */
	struct in_addr	alias4;		/* IPv4 alias address */
	uint32_t	count;		/* Number of states */
} ipfw_nat64lsn_stg_v1;

#endif /* _NETINET6_IP_FW_NAT64_H_ */
