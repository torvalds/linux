/*
 * INET		An implementation of the TCP/IP protocol suite for the LINUX
 *		operating system.  INET is implemented using the BSD Socket
 *		interface as the means of communication with the user level.
 *
 *		Generic INET transport hashtables
 *
 * Authors:	Lotsa people, from code originally in tcp
 *
 *	This program is free software; you can redistribute it and/or
 *      modify it under the terms of the GNU General Public License
 *      as published by the Free Software Foundation; either version
 *      2 of the License, or (at your option) any later version.
 */

#include <linux/module.h>
#include <linux/random.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/wait.h>
#include <linux/vmalloc.h>

#include <net/inet_connection_sock.h>
#include <net/inet_hashtables.h>
#include <net/secure_seq.h>
#include <net/ip.h>

static u32 inet_ehashfn(const struct net *net, const __be32 laddr,
			const __u16 lport, const __be32 faddr,
			const __be16 fport)
{
	static u32 inet_ehash_secret __read_mostly;

	net_get_random_once(&inet_ehash_secret, sizeof(inet_ehash_secret));

	return __inet_ehashfn(laddr, lport, faddr, fport,
			      inet_ehash_secret + net_hash_mix(net));
}

/* This function handles inet_sock, but also timewait and request sockets
 * for IPv4/IPv6.
 */
u32 sk_ehashfn(const struct sock *sk)
{
#if IS_ENABLED(CONFIG_IPV6)
	if (sk->sk_family == AF_INET6 &&
	    !ipv6_addr_v4mapped(&sk->sk_v6_daddr))
		return inet6_ehashfn(sock_net(sk),
				     &sk->sk_v6_rcv_saddr, sk->sk_num,
				     &sk->sk_v6_daddr, sk->sk_dport);
#endif
	return inet_ehashfn(sock_net(sk),
			    sk->sk_rcv_saddr, sk->sk_num,
			    sk->sk_daddr, sk->sk_dport);
}

/*
 * Allocate and initialize a new local port bind bucket.
 * The bindhash mutex for snum's hash chain must be held here.
 */
struct inet_bind_bucket *inet_bind_bucket_create(struct kmem_cache *cachep,
						 struct net *net,
						 struct inet_bind_hashbucket *head,
						 const unsigned short snum)
{
	struct inet_bind_bucket *tb = kmem_cache_alloc(cachep, GFP_ATOMIC);

	if (tb) {
		write_pnet(&tb->ib_net, net);
		tb->port      = snum;
		tb->fastreuse = 0;
		tb->fastreuseport = 0;
		tb->num_owners = 0;
		INIT_HLIST_HEAD(&tb->owners);
		hlist_add_head(&tb->node, &head->chain);
	}
	return tb;
}

/*
 * Caller must hold hashbucket lock for this tb with local BH disabled
 */
void inet_bind_bucket_destroy(struct kmem_cache *cachep, struct inet_bind_bucket *tb)
{
	if (hlist_empty(&tb->owners)) {
		__hlist_del(&tb->node);
		kmem_cache_free(cachep, tb);
	}
}

void inet_bind_hash(struct sock *sk, struct inet_bind_bucket *tb,
		    const unsigned short snum)
{
	inet_sk(sk)->inet_num = snum;
	sk_add_bind_node(sk, &tb->owners);
	tb->num_owners++;
	inet_csk(sk)->icsk_bind_hash = tb;
}

/*
 * Get rid of any references to a local port held by the given sock.
 */
static void __inet_put_port(struct sock *sk)
{
	struct inet_hashinfo *hashinfo = sk->sk_prot->h.hashinfo;
	const int bhash = inet_bhashfn(sock_net(sk), inet_sk(sk)->inet_num,
			hashinfo->bhash_size);
	struct inet_bind_hashbucket *head = &hashinfo->bhash[bhash];
	struct inet_bind_bucket *tb;

	spin_lock(&head->lock);
	tb = inet_csk(sk)->icsk_bind_hash;
	__sk_del_bind_node(sk);
	tb->num_owners--;
	inet_csk(sk)->icsk_bind_hash = NULL;
	inet_sk(sk)->inet_num = 0;
	inet_bind_bucket_destroy(hashinfo->bind_bucket_cachep, tb);
	spin_unlock(&head->lock);
}

void inet_put_port(struct sock *sk)
{
	local_bh_disable();
	__inet_put_port(sk);
	local_bh_enable();
}
EXPORT_SYMBOL(inet_put_port);

