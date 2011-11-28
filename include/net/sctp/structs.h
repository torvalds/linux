/* SCTP kernel implementation
 * (C) Copyright IBM Corp. 2001, 2004
 * Copyright (c) 1999-2000 Cisco, Inc.
 * Copyright (c) 1999-2001 Motorola, Inc.
 * Copyright (c) 2001 Intel Corp.
 *
 * This file is part of the SCTP kernel implementation
 *
 * This SCTP implementation is free software;
 * you can redistribute it and/or modify it under the terms of
 * the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 *
 * This SCTP implementation is distributed in the hope that it
 * will be useful, but WITHOUT ANY WARRANTY; without even the implied
 *		   ************************
 * warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with GNU CC; see the file COPYING.  If not, write to
 * the Free Software Foundation, 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 *
 * Please send any bug reports or fixes you make to the
 * email addresses:
 *    lksctp developers <lksctp-developers@lists.sourceforge.net>
 *
 * Or submit a bug report through the following website:
 *    http://www.sf.net/projects/lksctp
 *
 * Written or modified by:
 *    Randall Stewart	    <randall@sctp.chicago.il.us>
 *    Ken Morneau	    <kmorneau@cisco.com>
 *    Qiaobing Xie	    <qxie1@email.mot.com>
 *    La Monte H.P. Yarroll <piggy@acm.org>
 *    Karl Knutson	    <karl@athena.chicago.il.us>
 *    Jon Grimm		    <jgrimm@us.ibm.com>
 *    Xingang Guo	    <xingang.guo@intel.com>
 *    Hui Huang		    <hui.huang@nokia.com>
 *    Sridhar Samudrala	    <sri@us.ibm.com>
 *    Daisy Chang	    <daisyc@us.ibm.com>
 *    Dajiang Zhang	    <dajiang.zhang@nokia.com>
 *    Ardelle Fan	    <ardelle.fan@intel.com>
 *    Ryan Layer	    <rmlayer@us.ibm.com>
 *    Anup Pemmaiah	    <pemmaiah@cc.usu.edu>
 *    Kevin Gao             <kevin.gao@intel.com>
 *
 * Any bugs reported given to us we will try to fix... any fixes shared will
 * be incorporated into the next SCTP release.
 */

#ifndef __sctp_structs_h__
#define __sctp_structs_h__

#include <linux/time.h>		/* We get struct timespec.    */
#include <linux/socket.h>	/* linux/in.h needs this!!    */
#include <linux/in.h>		/* We get struct sockaddr_in. */
#include <linux/in6.h>		/* We get struct in6_addr     */
#include <linux/ipv6.h>
#include <asm/param.h>		/* We get MAXHOSTNAMELEN.     */
#include <linux/atomic.h>		/* This gets us atomic counters.  */
#include <linux/skbuff.h>	/* We need sk_buff_head. */
#include <linux/workqueue.h>	/* We need tq_struct.	 */
#include <linux/sctp.h>		/* We need sctp* header structs.  */
#include <net/sctp/auth.h>	/* We need auth specific structs */

/* A convenience structure for handling sockaddr structures.
 * We should wean ourselves off this.
 */
union sctp_addr {
	struct sockaddr_in v4;
	struct sockaddr_in6 v6;
	struct sockaddr sa;
};

/* Forward declarations for data structures. */
struct sctp_globals;
struct sctp_endpoint;
struct sctp_association;
struct sctp_transport;
struct sctp_packet;
struct sctp_chunk;
struct sctp_inq;
struct sctp_outq;
struct sctp_bind_addr;
struct sctp_ulpq;
struct sctp_ep_common;
struct sctp_ssnmap;
struct crypto_hash;


#include <net/sctp/tsnmap.h>
#include <net/sctp/ulpevent.h>
#include <net/sctp/ulpqueue.h>

/* Structures useful for managing bind/connect. */

struct sctp_bind_bucket {
	unsigned short	port;
	unsigned short	fastreuse;
	struct hlist_node	node;
	struct hlist_head	owner;
};

struct sctp_bind_hashbucket {
	spinlock_t	lock;
	struct hlist_head	chain;
};

/* Used for hashing all associations.  */
struct sctp_hashbucket {
	rwlock_t	lock;
	struct hlist_head	chain;
} __attribute__((__aligned__(8)));


/* The SCTP globals structure. */
extern struct sctp_globals {
	/* RFC2960 Section 14. Suggested SCTP Protocol Parameter Values
	 *
	 * The following protocol parameters are RECOMMENDED:
	 *
	 * RTO.Initial		    - 3	 seconds
	 * RTO.Min		    - 1	 second
	 * RTO.Max		   -  60 seconds
	 * RTO.Alpha		    - 1/8  (3 when converted to right shifts.)
	 * RTO.Beta		    - 1/4  (2 when converted to right shifts.)
	 */
	unsigned int rto_initial;
	unsigned int rto_min;
	unsigned int rto_max;

	/* Note: rto_alpha and rto_beta are really defined as inverse
	 * powers of two to facilitate integer operations.
	 */
	int rto_alpha;
	int rto_beta;

	/* Max.Burst		    - 4 */
	int max_burst;

	/* Whether Cookie Preservative is enabled(1) or not(0) */
	int cookie_preserve_enable;

	/* Valid.Cookie.Life	    - 60  seconds  */
	unsigned int valid_cookie_life;

	/* Delayed SACK timeout  200ms default*/
	unsigned int sack_timeout;

	/* HB.interval		    - 30 seconds  */
	unsigned int hb_interval;

	/* Association.Max.Retrans  - 10 attempts
	 * Path.Max.Retrans	    - 5	 attempts (per destination address)
	 * Max.Init.Retransmits	    - 8	 attempts
	 */
	int max_retrans_association;
	int max_retrans_path;
	int max_retrans_init;

	/*
	 * Policy for preforming sctp/socket accounting
	 * 0   - do socket level accounting, all assocs share sk_sndbuf
	 * 1   - do sctp accounting, each asoc may use sk_sndbuf bytes
	 */
	int sndbuf_policy;

	/*
	 * Policy for preforming sctp/socket accounting
	 * 0   - do socket level accounting, all assocs share sk_rcvbuf
	 * 1   - do sctp accounting, each asoc may use sk_rcvbuf bytes
	 */
	int rcvbuf_policy;

	/* The following variables are implementation specific.	 */

	/* Default initialization values to be applied to new associations. */
	__u16 max_instreams;
	__u16 max_outstreams;

	/* This is a list of groups of functions for each address
	 * family that we support.
	 */
	struct list_head address_families;

	/* This is the hash of all endpoints. */
	int ep_hashsize;
	struct sctp_hashbucket *ep_hashtable;

	/* This is the hash of all associations. */
	int assoc_hashsize;
	struct sctp_hashbucket *assoc_hashtable;

	/* This is the sctp port control hash.	*/
	int port_hashsize;
	struct sctp_bind_hashbucket *port_hashtable;

	/* This is the global local address list.
	 * We actively maintain this complete list of addresses on
	 * the system by catching address add/delete events.
	 *
	 * It is a list of sctp_sockaddr_entry.
	 */
	struct list_head local_addr_list;
	int default_auto_asconf;
	struct list_head addr_waitq;
	struct timer_list addr_wq_timer;
	struct list_head auto_asconf_splist;
	spinlock_t addr_wq_lock;

	/* Lock that protects the local_addr_list writers */
	spinlock_t addr_list_lock;
	
	/* Flag to indicate if addip is enabled. */
	int addip_enable;
	int addip_noauth_enable;

	/* Flag to indicate if PR-SCTP is enabled. */
	int prsctp_enable;

	/* Flag to idicate if SCTP-AUTH is enabled */
	int auth_enable;

	/*
	 * Policy to control SCTP IPv4 address scoping
	 * 0   - Disable IPv4 address scoping
	 * 1   - Enable IPv4 address scoping
	 * 2   - Selectively allow only IPv4 private addresses
	 * 3   - Selectively allow only IPv4 link local address
	 */
	int ipv4_scope_policy;

	/* Flag to indicate whether computing and verifying checksum
	 * is disabled. */
        int checksum_disable;

	/* Threshold for rwnd update SACKS.  Receive buffer shifted this many
	 * bits is an indicator of when to send and window update SACK.
	 */
	int rwnd_update_shift;
} sctp_globals;

