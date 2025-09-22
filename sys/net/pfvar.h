/*	$OpenBSD: pfvar.h,v 1.543 2025/04/14 20:02:34 sf Exp $ */

/*
 * Copyright (c) 2001 Daniel Hartmeier
 * Copyright (c) 2002 - 2013 Henning Brauer <henning@openbsd.org>
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
 */

#ifndef _NET_PFVAR_H_
#define _NET_PFVAR_H_

#include <sys/queue.h>
#include <sys/tree.h>
#include <sys/syslimits.h>
#include <sys/refcnt.h>
#include <sys/timeout.h>

#include <netinet/in.h>

#include <net/radix.h>
#include <net/route.h>

struct ip;
struct ip6_hdr;
struct mbuf_list;

#define	PF_TCPS_PROXY_SRC	((TCP_NSTATES)+0)
#define	PF_TCPS_PROXY_DST	((TCP_NSTATES)+1)

#define	PF_MD5_DIGEST_LENGTH	16
#ifdef MD5_DIGEST_LENGTH
#if PF_MD5_DIGEST_LENGTH != MD5_DIGEST_LENGTH
#error md5 digest length mismatch
#endif
#endif

typedef struct refcnt	pf_refcnt_t;
#define	PF_REF_INIT(_x)	refcnt_init(&(_x))
#define	PF_REF_TAKE(_x)	refcnt_take(&(_x))
#define	PF_REF_RELE(_x)	refcnt_rele(&(_x))

enum	{ PF_INOUT, PF_IN, PF_OUT, PF_FWD };
enum	{ PF_PASS, PF_DROP, PF_SCRUB, PF_NOSCRUB, PF_NAT, PF_NONAT,
	  PF_BINAT, PF_NOBINAT, PF_RDR, PF_NORDR, PF_SYNPROXY_DROP, PF_DEFER,
	  PF_MATCH, PF_DIVERT, PF_RT, PF_AFRT };
enum	{ PF_TRANS_RULESET, PF_TRANS_TABLE };
enum	{ PF_OP_NONE, PF_OP_IRG, PF_OP_EQ, PF_OP_NE, PF_OP_LT,
	  PF_OP_LE, PF_OP_GT, PF_OP_GE, PF_OP_XRG, PF_OP_RRG };
enum	{ PF_CHANGE_NONE, PF_CHANGE_ADD_HEAD, PF_CHANGE_ADD_TAIL,
	  PF_CHANGE_ADD_BEFORE, PF_CHANGE_ADD_AFTER,
	  PF_CHANGE_REMOVE, PF_CHANGE_GET_TICKET };
enum	{ PF_GET_NONE, PF_GET_CLR_CNTR };
enum	{ PF_SK_WIRE, PF_SK_STACK, PF_SK_BOTH };
enum	{ PF_PEER_SRC, PF_PEER_DST, PF_PEER_BOTH };

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
#define PFTM_FRAG_VAL			60	/* Fragment expire */
#define PFTM_INTERVAL_VAL		10	/* Expire interval */
#define PFTM_SRC_NODE_VAL		0	/* Source tracking */
#define PFTM_TS_DIFF_VAL		30	/* Allowed TS diff */

/*
 * For each connection (combination of proto,src,dst,af) the number
 * of fragments is limited.  Over the PFTM_FRAG interval the average
 * rate must be less than PF_FRAG_STALE fragments per second.
 * Otherwise older fragments are considered stale and are dropped.
 */
#define PF_FRAG_STALE			200

/*
 * Limit the length of the fragment queue traversal.  Remember
 * search entry points based on the fragment offset.
 */
#define PF_FRAG_ENTRY_POINTS		16

/*
 * The number of entries in the fragment queue must be limited
 * to avoid DoS by linear searching.  Instead of a global limit,
 * use a limit per entry point.  For large packets these sum up.
 */
#define PF_FRAG_ENTRY_LIMIT		64

enum	{ PF_NOPFROUTE, PF_ROUTETO, PF_DUPTO, PF_REPLYTO };
enum	{ PF_LIMIT_STATES, PF_LIMIT_SRC_NODES, PF_LIMIT_FRAGS,
	  PF_LIMIT_TABLES, PF_LIMIT_TABLE_ENTRIES, PF_LIMIT_PKTDELAY_PKTS,
	  PF_LIMIT_ANCHORS, PF_LIMIT_MAX };
#define PF_POOL_IDMASK		0x0f
enum	{ PF_POOL_NONE, PF_POOL_BITMASK, PF_POOL_RANDOM,
	  PF_POOL_SRCHASH, PF_POOL_ROUNDROBIN, PF_POOL_LEASTSTATES };
enum	{ PF_ADDR_ADDRMASK, PF_ADDR_NOROUTE, PF_ADDR_DYNIFTL,
	  PF_ADDR_TABLE, PF_ADDR_RTLABEL, PF_ADDR_URPFFAILED,
	  PF_ADDR_RANGE, PF_ADDR_NONE };
#define PF_POOL_TYPEMASK	0x0f
#define PF_POOL_STICKYADDR	0x20
#define	PF_WSCALE_FLAG		0x80
#define	PF_WSCALE_MASK		0x0f

#define PF_POOL_DYNTYPE(_o)						\
	((((_o) & PF_POOL_TYPEMASK) == PF_POOL_ROUNDROBIN) ||		\
	(((_o) & PF_POOL_TYPEMASK) == PF_POOL_LEASTSTATES) ||		\
	(((_o) & PF_POOL_TYPEMASK) == PF_POOL_RANDOM) ||		\
	(((_o) & PF_POOL_TYPEMASK) == PF_POOL_SRCHASH))

#define	PF_LOG			0x01
#define	PF_LOG_ALL		0x02
#define	PF_LOG_USER		0x04
#define	PF_LOG_FORCE		0x08
#define	PF_LOG_MATCHES		0x10

struct pf_addr {
	union {
		struct in_addr		v4;
		struct in6_addr		v6;
		u_int8_t		addr8[16];
		u_int16_t		addr16[8];
		u_int32_t		addr32[4];
	} pfa;		    /* 128-bit address */
#define v4	pfa.v4
#define v6	pfa.v6
#define addr8	pfa.addr8
#define addr16	pfa.addr16
#define addr32	pfa.addr32
};

#define	PF_TABLE_NAME_SIZE	 32

#define PFI_AFLAG_NETWORK	0x01
#define PFI_AFLAG_BROADCAST	0x02
#define PFI_AFLAG_PEER		0x04
#define PFI_AFLAG_MODEMASK	0x07
#define PFI_AFLAG_NOALIAS	0x08

struct pf_addr_wrap {
	union {
		struct {
			struct pf_addr		 addr;
			struct pf_addr		 mask;
		}			 a;
		char			 ifname[IFNAMSIZ];
		char			 tblname[PF_TABLE_NAME_SIZE];
		char			 rtlabelname[RTLABEL_LEN];
		u_int32_t		 rtlabel;
	}			 v;
	union {
		struct pfi_dynaddr	*dyn;
		struct pfr_ktable	*tbl;
		int			 dyncnt;
		int			 tblcnt;
	}			 p;
	u_int8_t		 type;		/* PF_ADDR_* */
	u_int8_t		 iflags;	/* PFI_AFLAG_* */
};

#ifdef _KERNEL
struct pfi_dynaddr {
	TAILQ_ENTRY(pfi_dynaddr)	 entry;
	struct pf_addr			 pfid_addr4;
	struct pf_addr			 pfid_mask4;
	struct pf_addr			 pfid_addr6;
	struct pf_addr			 pfid_mask6;
	struct pfr_ktable		*pfid_kt;
	struct pfi_kif			*pfid_kif;
	void				*pfid_hook_cookie;
	int				 pfid_net;	/* mask or 128 */
	int				 pfid_acnt4;	/* address count IPv4 */
	int				 pfid_acnt6;	/* address count IPv6 */
	sa_family_t			 pfid_af;	/* rule af */
	u_int8_t			 pfid_iflags;	/* PFI_AFLAG_* */
};
#endif /* _KERNEL */


/*
 * Logging macros
 */

#ifndef PF_DEBUGNAME
#define PF_DEBUGNAME "pf: "
#endif

