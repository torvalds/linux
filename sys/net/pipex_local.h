/*	$OpenBSD: pipex_local.h,v 1.54 2025/03/02 21:28:32 bluhm Exp $	*/

/*
 * Copyright (c) 2009 Internet Initiative Japan Inc.
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
 */

#include <sys/mutex.h>
#include <sys/refcnt.h>

extern struct mutex pipex_list_mtx;

#define	PIPEX_PPTP	1
#define	PIPEX_L2TP	1
#define	PIPEX_PPPOE	1
#define	PIPEX_MPPE	1

#define PIPEX_REWIND_LIMIT		64

#define PIPEX_ENABLED			0x0001

/* compile time option constants */
#ifndef	PIPEX_MAX_SESSION
#define PIPEX_MAX_SESSION		512
#endif
#define PIPEX_HASH_DIV			8
#define PIPEX_HASH_SIZE			(PIPEX_MAX_SESSION/PIPEX_HASH_DIV)
#define PIPEX_HASH_MASK			(PIPEX_HASH_SIZE-1)
#define PIPEX_CLOSE_TIMEOUT		30
#define	PIPEX_PPPMINLEN			5
	/* minimum PPP header length is 1 and minimum ppp payload length is 4 */

#define PIPEX_MPPE_NOLDKEY		64 /* should be power of two */
#define PIPEX_MPPE_OLDKEYMASK		(PIPEX_MPPE_NOLDKEY - 1)

/*
 * Locks used to protect struct members:
 *      A       atomic operation
 *      I       immutable after creation
 *      L       pipex_list_mtx
 *      s       this pipex_session' `pxs_mtx'
 *      m       this pipex_mppe' `pxm_mtx'
 */

#ifdef PIPEX_MPPE
/* mppe rc4 key */
struct pipex_mppe {
	struct mutex pxm_mtx;
	u_int flags;				/* [m] flags, see below */
#define PIPEX_MPPE_STATELESS	0x01		/* [I] key change mode */
#define PIPEX_MPPE_RESETREQ	0x02		/* [m] */

	int16_t	keylenbits;			/* [I] key length */
	int16_t keylen;				/* [I] */
	uint16_t coher_cnt;			/* [m] coherency counter */
	struct  rc4_ctx rc4ctx;			/* [m] */
	u_char master_key[PIPEX_MPPE_KEYLEN];	/* [m] master key of MPPE */
	u_char session_key[PIPEX_MPPE_KEYLEN];	/* [m] session key of MPPE */
	u_char (*old_session_keys)[PIPEX_MPPE_KEYLEN];
						/* [m] old session keys */
};
#endif /* PIPEX_MPPE */

#ifdef PIPEX_PPPOE
struct pipex_pppoe_session {
	u_int	 over_ifidx;                    /* [I] ether interface */
};
#endif /* PIPEX_PPPOE */

#ifdef PIPEX_PPTP
struct pipex_pptp_session {
	/* sequence number gap between pipex and userland */
	int32_t	snd_gap;		/* [s] gap of our sequence */
	int32_t rcv_gap;		/* [s] gap of peer's sequence */
	int32_t ul_snd_una;		/* [s] userland send acked seq */

	uint32_t snd_nxt;		/* [s] send next */
	uint32_t rcv_nxt;		/* [s] receive next */
	uint32_t snd_una;		/* [s] send acked sequence */
	uint32_t rcv_acked;		/* [s] recv acked sequence */

	int winsz;			/* [I] windows size */
	int maxwinsz;			/* [I] max windows size */
	int peer_maxwinsz;		/* [I] peer's max windows size */
};
#endif /* PIPEX_PPTP */