int __inet_inherit_port(struct sock *sk, struct sock *child)
{
	struct inet_hashinfo *table = sk->sk_prot->h.hashinfo;
	unsigned short port = inet_sk(child)->inet_num;
	const int bhash = inet_bhashfn(sock_net(sk), port,
			table->bhash_size);
	struct inet_bind_hashbucket *head = &table->bhash[bhash];
	struct inet_bind_bucket *tb;

	spin_lock(&head->lock);
	tb = inet_csk(sk)->icsk_bind_hash;
	if (tb->port != port) {
		/* NOTE: using tproxy and redirecting skbs to a proxy
		 * on a different listener port breaks the assumption
		 * that the listener socket's icsk_bind_hash is the same
		 * as that of the child socket. We have to look up or
		 * create a new bind bucket for the child here. */
		inet_bind_bucket_for_each(tb, &head->chain) {
			if (net_eq(ib_net(tb), sock_net(sk)) &&
			    tb->port == port)
				break;
		}
		if (!tb) {
			tb = inet_bind_bucket_create(table->bind_bucket_cachep,
						     sock_net(sk), head, port);
			if (!tb) {
				spin_unlock(&head->lock);
				return -ENOMEM;
			}
		}
	}
	inet_bind_hash(child, tb, port);
	spin_unlock(&head->lock);

	return 0;
}
EXPORT_SYMBOL_GPL(__inet_inherit_port);

static inline int compute_score(struct sock *sk, struct net *net,
				const unsigned short hnum, const __be32 daddr,
				const int dif)
{
	int score = -1;
	struct inet_sock *inet = inet_sk(sk);

	if (net_eq(sock_net(sk), net) && inet->inet_num == hnum &&
			!ipv6_only_sock(sk)) {
		__be32 rcv_saddr = inet->inet_rcv_saddr;
		score = sk->sk_family == PF_INET ? 2 : 1;
		if (rcv_saddr) {
			if (rcv_saddr != daddr)
				return -1;
			score += 4;
		}
		if (sk->sk_bound_dev_if) {
			if (sk->sk_bound_dev_if != dif)
				return -1;
			score += 4;
		}
	}
	return score;
}

/*
 * Don't inline this cruft. Here are some nice properties to exploit here. The
 * BSD API does not allow a listening sock to specify the remote port nor the
 * remote address for the connection. So always assume those are both
 * wildcarded during the search since they can never be otherwise.
 */


struct sock *__inet_lookup_listener(struct net *net,
				    struct inet_hashinfo *hashinfo,
				    const __be32 saddr, __be16 sport,
				    const __be32 daddr, const unsigned short hnum,
				    const int dif)
{
	struct sock *sk, *result;
	struct hlist_nulls_node *node;
	unsigned int hash = inet_lhashfn(net, hnum);
	struct inet_listen_hashbucket *ilb = &hashinfo->listening_hash[hash];
	int score, hiscore, matches = 0, reuseport = 0;
	u32 phash = 0;

	rcu_read_lock();
begin:
	result = NULL;
	hiscore = 0;
	sk_nulls_for_each_rcu(sk, node, &ilb->head) {
		score = compute_score(sk, net, hnum, daddr, dif);
		if (score > hiscore) {
			result = sk;
			hiscore = score;
			reuseport = sk->sk_reuseport;
			if (reuseport) {
				phash = inet_ehashfn(net, daddr, hnum,
						     saddr, sport);
				matches = 1;
			}
		} else if (score == hiscore && reuseport) {
			matches++;
			if (reciprocal_scale(phash, matches) == 0)
				result = sk;
			phash = next_pseudo_random32(phash);
		}
	}
	/*
	 * if the nulls value we got at the end of this lookup is
	 * not the expected one, we must restart lookup.
	 * We probably met an item that was moved to another chain.
	 */
	if (get_nulls_value(node) != hash + LISTENING_NULLS_BASE)
		goto begin;
	if (result) {
		if (unlikely(!atomic_inc_not_zero(&result->sk_refcnt)))
			result = NULL;
		else if (unlikely(compute_score(result, net, hnum, daddr,
				  dif) < hiscore)) {
			sock_put(result);
			goto begin;
		}
	}
	rcu_read_unlock();
	return result;
}
EXPORT_SYMBOL_GPL(__inet_lookup_listener);

/* All sockets share common refcount, but have different destructors */
void sock_gen_put(struct sock *sk)
{
	if (!atomic_dec_and_test(&sk->sk_refcnt))
		return;

	if (sk->sk_state == TCP_TIME_WAIT)
		inet_twsk_free(inet_twsk(sk));
	else if (sk->sk_state == TCP_NEW_SYN_RECV)
		reqsk_free(inet_reqsk(sk));
	else
		sk_free(sk);
}
EXPORT_SYMBOL_GPL(sock_gen_put);