#define sctp_rto_initial		(sctp_globals.rto_initial)
#define sctp_rto_min			(sctp_globals.rto_min)
#define sctp_rto_max			(sctp_globals.rto_max)
#define sctp_rto_alpha			(sctp_globals.rto_alpha)
#define sctp_rto_beta			(sctp_globals.rto_beta)
#define sctp_max_burst			(sctp_globals.max_burst)
#define sctp_valid_cookie_life		(sctp_globals.valid_cookie_life)
#define sctp_cookie_preserve_enable	(sctp_globals.cookie_preserve_enable)
#define sctp_max_retrans_association	(sctp_globals.max_retrans_association)
#define sctp_sndbuf_policy	 	(sctp_globals.sndbuf_policy)
#define sctp_rcvbuf_policy	 	(sctp_globals.rcvbuf_policy)
#define sctp_max_retrans_path		(sctp_globals.max_retrans_path)
#define sctp_max_retrans_init		(sctp_globals.max_retrans_init)
#define sctp_sack_timeout		(sctp_globals.sack_timeout)
#define sctp_hb_interval		(sctp_globals.hb_interval)
#define sctp_max_instreams		(sctp_globals.max_instreams)
#define sctp_max_outstreams		(sctp_globals.max_outstreams)
#define sctp_address_families		(sctp_globals.address_families)
#define sctp_ep_hashsize		(sctp_globals.ep_hashsize)
#define sctp_ep_hashtable		(sctp_globals.ep_hashtable)
#define sctp_assoc_hashsize		(sctp_globals.assoc_hashsize)
#define sctp_assoc_hashtable		(sctp_globals.assoc_hashtable)
#define sctp_port_hashsize		(sctp_globals.port_hashsize)
#define sctp_port_hashtable		(sctp_globals.port_hashtable)
#define sctp_local_addr_list		(sctp_globals.local_addr_list)
#define sctp_local_addr_lock		(sctp_globals.addr_list_lock)
#define sctp_auto_asconf_splist		(sctp_globals.auto_asconf_splist)
#define sctp_addr_waitq			(sctp_globals.addr_waitq)
#define sctp_addr_wq_timer		(sctp_globals.addr_wq_timer)
#define sctp_addr_wq_lock		(sctp_globals.addr_wq_lock)
#define sctp_default_auto_asconf	(sctp_globals.default_auto_asconf)
#define sctp_scope_policy		(sctp_globals.ipv4_scope_policy)
#define sctp_addip_enable		(sctp_globals.addip_enable)
#define sctp_addip_noauth		(sctp_globals.addip_noauth_enable)
#define sctp_prsctp_enable		(sctp_globals.prsctp_enable)
#define sctp_auth_enable		(sctp_globals.auth_enable)
#define sctp_checksum_disable		(sctp_globals.checksum_disable)
#define sctp_rwnd_upd_shift		(sctp_globals.rwnd_update_shift)

/* SCTP Socket type: UDP or TCP style. */
typedef enum {
	SCTP_SOCKET_UDP = 0,
	SCTP_SOCKET_UDP_HIGH_BANDWIDTH,
	SCTP_SOCKET_TCP
} sctp_socket_type_t;

/* Per socket SCTP information. */
struct sctp_sock {
	/* inet_sock has to be the first member of sctp_sock */
	struct inet_sock inet;
	/* What kind of a socket is this? */
	sctp_socket_type_t type;

	/* PF_ family specific functions.  */
	struct sctp_pf *pf;

	/* Access to HMAC transform. */
	struct crypto_hash *hmac;

	/* What is our base endpointer? */
	struct sctp_endpoint *ep;

	struct sctp_bind_bucket *bind_hash;
	/* Various Socket Options.  */
	__u16 default_stream;
	__u32 default_ppid;
	__u16 default_flags;
	__u32 default_context;
	__u32 default_timetolive;
	__u32 default_rcv_context;
	int max_burst;

	/* Heartbeat interval: The endpoint sends out a Heartbeat chunk to
	 * the destination address every heartbeat interval. This value
	 * will be inherited by all new associations.
	 */
	__u32 hbinterval;

	/* This is the max_retrans value for new associations. */
	__u16 pathmaxrxt;

	/* The initial Path MTU to use for new associations. */
	__u32 pathmtu;

	/* The default SACK delay timeout for new associations. */
	__u32 sackdelay;
	__u32 sackfreq;

	/* Flags controlling Heartbeat, SACK delay, and Path MTU Discovery. */
	__u32 param_flags;

	struct sctp_initmsg initmsg;
	struct sctp_rtoinfo rtoinfo;
	struct sctp_paddrparams paddrparam;
	struct sctp_event_subscribe subscribe;
	struct sctp_assocparams assocparams;
	int user_frag;
	__u32 autoclose;
	__u8 nodelay;
	__u8 disable_fragments;
	__u8 v4mapped;
	__u8 frag_interleave;
	__u32 adaptation_ind;
	__u32 pd_point;

	atomic_t pd_mode;
	/* Receive to here while partial delivery is in effect. */
	struct sk_buff_head pd_lobby;
	struct list_head auto_asconf_list;
	int do_auto_asconf;
};

static inline struct sctp_sock *sctp_sk(const struct sock *sk)
{
       return (struct sctp_sock *)sk;
}

static inline struct sock *sctp_opt2sk(const struct sctp_sock *sp)
{
       return (struct sock *)sp;
}

#if defined(CONFIG_IPV6) || defined(CONFIG_IPV6_MODULE)
struct sctp6_sock {
       struct sctp_sock  sctp;
       struct ipv6_pinfo inet6;
};
#endif /* CONFIG_IPV6 */


/* This is our APPLICATION-SPECIFIC state cookie.
 * THIS IS NOT DICTATED BY THE SPECIFICATION.
 */
/* These are the parts of an association which we send in the cookie.
 * Most of these are straight out of:
 * RFC2960 12.2 Parameters necessary per association (i.e. the TCB)
 *
 */

struct sctp_cookie {

	/* My	       : Tag expected in every inbound packet and sent
	 * Verification: in the INIT or INIT ACK chunk.
	 * Tag	       :
	 */
	__u32 my_vtag;

	/* Peer's      : Tag expected in every outbound packet except
	 * Verification: in the INIT chunk.
	 * Tag	       :
	 */
	__u32 peer_vtag;

	/* The rest of these are not from the spec, but really need to
	 * be in the cookie.
	 */

	/* My Tie Tag  : Assist in discovering a restarting association. */
	__u32 my_ttag;

	/* Peer's Tie Tag: Assist in discovering a restarting association. */
	__u32 peer_ttag;

	/* When does this cookie expire? */
	struct timeval expiration;

	/* Number of inbound/outbound streams which are set
	 * and negotiated during the INIT process.
	 */
	__u16 sinit_num_ostreams;
	__u16 sinit_max_instreams;

	/* This is the first sequence number I used.  */
	__u32 initial_tsn;

	/* This holds the originating address of the INIT packet.  */
	union sctp_addr peer_addr;

	/* IG Section 2.35.3 
	 * Include the source port of the INIT-ACK
	 */
	__u16		my_port;

	__u8 prsctp_capable;

	/* Padding for future use */
	__u8 padding;  		

	__u32 adaptation_ind;

	__u8 auth_random[sizeof(sctp_paramhdr_t) + SCTP_AUTH_RANDOM_LENGTH];
	__u8 auth_hmacs[SCTP_AUTH_NUM_HMACS * sizeof(__u16) + 2];
	__u8 auth_chunks[sizeof(sctp_paramhdr_t) + SCTP_AUTH_MAX_CHUNKS];

	/* This is a shim for my peer's INIT packet, followed by
	 * a copy of the raw address list of the association.
	 * The length of the raw address list is saved in the
	 * raw_addr_list_len field, which will be used at the time when
	 * the association TCB is re-constructed from the cookie.
	 */
	__u32 raw_addr_list_len;
	struct sctp_init_chunk peer_init[0];
};


/* The format of our cookie that we send to our peer. */
struct sctp_signed_cookie {
	__u8 signature[SCTP_SECRET_SIZE];
	__u32 __pad;		/* force sctp_cookie alignment to 64 bits */
	struct sctp_cookie c;
} __packed;

/* This is another convenience type to allocate memory for address
 * params for the maximum size and pass such structures around
 * internally.
 */
union sctp_addr_param {
	struct sctp_paramhdr p;
	struct sctp_ipv4addr_param v4;
	struct sctp_ipv6addr_param v6;
};

/* A convenience type to allow walking through the various
 * parameters and avoid casting all over the place.
 */
union sctp_params {
	void *v;
	struct sctp_paramhdr *p;
	struct sctp_cookie_preserve_param *life;
	struct sctp_hostname_param *dns;
	struct sctp_cookie_param *cookie;
	struct sctp_supported_addrs_param *sat;
	struct sctp_ipv4addr_param *v4;
	struct sctp_ipv6addr_param *v6;
	union sctp_addr_param *addr;
	struct sctp_adaptation_ind_param *aind;
	struct sctp_supported_ext_param *ext;
	struct sctp_random_param *random;
	struct sctp_chunks_param *chunks;
	struct sctp_hmac_algo_param *hmac_algo;
	struct sctp_addip_param *addip;
};

/* RFC 2960.  Section 3.3.5 Heartbeat.
 *    Heartbeat Information: variable length
 *    The Sender-specific Heartbeat Info field should normally include
 *    information about the sender's current time when this HEARTBEAT
 *    chunk is sent and the destination transport address to which this
 *    HEARTBEAT is sent (see Section 8.3).
 */
typedef struct sctp_sender_hb_info {
	struct sctp_paramhdr param_hdr;
	union sctp_addr daddr;
	unsigned long sent_at;
	__u64 hb_nonce;
} __packed sctp_sender_hb_info_t;

