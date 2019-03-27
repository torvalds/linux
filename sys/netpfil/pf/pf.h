/*-
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright (c) 2001 Daniel Hartmeier
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 *    - Redistributions of source code must retain the above copyright
 *      notice, this list of conditions and the following disclaimer.
 *    - Redistributions in binary form must reproduce the above
 *      copyright notice, this list of conditions and the following
 *      disclaimer in the documentation and/or other materials provided
 *      with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
 * COPYRIGHT HOLDERS OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 * CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
 * ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 *
 *	$OpenBSD: pfvar.h,v 1.282 2009/01/29 15:12:28 pyr Exp $
 *	$FreeBSD$
 */

#ifndef	_NET_PF_H_
#define	_NET_PF_H_

#define	PF_TCPS_PROXY_SRC	((TCP_NSTATES)+0)
#define	PF_TCPS_PROXY_DST	((TCP_NSTATES)+1)

#define	PF_MD5_DIGEST_LENGTH	16
#ifdef MD5_DIGEST_LENGTH
#if PF_MD5_DIGEST_LENGTH != MD5_DIGEST_LENGTH
#error
#endif
#endif

enum	{ PF_INOUT, PF_IN, PF_OUT };
enum	{ PF_PASS, PF_DROP, PF_SCRUB, PF_NOSCRUB, PF_NAT, PF_NONAT,
	  PF_BINAT, PF_NOBINAT, PF_RDR, PF_NORDR, PF_SYNPROXY_DROP, PF_DEFER };
enum	{ PF_RULESET_SCRUB, PF_RULESET_FILTER, PF_RULESET_NAT,
	  PF_RULESET_BINAT, PF_RULESET_RDR, PF_RULESET_MAX };
enum	{ PF_OP_NONE, PF_OP_IRG, PF_OP_EQ, PF_OP_NE, PF_OP_LT,
	  PF_OP_LE, PF_OP_GT, PF_OP_GE, PF_OP_XRG, PF_OP_RRG };
enum	{ PF_DEBUG_NONE, PF_DEBUG_URGENT, PF_DEBUG_MISC, PF_DEBUG_NOISY };
enum	{ PF_CHANGE_NONE, PF_CHANGE_ADD_HEAD, PF_CHANGE_ADD_TAIL,
	  PF_CHANGE_ADD_BEFORE, PF_CHANGE_ADD_AFTER,
	  PF_CHANGE_REMOVE, PF_CHANGE_GET_TICKET };
enum	{ PF_GET_NONE, PF_GET_CLR_CNTR };
enum	{ PF_SK_WIRE, PF_SK_STACK, PF_SK_BOTH };

/*
 * Note about PFTM_*: real indices into pf_rule.timeout[] come before
 * PFTM_MAX, special cases afterwards. See pf_state_expires().
 */
enum	{ PFTM_TCP_FIRST_PACKET, PFTM_TCP_OPENING, PFTM_TCP_ESTABLISHED,
	  PFTM_TCP_CLOSING, PFTM_TCP_FIN_WAIT, PFTM_TCP_CLOSED,
	  PFTM_UDP_FIRST_PACKET, PFTM_UDP_SINGLE, PFTM_UDP_MULTIPLE,
	  PFTM_ICMP_FIRST_PACKET, PFTM_ICMP_ERROR_REPLY,
	  PFTM_OTHER_FIRST_PACKET, PFTM_OTHER_SINGLE,
	  PFTM_OTHER_MULTIPLE, PFTM_FRAG, PFTM_INTERVAL,
	  PFTM_ADAPTIVE_START, PFTM_ADAPTIVE_END, PFTM_SRC_NODE,
	  PFTM_TS_DIFF, PFTM_MAX, PFTM_PURGE, PFTM_UNLINKED };

/* PFTM default values */
#define PFTM_TCP_FIRST_PACKET_VAL	120	/* First TCP packet */
#define PFTM_TCP_OPENING_VAL		30	/* No response yet */
#define PFTM_TCP_ESTABLISHED_VAL	24*60*60/* Established */
#define PFTM_TCP_CLOSING_VAL		15 * 60	/* Half closed */
#define PFTM_TCP_FIN_WAIT_VAL		45	/* Got both FINs */
#define PFTM_TCP_CLOSED_VAL		90	/* Got a RST */
#define PFTM_UDP_FIRST_PACKET_VAL	60	/* First UDP packet */
#define PFTM_UDP_SINGLE_VAL		30	/* Unidirectional */
#define PFTM_UDP_MULTIPLE_VAL		60	/* Bidirectional */
#define PFTM_ICMP_FIRST_PACKET_VAL	20	/* First ICMP packet */
#define PFTM_ICMP_ERROR_REPLY_VAL	10	/* Got error response */
#define PFTM_OTHER_FIRST_PACKET_VAL	60	/* First packet */
#define PFTM_OTHER_SINGLE_VAL		30	/* Unidirectional */
#define PFTM_OTHER_MULTIPLE_VAL		60	/* Bidirectional */
#define PFTM_FRAG_VAL			30	/* Fragment expire */
#define PFTM_INTERVAL_VAL		10	/* Expire interval */
#define PFTM_SRC_NODE_VAL		0	/* Source tracking */
#define PFTM_TS_DIFF_VAL		30	/* Allowed TS diff */

