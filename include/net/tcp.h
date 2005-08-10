/*
 * INET		An implementation of the TCP/IP protocol suite for the LINUX
 *		operating system.  INET is implemented using the  BSD Socket
 *		interface as the means of communication with the user level.
 *
 *		Definitions for the TCP module.
 *
 * Version:	@(#)tcp.h	1.0.5	05/23/93
 *
 * Authors:	Ross Biro
 *		Fred N. van Kempen, <waltje@uWalt.NL.Mugnet.ORG>
 *
 *		This program is free software; you can redistribute it and/or
 *		modify it under the terms of the GNU General Public License
 *		as published by the Free Software Foundation; either version
 *		2 of the License, or (at your option) any later version.
 */
#ifndef _TCP_H
#define _TCP_H

#define TCP_DEBUG 1
#define FASTRETRANS_DEBUG 1

/* Cancel timers, when they are not required. */
#undef TCP_CLEAR_TIMERS

#include <linux/config.h>
#include <linux/list.h>
#include <linux/tcp.h>
#include <linux/slab.h>
#include <linux/cache.h>
#include <linux/percpu.h>
#include <net/checksum.h>
#include <net/request_sock.h>
#include <net/sock.h>
#include <net/snmp.h>
#include <net/ip.h>
#if defined(CONFIG_IPV6) || defined (CONFIG_IPV6_MODULE)
#include <linux/ipv6.h>
#endif
#include <linux/seq_file.h>

/* This is for all connections with a full identity, no wildcards.
 * New scheme, half the table is for TIME_WAIT, the other half is
 * for the rest.  I'll experiment with dynamic table growth later.
 */
struct tcp_ehash_bucket {
	rwlock_t	  lock;
	struct hlist_head chain;
} __attribute__((__aligned__(8)));

/* This is for listening sockets, thus all sockets which possess wildcards. */
#define TCP_LHTABLE_SIZE	32	/* Yes, really, this is all you need. */

/* There are a few simple rules, which allow for local port reuse by
 * an application.  In essence:
 *
 *	1) Sockets bound to different interfaces may share a local port.
 *	   Failing that, goto test 2.
 *	2) If all sockets have sk->sk_reuse set, and none of them are in
 *	   TCP_LISTEN state, the port may be shared.
 *	   Failing that, goto test 3.
 *	3) If all sockets are bound to a specific inet_sk(sk)->rcv_saddr local
 *	   address, and none of them are the same, the port may be
 *	   shared.
 *	   Failing this, the port cannot be shared.
 *
 * The interesting point, is test #2.  This is what an FTP server does
 * all day.  To optimize this case we use a specific flag bit defined
 * below.  As we add sockets to a bind bucket list, we perform a
 * check of: (newsk->sk_reuse && (newsk->sk_state != TCP_LISTEN))
 * As long as all sockets added to a bind bucket pass this test,
 * the flag bit will be set.
 * The resulting situation is that tcp_v[46]_verify_bind() can just check
 * for this flag bit, if it is set and the socket trying to bind has
 * sk->sk_reuse set, we don't even have to walk the owners list at all,
 * we return that it is ok to bind this socket to the requested local port.
 *
 * Sounds like a lot of work, but it is worth it.  In a more naive
 * implementation (ie. current FreeBSD etc.) the entire list of ports
 * must be walked for each data port opened by an ftp server.  Needless
 * to say, this does not scale at all.  With a couple thousand FTP
 * users logged onto your box, isn't it nice to know that new data
 * ports are created in O(1) time?  I thought so. ;-)	-DaveM
 */
struct tcp_bind_bucket {
	unsigned short		port;
	signed short		fastreuse;
	struct hlist_node	node;
	struct hlist_head	owners;
};

#define tb_for_each(tb, node, head) hlist_for_each_entry(tb, node, head, node)

struct tcp_bind_hashbucket {
	spinlock_t		lock;
	struct hlist_head	chain;
};

static inline struct tcp_bind_bucket *__tb_head(struct tcp_bind_hashbucket *head)
{
	return hlist_entry(head->chain.first, struct tcp_bind_bucket, node);
}

static inline struct tcp_bind_bucket *tb_head(struct tcp_bind_hashbucket *head)
{
	return hlist_empty(&head->chain) ? NULL : __tb_head(head);
}

extern struct tcp_hashinfo {
	/* This is for sockets with full identity only.  Sockets here will
	 * always be without wildcards and will have the following invariant:
	 *
	 *          TCP_ESTABLISHED <= sk->sk_state < TCP_CLOSE
	 *
	 * First half of the table is for sockets not in TIME_WAIT, second half
	 * is for TIME_WAIT sockets only.
	 */
	struct tcp_ehash_bucket *__tcp_ehash;

	/* Ok, let's try this, I give up, we do need a local binding
	 * TCP hash as well as the others for fast bind/connect.
	 */
	struct tcp_bind_hashbucket *__tcp_bhash;

	int __tcp_bhash_size;
	int __tcp_ehash_size;

	/* All sockets in TCP_LISTEN state will be in here.  This is the only
	 * table where wildcard'd TCP sockets can exist.  Hash function here
	 * is just local port number.
	 */
	struct hlist_head __tcp_listening_hash[TCP_LHTABLE_SIZE];

	/* All the above members are written once at bootup and
	 * never written again _or_ are predominantly read-access.
	 *
	 * Now align to a new cache line as all the following members
	 * are often dirty.
	 */
	rwlock_t __tcp_lhash_lock ____cacheline_aligned;
	atomic_t __tcp_lhash_users;
	wait_queue_head_t __tcp_lhash_wait;
	spinlock_t __tcp_portalloc_lock;
} tcp_hashinfo;

#define tcp_ehash	(tcp_hashinfo.__tcp_ehash)
#define tcp_bhash	(tcp_hashinfo.__tcp_bhash)
#define tcp_ehash_size	(tcp_hashinfo.__tcp_ehash_size)
#define tcp_bhash_size	(tcp_hashinfo.__tcp_bhash_size)
#define tcp_listening_hash (tcp_hashinfo.__tcp_listening_hash)
#define tcp_lhash_lock	(tcp_hashinfo.__tcp_lhash_lock)
#define tcp_lhash_users	(tcp_hashinfo.__tcp_lhash_users)
#define tcp_lhash_wait	(tcp_hashinfo.__tcp_lhash_wait)
#define tcp_portalloc_lock (tcp_hashinfo.__tcp_portalloc_lock)

extern kmem_cache_t *tcp_bucket_cachep;
extern struct tcp_bind_bucket *tcp_bucket_create(struct tcp_bind_hashbucket *head,
						 unsigned short snum);
extern void tcp_bucket_destroy(struct tcp_bind_bucket *tb);
extern void tcp_bucket_unlock(struct sock *sk);
extern int tcp_port_rover;

/* These are AF independent. */
static __inline__ int tcp_bhashfn(__u16 lport)
{
	return (lport & (tcp_bhash_size - 1));
}

extern void tcp_bind_hash(struct sock *sk, struct tcp_bind_bucket *tb,
			  unsigned short snum);

#if (BITS_PER_LONG == 64)
#define TCP_ADDRCMP_ALIGN_BYTES 8
#else
#define TCP_ADDRCMP_ALIGN_BYTES 4
#endif

/* This is a TIME_WAIT bucket.  It works around the memory consumption
 * problems of sockets in such a state on heavily loaded servers, but
 * without violating the protocol specification.
 */
struct tcp_tw_bucket {
	/*
	 * Now struct sock also uses sock_common, so please just
	 * don't add nothing before this first member (__tw_common) --acme
	 */
	struct sock_common	__tw_common;
#define tw_family		__tw_common.skc_family
#define tw_state		__tw_common.skc_state
#define tw_reuse		__tw_common.skc_reuse
#define tw_bound_dev_if		__tw_common.skc_bound_dev_if
#define tw_node			__tw_common.skc_node
#define tw_bind_node		__tw_common.skc_bind_node
#define tw_refcnt		__tw_common.skc_refcnt
	volatile unsigned char	tw_substate;
	unsigned char		tw_rcv_wscale;
	__u16			tw_sport;
	/* Socket demultiplex comparisons on incoming packets. */
	/* these five are in inet_sock */
	__u32			tw_daddr
		__attribute__((aligned(TCP_ADDRCMP_ALIGN_BYTES)));
	__u32			tw_rcv_saddr;
	__u16			tw_dport;
	__u16			tw_num;
	/* And these are ours. */
	int			tw_hashent;
	int			tw_timeout;
	__u32			tw_rcv_nxt;
	__u32			tw_snd_nxt;
	__u32			tw_rcv_wnd;
	__u32			tw_ts_recent;
	long			tw_ts_recent_stamp;
	unsigned long		tw_ttd;
	struct tcp_bind_bucket	*tw_tb;
	struct hlist_node	tw_death_node;
#if defined(CONFIG_IPV6) || defined(CONFIG_IPV6_MODULE)
	struct in6_addr		tw_v6_daddr;
	struct in6_addr		tw_v6_rcv_saddr;
	int			tw_v6_ipv6only;
#endif
};