/*
 *  RFC 2960 1.3.2 Sequenced Delivery within Streams
 *
 *  The term "stream" is used in SCTP to refer to a sequence of user
 *  messages that are to be delivered to the upper-layer protocol in
 *  order with respect to other messages within the same stream.  This is
 *  in contrast to its usage in TCP, where it refers to a sequence of
 *  bytes (in this document a byte is assumed to be eight bits).
 *  ...
 *
 *  This is the structure we use to track both our outbound and inbound
 *  SSN, or Stream Sequence Numbers.
 */

struct sctp_stream {
	__u16 *ssn;
	unsigned int len;
};

struct sctp_ssnmap {
	struct sctp_stream in;
	struct sctp_stream out;
	int malloced;
};

struct sctp_ssnmap *sctp_ssnmap_new(__u16 in, __u16 out,
				    gfp_t gfp);
void sctp_ssnmap_free(struct sctp_ssnmap *map);
void sctp_ssnmap_clear(struct sctp_ssnmap *map);

/* What is the current SSN number for this stream? */
static inline __u16 sctp_ssn_peek(struct sctp_stream *stream, __u16 id)
{
	return stream->ssn[id];
}

/* Return the next SSN number for this stream.	*/
static inline __u16 sctp_ssn_next(struct sctp_stream *stream, __u16 id)
{
	return stream->ssn[id]++;
}

/* Skip over this ssn and all below. */
static inline void sctp_ssn_skip(struct sctp_stream *stream, __u16 id, 
				 __u16 ssn)
{
	stream->ssn[id] = ssn+1;
}
              
/*
 * Pointers to address related SCTP functions.
 * (i.e. things that depend on the address family.)
 */
struct sctp_af {
	int		(*sctp_xmit)	(struct sk_buff *skb,
					 struct sctp_transport *);
	int		(*setsockopt)	(struct sock *sk,
					 int level,
					 int optname,
					 char __user *optval,
					 unsigned int optlen);
	int		(*getsockopt)	(struct sock *sk,
					 int level,
					 int optname,
					 char __user *optval,
					 int __user *optlen);
	int		(*compat_setsockopt)	(struct sock *sk,
					 int level,
					 int optname,
					 char __user *optval,
					 unsigned int optlen);
	int		(*compat_getsockopt)	(struct sock *sk,
					 int level,
					 int optname,
					 char __user *optval,
					 int __user *optlen);
	void		(*get_dst)	(struct sctp_transport *t,
					 union sctp_addr *saddr,
					 struct flowi *fl,
					 struct sock *sk);
	void		(*get_saddr)	(struct sctp_sock *sk,
					 struct sctp_transport *t,
					 struct flowi *fl);
	void		(*copy_addrlist) (struct list_head *,
					  struct net_device *);
	int		(*cmp_addr)	(const union sctp_addr *addr1,
					 const union sctp_addr *addr2);
	void		(*addr_copy)	(union sctp_addr *dst,
					 union sctp_addr *src);
	void		(*from_skb)	(union sctp_addr *,
					 struct sk_buff *skb,
					 int saddr);
	void		(*from_sk)	(union sctp_addr *,
					 struct sock *sk);
	void		(*to_sk_saddr)	(union sctp_addr *,
					 struct sock *sk);
	void		(*to_sk_daddr)	(union sctp_addr *,
					 struct sock *sk);
	void		(*from_addr_param) (union sctp_addr *,
					    union sctp_addr_param *,
					    __be16 port, int iif);
	int		(*to_addr_param) (const union sctp_addr *,
					  union sctp_addr_param *); 
	int		(*addr_valid)	(union sctp_addr *,
					 struct sctp_sock *,
					 const struct sk_buff *);
	sctp_scope_t	(*scope) (union sctp_addr *);
	void		(*inaddr_any)	(union sctp_addr *, __be16);
	int		(*is_any)	(const union sctp_addr *);
	int		(*available)	(union sctp_addr *,
					 struct sctp_sock *);
	int		(*skb_iif)	(const struct sk_buff *sk);
	int		(*is_ce)	(const struct sk_buff *sk);
	void		(*seq_dump_addr)(struct seq_file *seq,
					 union sctp_addr *addr);
	void		(*ecn_capable)(struct sock *sk);
	__u16		net_header_len;
	int		sockaddr_len;
	sa_family_t	sa_family;
	struct list_head list;
};

struct sctp_af *sctp_get_af_specific(sa_family_t);
int sctp_register_af(struct sctp_af *);

/* Protocol family functions. */
struct sctp_pf {
	void (*event_msgname)(struct sctp_ulpevent *, char *, int *);
	void (*skb_msgname)  (struct sk_buff *, char *, int *);
	int  (*af_supported) (sa_family_t, struct sctp_sock *);
	int  (*cmp_addr) (const union sctp_addr *,
			  const union sctp_addr *,
			  struct sctp_sock *);
	int  (*bind_verify) (struct sctp_sock *, union sctp_addr *);
	int  (*send_verify) (struct sctp_sock *, union sctp_addr *);
	int  (*supported_addrs)(const struct sctp_sock *, __be16 *);
	struct sock *(*create_accept_sk) (struct sock *sk,
					  struct sctp_association *asoc);
	void (*addr_v4map) (struct sctp_sock *, union sctp_addr *);
	struct sctp_af *af;
};


/* Structure to track chunk fragments that have been acked, but peer
 * fragments of the same message have not.
 */
struct sctp_datamsg {
	/* Chunks waiting to be submitted to lower layer. */
	struct list_head chunks;
	/* Reference counting. */
	atomic_t refcnt;
	/* When is this message no longer interesting to the peer? */
	unsigned long expires_at;
	/* Did the messenge fail to send? */
	int send_error;
	u8 send_failed:1,
	   can_abandon:1,   /* can chunks from this message can be abandoned. */
	   can_delay;	    /* should this message be Nagle delayed */
};

struct sctp_datamsg *sctp_datamsg_from_user(struct sctp_association *,
					    struct sctp_sndrcvinfo *,
					    struct msghdr *, int len);
void sctp_datamsg_free(struct sctp_datamsg *);
void sctp_datamsg_put(struct sctp_datamsg *);
void sctp_chunk_fail(struct sctp_chunk *, int error);
int sctp_chunk_abandoned(struct sctp_chunk *);

/* RFC2960 1.4 Key Terms
 *
 * o Chunk: A unit of information within an SCTP packet, consisting of
 * a chunk header and chunk-specific content.
 *
 * As a matter of convenience, we remember the SCTP common header for
 * each chunk as well as a few other header pointers...
 */
struct sctp_chunk {
	struct list_head list;

	atomic_t refcnt;

	/* This is our link to the per-transport transmitted list.  */
	struct list_head transmitted_list;

	/* This field is used by chunks that hold fragmented data.
	 * For the first fragment this is the list that holds the rest of
	 * fragments. For the remaining fragments, this is the link to the
	 * frag_list maintained in the first fragment.
	 */
	struct list_head frag_list;

	/* This points to the sk_buff containing the actual data.  */
	struct sk_buff *skb;

	/* These are the SCTP headers by reverse order in a packet.
	 * Note that some of these may happen more than once.  In that
	 * case, we point at the "current" one, whatever that means
	 * for that level of header.
	 */

	/* We point this at the FIRST TLV parameter to chunk_hdr.  */
	union sctp_params param_hdr;
	union {
		__u8 *v;
		struct sctp_datahdr *data_hdr;
		struct sctp_inithdr *init_hdr;
		struct sctp_sackhdr *sack_hdr;
		struct sctp_heartbeathdr *hb_hdr;
		struct sctp_sender_hb_info *hbs_hdr;
		struct sctp_shutdownhdr *shutdown_hdr;
		struct sctp_signed_cookie *cookie_hdr;
		struct sctp_ecnehdr *ecne_hdr;
		struct sctp_cwrhdr *ecn_cwr_hdr;
		struct sctp_errhdr *err_hdr;
		struct sctp_addiphdr *addip_hdr;
		struct sctp_fwdtsn_hdr *fwdtsn_hdr;
		struct sctp_authhdr *auth_hdr;
	} subh;

	__u8 *chunk_end;

	struct sctp_chunkhdr *chunk_hdr;
	struct sctphdr *sctp_hdr;

	/* This needs to be recoverable for SCTP_SEND_FAILED events. */
	struct sctp_sndrcvinfo sinfo;

	/* Which association does this belong to?  */
	struct sctp_association *asoc;

	/* What endpoint received this chunk? */
	struct sctp_ep_common *rcvr;

	/* We fill this in if we are calculating RTT. */
	unsigned long sent_at;

	/* What is the origin IP address for this chunk?  */
	union sctp_addr source;
	/* Destination address for this chunk. */
	union sctp_addr dest;

	/* For outbound message, track all fragments for SEND_FAILED. */
	struct sctp_datamsg *msg;

	/* For an inbound chunk, this tells us where it came from.
	 * For an outbound chunk, it tells us where we'd like it to
	 * go.	It is NULL if we have no preference.
	 */
	struct sctp_transport *transport;

