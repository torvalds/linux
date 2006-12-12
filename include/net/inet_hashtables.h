/*
 * INET		An implementation of the TCP/IP protocol suite for the LINUX
 *		operating system.  INET is implemented using the BSD Socket
 *		interface as the means of communication with the user level.
 *
 * Authors:	Lotsa people, from code originally in tcp
 *
 *	This program is free software; you can redistribute it and/or
 *      modify it under the terms of the GNU General Public License
 *      as published by the Free Software Foundation; either version
 *      2 of the License, or (at your option) any later version.
 */

#ifndef _INET_HASHTABLES_H
#define _INET_HASHTABLES_H


#include <linux/interrupt.h>
#include <linux/ipv6.h>
#include <linux/list.h>
#include <linux/slab.h>
#include <linux/socket.h>
#include <linux/spinlock.h>
#include <linux/types.h>
#include <linux/wait.h>

#include <net/inet_connection_sock.h>
#include <net/inet_sock.h>
#include <net/route.h>
#include <net/sock.h>
#include <net/tcp_states.h>

#include <asm/atomic.h>
#include <asm/byteorder.h>

/* This is for all connections with a full identity, no wildcards.
 * New scheme, half the table is for TIME_WAIT, the other half is
 * for the rest.  I'll experiment with dynamic table growth later.
 */
struct inet_ehash_bucket {
	rwlock_t	  lock;
	struct hlist_head chain;
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
struct inet_bind_bucket {
	unsigned short		port;
	signed short		fastreuse;
	struct hlist_node	node;
	struct hlist_head	owners;
};

#define inet_bind_bucket_for_each(tb, node, head) \
	hlist_for_each_entry(tb, node, head, node)

struct inet_bind_hashbucket {
	spinlock_t		lock;
	struct hlist_head	chain;
};

/* This is for listening sockets, thus all sockets which possess wildcards. */
#define INET_LHTABLE_SIZE	32	/* Yes, really, this is all you need. */

struct inet_hashinfo {
	/* This is for sockets with full identity only.  Sockets here will
	 * always be without wildcards and will have the following invariant:
	 *
	 *          TCP_ESTABLISHED <= sk->sk_state < TCP_CLOSE
	 *
	 * First half of the table is for sockets not in TIME_WAIT, second half
	 * is for TIME_WAIT sockets only.
	 */
	struct inet_ehash_bucket	*ehash;

	/* Ok, let's try this, I give up, we do need a local binding
	 * TCP hash as well as the others for fast bind/connect.
	 */
	struct inet_bind_hashbucket	*bhash;

	int				bhash_size;
	unsigned int			ehash_size;

	/* All sockets in TCP_LISTEN state will be in here.  This is the only
	 * table where wildcard'd TCP sockets can exist.  Hash function here
	 * is just local port number.
	 */
	struct hlist_head		listening_hash[INET_LHTABLE_SIZE];

	/* All the above members are written once at bootup and
	 * never written again _or_ are predominantly read-access.
	 *
	 * Now align to a new cache line as all the following members
	 * are often dirty.
	 */
	rwlock_t			lhash_lock ____cacheline_aligned;
	atomic_t			lhash_users;
	wait_queue_head_t		lhash_wait;
	struct kmem_cache			*bind_bucket_cachep;
};

static inline struct inet_ehash_bucket *inet_ehash_bucket(
	struct inet_hashinfo *hashinfo,
	unsigned int hash)
{
	return &hashinfo->ehash[hash & (hashinfo->ehash_size - 1)];
}

extern struct inet_bind_bucket *
		    inet_bind_bucket_create(struct kmem_cache *cachep,
					    struct inet_bind_hashbucket *head,
					    const unsigned short snum);
extern void inet_bind_bucket_destroy(struct kmem_cache *cachep,
				     struct inet_bind_bucket *tb);

static inline int inet_bhashfn(const __u16 lport, const int bhash_size)
{
	return lport & (bhash_size - 1);
}

extern void inet_bind_hash(struct sock *sk, struct inet_bind_bucket *tb,
			   const unsigned short snum);

/* These can have wildcards, don't try too hard. */
static inline int inet_lhashfn(const unsigned short num)
{
	return num & (INET_LHTABLE_SIZE - 1);
}

static inline int inet_sk_listen_hashfn(const struct sock *sk)
{
	return inet_lhashfn(inet_sk(sk)->num);
}

/* Caller must disable local BH processing. */
static inline void __inet_inherit_port(struct inet_hashinfo *table,
				       struct sock *sk, struct sock *child)
{
	const int bhash = inet_bhashfn(inet_sk(child)->num, table->bhash_size);
	struct inet_bind_hashbucket *head = &table->bhash[bhash];
	struct inet_bind_bucket *tb;