void sock_edemux(struct sk_buff *skb)
{
	sock_gen_put(skb->sk);
}
EXPORT_SYMBOL(sock_edemux);

struct sock *__inet_lookup_established(struct net *net,
				  struct inet_hashinfo *hashinfo,
				  const __be32 saddr, const __be16 sport,
				  const __be32 daddr, const u16 hnum,
				  const int dif)
{
	INET_ADDR_COOKIE(acookie, saddr, daddr);
	const __portpair ports = INET_COMBINED_PORTS(sport, hnum);
	struct sock *sk;
	const struct hlist_nulls_node *node;
	/* Optimize here for direct hit, only listening connections can
	 * have wildcards anyways.
	 */
	unsigned int hash = inet_ehashfn(net, daddr, hnum, saddr, sport);
	unsigned int slot = hash & hashinfo->ehash_mask;
	struct inet_ehash_bucket *head = &hashinfo->ehash[slot];

	rcu_read_lock();
begin:
	sk_nulls_for_each_rcu(sk, node, &head->chain) {
		if (sk->sk_hash != hash)
			continue;
		if (likely(INET_MATCH(sk, net, acookie,
				      saddr, daddr, ports, dif))) {
			if (unlikely(!atomic_inc_not_zero(&sk->sk_refcnt)))
				goto out;
			if (unlikely(!INET_MATCH(sk, net, acookie,
						 saddr, daddr, ports, dif))) {
				sock_gen_put(sk);
				goto begin;
			}
			goto found;
		}
	}
	/*
	 * if the nulls value we got at the end of this lookup is
	 * not the expected one, we must restart lookup.
	 * We probably met an item that was moved to another chain.
	 */
	if (get_nulls_value(node) != slot)
		goto begin;
out:
	sk = NULL;
found:
	rcu_read_unlock();
	return sk;
}
EXPORT_SYMBOL_GPL(__inet_lookup_established);

/* called with local bh disabled */
static int __inet_check_established(struct inet_timewait_death_row *death_row,
				    struct sock *sk, __u16 lport,
				    struct inet_timewait_sock **twp)
{
	struct inet_hashinfo *hinfo = death_row->hashinfo;
	struct inet_sock *inet = inet_sk(sk);
	__be32 daddr = inet->inet_rcv_saddr;
	__be32 saddr = inet->inet_daddr;
	int dif = sk->sk_bound_dev_if;
	INET_ADDR_COOKIE(acookie, saddr, daddr);
	const __portpair ports = INET_COMBINED_PORTS(inet->inet_dport, lport);
	struct net *net = sock_net(sk);
	unsigned int hash = inet_ehashfn(net, daddr, lport,
					 saddr, inet->inet_dport);
	struct inet_ehash_bucket *head = inet_ehash_bucket(hinfo, hash);
	spinlock_t *lock = inet_ehash_lockp(hinfo, hash);
	struct sock *sk2;
	const struct hlist_nulls_node *node;
	struct inet_timewait_sock *tw = NULL;
	int twrefcnt = 0;

	spin_lock(lock);

	sk_nulls_for_each(sk2, node, &head->chain) {
		if (sk2->sk_hash != hash)
			continue;

		if (likely(INET_MATCH(sk2, net, acookie,
					 saddr, daddr, ports, dif))) {
			if (sk2->sk_state == TCP_TIME_WAIT) {
				tw = inet_twsk(sk2);
				if (twsk_unique(sk, sk2, twp))
					break;
			}
			goto not_unique;
		}
	}

	/* Must record num and sport now. Otherwise we will see
	 * in hash table socket with a funny identity.
	 */
	inet->inet_num = lport;
	inet->inet_sport = htons(lport);
	sk->sk_hash = hash;
	WARN_ON(!sk_unhashed(sk));
	__sk_nulls_add_node_rcu(sk, &head->chain);
	if (tw) {
		twrefcnt = inet_twsk_unhash(tw);
		NET_INC_STATS_BH(net, LINUX_MIB_TIMEWAITRECYCLED);
	}
	spin_unlock(lock);
	if (twrefcnt)
		inet_twsk_put(tw);
	sock_prot_inuse_add(sock_net(sk), sk->sk_prot, 1);