	/* SCTP-AUTH:  For the special case inbound processing of COOKIE-ECHO
	 * we need save a pointer to the AUTH chunk, since the SCTP-AUTH
	 * spec violates the principle premis that all chunks are processed
	 * in order.
	 */
	struct sk_buff *auth_chunk;

#define SCTP_CAN_FRTX 0x0
#define SCTP_NEED_FRTX 0x1
#define SCTP_DONT_FRTX 0x2
	__u16	rtt_in_progress:1,	/* This chunk used for RTT calc? */
		has_tsn:1,		/* Does this chunk have a TSN yet? */
		has_ssn:1,		/* Does this chunk have a SSN yet? */
		singleton:1,		/* Only chunk in the packet? */
		end_of_packet:1,	/* Last chunk in the packet? */
		ecn_ce_done:1,		/* Have we processed the ECN CE bit? */
		pdiscard:1,		/* Discard the whole packet now? */
		tsn_gap_acked:1,	/* Is this chunk acked by a GAP ACK? */
		data_accepted:1,	/* At least 1 chunk accepted */
		auth:1,			/* IN: was auth'ed | OUT: needs auth */
		has_asconf:1,		/* IN: have seen an asconf before */
		tsn_missing_report:2,	/* Data chunk missing counter. */
		fast_retransmit:2;	/* Is this chunk fast retransmitted? */
};

void sctp_chunk_hold(struct sctp_chunk *);
void sctp_chunk_put(struct sctp_chunk *);
int sctp_user_addto_chunk(struct sctp_chunk *chunk, int off, int len,
			  struct iovec *data);
void sctp_chunk_free(struct sctp_chunk *);
void  *sctp_addto_chunk(struct sctp_chunk *, int len, const void *data);
void  *sctp_addto_chunk_fixed(struct sctp_chunk *, int len, const void *data);
struct sctp_chunk *sctp_chunkify(struct sk_buff *,
				 const struct sctp_association *,
				 struct sock *);
void sctp_init_addrs(struct sctp_chunk *, union sctp_addr *,
		     union sctp_addr *);
const union sctp_addr *sctp_source(const struct sctp_chunk *chunk);

enum {
	SCTP_ADDR_NEW,		/* new address added to assoc/ep */
	SCTP_ADDR_SRC,		/* address can be used as source */
	SCTP_ADDR_DEL,		/* address about to be deleted */
};

/* This is a structure for holding either an IPv6 or an IPv4 address.  */
struct sctp_sockaddr_entry {
	struct list_head list;
	struct rcu_head	rcu;
	union sctp_addr a;
	__u8 state;
	__u8 valid;
};

#define SCTP_ADDRESS_TICK_DELAY	500

typedef struct sctp_chunk *(sctp_packet_phandler_t)(struct sctp_association *);

/* This structure holds lists of chunks as we are assembling for
 * transmission.
 */
struct sctp_packet {
	/* These are the SCTP header values (host order) for the packet. */
	__u16 source_port;
	__u16 destination_port;
	__u32 vtag;

	/* This contains the payload chunks.  */
	struct list_head chunk_list;

	/* This is the overhead of the sctp and ip headers. */
	size_t overhead;
	/* This is the total size of all chunks INCLUDING padding.  */
	size_t size;

	/* The packet is destined for this transport address.
	 * The function we finally use to pass down to the next lower
	 * layer lives in the transport structure.
	 */
	struct sctp_transport *transport;

	/* pointer to the auth chunk for this packet */
	struct sctp_chunk *auth;

	u8  has_cookie_echo:1,	/* This packet contains a COOKIE-ECHO chunk. */
	    has_sack:1,		/* This packet contains a SACK chunk. */
	    has_auth:1,		/* This packet contains an AUTH chunk */
	    has_data:1,		/* This packet contains at least 1 DATA chunk */
	    ipfragok:1,		/* So let ip fragment this packet */
	    malloced:1;		/* Is it malloced? */
};

struct sctp_packet *sctp_packet_init(struct sctp_packet *,
				     struct sctp_transport *,
				     __u16 sport, __u16 dport);
struct sctp_packet *sctp_packet_config(struct sctp_packet *, __u32 vtag, int);
sctp_xmit_t sctp_packet_transmit_chunk(struct sctp_packet *,
                                       struct sctp_chunk *, int);
sctp_xmit_t sctp_packet_append_chunk(struct sctp_packet *,
                                     struct sctp_chunk *);
int sctp_packet_transmit(struct sctp_packet *);
void sctp_packet_free(struct sctp_packet *);

static inline int sctp_packet_empty(struct sctp_packet *packet)
{
	return packet->size == packet->overhead;
}

/* This represents a remote transport address.
 * For local transport addresses, we just use union sctp_addr.
 *
 * RFC2960 Section 1.4 Key Terms
 *
 *   o	Transport address:  A Transport Address is traditionally defined
 *	by Network Layer address, Transport Layer protocol and Transport
 *	Layer port number.  In the case of SCTP running over IP, a
 *	transport address is defined by the combination of an IP address
 *	and an SCTP port number (where SCTP is the Transport protocol).
 *
 * RFC2960 Section 7.1 SCTP Differences from TCP Congestion control
 *
 *   o	The sender keeps a separate congestion control parameter set for
 *	each of the destination addresses it can send to (not each
 *	source-destination pair but for each destination).  The parameters
 *	should decay if the address is not used for a long enough time
 *	period.
 *
 */
struct sctp_transport {
	/* A list of transports. */
	struct list_head transports;

	/* Reference counting. */
	atomic_t refcnt;
	__u32	 dead:1,
		/* RTO-Pending : A flag used to track if one of the DATA
		 *		chunks sent to this address is currently being
		 *		used to compute a RTT. If this flag is 0,
		 *		the next DATA chunk sent to this destination
		 *		should be used to compute a RTT and this flag
		 *		should be set. Every time the RTT
		 *		calculation completes (i.e. the DATA chunk
		 *		is SACK'd) clear this flag.
		 */
		 rto_pending:1,

		/*
		 * hb_sent : a flag that signals that we have a pending
		 * heartbeat.
		 */
		hb_sent:1,

		/* Is the Path MTU update pending on this tranport */
		pmtu_pending:1,

		/* Is this structure kfree()able? */
		malloced:1;

	struct flowi fl;

	/* This is the peer's IP address and port. */
	union sctp_addr ipaddr;

	/* These are the functions we call to handle LLP stuff.	 */
	struct sctp_af *af_specific;

	/* Which association do we belong to?  */
	struct sctp_association *asoc;

	/* RFC2960
	 *
	 * 12.3 Per Transport Address Data
	 *
	 * For each destination transport address in the peer's
	 * address list derived from the INIT or INIT ACK chunk, a
	 * number of data elements needs to be maintained including:
	 */
	/* RTO	       : The current retransmission timeout value.  */
	unsigned long rto;

	__u32 rtt;		/* This is the most recent RTT.	 */

	/* RTTVAR      : The current RTT variation.  */
	__u32 rttvar;

	/* SRTT	       : The current smoothed round trip time.	*/
	__u32 srtt;

	/*
	 * These are the congestion stats.
	 */
	/* cwnd	       : The current congestion window.	 */
	__u32 cwnd;		  /* This is the actual cwnd.  */

	/* ssthresh    : The current slow start threshold value.  */
	__u32 ssthresh;

	/* partial     : The tracking method for increase of cwnd when in
	 * bytes acked : congestion avoidance mode (see Section 6.2.2)
	 */
	__u32 partial_bytes_acked;

	/* Data that has been sent, but not acknowledged. */
	__u32 flight_size;

	__u32 burst_limited;	/* Holds old cwnd when max.burst is applied */

	/* Destination */
	struct dst_entry *dst;
	/* Source address. */
	union sctp_addr saddr;

	/* Heartbeat interval: The endpoint sends out a Heartbeat chunk to
	 * the destination address every heartbeat interval.
	 */
	unsigned long hbinterval;

	/* SACK delay timeout */
	unsigned long sackdelay;
	__u32 sackfreq;

	/* When was the last time (in jiffies) that we heard from this
	 * transport?  We use this to pick new active and retran paths.
	 */
	unsigned long last_time_heard;

	/* Last time(in jiffies) when cwnd is reduced due to the congestion
	 * indication based on ECNE chunk.
	 */
	unsigned long last_time_ecne_reduced;

	/* This is the max_retrans value for the transport and will
	 * be initialized from the assocs value.  This can be changed
	 * using SCTP_SET_PEER_ADDR_PARAMS socket option.
	 */
	__u16 pathmaxrxt;

	/* PMTU	      : The current known path MTU.  */
	__u32 pathmtu;

	/* Flags controlling Heartbeat, SACK delay, and Path MTU Discovery. */
	__u32 param_flags;

	/* The number of times INIT has been sent on this transport. */
	int init_sent_count;

	/* state       : The current state of this destination,
	 *             : i.e. SCTP_ACTIVE, SCTP_INACTIVE, SCTP_UNKNOWN.
	 */
	int state;

	/* These are the error stats for this destination.  */

	/* Error count : The current error count for this destination.	*/
	unsigned short error_count;

	/* Per	       : A timer used by each destination.
	 * Destination :
	 * Timer       :
	 *
	 * [Everywhere else in the text this is called T3-rtx. -ed]
	 */
	struct timer_list T3_rtx_timer;

	/* Heartbeat timer is per destination. */
	struct timer_list hb_timer;

	/* Timer to handle ICMP proto unreachable envets */
	struct timer_list proto_unreach_timer;

