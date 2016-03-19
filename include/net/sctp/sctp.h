/* SCTP kernel implementation
 * (C) Copyright IBM Corp. 2001, 2004
 * Copyright (c) 1999-2000 Cisco, Inc.
 * Copyright (c) 1999-2001 Motorola, Inc.
 * Copyright (c) 2001-2003 Intel Corp.
 *
 * This file is part of the SCTP kernel implementation
 *
 * The base lksctp header.
 *
 * This SCTP implementation is free software;
 * you can redistribute it and/or modify it under the terms of
 * the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 *
 * This SCTP implementation is distributed in the hope that it
 * will be useful, but WITHOUT ANY WARRANTY; without even the implied
 *                 ************************
 * warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with GNU CC; see the file COPYING.  If not, see
 * <http://www.gnu.org/licenses/>.
 *
 * Please send any bug reports or fixes you make to the
 * email address(es):
 *    lksctp developers <linux-sctp@vger.kernel.org>
 *
 * Written or modified by:
 *    La Monte H.P. Yarroll <piggy@acm.org>
 *    Xingang Guo           <xingang.guo@intel.com>
 *    Jon Grimm             <jgrimm@us.ibm.com>
 *    Daisy Chang           <daisyc@us.ibm.com>
 *    Sridhar Samudrala     <sri@us.ibm.com>
 *    Ardelle Fan           <ardelle.fan@intel.com>
 *    Ryan Layer            <rmlayer@us.ibm.com>
 *    Kevin Gao             <kevin.gao@intel.com> 
 */

#ifndef __net_sctp_h__
#define __net_sctp_h__

/* Header Strategy.
 *    Start getting some control over the header file depencies:
 *       includes
 *       constants
 *       structs
 *       prototypes
 *       macros, externs, and inlines
 *
 *   Move test_frame specific items out of the kernel headers
 *   and into the test frame headers.   This is not perfect in any sense
 *   and will continue to evolve.
 */

#include <linux/types.h>
#include <linux/slab.h>
#include <linux/in.h>
#include <linux/tty.h>
#include <linux/proc_fs.h>
#include <linux/spinlock.h>
#include <linux/jiffies.h>
#include <linux/idr.h>

#if IS_ENABLED(CONFIG_IPV6)
#include <net/ipv6.h>
#include <net/ip6_route.h>
#endif

#include <asm/uaccess.h>
#include <asm/page.h>
#include <net/sock.h>
#include <net/snmp.h>
#include <net/sctp/structs.h>
#include <net/sctp/constants.h>

#ifdef CONFIG_IP_SCTP_MODULE
#define SCTP_PROTOSW_FLAG 0
#else /* static! */
#define SCTP_PROTOSW_FLAG INET_PROTOSW_PERMANENT
#endif

/* Round an int up to the next multiple of 4.  */
#define WORD_ROUND(s) (((s)+3)&~3)
/* Truncate to the previous multiple of 4.  */
#define WORD_TRUNC(s) ((s)&~3)

/*
 * Function declarations.
 */

/*
 * sctp/protocol.c
 */
int sctp_copy_local_addr_list(struct net *, struct sctp_bind_addr *,
			      sctp_scope_t, gfp_t gfp, int flags);
struct sctp_pf *sctp_get_pf_specific(sa_family_t family);
int sctp_register_pf(struct sctp_pf *, sa_family_t);
void sctp_addr_wq_mgmt(struct net *, struct sctp_sockaddr_entry *, int);

/*
 * sctp/socket.c
 */
int sctp_backlog_rcv(struct sock *sk, struct sk_buff *skb);
int sctp_inet_listen(struct socket *sock, int backlog);
void sctp_write_space(struct sock *sk);
void sctp_data_ready(struct sock *sk);
unsigned int sctp_poll(struct file *file, struct socket *sock,
		poll_table *wait);
void sctp_sock_rfree(struct sk_buff *skb);
void sctp_copy_sock(struct sock *newsk, struct sock *sk,
		    struct sctp_association *asoc);
