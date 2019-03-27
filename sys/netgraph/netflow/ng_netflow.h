/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2010-2011 Alexander V. Chernikov <melifaro@ipfw.ru>
 * Copyright (c) 2004-2005 Gleb Smirnoff <glebius@FreeBSD.org>
 * Copyright (c) 2001-2003 Roman V. Palagin <romanp@unshadow.net>
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
 *	 $SourceForge: ng_netflow.h,v 1.26 2004/09/04 15:44:55 glebius Exp $
 *	 $FreeBSD$
 */

#ifndef	_NG_NETFLOW_H_
#define	_NG_NETFLOW_H_

#define NG_NETFLOW_NODE_TYPE	"netflow"
#define NGM_NETFLOW_COOKIE	1365756954
#define NGM_NETFLOW_V9_COOKIE	1349865386

#define	NG_NETFLOW_MAXIFACES	USHRT_MAX

/* Hook names */

#define	NG_NETFLOW_HOOK_DATA	"iface"
#define	NG_NETFLOW_HOOK_OUT	"out"
#define NG_NETFLOW_HOOK_EXPORT	"export"
#define NG_NETFLOW_HOOK_EXPORT9	"export9"

/* This define effectively disable (v5) netflow export hook! */
/* #define COUNTERS_64 */

/* Netgraph commands understood by netflow node */
enum {
    NGM_NETFLOW_INFO = 1|NGM_READONLY|NGM_HASREPLY,	/* get node info */
    NGM_NETFLOW_IFINFO = 2|NGM_READONLY|NGM_HASREPLY,	/* get iface info */
    NGM_NETFLOW_SHOW = 3|NGM_READONLY|NGM_HASREPLY,	/* show ip cache flow */
    NGM_NETFLOW_SETDLT		= 4,	/* set data-link type */	
    NGM_NETFLOW_SETIFINDEX	= 5, 	/* set interface index */
    NGM_NETFLOW_SETTIMEOUTS	= 6, 	/* set active/inactive flow timeouts */
    NGM_NETFLOW_SETCONFIG	= 7, 	/* set flow generation options */
    NGM_NETFLOW_SETTEMPLATE	= 8, 	/* set v9 flow template periodic */
    NGM_NETFLOW_SETMTU		= 9, 	/* set outgoing interface MTU */
    NGM_NETFLOW_V9INFO = 10|NGM_READONLY|NGM_HASREPLY, 	/* get v9 info */
};

/* This structure is returned by the NGM_NETFLOW_INFO message */
struct ng_netflow_info {
	uint64_t	nfinfo_bytes;		/* accounted IPv4 bytes */
	uint64_t	nfinfo_packets;		/* accounted IPv4 packets */
	uint64_t	nfinfo_bytes6;		/* accounted IPv6 bytes */
	uint64_t	nfinfo_packets6;	/* accounted IPv6 packets */
	uint64_t	nfinfo_sbytes;		/* skipped IPv4 bytes */
	uint64_t	nfinfo_spackets;	/* skipped IPv4 packets */
	uint64_t	nfinfo_sbytes6;		/* skipped IPv6 bytes */
	uint64_t	nfinfo_spackets6;	/* skipped IPv6 packets */
	uint64_t	nfinfo_act_exp;		/* active expiries */
	uint64_t	nfinfo_inact_exp;	/* inactive expiries */
	uint32_t	nfinfo_used;		/* used cache records */
	uint32_t	nfinfo_used6;		/* used IPv6 cache records */
	uint32_t	nfinfo_alloc_failed;	/* failed allocations */
	uint32_t	nfinfo_export_failed;	/* failed exports */
	uint32_t	nfinfo_export9_failed;	/* failed exports */
	uint32_t	nfinfo_realloc_mbuf;	/* reallocated mbufs */
	uint32_t	nfinfo_alloc_fibs;	/* fibs allocated */
	uint32_t	nfinfo_inact_t;		/* flow inactive timeout */
	uint32_t	nfinfo_act_t;		/* flow active timeout */
};