	/* Since we're using per-destination retransmission timers
	 * (see above), we're also using per-destination "transmitted"
	 * queues.  This probably ought to be a private struct
	 * accessible only within the outqueue, but it's not, yet.
	 */
	struct list_head transmitted;

	/* We build bundle-able packets for this transport here.  */
	struct sctp_packet packet;

	/* This is the list of transports that have chunks to send.  */
	struct list_head send_ready;

	/* State information saved for SFR_CACC algorithm. The key
	 * idea in SFR_CACC is to maintain state at the sender on a
	 * per-destination basis when a changeover happens.
	 *	char changeover_active;
	 *	char cycling_changeover;
	 *	__u32 next_tsn_at_change;
	 *	char cacc_saw_newack;
	 */
	struct {
		/* An unsigned integer, which stores the next TSN to be
		 * used by the sender, at the moment of changeover.
		 */
		__u32 next_tsn_at_change;

		/* A flag which indicates the occurrence of a changeover */
		char changeover_active;

		/* A flag which indicates whether the change of primary is
		 * the first switch to this destination address during an
		 * active switch.
		 */
		char cycling_changeover;

		/* A temporary flag, which is used during the processing of
		 * a SACK to estimate the causative TSN(s)'s group.
		 */
		char cacc_saw_newack;
	} cacc;

	/* 64-bit random number sent with heartbeat. */
	__u64 hb_nonce;
};

struct sctp_transport *sctp_transport_new(const union sctp_addr *,
					  gfp_t);
void sctp_transport_set_owner(struct sctp_transport *,
			      struct sctp_association *);
void sctp_transport_route(struct sctp_transport *, union sctp_addr *,
			  struct sctp_sock *);
void sctp_transport_pmtu(struct sctp_transport *, struct sock *sk);
void sctp_transport_free(struct sctp_transport *);
void sctp_transport_reset_timers(struct sctp_transport *);
void sctp_transport_hold(struct sctp_transport *);
void sctp_transport_put(struct sctp_transport *);
void sctp_transport_update_rto(struct sctp_transport *, __u32);
void sctp_transport_raise_cwnd(struct sctp_transport *, __u32, __u32);
void sctp_transport_lower_cwnd(struct sctp_transport *, sctp_lower_cwnd_t);
void sctp_transport_burst_limited(struct sctp_transport *);
void sctp_transport_burst_reset(struct sctp_transport *);
unsigned long sctp_transport_timeout(struct sctp_transport *);
void sctp_transport_reset(struct sctp_transport *);
void sctp_transport_update_pmtu(struct sctp_transport *, u32);


/* This is the structure we use to queue packets as they come into
 * SCTP.  We write packets to it and read chunks from it.
 */
struct sctp_inq {
	/* This is actually a queue of sctp_chunk each
	 * containing a partially decoded packet.
	 */
	struct list_head in_chunk_list;
	/* This is the packet which is currently off the in queue and is
	 * being worked on through the inbound chunk processing.
	 */
	struct sctp_chunk *in_progress;

	/* This is the delayed task to finish delivering inbound
	 * messages.
	 */
	struct work_struct immediate;

	int malloced;	     /* Is this structure kfree()able?	*/
};

void sctp_inq_init(struct sctp_inq *);
void sctp_inq_free(struct sctp_inq *);
void sctp_inq_push(struct sctp_inq *, struct sctp_chunk *packet);
struct sctp_chunk *sctp_inq_pop(struct sctp_inq *);
struct sctp_chunkhdr *sctp_inq_peek(struct sctp_inq *);
void sctp_inq_set_th_handler(struct sctp_inq *, work_func_t);

/* This is the structure we use to hold outbound chunks.  You push
 * chunks in and they automatically pop out the other end as bundled
 * packets (it calls (*output_handler)()).
 *
 * This structure covers sections 6.3, 6.4, 6.7, 6.8, 6.10, 7., 8.1,
 * and 8.2 of the v13 draft.
 *
 * It handles retransmissions.	The connection to the timeout portion
 * of the state machine is through sctp_..._timeout() and timeout_handler.
 *
 * If you feed it SACKs, it will eat them.
 *
 * If you give it big chunks, it will fragment them.
 *
 * It assigns TSN's to data chunks.  This happens at the last possible
 * instant before transmission.
 *
 * When free()'d, it empties itself out via output_handler().
 */
struct sctp_outq {
	struct sctp_association *asoc;

	/* Data pending that has never been transmitted.  */
	struct list_head out_chunk_list;

	unsigned out_qlen;	/* Total length of queued data chunks. */

	/* Error of send failed, may used in SCTP_SEND_FAILED event. */
	unsigned error;

	/* These are control chunks we want to send.  */
	struct list_head control_chunk_list;

	/* These are chunks that have been sacked but are above the
	 * CTSN, or cumulative tsn ack point.
	 */
	struct list_head sacked;

	/* Put chunks on this list to schedule them for
	 * retransmission.
	 */
	struct list_head retransmit;

	/* Put chunks on this list to save them for FWD TSN processing as
	 * they were abandoned.
	 */
	struct list_head abandoned;

	/* How many unackd bytes do we have in-flight?	*/
	__u32 outstanding_bytes;

	/* Are we doing fast-rtx on this queue */
	char fast_rtx;

	/* Corked? */
	char cork;

	/* Is this structure empty?  */
	char empty;

	/* Are we kfree()able? */
	char malloced;
};

void sctp_outq_init(struct sctp_association *, struct sctp_outq *);
void sctp_outq_teardown(struct sctp_outq *);
void sctp_outq_free(struct sctp_outq*);
int sctp_outq_tail(struct sctp_outq *, struct sctp_chunk *chunk);
int sctp_outq_sack(struct sctp_outq *, struct sctp_sackhdr *);
int sctp_outq_is_empty(const struct sctp_outq *);
void sctp_outq_restart(struct sctp_outq *);

void sctp_retransmit(struct sctp_outq *, struct sctp_transport *,
		     sctp_retransmit_reason_t);
void sctp_retransmit_mark(struct sctp_outq *, struct sctp_transport *, __u8);
int sctp_outq_uncork(struct sctp_outq *);
/* Uncork and flush an outqueue.  */
static inline void sctp_outq_cork(struct sctp_outq *q)
{
	q->cork = 1;
}

/* These bind address data fields common between endpoints and associations */
struct sctp_bind_addr {

	/* RFC 2960 12.1 Parameters necessary for the SCTP instance
	 *
	 * SCTP Port:	The local SCTP port number the endpoint is
	 *		bound to.
	 */
	__u16 port;

	/* RFC 2960 12.1 Parameters necessary for the SCTP instance
	 *
	 * Address List: The list of IP addresses that this instance
	 *	has bound.  This information is passed to one's
	 *	peer(s) in INIT and INIT ACK chunks.
	 */
	struct list_head address_list;

	int malloced;	     /* Are we kfree()able?  */
};

void sctp_bind_addr_init(struct sctp_bind_addr *, __u16 port);
void sctp_bind_addr_free(struct sctp_bind_addr *);
int sctp_bind_addr_copy(struct sctp_bind_addr *dest,
			const struct sctp_bind_addr *src,
			sctp_scope_t scope, gfp_t gfp,
			int flags);
int sctp_bind_addr_dup(struct sctp_bind_addr *dest,
			const struct sctp_bind_addr *src,
			gfp_t gfp);
int sctp_add_bind_addr(struct sctp_bind_addr *, union sctp_addr *,
		       __u8 addr_state, gfp_t gfp);
int sctp_del_bind_addr(struct sctp_bind_addr *, union sctp_addr *);
int sctp_bind_addr_match(struct sctp_bind_addr *, const union sctp_addr *,
			 struct sctp_sock *);
int sctp_bind_addr_conflict(struct sctp_bind_addr *, const union sctp_addr *,
			 struct sctp_sock *, struct sctp_sock *);
int sctp_bind_addr_state(const struct sctp_bind_addr *bp,
			 const union sctp_addr *addr);
union sctp_addr *sctp_find_unmatch_addr(struct sctp_bind_addr	*bp,
					const union sctp_addr	*addrs,
					int			addrcnt,
					struct sctp_sock	*opt);
union sctp_params sctp_bind_addrs_to_raw(const struct sctp_bind_addr *bp,
					 int *addrs_len,
					 gfp_t gfp);
int sctp_raw_to_bind_addrs(struct sctp_bind_addr *bp, __u8 *raw, int len,
			   __u16 port, gfp_t gfp);

sctp_scope_t sctp_scope(const union sctp_addr *);
int sctp_in_scope(const union sctp_addr *addr, const sctp_scope_t scope);
int sctp_is_any(struct sock *sk, const union sctp_addr *addr);
int sctp_addr_is_valid(const union sctp_addr *addr);
int sctp_is_ep_boundall(struct sock *sk);


/* What type of endpoint?  */
typedef enum {
	SCTP_EP_TYPE_SOCKET,
	SCTP_EP_TYPE_ASSOCIATION,
} sctp_endpoint_type_t;

