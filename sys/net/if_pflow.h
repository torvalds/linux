/*	$OpenBSD: if_pflow.h,v 1.23 2023/12/16 22:16:02 mvs Exp $	*/

/*
 * Copyright (c) 2008 Henning Brauer <henning@openbsd.org>
 * Copyright (c) 2008 Joerg Goltermann <jg@osn.de>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF MIND, USE, DATA OR PROFITS, WHETHER IN
 * AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT
 * OF OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#ifndef _NET_IF_PFLOW_H_
#define _NET_IF_PFLOW_H_

#define PFLOW_ID_LEN	sizeof(u_int64_t)

#define PFLOW_MAXFLOWS 30
#define PFLOW_ENGINE_TYPE 42
#define PFLOW_ENGINE_ID 42
#define PFLOW_MAXBYTES 0xffffffff
#define PFLOW_TIMEOUT 30
#define PFLOW_TMPL_TIMEOUT 30 /* rfc 5101 10.3.6 (p.40) recommends 600 */

#define PFLOW_IPFIX_TMPL_SET_ID 2

/* RFC 5102 Information Element Identifiers */

#define PFIX_IE_octetDeltaCount			  1
#define PFIX_IE_packetDeltaCount		  2
#define PFIX_IE_protocolIdentifier		  4
#define PFIX_IE_ipClassOfService		  5
#define PFIX_IE_sourceTransportPort		  7
#define PFIX_IE_sourceIPv4Address		  8
#define PFIX_IE_ingressInterface		 10
#define PFIX_IE_destinationTransportPort	 11
#define PFIX_IE_destinationIPv4Address		 12
#define PFIX_IE_egressInterface			 14
#define PFIX_IE_flowEndSysUpTime		 21
#define PFIX_IE_flowStartSysUpTime		 22
#define PFIX_IE_sourceIPv6Address		 27
#define PFIX_IE_destinationIPv6Address		 28
#define PFIX_IE_flowStartMilliseconds		152
#define PFIX_IE_flowEndMilliseconds		153

struct pflow_flow {
	u_int32_t	src_ip;
	u_int32_t	dest_ip;
	u_int32_t	nexthop_ip;
	u_int16_t	if_index_in;
	u_int16_t	if_index_out;
	u_int32_t	flow_packets;
	u_int32_t	flow_octets;
	u_int32_t	flow_start;
	u_int32_t	flow_finish;
	u_int16_t	src_port;
	u_int16_t	dest_port;
	u_int8_t	pad1;
	u_int8_t	tcp_flags;
	u_int8_t	protocol;
	u_int8_t	tos;
	u_int16_t	src_as;
	u_int16_t	dest_as;
	u_int8_t	src_mask;
	u_int8_t	dest_mask;
	u_int16_t	pad2;
} __packed;

struct pflow_set_header {
	u_int16_t	set_id;
	u_int16_t	set_length; /* total length of the set,
				       in octets, including the set header */
} __packed;

#define PFLOW_SET_HDRLEN sizeof(struct pflow_set_header)

struct pflow_tmpl_hdr {
	u_int16_t	tmpl_id;
	u_int16_t	field_count;
} __packed;

struct pflow_tmpl_fspec {
	u_int16_t	field_id;
	u_int16_t	len;
} __packed;

/* update pflow_clone_create() when changing pflow_ipfix_tmpl_ipv4 */
struct pflow_ipfix_tmpl_ipv4 {
	struct pflow_tmpl_hdr	h;
	struct pflow_tmpl_fspec	src_ip;
	struct pflow_tmpl_fspec	dest_ip;
	struct pflow_tmpl_fspec	if_index_in;
	struct pflow_tmpl_fspec	if_index_out;
	struct pflow_tmpl_fspec	packets;
	struct pflow_tmpl_fspec	octets;
	struct pflow_tmpl_fspec	start;
	struct pflow_tmpl_fspec	finish;
	struct pflow_tmpl_fspec	src_port;
	struct pflow_tmpl_fspec	dest_port;
	struct pflow_tmpl_fspec	tos;
	struct pflow_tmpl_fspec	protocol;
#define PFLOW_IPFIX_TMPL_IPV4_FIELD_COUNT 12
#define PFLOW_IPFIX_TMPL_IPV4_ID 256
} __packed;

/* update pflow_clone_create() when changing pflow_ipfix_tmpl_v6 */
struct pflow_ipfix_tmpl_ipv6 {
	struct pflow_tmpl_hdr	h;
	struct pflow_tmpl_fspec	src_ip;
	struct pflow_tmpl_fspec	dest_ip;
	struct pflow_tmpl_fspec	if_index_in;
	struct pflow_tmpl_fspec	if_index_out;
	struct pflow_tmpl_fspec	packets;
	struct pflow_tmpl_fspec	octets;
	struct pflow_tmpl_fspec	start;
	struct pflow_tmpl_fspec	finish;
	struct pflow_tmpl_fspec	src_port;
	struct pflow_tmpl_fspec	dest_port;
	struct pflow_tmpl_fspec	tos;
	struct pflow_tmpl_fspec	protocol;
#define PFLOW_IPFIX_TMPL_IPV6_FIELD_COUNT 12
#define PFLOW_IPFIX_TMPL_IPV6_ID 257
} __packed;

struct pflow_ipfix_tmpl {
	struct pflow_set_header	set_header;
	struct pflow_ipfix_tmpl_ipv4	ipv4_tmpl;
	struct pflow_ipfix_tmpl_ipv6	ipv6_tmpl;
} __packed;