/* Parse the info structure */
#define	NG_NETFLOW_INFO_TYPE {					\
	{ "IPv4 bytes",			&ng_parse_uint64_type },\
	{ "IPv4 packets",		&ng_parse_uint64_type },\
	{ "IPv6 bytes",			&ng_parse_uint64_type },\
	{ "IPv6 packets",		&ng_parse_uint64_type },\
	{ "IPv4 skipped bytes",		&ng_parse_uint64_type },\
	{ "IPv4 skipped packets",	&ng_parse_uint64_type },\
	{ "IPv6 skipped bytes",		&ng_parse_uint64_type },\
	{ "IPv6 skipped packets",	&ng_parse_uint64_type },\
	{ "Active expiries",		&ng_parse_uint64_type },\
	{ "Inactive expiries",		&ng_parse_uint64_type },\
	{ "IPv4 records used",		&ng_parse_uint32_type },\
	{ "IPv6 records used",		&ng_parse_uint32_type },\
	{ "Failed allocations",		&ng_parse_uint32_type },\
	{ "V5 failed exports",		&ng_parse_uint32_type },\
	{ "V9 failed exports",		&ng_parse_uint32_type },\
	{ "mbuf reallocations",		&ng_parse_uint32_type },\
	{ "fibs allocated",		&ng_parse_uint32_type },\
	{ "Inactive timeout",		&ng_parse_uint32_type },\
	{ "Active timeout",		&ng_parse_uint32_type },\
	{ NULL }						\
}


/* This structure is returned by the NGM_NETFLOW_IFINFO message */
struct ng_netflow_ifinfo {
	uint32_t	ifinfo_packets;	/* number of packets for this iface */
	uint8_t		ifinfo_dlt;	/* Data Link Type, DLT_XXX */
#define	MAXDLTNAMELEN	20
	uint16_t	ifinfo_index;	/* connected iface index */
	uint32_t	conf;
};


/* This structure is passed to NGM_NETFLOW_SETDLT message */
struct ng_netflow_setdlt {
	uint16_t iface;		/* which iface dlt change */
	uint8_t  dlt;		/* DLT_XXX from bpf.h */
};

/* This structure is passed to NGM_NETFLOW_SETIFINDEX */
struct ng_netflow_setifindex {
	uint16_t iface;		/* which iface index change */
	uint16_t index;		/* new index */
};

/* This structure is passed to NGM_NETFLOW_SETTIMEOUTS */
struct ng_netflow_settimeouts {
	uint32_t	inactive_timeout;	/* flow inactive timeout */
	uint32_t	active_timeout;		/* flow active timeout */
};

#define NG_NETFLOW_CONF_INGRESS		0x01	/* Account on ingress */
#define NG_NETFLOW_CONF_EGRESS		0x02	/* Account on egress */
#define NG_NETFLOW_CONF_ONCE		0x04	/* Add tag to account only once */
#define NG_NETFLOW_CONF_THISONCE	0x08	/* Account once in current node */
#define NG_NETFLOW_CONF_NOSRCLOOKUP	0x10	/* No radix lookup on src */
#define NG_NETFLOW_CONF_NODSTLOOKUP	0x20	/* No radix lookup on dst */

#define NG_NETFLOW_IS_FRAG		0x01
#define NG_NETFLOW_FLOW_FLAGS		(NG_NETFLOW_CONF_NOSRCLOOKUP|\
					NG_NETFLOW_CONF_NODSTLOOKUP)

/* This structure is passed to NGM_NETFLOW_SETCONFIG */
struct ng_netflow_setconfig {
	uint16_t iface;		/* which iface config change */
	uint32_t conf;		/* new config */
};

/* This structure is passed to NGM_NETFLOW_SETTEMPLATE */
struct ng_netflow_settemplate {
	uint16_t time;		/* max time between announce */
	uint16_t packets;	/* max packets between announce */
};

/* This structure is passed to NGM_NETFLOW_SETMTU */
struct ng_netflow_setmtu {
	uint16_t mtu;		/* MTU for packet */
};

/* This structure is used in NGM_NETFLOW_SHOW request/response */
struct ngnf_show_header {
	u_char		version;	/* IPv4 or IPv6 */
	uint32_t	hash_id;	/* current hash index */
	uint32_t	list_id;	/* current record number in hash */
	uint32_t	nentries;	/* number of records in response */
};

/* This structure is used in NGM_NETFLOW_V9INFO message */
struct ng_netflow_v9info {
	uint16_t	templ_packets;	/* v9 template packets */
	uint16_t	templ_time;	/* v9 template time */
	uint16_t	mtu;		/* v9 MTU */
};

/* XXXGL
 * Somewhere flow_rec6 is casted to flow_rec, and flow6_entry_data is
 * casted to flow_entry_data. After casting, fle->r.fib is accessed.
 * So beginning of these structs up to fib should be kept common.
 */