static __inline__ void tw_add_node(struct tcp_tw_bucket *tw,
				   struct hlist_head *list)
{
	hlist_add_head(&tw->tw_node, list);
}

static __inline__ void tw_add_bind_node(struct tcp_tw_bucket *tw,
					struct hlist_head *list)
{
	hlist_add_head(&tw->tw_bind_node, list);
}

static inline int tw_dead_hashed(struct tcp_tw_bucket *tw)
{
	return tw->tw_death_node.pprev != NULL;
}

static __inline__ void tw_dead_node_init(struct tcp_tw_bucket *tw)
{
	tw->tw_death_node.pprev = NULL;
}

static __inline__ void __tw_del_dead_node(struct tcp_tw_bucket *tw)
{
	__hlist_del(&tw->tw_death_node);
	tw_dead_node_init(tw);
}

static __inline__ int tw_del_dead_node(struct tcp_tw_bucket *tw)
{
	if (tw_dead_hashed(tw)) {
		__tw_del_dead_node(tw);
		return 1;
	}
	return 0;
}

#define tw_for_each(tw, node, head) \
	hlist_for_each_entry(tw, node, head, tw_node)

#define tw_for_each_inmate(tw, node, jail) \
	hlist_for_each_entry(tw, node, jail, tw_death_node)

#define tw_for_each_inmate_safe(tw, node, safe, jail) \
	hlist_for_each_entry_safe(tw, node, safe, jail, tw_death_node)

#define tcptw_sk(__sk)	((struct tcp_tw_bucket *)(__sk))

static inline u32 tcp_v4_rcv_saddr(const struct sock *sk)
{
	return likely(sk->sk_state != TCP_TIME_WAIT) ?
		inet_sk(sk)->rcv_saddr : tcptw_sk(sk)->tw_rcv_saddr;
}

#if defined(CONFIG_IPV6) || defined(CONFIG_IPV6_MODULE)
static inline struct in6_addr *__tcp_v6_rcv_saddr(const struct sock *sk)
{
	return likely(sk->sk_state != TCP_TIME_WAIT) ?
		&inet6_sk(sk)->rcv_saddr : &tcptw_sk(sk)->tw_v6_rcv_saddr;
}

static inline struct in6_addr *tcp_v6_rcv_saddr(const struct sock *sk)
{
	return sk->sk_family == AF_INET6 ? __tcp_v6_rcv_saddr(sk) : NULL;
}

#define tcptw_sk_ipv6only(__sk)	(tcptw_sk(__sk)->tw_v6_ipv6only)

static inline int tcp_v6_ipv6only(const struct sock *sk)
{
	return likely(sk->sk_state != TCP_TIME_WAIT) ?
		ipv6_only_sock(sk) : tcptw_sk_ipv6only(sk);
}
#else
# define __tcp_v6_rcv_saddr(__sk)	NULL
# define tcp_v6_rcv_saddr(__sk)		NULL
# define tcptw_sk_ipv6only(__sk)	0
# define tcp_v6_ipv6only(__sk)		0
#endif

extern kmem_cache_t *tcp_timewait_cachep;

static inline void tcp_tw_put(struct tcp_tw_bucket *tw)
{
	if (atomic_dec_and_test(&tw->tw_refcnt)) {
#ifdef INET_REFCNT_DEBUG
		printk(KERN_DEBUG "tw_bucket %p released\n", tw);
#endif
		kmem_cache_free(tcp_timewait_cachep, tw);
	}
}

extern atomic_t tcp_orphan_count;
extern int tcp_tw_count;
extern void tcp_time_wait(struct sock *sk, int state, int timeo);
extern void tcp_tw_deschedule(struct tcp_tw_bucket *tw);


/* Socket demux engine toys. */
#ifdef __BIG_ENDIAN
#define TCP_COMBINED_PORTS(__sport, __dport) \
	(((__u32)(__sport)<<16) | (__u32)(__dport))
#else /* __LITTLE_ENDIAN */
#define TCP_COMBINED_PORTS(__sport, __dport) \
	(((__u32)(__dport)<<16) | (__u32)(__sport))
#endif

#if (BITS_PER_LONG == 64)
#ifdef __BIG_ENDIAN
#define TCP_V4_ADDR_COOKIE(__name, __saddr, __daddr) \
	__u64 __name = (((__u64)(__saddr))<<32)|((__u64)(__daddr));
#else /* __LITTLE_ENDIAN */
#define TCP_V4_ADDR_COOKIE(__name, __saddr, __daddr) \
	__u64 __name = (((__u64)(__daddr))<<32)|((__u64)(__saddr));
#endif /* __BIG_ENDIAN */
#define TCP_IPV4_MATCH(__sk, __cookie, __saddr, __daddr, __ports, __dif)\
	(((*((__u64 *)&(inet_sk(__sk)->daddr)))== (__cookie))	&&	\
	 ((*((__u32 *)&(inet_sk(__sk)->dport)))== (__ports))	&&	\
	 (!((__sk)->sk_bound_dev_if) || ((__sk)->sk_bound_dev_if == (__dif))))
#define TCP_IPV4_TW_MATCH(__sk, __cookie, __saddr, __daddr, __ports, __dif)\
	(((*((__u64 *)&(tcptw_sk(__sk)->tw_daddr))) == (__cookie)) &&	\
	 ((*((__u32 *)&(tcptw_sk(__sk)->tw_dport))) == (__ports)) &&	\
	 (!((__sk)->sk_bound_dev_if) || ((__sk)->sk_bound_dev_if == (__dif))))
#else /* 32-bit arch */
#define TCP_V4_ADDR_COOKIE(__name, __saddr, __daddr)
#define TCP_IPV4_MATCH(__sk, __cookie, __saddr, __daddr, __ports, __dif)\
	((inet_sk(__sk)->daddr			== (__saddr))	&&	\
	 (inet_sk(__sk)->rcv_saddr		== (__daddr))	&&	\
	 ((*((__u32 *)&(inet_sk(__sk)->dport)))== (__ports))	&&	\
	 (!((__sk)->sk_bound_dev_if) || ((__sk)->sk_bound_dev_if == (__dif))))
#define TCP_IPV4_TW_MATCH(__sk, __cookie, __saddr, __daddr, __ports, __dif)\
	((tcptw_sk(__sk)->tw_daddr		== (__saddr))	&&	\
	 (tcptw_sk(__sk)->tw_rcv_saddr		== (__daddr))	&&	\
	 ((*((__u32 *)&(tcptw_sk(__sk)->tw_dport))) == (__ports)) &&	\
	 (!((__sk)->sk_bound_dev_if) || ((__sk)->sk_bound_dev_if == (__dif))))
#endif /* 64-bit arch */

#define TCP_IPV6_MATCH(__sk, __saddr, __daddr, __ports, __dif)	   \
	(((*((__u32 *)&(inet_sk(__sk)->dport)))== (__ports))   	&& \
	 ((__sk)->sk_family		== AF_INET6)		&& \
	 ipv6_addr_equal(&inet6_sk(__sk)->daddr, (__saddr))	&& \
	 ipv6_addr_equal(&inet6_sk(__sk)->rcv_saddr, (__daddr))	&& \
	 (!((__sk)->sk_bound_dev_if) || ((__sk)->sk_bound_dev_if == (__dif))))

/* These can have wildcards, don't try too hard. */
static __inline__ int tcp_lhashfn(unsigned short num)
{
	return num & (TCP_LHTABLE_SIZE - 1);
}

static __inline__ int tcp_sk_listen_hashfn(struct sock *sk)
{
	return tcp_lhashfn(inet_sk(sk)->num);
}

#define MAX_TCP_HEADER	(128 + MAX_HEADER)

/* 
 * Never offer a window over 32767 without using window scaling. Some
 * poor stacks do signed 16bit maths! 
 */
#define MAX_TCP_WINDOW		32767U

/* Minimal accepted MSS. It is (60+60+8) - (20+20). */
#define TCP_MIN_MSS		88U

/* Minimal RCV_MSS. */
#define TCP_MIN_RCVMSS		536U

/* After receiving this amount of duplicate ACKs fast retransmit starts. */
#define TCP_FASTRETRANS_THRESH 3