#ifdef _KERNEL
#define	DPFPRINTF(n, format, x...)					\
	do {								\
		if (pf_status.debug >= (n)) {				\
			log(n, PF_DEBUGNAME);				\
			addlog(format, ##x);				\
			addlog("\n");					\
		}							\
	} while (0)
#else
#ifdef PFDEBUG
#define	DPFPRINTF(n, format, x...)					\
	do {								\
		fprintf(stderr, format, ##x);				\
		fprintf(stderr, "\n");					\
	} while (0)
#else
#define	DPFPRINTF(n, format, x...)	((void)0)
#endif /* PFDEBUG */
#endif /* _KERNEL */


/*
 * Address manipulation macros
 */

#define PF_AEQ(a, b, c) \
	((c == AF_INET && (a)->addr32[0] == (b)->addr32[0]) || \
	(c == AF_INET6 && \
	(a)->addr32[3] == (b)->addr32[3] && \
	(a)->addr32[2] == (b)->addr32[2] && \
	(a)->addr32[1] == (b)->addr32[1] && \
	(a)->addr32[0] == (b)->addr32[0])) \

#define PF_ANEQ(a, b, c) \
	((c == AF_INET && (a)->addr32[0] != (b)->addr32[0]) || \
	(c == AF_INET6 && \
	((a)->addr32[3] != (b)->addr32[3] || \
	(a)->addr32[2] != (b)->addr32[2] || \
	(a)->addr32[1] != (b)->addr32[1] || \
	(a)->addr32[0] != (b)->addr32[0]))) \

#define PF_AZERO(a, c) \
	((c == AF_INET && !(a)->addr32[0]) || \
	(c == AF_INET6 && \
	!(a)->addr32[0] && !(a)->addr32[1] && \
	!(a)->addr32[2] && !(a)->addr32[3] )) \

#define	PF_MISMATCHAW(aw, x, af, neg, ifp, rtid)			\
	(								\
		(((aw)->type == PF_ADDR_NOROUTE &&			\
		    pf_routable((x), (af), NULL, (rtid))) ||		\
		(((aw)->type == PF_ADDR_URPFFAILED && (ifp) != NULL &&	\
		    pf_routable((x), (af), (ifp), (rtid))) ||		\
		((aw)->type == PF_ADDR_RTLABEL &&			\
		    !pf_rtlabel_match((x), (af), (aw), (rtid))) ||	\
		((aw)->type == PF_ADDR_TABLE &&				\
		    !pfr_match_addr((aw)->p.tbl, (x), (af))) ||		\
		((aw)->type == PF_ADDR_DYNIFTL &&			\
		    !pfi_match_addr((aw)->p.dyn, (x), (af))) ||		\
		((aw)->type == PF_ADDR_RANGE &&				\
		    !pf_match_addr_range(&(aw)->v.a.addr,		\
		    &(aw)->v.a.mask, (x), (af))) ||			\
		((aw)->type == PF_ADDR_ADDRMASK &&			\
		    !PF_AZERO(&(aw)->v.a.mask, (af)) &&			\
		    !pf_match_addr(0, &(aw)->v.a.addr,			\
		    &(aw)->v.a.mask, (x), (af))))) !=			\
		(neg)							\
	)

struct pf_rule_uid {
	uid_t		 uid[2];
	u_int8_t	 op;
};

struct pf_rule_gid {
	uid_t		 gid[2];
	u_int8_t	 op;
};

struct pf_rule_addr {
	struct pf_addr_wrap	 addr;
	u_int16_t		 port[2];
	u_int8_t		 neg;
	u_int8_t		 port_op;
	u_int16_t		 weight;
};

struct pf_threshold {
	u_int32_t	limit;
#define	PF_THRESHOLD_MULT	1000
#define	PF_THRESHOLD_MAX	0xffffffff / PF_THRESHOLD_MULT
	u_int32_t	seconds;
	u_int32_t	count;
	u_int32_t	last;
};

struct pf_poolhashkey {
	union {
		u_int8_t		key8[16];
		u_int16_t		key16[8];
		u_int32_t		key32[4];
	} pfk;		    /* 128-bit hash key */
#define key8	pfk.key8
#define key16	pfk.key16
#define key32	pfk.key32
};

struct pf_pool {
	struct pf_addr_wrap	 addr;
	struct pf_poolhashkey	 key;
	struct pf_addr		 counter;
	char			 ifname[IFNAMSIZ];
	struct pfi_kif		*kif;
	int			 tblidx;
	u_int64_t		 states;
	int			 curweight;
	u_int16_t		 weight;
	u_int16_t		 proxy_port[2];
	u_int8_t		 port_op;
	u_int8_t		 opts;
};

/* A packed Operating System description for fingerprinting */
typedef u_int32_t pf_osfp_t;
#define PF_OSFP_ANY	((pf_osfp_t)0)
#define PF_OSFP_UNKNOWN	((pf_osfp_t)-1)
#define PF_OSFP_NOMATCH	((pf_osfp_t)-2)

struct pf_osfp_entry {
	SLIST_ENTRY(pf_osfp_entry) fp_entry;
	pf_osfp_t		fp_os;
	int			fp_enflags;
#define PF_OSFP_EXPANDED	0x001		/* expanded entry */
#define PF_OSFP_GENERIC		0x002		/* generic signature */
#define PF_OSFP_NODETAIL	0x004		/* no p0f details */
#define PF_OSFP_LEN	32
	u_char			fp_class_nm[PF_OSFP_LEN];
	u_char			fp_version_nm[PF_OSFP_LEN];
	u_char			fp_subtype_nm[PF_OSFP_LEN];
};
#define PF_OSFP_ENTRY_EQ(a, b) \
    ((a)->fp_os == (b)->fp_os && \
    memcmp((a)->fp_class_nm, (b)->fp_class_nm, PF_OSFP_LEN) == 0 && \
    memcmp((a)->fp_version_nm, (b)->fp_version_nm, PF_OSFP_LEN) == 0 && \
    memcmp((a)->fp_subtype_nm, (b)->fp_subtype_nm, PF_OSFP_LEN) == 0)

/* handle pf_osfp_t packing */
#define _FP_RESERVED_BIT	1  /* For the special negative #defines */
#define _FP_UNUSED_BITS		1
#define _FP_CLASS_BITS		10 /* OS Class (Windows, Linux) */
#define _FP_VERSION_BITS	10 /* OS version (95, 98, NT, 2.4.54, 3.2) */
#define _FP_SUBTYPE_BITS	10 /* patch level (NT SP4, SP3, ECN patch) */
#define PF_OSFP_UNPACK(osfp, class, version, subtype) do { \
	(class) = ((osfp) >> (_FP_VERSION_BITS+_FP_SUBTYPE_BITS)) & \
	    ((1 << _FP_CLASS_BITS) - 1); \
	(version) = ((osfp) >> _FP_SUBTYPE_BITS) & \
	    ((1 << _FP_VERSION_BITS) - 1);\
	(subtype) = (osfp) & ((1 << _FP_SUBTYPE_BITS) - 1); \
} while(0)
#define PF_OSFP_PACK(osfp, class, version, subtype) do { \
	(osfp) = ((class) & ((1 << _FP_CLASS_BITS) - 1)) << (_FP_VERSION_BITS \
	    + _FP_SUBTYPE_BITS); \
	(osfp) |= ((version) & ((1 << _FP_VERSION_BITS) - 1)) << \
	    _FP_SUBTYPE_BITS; \
	(osfp) |= (subtype) & ((1 << _FP_SUBTYPE_BITS) - 1); \
} while(0)

/* the fingerprint of an OSes TCP SYN packet */
typedef u_int64_t	pf_tcpopts_t;
struct pf_os_fingerprint {
	SLIST_HEAD(pf_osfp_enlist, pf_osfp_entry) fp_oses; /* list of matches */
	pf_tcpopts_t		fp_tcpopts;	/* packed TCP options */
	u_int16_t		fp_wsize;	/* TCP window size */
	u_int16_t		fp_psize;	/* ip->ip_len */
	u_int16_t		fp_mss;		/* TCP MSS */
	u_int16_t		fp_flags;
#define PF_OSFP_WSIZE_MOD	0x0001		/* Window modulus */
#define PF_OSFP_WSIZE_DC	0x0002		/* Window don't care */
#define PF_OSFP_WSIZE_MSS	0x0004		/* Window multiple of MSS */
#define PF_OSFP_WSIZE_MTU	0x0008		/* Window multiple of MTU */
#define PF_OSFP_PSIZE_MOD	0x0010		/* packet size modulus */
#define PF_OSFP_PSIZE_DC	0x0020		/* packet size don't care */
#define PF_OSFP_WSCALE		0x0040		/* TCP window scaling */
#define PF_OSFP_WSCALE_MOD	0x0080		/* TCP window scale modulus */
#define PF_OSFP_WSCALE_DC	0x0100		/* TCP window scale dont-care */
#define PF_OSFP_MSS		0x0200		/* TCP MSS */
#define PF_OSFP_MSS_MOD		0x0400		/* TCP MSS modulus */
#define PF_OSFP_MSS_DC		0x0800		/* TCP MSS dont-care */
#define PF_OSFP_DF		0x1000		/* IPv4 don't fragment bit */
#define PF_OSFP_TS0		0x2000		/* Zero timestamp */
#define PF_OSFP_INET6		0x4000		/* IPv6 */
	u_int8_t		fp_optcnt;	/* TCP option count */
	u_int8_t		fp_wscale;	/* TCP window scaling */
	u_int8_t		fp_ttl;		/* IPv4 TTL */
#define PF_OSFP_MAXTTL_OFFSET	40
/* TCP options packing */
#define PF_OSFP_TCPOPT_NOP	0x0		/* TCP NOP option */
#define PF_OSFP_TCPOPT_WSCALE	0x1		/* TCP window scaling option */
#define PF_OSFP_TCPOPT_MSS	0x2		/* TCP max segment size opt */
#define PF_OSFP_TCPOPT_SACK	0x3		/* TCP SACK OK option */
#define PF_OSFP_TCPOPT_TS	0x4		/* TCP timestamp option */
#define PF_OSFP_TCPOPT_BITS	3		/* bits used by each option */
#define PF_OSFP_MAX_OPTS \
    (sizeof(((struct pf_os_fingerprint *)0)->fp_tcpopts) * 8) \
    / PF_OSFP_TCPOPT_BITS

	SLIST_ENTRY(pf_os_fingerprint)	fp_next;
};

struct pf_osfp_ioctl {
	struct pf_osfp_entry	fp_os;
	pf_tcpopts_t		fp_tcpopts;	/* packed TCP options */
	u_int16_t		fp_wsize;	/* TCP window size */
	u_int16_t		fp_psize;	/* ip->ip_len */
	u_int16_t		fp_mss;		/* TCP MSS */
	u_int16_t		fp_flags;
	u_int8_t		fp_optcnt;	/* TCP option count */
	u_int8_t		fp_wscale;	/* TCP window scaling */
	u_int8_t		fp_ttl;		/* IPv4 TTL */

	int			fp_getnum;	/* DIOCOSFPGET number */
};

struct pf_rule_actions {
	int		rtableid;
	u_int16_t	qid;
	u_int16_t	pqid;
	u_int16_t	max_mss;
	u_int16_t	flags;
	u_int16_t	delay;
	u_int8_t	log;
	u_int8_t	set_tos;
	u_int8_t	min_ttl;
	u_int8_t	set_prio[2];
	u_int8_t	pad[1];
};

union pf_rule_ptr {
	struct pf_rule		*ptr;
	u_int32_t		 nr;
};

#define	PF_ANCHOR_STACK_MAX	 64
#define	PF_ANCHOR_NAME_SIZE	 64
#define	PF_ANCHOR_MAXPATH	(PATH_MAX - PF_ANCHOR_NAME_SIZE - 1)
#define	PF_ANCHOR_HIWAT		 512
#define	PF_OPTIMIZER_TABLE_PFX	"__automatic_"

