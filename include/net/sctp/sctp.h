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
 * along with GNU CC; see the file COPYING.  If not, write to
 * the Free Software Foundation, 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 *
 * Please send any bug reports or fixes you make to the
 * email address(es):
 *    lksctp developers <lksctp-developers@lists.sourceforge.net>
 *
 * Or submit a bug report through the following website:
 *    http://www.sf.net/projects/lksctp
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
 *
 * Any bugs reported given to us we will try to fix... any fixes shared will
 * be incorporated into the next SCTP release.
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

#if defined(CONFIG_IPV6) || defined(CONFIG_IPV6_MODULE)
#include <net/ipv6.h>
#include <net/ip6_route.h>
#endif

#include <asm/uaccess.h>
#include <asm/page.h>
#include <net/sock.h>
#include <net/snmp.h>
#include <net/sctp/structs.h>
#include <net/sctp/constants.h>


/* Set SCTP_DEBUG flag via config if not already set. */
#ifndef SCTP_DEBUG
#ifdef CONFIG_SCTP_DBG_MSG
#define SCTP_DEBUG	1
#else
#define SCTP_DEBUG      0
#endif /* CONFIG_SCTP_DBG */
#endif /* SCTP_DEBUG */

#ifdef CONFIG_IP_SCTP_MODULE
#define SCTP_PROTOSW_FLAG 0
#else /* static! */
#define SCTP_PROTOSW_FLAG INET_PROTOSW_PERMANENT
#endif


/* Certain internal static functions need to be exported when
 * compiled into the test frame.
 */
#ifndef SCTP_STATIC
#define SCTP_STATIC static
#endif

/*
 * Function declarations.
 */

/*
 * sctp/protocol.c
 */
extern struct sock *sctp_get_ctl_sock(void);
extern void sctp_local_addr_free(struct rcu_head *head);
extern int sctp_copy_local_addr_list(struct sctp_bind_addr *,
				     sctp_scope_t, gfp_t gfp,
				     int flags);
extern struct sctp_pf *sctp_get_pf_specific(sa_family_t family);
extern int sctp_register_pf(struct sctp_pf *, sa_family_t);

/*
 * sctp/socket.c
 */
int sctp_backlog_rcv(struct sock *sk, struct sk_buff *skb);
int sctp_inet_listen(struct socket *sock, int backlog);
void sctp_write_space(struct sock *sk);
void sctp_data_ready(struct sock *sk, int len);
unsigned int sctp_poll(struct file *file, struct socket *sock,
		poll_table *wait);
void sctp_sock_rfree(struct sk_buff *skb);
void sctp_copy_sock(struct sock *newsk, struct sock *sk,
		    struct sctp_association *asoc);
extern struct percpu_counter sctp_sockets_allocated;

/*
 * sctp/primitive.c
 */
int sctp_primitive_ASSOCIATE(struct sctp_association *, void *arg);
int sctp_primitive_SHUTDOWN(struct sctp_association *, void *arg);
int sctp_primitive_ABORT(struct sctp_association *, void *arg);
int sctp_primitive_SEND(struct sctp_association *, void *arg);
int sctp_primitive_REQUESTHEARTBEAT(struct sctp_association *, void *arg);
int sctp_primitive_ASCONF(struct sctp_association *, void *arg);

/*
 * sctp/input.c
 */
int sctp_rcv(struct sk_buff *skb);
void sctp_v4_err(struct sk_buff *skb, u32 info);
void sctp_hash_established(struct sctp_association *);
void sctp_unhash_established(struct sctp_association *);
void sctp_hash_endpoint(struct sctp_endpoint *);
void sctp_unhash_endpoint(struct sctp_endpoint *);
struct sock *sctp_err_lookup(int family, struct sk_buff *,
			     struct sctphdr *, struct sctp_association **,
			     struct sctp_transport **);
void sctp_err_finish(struct sock *, struct sctp_association *);
void sctp_icmp_frag_needed(struct sock *, struct sctp_association *,
			   struct sctp_transport *t, __u32 pmtu);