	spin_lock(&head->lock);
	tb = inet_csk(sk)->icsk_bind_hash;
	sk_add_bind_node(child, &tb->owners);
	inet_csk(child)->icsk_bind_hash = tb;
	spin_unlock(&head->lock);
}

static inline void inet_inherit_port(struct inet_hashinfo *table,
				     struct sock *sk, struct sock *child)
{
	local_bh_disable();
	__inet_inherit_port(table, sk, child);
	local_bh_enable();
}

extern void inet_put_port(struct inet_hashinfo *table, struct sock *sk);

extern void inet_listen_wlock(struct inet_hashinfo *hashinfo);

/*
 * - We may sleep inside this lock.
 * - If sleeping is not required (or called from BH),
 *   use plain read_(un)lock(&inet_hashinfo.lhash_lock).
 */
static inline void inet_listen_lock(struct inet_hashinfo *hashinfo)
{
	/* read_lock synchronizes to candidates to writers */
	read_lock(&hashinfo->lhash_lock);
	atomic_inc(&hashinfo->lhash_users);
	read_unlock(&hashinfo->lhash_lock);
}

static inline void inet_listen_unlock(struct inet_hashinfo *hashinfo)
{
	if (atomic_dec_and_test(&hashinfo->lhash_users))
		wake_up(&hashinfo->lhash_wait);
}

static inline void __inet_hash(struct inet_hashinfo *hashinfo,
			       struct sock *sk, const int listen_possible)
{
	struct hlist_head *list;
	rwlock_t *lock;