/* This is unique data, which identifies flow */
struct flow_rec {
	uint16_t	flow_type;
	uint16_t	fib;
	struct in_addr	r_src;
	struct in_addr	r_dst;
	union {
		struct {
			uint16_t	s_port;	/* source TCP/UDP port */
			uint16_t	d_port; /* destination TCP/UDP port */
		} dir;
		uint32_t both;
	} ports;
	union {
		struct {
			u_char		prot;	/* IP protocol */
			u_char		tos;	/* IP TOS */
			uint16_t	i_ifx;	/* input interface index */
		} i;
		uint32_t all;
	} misc;
};

/* This is unique data, which identifies flow */
struct flow6_rec {
	uint16_t	flow_type;
	uint16_t	fib;
	union {
		struct in_addr	r_src;
		struct in6_addr	r_src6;
	} src;
	union {
		struct in_addr	r_dst;
		struct in6_addr	r_dst6;
	} dst;
	union {
		struct {
			uint16_t	s_port;	/* source TCP/UDP port */
			uint16_t	d_port; /* destination TCP/UDP port */
		} dir;
		uint32_t both;
	} ports;
	union {
		struct {
			u_char		prot;	/* IP protocol */
			u_char		tos;	/* IP TOS */
			uint16_t	i_ifx;	/* input interface index */
		} i;
		uint32_t all;
	} misc;
};

#define	r_ip_p	misc.i.prot
#define	r_tos	misc.i.tos
#define	r_i_ifx	misc.i.i_ifx
#define r_misc	misc.all
#define r_ports	ports.both
#define r_sport	ports.dir.s_port
#define r_dport	ports.dir.d_port
	
/* A flow entry which accumulates statistics */
struct flow_entry_data {
	uint16_t	version;	/* Protocol version */
	struct flow_rec	r;
	struct in_addr	next_hop;
	uint16_t	fle_o_ifx;	/* output interface index */
#define	fle_i_ifx	r.misc.i.i_ifx
	uint8_t		dst_mask;	/* destination route mask bits */
	uint8_t		src_mask;	/* source route mask bits */
	u_long		packets;
	u_long		bytes;
	long		first;		/* uptime on first packet */
	long		last;		/* uptime on last packet */
	u_char		tcp_flags;	/* cumulative OR */
};

struct flow6_entry_data {
	uint16_t		version;	/* Protocol version */
	struct flow6_rec	r;
	union {
		struct in_addr	next_hop;
		struct in6_addr	next_hop6;
	} n;
	uint16_t	fle_o_ifx;	/* output interface index */
#define	fle_i_ifx	r.misc.i.i_ifx
	uint8_t		dst_mask;	/* destination route mask bits */
	uint8_t		src_mask;	/* source route mask bits */
	u_long		packets;
	u_long		bytes;
	long		first;		/* uptime on first packet */
	long		last;		/* uptime on last packet */
	u_char		tcp_flags;	/* cumulative OR */
};

/*
 * How many flow records we will transfer at once
 * without overflowing socket receive buffer
 */
#define NREC_AT_ONCE	 1000
#define NREC6_AT_ONCE	(NREC_AT_ONCE * sizeof(struct flow_entry_data) / \
			sizeof(struct flow6_entry_data))
#define NGRESP_SIZE	(sizeof(struct ngnf_show_header) + (NREC_AT_ONCE * \
			sizeof(struct flow_entry_data)))
#define SORCVBUF_SIZE	(NGRESP_SIZE + 2 * sizeof(struct ng_mesg))

/* Everything below is for kernel */

#ifdef _KERNEL

struct flow_entry {
	TAILQ_ENTRY(flow_entry)	fle_hash;	/* entries in hash slot */
	struct flow_entry_data	f;
};

struct flow6_entry {
	TAILQ_ENTRY(flow_entry)	fle_hash;	/* entries in hash slot */
	struct flow6_entry_data	f;
};
/* Parsing declarations */

/* Parse the ifinfo structure */
#define NG_NETFLOW_IFINFO_TYPE	{			\
	{ "packets",		&ng_parse_uint32_type },\
	{ "data link type",	&ng_parse_uint8_type },	\
	{ "index",		&ng_parse_uint16_type },\
	{ "conf",		&ng_parse_uint32_type },\
	{ NULL }					\
}

/* Parse the setdlt structure */
#define	NG_NETFLOW_SETDLT_TYPE {			\
	{ "iface",	&ng_parse_uint16_type },	\
	{ "dlt",	&ng_parse_uint8_type },		\
	{ NULL }					\
}

/* Parse the setifindex structure */
#define	NG_NETFLOW_SETIFINDEX_TYPE {			\
	{ "iface",	&ng_parse_uint16_type },	\
	{ "index",	&ng_parse_uint16_type },	\
	{ NULL }					\
}