void sctp_icmp_proto_unreachable(struct sock *sk,
				 struct sctp_association *asoc,
				 struct sctp_transport *t);
void sctp_backlog_migrate(struct sctp_association *assoc,
			  struct sock *oldsk, struct sock *newsk);

/*
 * sctp/proc.c
 */
int sctp_snmp_proc_init(void);
void sctp_snmp_proc_exit(void);
int sctp_eps_proc_init(void);
void sctp_eps_proc_exit(void);
int sctp_assocs_proc_init(void);
void sctp_assocs_proc_exit(void);
int sctp_remaddr_proc_init(void);
void sctp_remaddr_proc_exit(void);


/*
 * Module global variables
 */

 /*
  * sctp/protocol.c
  */
extern struct kmem_cache *sctp_chunk_cachep __read_mostly;
extern struct kmem_cache *sctp_bucket_cachep __read_mostly;

/*
 *  Section:  Macros, externs, and inlines
 */


#ifdef TEST_FRAME
#include <test_frame.h>
#else

/* spin lock wrappers. */
#define sctp_spin_lock_irqsave(lock, flags) spin_lock_irqsave(lock, flags)
#define sctp_spin_unlock_irqrestore(lock, flags)  \
       spin_unlock_irqrestore(lock, flags)
#define sctp_local_bh_disable() local_bh_disable()
#define sctp_local_bh_enable()  local_bh_enable()
#define sctp_spin_lock(lock)    spin_lock(lock)
#define sctp_spin_unlock(lock)  spin_unlock(lock)
#define sctp_write_lock(lock)   write_lock(lock)
#define sctp_write_unlock(lock) write_unlock(lock)
#define sctp_read_lock(lock)    read_lock(lock)
#define sctp_read_unlock(lock)  read_unlock(lock)

/* sock lock wrappers. */
#define sctp_lock_sock(sk)       lock_sock(sk)
#define sctp_release_sock(sk)    release_sock(sk)
#define sctp_bh_lock_sock(sk)    bh_lock_sock(sk)
#define sctp_bh_unlock_sock(sk)  bh_unlock_sock(sk)

/* SCTP SNMP MIB stats handlers */
DECLARE_SNMP_STAT(struct sctp_mib, sctp_statistics);
#define SCTP_INC_STATS(field)      SNMP_INC_STATS(sctp_statistics, field)
#define SCTP_INC_STATS_BH(field)   SNMP_INC_STATS_BH(sctp_statistics, field)
#define SCTP_INC_STATS_USER(field) SNMP_INC_STATS_USER(sctp_statistics, field)
#define SCTP_DEC_STATS(field)      SNMP_DEC_STATS(sctp_statistics, field)

#endif /* !TEST_FRAME */

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
} __SNMP_MIB_ALIGN__;


/* Print debugging messages.  */
#if SCTP_DEBUG
extern int sctp_debug_flag;
#define SCTP_DEBUG_PRINTK(whatever...) \
	((void) (sctp_debug_flag && printk(KERN_DEBUG whatever)))
#define SCTP_DEBUG_PRINTK_IPADDR(lead, trail, leadparm, saddr, otherparms...) \
	if (sctp_debug_flag) { \
		if (saddr->sa.sa_family == AF_INET6) { \
			printk(KERN_DEBUG \
			       lead "%pI6" trail, \
			       leadparm, \
			       &saddr->v6.sin6_addr, \
			       otherparms); \
		} else { \
			printk(KERN_DEBUG \
			       lead "%pI4" trail, \
			       leadparm, \
			       &saddr->v4.sin_addr.s_addr, \
			       otherparms); \
		} \
	}
#define SCTP_ENABLE_DEBUG { sctp_debug_flag = 1; }
#define SCTP_DISABLE_DEBUG { sctp_debug_flag = 0; }