struct pf_rule {
	struct pf_rule_addr	 src;
	struct pf_rule_addr	 dst;
#define PF_SKIP_IFP		0
#define PF_SKIP_DIR		1
#define PF_SKIP_RDOM		2
#define PF_SKIP_AF		3
#define PF_SKIP_PROTO		4
#define PF_SKIP_SRC_ADDR	5
#define PF_SKIP_DST_ADDR	6
#define PF_SKIP_SRC_PORT	7
#define PF_SKIP_DST_PORT	8
#define PF_SKIP_COUNT		9
	union pf_rule_ptr	 skip[PF_SKIP_COUNT];
#define PF_RULE_LABEL_SIZE	 64
	char			 label[PF_RULE_LABEL_SIZE];
#define PF_QNAME_SIZE		 64
	char			 ifname[IFNAMSIZ];
	char			 rcv_ifname[IFNAMSIZ];
	char			 qname[PF_QNAME_SIZE];
	char			 pqname[PF_QNAME_SIZE];
#define	PF_TAG_NAME_SIZE	 64
	char			 tagname[PF_TAG_NAME_SIZE];
	char			 match_tagname[PF_TAG_NAME_SIZE];

	char			 overload_tblname[PF_TABLE_NAME_SIZE];

	TAILQ_ENTRY(pf_rule)	 entries;
	struct pf_pool		 nat;
	struct pf_pool		 rdr;
	struct pf_pool		 route;
	struct pf_threshold	 pktrate;

	u_int64_t		 evaluations;
	u_int64_t		 packets[2];
	u_int64_t		 bytes[2];

	struct pfi_kif		*kif;
	struct pfi_kif		*rcv_kif;
	struct pf_anchor	*anchor;
	struct pfr_ktable	*overload_tbl;

	pf_osfp_t		 os_fingerprint;

	int			 rtableid;
	int			 onrdomain;
	u_int32_t		 timeout[PFTM_MAX];
	u_int32_t		 states_cur;
	u_int32_t		 states_tot;
	u_int32_t		 max_states;
	u_int32_t		 src_nodes;
	u_int32_t		 max_src_nodes;
	u_int32_t		 max_src_states;
	u_int32_t		 max_src_conn;
	struct {
		u_int32_t		limit;
		u_int32_t		seconds;
	}			 max_src_conn_rate;
	u_int32_t		 qid;
	u_int32_t		 pqid;
	u_int32_t		 rt_listid;
	u_int32_t		 nr;
	u_int32_t		 prob;
	uid_t			 cuid;
	pid_t			 cpid;

	u_int16_t		 return_icmp;
	u_int16_t		 return_icmp6;
	u_int16_t		 max_mss;
	u_int16_t		 tag;
	u_int16_t		 match_tag;
	u_int16_t		 scrub_flags;
	u_int16_t		 delay;

	struct pf_rule_uid	 uid;
	struct pf_rule_gid	 gid;

	u_int32_t		 rule_flag;
	u_int8_t		 action;
	u_int8_t		 direction;
	u_int8_t		 log;
	u_int8_t		 logif;
	u_int8_t		 quick;
	u_int8_t		 ifnot;
	u_int8_t		 match_tag_not;

#define PF_STATE_NORMAL		0x1
#define PF_STATE_MODULATE	0x2
#define PF_STATE_SYNPROXY	0x3
	u_int8_t		 keep_state;
	sa_family_t		 af;
	u_int8_t		 proto;
	u_int16_t		 type;	/* aux. value 256 is legit */
	u_int16_t		 code;	/* aux. value 256 is legit */
	u_int8_t		 flags;
	u_int8_t		 flagset;
	u_int8_t		 min_ttl;
	u_int8_t		 allow_opts;
	u_int8_t		 rt;
	u_int8_t		 return_ttl;
	u_int8_t		 tos;
	u_int8_t		 set_tos;
	u_int8_t		 anchor_relative;
	u_int8_t		 anchor_wildcard;

#define PF_FLUSH		0x01
#define PF_FLUSH_GLOBAL		0x02
	u_int8_t		 flush;
	u_int8_t		 prio;
	u_int8_t		 set_prio[2];
	sa_family_t		 naf;
	u_int8_t		 rcvifnot;

	struct {
		struct pf_addr		addr;
		u_int16_t		port;
		u_int8_t		type;
	}			divert;

	time_t			 exptime;
};

/* rule flags */
#define	PFRULE_DROP		0x0000
#define	PFRULE_RETURNRST	0x0001
#define	PFRULE_FRAGMENT		0x0002
#define	PFRULE_RETURNICMP	0x0004
#define	PFRULE_RETURN		0x0008
#define	PFRULE_NOSYNC		0x0010
#define	PFRULE_SRCTRACK		0x0020  /* track source states */
#define	PFRULE_RULESRCTRACK	0x0040  /* per rule */
#define	PFRULE_SETDELAY		0x0080

/* rule flags again */
#define PFRULE_IFBOUND		0x00010000	/* if-bound */
#define PFRULE_STATESLOPPY	0x00020000	/* sloppy state tracking */
#define PFRULE_PFLOW		0x00040000
#define PFRULE_ONCE		0x00100000	/* one shot rule */
#define PFRULE_AFTO		0x00200000	/* af-to rule */
#define	PFRULE_EXPIRED		0x00400000	/* one shot rule hit by pkt */

#define PFSTATE_HIWAT		100000	/* default state table size */
#define PFSTATE_ADAPT_START	60000	/* default adaptive timeout start */
#define PFSTATE_ADAPT_END	120000	/* default adaptive timeout end */
#define	PF_PKTDELAY_MAXPKTS	10000	/* max # of pkts held in delay queue */

struct pf_rule_item {
	SLIST_ENTRY(pf_rule_item)	 entry;
	struct pf_rule			*r;
};

SLIST_HEAD(pf_rule_slist, pf_rule_item);

enum pf_sn_types { PF_SN_NONE, PF_SN_NAT, PF_SN_RDR, PF_SN_ROUTE, PF_SN_MAX };

struct pf_src_node {
	RB_ENTRY(pf_src_node)	 entry;
	struct pf_addr		 addr;
	struct pf_addr		 raddr;
	union pf_rule_ptr	 rule;
	struct pfi_kif		*kif;
	u_int64_t		 bytes[2];
	u_int64_t		 packets[2];
	u_int32_t		 states;
	u_int32_t		 conn;
	struct pf_threshold	 conn_rate;
	int32_t			 creation;
	int32_t			 expire;
	sa_family_t		 af;
	sa_family_t		 naf;
	u_int8_t		 type;
};

struct pf_sn_item {
	SLIST_ENTRY(pf_sn_item)	 next;
	struct pf_src_node	*sn;
};

SLIST_HEAD(pf_sn_head, pf_sn_item);

#define PFSNODE_HIWAT		10000	/* default source node table size */

struct pf_state_scrub {
	struct timeval	pfss_last;	/* time received last packet	*/
	u_int32_t	pfss_tsecr;	/* last echoed timestamp	*/
	u_int32_t	pfss_tsval;	/* largest timestamp		*/
	u_int32_t	pfss_tsval0;	/* original timestamp		*/
	u_int16_t	pfss_flags;
#define PFSS_TIMESTAMP	0x0001		/* modulate timestamp		*/
#define PFSS_PAWS	0x0010		/* stricter PAWS checks		*/
#define PFSS_PAWS_IDLED	0x0020		/* was idle too long.  no PAWS	*/
#define PFSS_DATA_TS	0x0040		/* timestamp on data packets	*/
#define PFSS_DATA_NOTS	0x0080		/* no timestamp on data packets	*/
	u_int8_t	pfss_ttl;	/* stashed TTL			*/
	u_int8_t	pad;
	u_int32_t	pfss_ts_mod;	/* timestamp modulation		*/
};

struct pf_state_host {
	struct pf_addr	addr;
	u_int16_t	port;
	u_int16_t	pad;
};

struct pf_state_peer {
	struct pf_state_scrub	*scrub;	/* state is scrubbed		*/
	u_int32_t	seqlo;		/* Max sequence number sent	*/
	u_int32_t	seqhi;		/* Max the other end ACKd + win	*/
	u_int32_t	seqdiff;	/* Sequence number modulator	*/
	u_int16_t	max_win;	/* largest window (pre scaling)	*/
	u_int16_t	mss;		/* Maximum segment size option	*/
	u_int8_t	state;		/* active state level		*/
	u_int8_t	wscale;		/* window scaling factor	*/
	u_int8_t	tcp_est;	/* Did we reach TCPS_ESTABLISHED */
	u_int8_t	pad[1];
};

TAILQ_HEAD(pf_state_queue, pf_state);

/* keep synced with struct pf_state_key, used in RB_FIND */
struct pf_state_key_cmp {
	struct pf_addr	 addr[2];
	u_int16_t	 port[2];
	u_int16_t	 rdomain;
	u_int16_t	 hash;
	sa_family_t	 af;
	u_int8_t	 proto;
};

/* keep synced with struct pf_state, used in RB_FIND */
struct pf_state_cmp {
	u_int64_t		 id;
	u_int32_t		 creatorid;
	u_int8_t		 direction;
	u_int8_t		 pad[3];
};

/* struct pf_state.state_flags */
#define	PFSTATE_ALLOWOPTS	0x0001
#define	PFSTATE_SLOPPY		0x0002
#define	PFSTATE_PFLOW		0x0004
#define	PFSTATE_NOSYNC		0x0008
#define	PFSTATE_ACK		0x0010
#define	PFSTATE_NODF		0x0020
#define	PFSTATE_SETTOS		0x0040
#define	PFSTATE_RANDOMID	0x0080
#define	PFSTATE_SCRUB_TCP	0x0100
#define	PFSTATE_SETPRIO		0x0200
#define	PFSTATE_INP_UNLINKED	0x0400
#define	PFSTATE_SCRUBMASK (PFSTATE_NODF|PFSTATE_RANDOMID|PFSTATE_SCRUB_TCP)
#define	PFSTATE_SETMASK   (PFSTATE_SETTOS|PFSTATE_SETPRIO)