/*
 * A common base class to bridge the implmentation view of a
 * socket (usually listening) endpoint versus an association's
 * local endpoint.
 * This common structure is useful for several purposes:
 *   1) Common interface for lookup routines.
 *	a) Subfunctions work for either endpoint or association
 *	b) Single interface to lookup allows hiding the lookup lock rather
 *	   than acquiring it externally.
 *   2) Common interface for the inbound chunk handling/state machine.
 *   3) Common object handling routines for reference counting, etc.
 *   4) Disentangle association lookup from endpoint lookup, where we
 *	do not have to find our endpoint to find our association.
 *
 */

struct sctp_ep_common {
	/* Fields to help us manage our entries in the hash tables. */
	struct hlist_node node;
	int hashent;

	/* Runtime type information.  What kind of endpoint is this? */
	sctp_endpoint_type_t type;

	/* Some fields to help us manage this object.
	 *   refcnt   - Reference count access to this object.
	 *   dead     - Do not attempt to use this object.
	 *   malloced - Do we need to kfree this object?
	 */
	atomic_t    refcnt;
	char	    dead;
	char	    malloced;

	/* What socket does this endpoint belong to?  */
	struct sock *sk;

	/* This is where we receive inbound chunks.  */
	struct sctp_inq	  inqueue;

	/* This substructure includes the defining parameters of the
	 * endpoint:
	 * bind_addr.port is our shared port number.
	 * bind_addr.address_list is our set of local IP addresses.
	 */
	struct sctp_bind_addr bind_addr;
};


/* RFC Section 1.4 Key Terms
 *
 * o SCTP endpoint: The logical sender/receiver of SCTP packets. On a
 *   multi-homed host, an SCTP endpoint is represented to its peers as a
 *   combination of a set of eligible destination transport addresses to
 *   which SCTP packets can be sent and a set of eligible source
 *   transport addresses from which SCTP packets can be received.
 *   All transport addresses used by an SCTP endpoint must use the
 *   same port number, but can use multiple IP addresses. A transport
 *   address used by an SCTP endpoint must not be used by another
 *   SCTP endpoint. In other words, a transport address is unique
 *   to an SCTP endpoint.
 *
 * From an implementation perspective, each socket has one of these.
 * A TCP-style socket will have exactly one association on one of
 * these.  An UDP-style socket will have multiple associations hanging
 * off one of these.
 */

struct sctp_endpoint {
	/* Common substructure for endpoint and association. */
	struct sctp_ep_common base;

	/* Associations: A list of current associations and mappings
	 *	      to the data consumers for each association. This
	 *	      may be in the form of a hash table or other
	 *	      implementation dependent structure. The data
	 *	      consumers may be process identification
	 *	      information such as file descriptors, named pipe
	 *	      pointer, or table pointers dependent on how SCTP
	 *	      is implemented.
	 */
	/* This is really a list of struct sctp_association entries. */
	struct list_head asocs;

	/* Secret Key: A secret key used by this endpoint to compute
	 *	      the MAC.	This SHOULD be a cryptographic quality
	 *	      random number with a sufficient length.
	 *	      Discussion in [RFC1750] can be helpful in
	 *	      selection of the key.
	 */
	__u8 secret_key[SCTP_HOW_MANY_SECRETS][SCTP_SECRET_SIZE];
	int current_key;
	int last_key;
	int key_changed_at;

 	/* digest:  This is a digest of the sctp cookie.  This field is
 	 * 	    only used on the receive path when we try to validate
 	 * 	    that the cookie has not been tampered with.  We put
 	 * 	    this here so we pre-allocate this once and can re-use
 	 * 	    on every receive.
 	 */
 	__u8 *digest;
 
	/* sendbuf acct. policy.	*/
	__u32 sndbuf_policy;

	/* rcvbuf acct. policy.	*/
	__u32 rcvbuf_policy;

	/* SCTP AUTH: array of the HMACs that will be allocated
	 * we need this per association so that we don't serialize
	 */
	struct crypto_hash **auth_hmacs;

	/* SCTP-AUTH: hmacs for the endpoint encoded into parameter */
	 struct sctp_hmac_algo_param *auth_hmacs_list;

	/* SCTP-AUTH: chunks to authenticate encoded into parameter */
	struct sctp_chunks_param *auth_chunk_list;

	/* SCTP-AUTH: endpoint shared keys */
	struct list_head endpoint_shared_keys;
	__u16 active_key_id;
};

/* Recover the outter endpoint structure. */
static inline struct sctp_endpoint *sctp_ep(struct sctp_ep_common *base)
{
	struct sctp_endpoint *ep;

	ep = container_of(base, struct sctp_endpoint, base);
	return ep;
}

/* These are function signatures for manipulating endpoints.  */
struct sctp_endpoint *sctp_endpoint_new(struct sock *, gfp_t);
void sctp_endpoint_free(struct sctp_endpoint *);
void sctp_endpoint_put(struct sctp_endpoint *);
void sctp_endpoint_hold(struct sctp_endpoint *);
void sctp_endpoint_add_asoc(struct sctp_endpoint *, struct sctp_association *);
struct sctp_association *sctp_endpoint_lookup_assoc(
	const struct sctp_endpoint *ep,
	const union sctp_addr *paddr,
	struct sctp_transport **);
int sctp_endpoint_is_peeled_off(struct sctp_endpoint *,
				const union sctp_addr *);
struct sctp_endpoint *sctp_endpoint_is_match(struct sctp_endpoint *,
					const union sctp_addr *);
int sctp_has_association(const union sctp_addr *laddr,
			 const union sctp_addr *paddr);

int sctp_verify_init(const struct sctp_association *asoc, sctp_cid_t,
		     sctp_init_chunk_t *peer_init, struct sctp_chunk *chunk,
		     struct sctp_chunk **err_chunk);
int sctp_process_init(struct sctp_association *, struct sctp_chunk *chunk,
		      const union sctp_addr *peer,
		      sctp_init_chunk_t *init, gfp_t gfp);
__u32 sctp_generate_tag(const struct sctp_endpoint *);
__u32 sctp_generate_tsn(const struct sctp_endpoint *);

struct sctp_inithdr_host {
	__u32 init_tag;
	__u32 a_rwnd;
	__u16 num_outbound_streams;
	__u16 num_inbound_streams;
	__u32 initial_tsn;
};

/* RFC2960
 *
 * 12. Recommended Transmission Control Block (TCB) Parameters
 *
 * This section details a recommended set of parameters that should
 * be contained within the TCB for an implementation. This section is
 * for illustrative purposes and should not be deemed as requirements
 * on an implementation or as an exhaustive list of all parameters
 * inside an SCTP TCB. Each implementation may need its own additional
 * parameters for optimization.
 */


/* Here we have information about each individual association. */
struct sctp_association {

	/* A base structure common to endpoint and association.
	 * In this context, it represents the associations's view
	 * of the local endpoint of the association.
	 */
	struct sctp_ep_common base;

	/* Associations on the same socket. */
	struct list_head asocs;

	/* association id. */
	sctp_assoc_t assoc_id;

	/* This is our parent endpoint.	 */
	struct sctp_endpoint *ep;

	/* These are those association elements needed in the cookie.  */
	struct sctp_cookie c;

	/* This is all information about our peer.  */
	struct {
		/* rwnd
		 *
		 * Peer Rwnd   : Current calculated value of the peer's rwnd.
		 */
		__u32 rwnd;

		/* transport_addr_list
		 *
		 * Peer	       : A list of SCTP transport addresses that the
		 * Transport   : peer is bound to. This information is derived
		 * Address     : from the INIT or INIT ACK and is used to
		 * List	       : associate an inbound packet with a given
		 *	       : association. Normally this information is
		 *	       : hashed or keyed for quick lookup and access
		 *	       : of the TCB.
		 *	       : The list is also initialized with the list
		 *	       : of addresses passed with the sctp_connectx()
		 *	       : call.
		 *
		 * It is a list of SCTP_transport's.
		 */
		struct list_head transport_addr_list;

		/* transport_count
		 *
		 * Peer        : A count of the number of peer addresses
		 * Transport   : in the Peer Transport Address List.
		 * Address     :
		 * Count       :
		 */
		__u16 transport_count;

		/* port
		 *   The transport layer port number.
		 */
		__u16 port;

		/* primary_path
		 *
		 * Primary     : This is the current primary destination
		 * Path	       : transport address of the peer endpoint.  It
		 *	       : may also specify a source transport address
		 *	       : on this endpoint.
		 *
		 * All of these paths live on transport_addr_list.
		 *
		 * At the bakeoffs, we discovered that the intent of
		 * primaryPath is that it only changes when the ULP
		 * asks to have it changed.  We add the activePath to
		 * designate the connection we are currently using to
		 * transmit new data and most control chunks.
		 */
		struct sctp_transport *primary_path;

		/* Cache the primary path address here, when we
		 * need a an address for msg_name.
		 */
		union sctp_addr primary_addr;

		/* active_path
		 *   The path that we are currently using to
		 *   transmit new data and most control chunks.
		 */
		struct sctp_transport *active_path;

		/* retran_path
		 *
		 * RFC2960 6.4 Multi-homed SCTP Endpoints
		 * ...
		 * Furthermore, when its peer is multi-homed, an
		 * endpoint SHOULD try to retransmit a chunk to an
		 * active destination transport address that is
		 * different from the last destination address to
		 * which the DATA chunk was sent.
		 */
		struct sctp_transport *retran_path;