/* Parse the settimeouts structure */
#define NG_NETFLOW_SETTIMEOUTS_TYPE {			\
	{ "inactive",	&ng_parse_uint32_type },	\
	{ "active",	&ng_parse_uint32_type },	\
	{ NULL }					\
}

/* Parse the setifindex structure */
#define	NG_NETFLOW_SETCONFIG_TYPE {			\
	{ "iface",	&ng_parse_uint16_type },	\
	{ "conf",	&ng_parse_uint32_type },	\
	{ NULL }					\
}

/* Parse the settemplate structure */
#define	NG_NETFLOW_SETTEMPLATE_TYPE {		\
	{ "time",	&ng_parse_uint16_type },	\
	{ "packets",	&ng_parse_uint16_type },	\
	{ NULL }					\
}

/* Parse the setmtu structure */
#define	NG_NETFLOW_SETMTU_TYPE {			\
	{ "mtu",	&ng_parse_uint16_type },	\
	{ NULL }					\
}

/* Parse the v9info structure */
#define	NG_NETFLOW_V9INFO_TYPE {				\
	{ "v9 template packets",	&ng_parse_uint16_type },\
	{ "v9 template time",		&ng_parse_uint16_type },\
	{ "v9 MTU",			&ng_parse_uint16_type },\
	{ NULL }						\
}

/* Private hook data */
struct ng_netflow_iface {
	hook_p		hook;		/* NULL when disconnected */
	hook_p		out;		/* NULL when no bypass hook */
	struct ng_netflow_ifinfo	info;
};

typedef struct ng_netflow_iface *iface_p;
typedef struct ng_netflow_ifinfo *ifinfo_p;

struct netflow_export_item {
	item_p		item;
	item_p		item9;
	struct netflow_v9_packet_opt	*item9_opt;
};

/* Structure contatining fib-specific data */
struct fib_export {
	uint32_t	fib;		/* kernel fib id */

	/* Various data used for export */
	struct netflow_export_item exp;

	struct mtx	export_mtx;	/* exp.item mutex */
	struct mtx	export9_mtx;	/* exp.item9 mutex */
	uint32_t	flow_seq;	/* current V5 flow sequence */
	uint32_t	flow9_seq;	/* current V9 flow sequence */
	uint32_t	domain_id;	/* Observartion domain id */
	/* Netflow V9 counters */
	uint32_t	templ_last_ts;	/* unixtime of last template announce */
	uint32_t	templ_last_pkt;	/* packet count on last announce */
	uint32_t	sent_packets;	/* packets sent by exporter; */

	/* Current packet specific options */
	struct netflow_v9_packet_opt *export9_opt;
};

typedef struct fib_export *fib_export_p;

/* Structure describing our flow engine */
struct netflow {
	node_p		node;		/* link to the node itself */
	hook_p		export;		/* export data goes there */
	hook_p		export9;	/* Netflow V9 export data goes there */
	struct callout	exp_callout;	/* expiry periodic job */

	/*
	 * Flow entries are allocated in uma(9) zone zone. They are
	 * indexed by hash hash. Each hash element consist of tailqueue
	 * head and mutex to protect this element.
	 */
#define	CACHESIZE			(65536*16)
#define	CACHELOWAT			(CACHESIZE * 3/4)
#define	CACHEHIGHWAT			(CACHESIZE * 9/10)
	uma_zone_t		zone;
	struct flow_hash_entry	*hash;

	/*
	 * NetFlow data export
	 *
	 * export_item is a data item, it has an mbuf with cluster
	 * attached to it. A thread detaches export_item from priv
	 * and works with it. If the export is full it is sent, and
	 * a new one is allocated. Before exiting thread re-attaches
	 * its current item back to priv. If there is item already,
	 * current incomplete datagram is sent.
	 * export_mtx is used for attaching/detaching.
	 */

	/* IPv6 support */
#ifdef INET6
	uma_zone_t		zone6;
	struct flow_hash_entry	*hash6;
#endif