/* Maximal reordering. */
#define TCP_MAX_REORDERING	127

/* Maximal number of ACKs sent quickly to accelerate slow-start. */
#define TCP_MAX_QUICKACKS	16U

/* urg_data states */
#define TCP_URG_VALID	0x0100
#define TCP_URG_NOTYET	0x0200
#define TCP_URG_READ	0x0400

#define TCP_RETR1	3	/*
				 * This is how many retries it does before it
				 * tries to figure out if the gateway is
				 * down. Minimal RFC value is 3; it corresponds
				 * to ~3sec-8min depending on RTO.
				 */

#define TCP_RETR2	15	/*
				 * This should take at least
				 * 90 minutes to time out.
				 * RFC1122 says that the limit is 100 sec.
				 * 15 is ~13-30min depending on RTO.
				 */

#define TCP_SYN_RETRIES	 5	/* number of times to retry active opening a
				 * connection: ~180sec is RFC minumum	*/

#define TCP_SYNACK_RETRIES 5	/* number of times to retry passive opening a
				 * connection: ~180sec is RFC minumum	*/


#define TCP_ORPHAN_RETRIES 7	/* number of times to retry on an orphaned
				 * socket. 7 is ~50sec-16min.
				 */


#define TCP_TIMEWAIT_LEN (60*HZ) /* how long to wait to destroy TIME-WAIT
				  * state, about 60 seconds	*/
#define TCP_FIN_TIMEOUT	TCP_TIMEWAIT_LEN
                                 /* BSD style FIN_WAIT2 deadlock breaker.
				  * It used to be 3min, new value is 60sec,
				  * to combine FIN-WAIT-2 timeout with
				  * TIME-WAIT timer.
				  */

#define TCP_DELACK_MAX	((unsigned)(HZ/5))	/* maximal time to delay before sending an ACK */
#if HZ >= 100
#define TCP_DELACK_MIN	((unsigned)(HZ/25))	/* minimal time to delay before sending an ACK */
#define TCP_ATO_MIN	((unsigned)(HZ/25))
#else
#define TCP_DELACK_MIN	4U
#define TCP_ATO_MIN	4U
#endif
#define TCP_RTO_MAX	((unsigned)(120*HZ))
#define TCP_RTO_MIN	((unsigned)(HZ/5))
#define TCP_TIMEOUT_INIT ((unsigned)(3*HZ))	/* RFC 1122 initial RTO value	*/

#define TCP_RESOURCE_PROBE_INTERVAL ((unsigned)(HZ/2U)) /* Maximal interval between probes
					                 * for local resources.
					                 */

#define TCP_KEEPALIVE_TIME	(120*60*HZ)	/* two hours */
#define TCP_KEEPALIVE_PROBES	9		/* Max of 9 keepalive probes	*/
#define TCP_KEEPALIVE_INTVL	(75*HZ)

#define MAX_TCP_KEEPIDLE	32767
#define MAX_TCP_KEEPINTVL	32767
#define MAX_TCP_KEEPCNT		127
#define MAX_TCP_SYNCNT		127

#define TCP_SYNQ_INTERVAL	(HZ/5)	/* Period of SYNACK timer */
#define TCP_SYNQ_HSIZE		512	/* Size of SYNACK hash table */

#define TCP_PAWS_24DAYS	(60 * 60 * 24 * 24)
#define TCP_PAWS_MSL	60		/* Per-host timestamps are invalidated
					 * after this time. It should be equal
					 * (or greater than) TCP_TIMEWAIT_LEN
					 * to provide reliability equal to one
					 * provided by timewait state.
					 */
#define TCP_PAWS_WINDOW	1		/* Replay window for per-host
					 * timestamps. It must be less than
					 * minimal timewait lifetime.
					 */

#define TCP_TW_RECYCLE_SLOTS_LOG	5
#define TCP_TW_RECYCLE_SLOTS		(1<<TCP_TW_RECYCLE_SLOTS_LOG)

/* If time > 4sec, it is "slow" path, no recycling is required,
   so that we select tick to get range about 4 seconds.
 */

#if HZ <= 16 || HZ > 4096
# error Unsupported: HZ <= 16 or HZ > 4096
#elif HZ <= 32
# define TCP_TW_RECYCLE_TICK (5+2-TCP_TW_RECYCLE_SLOTS_LOG)
#elif HZ <= 64
# define TCP_TW_RECYCLE_TICK (6+2-TCP_TW_RECYCLE_SLOTS_LOG)
#elif HZ <= 128
# define TCP_TW_RECYCLE_TICK (7+2-TCP_TW_RECYCLE_SLOTS_LOG)
#elif HZ <= 256
# define TCP_TW_RECYCLE_TICK (8+2-TCP_TW_RECYCLE_SLOTS_LOG)
#elif HZ <= 512
# define TCP_TW_RECYCLE_TICK (9+2-TCP_TW_RECYCLE_SLOTS_LOG)
#elif HZ <= 1024
# define TCP_TW_RECYCLE_TICK (10+2-TCP_TW_RECYCLE_SLOTS_LOG)
#elif HZ <= 2048
# define TCP_TW_RECYCLE_TICK (11+2-TCP_TW_RECYCLE_SLOTS_LOG)
#else
# define TCP_TW_RECYCLE_TICK (12+2-TCP_TW_RECYCLE_SLOTS_LOG)
#endif
/*
 *	TCP option
 */
 
#define TCPOPT_NOP		1	/* Padding */
#define TCPOPT_EOL		0	/* End of options */
#define TCPOPT_MSS		2	/* Segment size negotiating */
#define TCPOPT_WINDOW		3	/* Window scaling */
#define TCPOPT_SACK_PERM        4       /* SACK Permitted */
#define TCPOPT_SACK             5       /* SACK Block */
#define TCPOPT_TIMESTAMP	8	/* Better RTT estimations/PAWS */

/*
 *     TCP option lengths
 */

#define TCPOLEN_MSS            4
#define TCPOLEN_WINDOW         3
#define TCPOLEN_SACK_PERM      2
#define TCPOLEN_TIMESTAMP      10

/* But this is what stacks really send out. */
#define TCPOLEN_TSTAMP_ALIGNED		12
#define TCPOLEN_WSCALE_ALIGNED		4
#define TCPOLEN_SACKPERM_ALIGNED	4
#define TCPOLEN_SACK_BASE		2
#define TCPOLEN_SACK_BASE_ALIGNED	4
#define TCPOLEN_SACK_PERBLOCK		8

#define TCP_TIME_RETRANS	1	/* Retransmit timer */
#define TCP_TIME_DACK		2	/* Delayed ack timer */
#define TCP_TIME_PROBE0		3	/* Zero window probe timer */
#define TCP_TIME_KEEPOPEN	4	/* Keepalive timer */

/* Flags in tp->nonagle */
#define TCP_NAGLE_OFF		1	/* Nagle's algo is disabled */
#define TCP_NAGLE_CORK		2	/* Socket is corked	    */
#define TCP_NAGLE_PUSH		4	/* Cork is overriden for already queued data */

/* sysctl variables for tcp */
extern int sysctl_tcp_timestamps;
extern int sysctl_tcp_window_scaling;
extern int sysctl_tcp_sack;
extern int sysctl_tcp_fin_timeout;
extern int sysctl_tcp_tw_recycle;
extern int sysctl_tcp_keepalive_time;
extern int sysctl_tcp_keepalive_probes;
extern int sysctl_tcp_keepalive_intvl;
extern int sysctl_tcp_syn_retries;
extern int sysctl_tcp_synack_retries;
extern int sysctl_tcp_retries1;
extern int sysctl_tcp_retries2;
extern int sysctl_tcp_orphan_retries;
extern int sysctl_tcp_syncookies;
extern int sysctl_tcp_retrans_collapse;
extern int sysctl_tcp_stdurg;
extern int sysctl_tcp_rfc1337;
extern int sysctl_tcp_abort_on_overflow;
extern int sysctl_tcp_max_orphans;
extern int sysctl_tcp_max_tw_buckets;
extern int sysctl_tcp_fack;
extern int sysctl_tcp_reordering;
extern int sysctl_tcp_ecn;
extern int sysctl_tcp_dsack;
extern int sysctl_tcp_mem[3];
extern int sysctl_tcp_wmem[3];
extern int sysctl_tcp_rmem[3];
extern int sysctl_tcp_app_win;
extern int sysctl_tcp_adv_win_scale;
extern int sysctl_tcp_tw_reuse;
extern int sysctl_tcp_frto;
extern int sysctl_tcp_low_latency;
extern int sysctl_tcp_nometrics_save;
extern int sysctl_tcp_moderate_rcvbuf;
extern int sysctl_tcp_tso_win_divisor;