#ifdef PIPEX_L2TP
/*
 * L2TP Packet headers
 *
 *   +----+---+----+---+----+--------+
 *   |IPv4|UDP|L2TP|PPP|IPv4|Data....|
 *   +----+---+----+---+----+--------+
 *
 * Session Data
 *
 *   IPv4    IP_SRC         <-- required for encap.
 *           IP_DST         <-- required for encap.
 *
 *   UDP     SPort          <-- required for encap.
 *           DPort          <-- required for encap.
 *
 *   L2TP    FLAGS          <-- only handle TYPE=0 (data)
 *           Tunnel ID      <-- ID per tunnel(NOT a key: differed from RFC)
 *           Session ID     <-- ID per PPP session(KEY to look up session)
 *           Ns(SEND SEQ)   <-- sequence number of packet to send(opt.)
 *           Nr(RECV SEQ)   <-- sequence number of packet to recv(opt.)
 *
 * - Recv Session lookup key is (Tunnnel ID, Session ID) in RFC.
 *   - BUT (Session ID) in PIPEX. SESSION ID MUST BE UNIQ.
 *
 * - We must update (Ns, Nr) of data channel. and we must adjust (Ns, Nr)
 *   in packets from/to userland.
 */
struct pipex_l2tp_session {
	/* KEYS for session lookup (host byte order) */
	uint16_t tunnel_id;		/* [I] our tunnel-id */
	uint16_t peer_tunnel_id;	/* [I] peer's tunnel-id */

	uint32_t option_flags;		/* [I] protocol options */
	uint32_t ipsecflowinfo;		/* [I] IPsec SA flow id for NAT-T */

	int16_t ns_gap;		/* [s] gap between userland and pipex */
	int16_t nr_gap;		/* [s] gap between userland and pipex */
	uint16_t ul_ns_una;	/* [s] unacked sequence number (userland) */

	uint16_t ns_nxt;	/* [s] next sequence number to send */
	uint16_t ns_una;	/* [s] unacked sequence number to send */

	uint16_t nr_nxt;	/* [s] next sequence number to recv */
	uint16_t nr_acked;	/* [s] acked sequence number to recv */
};
#endif /* PIPEX_L2TP */

struct cpumem;

/* special iterator session */
struct pipex_session_iterator {
	/* Fields below should be in sync with pipex_session structure */
	struct radix_node	ps4_rn[2];
	u_int		flags;			/* [I] flags, see below */
	LIST_ENTRY(pipex_session) session_list;	/* [L] all session chain */
};

/* pppac ip-extension session table */
struct pipex_session {
	struct radix_node	ps4_rn[2];
					/* [L] tree glue, and other values */
	u_int		flags;		/* [I] flags, see below */
#define PIPEX_SFLAGS_MULTICAST		0x01 /* virtual entry for multicast */
#define PIPEX_SFLAGS_PPPX		0x02 /* interface is
						point2point(pppx) */
#define PIPEX_SFLAGS_ITERATOR		0x04 /* iterator session */

	LIST_ENTRY(pipex_session) session_list;	/* [L] all session chain */
	LIST_ENTRY(pipex_session) state_list;	/* [L] state list chain */
	LIST_ENTRY(pipex_session) id_chain;	/* [L] id hash chain */
	LIST_ENTRY(pipex_session) peer_addr_chain;
					/* [L] peer's address hash chain */
	struct refcnt pxs_refcnt;
	struct mutex pxs_mtx;

	u_int		state;		/* [L] pipex session state */
#define PIPEX_STATE_INITIAL		0x0000
#define PIPEX_STATE_OPENED		0x0001
#define PIPEX_STATE_CLOSE_WAIT		0x0002
#define PIPEX_STATE_CLOSE_WAIT2		0x0003
#define PIPEX_STATE_CLOSED		0x0004

	uint32_t	idle_time;	/* [L] idle time in seconds */

	uint16_t	protocol;		/* [I] tunnel protocol (PK) */
	uint16_t	session_id;		/* [I] session-id (PK) */
	uint16_t	peer_session_id;	/* [I] peer's session-id */
	uint16_t	peer_mru;		/* [I] peer's MRU */
	uint32_t	timeout_sec;		/* [I] idle timeout */
	int		ppp_id;			/* [I] PPP id */

	struct sockaddr_in ip_address;   /* [I] remote address (AK) */
	struct sockaddr_in ip_netmask;   /* [I] remote address mask (AK) */
	struct sockaddr_in6 ip6_address; /* [I] remote IPv6 address */
	int		ip6_prefixlen;   /* [I] remote IPv6 prefixlen */

	u_int		ifindex;		/* [A] interface index */
	void		*ownersc;		/* [I] owner context */