		/* Pointer to last transport I have sent on.  */
		struct sctp_transport *last_sent_to;

		/* This is the last transport I have received DATA on.	*/
		struct sctp_transport *last_data_from;

		/*
		 * Mapping  An array of bits or bytes indicating which out of
		 * Array    order TSN's have been received (relative to the
		 *	    Last Rcvd TSN). If no gaps exist, i.e. no out of
		 *	    order packets have been received, this array
		 *	    will be set to all zero. This structure may be
		 *	    in the form of a circular buffer or bit array.
		 *
		 * Last Rcvd   : This is the last TSN received in
		 * TSN	       : sequence. This value is set initially by
		 *	       : taking the peer's Initial TSN, received in
		 *	       : the INIT or INIT ACK chunk, and subtracting
		 *	       : one from it.
		 *
		 * Throughout most of the specification this is called the
		 * "Cumulative TSN ACK Point".	In this case, we
		 * ignore the advice in 12.2 in favour of the term
		 * used in the bulk of the text.  This value is hidden
		 * in tsn_map--we get it by calling sctp_tsnmap_get_ctsn().
		 */
		struct sctp_tsnmap tsn_map;

		/* Ack State   : This flag indicates if the next received
		 *             : packet is to be responded to with a
		 *             : SACK. This is initializedto 0.  When a packet
		 *             : is received it is incremented. If this value
		 *             : reaches 2 or more, a SACK is sent and the
		 *             : value is reset to 0. Note: This is used only
		 *             : when no DATA chunks are received out of
		 *             : order.  When DATA chunks are out of order,
		 *             : SACK's are not delayed (see Section 6).
		 */
		__u8    sack_needed;     /* Do we need to sack the peer? */
		__u32	sack_cnt;

		/* These are capabilities which our peer advertised.  */
		__u8	ecn_capable:1,	    /* Can peer do ECN? */
			ipv4_address:1,	    /* Peer understands IPv4 addresses? */
			ipv6_address:1,	    /* Peer understands IPv6 addresses? */
			hostname_address:1, /* Peer understands DNS addresses? */
			asconf_capable:1,   /* Does peer support ADDIP? */
			prsctp_capable:1,   /* Can peer do PR-SCTP? */
			auth_capable:1;	    /* Is peer doing SCTP-AUTH? */

		__u32   adaptation_ind;	 /* Adaptation Code point. */

		/* This mask is used to disable sending the ASCONF chunk
		 * with specified parameter to peer.
		 */
		__be16 addip_disabled_mask;

		struct sctp_inithdr_host i;
		int cookie_len;
		void *cookie;

		/* ADDIP Section 4.2 Upon reception of an ASCONF Chunk.
		 * C1) ... "Peer-Serial-Number'. This value MUST be initialized to the
		 * Initial TSN Value minus 1
		 */
		__u32 addip_serial;

		/* SCTP-AUTH: We need to know pears random number, hmac list
		 * and authenticated chunk list.  All that is part of the
		 * cookie and these are just pointers to those locations
		 */
		sctp_random_param_t *peer_random;
		sctp_chunks_param_t *peer_chunks;
		sctp_hmac_algo_param_t *peer_hmacs;
	} peer;

	/* State       : A state variable indicating what state the
	 *	       : association is in, i.e. COOKIE-WAIT,
	 *	       : COOKIE-ECHOED, ESTABLISHED, SHUTDOWN-PENDING,
	 *	       : SHUTDOWN-SENT, SHUTDOWN-RECEIVED, SHUTDOWN-ACK-SENT.
	 *
	 *		Note: No "CLOSED" state is illustrated since if a
	 *		association is "CLOSED" its TCB SHOULD be removed.
	 *
	 *		In this implementation we DO have a CLOSED
	 *		state which is used during initiation and shutdown.
	 *
	 *		State takes values from SCTP_STATE_*.
	 */
	sctp_state_t state;

	/* The cookie life I award for any cookie.  */
	struct timeval cookie_life;

	/* Overall     : The overall association error count.
	 * Error Count : [Clear this any time I get something.]
	 */
	int overall_error_count;

	/* These are the association's initial, max, and min RTO values.
	 * These values will be initialized by system defaults, but can
	 * be modified via the SCTP_RTOINFO socket option.
	 */
	unsigned long rto_initial;
	unsigned long rto_max;
	unsigned long rto_min;

	/* Maximum number of new data packets that can be sent in a burst.  */
	int max_burst;

	/* This is the max_retrans value for the association.  This value will
	 * be initialized initialized from system defaults, but can be
	 * modified by the SCTP_ASSOCINFO socket option.
	 */
	int max_retrans;

	/* Maximum number of times the endpoint will retransmit INIT  */
	__u16 max_init_attempts;

	/* How many times have we resent an INIT? */
	__u16 init_retries;

	/* The largest timeout or RTO value to use in attempting an INIT */
	unsigned long max_init_timeo;

	/* Heartbeat interval: The endpoint sends out a Heartbeat chunk to
	 * the destination address every heartbeat interval. This value
	 * will be inherited by all new transports.
	 */
	unsigned long hbinterval;

	/* This is the max_retrans value for new transports in the
	 * association.
	 */
	__u16 pathmaxrxt;

	/* Flag that path mtu update is pending */
	__u8   pmtu_pending;

	/* Association : The smallest PMTU discovered for all of the
	 * PMTU	       : peer's transport addresses.
	 */
	__u32 pathmtu;

	/* Flags controlling Heartbeat, SACK delay, and Path MTU Discovery. */
	__u32 param_flags;

	/* SACK delay timeout */
	unsigned long sackdelay;
	__u32 sackfreq;


	unsigned long timeouts[SCTP_NUM_TIMEOUT_TYPES];
	struct timer_list timers[SCTP_NUM_TIMEOUT_TYPES];

	/* Transport to which SHUTDOWN chunk was last sent.  */
	struct sctp_transport *shutdown_last_sent_to;

	/* How many times have we resent a SHUTDOWN */
	int shutdown_retries;

	/* Transport to which INIT chunk was last sent.  */
	struct sctp_transport *init_last_sent_to;

	/* Next TSN    : The next TSN number to be assigned to a new
	 *	       : DATA chunk.  This is sent in the INIT or INIT
	 *	       : ACK chunk to the peer and incremented each
	 *	       : time a DATA chunk is assigned a TSN
	 *	       : (normally just prior to transmit or during
	 *	       : fragmentation).
	 */
	__u32 next_tsn;

	/*
	 * Last Rcvd   : This is the last TSN received in sequence.  This value
	 * TSN	       : is set initially by taking the peer's Initial TSN,
	 *	       : received in the INIT or INIT ACK chunk, and
	 *	       : subtracting one from it.
	 *
	 * Most of RFC 2960 refers to this as the Cumulative TSN Ack Point.
	 */

	__u32 ctsn_ack_point;

	/* PR-SCTP Advanced.Peer.Ack.Point */
	__u32 adv_peer_ack_point;

	/* Highest TSN that is acknowledged by incoming SACKs. */
	__u32 highest_sacked;

	/* TSN marking the fast recovery exit point */
	__u32 fast_recovery_exit;

	/* Flag to track the current fast recovery state */
	__u8 fast_recovery;

	/* The number of unacknowledged data chunks.  Reported through
	 * the SCTP_STATUS sockopt.
	 */
	__u16 unack_data;

	/* The total number of data chunks that we've had to retransmit
	 * as the result of a T3 timer expiration
	 */
	__u32 rtx_data_chunks;

	/* This is the association's receive buffer space.  This value is used
	 * to set a_rwnd field in an INIT or a SACK chunk.
	 */
	__u32 rwnd;

	/* This is the last advertised value of rwnd over a SACK chunk. */
	__u32 a_rwnd;

	/* Number of bytes by which the rwnd has slopped.  The rwnd is allowed
	 * to slop over a maximum of the association's frag_point.
	 */
	__u32 rwnd_over;

	/* Keeps treack of rwnd pressure.  This happens when we have
	 * a window, but not recevie buffer (i.e small packets).  This one
	 * is releases slowly (1 PMTU at a time ).
	 */
	__u32 rwnd_press;

	/* This is the sndbuf size in use for the association.
	 * This corresponds to the sndbuf size for the association,
	 * as specified in the sk->sndbuf.
	 */
	int sndbuf_used;

	/* This is the amount of memory that this association has allocated
	 * in the receive path at any given time.
	 */
	atomic_t rmem_alloc;

	/* This is the wait queue head for send requests waiting on
	 * the association sndbuf space.
	 */
	wait_queue_head_t	wait;

	/* The message size at which SCTP fragmentation will occur. */
	__u32 frag_point;
	__u32 user_frag;

	/* Counter used to count INIT errors. */
	int init_err_counter;

	/* Count the number of INIT cycles (for doubling timeout). */
	int init_cycle;

	/* Default send parameters. */
	__u16 default_stream;
	__u16 default_flags;
	__u32 default_ppid;
	__u32 default_context;
	__u32 default_timetolive;

	/* Default receive parameters */
	__u32 default_rcv_context;

