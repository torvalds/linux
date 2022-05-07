/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * INET		An implementation of the TCP/IP protocol suite for the LINUX
 *		operating system.  INET is implemented using the BSD Socket
 *		interface as the means of communication with the user level.
 *
 * Authors:	Lotsa people, from code originally in tcp
 */

#ifndef _INET_HASHTABLES_H
#define _INET_HASHTABLES_H


#include <linux/interrupt.h>
#include <linux/ip.h>
#include <linux/ipv6.h>
#include <linux/list.h>
#include <linux/slab.h>
#include <linux/socket.h>
#include <linux/spinlock.h>
#include <linux/types.h>
#include <linux/wait.h>

#include <net/inet_connection_sock.h>
#include <net/inet_sock.h>
#include <net/sock.h>
#include <net/route.h>
#include <net/tcp_states.h>
#include <net/netns/hash.h>

#include <linux/refcount.h>
#include <asm/byteorder.h>

/* This is for all connections with a full identity, no wildcards.
 * The 'e' prefix stands for Establish, but we really put all sockets
 * but LISTEN ones.
 */
struct inet_ehash_bucket {
	struct hlist_nulls_head chain;
};

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
#define FASTREUSEPORT_ANY	1
#define FASTREUSEPORT_STRICT	2

struct inet_bind_bucket {
	possible_net_t		ib_net;
	int			l3mdev;
	unsigned short		port;
	signed char		fastreuse;
	signed char		fastreuseport;
	kuid_t			fastuid;
#if IS_ENABLED(CONFIG_IPV6)
	struct in6_addr		fast_v6_rcv_saddr;
#endif
	__be32			fast_rcv_saddr;
	unsigned short		fast_sk_family;
	bool			fast_ipv6_only;
	struct hlist_node	node;
	struct hlist_head	owners;
};

static inline struct net *ib_net(struct inet_bind_bucket *ib)
{
	return read_pnet(&ib->ib_net);
}

#define inet_bind_bucket_for_each(tb, head) \
	hlist_for_each_entry(tb, head, node)

struct inet_bind_hashbucket {
	spinlock_t		lock;
	struct hlist_head	chain;
};

/* Sockets can be hashed in established or listening table.
 * We must use different 'nulls' end-of-chain value for all hash buckets :
 * A socket might transition from ESTABLISH to LISTEN state without
 * RCU grace period. A lookup in ehash table needs to handle this case.
 */
#define LISTENING_NULLS_BASE (1U << 29)
struct inet_listen_hashbucket {
	spinlock_t		lock;
	unsigned int		count;
	union {
		struct hlist_head	head;
		struct hlist_nulls_head	nulls_head;
	};
};

/* This is for listening sockets, thus all sockets which possess wildcards. */
#define INET_LHTABLE_SIZE	32	/* Yes, really, this is all you need. */

struct inet_hashinfo {
	/* This is for sockets with full identity only.  Sockets here will
	 * always be without wildcards and will have the following invariant:
	 *
	 *          TCP_ESTABLISHED <= sk->sk_state < TCP_CLOSE
	 *
	 */
	struct inet_ehash_bucket	*ehash;
	spinlock_t			*ehash_locks;
	unsigned int			ehash_mask;
	unsigned int			ehash_locks_mask;

	/* Ok, let's try this, I give up, we do need a local binding
	 * TCP hash as well as the others for fast bind/connect.
	 */
	struct kmem_cache		*bind_bucket_cachep;
	struct inet_bind_hashbucket	*bhash;
	unsigned int			bhash_size;

	/* The 2nd listener table hashed by local port and address */
	unsigned int			lhash2_mask;
	struct inet_listen_hashbucket	*lhash2;

	/* All the above members are written once at bootup and
	 * never written again _or_ are predominantly read-access.
	 *
	 * Now align to a new cache line as all the following members
	 * might be often dirty.
	 */
	/* All sockets in TCP_LISTEN state will be in listening_hash.
	 * This is the only table where wildcard'd TCP sockets can
	 * exist.  listening_hash is only hashed by local port number.
	 * If lhash2 is initialized, the same socket will also be hashed
	 * to lhash2 by port and address.
	 */
	struct inet_listen_hashbucket	listening_hash[INET_LHTABLE_SIZE]
					____cacheline_aligned_in_smp;
};

#define inet_lhash2_for_each_icsk_rcu(__icsk, list) \
	hlist_for_each_entry_rcu(__icsk, list, icsk_listen_portaddr_node)

static inline struct inet_listen_hashbucket *
inet_lhash2_bucket(struct inet_hashinfo *h, u32 hash)
{
	return &h->lhash2[hash & h->lhash2_mask];
}

static inline struct inet_ehash_bucket *inet_ehash_bucket(
	struct inet_hashinfo *hashinfo,
	unsigned int hash)
{
	return &hashinfo->ehash[hash & hashinfo->ehash_mask];
}

static inline spinlock_t *inet_ehash_lockp(
	struct inet_hashinfo *hashinfo,
	unsigned int hash)
{
	return &hashinfo->ehash_locks[hash & hashinfo->ehash_locks_mask];
}