#define SCTP_ASSERT(expr, str, func) \
	if (!(expr)) { \
		SCTP_DEBUG_PRINTK("Assertion Failed: %s(%s) at %s:%s:%d\n", \
			str, (#expr), __FILE__, __func__, __LINE__); \
		func; \
	}

#else	/* SCTP_DEBUG */

#define SCTP_DEBUG_PRINTK(whatever...)
#define SCTP_DEBUG_PRINTK_IPADDR(whatever...)
#define SCTP_ENABLE_DEBUG
#define SCTP_DISABLE_DEBUG
#define SCTP_ASSERT(expr, str, func)

#endif /* SCTP_DEBUG */


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

void sctp_dbg_objcnt_init(void);
void sctp_dbg_objcnt_exit(void);

#else

#define SCTP_DBG_OBJCNT_INC(name)
#define SCTP_DBG_OBJCNT_DEC(name)

static inline void sctp_dbg_objcnt_init(void) { return; }
static inline void sctp_dbg_objcnt_exit(void) { return; }

#endif /* CONFIG_SCTP_DBG_OBJCOUNT */

#if defined CONFIG_SYSCTL
void sctp_sysctl_register(void);
void sctp_sysctl_unregister(void);
#else
static inline void sctp_sysctl_register(void) { return; }
static inline void sctp_sysctl_unregister(void) { return; }
#endif

/* Size of Supported Address Parameter for 'x' address types. */
#define SCTP_SAT_LEN(x) (sizeof(struct sctp_paramhdr) + (x) * sizeof(__u16))

#if defined(CONFIG_IPV6) || defined(CONFIG_IPV6_MODULE)

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
	return (asoc?asoc->assoc_id:0);
}

/* Look up the association by its id.  */
struct sctp_association *sctp_id2assoc(struct sock *sk, sctp_assoc_t id);


/* A macro to walk a list of skbs.  */
#define sctp_skb_for_each(pos, head, tmp) \
	skb_queue_walk_safe(head, pos, tmp)

/* A helper to append an entire skb list (list) to another (head). */
static inline void sctp_skb_list_tail(struct sk_buff_head *list,
				      struct sk_buff_head *head)
{
	unsigned long flags;

	sctp_spin_lock_irqsave(&head->lock, flags);
	sctp_spin_lock(&list->lock);

	skb_queue_splice_tail_init(list, head);

	sctp_spin_unlock(&list->lock);
	sctp_spin_unlock_irqrestore(&head->lock, flags);
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
	return ((head->next != head) && (head->next == head->prev));
}

/* Generate a random jitter in the range of -50% ~ +50% of input RTO. */
static inline __s32 sctp_jitter(__u32 rto)
{
	static __u32 sctp_rand;
	__s32 ret;

	/* Avoid divide by zero. */
	if (!rto)
		rto = 1;

	sctp_rand += jiffies;
	sctp_rand ^= (sctp_rand << 12);
	sctp_rand ^= (sctp_rand >> 20);

	/* Choose random number from 0 to rto, then move to -50% ~ +50%
	 * of rto.
	 */
	ret = sctp_rand % rto - (rto >> 1);
	return ret;
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

	frag = min_t(int, frag, SCTP_MAX_CHUNK_LEN);

	return frag;
}

static inline void sctp_assoc_pending_pmtu(struct sctp_association *asoc)
{

	sctp_assoc_sync_pmtu(asoc);
	asoc->pmtu_pending = 0;
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
     pos.v <= (void *)chunk + end - sizeof(sctp_paramhdr_t) &&\
     pos.v <= (void *)chunk + end - ntohs(pos.p->length) &&\
     ntohs(pos.p->length) >= sizeof(sctp_paramhdr_t);\
     pos.v += WORD_ROUND(ntohs(pos.p->length)))

#define sctp_walk_errors(err, chunk_hdr)\
_sctp_walk_errors((err), (chunk_hdr), ntohs((chunk_hdr)->length))

#define _sctp_walk_errors(err, chunk_hdr, end)\
for (err = (sctp_errhdr_t *)((void *)chunk_hdr + \
	    sizeof(sctp_chunkhdr_t));\
     (void *)err <= (void *)chunk_hdr + end - sizeof(sctp_errhdr_t) &&\
     (void *)err <= (void *)chunk_hdr + end - ntohs(err->length) &&\
     ntohs(err->length) >= sizeof(sctp_errhdr_t); \
     err = (sctp_errhdr_t *)((void *)err + WORD_ROUND(ntohs(err->length))))

#define sctp_walk_fwdtsn(pos, chunk)\
_sctp_walk_fwdtsn((pos), (chunk), ntohs((chunk)->chunk_hdr->length) - sizeof(struct sctp_fwdtsn_chunk))

#define _sctp_walk_fwdtsn(pos, chunk, end)\
for (pos = chunk->subh.fwdtsn_hdr->skip;\
     (void *)pos <= (void *)chunk->subh.fwdtsn_hdr->skip + end - sizeof(struct sctp_fwdtsn_skip);\
     pos++)

/* Round an int up to the next multiple of 4.  */
#define WORD_ROUND(s) (((s)+3)&~3)

/* Make a new instance of type.  */
#define t_new(type, flags)	(type *)kmalloc(sizeof(type), flags)

/* Compare two timevals.  */
#define tv_lt(s, t) \
   (s.tv_sec < t.tv_sec || (s.tv_sec == t.tv_sec && s.tv_usec < t.tv_usec))

/* Add tv1 to tv2. */
#define TIMEVAL_ADD(tv1, tv2) \
({ \
        suseconds_t usecs = (tv2).tv_usec + (tv1).tv_usec; \
        time_t secs = (tv2).tv_sec + (tv1).tv_sec; \
\
        if (usecs >= 1000000) { \
                usecs -= 1000000; \
                secs++; \
        } \
        (tv2).tv_sec = secs; \
        (tv2).tv_usec = usecs; \
})

/* External references. */

extern struct proto sctp_prot;
extern struct proto sctpv6_prot;
extern struct proc_dir_entry *proc_net_sctp;
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
	};
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
	};
}