	/* This tracks outbound ssn for a given stream.	 */
	struct sctp_ssnmap *ssnmap;

	/* All outbound chunks go through this structure.  */
	struct sctp_outq outqueue;

	/* A smart pipe that will handle reordering and fragmentation,
	 * as well as handle passing events up to the ULP.
	 */
	struct sctp_ulpq ulpq;

	/* Last TSN that caused an ECNE Chunk to be sent.  */
	__u32 last_ecne_tsn;

	/* Last TSN that caused a CWR Chunk to be sent.	 */
	__u32 last_cwr_tsn;

	/* How many duplicated TSNs have we seen?  */
	int numduptsns;

	/* Number of seconds of idle time before an association is closed.
	 * In the association context, this is really used as a boolean
	 * since the real timeout is stored in the timeouts array
	 */
	__u32 autoclose;

	/* These are to support
	 * "SCTP Extensions for Dynamic Reconfiguration of IP Addresses
	 *  and Enforcement of Flow and Message Limits"
	 * <draft-ietf-tsvwg-addip-sctp-02.txt>
	 * or "ADDIP" for short.
	 */



	/* ADDIP Section 4.1.1 Congestion Control of ASCONF Chunks
	 *
	 * R1) One and only one ASCONF Chunk MAY be in transit and
	 * unacknowledged at any one time.  If a sender, after sending
	 * an ASCONF chunk, decides it needs to transfer another
	 * ASCONF Chunk, it MUST wait until the ASCONF-ACK Chunk
	 * returns from the previous ASCONF Chunk before sending a
	 * subsequent ASCONF. Note this restriction binds each side,
	 * so at any time two ASCONF may be in-transit on any given
	 * association (one sent from each endpoint).
	 *
	 * [This is our one-and-only-one ASCONF in flight.  If we do
	 * not have an ASCONF in flight, this is NULL.]
	 */
	struct sctp_chunk *addip_last_asconf;

	/* ADDIP Section 5.2 Upon reception of an ASCONF Chunk.
	 *
	 * This is needed to implement itmes E1 - E4 of the updated
	 * spec.  Here is the justification:
	 *
	 * Since the peer may bundle multiple ASCONF chunks toward us,
	 * we now need the ability to cache multiple ACKs.  The section
	 * describes in detail how they are cached and cleaned up.
	 */
	struct list_head asconf_ack_list;

	/* These ASCONF chunks are waiting to be sent.
	 *
	 * These chunaks can't be pushed to outqueue until receiving
	 * ASCONF_ACK for the previous ASCONF indicated by
	 * addip_last_asconf, so as to guarantee that only one ASCONF
	 * is in flight at any time.
	 *
	 * ADDIP Section 4.1.1 Congestion Control of ASCONF Chunks
	 *
	 * In defining the ASCONF Chunk transfer procedures, it is
	 * essential that these transfers MUST NOT cause congestion
	 * within the network.	To achieve this, we place these
	 * restrictions on the transfer of ASCONF Chunks:
	 *
	 * R1) One and only one ASCONF Chunk MAY be in transit and
	 * unacknowledged at any one time.  If a sender, after sending
	 * an ASCONF chunk, decides it needs to transfer another
	 * ASCONF Chunk, it MUST wait until the ASCONF-ACK Chunk
	 * returns from the previous ASCONF Chunk before sending a
	 * subsequent ASCONF. Note this restriction binds each side,
	 * so at any time two ASCONF may be in-transit on any given
	 * association (one sent from each endpoint).
	 *
	 *
	 * [I really think this is EXACTLY the sort of intelligence
	 *  which already resides in sctp_outq.	 Please move this
	 *  queue and its supporting logic down there.	--piggy]
	 */
	struct list_head addip_chunk_list;

	/* ADDIP Section 4.1 ASCONF Chunk Procedures
	 *
	 * A2) A serial number should be assigned to the Chunk. The
	 * serial number SHOULD be a monotonically increasing
	 * number. The serial number SHOULD be initialized at
	 * the start of the association to the same value as the
	 * Initial TSN and every time a new ASCONF chunk is created
	 * it is incremented by one after assigning the serial number
	 * to the newly created chunk.
	 *
	 * ADDIP
	 * 3.1.1  Address/Stream Configuration Change Chunk (ASCONF)
	 *
	 * Serial Number : 32 bits (unsigned integer)
	 *
	 * This value represents a Serial Number for the ASCONF
	 * Chunk. The valid range of Serial Number is from 0 to
	 * 4294967295 (2^32 - 1).  Serial Numbers wrap back to 0
	 * after reaching 4294967295.
	 */
	__u32 addip_serial;
	union sctp_addr *asconf_addr_del_pending;
	int src_out_of_asoc_ok;
	struct sctp_transport *new_transport;

	/* SCTP AUTH: list of the endpoint shared keys.  These
	 * keys are provided out of band by the user applicaton
	 * and can't change during the lifetime of the association
	 */
	struct list_head endpoint_shared_keys;

	/* SCTP AUTH:
	 * The current generated assocaition shared key (secret)
	 */
	struct sctp_auth_bytes *asoc_shared_key;

	/* SCTP AUTH: hmac id of the first peer requested algorithm
	 * that we support.
	 */
	__u16 default_hmac_id;

	__u16 active_key_id;

	__u8 need_ecne:1,	/* Need to send an ECNE Chunk? */
	     temp:1;		/* Is it a temporary association? */
};


/* An eyecatcher for determining if we are really looking at an
 * association data structure.
 */
enum {
	SCTP_ASSOC_EYECATCHER = 0xa550c123,
};

/* Recover the outter association structure. */
static inline struct sctp_association *sctp_assoc(struct sctp_ep_common *base)
{
	struct sctp_association *asoc;

	asoc = container_of(base, struct sctp_association, base);
	return asoc;
}

/* These are function signatures for manipulating associations.	 */


struct sctp_association *
sctp_association_new(const struct sctp_endpoint *, const struct sock *,
		     sctp_scope_t scope, gfp_t gfp);
void sctp_association_free(struct sctp_association *);
void sctp_association_put(struct sctp_association *);
void sctp_association_hold(struct sctp_association *);

struct sctp_transport *sctp_assoc_choose_alter_transport(
	struct sctp_association *, struct sctp_transport *);
void sctp_assoc_update_retran_path(struct sctp_association *);
struct sctp_transport *sctp_assoc_lookup_paddr(const struct sctp_association *,
					  const union sctp_addr *);
int sctp_assoc_lookup_laddr(struct sctp_association *asoc,
			    const union sctp_addr *laddr);
struct sctp_transport *sctp_assoc_add_peer(struct sctp_association *,
				     const union sctp_addr *address,
				     const gfp_t gfp,
				     const int peer_state);
void sctp_assoc_del_peer(struct sctp_association *asoc,
			 const union sctp_addr *addr);
void sctp_assoc_rm_peer(struct sctp_association *asoc,
			 struct sctp_transport *peer);
void sctp_assoc_control_transport(struct sctp_association *,
				  struct sctp_transport *,
				  sctp_transport_cmd_t, sctp_sn_error_t);
struct sctp_transport *sctp_assoc_lookup_tsn(struct sctp_association *, __u32);
struct sctp_transport *sctp_assoc_is_match(struct sctp_association *,
					   const union sctp_addr *,
					   const union sctp_addr *);
void sctp_assoc_migrate(struct sctp_association *, struct sock *);
void sctp_assoc_update(struct sctp_association *old,
		       struct sctp_association *new);

__u32 sctp_association_get_next_tsn(struct sctp_association *);

void sctp_assoc_sync_pmtu(struct sctp_association *);
void sctp_assoc_rwnd_increase(struct sctp_association *, unsigned);
void sctp_assoc_rwnd_decrease(struct sctp_association *, unsigned);
void sctp_assoc_set_primary(struct sctp_association *,
			    struct sctp_transport *);
void sctp_assoc_del_nonprimary_peers(struct sctp_association *,
				    struct sctp_transport *);
int sctp_assoc_set_bind_addr_from_ep(struct sctp_association *,
				     sctp_scope_t, gfp_t);
int sctp_assoc_set_bind_addr_from_cookie(struct sctp_association *,
					 struct sctp_cookie*,
					 gfp_t gfp);
int sctp_assoc_set_id(struct sctp_association *, gfp_t);
void sctp_assoc_clean_asconf_ack_cache(const struct sctp_association *asoc);
struct sctp_chunk *sctp_assoc_lookup_asconf_ack(
					const struct sctp_association *asoc,
					__be32 serial);
void sctp_asconf_queue_teardown(struct sctp_association *asoc);

int sctp_cmp_addr_exact(const union sctp_addr *ss1,
			const union sctp_addr *ss2);
struct sctp_chunk *sctp_get_ecne_prepend(struct sctp_association *asoc);

/* A convenience structure to parse out SCTP specific CMSGs. */
typedef struct sctp_cmsgs {
	struct sctp_initmsg *init;
	struct sctp_sndrcvinfo *info;
} sctp_cmsgs_t;

/* Structure for tracking memory objects */
typedef struct {
	char *label;
	atomic_t *counter;
} sctp_dbg_objcnt_entry_t;

#endif /* __sctp_structs_h__ */