int inet_ehash_locks_alloc(struct inet_hashinfo *hashinfo);

static inline void inet_hashinfo2_free_mod(struct inet_hashinfo *h)
{
	kfree(h->lhash2);
	h->lhash2 = NULL;
}

static inline void inet_ehash_locks_free(struct inet_hashinfo *hashinfo)
{
	kvfree(hashinfo->ehash_locks);
	hashinfo->ehash_locks = NULL;
}

static inline bool inet_sk_bound_dev_eq(struct net *net, int bound_dev_if,
					int dif, int sdif)
{
#if IS_ENABLED(CONFIG_NET_L3_MASTER_DEV)
	return inet_bound_dev_eq(!!net->ipv4.sysctl_tcp_l3mdev_accept,
				 bound_dev_if, dif, sdif);
#else
	return inet_bound_dev_eq(true, bound_dev_if, dif, sdif);
#endif
}

struct inet_bind_bucket *
inet_bind_bucket_create(struct kmem_cache *cachep, struct net *net,
			struct inet_bind_hashbucket *head,
			const unsigned short snum, int l3mdev);
void inet_bind_bucket_destroy(struct kmem_cache *cachep,
			      struct inet_bind_bucket *tb);

static inline u32 inet_bhashfn(const struct net *net, const __u16 lport,
			       const u32 bhash_size)
{
	return (lport + net_hash_mix(net)) & (bhash_size - 1);
}

void inet_bind_hash(struct sock *sk, struct inet_bind_bucket *tb,
		    const unsigned short snum);

/* These can have wildcards, don't try too hard. */
static inline u32 inet_lhashfn(const struct net *net, const unsigned short num)
{
	return (num + net_hash_mix(net)) & (INET_LHTABLE_SIZE - 1);
}

static inline int inet_sk_listen_hashfn(const struct sock *sk)
{
	return inet_lhashfn(sock_net(sk), inet_sk(sk)->inet_num);
}

/* Caller must disable local BH processing. */
int __inet_inherit_port(const struct sock *sk, struct sock *child);

void inet_put_port(struct sock *sk);

void inet_hashinfo_init(struct inet_hashinfo *h);
void inet_hashinfo2_init(struct inet_hashinfo *h, const char *name,
			 unsigned long numentries, int scale,
			 unsigned long low_limit,
			 unsigned long high_limit);
int inet_hashinfo2_init_mod(struct inet_hashinfo *h);

bool inet_ehash_insert(struct sock *sk, struct sock *osk, bool *found_dup_sk);
bool inet_ehash_nolisten(struct sock *sk, struct sock *osk,
			 bool *found_dup_sk);
int __inet_hash(struct sock *sk, struct sock *osk);
int inet_hash(struct sock *sk);
void inet_unhash(struct sock *sk);

struct sock *__inet_lookup_listener(struct net *net,
				    struct inet_hashinfo *hashinfo,
				    struct sk_buff *skb, int doff,
				    const __be32 saddr, const __be16 sport,
				    const __be32 daddr,
				    const unsigned short hnum,
				    const int dif, const int sdif);

static inline struct sock *inet_lookup_listener(struct net *net,
		struct inet_hashinfo *hashinfo,
		struct sk_buff *skb, int doff,
		__be32 saddr, __be16 sport,
		__be32 daddr, __be16 dport, int dif, int sdif)
{
	return __inet_lookup_listener(net, hashinfo, skb, doff, saddr, sport,
				      daddr, ntohs(dport), dif, sdif);
}

/* Socket demux engine toys. */
/* What happens here is ugly; there's a pair of adjacent fields in
   struct inet_sock; __be16 dport followed by __u16 num.  We want to
   search by pair, so we combine the keys into a single 32bit value
   and compare with 32bit value read from &...->dport.  Let's at least
   make sure that it's not mixed with anything else...
   On 64bit targets we combine comparisons with pair of adjacent __be32
   fields in the same way.
*/
#ifdef __BIG_ENDIAN
#define INET_COMBINED_PORTS(__sport, __dport) \
	((__force __portpair)(((__force __u32)(__be16)(__sport) << 16) | (__u32)(__dport)))
#else /* __LITTLE_ENDIAN */
#define INET_COMBINED_PORTS(__sport, __dport) \
	((__force __portpair)(((__u32)(__dport) << 16) | (__force __u32)(__be16)(__sport)))
#endif

#if (BITS_PER_LONG == 64)
#ifdef __BIG_ENDIAN
#define INET_ADDR_COOKIE(__name, __saddr, __daddr) \
	const __addrpair __name = (__force __addrpair) ( \
				   (((__force __u64)(__be32)(__saddr)) << 32) | \
				   ((__force __u64)(__be32)(__daddr)))
#else /* __LITTLE_ENDIAN */
#define INET_ADDR_COOKIE(__name, __saddr, __daddr) \
	const __addrpair __name = (__force __addrpair) ( \
				   (((__force __u64)(__be32)(__daddr)) << 32) | \
				   ((__force __u64)(__be32)(__saddr)))