struct pflow_ipfix_flow4 {
	u_int32_t	src_ip;		/* sourceIPv4Address*/
	u_int32_t	dest_ip;	/* destinationIPv4Address */
	u_int32_t	if_index_in;	/* ingressInterface */
	u_int32_t	if_index_out;	/* egressInterface */
	u_int64_t	flow_packets;	/* packetDeltaCount */
	u_int64_t	flow_octets;	/* octetDeltaCount */
	int64_t		flow_start;	/* flowStartMilliseconds */
	int64_t		flow_finish;	/* flowEndMilliseconds */
	u_int16_t	src_port;	/* sourceTransportPort */
	u_int16_t	dest_port;	/* destinationTransportPort */
	u_int8_t	tos;		/* ipClassOfService */
	u_int8_t	protocol;	/* protocolIdentifier */
	/* XXX padding needed? */
} __packed;

struct pflow_ipfix_flow6 {
	struct in6_addr src_ip;		/* sourceIPv6Address */
	struct in6_addr dest_ip;	/* destinationIPv6Address */
	u_int32_t	if_index_in;	/* ingressInterface */
	u_int32_t	if_index_out;	/* egressInterface */
	u_int64_t	flow_packets;	/* packetDeltaCount */
	u_int64_t	flow_octets;	/* octetDeltaCount */
	int64_t		flow_start;	/* flowStartMilliseconds */
	int64_t		flow_finish;	/* flowEndMilliseconds */
	u_int16_t	src_port;	/* sourceTransportPort */
	u_int16_t	dest_port;	/* destinationTransportPort */
	u_int8_t	tos;		/* ipClassOfService */
	u_int8_t	protocol;	/* protocolIdentifier */
	/* XXX padding needed? */
} __packed;

#ifdef _KERNEL

#include <sys/smr.h>

/*
 * Locks used to protect struct members and global data
 *       I       immutable after creation
 *       m       this pflow_softc' `sc_mtx'
 *       p       this pflow_softc' `sc_lock'
 */

struct pflow_softc {
	struct mutex		 sc_mtx;
	struct rwlock		 sc_lock;

	int			 sc_dying;	/* [p] */
	struct ifnet		 sc_if;

	unsigned int		 sc_count;	/* [m] */
	unsigned int		 sc_count4;	/* [m] */
	unsigned int		 sc_count6;	/* [m] */
	unsigned int		 sc_maxcount;	/* [m] */
	unsigned int		 sc_maxcount4;	/* [m] */
	unsigned int		 sc_maxcount6;	/* [m] */
	u_int32_t		 sc_gcounter;	/* [m] */
	u_int32_t		 sc_sequence;	/* [m] */
	struct timeout		 sc_tmo;
	struct timeout		 sc_tmo6;
	struct timeout		 sc_tmo_tmpl;
	struct mbuf_queue	 sc_outputqueue;
	struct task		 sc_outputtask;
	struct socket		*so;		/* [p] */
	struct mbuf		*send_nam;	/* [p] */
	struct sockaddr		*sc_flowsrc;	/* [p] */
	struct sockaddr		*sc_flowdst;	/* [p] */
	struct pflow_ipfix_tmpl	 sc_tmpl_ipfix;	/* [I] */
	u_int8_t		 sc_version;	/* [m] */
	struct mbuf		*sc_mbuf;	/* [m] current cumulative
						    mbuf */
	struct mbuf		*sc_mbuf6;	/* [m] current cumulative
						    mbuf */
	SMR_SLIST_ENTRY(pflow_softc) sc_next;
};

extern struct pflow_softc	*pflowif;

#endif /* _KERNEL */

struct pflow_header {
	u_int16_t	version;
	u_int16_t	count;
	u_int32_t	uptime_ms;
	u_int32_t	time_sec;
	u_int32_t	time_nanosec;
	u_int32_t	flow_sequence;
	u_int8_t	engine_type;
	u_int8_t	engine_id;
	u_int8_t	reserved1;
	u_int8_t	reserved2;
} __packed;

#define PFLOW_HDRLEN sizeof(struct pflow_header)

struct pflow_v10_header {
	u_int16_t	version;
	u_int16_t	length;
	u_int32_t	time_sec;
	u_int32_t	flow_sequence;
	u_int32_t	observation_dom;
} __packed;

#define PFLOW_IPFIX_HDRLEN sizeof(struct pflow_v10_header)

struct pflowstats {
	u_int64_t	pflow_flows;
	u_int64_t	pflow_packets;
	u_int64_t	pflow_onomem;
	u_int64_t	pflow_oerrors;
};

/* Supported flow protocols */
#define PFLOW_PROTO_5	5	/* original pflow */
#define PFLOW_PROTO_10	10	/* ipfix */
#define PFLOW_PROTO_MAX	11

#define PFLOW_PROTO_DEFAULT PFLOW_PROTO_5

struct pflow_protos {
	const char	*ppr_name;
	u_int8_t	 ppr_proto;
};

#define PFLOW_PROTOS {                                 \
		{ "5",	PFLOW_PROTO_5 },	       \
		{ "10",	PFLOW_PROTO_10 },	       \
}

/*
 * Configuration structure for SIOCSETPFLOW SIOCGETPFLOW
 */
struct pflowreq {
	struct sockaddr_storage	flowsrc;
	struct sockaddr_storage	flowdst;
	u_int16_t		addrmask;
	u_int8_t		version;
#define PFLOW_MASK_SRCIP	0x01
#define PFLOW_MASK_DSTIP	0x02
#define PFLOW_MASK_VERSION	0x04
};

#ifdef _KERNEL
int export_pflow(struct pf_state *);
int pflow_sysctl(int *, u_int,  void *, size_t *, void *, size_t);
#endif /* _KERNEL */

#endif /* _NET_IF_PFLOW_H_ */