/*
 * Unified state structures for pulling states out of the kernel
 * used by pfsync(4) and the pf(4) ioctl.
 */
struct pfsync_state_scrub {
	u_int16_t	pfss_flags;
	u_int8_t	pfss_ttl;	/* stashed TTL		*/
#define PFSYNC_SCRUB_FLAG_VALID		0x01
	u_int8_t	scrub_flag;
	u_int32_t	pfss_ts_mod;	/* timestamp modulation	*/
} __packed;

struct pfsync_state_peer {
	struct pfsync_state_scrub scrub;	/* state is scrubbed	*/
	u_int32_t	seqlo;		/* Max sequence number sent	*/
	u_int32_t	seqhi;		/* Max the other end ACKd + win	*/
	u_int32_t	seqdiff;	/* Sequence number modulator	*/
	u_int16_t	max_win;	/* largest window (pre scaling)	*/
	u_int16_t	mss;		/* Maximum segment size option	*/
	u_int8_t	state;		/* active state level		*/
	u_int8_t	wscale;		/* window scaling factor	*/
	u_int8_t	pad[6];
} __packed;

struct pfsync_state_key {
	struct pf_addr	 addr[2];
	u_int16_t	 port[2];
	u_int16_t	 rdomain;
	sa_family_t	 af;
	u_int8_t	 pad;
};

struct pfsync_state {
	u_int64_t	 id;
	char		 ifname[IFNAMSIZ];
	struct pfsync_state_key	key[2];
	struct pfsync_state_peer src;
	struct pfsync_state_peer dst;
	struct pf_addr	 rt_addr;
	u_int32_t	 rule;
	u_int32_t	 anchor;
	u_int32_t	 nat_rule;
	u_int32_t	 creation;
	u_int32_t	 expire;
	u_int32_t	 packets[2][2];
	u_int32_t	 bytes[2][2];
	u_int32_t	 creatorid;
	int32_t		 rtableid[2];
	u_int16_t	 max_mss;
	sa_family_t	 af;
	u_int8_t	 proto;
	u_int8_t	 direction;
	u_int8_t	 log;
	u_int8_t	 rt;
	u_int8_t	 timeout;
	u_int8_t	 sync_flags;
	u_int8_t	 updates;
	u_int8_t	 min_ttl;
	u_int8_t	 set_tos;
	u_int16_t	 state_flags;
	u_int8_t	 set_prio[2];
} __packed;

#define PFSYNC_FLAG_SRCNODE	0x04
#define PFSYNC_FLAG_NATSRCNODE	0x08

#define pf_state_counter_hton(s,d) do {				\
	d[0] = htonl((s>>32)&0xffffffff);			\
	d[1] = htonl(s&0xffffffff);				\
} while (0)

#define pf_state_counter_from_pfsync(s)				\
	(((u_int64_t)(s[0])<<32) | (u_int64_t)(s[1]))

#define pf_state_counter_ntoh(s,d) do {				\
	d = ntohl(s[0]);					\
	d = d<<32;						\
	d += ntohl(s[1]);					\
} while (0)

TAILQ_HEAD(pf_rulequeue, pf_rule);

struct pf_anchor;

struct pf_ruleset {
	struct {
		struct pf_rulequeue	 queues[2];
		struct {
			struct pf_rulequeue	*ptr;
			u_int32_t		 rcount;
			u_int32_t		 version;
			int			 open;
		}			 active, inactive;
	}			 rules;
	struct pf_anchor	*anchor;
	u_int32_t		 tticket;
	int			 tables;
	int			 topen;
};

RB_HEAD(pf_anchor_global, pf_anchor);
RB_HEAD(pf_anchor_node, pf_anchor);
struct pf_anchor {
	RB_ENTRY(pf_anchor)	 entry_global;
	RB_ENTRY(pf_anchor)	 entry_node;
	struct pf_anchor	*parent;
	struct pf_anchor_node	 children;
	char			 name[PF_ANCHOR_NAME_SIZE];
	char			 path[PATH_MAX];
	struct pf_ruleset	 ruleset;
	int			 refcnt;	/* anchor rules */
	int			 match;
	struct refcnt		 ref;		/* for transactions */
};
RB_PROTOTYPE(pf_anchor_global, pf_anchor, entry_global, pf_anchor_compare)
RB_PROTOTYPE(pf_anchor_node, pf_anchor, entry_node, pf_anchor_compare)

#define PF_RESERVED_ANCHOR	"_pf"

#define PFR_TFLAG_PERSIST	0x00000001
#define PFR_TFLAG_CONST		0x00000002
#define PFR_TFLAG_ACTIVE	0x00000004
#define PFR_TFLAG_INACTIVE	0x00000008
#define PFR_TFLAG_REFERENCED	0x00000010
#define PFR_TFLAG_REFDANCHOR	0x00000020
#define PFR_TFLAG_COUNTERS	0x00000040
/* Adjust masks below when adding flags. */
#define PFR_TFLAG_USRMASK	0x00000043
#define PFR_TFLAG_SETMASK	0x0000003C
#define PFR_TFLAG_ALLMASK	0x0000007F

struct pfr_table {
	char			 pfrt_anchor[PATH_MAX];
	char			 pfrt_name[PF_TABLE_NAME_SIZE];
	u_int32_t		 pfrt_flags;
	u_int8_t		 pfrt_fback;
};

enum { PFR_FB_NONE, PFR_FB_MATCH, PFR_FB_ADDED, PFR_FB_DELETED,
	PFR_FB_CHANGED, PFR_FB_CLEARED, PFR_FB_DUPLICATE,
	PFR_FB_NOTMATCH, PFR_FB_CONFLICT, PFR_FB_NOCOUNT, PFR_FB_MAX };

struct pfr_addr {
	union {
		struct in_addr	 _pfra_ip4addr;
		struct in6_addr	 _pfra_ip6addr;
	}		 pfra_u;
	char		 pfra_ifname[IFNAMSIZ];
	u_int32_t	 pfra_states;
	u_int16_t	 pfra_weight;
	u_int8_t	 pfra_af;
	u_int8_t	 pfra_net;
	u_int8_t	 pfra_not;
	u_int8_t	 pfra_fback;
	u_int8_t	 pfra_type;
	u_int8_t	 pad[7];
};
#define	pfra_ip4addr	pfra_u._pfra_ip4addr
#define	pfra_ip6addr	pfra_u._pfra_ip6addr

enum { PFR_DIR_IN, PFR_DIR_OUT, PFR_DIR_MAX };
enum { PFR_OP_BLOCK, PFR_OP_MATCH, PFR_OP_PASS, PFR_OP_ADDR_MAX,
    PFR_OP_TABLE_MAX };
#define PFR_OP_XPASS	PFR_OP_ADDR_MAX

struct pfr_astats {
	struct pfr_addr	 pfras_a;
	u_int64_t	 pfras_packets[PFR_DIR_MAX][PFR_OP_ADDR_MAX];
	u_int64_t	 pfras_bytes[PFR_DIR_MAX][PFR_OP_ADDR_MAX];
	time_t		 pfras_tzero;
};

enum { PFR_REFCNT_RULE, PFR_REFCNT_ANCHOR, PFR_REFCNT_MAX };

struct pfr_tstats {
	struct pfr_table pfrts_t;
	u_int64_t	 pfrts_packets[PFR_DIR_MAX][PFR_OP_TABLE_MAX];
	u_int64_t	 pfrts_bytes[PFR_DIR_MAX][PFR_OP_TABLE_MAX];
	u_int64_t	 pfrts_match;
	u_int64_t	 pfrts_nomatch;
	time_t		 pfrts_tzero;
	int		 pfrts_cnt;
	int		 pfrts_refcnt[PFR_REFCNT_MAX];
};
#define	pfrts_name	pfrts_t.pfrt_name
#define pfrts_flags	pfrts_t.pfrt_flags

struct pfr_kcounters {
	u_int64_t		 pfrkc_packets[PFR_DIR_MAX][PFR_OP_ADDR_MAX];
	u_int64_t		 pfrkc_bytes[PFR_DIR_MAX][PFR_OP_ADDR_MAX];
	u_int64_t		 states;
};

/*
 * XXX ip_ipsp.h's sockaddr_union should be converted to sockaddr *
 * passing with correct sa_len, then a good approach for cleaning this
 * will become more clear.
 */
union pfsockaddr_union {
	struct sockaddr		sa;
	struct sockaddr_in	sin;
	struct sockaddr_in6	sin6;
};

SLIST_HEAD(pfr_kentryworkq, pfr_kentry);
struct _pfr_kentry {
	struct radix_node	 _pfrke_node[2];
	union pfsockaddr_union	 _pfrke_sa;
	SLIST_ENTRY(pfr_kentry)	 _pfrke_workq;
	SLIST_ENTRY(pfr_kentry)	 _pfrke_ioq;
	struct pfr_kcounters	*_pfrke_counters;
	time_t			 _pfrke_tzero;
	u_int8_t		 _pfrke_af;
	u_int8_t		 _pfrke_net;
	u_int8_t		 _pfrke_flags;
	u_int8_t		 _pfrke_type;
	u_int8_t		 _pfrke_fb;
};
#define PFRKE_FLAG_NOT		0x01
#define PFRKE_FLAG_MARK		0x02

/* pfrke_type */
enum { PFRKE_PLAIN, PFRKE_ROUTE, PFRKE_COST, PFRKE_MAX };

struct pfr_kentry {
	union {
		struct _pfr_kentry	_ke;
	} u;
};
#define pfrke_node	u._ke._pfrke_node
#define pfrke_sa	u._ke._pfrke_sa
#define pfrke_workq	u._ke._pfrke_workq
#define pfrke_ioq	u._ke._pfrke_ioq
#define pfrke_counters	u._ke._pfrke_counters
#define pfrke_tzero	u._ke._pfrke_tzero
#define pfrke_af	u._ke._pfrke_af
#define pfrke_net	u._ke._pfrke_net
#define pfrke_flags	u._ke._pfrke_flags
#define pfrke_type	u._ke._pfrke_type
#define pfrke_fb	u._ke._pfrke_fb