/* Perform some sanity checks. */
static inline int sctp_sanity_check(void)
{
	SCTP_ASSERT(sizeof(struct sctp_ulpevent) <=
		    sizeof(((struct sk_buff *)0)->cb),
		    "SCTP: ulpevent does not fit in skb!\n", return 0);

	return 1;
}

/* Warning: The following hash functions assume a power of two 'size'. */
/* This is the hash function for the SCTP port hash table. */
static inline int sctp_phashfn(__u16 lport)
{
	return (lport & (sctp_port_hashsize - 1));
}

/* This is the hash function for the endpoint hash table. */
static inline int sctp_ep_hashfn(__u16 lport)
{
	return (lport & (sctp_ep_hashsize - 1));
}

/* This is the hash function for the association hash table. */
static inline int sctp_assoc_hashfn(__u16 lport, __u16 rport)
{
	int h = (lport << 16) + rport;
	h ^= h>>8;
	return (h & (sctp_assoc_hashsize - 1));
}

/* This is the hash function for the association hash table.  This is
 * not used yet, but could be used as a better hash function when
 * we have a vtag.
 */
static inline int sctp_vtag_hashfn(__u16 lport, __u16 rport, __u32 vtag)
{
	int h = (lport << 16) + rport;
	h ^= vtag;
	return (h & (sctp_assoc_hashsize-1));
}

#define sctp_for_each_hentry(epb, node, head) \
	hlist_for_each_entry(epb, node, head, node)

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
	addr->v6.sin6_family = AF_INET6;
	addr->v6.sin6_port = addr->v4.sin_port;
	addr->v6.sin6_addr.s6_addr32[3] = addr->v4.sin_addr.s_addr;
	addr->v6.sin6_addr.s6_addr32[0] = 0;
	addr->v6.sin6_addr.s6_addr32[1] = 0;
	addr->v6.sin6_addr.s6_addr32[2] = htonl(0x0000ffff);
}

#endif /* __net_sctp_h__ */