	if (twp) {
		*twp = tw;
	} else if (tw) {
		/* Silly. Should hash-dance instead... */
		inet_twsk_deschedule(tw);

		inet_twsk_put(tw);
	}
	return 0;

not_unique:
	spin_unlock(lock);
	return -EADDRNOTAVAIL;
}

static u32 inet_sk_port_offset(const struct sock *sk)
{
	const struct inet_sock *inet = inet_sk(sk);

	return secure_ipv4_port_ephemeral(inet->inet_rcv_saddr,
					  inet->inet_daddr,
					  inet->inet_dport);
}

int __inet_hash_nolisten(struct sock *sk, struct inet_timewait_sock *tw)
{
	struct inet_hashinfo *hashinfo = sk->sk_prot->h.hashinfo;
	struct hlist_nulls_head *list;
	struct inet_ehash_bucket *head;
	spinlock_t *lock;
	int twrefcnt = 0;

	WARN_ON(!sk_unhashed(sk));

	sk->sk_hash = sk_ehashfn(sk);
	head = inet_ehash_bucket(hashinfo, sk->sk_hash);
	list = &head->chain;
	lock = inet_ehash_lockp(hashinfo, sk->sk_hash);

	spin_lock(lock);
	__sk_nulls_add_node_rcu(sk, list);
	if (tw) {
		WARN_ON(sk->sk_hash != tw->tw_hash);
		twrefcnt = inet_twsk_unhash(tw);
	}
	spin_unlock(lock);
	sock_prot_inuse_add(sock_net(sk), sk->sk_prot, 1);
	return twrefcnt;
}
EXPORT_SYMBOL_GPL(__inet_hash_nolisten);

int __inet_hash(struct sock *sk, struct inet_timewait_sock *tw)
{
	struct inet_hashinfo *hashinfo = sk->sk_prot->h.hashinfo;
	struct inet_listen_hashbucket *ilb;

	if (sk->sk_state != TCP_LISTEN)
		return __inet_hash_nolisten(sk, tw);

	WARN_ON(!sk_unhashed(sk));
	ilb = &hashinfo->listening_hash[inet_sk_listen_hashfn(sk)];

	spin_lock(&ilb->lock);
	__sk_nulls_add_node_rcu(sk, &ilb->head);
	sock_prot_inuse_add(sock_net(sk), sk->sk_prot, 1);
	spin_unlock(&ilb->lock);
	return 0;
}
EXPORT_SYMBOL(__inet_hash);

void inet_hash(struct sock *sk)
{
	if (sk->sk_state != TCP_CLOSE) {
		local_bh_disable();
		__inet_hash(sk, NULL);
		local_bh_enable();
	}
}
EXPORT_SYMBOL_GPL(inet_hash);

void inet_unhash(struct sock *sk)
{
	struct inet_hashinfo *hashinfo = sk->sk_prot->h.hashinfo;
	spinlock_t *lock;
	int done;

	if (sk_unhashed(sk))
		return;

	if (sk->sk_state == TCP_LISTEN)
		lock = &hashinfo->listening_hash[inet_sk_listen_hashfn(sk)].lock;
	else
		lock = inet_ehash_lockp(hashinfo, sk->sk_hash);

	spin_lock_bh(lock);
	done = __sk_nulls_del_node_init_rcu(sk);
	if (done)
		sock_prot_inuse_add(sock_net(sk), sk->sk_prot, -1);
	spin_unlock_bh(lock);
}
EXPORT_SYMBOL_GPL(inet_unhash);

int __inet_hash_connect(struct inet_timewait_death_row *death_row,
		struct sock *sk, u32 port_offset,
		int (*check_established)(struct inet_timewait_death_row *,
			struct sock *, __u16, struct inet_timewait_sock **))
{
	struct inet_hashinfo *hinfo = death_row->hashinfo;
	const unsigned short snum = inet_sk(sk)->inet_num;
	struct inet_bind_hashbucket *head;
	struct inet_bind_bucket *tb;
	int ret;
	struct net *net = sock_net(sk);
	int twrefcnt = 1;