struct pfr_kentry_route {
	union {
		struct _pfr_kentry	_ke;
	} u;

	struct pfi_kif		*kif;
	char			ifname[IFNAMSIZ];
};

struct pfr_kentry_cost {
	union {
		struct _pfr_kentry	_ke;
	} u;

	struct pfi_kif		*kif;
	char			ifname[IFNAMSIZ];
	/* Above overlaps with pfr_kentry route */

	u_int16_t		 weight;
};

struct pfr_kentry_all {
	union {
		struct _pfr_kentry		_ke;
		struct pfr_kentry_route		kr;
		struct pfr_kentry_cost		kc;
	} u;
};
#define pfrke_rkif	u.kr.kif
#define pfrke_rifname	u.kr.ifname

SLIST_HEAD(pfr_ktableworkq, pfr_ktable);
RB_HEAD(pfr_ktablehead, pfr_ktable);
struct pfr_ktable {
	struct pfr_tstats	 pfrkt_ts;
	RB_ENTRY(pfr_ktable)	 pfrkt_tree;
	SLIST_ENTRY(pfr_ktable)	 pfrkt_workq;
	struct radix_node_head	*pfrkt_ip4;
	struct radix_node_head	*pfrkt_ip6;
	struct pfr_ktable	*pfrkt_shadow;
	struct pfr_ktable	*pfrkt_root;
	struct pf_ruleset	*pfrkt_rs;
	long			 pfrkt_larg;
	int			 pfrkt_nflags;
	u_int64_t		 pfrkt_refcntcost;
	u_int16_t		 pfrkt_gcdweight;
	u_int16_t		 pfrkt_maxweight;
};
#define pfrkt_t		pfrkt_ts.pfrts_t
#define pfrkt_name	pfrkt_t.pfrt_name
#define pfrkt_anchor	pfrkt_t.pfrt_anchor
#define pfrkt_ruleset	pfrkt_t.pfrt_ruleset
#define pfrkt_flags	pfrkt_t.pfrt_flags
#define pfrkt_cnt	pfrkt_ts.pfrts_cnt
#define pfrkt_refcnt	pfrkt_ts.pfrts_refcnt
#define pfrkt_packets	pfrkt_ts.pfrts_packets
#define pfrkt_bytes	pfrkt_ts.pfrts_bytes
#define pfrkt_match	pfrkt_ts.pfrts_match
#define pfrkt_nomatch	pfrkt_ts.pfrts_nomatch
#define pfrkt_tzero	pfrkt_ts.pfrts_tzero

RB_HEAD(pfi_ifhead, pfi_kif);

/* state tables */
extern struct pf_state_tree	 pf_statetbl;

/* keep synced with pfi_kif, used in RB_FIND */
struct pfi_kif_cmp {
	char				 pfik_name[IFNAMSIZ];
};

struct ifnet;
struct ifg_group;

struct pfi_kif {
	char				 pfik_name[IFNAMSIZ];
	RB_ENTRY(pfi_kif)		 pfik_tree;
	u_int64_t			 pfik_packets[2][2][2];
	u_int64_t			 pfik_bytes[2][2][2];
	time_t				 pfik_tzero;
	int				 pfik_flags;
	int				 pfik_flags_new;
	void				*pfik_ah_cookie;
	struct ifnet			*pfik_ifp;
	struct ifg_group		*pfik_group;
	int				 pfik_states;
	int				 pfik_rules;
	int				 pfik_routes;
	int				 pfik_srcnodes;
	int				 pfik_flagrefs;
	TAILQ_HEAD(, pfi_dynaddr)	 pfik_dynaddrs;
};

enum pfi_kif_refs {
	PFI_KIF_REF_NONE,
	PFI_KIF_REF_STATE,
	PFI_KIF_REF_RULE,
	PFI_KIF_REF_ROUTE,
	PFI_KIF_REF_SRCNODE,
	PFI_KIF_REF_FLAG
};

#define PFI_IFLAG_SKIP		0x0100	/* skip filtering on interface */
#define PFI_IFLAG_ANY		0x0200	/* match any non-loopback interface */

/* flags for RDR options */
#define PF_DPORT_RANGE	0x01		/* Dest port uses range */
#define PF_RPORT_RANGE	0x02		/* RDR'ed port uses range */

/* Reasons code for passing/dropping a packet */
#define PFRES_MATCH	0		/* Explicit match of a rule */
#define PFRES_BADOFF	1		/* Bad offset for pull_hdr */
#define PFRES_FRAG	2		/* Dropping following fragment */
#define PFRES_SHORT	3		/* Dropping short packet */
#define PFRES_NORM	4		/* Dropping by normalizer */
#define PFRES_MEMORY	5		/* Dropped due to lacking mem */
#define PFRES_TS	6		/* Bad TCP Timestamp (RFC1323) */
#define PFRES_CONGEST	7		/* Congestion */
#define PFRES_IPOPTIONS 8		/* IP option */
#define PFRES_PROTCKSUM 9		/* Protocol checksum invalid */
#define PFRES_BADSTATE	10		/* State mismatch */
#define PFRES_STATEINS	11		/* State insertion failure */
#define PFRES_MAXSTATES	12		/* State limit */
#define PFRES_SRCLIMIT	13		/* Source node/conn limit */
#define PFRES_SYNPROXY	14		/* SYN proxy */
#define PFRES_TRANSLATE	15		/* No translation address available */
#define PFRES_NOROUTE	16		/* No route found for PBR action */
#define PFRES_MAX	17		/* total+1 */

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
	"translate", \
	"no-route", \
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
#define	LCNT_SYNFLOODS		7	/* synfloods detected */
#define	LCNT_SYNCOOKIES_SENT	8	/* syncookies sent */
#define	LCNT_SYNCOOKIES_VALID	9	/* syncookies validated */
#define LCNT_MAX		10	/* total+1 */

#define LCNT_NAMES { \
	"max states per rule", \
	"max-src-states", \
	"max-src-nodes", \
	"max-src-conn", \
	"max-src-conn-rate", \
	"overload table insertion", \
	"overload flush states", \
	"synfloods detected", \
	"syncookies sent", \
	"syncookies validated", \
	NULL \
}

/* UDP state enumeration */
#define PFUDPS_NO_TRAFFIC	0
#define PFUDPS_SINGLE		1
#define PFUDPS_MULTIPLE		2

#define PFUDPS_NSTATES		3	/* number of state levels */

#define PFUDPS_NAMES { \
	"NO_TRAFFIC", \
	"SINGLE", \
	"MULTIPLE", \
	NULL \
}

/* Other protocol state enumeration */
#define PFOTHERS_NO_TRAFFIC	0
#define PFOTHERS_SINGLE		1
#define PFOTHERS_MULTIPLE	2

#define PFOTHERS_NSTATES	3	/* number of state levels */

#define PFOTHERS_NAMES { \
	"NO_TRAFFIC", \
	"SINGLE", \
	"MULTIPLE", \
	NULL \
}

#define FCNT_STATE_SEARCH	0
#define FCNT_STATE_INSERT	1
#define FCNT_STATE_REMOVALS	2
#define FCNT_MAX		3

#define SCNT_SRC_NODE_SEARCH	0
#define SCNT_SRC_NODE_INSERT	1
#define SCNT_SRC_NODE_REMOVALS	2
#define SCNT_MAX		3

#define NCNT_FRAG_SEARCH	0
#define NCNT_FRAG_INSERT	1
#define NCNT_FRAG_REMOVALS	2
#define NCNT_MAX		3

#define REASON_SET(a, x) \
	do { \
		if ((void *)(a) != NULL) { \
			*(a) = (x); \
			if (x < PFRES_MAX) \
				pf_status.counters[x]++; \
		} \
	} while (0)

struct pf_status {
	u_int64_t	counters[PFRES_MAX];
	u_int64_t	lcounters[LCNT_MAX];	/* limit counters */
	u_int64_t	fcounters[FCNT_MAX];
	u_int64_t	scounters[SCNT_MAX];
	u_int64_t	ncounters[NCNT_MAX];
	u_int64_t	pcounters[2][2][3];
	u_int64_t	bcounters[2][2];
	u_int64_t	stateid;
	u_int64_t	syncookies_inflight[2];	/* unACKed SYNcookies */
	time_t		since;
	u_int32_t	running;
	u_int32_t	states;
	u_int32_t	states_halfopen;
	u_int32_t	src_nodes;
	u_int32_t	fragments;
	u_int32_t	debug;
	u_int32_t	hostid;
	u_int32_t	reass;			/* reassembly */
	u_int8_t	syncookies_active;
	u_int8_t	syncookies_mode;	/* never/always/adaptive */
	u_int8_t	pad[2];
	char		ifname[IFNAMSIZ];
	u_int8_t	pf_chksum[PF_MD5_DIGEST_LENGTH];
};

#define PF_REASS_ENABLED	0x01
#define PF_REASS_NODF		0x02

#define	PF_SYNCOOKIES_NEVER	0
#define	PF_SYNCOOKIES_ALWAYS	1
#define	PF_SYNCOOKIES_ADAPTIVE	2
#define	PF_SYNCOOKIES_MODE_MAX	PF_SYNCOOKIES_ADAPTIVE

#define	PF_SYNCOOKIES_HIWATPCT	25
#define	PF_SYNCOOKIES_LOWATPCT	PF_SYNCOOKIES_HIWATPCT/2

#define PF_PRIO_ZERO		0xff		/* match "prio 0" packets */

struct pf_queue_bwspec {
	uint64_t	absolute;
	u_int		percent;
};

struct pf_queue_scspec {
	struct pf_queue_bwspec	m1;
	struct pf_queue_bwspec	m2;
	u_int			d;
};

struct pf_queue_fqspec {
	u_int		flows;
	u_int		quantum;
	u_int		target;
	u_int		interval;
};