	BUG_TRAP(sk_unhashed(sk));
	if (listen_possible && sk->sk_state == TCP_LISTEN) {
		list = &hashinfo->listening_hash[inet_sk_listen_hashfn(sk)];
		lock = &hashinfo->lhash_lock;
		inet_listen_wlock(hashinfo);
	} else {
		struct inet_ehash_bucket *head;
		sk->sk_hash = inet_sk_ehashfn(sk);
		head = inet_ehash_bucket(hashinfo, sk->sk_hash);
		list = &head->chain;
		lock = &head->lock;
		write_lock(lock);
	}
	__sk_add_node(sk, list);
	sock_prot_inc_use(sk->sk_prot);
	write_unlock(lock);
	if (listen_possible && sk->sk_state == TCP_LISTEN)
		wake_up(&hashinfo->lhash_wait);
}

static inline void inet_hash(struct inet_hashinfo *hashinfo, struct sock *sk)
{
	if (sk->sk_state != TCP_CLOSE) {
		local_bh_disable();
		__inet_hash(hashinfo, sk, 1);
		local_bh_enable();
	}
}

static inline void inet_unhash(struct inet_hashinfo *hashinfo, struct sock *sk)
{
	rwlock_t *lock;

	if (sk_unhashed(sk))
		goto out;

	if (sk->sk_state == TCP_LISTEN) {
		local_bh_disable();
		inet_listen_wlock(hashinfo);
		lock = &hashinfo->lhash_lock;
	} else {
		lock = &inet_ehash_bucket(hashinfo, sk->sk_hash)->lock;
		write_lock_bh(lock);
	}

	if (__sk_del_node_init(sk))
		sock_prot_dec_use(sk->sk_prot);
	write_unlock_bh(lock);
out:
	if (sk->sk_state == TCP_LISTEN)
		wake_up(&hashinfo->lhash_wait);
}

static inline int inet_iif(const struct sk_buff *skb)
{
	return ((struct rtable *)skb->dst)->rt_iif;
}

extern struct sock *__inet_lookup_listener(struct inet_hashinfo *hashinfo,
					   const __be32 daddr,
					   const unsigned short hnum,
					   const int dif);

static inline struct sock *inet_lookup_listener(struct inet_hashinfo *hashinfo,
						__be32 daddr, __be16 dport, int dif)
{
	return __inet_lookup_listener(hashinfo, daddr, ntohs(dport), dif);
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
typedef __u32 __bitwise __portpair;
#ifdef __BIG_ENDIAN
#define INET_COMBINED_PORTS(__sport, __dport) \
	((__force __portpair)(((__force __u32)(__be16)(__sport) << 16) | (__u32)(__dport)))
#else /* __LITTLE_ENDIAN */
#define INET_COMBINED_PORTS(__sport, __dport) \
	((__force __portpair)(((__u32)(__dport) << 16) | (__force __u32)(__be16)(__sport)))
#endif

#if (BITS_PER_LONG == 64)
typedef __u64 __bitwise __addrpair;
#ifdef __BIG_ENDIAN
#define INET_ADDR_COOKIE(__name, __saddr, __daddr) \
	const __addrpair __name = (__force __addrpair) ( \
				   (((__force __u64)(__be32)(__saddr)) << 32) | \
				   ((__force __u64)(__be32)(__daddr)));
#else /* __LITTLE_ENDIAN */
#define INET_ADDR_COOKIE(__name, __saddr, __daddr) \
	const __addrpair __name = (__force __addrpair) ( \
				   (((__force __u64)(__be32)(__daddr)) << 32) | \
				   ((__force __u64)(__be32)(__saddr)));
#endif /* __BIG_ENDIAN */
#define INET_MATCH(__sk, __hash, __cookie, __saddr, __daddr, __ports, __dif)\
	(((__sk)->sk_hash == (__hash))				&&	\
	 ((*((__addrpair *)&(inet_sk(__sk)->daddr))) == (__cookie))	&&	\
	 ((*((__portpair *)&(inet_sk(__sk)->dport))) == (__ports))	&&	\
	 (!((__sk)->sk_bound_dev_if) || ((__sk)->sk_bound_dev_if == (__dif))))
#define INET_TW_MATCH(__sk, __hash, __cookie, __saddr, __daddr, __ports, __dif)\
	(((__sk)->sk_hash == (__hash))				&&	\
	 ((*((__addrpair *)&(inet_twsk(__sk)->tw_daddr))) == (__cookie)) &&	\
	 ((*((__portpair *)&(inet_twsk(__sk)->tw_dport))) == (__ports)) &&	\
	 (!((__sk)->sk_bound_dev_if) || ((__sk)->sk_bound_dev_if == (__dif))))
#else /* 32-bit arch */
#define INET_ADDR_COOKIE(__name, __saddr, __daddr)
#define INET_MATCH(__sk, __hash, __cookie, __saddr, __daddr, __ports, __dif)	\
	(((__sk)->sk_hash == (__hash))				&&	\
	 (inet_sk(__sk)->daddr		== (__saddr))		&&	\
	 (inet_sk(__sk)->rcv_saddr	== (__daddr))		&&	\
	 ((*((__portpair *)&(inet_sk(__sk)->dport))) == (__ports))	&&	\
	 (!((__sk)->sk_bound_dev_if) || ((__sk)->sk_bound_dev_if == (__dif))))
#define INET_TW_MATCH(__sk, __hash,__cookie, __saddr, __daddr, __ports, __dif)	\
	(((__sk)->sk_hash == (__hash))				&&	\
	 (inet_twsk(__sk)->tw_daddr	== (__saddr))		&&	\
	 (inet_twsk(__sk)->tw_rcv_saddr	== (__daddr))		&&	\
	 ((*((__portpair *)&(inet_twsk(__sk)->tw_dport))) == (__ports)) &&	\
	 (!((__sk)->sk_bound_dev_if) || ((__sk)->sk_bound_dev_if == (__dif))))
#endif /* 64-bit arch */

/*
 * Sockets in TCP_CLOSE state are _always_ taken out of the hash, so we need
 * not check it for lookups anymore, thanks Alexey. -DaveM
 *
 * Local BH must be disabled here.
 */
static inline struct sock *
	__inet_lookup_established(struct inet_hashinfo *hashinfo,
				  const __be32 saddr, const __be16 sport,
				  const __be32 daddr, const u16 hnum,
				  const int dif)
{
	INET_ADDR_COOKIE(acookie, saddr, daddr)
	const __portpair ports = INET_COMBINED_PORTS(sport, hnum);
	struct sock *sk;
	const struct hlist_node *node;
	/* Optimize here for direct hit, only listening connections can
	 * have wildcards anyways.
	 */
	unsigned int hash = inet_ehashfn(daddr, hnum, saddr, sport);
	struct inet_ehash_bucket *head = inet_ehash_bucket(hashinfo, hash);

	prefetch(head->chain.first);
	read_lock(&head->lock);
	sk_for_each(sk, node, &head->chain) {
		if (INET_MATCH(sk, hash, acookie, saddr, daddr, ports, dif))
			goto hit; /* You sunk my battleship! */
	}

	/* Must check for a TIME_WAIT'er before going to listener hash. */
	sk_for_each(sk, node, &(head + hashinfo->ehash_size)->chain) {
		if (INET_TW_MATCH(sk, hash, acookie, saddr, daddr, ports, dif))
			goto hit;
	}
	sk = NULL;
out:
	read_unlock(&head->lock);
	return sk;
hit:
	sock_hold(sk);
	goto out;
}

static inline struct sock *
	inet_lookup_established(struct inet_hashinfo *hashinfo,
				const __be32 saddr, const __be16 sport,
				const __be32 daddr, const __be16 dport,
				const int dif)
{
	return __inet_lookup_established(hashinfo, saddr, sport, daddr,
					 ntohs(dport), dif);
}

static inline struct sock *__inet_lookup(struct inet_hashinfo *hashinfo,
					 const __be32 saddr, const __be16 sport,
					 const __be32 daddr, const __be16 dport,
					 const int dif)
{
	u16 hnum = ntohs(dport);
	struct sock *sk = __inet_lookup_established(hashinfo, saddr, sport, daddr,
						    hnum, dif);
	return sk ? : __inet_lookup_listener(hashinfo, daddr, hnum, dif);
}

static inline struct sock *inet_lookup(struct inet_hashinfo *hashinfo,
				       const __be32 saddr, const __be16 sport,
				       const __be32 daddr, const __be16 dport,
				       const int dif)
{
	struct sock *sk;

	local_bh_disable();
	sk = __inet_lookup(hashinfo, saddr, sport, daddr, dport, dif);
	local_bh_enable();

	return sk;
}

extern int inet_hash_connect(struct inet_timewait_death_row *death_row,
			     struct sock *sk);
#endif /* _INET_HASHTABLES_H */