	if (!snum) {
		int i, remaining, low, high, port;
		static u32 hint;
		u32 offset = hint + port_offset;
		struct inet_timewait_sock *tw = NULL;

		inet_get_local_port_range(net, &low, &high);
		remaining = (high - low) + 1;

		/* By starting with offset being an even number,
		 * we tend to leave about 50% of ports for other uses,
		 * like bind(0).
		 */
		offset &= ~1;

		local_bh_disable();
		for (i = 0; i < remaining; i++) {
			port = low + (i + offset) % remaining;
			if (inet_is_local_reserved_port(net, port))
				continue;
			head = &hinfo->bhash[inet_bhashfn(net, port,
					hinfo->bhash_size)];
			spin_lock(&head->lock);

			/* Does not bother with rcv_saddr checks,
			 * because the established check is already
			 * unique enough.
			 */
			inet_bind_bucket_for_each(tb, &head->chain) {
				if (net_eq(ib_net(tb), net) &&
				    tb->port == port) {
					if (tb->fastreuse >= 0 ||
					    tb->fastreuseport >= 0)
						goto next_port;
					WARN_ON(hlist_empty(&tb->owners));
					if (!check_established(death_row, sk,
								port, &tw))
						goto ok;
					goto next_port;
				}
			}

			tb = inet_bind_bucket_create(hinfo->bind_bucket_cachep,
					net, head, port);
			if (!tb) {
				spin_unlock(&head->lock);
				break;
			}
			tb->fastreuse = -1;
			tb->fastreuseport = -1;
			goto ok;

		next_port:
			spin_unlock(&head->lock);
		}
		local_bh_enable();

		return -EADDRNOTAVAIL;

ok:
		hint += (i + 2) & ~1;

		/* Head lock still held and bh's disabled */
		inet_bind_hash(sk, tb, port);
		if (sk_unhashed(sk)) {
			inet_sk(sk)->inet_sport = htons(port);
			twrefcnt += __inet_hash_nolisten(sk, tw);
		}
		if (tw)
			twrefcnt += inet_twsk_bind_unhash(tw, hinfo);
		spin_unlock(&head->lock);

		if (tw) {
			inet_twsk_deschedule(tw);
			while (twrefcnt) {
				twrefcnt--;
				inet_twsk_put(tw);
			}
		}

		ret = 0;
		goto out;
	}

	head = &hinfo->bhash[inet_bhashfn(net, snum, hinfo->bhash_size)];
	tb  = inet_csk(sk)->icsk_bind_hash;
	spin_lock_bh(&head->lock);
	if (sk_head(&tb->owners) == sk && !sk->sk_bind_node.next) {
		__inet_hash_nolisten(sk, NULL);
		spin_unlock_bh(&head->lock);
		return 0;
	} else {
		spin_unlock(&head->lock);
		/* No definite answer... Walk to established hash table */
		ret = check_established(death_row, sk, snum, NULL);
out:
		local_bh_enable();
		return ret;
	}
}

/*
 * Bind a port for a connect operation and hash it.
 */
int inet_hash_connect(struct inet_timewait_death_row *death_row,
		      struct sock *sk)
{
	u32 port_offset = 0;

	if (!inet_sk(sk)->inet_num)
		port_offset = inet_sk_port_offset(sk);
	return __inet_hash_connect(death_row, sk, port_offset,
				   __inet_check_established);
}
EXPORT_SYMBOL_GPL(inet_hash_connect);

void inet_hashinfo_init(struct inet_hashinfo *h)
{
	int i;

	for (i = 0; i < INET_LHTABLE_SIZE; i++) {
		spin_lock_init(&h->listening_hash[i].lock);
		INIT_HLIST_NULLS_HEAD(&h->listening_hash[i].head,
				      i + LISTENING_NULLS_BASE);
		}
}
EXPORT_SYMBOL_GPL(inet_hashinfo_init);

int inet_ehash_locks_alloc(struct inet_hashinfo *hashinfo)
{
	unsigned int locksz = sizeof(spinlock_t);
	unsigned int i, nblocks = 1;

	if (locksz != 0) {
		/* allocate 2 cache lines or at least one spinlock per cpu */
		nblocks = max(2U * L1_CACHE_BYTES / locksz, 1U);
		nblocks = roundup_pow_of_two(nblocks * num_possible_cpus());

		/* no more locks than number of hash buckets */
		nblocks = min(nblocks, hashinfo->ehash_mask + 1);

		hashinfo->ehash_locks =	kmalloc_array(nblocks, locksz,
						      GFP_KERNEL | __GFP_NOWARN);
		if (!hashinfo->ehash_locks)
			hashinfo->ehash_locks = vmalloc(nblocks * locksz);

		if (!hashinfo->ehash_locks)
			return -ENOMEM;

		for (i = 0; i < nblocks; i++)
			spin_lock_init(&hashinfo->ehash_locks[i]);
	}
	hashinfo->ehash_locks_mask = nblocks - 1;
	return 0;
}
EXPORT_SYMBOL_GPL(inet_ehash_locks_alloc);