struct pf_queuespec {
	TAILQ_ENTRY(pf_queuespec)	 entries;
	char				 qname[PF_QNAME_SIZE];
	char				 parent[PF_QNAME_SIZE];
	char				 ifname[IFNAMSIZ];
	struct pf_queue_scspec		 realtime;
	struct pf_queue_scspec		 linkshare;
	struct pf_queue_scspec		 upperlimit;
	struct pf_queue_fqspec		 flowqueue;
	struct pfi_kif			*kif;
	u_int				 flags;
	u_int				 qlimit;
	u_int32_t			 qid;
	u_int32_t			 parent_qid;
};

#define PFQS_FLOWQUEUE			0x0001
#define PFQS_ROOTCLASS			0x0002
#define PFQS_DEFAULT			0x1000 /* maps to HFSC_DEFAULTCLASS */

struct priq_opts {
	int		flags;
};

struct hfsc_opts {
	/* real-time service curve */
	u_int		rtsc_m1;	/* slope of the 1st segment in bps */
	u_int		rtsc_d;		/* the x-projection of m1 in msec */
	u_int		rtsc_m2;	/* slope of the 2nd segment in bps */
	/* link-sharing service curve */
	u_int		lssc_m1;
	u_int		lssc_d;
	u_int		lssc_m2;
	/* upper-limit service curve */
	u_int		ulsc_m1;
	u_int		ulsc_d;
	u_int		ulsc_m2;
	int		flags;
};

struct pfq_ops {
	void *		(*pfq_alloc)(struct ifnet *);
	int		(*pfq_addqueue)(void *, struct pf_queuespec *);
	void		(*pfq_free)(void *);
	int		(*pfq_qstats)(struct pf_queuespec *, void *, int *);
	/* Queue manager ops */
	unsigned int	(*pfq_qlength)(void *);
	struct mbuf *	(*pfq_enqueue)(void *, struct mbuf *);
	struct mbuf *	(*pfq_deq_begin)(void *, void **, struct mbuf_list *);
	void		(*pfq_deq_commit)(void *, struct mbuf *, void *);
	void		(*pfq_purge)(void *, struct mbuf_list *);
};

struct pf_tagname {
	TAILQ_ENTRY(pf_tagname)	entries;
	char			name[PF_TAG_NAME_SIZE];
	u_int16_t		tag;
	int			ref;
};

struct pf_divert {
	struct pf_addr	addr;
	u_int16_t	port;
	u_int16_t	rdomain;
	u_int8_t	type;
};

enum pf_divert_types {
	PF_DIVERT_NONE,
	PF_DIVERT_TO,
	PF_DIVERT_REPLY,
	PF_DIVERT_PACKET
};

struct pf_pktdelay {
	struct timeout	 to;
	struct mbuf	*m;
	u_int		 ifidx;
};

/* Fragment entries reference mbuf clusters, so base the default on that. */
#define PFFRAG_FRENT_HIWAT	(NMBCLUSTERS / 16) /* Number of entries */
#define PFFRAG_FRAG_HIWAT	(NMBCLUSTERS / 32) /* Number of packets */

#define PFR_KTABLE_HIWAT	1000	/* Number of tables */
#define PFR_KENTRY_HIWAT	200000	/* Number of table entries */
#define PFR_KENTRY_HIWAT_SMALL	100000	/* Number of entries for tiny hosts */

/*
 * ioctl parameter structures
 */

struct pfioc_rule {
	u_int32_t	 action;
	u_int32_t	 ticket;
	u_int32_t	 nr;
	char		 anchor[PATH_MAX];
	char		 anchor_call[PATH_MAX];
	struct pf_rule	 rule;
};

struct pfioc_natlook {
	struct pf_addr	 saddr;
	struct pf_addr	 daddr;
	struct pf_addr	 rsaddr;
	struct pf_addr	 rdaddr;
	u_int16_t	 rdomain;
	u_int16_t	 rrdomain;
	u_int16_t	 sport;
	u_int16_t	 dport;
	u_int16_t	 rsport;
	u_int16_t	 rdport;
	sa_family_t	 af;
	u_int8_t	 proto;
	u_int8_t	 direction;
};

struct pfioc_state {
	struct pfsync_state	state;
};

struct pfioc_src_node_kill {
	sa_family_t psnk_af;
	struct pf_rule_addr psnk_src;
	struct pf_rule_addr psnk_dst;
	u_int		    psnk_killed;
};

struct pfioc_state_kill {
	struct pf_state_cmp	psk_pfcmp;
	sa_family_t		psk_af;
	int			psk_proto;
	struct pf_rule_addr	psk_src;
	struct pf_rule_addr	psk_dst;
	char			psk_ifname[IFNAMSIZ];
	char			psk_label[PF_RULE_LABEL_SIZE];
	u_int			psk_killed;
	u_int16_t		psk_rdomain;
};

struct pfioc_states {
	size_t	ps_len;
	union {
		caddr_t			 psu_buf;
		struct pfsync_state	*psu_states;
	} ps_u;
#define ps_buf		ps_u.psu_buf
#define ps_states	ps_u.psu_states
};

struct pfioc_src_nodes {
	size_t	psn_len;
	union {
		caddr_t		 psu_buf;
		struct pf_src_node	*psu_src_nodes;
	} psn_u;
#define psn_buf		psn_u.psu_buf
#define psn_src_nodes	psn_u.psu_src_nodes
};

struct pfioc_tm {
	int		 timeout;
	int		 seconds;
};

struct pfioc_limit {
	int		 index;
	unsigned	 limit;
};

struct pfioc_ruleset {
	u_int32_t	 nr;
	char		 path[PATH_MAX];
	char		 name[PF_ANCHOR_NAME_SIZE];
};

struct pfioc_trans {
	int		 size;	/* number of elements */
	int		 esize; /* size of each element in bytes */
	struct pfioc_trans_e {
		int		type;
		char		anchor[PATH_MAX];
		u_int32_t	ticket;
	}		*array;
};

struct pfioc_queue {
	u_int32_t		ticket;
	u_int			nr;
	struct pf_queuespec	queue;
};

struct pfioc_qstats {
	u_int32_t		 ticket;
	u_int32_t		 nr;
	struct pf_queuespec	 queue;
	void			*buf;
	int			 nbytes;
};

#define PFR_FLAG_DUMMY		0x00000002
#define PFR_FLAG_FEEDBACK	0x00000004
#define PFR_FLAG_CLSTATS	0x00000008
#define PFR_FLAG_ADDRSTOO	0x00000010
#define PFR_FLAG_REPLACE	0x00000020
#define PFR_FLAG_ALLRSETS	0x00000040
#define PFR_FLAG_ALLMASK	0x0000007F
#ifdef _KERNEL
#define PFR_FLAG_USERIOCTL	0x10000000
#endif

struct pfioc_table {
	struct pfr_table	 pfrio_table;
	void			*pfrio_buffer;
	int			 pfrio_esize;
	int			 pfrio_size;
	int			 pfrio_size2;
	int			 pfrio_nadd;
	int			 pfrio_ndel;
	int			 pfrio_nchange;
	int			 pfrio_flags;
	u_int32_t		 pfrio_ticket;
};
#define	pfrio_exists	pfrio_nadd
#define	pfrio_nzero	pfrio_nadd
#define	pfrio_nmatch	pfrio_nadd
#define pfrio_naddr	pfrio_size2
#define pfrio_setflag	pfrio_size2
#define pfrio_clrflag	pfrio_nadd

struct pfioc_iface {
	char	 pfiio_name[IFNAMSIZ];
	void	*pfiio_buffer;
	int	 pfiio_esize;
	int	 pfiio_size;
	int	 pfiio_nzero;
	int	 pfiio_flags;
};

struct pfioc_synflwats {
	u_int32_t	hiwat;
	u_int32_t	lowat;
};

/*
 * ioctl operations
 */