extern atomic_t tcp_memory_allocated;
extern atomic_t tcp_sockets_allocated;
extern int tcp_memory_pressure;

#if defined(CONFIG_IPV6) || defined(CONFIG_IPV6_MODULE)
#define TCP_INET_FAMILY(fam) ((fam) == AF_INET)
#else
#define TCP_INET_FAMILY(fam) 1
#endif

/*
 *	Pointers to address related TCP functions
 *	(i.e. things that depend on the address family)
 */

struct tcp_func {
	int			(*queue_xmit)		(struct sk_buff *skb,
							 int ipfragok);

	void			(*send_check)		(struct sock *sk,
							 struct tcphdr *th,
							 int len,
							 struct sk_buff *skb);

	int			(*rebuild_header)	(struct sock *sk);

	int			(*conn_request)		(struct sock *sk,
							 struct sk_buff *skb);

	struct sock *		(*syn_recv_sock)	(struct sock *sk,
							 struct sk_buff *skb,
							 struct request_sock *req,
							 struct dst_entry *dst);
    
	int			(*remember_stamp)	(struct sock *sk);

	__u16			net_header_len;

	int			(*setsockopt)		(struct sock *sk, 
							 int level, 
							 int optname, 
							 char __user *optval, 
							 int optlen);

	int			(*getsockopt)		(struct sock *sk, 
							 int level, 
							 int optname, 
							 char __user *optval, 
							 int __user *optlen);


	void			(*addr2sockaddr)	(struct sock *sk,
							 struct sockaddr *);

	int sockaddr_len;
};

/*
 * The next routines deal with comparing 32 bit unsigned ints
 * and worry about wraparound (automatic with unsigned arithmetic).
 */

static inline int before(__u32 seq1, __u32 seq2)
{
        return (__s32)(seq1-seq2) < 0;
}

static inline int after(__u32 seq1, __u32 seq2)
{
	return (__s32)(seq2-seq1) < 0;
}


/* is s2<=s1<=s3 ? */
static inline int between(__u32 seq1, __u32 seq2, __u32 seq3)
{
	return seq3 - seq2 >= seq1 - seq2;
}


extern struct proto tcp_prot;

DECLARE_SNMP_STAT(struct tcp_mib, tcp_statistics);
#define TCP_INC_STATS(field)		SNMP_INC_STATS(tcp_statistics, field)
#define TCP_INC_STATS_BH(field)		SNMP_INC_STATS_BH(tcp_statistics, field)
#define TCP_INC_STATS_USER(field) 	SNMP_INC_STATS_USER(tcp_statistics, field)
#define TCP_DEC_STATS(field)		SNMP_DEC_STATS(tcp_statistics, field)
#define TCP_ADD_STATS_BH(field, val)	SNMP_ADD_STATS_BH(tcp_statistics, field, val)
#define TCP_ADD_STATS_USER(field, val)	SNMP_ADD_STATS_USER(tcp_statistics, field, val)

extern void			tcp_put_port(struct sock *sk);
extern void			tcp_inherit_port(struct sock *sk, struct sock *child);

extern void			tcp_v4_err(struct sk_buff *skb, u32);

extern void			tcp_shutdown (struct sock *sk, int how);

extern int			tcp_v4_rcv(struct sk_buff *skb);

extern int			tcp_v4_remember_stamp(struct sock *sk);

extern int		    	tcp_v4_tw_remember_stamp(struct tcp_tw_bucket *tw);

extern int			tcp_sendmsg(struct kiocb *iocb, struct sock *sk,
					    struct msghdr *msg, size_t size);
extern ssize_t			tcp_sendpage(struct socket *sock, struct page *page, int offset, size_t size, int flags);

extern int			tcp_ioctl(struct sock *sk, 
					  int cmd, 
					  unsigned long arg);

extern int			tcp_rcv_state_process(struct sock *sk, 
						      struct sk_buff *skb,
						      struct tcphdr *th,
						      unsigned len);

extern int			tcp_rcv_established(struct sock *sk, 
						    struct sk_buff *skb,
						    struct tcphdr *th, 
						    unsigned len);

extern void			tcp_rcv_space_adjust(struct sock *sk);

enum tcp_ack_state_t
{
	TCP_ACK_SCHED = 1,
	TCP_ACK_TIMER = 2,
	TCP_ACK_PUSHED= 4
};

static inline void tcp_schedule_ack(struct tcp_sock *tp)
{
	tp->ack.pending |= TCP_ACK_SCHED;
}

static inline int tcp_ack_scheduled(struct tcp_sock *tp)
{
	return tp->ack.pending&TCP_ACK_SCHED;
}

static __inline__ void tcp_dec_quickack_mode(struct tcp_sock *tp, unsigned int pkts)
{
	if (tp->ack.quick) {
		if (pkts >= tp->ack.quick) {
			tp->ack.quick = 0;

			/* Leaving quickack mode we deflate ATO. */
			tp->ack.ato = TCP_ATO_MIN;
		} else
			tp->ack.quick -= pkts;
	}
}

extern void tcp_enter_quickack_mode(struct tcp_sock *tp);

static __inline__ void tcp_delack_init(struct tcp_sock *tp)
{
	memset(&tp->ack, 0, sizeof(tp->ack));
}

static inline void tcp_clear_options(struct tcp_options_received *rx_opt)
{
 	rx_opt->tstamp_ok = rx_opt->sack_ok = rx_opt->wscale_ok = rx_opt->snd_wscale = 0;
}

enum tcp_tw_status
{
	TCP_TW_SUCCESS = 0,
	TCP_TW_RST = 1,
	TCP_TW_ACK = 2,
	TCP_TW_SYN = 3
};


extern enum tcp_tw_status	tcp_timewait_state_process(struct tcp_tw_bucket *tw,
							   struct sk_buff *skb,
							   struct tcphdr *th,
							   unsigned len);

extern struct sock *		tcp_check_req(struct sock *sk,struct sk_buff *skb,
					      struct request_sock *req,
					      struct request_sock **prev);
extern int			tcp_child_process(struct sock *parent,
						  struct sock *child,
						  struct sk_buff *skb);
extern void			tcp_enter_frto(struct sock *sk);
extern void			tcp_enter_loss(struct sock *sk, int how);
extern void			tcp_clear_retrans(struct tcp_sock *tp);
extern void			tcp_update_metrics(struct sock *sk);

extern void			tcp_close(struct sock *sk, 
					  long timeout);
extern struct sock *		tcp_accept(struct sock *sk, int flags, int *err);
extern unsigned int		tcp_poll(struct file * file, struct socket *sock, struct poll_table_struct *wait);

extern int			tcp_getsockopt(struct sock *sk, int level, 
					       int optname,
					       char __user *optval, 
					       int __user *optlen);
extern int			tcp_setsockopt(struct sock *sk, int level, 
					       int optname, char __user *optval, 
					       int optlen);
extern void			tcp_set_keepalive(struct sock *sk, int val);
extern int			tcp_recvmsg(struct kiocb *iocb, struct sock *sk,
					    struct msghdr *msg,
					    size_t len, int nonblock, 
					    int flags, int *addr_len);

extern int			tcp_listen_start(struct sock *sk);

extern void			tcp_parse_options(struct sk_buff *skb,
						  struct tcp_options_received *opt_rx,
						  int estab);

/*
 *	TCP v4 functions exported for the inet6 API
 */

extern int		       	tcp_v4_rebuild_header(struct sock *sk);

extern int		       	tcp_v4_build_header(struct sock *sk, 
						    struct sk_buff *skb);

extern void		       	tcp_v4_send_check(struct sock *sk, 
						  struct tcphdr *th, int len, 
						  struct sk_buff *skb);

extern int			tcp_v4_conn_request(struct sock *sk,
						    struct sk_buff *skb);

extern struct sock *		tcp_create_openreq_child(struct sock *sk,
							 struct request_sock *req,
							 struct sk_buff *skb);

extern struct sock *		tcp_v4_syn_recv_sock(struct sock *sk,
						     struct sk_buff *skb,
						     struct request_sock *req,
							struct dst_entry *dst);

extern int			tcp_v4_do_rcv(struct sock *sk,
					      struct sk_buff *skb);

extern int			tcp_v4_connect(struct sock *sk,
					       struct sockaddr *uaddr,
					       int addr_len);

extern int			tcp_connect(struct sock *sk);

extern struct sk_buff *		tcp_make_synack(struct sock *sk,
						struct dst_entry *dst,
						struct request_sock *req);

extern int			tcp_disconnect(struct sock *sk, int flags);