#endif /* __BIG_ENDIAN */
#define INET_MATCH(__sk, __net, __cookie, __saddr, __daddr, __ports, __dif, __sdif) \
	(((__sk)->sk_portpair == (__ports))			&&	\
	 ((__sk)->sk_addrpair == (__cookie))			&&	\
	 (((__sk)->sk_bound_dev_if == (__dif))			||	\
	  ((__sk)->sk_bound_dev_if == (__sdif)))		&&	\
	 net_eq(sock_net(__sk), (__net)))
#else /* 32-bit arch */
#define INET_ADDR_COOKIE(__name, __saddr, __daddr) \
	const int __name __deprecated __attribute__((unused))

#define INET_MATCH(__sk, __net, __cookie, __saddr, __daddr, __ports, __dif, __sdif) \
	(((__sk)->sk_portpair == (__ports))		&&		\
	 ((__sk)->sk_daddr	== (__saddr))		&&		\
	 ((__sk)->sk_rcv_saddr	== (__daddr))		&&		\
	 (((__sk)->sk_bound_dev_if == (__dif))		||		\
	  ((__sk)->sk_bound_dev_if == (__sdif)))	&&		\
	 net_eq(sock_net(__sk), (__net)))
#endif /* 64-bit arch */

/* Sockets in TCP_CLOSE state are _always_ taken out of the hash, so we need
 * not check it for lookups anymore, thanks Alexey. -DaveM
 */
struct sock *__inet_lookup_established(struct net *net,
				       struct inet_hashinfo *hashinfo,
				       const __be32 saddr, const __be16 sport,
				       const __be32 daddr, const u16 hnum,
				       const int dif, const int sdif);

static inline struct sock *
	inet_lookup_established(struct net *net, struct inet_hashinfo *hashinfo,
				const __be32 saddr, const __be16 sport,
				const __be32 daddr, const __be16 dport,
				const int dif)
{
	return __inet_lookup_established(net, hashinfo, saddr, sport, daddr,
					 ntohs(dport), dif, 0);
}

static inline struct sock *__inet_lookup(struct net *net,
					 struct inet_hashinfo *hashinfo,
					 struct sk_buff *skb, int doff,
					 const __be32 saddr, const __be16 sport,
					 const __be32 daddr, const __be16 dport,
					 const int dif, const int sdif,
					 bool *refcounted)
{
	u16 hnum = ntohs(dport);
	struct sock *sk;

	sk = __inet_lookup_established(net, hashinfo, saddr, sport,
				       daddr, hnum, dif, sdif);
	*refcounted = true;
	if (sk)
		return sk;
	*refcounted = false;
	return __inet_lookup_listener(net, hashinfo, skb, doff, saddr,
				      sport, daddr, hnum, dif, sdif);
}

static inline struct sock *inet_lookup(struct net *net,
				       struct inet_hashinfo *hashinfo,
				       struct sk_buff *skb, int doff,
				       const __be32 saddr, const __be16 sport,
				       const __be32 daddr, const __be16 dport,
				       const int dif)
{
	struct sock *sk;
	bool refcounted;

	sk = __inet_lookup(net, hashinfo, skb, doff, saddr, sport, daddr,
			   dport, dif, 0, &refcounted);

	if (sk && !refcounted && !refcount_inc_not_zero(&sk->sk_refcnt))
		sk = NULL;
	return sk;
}

static inline struct sock *__inet_lookup_skb(struct inet_hashinfo *hashinfo,
					     struct sk_buff *skb,
					     int doff,
					     const __be16 sport,
					     const __be16 dport,
					     const int sdif,
					     bool *refcounted)
{
	struct sock *sk = skb_steal_sock(skb, refcounted);
	const struct iphdr *iph = ip_hdr(skb);

	if (sk)
		return sk;

	return __inet_lookup(dev_net(skb_dst(skb)->dev), hashinfo, skb,
			     doff, iph->saddr, sport,
			     iph->daddr, dport, inet_iif(skb), sdif,
			     refcounted);
}

u32 inet6_ehashfn(const struct net *net,
		  const struct in6_addr *laddr, const u16 lport,
		  const struct in6_addr *faddr, const __be16 fport);

static inline void sk_daddr_set(struct sock *sk, __be32 addr)
{
	sk->sk_daddr = addr; /* alias of inet_daddr */
#if IS_ENABLED(CONFIG_IPV6)
	ipv6_addr_set_v4mapped(addr, &sk->sk_v6_daddr);
#endif
}

static inline void sk_rcv_saddr_set(struct sock *sk, __be32 addr)
{
	sk->sk_rcv_saddr = addr; /* alias of inet_rcv_saddr */
#if IS_ENABLED(CONFIG_IPV6)
	ipv6_addr_set_v4mapped(addr, &sk->sk_v6_rcv_saddr);
#endif
}

int __inet_hash_connect(struct inet_timewait_death_row *death_row,
			struct sock *sk, u32 port_offset,
			int (*check_established)(struct inet_timewait_death_row *,
						 struct sock *, __u16,
						 struct inet_timewait_sock **));

int inet_hash_connect(struct inet_timewait_death_row *death_row,
		      struct sock *sk);
#endif /* _INET_HASHTABLES_H */