#define DIOCSTART	_IO  ('D',  1)
#define DIOCSTOP	_IO  ('D',  2)
#define DIOCADDRULE	_IOWR('D',  4, struct pfioc_rule)
#define DIOCGETRULES	_IOWR('D',  6, struct pfioc_rule)
#define DIOCGETRULE	_IOWR('D',  7, struct pfioc_rule)
/* cut 8 - 17 */
#define DIOCCLRSTATES	_IOWR('D', 18, struct pfioc_state_kill)
#define DIOCGETSTATE	_IOWR('D', 19, struct pfioc_state)
#define DIOCSETSTATUSIF _IOWR('D', 20, struct pfioc_iface)
#define DIOCGETSTATUS	_IOWR('D', 21, struct pf_status)
#define DIOCCLRSTATUS	_IOWR('D', 22, struct pfioc_iface)
#define DIOCNATLOOK	_IOWR('D', 23, struct pfioc_natlook)
#define DIOCSETDEBUG	_IOWR('D', 24, u_int32_t)
#define DIOCGETSTATES	_IOWR('D', 25, struct pfioc_states)
#define DIOCCHANGERULE	_IOWR('D', 26, struct pfioc_rule)
/* cut 27 - 28 */
#define DIOCSETTIMEOUT	_IOWR('D', 29, struct pfioc_tm)
#define DIOCGETTIMEOUT	_IOWR('D', 30, struct pfioc_tm)
#define DIOCADDSTATE	_IOWR('D', 37, struct pfioc_state)
/* cut 38 */
#define DIOCGETLIMIT	_IOWR('D', 39, struct pfioc_limit)
#define DIOCSETLIMIT	_IOWR('D', 40, struct pfioc_limit)
#define DIOCKILLSTATES	_IOWR('D', 41, struct pfioc_state_kill)
/* cut 42 - 57 */
#define	DIOCGETRULESETS	_IOWR('D', 58, struct pfioc_ruleset)
#define	DIOCGETRULESET	_IOWR('D', 59, struct pfioc_ruleset)
#define	DIOCRCLRTABLES	_IOWR('D', 60, struct pfioc_table)
#define	DIOCRADDTABLES	_IOWR('D', 61, struct pfioc_table)
#define	DIOCRDELTABLES	_IOWR('D', 62, struct pfioc_table)
#define	DIOCRGETTABLES	_IOWR('D', 63, struct pfioc_table)
#define	DIOCRGETTSTATS	_IOWR('D', 64, struct pfioc_table)
#define DIOCRCLRTSTATS  _IOWR('D', 65, struct pfioc_table)
#define	DIOCRCLRADDRS	_IOWR('D', 66, struct pfioc_table)
#define	DIOCRADDADDRS	_IOWR('D', 67, struct pfioc_table)
#define	DIOCRDELADDRS	_IOWR('D', 68, struct pfioc_table)
#define	DIOCRSETADDRS	_IOWR('D', 69, struct pfioc_table)
#define	DIOCRGETADDRS	_IOWR('D', 70, struct pfioc_table)
#define	DIOCRGETASTATS	_IOWR('D', 71, struct pfioc_table)
#define DIOCRCLRASTATS  _IOWR('D', 72, struct pfioc_table)
#define	DIOCRTSTADDRS	_IOWR('D', 73, struct pfioc_table)
#define	DIOCRSETTFLAGS	_IOWR('D', 74, struct pfioc_table)
#define DIOCRINADEFINE	_IOWR('D', 77, struct pfioc_table)
#define DIOCOSFPFLUSH	_IO('D', 78)
#define DIOCOSFPADD	_IOWR('D', 79, struct pf_osfp_ioctl)
#define DIOCOSFPGET	_IOWR('D', 80, struct pf_osfp_ioctl)
#define DIOCXBEGIN      _IOWR('D', 81, struct pfioc_trans)
#define DIOCXCOMMIT     _IOWR('D', 82, struct pfioc_trans)
#define DIOCXROLLBACK   _IOWR('D', 83, struct pfioc_trans)
#define DIOCGETSRCNODES	_IOWR('D', 84, struct pfioc_src_nodes)
#define DIOCCLRSRCNODES	_IO('D', 85)
#define DIOCSETHOSTID	_IOWR('D', 86, u_int32_t)
#define DIOCIGETIFACES	_IOWR('D', 87, struct pfioc_iface)
#define DIOCSETIFFLAG	_IOWR('D', 89, struct pfioc_iface)
#define DIOCCLRIFFLAG	_IOWR('D', 90, struct pfioc_iface)
#define DIOCKILLSRCNODES	_IOWR('D', 91, struct pfioc_src_node_kill)
#define DIOCSETREASS	_IOWR('D', 92, u_int32_t)
#define DIOCADDQUEUE	_IOWR('D', 93, struct pfioc_queue)
#define DIOCGETQUEUES	_IOWR('D', 94, struct pfioc_queue)
#define DIOCGETQUEUE	_IOWR('D', 95, struct pfioc_queue)
#define DIOCGETQSTATS	_IOWR('D', 96, struct pfioc_qstats)
#define DIOCSETSYNFLWATS	_IOWR('D', 97, struct pfioc_synflwats)
#define DIOCSETSYNCOOKIES	_IOWR('D', 98, u_int8_t)
#define DIOCGETSYNFLWATS	_IOWR('D', 99, struct pfioc_synflwats)
#define DIOCXEND	_IOWR('D', 100, u_int32_t)

#ifdef _KERNEL

struct pf_pdesc;

RB_HEAD(pf_src_tree, pf_src_node);
RB_PROTOTYPE(pf_src_tree, pf_src_node, entry, pf_src_compare);
extern struct pf_src_tree tree_src_tracking;

extern struct pf_state_list pf_state_list;

TAILQ_HEAD(pf_queuehead, pf_queuespec);
extern struct pf_queuehead		  pf_queues[2];
extern struct pf_queuehead		 *pf_queues_active, *pf_queues_inactive;

extern struct pool		 pf_src_tree_pl, pf_sn_item_pl, pf_rule_pl;
extern struct pool		 pf_state_pl, pf_state_key_pl, pf_state_item_pl,
				    pf_rule_item_pl, pf_queue_pl,
				    pf_pktdelay_pl, pf_anchor_pl;
extern struct pool		 pf_state_scrub_pl;
extern struct pf_rule		 pf_default_rule;

extern int			 pf_tbladdr_setup(struct pf_ruleset *,
				    struct pf_addr_wrap *, int);
extern void			 pf_tbladdr_remove(struct pf_addr_wrap *);
extern void			 pf_tbladdr_copyout(struct pf_addr_wrap *);
extern void			 pf_calc_skip_steps(struct pf_rulequeue *);
extern void			 pf_purge_expired_src_nodes(void);
extern void			 pf_remove_state(struct pf_state *);
extern void			 pf_remove_divert_state(struct inpcb *);
extern void			 pf_free_state(struct pf_state *);
int				 pf_insert_src_node(struct pf_src_node **,
				    struct pf_rule *, enum pf_sn_types,
				    sa_family_t, struct pf_addr *,
				    struct pf_addr *, struct pfi_kif *);
void				 pf_remove_src_node(struct pf_src_node *);
struct pf_src_node		*pf_get_src_node(struct pf_state *,
				    enum pf_sn_types);
void				 pf_src_tree_remove_state(struct pf_state *);
void				 pf_state_rm_src_node(struct pf_state *,
				    struct pf_src_node *);

extern struct pf_state		*pf_find_state_byid(struct pf_state_cmp *);
extern struct pf_state		*pf_find_state_all(struct pf_state_key_cmp *,
				    u_int, int *);
extern void			 pf_state_export(struct pfsync_state *,
				    struct pf_state *);
int				 pf_state_import(const struct pfsync_state *,
				     int);
int				 pf_state_alloc_scrub_memory(
				     const struct pfsync_state_peer *,
				     struct pf_state_peer *);
extern void			 pf_print_state(struct pf_state *);
extern void			 pf_print_flags(u_int8_t);
extern void			 pf_addrcpy(struct pf_addr *, struct pf_addr *,
				    sa_family_t);
extern void			 pf_cksum_fixup(u_int16_t *, u_int16_t,
				    u_int16_t, u_int8_t);
void				 pf_rm_rule(struct pf_rulequeue *,
				    struct pf_rule *);
struct pf_divert		*pf_find_divert(struct mbuf *);
int				 pf_setup_pdesc(struct pf_pdesc *, sa_family_t,
				    int, struct pfi_kif *, struct mbuf *,
				    u_short *);

int	pf_test(sa_family_t, int, struct ifnet *, struct mbuf **);

void	pf_poolmask(struct pf_addr *, struct pf_addr*,
	    struct pf_addr *, struct pf_addr *, sa_family_t);
void	pf_addr_inc(struct pf_addr *, sa_family_t);

void   *pf_pull_hdr(struct mbuf *, int, void *, int, u_short *, sa_family_t);
#define PF_HI (true)
#define PF_LO (!PF_HI)
#define PF_ALGNMNT(off) (((off) % 2) == 0 ? PF_HI : PF_LO)
int	pf_patch_8(struct pf_pdesc *, u_int8_t *, u_int8_t, bool);
int	pf_patch_16(struct pf_pdesc *, u_int16_t *, u_int16_t);
int	pf_patch_16_unaligned(struct pf_pdesc *, void *, u_int16_t, bool);
int	pf_patch_32(struct pf_pdesc *, u_int32_t *, u_int32_t);
int	pf_patch_32_unaligned(struct pf_pdesc *, void *, u_int32_t, bool);
int	pflog_packet(struct pf_pdesc *, u_int8_t, struct pf_rule *,
	    struct pf_rule *, struct pf_ruleset *, struct pf_rule *);
int	pf_match_addr(u_int8_t, struct pf_addr *, struct pf_addr *,
	    struct pf_addr *, sa_family_t);
int	pf_match_addr_range(struct pf_addr *, struct pf_addr *,
	    struct pf_addr *, sa_family_t);
int	pf_match(u_int8_t, u_int32_t, u_int32_t, u_int32_t);
int	pf_match_port(u_int8_t, u_int16_t, u_int16_t, u_int16_t);
int	pf_match_uid(u_int8_t, uid_t, uid_t, uid_t);
int	pf_match_gid(u_int8_t, gid_t, gid_t, gid_t);

struct pf_state_scrub *
	pf_state_scrub_get(void);
void	pf_state_scrub_put(struct pf_state_scrub *);

int	pf_refragment6(struct mbuf **, struct m_tag *mtag,
	    struct sockaddr_in6 *, struct ifnet *, struct rtentry *);
void	pf_normalize_init(void);
int	pf_normalize_ip(struct pf_pdesc *, u_short *);
int	pf_normalize_ip6(struct pf_pdesc *, u_short *);
int	pf_normalize_tcp(struct pf_pdesc *);
void	pf_normalize_tcp_cleanup(struct pf_state *);
int	pf_normalize_tcp_init(struct pf_pdesc *, struct pf_state_peer *);
int	pf_normalize_tcp_alloc(struct pf_state_peer *);
int	pf_normalize_tcp_stateful(struct pf_pdesc *, u_short *,
	    struct pf_state *, struct pf_state_peer *, struct pf_state_peer *,
	    int *);
int	pf_normalize_mss(struct pf_pdesc *, u_int16_t);
void	pf_scrub(struct mbuf *, u_int16_t, sa_family_t, u_int8_t, u_int8_t);
void	pf_purge_expired_fragments(void);
int	pf_routable(struct pf_addr *addr, sa_family_t af, struct pfi_kif *,
	    int);
int	pf_rtlabel_match(struct pf_addr *, sa_family_t, struct pf_addr_wrap *,
	    int);
int	pf_socket_lookup(struct pf_pdesc *);
struct pf_state_key *pf_alloc_state_key(int);
int	pf_ouraddr(struct mbuf *);
void	pf_pkt_addr_changed(struct mbuf *);
struct inpcb *pf_inp_lookup(struct mbuf *);
void	pf_inp_link(struct mbuf *, struct inpcb *);
void	pf_inp_unlink(struct inpcb *);
int	pf_translate(struct pf_pdesc *, struct pf_addr *, u_int16_t,
	    struct pf_addr *, u_int16_t, u_int16_t, int);
int	pf_translate_af(struct pf_pdesc *);
void	pf_route(struct pf_pdesc *, struct pf_state *);
void	pf_route6(struct pf_pdesc *, struct pf_state *);
void	pf_init_threshold(struct pf_threshold *, u_int32_t, u_int32_t);
int	pf_delay_pkt(struct mbuf *, u_int);