extern void			tcp_unhash(struct sock *sk);

extern int			tcp_v4_hash_connecting(struct sock *sk);


/* From syncookies.c */
extern struct sock *cookie_v4_check(struct sock *sk, struct sk_buff *skb, 
				    struct ip_options *opt);
extern __u32 cookie_v4_init_sequence(struct sock *sk, struct sk_buff *skb, 
				     __u16 *mss);

/* tcp_output.c */

extern void __tcp_push_pending_frames(struct sock *sk, struct tcp_sock *tp,
				      unsigned int cur_mss, int nonagle);
extern int tcp_may_send_now(struct sock *sk, struct tcp_sock *tp);
extern int tcp_retransmit_skb(struct sock *, struct sk_buff *);
extern void tcp_xmit_retransmit_queue(struct sock *);
extern void tcp_simple_retransmit(struct sock *);
extern int tcp_trim_head(struct sock *, struct sk_buff *, u32);

extern void tcp_send_probe0(struct sock *);
extern void tcp_send_partial(struct sock *);
extern int  tcp_write_wakeup(struct sock *);
extern void tcp_send_fin(struct sock *sk);
extern void tcp_send_active_reset(struct sock *sk,
                                  unsigned int __nocast priority);
extern int  tcp_send_synack(struct sock *);
extern void tcp_push_one(struct sock *, unsigned int mss_now);
extern void tcp_send_ack(struct sock *sk);
extern void tcp_send_delayed_ack(struct sock *sk);

/* tcp_input.c */
extern void tcp_cwnd_application_limited(struct sock *sk);

/* tcp_timer.c */
extern void tcp_init_xmit_timers(struct sock *);
extern void tcp_clear_xmit_timers(struct sock *);

extern void tcp_delete_keepalive_timer(struct sock *);
extern void tcp_reset_keepalive_timer(struct sock *, unsigned long);
extern unsigned int tcp_sync_mss(struct sock *sk, u32 pmtu);
extern unsigned int tcp_current_mss(struct sock *sk, int large);

#ifdef TCP_DEBUG
extern const char tcp_timer_bug_msg[];
#endif

/* tcp_diag.c */
extern void tcp_get_info(struct sock *, struct tcp_info *);

/* Read 'sendfile()'-style from a TCP socket */
typedef int (*sk_read_actor_t)(read_descriptor_t *, struct sk_buff *,
				unsigned int, size_t);
extern int tcp_read_sock(struct sock *sk, read_descriptor_t *desc,
			 sk_read_actor_t recv_actor);

static inline void tcp_clear_xmit_timer(struct sock *sk, int what)
{
	struct tcp_sock *tp = tcp_sk(sk);
	
	switch (what) {
	case TCP_TIME_RETRANS:
	case TCP_TIME_PROBE0:
		tp->pending = 0;

#ifdef TCP_CLEAR_TIMERS
		sk_stop_timer(sk, &tp->retransmit_timer);
#endif
		break;
	case TCP_TIME_DACK:
		tp->ack.blocked = 0;
		tp->ack.pending = 0;

#ifdef TCP_CLEAR_TIMERS
		sk_stop_timer(sk, &tp->delack_timer);
#endif
		break;
	default:
#ifdef TCP_DEBUG
		printk(tcp_timer_bug_msg);
#endif
		return;
	};

}

/*
 *	Reset the retransmission timer
 */
static inline void tcp_reset_xmit_timer(struct sock *sk, int what, unsigned long when)
{
	struct tcp_sock *tp = tcp_sk(sk);

	if (when > TCP_RTO_MAX) {
#ifdef TCP_DEBUG
		printk(KERN_DEBUG "reset_xmit_timer sk=%p %d when=0x%lx, caller=%p\n", sk, what, when, current_text_addr());
#endif
		when = TCP_RTO_MAX;
	}

	switch (what) {
	case TCP_TIME_RETRANS:
	case TCP_TIME_PROBE0:
		tp->pending = what;
		tp->timeout = jiffies+when;
		sk_reset_timer(sk, &tp->retransmit_timer, tp->timeout);
		break;

	case TCP_TIME_DACK:
		tp->ack.pending |= TCP_ACK_TIMER;
		tp->ack.timeout = jiffies+when;
		sk_reset_timer(sk, &tp->delack_timer, tp->ack.timeout);
		break;

	default:
#ifdef TCP_DEBUG
		printk(tcp_timer_bug_msg);
#endif
		return;
	};
}

/* Initialize RCV_MSS value.
 * RCV_MSS is an our guess about MSS used by the peer.
 * We haven't any direct information about the MSS.
 * It's better to underestimate the RCV_MSS rather than overestimate.
 * Overestimations make us ACKing less frequently than needed.
 * Underestimations are more easy to detect and fix by tcp_measure_rcv_mss().
 */

static inline void tcp_initialize_rcv_mss(struct sock *sk)
{
	struct tcp_sock *tp = tcp_sk(sk);
	unsigned int hint = min_t(unsigned int, tp->advmss, tp->mss_cache);

	hint = min(hint, tp->rcv_wnd/2);
	hint = min(hint, TCP_MIN_RCVMSS);
	hint = max(hint, TCP_MIN_MSS);

	tp->ack.rcv_mss = hint;
}

static __inline__ void __tcp_fast_path_on(struct tcp_sock *tp, u32 snd_wnd)
{
	tp->pred_flags = htonl((tp->tcp_header_len << 26) |
			       ntohl(TCP_FLAG_ACK) |
			       snd_wnd);
}

static __inline__ void tcp_fast_path_on(struct tcp_sock *tp)
{
	__tcp_fast_path_on(tp, tp->snd_wnd >> tp->rx_opt.snd_wscale);
}

static inline void tcp_fast_path_check(struct sock *sk, struct tcp_sock *tp)
{
	if (skb_queue_empty(&tp->out_of_order_queue) &&
	    tp->rcv_wnd &&
	    atomic_read(&sk->sk_rmem_alloc) < sk->sk_rcvbuf &&
	    !tp->urg_data)
		tcp_fast_path_on(tp);
}

/* Compute the actual receive window we are currently advertising.
 * Rcv_nxt can be after the window if our peer push more data
 * than the offered window.
 */
static __inline__ u32 tcp_receive_window(const struct tcp_sock *tp)
{
	s32 win = tp->rcv_wup + tp->rcv_wnd - tp->rcv_nxt;

	if (win < 0)
		win = 0;
	return (u32) win;
}

/* Choose a new window, without checks for shrinking, and without
 * scaling applied to the result.  The caller does these things
 * if necessary.  This is a "raw" window selection.
 */
extern u32	__tcp_select_window(struct sock *sk);

/* TCP timestamps are only 32-bits, this causes a slight
 * complication on 64-bit systems since we store a snapshot
 * of jiffies in the buffer control blocks below.  We decidely
 * only use of the low 32-bits of jiffies and hide the ugly
 * casts with the following macro.
 */
#define tcp_time_stamp		((__u32)(jiffies))

/* This is what the send packet queueing engine uses to pass
 * TCP per-packet control information to the transmission
 * code.  We also store the host-order sequence numbers in
 * here too.  This is 36 bytes on 32-bit architectures,
 * 40 bytes on 64-bit machines, if this grows please adjust
 * skbuff.h:skbuff->cb[xxx] size appropriately.
 */
struct tcp_skb_cb {
	union {
		struct inet_skb_parm	h4;
#if defined(CONFIG_IPV6) || defined (CONFIG_IPV6_MODULE)
		struct inet6_skb_parm	h6;
#endif
	} header;	/* For incoming frames		*/
	__u32		seq;		/* Starting sequence number	*/
	__u32		end_seq;	/* SEQ + FIN + SYN + datalen	*/
	__u32		when;		/* used to compute rtt's	*/
	__u8		flags;		/* TCP header flags.		*/

	/* NOTE: These must match up to the flags byte in a
	 *       real TCP header.
	 */
#define TCPCB_FLAG_FIN		0x01
#define TCPCB_FLAG_SYN		0x02
#define TCPCB_FLAG_RST		0x04
#define TCPCB_FLAG_PSH		0x08
#define TCPCB_FLAG_ACK		0x10
#define TCPCB_FLAG_URG		0x20
#define TCPCB_FLAG_ECE		0x40
#define TCPCB_FLAG_CWR		0x80