	uint32_t	ppp_flags;		/* [I] configure flags */
#ifdef PIPEX_MPPE
	int ccp_id;				/* [s] CCP packet id */
	struct pipex_mppe
	    mppe_recv,				/* MPPE context for incoming */
	    mppe_send;				/* MPPE context for outgoing */ 
#endif /*PIPEXMPPE */

	struct cpumem	*stat_counters;

	union {
#ifdef PIPEX_PPPOE
		struct pipex_pppoe_session pppoe;	/* context for PPPoE */
#endif /* PIPEX_PPPOE */
#ifdef PIPEX_PPTP
		struct pipex_pptp_session pptp;		/* context for PPTP */
#endif /* PIPEX_PPTP */
#ifdef PIPEX_L2TP
		struct pipex_l2tp_session l2tp;
#endif
		char _proto_unknown[0];
	} proto;
	union {
		struct sockaddr		sa;
		struct sockaddr_in	sin4;
		struct sockaddr_in6	sin6;
		struct sockaddr_dl	sdl;
	} peer, local;					/* [I] */
};

enum pipex_counters {
	pxc_ipackets,	/* packets received from tunnel */
	pxc_ierrors,	/* error packets received from tunnel */
	pxc_ibytes,	/* number of received bytes from tunnel */
	pxc_opackets,	/* packets sent to tunnel */
	pxc_oerrors,	/* error packets on sending to tunnel */
	pxc_obytes,	/* number of sent bytes to tunnel */
	pxc_ncounters
};

/* gre header */
struct pipex_gre_header {
	uint16_t flags;				/* flags and version*/
#define PIPEX_GRE_KFLAG			0x2000	/* keys present */
#define PIPEX_GRE_SFLAG			0x1000	/* seq present */
#define PIPEX_GRE_AFLAG			0x0080	/* ack present */
#define PIPEX_GRE_VER			0x0001	/* gre version code */
#define PIPEX_GRE_VERMASK		0x0007	/* gre version mask */
#define PIPEX_GRE_UNUSEDFLAGS		0xcf78	/* unused at pptp. set 0 in rfc2637 */

	uint16_t type;
#define PIPEX_GRE_PROTO_PPP		0x880b	/* gre/ppp */

	uint16_t len;			/* length not include gre header */
	uint16_t call_id;			/* call_id */
} __packed;

/* pppoe header */
struct pipex_pppoe_header {
	uint8_t vertype;			/* version and type */
#define PIPEX_PPPOE_VERTYPE		0x11	/* version and type code */

	uint8_t code;				/* code */
#define PIPEX_PPPOE_CODE_SESSION	0x00	/* code session */

	uint16_t session_id;			/* session id */
	uint16_t length;			/* length */
} __packed;

/* l2tp header */
struct pipex_l2tp_header {
	uint16_t flagsver;
#define PIPEX_L2TP_FLAG_MASK		0xfff0
#define PIPEX_L2TP_FLAG_TYPE		0x8000
#define PIPEX_L2TP_FLAG_LENGTH		0x4000
#define PIPEX_L2TP_FLAG_SEQUENCE	0x0800
#define PIPEX_L2TP_FLAG_OFFSET		0x0200
#define PIPEX_L2TP_FLAG_PRIORITY	0x0100
#define PIPEX_L2TP_VER_MASK		0x000f
#define PIPEX_L2TP_VER			2
	uint16_t length; /* optional */
	uint16_t tunnel_id;
	uint16_t session_id;
	/* can be followed by option header */
} __packed;

/* l2tp option header */
struct pipex_l2tp_seq_header {
	uint16_t ns;
	uint16_t nr;
} __packed;

struct pipex_l2tp_offset_header {
	uint16_t offset_size;
	/* uint8_t offset_pad[] */
} __packed;

#ifdef PIPEX_DEBUG
#define PIPEX_DBG(a) if (pipex_debug & 1) pipex_session_log a
/* #define PIPEX_MPPE_DBG(a) if (pipex_debug & 1) pipex_session_log a */
#define PIPEX_MPPE_DBG(a)
#else
#define PIPEX_DBG(a)
#define PIPEX_MPPE_DBG(a)
#endif /* PIPEX_DEBUG */

LIST_HEAD(pipex_hash_head, pipex_session);