extern struct percpu_counter sctp_sockets_allocated;
int sctp_asconf_mgmt(struct sctp_sock *, struct sctp_sockaddr_entry *);
struct sk_buff *sctp_skb_recv_datagram(struct sock *, int, int, int *);

/*
 * sctp/primitive.c
 */
int sctp_primitive_ASSOCIATE(struct net *, struct sctp_association *, void *arg);
int sctp_primitive_SHUTDOWN(struct net *, struct sctp_association *, void *arg);
int sctp_primitive_ABORT(struct net *, struct sctp_association *, void *arg);
int sctp_primitive_SEND(struct net *, struct sctp_association *, void *arg);
int sctp_primitive_REQUESTHEARTBEAT(struct net *, struct sctp_association *, void *arg);
int sctp_primitive_ASCONF(struct net *, struct sctp_association *, void *arg);

/*
 * sctp/input.c
 */
int sctp_rcv(struct sk_buff *skb);
void sctp_v4_err(struct sk_buff *skb, u32 info);
void sctp_hash_endpoint(struct sctp_endpoint *);
void sctp_unhash_endpoint(struct sctp_endpoint *);
struct sock *sctp_err_lookup(struct net *net, int family, struct sk_buff *,
			     struct sctphdr *, struct sctp_association **,
			     struct sctp_transport **);
void sctp_err_finish(struct sock *, struct sctp_association *);
void sctp_icmp_frag_needed(struct sock *, struct sctp_association *,
			   struct sctp_transport *t, __u32 pmtu);
void sctp_icmp_redirect(struct sock *, struct sctp_transport *,
			struct sk_buff *);
void sctp_icmp_proto_unreachable(struct sock *sk,
				 struct sctp_association *asoc,
				 struct sctp_transport *t);
void sctp_backlog_migrate(struct sctp_association *assoc,
			  struct sock *oldsk, struct sock *newsk);
int sctp_transport_hashtable_init(void);
void sctp_transport_hashtable_destroy(void);
void sctp_hash_transport(struct sctp_transport *t);
void sctp_unhash_transport(struct sctp_transport *t);
struct sctp_transport *sctp_addrs_lookup_transport(
				struct net *net,
				const union sctp_addr *laddr,
				const union sctp_addr *paddr);
struct sctp_transport *sctp_epaddr_lookup_transport(
				const struct sctp_endpoint *ep,
				const union sctp_addr *paddr);

/*
 * sctp/proc.c
 */
int sctp_snmp_proc_init(struct net *net);
void sctp_snmp_proc_exit(struct net *net);
int sctp_eps_proc_init(struct net *net);
void sctp_eps_proc_exit(struct net *net);
int sctp_assocs_proc_init(struct net *net);
void sctp_assocs_proc_exit(struct net *net);
int sctp_remaddr_proc_init(struct net *net);
void sctp_remaddr_proc_exit(struct net *net);


/*
 * Module global variables
 */

 /*
  * sctp/protocol.c
  */
extern struct kmem_cache *sctp_chunk_cachep __read_mostly;
extern struct kmem_cache *sctp_bucket_cachep __read_mostly;
extern long sysctl_sctp_mem[3];
extern int sysctl_sctp_rmem[3];
extern int sysctl_sctp_wmem[3];

/*
 *  Section:  Macros, externs, and inlines
 */

/* SCTP SNMP MIB stats handlers */
#define SCTP_INC_STATS(net, field)      SNMP_INC_STATS((net)->sctp.sctp_statistics, field)
#define SCTP_INC_STATS_BH(net, field)   SNMP_INC_STATS_BH((net)->sctp.sctp_statistics, field)
#define SCTP_INC_STATS_USER(net, field) SNMP_INC_STATS_USER((net)->sctp.sctp_statistics, field)
#define SCTP_DEC_STATS(net, field)      SNMP_DEC_STATS((net)->sctp.sctp_statistics, field)