	__u8		sacked;		/* State flags for SACK/FACK.	*/
#define TCPCB_SACKED_ACKED	0x01	/* SKB ACK'd by a SACK block	*/
#define TCPCB_SACKED_RETRANS	0x02	/* SKB retransmitted		*/
#define TCPCB_LOST		0x04	/* SKB is lost			*/
#define TCPCB_TAGBITS		0x07	/* All tag bits			*/

#define TCPCB_EVER_RETRANS	0x80	/* Ever retransmitted frame	*/
#define TCPCB_RETRANS		(TCPCB_SACKED_RETRANS|TCPCB_EVER_RETRANS)

#define TCPCB_URG		0x20	/* Urgent pointer advenced here	*/

#define TCPCB_AT_TAIL		(TCPCB_URG)

	__u16		urg_ptr;	/* Valid w/URG flags is set.	*/
	__u32		ack_seq;	/* Sequence number ACK'd	*/
};

#define TCP_SKB_CB(__skb)	((struct tcp_skb_cb *)&((__skb)->cb[0]))

#include <net/tcp_ecn.h>

/* Due to TSO, an SKB can be composed of multiple actual
 * packets.  To keep these tracked properly, we use this.
 */
static inline int tcp_skb_pcount(const struct sk_buff *skb)
{
	return skb_shinfo(skb)->tso_segs;
}

/* This is valid iff tcp_skb_pcount() > 1. */
static inline int tcp_skb_mss(const struct sk_buff *skb)
{
	return skb_shinfo(skb)->tso_size;
}

static inline void tcp_dec_pcount_approx(__u32 *count,
					 const struct sk_buff *skb)
{
	if (*count) {
		*count -= tcp_skb_pcount(skb);
		if ((int)*count < 0)
			*count = 0;
	}
}

static inline void tcp_packets_out_inc(struct sock *sk, 
				       struct tcp_sock *tp,
				       const struct sk_buff *skb)
{
	int orig = tp->packets_out;

	tp->packets_out += tcp_skb_pcount(skb);
	if (!orig)
		tcp_reset_xmit_timer(sk, TCP_TIME_RETRANS, tp->rto);
}

static inline void tcp_packets_out_dec(struct tcp_sock *tp, 
				       const struct sk_buff *skb)
{
	tp->packets_out -= tcp_skb_pcount(skb);
}

/* Events passed to congestion control interface */
enum tcp_ca_event {
	CA_EVENT_TX_START,	/* first transmit when no packets in flight */
	CA_EVENT_CWND_RESTART,	/* congestion window restart */
	CA_EVENT_COMPLETE_CWR,	/* end of congestion recovery */
	CA_EVENT_FRTO,		/* fast recovery timeout */
	CA_EVENT_LOSS,		/* loss timeout */
	CA_EVENT_FAST_ACK,	/* in sequence ack */
	CA_EVENT_SLOW_ACK,	/* other ack */
};

/*
 * Interface for adding new TCP congestion control handlers
 */
#define TCP_CA_NAME_MAX	16
struct tcp_congestion_ops {
	struct list_head	list;

	/* initialize private data (optional) */
	void (*init)(struct tcp_sock *tp);
	/* cleanup private data  (optional) */
	void (*release)(struct tcp_sock *tp);

	/* return slow start threshold (required) */
	u32 (*ssthresh)(struct tcp_sock *tp);
	/* lower bound for congestion window (optional) */
	u32 (*min_cwnd)(struct tcp_sock *tp);
	/* do new cwnd calculation (required) */
	void (*cong_avoid)(struct tcp_sock *tp, u32 ack,
			   u32 rtt, u32 in_flight, int good_ack);
	/* round trip time sample per acked packet (optional) */
	void (*rtt_sample)(struct tcp_sock *tp, u32 usrtt);
	/* call before changing ca_state (optional) */
	void (*set_state)(struct tcp_sock *tp, u8 new_state);
	/* call when cwnd event occurs (optional) */
	void (*cwnd_event)(struct tcp_sock *tp, enum tcp_ca_event ev);
	/* new value of cwnd after loss (optional) */
	u32  (*undo_cwnd)(struct tcp_sock *tp);
	/* hook for packet ack accounting (optional) */
	void (*pkts_acked)(struct tcp_sock *tp, u32 num_acked);
	/* get info for tcp_diag (optional) */
	void (*get_info)(struct tcp_sock *tp, u32 ext, struct sk_buff *skb);

	char 		name[TCP_CA_NAME_MAX];
	struct module 	*owner;
};

extern int tcp_register_congestion_control(struct tcp_congestion_ops *type);
extern void tcp_unregister_congestion_control(struct tcp_congestion_ops *type);

extern void tcp_init_congestion_control(struct tcp_sock *tp);
extern void tcp_cleanup_congestion_control(struct tcp_sock *tp);
extern int tcp_set_default_congestion_control(const char *name);
extern void tcp_get_default_congestion_control(char *name);
extern int tcp_set_congestion_control(struct tcp_sock *tp, const char *name);

extern struct tcp_congestion_ops tcp_init_congestion_ops;
extern u32 tcp_reno_ssthresh(struct tcp_sock *tp);
extern void tcp_reno_cong_avoid(struct tcp_sock *tp, u32 ack,
				u32 rtt, u32 in_flight, int flag);
extern u32 tcp_reno_min_cwnd(struct tcp_sock *tp);
extern struct tcp_congestion_ops tcp_reno;

static inline void tcp_set_ca_state(struct tcp_sock *tp, u8 ca_state)
{
	if (tp->ca_ops->set_state)
		tp->ca_ops->set_state(tp, ca_state);
	tp->ca_state = ca_state;
}

static inline void tcp_ca_event(struct tcp_sock *tp, enum tcp_ca_event event)
{
	if (tp->ca_ops->cwnd_event)
		tp->ca_ops->cwnd_event(tp, event);
}

/* This determines how many packets are "in the network" to the best
 * of our knowledge.  In many cases it is conservative, but where
 * detailed information is available from the receiver (via SACK
 * blocks etc.) we can make more aggressive calculations.
 *
 * Use this for decisions involving congestion control, use just
 * tp->packets_out to determine if the send queue is empty or not.
 *
 * Read this equation as:
 *
 *	"Packets sent once on transmission queue" MINUS
 *	"Packets left network, but not honestly ACKed yet" PLUS
 *	"Packets fast retransmitted"
 */
static __inline__ unsigned int tcp_packets_in_flight(const struct tcp_sock *tp)
{
	return (tp->packets_out - tp->left_out + tp->retrans_out);
}

/* If cwnd > ssthresh, we may raise ssthresh to be half-way to cwnd.
 * The exception is rate halving phase, when cwnd is decreasing towards
 * ssthresh.
 */
static inline __u32 tcp_current_ssthresh(struct tcp_sock *tp)
{
	if ((1<<tp->ca_state)&(TCPF_CA_CWR|TCPF_CA_Recovery))
		return tp->snd_ssthresh;
	else
		return max(tp->snd_ssthresh,
			   ((tp->snd_cwnd >> 1) +
			    (tp->snd_cwnd >> 2)));
}

static inline void tcp_sync_left_out(struct tcp_sock *tp)
{
	if (tp->rx_opt.sack_ok &&
	    (tp->sacked_out >= tp->packets_out - tp->lost_out))
		tp->sacked_out = tp->packets_out - tp->lost_out;
	tp->left_out = tp->sacked_out + tp->lost_out;
}

/* Set slow start threshold and cwnd not falling to slow start */
static inline void __tcp_enter_cwr(struct tcp_sock *tp)
{
	tp->undo_marker = 0;
	tp->snd_ssthresh = tp->ca_ops->ssthresh(tp);
	tp->snd_cwnd = min(tp->snd_cwnd,
			   tcp_packets_in_flight(tp) + 1U);
	tp->snd_cwnd_cnt = 0;
	tp->high_seq = tp->snd_nxt;
	tp->snd_cwnd_stamp = tcp_time_stamp;
	TCP_ECN_queue_cwr(tp);
}

static inline void tcp_enter_cwr(struct tcp_sock *tp)
{
	tp->prior_ssthresh = 0;
	if (tp->ca_state < TCP_CA_CWR) {
		__tcp_enter_cwr(tp);
		tcp_set_ca_state(tp, TCP_CA_CWR);
	}
}

extern __u32 tcp_init_cwnd(struct tcp_sock *tp, struct dst_entry *dst);

/* Slow start with delack produces 3 packets of burst, so that
 * it is safe "de facto".
 */
static __inline__ __u32 tcp_max_burst(const struct tcp_sock *tp)
{
	return 3;
}

static __inline__ void tcp_minshall_update(struct tcp_sock *tp, int mss, 
					   const struct sk_buff *skb)
{
	if (skb->len < mss)
		tp->snd_sml = TCP_SKB_CB(skb)->end_seq;
}