	/* Statistics. */
	counter_u64_t	nfinfo_bytes;		/* accounted IPv4 bytes */
	counter_u64_t	nfinfo_packets;		/* accounted IPv4 packets */
	counter_u64_t	nfinfo_bytes6;		/* accounted IPv6 bytes */
	counter_u64_t	nfinfo_packets6;	/* accounted IPv6 packets */
	counter_u64_t	nfinfo_sbytes;		/* skipped IPv4 bytes */
	counter_u64_t	nfinfo_spackets;	/* skipped IPv4 packets */
	counter_u64_t	nfinfo_sbytes6;		/* skipped IPv6 bytes */
	counter_u64_t	nfinfo_spackets6;	/* skipped IPv6 packets */
	counter_u64_t	nfinfo_act_exp;		/* active expiries */
	counter_u64_t	nfinfo_inact_exp;	/* inactive expiries */
	uint32_t	nfinfo_alloc_failed;	/* failed allocations */
	uint32_t	nfinfo_export_failed;	/* failed exports */
	uint32_t	nfinfo_export9_failed;	/* failed exports */
	uint32_t	nfinfo_realloc_mbuf;	/* reallocated mbufs */
	uint32_t	nfinfo_alloc_fibs;	/* fibs allocated */
	uint32_t	nfinfo_inact_t;		/* flow inactive timeout */
	uint32_t	nfinfo_act_t;		/* flow active timeout */

	/* Multiple FIB support */
	fib_export_p	*fib_data;	/* vector to per-fib data */
	uint16_t	maxfibs;	/* number of allocated fibs */

	/* Netflow v9 configuration options */
	/*
	 * RFC 3954 clause 7.3
	 * "Both options MUST be configurable by the user on the Exporter."
	 */
	uint16_t	templ_time;	/* time between sending templates */
	uint16_t	templ_packets;	/* packets between sending templates */
#define NETFLOW_V9_MAX_FLOWSETS	2
	u_char		flowsets_count; /* current flowsets used */

	/* Count of records in each flowset */
	u_char		flowset_records[NETFLOW_V9_MAX_FLOWSETS - 1];
	uint16_t	mtu;		/* export interface MTU */

	/* Pointers to pre-compiled flowsets */
	struct netflow_v9_flowset_header
	    *v9_flowsets[NETFLOW_V9_MAX_FLOWSETS - 1];

	struct ng_netflow_iface	ifaces[NG_NETFLOW_MAXIFACES];
};

typedef struct netflow *priv_p;

/* Header of a small list in hash cell */
struct flow_hash_entry {
	struct mtx		mtx;
	TAILQ_HEAD(fhead, flow_entry) head;
};

#define	ERROUT(x)	{ error = (x); goto done; }

#define MTAG_NETFLOW		1221656444
#define MTAG_NETFLOW_CALLED	0

#define m_pktlen(m)	((m)->m_pkthdr.len)
#define IP6VERSION	6

#define priv_to_fib(priv, fib)	(priv)->fib_data[(fib)]

/*
 * Cisco uses milliseconds for uptime. Bad idea, since it overflows
 * every 48+ days. But we will do same to keep compatibility. This macro
 * does overflowable multiplication to 1000.
 */
#define	MILLIUPTIME(t)	(((t) << 9) +	/* 512 */	\
			 ((t) << 8) +	/* 256 */	\
			 ((t) << 7) +	/* 128 */	\
			 ((t) << 6) +	/* 64  */	\
			 ((t) << 5) +	/* 32  */	\
			 ((t) << 3))	/* 8   */

/* Prototypes for netflow.c */
void	ng_netflow_cache_init(priv_p);
void	ng_netflow_cache_flush(priv_p);
int	ng_netflow_fib_init(priv_p priv, int fib);
void	ng_netflow_copyinfo(priv_p, struct ng_netflow_info *);
void	ng_netflow_copyv9info(priv_p, struct ng_netflow_v9info *);
timeout_t ng_netflow_expire;
int 	ng_netflow_flow_add(priv_p, fib_export_p, struct ip *, caddr_t,
	uint8_t, uint8_t, unsigned int);
int	ng_netflow_flow6_add(priv_p, fib_export_p, struct ip6_hdr *, caddr_t,
	uint8_t, uint8_t, unsigned int);
int	ng_netflow_flow_show(priv_p, struct ngnf_show_header *req,
	struct ngnf_show_header *resp);
void	ng_netflow_v9_cache_init(priv_p);
void	ng_netflow_v9_cache_flush(priv_p);
item_p	get_export9_dgram(priv_p, fib_export_p,
	struct netflow_v9_packet_opt **);
void	return_export9_dgram(priv_p, fib_export_p, item_p,
	struct netflow_v9_packet_opt *, int);
int	export9_add(item_p, struct netflow_v9_packet_opt *,
	struct flow_entry *);
int	export9_send(priv_p, fib_export_p, item_p,
	struct netflow_v9_packet_opt *, int);

#endif	/* _KERNEL */
#endif	/* _NG_NETFLOW_H_ */