/* sctp mib definitions */
enum {
	SCTP_MIB_NUM = 0,
	SCTP_MIB_CURRESTAB,			/* CurrEstab */
	SCTP_MIB_ACTIVEESTABS,			/* ActiveEstabs */
	SCTP_MIB_PASSIVEESTABS,			/* PassiveEstabs */
	SCTP_MIB_ABORTEDS,			/* Aborteds */
	SCTP_MIB_SHUTDOWNS,			/* Shutdowns */
	SCTP_MIB_OUTOFBLUES,			/* OutOfBlues */
	SCTP_MIB_CHECKSUMERRORS,		/* ChecksumErrors */
	SCTP_MIB_OUTCTRLCHUNKS,			/* OutCtrlChunks */
	SCTP_MIB_OUTORDERCHUNKS,		/* OutOrderChunks */
	SCTP_MIB_OUTUNORDERCHUNKS,		/* OutUnorderChunks */
	SCTP_MIB_INCTRLCHUNKS,			/* InCtrlChunks */
	SCTP_MIB_INORDERCHUNKS,			/* InOrderChunks */
	SCTP_MIB_INUNORDERCHUNKS,		/* InUnorderChunks */
	SCTP_MIB_FRAGUSRMSGS,			/* FragUsrMsgs */
	SCTP_MIB_REASMUSRMSGS,			/* ReasmUsrMsgs */
	SCTP_MIB_OUTSCTPPACKS,			/* OutSCTPPacks */
	SCTP_MIB_INSCTPPACKS,			/* InSCTPPacks */
	SCTP_MIB_T1_INIT_EXPIREDS,
	SCTP_MIB_T1_COOKIE_EXPIREDS,
	SCTP_MIB_T2_SHUTDOWN_EXPIREDS,
	SCTP_MIB_T3_RTX_EXPIREDS,
	SCTP_MIB_T4_RTO_EXPIREDS,
	SCTP_MIB_T5_SHUTDOWN_GUARD_EXPIREDS,
	SCTP_MIB_DELAY_SACK_EXPIREDS,
	SCTP_MIB_AUTOCLOSE_EXPIREDS,
	SCTP_MIB_T1_RETRANSMITS,
	SCTP_MIB_T3_RETRANSMITS,
	SCTP_MIB_PMTUD_RETRANSMITS,
	SCTP_MIB_FAST_RETRANSMITS,
	SCTP_MIB_IN_PKT_SOFTIRQ,
	SCTP_MIB_IN_PKT_BACKLOG,
	SCTP_MIB_IN_PKT_DISCARDS,
	SCTP_MIB_IN_DATA_CHUNK_DISCARDS,
	__SCTP_MIB_MAX
};

#define SCTP_MIB_MAX    __SCTP_MIB_MAX
struct sctp_mib {
        unsigned long   mibs[SCTP_MIB_MAX];
};

/* helper function to track stats about max rto and related transport */
static inline void sctp_max_rto(struct sctp_association *asoc,
				struct sctp_transport *trans)
{
	if (asoc->stats.max_obs_rto < (__u64)trans->rto) {
		asoc->stats.max_obs_rto = trans->rto;
		memset(&asoc->stats.obs_rto_ipaddr, 0,
			sizeof(struct sockaddr_storage));
		memcpy(&asoc->stats.obs_rto_ipaddr, &trans->ipaddr,
			trans->af_specific->sockaddr_len);
	}
}

/*
 * Macros for keeping a global reference of object allocations.
 */
#ifdef CONFIG_SCTP_DBG_OBJCNT

extern atomic_t sctp_dbg_objcnt_sock;
extern atomic_t sctp_dbg_objcnt_ep;
extern atomic_t sctp_dbg_objcnt_assoc;
extern atomic_t sctp_dbg_objcnt_transport;
extern atomic_t sctp_dbg_objcnt_chunk;
extern atomic_t sctp_dbg_objcnt_bind_addr;
extern atomic_t sctp_dbg_objcnt_bind_bucket;
extern atomic_t sctp_dbg_objcnt_addr;
extern atomic_t sctp_dbg_objcnt_ssnmap;
extern atomic_t sctp_dbg_objcnt_datamsg;
extern atomic_t sctp_dbg_objcnt_keys;