enum	{ PF_NOPFROUTE, PF_FASTROUTE, PF_ROUTETO, PF_DUPTO, PF_REPLYTO };
enum	{ PF_LIMIT_STATES, PF_LIMIT_SRC_NODES, PF_LIMIT_FRAGS,
	  PF_LIMIT_TABLE_ENTRIES, PF_LIMIT_MAX };
#define PF_POOL_IDMASK		0x0f
enum	{ PF_POOL_NONE, PF_POOL_BITMASK, PF_POOL_RANDOM,
	  PF_POOL_SRCHASH, PF_POOL_ROUNDROBIN };
enum	{ PF_ADDR_ADDRMASK, PF_ADDR_NOROUTE, PF_ADDR_DYNIFTL,
	  PF_ADDR_TABLE, PF_ADDR_URPFFAILED,
	  PF_ADDR_RANGE };
#define PF_POOL_TYPEMASK	0x0f
#define PF_POOL_STICKYADDR	0x20
#define	PF_WSCALE_FLAG		0x80
#define	PF_WSCALE_MASK		0x0f

#define	PF_LOG			0x01
#define	PF_LOG_ALL		0x02
#define	PF_LOG_SOCKET_LOOKUP	0x04

/* Reasons code for passing/dropping a packet */
#define PFRES_MATCH	0		/* Explicit match of a rule */
#define PFRES_BADOFF	1		/* Bad offset for pull_hdr */
#define PFRES_FRAG	2		/* Dropping following fragment */
#define PFRES_SHORT	3		/* Dropping short packet */
#define PFRES_NORM	4		/* Dropping by normalizer */
#define PFRES_MEMORY	5		/* Dropped due to lacking mem */
#define PFRES_TS	6		/* Bad TCP Timestamp (RFC1323) */
#define PFRES_CONGEST	7		/* Congestion (of ipintrq) */
#define PFRES_IPOPTIONS 8		/* IP option */
#define PFRES_PROTCKSUM 9		/* Protocol checksum invalid */
#define PFRES_BADSTATE	10		/* State mismatch */
#define PFRES_STATEINS	11		/* State insertion failure */
#define PFRES_MAXSTATES	12		/* State limit */
#define PFRES_SRCLIMIT	13		/* Source node/conn limit */
#define PFRES_SYNPROXY	14		/* SYN proxy */
#define PFRES_MAPFAILED	15		/* pf_map_addr() failed */
#define PFRES_MAX	16		/* total+1 */

#define PFRES_NAMES { \
	"match", \
	"bad-offset", \
	"fragment", \
	"short", \
	"normalize", \
	"memory", \
	"bad-timestamp", \
	"congestion", \
	"ip-option", \
	"proto-cksum", \
	"state-mismatch", \
	"state-insert", \
	"state-limit", \
	"src-limit", \
	"synproxy", \
	"map-failed", \
	NULL \
}

/* Counters for other things we want to keep track of */
#define LCNT_STATES		0	/* states */
#define LCNT_SRCSTATES		1	/* max-src-states */
#define LCNT_SRCNODES		2	/* max-src-nodes */
#define LCNT_SRCCONN		3	/* max-src-conn */
#define LCNT_SRCCONNRATE	4	/* max-src-conn-rate */
#define LCNT_OVERLOAD_TABLE	5	/* entry added to overload table */
#define LCNT_OVERLOAD_FLUSH	6	/* state entries flushed */
#define LCNT_MAX		7	/* total+1 */

#define LCNT_NAMES { \
	"max states per rule", \
	"max-src-states", \
	"max-src-nodes", \
	"max-src-conn", \
	"max-src-conn-rate", \
	"overload table insertion", \
	"overload flush states", \
	NULL \
}

/* state operation counters */
#define FCNT_STATE_SEARCH	0
#define FCNT_STATE_INSERT	1
#define FCNT_STATE_REMOVALS	2
#define FCNT_MAX		3

/* src_node operation counters */
#define SCNT_SRC_NODE_SEARCH	0
#define SCNT_SRC_NODE_INSERT	1
#define SCNT_SRC_NODE_REMOVALS	2
#define SCNT_MAX		3

#define	PF_TABLE_NAME_SIZE	32
#define	PF_QNAME_SIZE		64

struct pf_status {
	uint64_t	counters[PFRES_MAX];
	uint64_t	lcounters[LCNT_MAX];
	uint64_t	fcounters[FCNT_MAX];
	uint64_t	scounters[SCNT_MAX];
	uint64_t	pcounters[2][2][3];
	uint64_t	bcounters[2][2];
	uint32_t	running;
	uint32_t	states;
	uint32_t	src_nodes;
	uint32_t	since;
	uint32_t	debug;
	uint32_t	hostid;
	char		ifname[IFNAMSIZ];
	uint8_t		pf_chksum[PF_MD5_DIGEST_LENGTH];
};

#endif	/* _NET_PF_H_ */