static __inline__ void tcp_check_probe_timer(struct sock *sk, struct tcp_sock *tp)
{
	if (!tp->packets_out && !tp->pending)
		tcp_reset_xmit_timer(sk, TCP_TIME_PROBE0, tp->rto);
}

static __inline__ void tcp_push_pending_frames(struct sock *sk,
					       struct tcp_sock *tp)
{
	__tcp_push_pending_frames(sk, tp, tcp_current_mss(sk, 1), tp->nonagle);
}

static __inline__ void tcp_init_wl(struct tcp_sock *tp, u32 ack, u32 seq)
{
	tp->snd_wl1 = seq;
}

static __inline__ void tcp_update_wl(struct tcp_sock *tp, u32 ack, u32 seq)
{
	tp->snd_wl1 = seq;
}

extern void tcp_destroy_sock(struct sock *sk);


/*
 * Calculate(/check) TCP checksum
 */
static __inline__ u16 tcp_v4_check(struct tcphdr *th, int len,
				   unsigned long saddr, unsigned long daddr, 
				   unsigned long base)
{
	return csum_tcpudp_magic(saddr,daddr,len,IPPROTO_TCP,base);
}

static __inline__ int __tcp_checksum_complete(struct sk_buff *skb)
{
	return (unsigned short)csum_fold(skb_checksum(skb, 0, skb->len, skb->csum));
}

static __inline__ int tcp_checksum_complete(struct sk_buff *skb)
{
	return skb->ip_summed != CHECKSUM_UNNECESSARY &&
		__tcp_checksum_complete(skb);
}

/* Prequeue for VJ style copy to user, combined with checksumming. */

static __inline__ void tcp_prequeue_init(struct tcp_sock *tp)
{
	tp->ucopy.task = NULL;
	tp->ucopy.len = 0;
	tp->ucopy.memory = 0;
	skb_queue_head_init(&tp->ucopy.prequeue);
}

/* Packet is added to VJ-style prequeue for processing in process
 * context, if a reader task is waiting. Apparently, this exciting
 * idea (VJ's mail "Re: query about TCP header on tcp-ip" of 07 Sep 93)
 * failed somewhere. Latency? Burstiness? Well, at least now we will
 * see, why it failed. 8)8)				  --ANK
 *
 * NOTE: is this not too big to inline?
 */
static __inline__ int tcp_prequeue(struct sock *sk, struct sk_buff *skb)
{
	struct tcp_sock *tp = tcp_sk(sk);

	if (!sysctl_tcp_low_latency && tp->ucopy.task) {
		__skb_queue_tail(&tp->ucopy.prequeue, skb);
		tp->ucopy.memory += skb->truesize;
		if (tp->ucopy.memory > sk->sk_rcvbuf) {
			struct sk_buff *skb1;

			BUG_ON(sock_owned_by_user(sk));

			while ((skb1 = __skb_dequeue(&tp->ucopy.prequeue)) != NULL) {
				sk->sk_backlog_rcv(sk, skb1);
				NET_INC_STATS_BH(LINUX_MIB_TCPPREQUEUEDROPPED);
			}

			tp->ucopy.memory = 0;
		} else if (skb_queue_len(&tp->ucopy.prequeue) == 1) {
			wake_up_interruptible(sk->sk_sleep);
			if (!tcp_ack_scheduled(tp))
				tcp_reset_xmit_timer(sk, TCP_TIME_DACK, (3*TCP_RTO_MIN)/4);
		}
		return 1;
	}
	return 0;
}


#undef STATE_TRACE

#ifdef STATE_TRACE
static const char *statename[]={
	"Unused","Established","Syn Sent","Syn Recv",
	"Fin Wait 1","Fin Wait 2","Time Wait", "Close",
	"Close Wait","Last ACK","Listen","Closing"
};
#endif

static __inline__ void tcp_set_state(struct sock *sk, int state)
{
	int oldstate = sk->sk_state;

	switch (state) {
	case TCP_ESTABLISHED:
		if (oldstate != TCP_ESTABLISHED)
			TCP_INC_STATS(TCP_MIB_CURRESTAB);
		break;

	case TCP_CLOSE:
		if (oldstate == TCP_CLOSE_WAIT || oldstate == TCP_ESTABLISHED)
			TCP_INC_STATS(TCP_MIB_ESTABRESETS);

		sk->sk_prot->unhash(sk);
		if (tcp_sk(sk)->bind_hash &&
		    !(sk->sk_userlocks & SOCK_BINDPORT_LOCK))
			tcp_put_port(sk);
		/* fall through */
	default:
		if (oldstate==TCP_ESTABLISHED)
			TCP_DEC_STATS(TCP_MIB_CURRESTAB);
	}

	/* Change state AFTER socket is unhashed to avoid closed
	 * socket sitting in hash tables.
	 */
	sk->sk_state = state;

#ifdef STATE_TRACE
	SOCK_DEBUG(sk, "TCP sk=%p, State %s -> %s\n",sk, statename[oldstate],statename[state]);
#endif	
}

static __inline__ void tcp_done(struct sock *sk)
{
	tcp_set_state(sk, TCP_CLOSE);
	tcp_clear_xmit_timers(sk);

	sk->sk_shutdown = SHUTDOWN_MASK;

	if (!sock_flag(sk, SOCK_DEAD))
		sk->sk_state_change(sk);
	else
		tcp_destroy_sock(sk);
}

static __inline__ void tcp_sack_reset(struct tcp_options_received *rx_opt)
{
	rx_opt->dsack = 0;
	rx_opt->eff_sacks = 0;
	rx_opt->num_sacks = 0;
}

static __inline__ void tcp_build_and_update_options(__u32 *ptr, struct tcp_sock *tp, __u32 tstamp)
{
	if (tp->rx_opt.tstamp_ok) {
		*ptr++ = __constant_htonl((TCPOPT_NOP << 24) |
					  (TCPOPT_NOP << 16) |
					  (TCPOPT_TIMESTAMP << 8) |
					  TCPOLEN_TIMESTAMP);
		*ptr++ = htonl(tstamp);
		*ptr++ = htonl(tp->rx_opt.ts_recent);
	}
	if (tp->rx_opt.eff_sacks) {
		struct tcp_sack_block *sp = tp->rx_opt.dsack ? tp->duplicate_sack : tp->selective_acks;
		int this_sack;

		*ptr++ = __constant_htonl((TCPOPT_NOP << 24) |
					  (TCPOPT_NOP << 16) |
					  (TCPOPT_SACK << 8) |
					  (TCPOLEN_SACK_BASE +
					   (tp->rx_opt.eff_sacks * TCPOLEN_SACK_PERBLOCK)));
		for(this_sack = 0; this_sack < tp->rx_opt.eff_sacks; this_sack++) {
			*ptr++ = htonl(sp[this_sack].start_seq);
			*ptr++ = htonl(sp[this_sack].end_seq);
		}
		if (tp->rx_opt.dsack) {
			tp->rx_opt.dsack = 0;
			tp->rx_opt.eff_sacks--;
		}
	}
}

/* Construct a tcp options header for a SYN or SYN_ACK packet.
 * If this is every changed make sure to change the definition of
 * MAX_SYN_SIZE to match the new maximum number of options that you
 * can generate.
 */
static inline void tcp_syn_build_options(__u32 *ptr, int mss, int ts, int sack,
					     int offer_wscale, int wscale, __u32 tstamp, __u32 ts_recent)
{
	/* We always get an MSS option.
	 * The option bytes which will be seen in normal data
	 * packets should timestamps be used, must be in the MSS
	 * advertised.  But we subtract them from tp->mss_cache so
	 * that calculations in tcp_sendmsg are simpler etc.
	 * So account for this fact here if necessary.  If we
	 * don't do this correctly, as a receiver we won't
	 * recognize data packets as being full sized when we
	 * should, and thus we won't abide by the delayed ACK
	 * rules correctly.
	 * SACKs don't matter, we never delay an ACK when we
	 * have any of those going out.
	 */
	*ptr++ = htonl((TCPOPT_MSS << 24) | (TCPOLEN_MSS << 16) | mss);
	if (ts) {
		if(sack)
			*ptr++ = __constant_htonl((TCPOPT_SACK_PERM << 24) | (TCPOLEN_SACK_PERM << 16) |
						  (TCPOPT_TIMESTAMP << 8) | TCPOLEN_TIMESTAMP);
		else
			*ptr++ = __constant_htonl((TCPOPT_NOP << 24) | (TCPOPT_NOP << 16) |
						  (TCPOPT_TIMESTAMP << 8) | TCPOLEN_TIMESTAMP);
		*ptr++ = htonl(tstamp);		/* TSVAL */
		*ptr++ = htonl(ts_recent);	/* TSECR */
	} else if(sack)
		*ptr++ = __constant_htonl((TCPOPT_NOP << 24) | (TCPOPT_NOP << 16) |
					  (TCPOPT_SACK_PERM << 8) | TCPOLEN_SACK_PERM);
	if (offer_wscale)
		*ptr++ = htonl((TCPOPT_NOP << 24) | (TCPOPT_WINDOW << 16) | (TCPOLEN_WINDOW << 8) | (wscale));
}