/* Macros to atomically increment/decrement objcnt counters.  */
#define SCTP_DBG_OBJCNT_INC(name) \
atomic_inc(&sctp_dbg_objcnt_## name)
#define SCTP_DBG_OBJCNT_DEC(name) \
atomic_dec(&sctp_dbg_objcnt_## name)
#define SCTP_DBG_OBJCNT(name) \
atomic_t sctp_dbg_objcnt_## name = ATOMIC_INIT(0)

/* Macro to help create new entries in in the global array of
 * objcnt counters.
 */
#define SCTP_DBG_OBJCNT_ENTRY(name) \
{.label= #name, .counter= &sctp_dbg_objcnt_## name}

void sctp_dbg_objcnt_init(struct net *);
void sctp_dbg_objcnt_exit(struct net *);

#else

#define SCTP_DBG_OBJCNT_INC(name)
#define SCTP_DBG_OBJCNT_DEC(name)

static inline void sctp_dbg_objcnt_init(struct net *net) { return; }
static inline void sctp_dbg_objcnt_exit(struct net *net) { return; }

#endif /* CONFIG_SCTP_DBG_OBJCOUNT */

#if defined CONFIG_SYSCTL
void sctp_sysctl_register(void);
void sctp_sysctl_unregister(void);
int sctp_sysctl_net_register(struct net *net);
void sctp_sysctl_net_unregister(struct net *net);
#else
static inline void sctp_sysctl_register(void) { return; }
static inline void sctp_sysctl_unregister(void) { return; }
static inline int sctp_sysctl_net_register(struct net *net) { return 0; }
static inline void sctp_sysctl_net_unregister(struct net *net) { return; }
#endif

/* Size of Supported Address Parameter for 'x' address types. */
#define SCTP_SAT_LEN(x) (sizeof(struct sctp_paramhdr) + (x) * sizeof(__u16))

#if IS_ENABLED(CONFIG_IPV6)

void sctp_v6_pf_init(void);
void sctp_v6_pf_exit(void);
int sctp_v6_protosw_init(void);
void sctp_v6_protosw_exit(void);
int sctp_v6_add_protocol(void);
void sctp_v6_del_protocol(void);

#else /* #ifdef defined(CONFIG_IPV6) */

static inline void sctp_v6_pf_init(void) { return; }
static inline void sctp_v6_pf_exit(void) { return; }
static inline int sctp_v6_protosw_init(void) { return 0; }
static inline void sctp_v6_protosw_exit(void) { return; }
static inline int sctp_v6_add_protocol(void) { return 0; }
static inline void sctp_v6_del_protocol(void) { return; }

#endif /* #if defined(CONFIG_IPV6) */


/* Map an association to an assoc_id. */
static inline sctp_assoc_t sctp_assoc2id(const struct sctp_association *asoc)
{
	return asoc ? asoc->assoc_id : 0;
}

static inline enum sctp_sstat_state
sctp_assoc_to_state(const struct sctp_association *asoc)
{
	/* SCTP's uapi always had SCTP_EMPTY(=0) as a dummy state, but we
	 * got rid of it in kernel space. Therefore SCTP_CLOSED et al
	 * start at =1 in user space, but actually as =0 in kernel space.
	 * Now that we can not break user space and SCTP_EMPTY is exposed
	 * there, we need to fix it up with an ugly offset not to break
	 * applications. :(
	 */
	return asoc->state + 1;
}

/* Look up the association by its id.  */
struct sctp_association *sctp_id2assoc(struct sock *sk, sctp_assoc_t id);

int sctp_do_peeloff(struct sock *sk, sctp_assoc_t id, struct socket **sockp);

/* A macro to walk a list of skbs.  */
#define sctp_skb_for_each(pos, head, tmp) \
	skb_queue_walk_safe(head, pos, tmp)

/* A helper to append an entire skb list (list) to another (head). */
static inline void sctp_skb_list_tail(struct sk_buff_head *list,
				      struct sk_buff_head *head)
{
	unsigned long flags;

	spin_lock_irqsave(&head->lock, flags);
	spin_lock(&list->lock);

	skb_queue_splice_tail_init(list, head);

	spin_unlock(&list->lock);
	spin_unlock_irqrestore(&head->lock, flags);
}

/**
 *	sctp_list_dequeue - remove from the head of the queue
 *	@list: list to dequeue from
 *
 *	Remove the head of the list. The head item is
 *	returned or %NULL if the list is empty.
 */

static inline struct list_head *sctp_list_dequeue(struct list_head *list)
{
	struct list_head *result = NULL;

	if (list->next != list) {
		result = list->next;
		list->next = result->next;
		list->next->prev = list;
		INIT_LIST_HEAD(result);
	}
	return result;
}

/* SCTP version of skb_set_owner_r.  We need this one because
 * of the way we have to do receive buffer accounting on bundled
 * chunks.
 */
static inline void sctp_skb_set_owner_r(struct sk_buff *skb, struct sock *sk)
{
	struct sctp_ulpevent *event = sctp_skb2event(skb);

	skb_orphan(skb);
	skb->sk = sk;
	skb->destructor = sctp_sock_rfree;
	atomic_add(event->rmem_len, &sk->sk_rmem_alloc);
	/*
	 * This mimics the behavior of skb_set_owner_r
	 */
	sk->sk_forward_alloc -= event->rmem_len;
}

/* Tests if the list has one and only one entry. */
static inline int sctp_list_single_entry(struct list_head *head)
{
	return (head->next != head) && (head->next == head->prev);
}

/* Break down data chunks at this point.  */
static inline int sctp_frag_point(const struct sctp_association *asoc, int pmtu)
{
	struct sctp_sock *sp = sctp_sk(asoc->base.sk);
	int frag = pmtu;

	frag -= sp->pf->af->net_header_len;
	frag -= sizeof(struct sctphdr) + sizeof(struct sctp_data_chunk);

	if (asoc->user_frag)
		frag = min_t(int, frag, asoc->user_frag);

	frag = WORD_TRUNC(min_t(int, frag, SCTP_MAX_CHUNK_LEN));

	return frag;
}

static inline void sctp_assoc_pending_pmtu(struct sock *sk, struct sctp_association *asoc)
{

	sctp_assoc_sync_pmtu(sk, asoc);
	asoc->pmtu_pending = 0;
}

static inline bool sctp_chunk_pending(const struct sctp_chunk *chunk)
{
	return !list_empty(&chunk->list);
}

/* Walk through a list of TLV parameters.  Don't trust the
 * individual parameter lengths and instead depend on
 * the chunk length to indicate when to stop.  Make sure
 * there is room for a param header too.
 */
#define sctp_walk_params(pos, chunk, member)\
_sctp_walk_params((pos), (chunk), ntohs((chunk)->chunk_hdr.length), member)

#define _sctp_walk_params(pos, chunk, end, member)\
for (pos.v = chunk->member;\
     pos.v <= (void *)chunk + end - ntohs(pos.p->length) &&\
     ntohs(pos.p->length) >= sizeof(sctp_paramhdr_t);\
     pos.v += WORD_ROUND(ntohs(pos.p->length)))

#define sctp_walk_errors(err, chunk_hdr)\
_sctp_walk_errors((err), (chunk_hdr), ntohs((chunk_hdr)->length))

#define _sctp_walk_errors(err, chunk_hdr, end)\
for (err = (sctp_errhdr_t *)((void *)chunk_hdr + \
	    sizeof(sctp_chunkhdr_t));\
     (void *)err <= (void *)chunk_hdr + end - ntohs(err->length) &&\
     ntohs(err->length) >= sizeof(sctp_errhdr_t); \
     err = (sctp_errhdr_t *)((void *)err + WORD_ROUND(ntohs(err->length))))

#define sctp_walk_fwdtsn(pos, chunk)\
_sctp_walk_fwdtsn((pos), (chunk), ntohs((chunk)->chunk_hdr->length) - sizeof(struct sctp_fwdtsn_chunk))

#define _sctp_walk_fwdtsn(pos, chunk, end)\
for (pos = chunk->subh.fwdtsn_hdr->skip;\
     (void *)pos <= (void *)chunk->subh.fwdtsn_hdr->skip + end - sizeof(struct sctp_fwdtsn_skip);\
     pos++)

/* External references. */

extern struct proto sctp_prot;
extern struct proto sctpv6_prot;
void sctp_put_port(struct sock *sk);

extern struct idr sctp_assocs_id;
extern spinlock_t sctp_assocs_id_lock;

/* Static inline functions. */

/* Convert from an IP version number to an Address Family symbol.  */
static inline int ipver2af(__u8 ipver)
{
	switch (ipver) {
	case 4:
	        return  AF_INET;
	case 6:
		return AF_INET6;
	default:
		return 0;
	}
}

/* Convert from an address parameter type to an address family.  */
static inline int param_type2af(__be16 type)
{
	switch (type) {
	case SCTP_PARAM_IPV4_ADDRESS:
	        return  AF_INET;
	case SCTP_PARAM_IPV6_ADDRESS:
		return AF_INET6;
	default:
		return 0;
	}
}

/* Warning: The following hash functions assume a power of two 'size'. */
/* This is the hash function for the SCTP port hash table. */
static inline int sctp_phashfn(struct net *net, __u16 lport)
{
	return (net_hash_mix(net) + lport) & (sctp_port_hashsize - 1);
}

/* This is the hash function for the endpoint hash table. */
static inline int sctp_ep_hashfn(struct net *net, __u16 lport)
{
	return (net_hash_mix(net) + lport) & (sctp_ep_hashsize - 1);
}

#define sctp_for_each_hentry(epb, head) \
	hlist_for_each_entry(epb, head, node)

/* Is a socket of this style? */
#define sctp_style(sk, style) __sctp_style((sk), (SCTP_SOCKET_##style))
static inline int __sctp_style(const struct sock *sk, sctp_socket_type_t style)
{
	return sctp_sk(sk)->type == style;
}

/* Is the association in this state? */
#define sctp_state(asoc, state) __sctp_state((asoc), (SCTP_STATE_##state))
static inline int __sctp_state(const struct sctp_association *asoc,
			       sctp_state_t state)
{
	return asoc->state == state;
}

/* Is the socket in this state? */
#define sctp_sstate(sk, state) __sctp_sstate((sk), (SCTP_SS_##state))
static inline int __sctp_sstate(const struct sock *sk, sctp_sock_state_t state)
{
	return sk->sk_state == state;
}

/* Map v4-mapped v6 address back to v4 address */
static inline void sctp_v6_map_v4(union sctp_addr *addr)
{
	addr->v4.sin_family = AF_INET;
	addr->v4.sin_port = addr->v6.sin6_port;
	addr->v4.sin_addr.s_addr = addr->v6.sin6_addr.s6_addr32[3];
}

/* Map v4 address to v4-mapped v6 address */
static inline void sctp_v4_map_v6(union sctp_addr *addr)
{
	__be16 port;

	port = addr->v4.sin_port;
	addr->v6.sin6_addr.s6_addr32[3] = addr->v4.sin_addr.s_addr;
	addr->v6.sin6_port = port;
	addr->v6.sin6_family = AF_INET6;
	addr->v6.sin6_flowinfo = 0;
	addr->v6.sin6_scope_id = 0;
	addr->v6.sin6_addr.s6_addr32[0] = 0;
	addr->v6.sin6_addr.s6_addr32[1] = 0;
	addr->v6.sin6_addr.s6_addr32[2] = htonl(0x0000ffff);
}

/* The cookie is always 0 since this is how it's used in the
 * pmtu code.
 */
static inline struct dst_entry *sctp_transport_dst_check(struct sctp_transport *t)
{
	if (t->dst && !dst_check(t->dst, t->dst_cookie)) {
		dst_release(t->dst);
		t->dst = NULL;
	}

	return t->dst;
}

#endif /* __net_sctp_h__ */