extern struct pipex_hash_head	pipex_session_list;
extern struct pipex_hash_head	pipex_close_wait_list;
extern struct pipex_hash_head	pipex_peer_addr_hashtable[];
extern struct pipex_hash_head	pipex_id_hashtable[];
extern struct pool		pipex_session_pool;


#define PIPEX_ID_HASHTABLE(key)						\
	(&pipex_id_hashtable[(key) & PIPEX_HASH_MASK])
#define PIPEX_PEER_ADDR_HASHTABLE(key)					\
	(&pipex_peer_addr_hashtable[(key) & PIPEX_HASH_MASK])

#define GETCHAR(c, cp) do {						\
	(c) = *(cp)++;							\
} while (0)

#define PUTCHAR(s, cp) do {						\
	*(cp)++ = (u_char)(s);						\
} while (0)

#define GETSHORT(s, cp) do {						\
	(s) = *(cp)++ << 8;						\
	(s) |= *(cp)++;							\
} while (0)

#define PUTSHORT(s, cp) do {						\
	*(cp)++ = (u_char) ((s) >> 8);					\
	*(cp)++ = (u_char) (s);						\
} while (0)

#define GETLONG(l, cp) do {						\
	(l) = *(cp)++ << 8;						\
	(l) |= *(cp)++; (l) <<= 8;					\
	(l) |= *(cp)++; (l) <<= 8;					\
	(l) |= *(cp)++;							\
} while (0)

#define PUTLONG(l, cp) do {						\
	*(cp)++ = (u_char) ((l) >> 24);					\
	*(cp)++ = (u_char) ((l) >> 16);					\
	*(cp)++ = (u_char) ((l) >> 8);					\
	*(cp)++ = (u_char) (l);						\
} while (0)

#define PIPEX_PULLUP(m0, l)						\
	if ((m0)->m_len < (l)) {					\
		if ((m0)->m_pkthdr.len < (l)) {				\
			PIPEX_DBG((NULL, LOG_DEBUG,			\
			    "<%s> received packet is too short.",	\
			    __func__));					\
			m_freem(m0);					\
			(m0) = NULL;					\
		} else  {						\
			(m0) = m_pullup((m0), (l));			\
			KASSERT((m0) != NULL);				\
		}							\
	}
#define PIPEX_SEEK_NEXTHDR(ptr, len, t)					\
    ((t) (((char *)ptr) + len))
#define SEQ32_LT(a,b)	((int32_t)((a) - (b)) <  0)
#define SEQ32_LE(a,b)	((int32_t)((a) - (b)) <= 0)
#define SEQ32_GT(a,b)	((int32_t)((a) - (b)) >  0)
#define SEQ32_GE(a,b)	((int32_t)((a) - (b)) >= 0)
#define SEQ32_SUB(a,b)	((int32_t)((a) - (b)))

#define SEQ16_LT(a,b)	((int16_t)((a) - (b)) <  0)
#define SEQ16_LE(a,b)	((int16_t)((a) - (b)) <= 0)
#define SEQ16_GT(a,b)	((int16_t)((a) - (b)) >  0)
#define SEQ16_GE(a,b)	((int16_t)((a) - (b)) >= 0)
#define SEQ16_SUB(a,b)	((int16_t)((a) - (b)))

#define	pipex_session_is_acfc_accepted(s)				\
    (((s)->ppp_flags & PIPEX_PPP_ACFC_ACCEPTED)? 1 : 0)
#define	pipex_session_is_pfc_accepted(s)				\
    (((s)->ppp_flags & PIPEX_PPP_PFC_ACCEPTED)? 1 : 0)
#define	pipex_session_is_acfc_enabled(s)				\
    (((s)->ppp_flags & PIPEX_PPP_ACFC_ENABLED)? 1 : 0)
#define	pipex_session_is_pfc_enabled(s)					\
    (((s)->ppp_flags & PIPEX_PPP_PFC_ENABLED)? 1 : 0)
#define	pipex_session_has_acf(s)					\
    (((s)->ppp_flags & PIPEX_PPP_HAS_ACF)? 1 : 0)
#define	pipex_session_is_mppe_accepted(s)				\
    (((s)->ppp_flags & PIPEX_PPP_MPPE_ACCEPTED)? 1 : 0)
#define	pipex_session_is_mppe_enabled(s)				\
    (((s)->ppp_flags & PIPEX_PPP_MPPE_ENABLED)? 1 : 0)