/* Determine a window scaling and initial window to offer. */
extern void tcp_select_initial_window(int __space, __u32 mss,
				      __u32 *rcv_wnd, __u32 *window_clamp,
				      int wscale_ok, __u8 *rcv_wscale);

static inline int tcp_win_from_space(int space)
{
	return sysctl_tcp_adv_win_scale<=0 ?
		(space>>(-sysctl_tcp_adv_win_scale)) :
		space - (space>>sysctl_tcp_adv_win_scale);
}

/* Note: caller must be prepared to deal with negative returns */ 
static inline int tcp_space(const struct sock *sk)
{
	return tcp_win_from_space(sk->sk_rcvbuf -
				  atomic_read(&sk->sk_rmem_alloc));
} 

static inline int tcp_full_space(const struct sock *sk)
{
	return tcp_win_from_space(sk->sk_rcvbuf); 
}

static inline void tcp_acceptq_queue(struct sock *sk, struct request_sock *req,
					 struct sock *child)
{
	reqsk_queue_add(&tcp_sk(sk)->accept_queue, req, sk, child);
}

static inline void
tcp_synq_removed(struct sock *sk, struct request_sock *req)
{
	if (reqsk_queue_removed(&tcp_sk(sk)->accept_queue, req) == 0)
		tcp_delete_keepalive_timer(sk);
}

static inline void tcp_synq_added(struct sock *sk)
{
	if (reqsk_queue_added(&tcp_sk(sk)->accept_queue) == 0)
		tcp_reset_keepalive_timer(sk, TCP_TIMEOUT_INIT);
}

static inline int tcp_synq_len(struct sock *sk)
{
	return reqsk_queue_len(&tcp_sk(sk)->accept_queue);
}

static inline int tcp_synq_young(struct sock *sk)
{
	return reqsk_queue_len_young(&tcp_sk(sk)->accept_queue);
}

static inline int tcp_synq_is_full(struct sock *sk)
{
	return reqsk_queue_is_full(&tcp_sk(sk)->accept_queue);
}

static inline void tcp_synq_unlink(struct tcp_sock *tp, struct request_sock *req,
				   struct request_sock **prev)
{
	reqsk_queue_unlink(&tp->accept_queue, req, prev);
}

static inline void tcp_synq_drop(struct sock *sk, struct request_sock *req,
				     struct request_sock **prev)
{
	tcp_synq_unlink(tcp_sk(sk), req, prev);
	tcp_synq_removed(sk, req);
	reqsk_free(req);
}

static __inline__ void tcp_openreq_init(struct request_sock *req,
					struct tcp_options_received *rx_opt,
					struct sk_buff *skb)
{
	struct inet_request_sock *ireq = inet_rsk(req);

	req->rcv_wnd = 0;		/* So that tcp_send_synack() knows! */
	tcp_rsk(req)->rcv_isn = TCP_SKB_CB(skb)->seq;
	req->mss = rx_opt->mss_clamp;
	req->ts_recent = rx_opt->saw_tstamp ? rx_opt->rcv_tsval : 0;
	ireq->tstamp_ok = rx_opt->tstamp_ok;
	ireq->sack_ok = rx_opt->sack_ok;
	ireq->snd_wscale = rx_opt->snd_wscale;
	ireq->wscale_ok = rx_opt->wscale_ok;
	ireq->acked = 0;
	ireq->ecn_ok = 0;
	ireq->rmt_port = skb->h.th->source;
}

extern void tcp_enter_memory_pressure(void);

extern void tcp_listen_wlock(void);

/* - We may sleep inside this lock.
 * - If sleeping is not required (or called from BH),
 *   use plain read_(un)lock(&tcp_lhash_lock).
 */

static inline void tcp_listen_lock(void)
{
	/* read_lock synchronizes to candidates to writers */
	read_lock(&tcp_lhash_lock);
	atomic_inc(&tcp_lhash_users);
	read_unlock(&tcp_lhash_lock);
}

static inline void tcp_listen_unlock(void)
{
	if (atomic_dec_and_test(&tcp_lhash_users))
		wake_up(&tcp_lhash_wait);
}

static inline int keepalive_intvl_when(const struct tcp_sock *tp)
{
	return tp->keepalive_intvl ? : sysctl_tcp_keepalive_intvl;
}

static inline int keepalive_time_when(const struct tcp_sock *tp)
{
	return tp->keepalive_time ? : sysctl_tcp_keepalive_time;
}

static inline int tcp_fin_time(const struct tcp_sock *tp)
{
	int fin_timeout = tp->linger2 ? : sysctl_tcp_fin_timeout;

	if (fin_timeout < (tp->rto<<2) - (tp->rto>>1))
		fin_timeout = (tp->rto<<2) - (tp->rto>>1);

	return fin_timeout;
}

static inline int tcp_paws_check(const struct tcp_options_received *rx_opt, int rst)
{
	if ((s32)(rx_opt->rcv_tsval - rx_opt->ts_recent) >= 0)
		return 0;
	if (xtime.tv_sec >= rx_opt->ts_recent_stamp + TCP_PAWS_24DAYS)
		return 0;

	/* RST segments are not recommended to carry timestamp,
	   and, if they do, it is recommended to ignore PAWS because
	   "their cleanup function should take precedence over timestamps."
	   Certainly, it is mistake. It is necessary to understand the reasons
	   of this constraint to relax it: if peer reboots, clock may go
	   out-of-sync and half-open connections will not be reset.
	   Actually, the problem would be not existing if all
	   the implementations followed draft about maintaining clock
	   via reboots. Linux-2.2 DOES NOT!

	   However, we can relax time bounds for RST segments to MSL.
	 */
	if (rst && xtime.tv_sec >= rx_opt->ts_recent_stamp + TCP_PAWS_MSL)
		return 0;
	return 1;
}

static inline void tcp_v4_setup_caps(struct sock *sk, struct dst_entry *dst)
{
	sk->sk_route_caps = dst->dev->features;
	if (sk->sk_route_caps & NETIF_F_TSO) {
		if (sock_flag(sk, SOCK_NO_LARGESEND) || dst->header_len)
			sk->sk_route_caps &= ~NETIF_F_TSO;
	}
}

#define TCP_CHECK_TIMER(sk) do { } while (0)

static inline int tcp_use_frto(const struct sock *sk)
{
	const struct tcp_sock *tp = tcp_sk(sk);
	
	/* F-RTO must be activated in sysctl and there must be some
	 * unsent new data, and the advertised window should allow
	 * sending it.
	 */
	return (sysctl_tcp_frto && sk->sk_send_head &&
		!after(TCP_SKB_CB(sk->sk_send_head)->end_seq,
		       tp->snd_una + tp->snd_wnd));
}

static inline void tcp_mib_init(void)
{
	/* See RFC 2012 */
	TCP_ADD_STATS_USER(TCP_MIB_RTOALGORITHM, 1);
	TCP_ADD_STATS_USER(TCP_MIB_RTOMIN, TCP_RTO_MIN*1000/HZ);
	TCP_ADD_STATS_USER(TCP_MIB_RTOMAX, TCP_RTO_MAX*1000/HZ);
	TCP_ADD_STATS_USER(TCP_MIB_MAXCONN, -1);
}

/* /proc */
enum tcp_seq_states {
	TCP_SEQ_STATE_LISTENING,
	TCP_SEQ_STATE_OPENREQ,
	TCP_SEQ_STATE_ESTABLISHED,
	TCP_SEQ_STATE_TIME_WAIT,
};

struct tcp_seq_afinfo {
	struct module		*owner;
	char			*name;
	sa_family_t		family;
	int			(*seq_show) (struct seq_file *m, void *v);
	struct file_operations	*seq_fops;
};

struct tcp_iter_state {
	sa_family_t		family;
	enum tcp_seq_states	state;
	struct sock		*syn_wait_sk;
	int			bucket, sbucket, num, uid;
	struct seq_operations	seq_ops;
};

extern int tcp_proc_register(struct tcp_seq_afinfo *afinfo);
extern void tcp_proc_unregister(struct tcp_seq_afinfo *afinfo);

#endif	/* _TCP_H */