void	pfr_initialize(void);
int	pfr_match_addr(struct pfr_ktable *, struct pf_addr *, sa_family_t);
void	pfr_update_stats(struct pfr_ktable *, struct pf_addr *,
	    struct pf_pdesc *, int, int);
int	pfr_pool_get(struct pf_pool *, struct pf_addr **,
	    struct pf_addr **, sa_family_t);
int	pfr_states_increase(struct pfr_ktable *, struct pf_addr *, int);
int	pfr_states_decrease(struct pfr_ktable *, struct pf_addr *, int);
struct pfr_kentry *
	pfr_kentry_byaddr(struct pfr_ktable *, struct pf_addr *, sa_family_t,
	    int);
void	pfr_dynaddr_update(struct pfr_ktable *, struct pfi_dynaddr *);
struct pfr_ktable *
	pfr_attach_table(struct pf_ruleset *, char *, int);
void	pfr_detach_table(struct pfr_ktable *);
int	pfr_clr_tables(struct pfr_table *, int *, int);
int	pfr_add_tables(struct pfr_table *, int, int *, int);
int	pfr_del_tables(struct pfr_table *, int, int *, int);
int	pfr_get_tables(struct pfr_table *, struct pfr_table *, int *, int);
int	pfr_get_tstats(struct pfr_table *, struct pfr_tstats *, int *, int);
int	pfr_clr_tstats(struct pfr_table *, int, int *, int);
int	pfr_set_tflags(struct pfr_table *, int, int, int, int *, int *, int);
int	pfr_clr_addrs(struct pfr_table *, int *, int);
int	pfr_insert_kentry(struct pfr_ktable *, struct pfr_addr *, time_t);
int	pfr_add_addrs(struct pfr_table *, struct pfr_addr *, int, int *,
	    int);
int	pfr_del_addrs(struct pfr_table *, struct pfr_addr *, int, int *,
	    int);
int	pfr_set_addrs(struct pfr_table *, struct pfr_addr *, int, int *,
	    int *, int *, int *, int, u_int32_t);
int	pfr_get_addrs(struct pfr_table *, struct pfr_addr *, int *, int);
int	pfr_get_astats(struct pfr_table *, struct pfr_astats *, int *, int);
int	pfr_clr_astats(struct pfr_table *, struct pfr_addr *, int, int *,
	    int);
int	pfr_tst_addrs(struct pfr_table *, struct pfr_addr *, int, int *,
	    int);
int	pfr_ina_begin(struct pfr_table *, u_int32_t *, int *, int);
int	pfr_ina_rollback(struct pfr_table *, u_int32_t, int *, int);
int	pfr_ina_commit(struct pfr_table *, u_int32_t, int *, int *, int);
int	pfr_ina_define(struct pfr_table *, struct pfr_addr *, int, int *,
	    int *, u_int32_t, int);
struct pfr_ktable
	*pfr_ktable_select_active(struct pfr_ktable *);

extern struct pfi_kif		*pfi_all;

void		 pfi_initialize(void);
struct pfi_kif	*pfi_kif_alloc(const char *, int);
void		 pfi_kif_free(struct pfi_kif *);
struct pfi_kif	*pfi_kif_find(const char *);
struct pfi_kif	*pfi_kif_get(const char *, struct pfi_kif **);
void		 pfi_kif_ref(struct pfi_kif *, enum pfi_kif_refs);
void		 pfi_kif_unref(struct pfi_kif *, enum pfi_kif_refs);
int		 pfi_kif_match(struct pfi_kif *, struct pfi_kif *);
void		 pfi_attach_ifnet(struct ifnet *);
void		 pfi_detach_ifnet(struct ifnet *);
void		 pfi_attach_ifgroup(struct ifg_group *);
void		 pfi_detach_ifgroup(struct ifg_group *);
void		 pfi_group_addmember(const char *);
void		 pfi_group_delmember(const char *);
int		 pfi_match_addr(struct pfi_dynaddr *, struct pf_addr *,
		    sa_family_t);
int		 pfi_dynaddr_setup(struct pf_addr_wrap *, sa_family_t, int);
void		 pfi_dynaddr_remove(struct pf_addr_wrap *);
void		 pfi_dynaddr_copyout(struct pf_addr_wrap *);
void		 pfi_update_status(const char *, struct pf_status *);
void		 pfi_get_ifaces(const char *, struct pfi_kif *, int *);
int		 pfi_set_flags(const char *, int);
int		 pfi_clear_flags(const char *, int);
void		 pfi_xcommit(void);

int		 pf_match_tag(struct mbuf *, struct pf_rule *, int *);
u_int16_t	 pf_tagname2tag(char *, int);
void		 pf_tag2tagname(u_int16_t, char *);
void		 pf_tag_ref(u_int16_t);
void		 pf_tag_unref(u_int16_t);
void		 pf_tag_packet(struct mbuf *, int, int);
int		 pf_addr_compare(const struct pf_addr *,
		     const struct pf_addr *, sa_family_t);

const struct pfq_ops
		*pf_queue_manager(struct pf_queuespec *);

extern struct pf_status	pf_status;
extern struct pool	pf_frent_pl, pf_frag_pl;

/*
 * Protection/ownership of pf_pool_limit:
 *	I	immutable after pfattach()
 *	p	pf_lock
 */

struct pf_pool_limit {
	void		*pp;		/* [I] */
	unsigned	 limit;		/* [p] */
	unsigned	 limit_new;	/* [p] */
};
extern struct pf_pool_limit	pf_pool_limits[PF_LIMIT_MAX];

#endif /* _KERNEL */

extern struct pf_anchor_global	pf_anchors;
extern struct pf_anchor		pf_main_anchor;
#define pf_main_ruleset		pf_main_anchor.ruleset

struct tcphdr;

/* these ruleset functions can be linked into userland programs (pfctl) */
void			 pf_init_ruleset(struct pf_ruleset *);
int			 pf_anchor_setup(struct pf_rule *,
			    const struct pf_ruleset *, const char *);
int			 pf_anchor_copyout(const struct pf_ruleset *,
			    const struct pf_rule *, struct pfioc_rule *);
void			 pf_remove_anchor(struct pf_rule *);
void			 pf_remove_if_empty_ruleset(struct pf_ruleset *);
struct pf_anchor	*pf_find_anchor(const char *);
struct pf_ruleset	*pf_find_ruleset(const char *);
struct pf_ruleset 	*pf_get_leaf_ruleset(char *, char **);
struct pf_anchor 	*pf_create_anchor(struct pf_anchor *, const char *);
struct pf_ruleset	*pf_find_or_create_ruleset(const char *);
void			 pf_anchor_rele(struct pf_anchor *);
struct pf_anchor	*pf_anchor_take(struct pf_anchor *);

/* The fingerprint functions can be linked into userland programs (tcpdump) */
int	pf_osfp_add(struct pf_osfp_ioctl *);
#ifdef _KERNEL
struct pf_osfp_enlist *
	pf_osfp_fingerprint(struct pf_pdesc *);
#endif /* _KERNEL */
struct pf_osfp_enlist *
	pf_osfp_fingerprint_hdr(const struct ip *, const struct ip6_hdr *,
	    const struct tcphdr *);
void	pf_osfp_flush(void);
int	pf_osfp_get(struct pf_osfp_ioctl *);
void	pf_osfp_initialize(void);
int	pf_osfp_match(struct pf_osfp_enlist *, pf_osfp_t);
struct pf_os_fingerprint *
	pf_osfp_validate(void);

#ifdef _KERNEL
void			 pf_print_host(struct pf_addr *, u_int16_t,
			    sa_family_t);
int			 pf_get_transaddr(struct pf_rule *, struct pf_pdesc *,
			    struct pf_src_node **, struct pf_rule **);
int			 pf_map_addr(sa_family_t, struct pf_rule *,
			    struct pf_addr *, struct pf_addr *,
			    struct pf_addr *, struct pf_src_node **,
			    struct pf_pool *, enum pf_sn_types);
int			 pf_postprocess_addr(struct pf_state *);

void			 pf_mbuf_link_state_key(struct mbuf *,
			    struct pf_state_key *);
void			 pf_mbuf_unlink_state_key(struct mbuf *);
void			 pf_mbuf_link_inpcb(struct mbuf *, struct inpcb *);
void			 pf_mbuf_unlink_inpcb(struct mbuf *);

u_int8_t*		 pf_find_tcpopt(u_int8_t *, u_int8_t *, size_t,
			    u_int8_t, u_int8_t);
u_int8_t		 pf_get_wscale(struct pf_pdesc *);
u_int16_t		 pf_get_mss(struct pf_pdesc *, uint16_t);
struct mbuf *		 pf_build_tcp(const struct pf_rule *, sa_family_t,
			    const struct pf_addr *, const struct pf_addr *,
			    u_int16_t, u_int16_t, u_int32_t, u_int32_t,
			    u_int8_t, u_int16_t, u_int16_t, u_int8_t, int,
			    u_int16_t, u_int, u_int, u_short *);
void			 pf_send_tcp(const struct pf_rule *, sa_family_t,
			    const struct pf_addr *, const struct pf_addr *,
			    u_int16_t, u_int16_t, u_int32_t, u_int32_t,
			    u_int8_t, u_int16_t, u_int16_t, u_int8_t, int,
			    u_int16_t, u_int, u_short *);
void			 pf_syncookies_init(void);
int			 pf_syncookies_setmode(u_int8_t);
int			 pf_syncookies_setwats(u_int32_t, u_int32_t);
int			 pf_syncookies_getwats(struct pfioc_synflwats *);
int			 pf_synflood_check(struct pf_pdesc *);
void			 pf_syncookie_send(struct pf_pdesc *, u_short *);
u_int8_t		 pf_syncookie_validate(struct pf_pdesc *);
struct mbuf *		 pf_syncookie_recreate_syn(struct pf_pdesc *,
			    u_short *);
#endif /* _KERNEL */

#endif /* _NET_PFVAR_H_ */