#define	pipex_session_is_mppe_required(s)				\
    (((s)->ppp_flags & PIPEX_PPP_MPPE_REQUIRED)? 1 : 0)
#define pipex_mppe_rc4_keybits(r) ((r)->keylen << 3)
#define pipex_session_is_l2tp_data_sequencing_on(s)			\
    (((s)->proto.l2tp.option_flags & PIPEX_L2TP_USE_SEQUENCING) ? 1 : 0)

#define PIPEX_IPGRE_HDRLEN (sizeof(struct ip) + sizeof(struct pipex_gre_header))
#define PIPEX_TCP_OPTLEN 40
#define	PIPEX_L2TP_MINLEN	8

void                  pipex_destroy_all_sessions (void *);
int                   pipex_init_session(struct pipex_session **,
                                             struct pipex_session_req *);
int                   pipex_link_session(struct pipex_session *,
                          struct ifnet *, void *);
void                  pipex_unlink_session(struct pipex_session *);
void                  pipex_unlink_session_locked(struct pipex_session *);
void                  pipex_export_session_stats(struct pipex_session *,
                          struct pipex_statistics *);
int                   pipex_get_stat (struct pipex_session_stat_req *,
                          void *);
int                   pipex_get_closed (struct pipex_session_list_req *,
                          void *);
struct pipex_session  *pipex_lookup_by_ip_address_locked (struct in_addr);
struct pipex_session  *pipex_lookup_by_ip_address (struct in_addr);
struct pipex_session  *pipex_lookup_by_session_id_locked (int, int);
struct pipex_session  *pipex_lookup_by_session_id (int, int);
void                  pipex_ip_output(struct mbuf *, struct pipex_session *);
void                  pipex_ppp_output(struct mbuf *, struct pipex_session *,
			int);
int                   pipex_ppp_proto(struct mbuf *, struct pipex_session *,
			int, int *);
void                  pipex_ppp_input(struct mbuf *, struct pipex_session *,
			int, struct netstack *);
void                  pipex_ip_input(struct mbuf *, struct pipex_session *,
			struct netstack *);
void                  pipex_ip6_input(struct mbuf *, struct pipex_session *,
			struct netstack *);
struct mbuf           *pipex_common_input(struct pipex_session *,
                          struct mbuf *, int, int, int, struct netstack *);

#ifdef PIPEX_PPPOE
void                  pipex_pppoe_output (struct mbuf *, struct pipex_session *);
#endif

#ifdef PIPEX_PPTP
void                  pipex_pptp_output (struct mbuf *, struct pipex_session *, int, int);
struct pipex_session  *pipex_pptp_userland_lookup_session(struct mbuf *, struct sockaddr *);
#endif

#ifdef PIPEX_L2TP
void                  pipex_l2tp_output (struct mbuf *, struct pipex_session *);
#endif

#ifdef PIPEX_MPPE
void                  pipex_mppe_init (struct pipex_mppe *, int, int, u_char *, int);
void                  GetNewKeyFromSHA (u_char *, u_char *, int, u_char *);
void                  pipex_mppe_reduce_key (struct pipex_mppe *);
void                  mppe_key_change (struct pipex_mppe *);
struct mbuf           *pipex_mppe_input (struct mbuf *,
                          struct pipex_session *);
struct mbuf           *pipex_mppe_output (struct mbuf *,
                          struct pipex_session *, uint16_t);
void                  pipex_ccp_input (struct mbuf *, struct pipex_session *);
int                   pipex_ccp_output (struct pipex_session *, int, int);
#endif

struct mbuf           *adjust_tcp_mss (struct mbuf *, int);
struct mbuf           *ip_is_idle_packet (struct mbuf *, int *);
void                  pipex_session_log (struct pipex_session *, int, const char *, ...)  __attribute__((__format__(__printf__,3,4)));
uint32_t              pipex_sockaddr_hash_key(struct sockaddr *);
int                   pipex_sockaddr_compar_addr(struct sockaddr *, struct sockaddr *);
void                  pipex_timer_start (void);
void                  pipex_timer_stop (void);
void                  pipex_timer (void *);
struct pipex_session  *pipex_iterator(struct pipex_session *,
                          struct pipex_session_iterator *, void *);
