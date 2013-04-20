/*
 * INET		An implementation of the TCP/IP protocol suite for the LINUX
 *		operating system.  INET is implemented using the BSD Socket
 *		interface as the means of communication with the user level.
 *
 *		Generic INET6 transport hashtables
 *
 * Authors:	Lotsa people, from code originally in tcp, generalised here
 * 		by Arnaldo Carvalho de Melo <acme@mandriva.com>
 *
 *	This program is free software; you can redistribute it and/or
 *      modify it under the terms of the GNU General Public License
 *      as published by the Free Software Foundation; either version
 *      2 of the License, or (at your option) any later version.
 */

#include <linux/module.h>
#include <linux/random.h>

#include <net/inet_connection_sock.h>
#include <net/inet_hashtables.h>
#include <net/inet6_hashtables.h>
#include <net/secure_seq.h>
#include <net/ip.h>

int __inet6_hash(struct sock *sk, struct inet_timewait_sock *tw)
{
	struct inet_hashinfo *hashinfo = sk->sk_prot->h.hashinfo;
	int twrefcnt = 0;

	WARN_ON(!sk_unhashed(sk));

	if (sk->sk_state == TCP_LISTEN) {
		struct inet_listen_hashbucket *ilb;

		ilb = &hashinfo->listening_hash[inet_sk_listen_hashfn(sk)];
		spin_lock(&ilb->lock);
		__sk_nulls_add_node_rcu(sk, &ilb->head);
		spin_unlock(&ilb->lock);
	} else {
		unsigned int hash;
		struct hlist_nulls_head *list;
		spinlock_t *lock;

		sk->sk_hash = hash = inet6_sk_ehashfn(sk);
		list = &inet_ehash_bucket(hashinfo, hash)->chain;
		lock = inet_ehash_lockp(hashinfo, hash);
		spin_lock(lock);
		__sk_nulls_add_node_rcu(sk, list);
		if (tw) {
			WARN_ON(sk->sk_hash != tw->tw_hash);
			twrefcnt = inet_twsk_unhash(tw);
		}
		spin_unlock(lock);
	}

	sock_prot_inuse_add(sock_net(sk), sk->sk_prot, 1);
	return twrefcnt;
}
EXPORT_SYMBOL(__inet6_hash);

/*
 * Sockets in TCP_CLOSE state are _always_ taken out of the hash, so
 * we need not check it for TCP lookups anymore, thanks Alexey. -DaveM
 *
 * The sockhash lock must be held as a reader here.
 */
struct sock *__inet6_lookup_established(struct net *net,
					struct inet_hashinfo *hashinfo,
					   const struct in6_addr *saddr,
					   const __be16 sport,
					   const struct in6_addr *daddr,
					   const u16 hnum,
					   const int dif)
{
	struct sock *sk;
	const struct hlist_nulls_node *node;
	const __portpair ports = INET_COMBINED_PORTS(sport, hnum);
	/* Optimize here for direct hit, only listening connections can
	 * have wildcards anyways.
	 */
	unsigned int hash = inet6_ehashfn(net, daddr, hnum, saddr, sport);
	unsigned int slot = hash & hashinfo->ehash_mask;
	struct inet_ehash_bucket *head = &hashinfo->ehash[slot];


	rcu_read_lock();
begin:
	sk_nulls_for_each_rcu(sk, node, &head->chain) {
		if (sk->sk_hash != hash)
			continue;
		if (likely(INET6_MATCH(sk, net, saddr, daddr, ports, dif))) {
			if (unlikely(!atomic_inc_not_zero(&sk->sk_refcnt)))
				goto begintw;
			if (unlikely(!INET6_MATCH(sk, net, saddr, daddr,
						  ports, dif))) {
				sock_put(sk);
				goto begin;
			}
		goto out;
		}
	}
	if (get_nulls_value(node) != slot)
		goto begin;

begintw:
	/* Must check for a TIME_WAIT'er before going to listener hash. */
	sk_nulls_for_each_rcu(sk, node, &head->twchain) {
		if (sk->sk_hash != hash)
			continue;
		if (likely(INET6_TW_MATCH(sk, net, saddr, daddr,
					  ports, dif))) {
			if (unlikely(!atomic_inc_not_zero(&sk->sk_refcnt))) {
				sk = NULL;
				goto out;
			}
			if (unlikely(!INET6_TW_MATCH(sk, net, saddr, daddr,
						     ports, dif))) {
				sock_put(sk);
				goto begintw;
			}
			goto out;
		}
	}
	if (get_nulls_value(node) != slot)
		goto begintw;
	sk = NULL;
out:
	rcu_read_unlock();
	return sk;
}
EXPORT_SYMBOL(__inet6_lookup_established);

static inline int compute_score(struct sock *sk, struct net *net,
				const unsigned short hnum,
				const struct in6_addr *daddr,
				const int dif)
{
	int score = -1;

	if (net_eq(sock_net(sk), net) && inet_sk(sk)->inet_num == hnum &&
	    sk->sk_family == PF_INET6) {
		const struct ipv6_pinfo *np = inet6_sk(sk);

		score = 1;
		if (!ipv6_addr_any(&np->rcv_saddr)) {
			if (!ipv6_addr_equal(&np->rcv_saddr, daddr))
				return -1;
			score++;
		}
		if (sk->sk_bound_dev_if) {
			if (sk->sk_bound_dev_if != dif)
				return -1;
			score++;
		}
	}
	return score;
}

struct sock *inet6_lookup_listener(struct net *net,
		struct inet_hashinfo *hashinfo, const struct in6_addr *saddr,
		const __be16 sport, const struct in6_addr *daddr,
		const unsigned short hnum, const int dif)
{
	struct sock *sk;
	const struct hlist_nulls_node *node;
	struct sock *result;
	int score, hiscore, matches = 0, reuseport = 0;
	u32 phash = 0;
	unsigned int hash = inet_lhashfn(net, hnum);
	struct inet_listen_hashbucket *ilb = &hashinfo->listening_hash[hash];

	rcu_read_lock();
begin:
	result = NULL;
	hiscore = 0;
	sk_nulls_for_each(sk, node, &ilb->head) {
		score = compute_score(sk, net, hnum, daddr, dif);
		if (score > hiscore) {
			hiscore = score;
			result = sk;
			reuseport = sk->sk_reuseport;
			if (reuseport) {
				phash = inet6_ehashfn(net, daddr, hnum,
						      saddr, sport);
				matches = 1;
			}
		} else if (score == hiscore && reuseport) {
			matches++;
			if (((u64)phash * matches) >> 32 == 0)
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

EXPORT_SYMBOL_GPL(inet6_lookup_listener);

struct sock *inet6_lookup(struct net *net, struct inet_hashinfo *hashinfo,
			  const struct in6_addr *saddr, const __be16 sport,
			  const struct in6_addr *daddr, const __be16 dport,
			  const int dif)
{
	struct sock *sk;

	local_bh_disable();
	sk = __inet6_lookup(net, hashinfo, saddr, sport, daddr, ntohs(dport), dif);
	local_bh_enable();

	return sk;
}

EXPORT_SYMBOL_GPL(inet6_lookup);

static int __inet6_check_established(struct inet_timewait_death_row *death_row,
				     struct sock *sk, const __u16 lport,
				     struct inet_timewait_sock **twp)
{
	struct inet_hashinfo *hinfo = death_row->hashinfo;
	struct inet_sock *inet = inet_sk(sk);
	const struct ipv6_pinfo *np = inet6_sk(sk);
	const struct in6_addr *daddr = &np->rcv_saddr;
	const struct in6_addr *saddr = &np->daddr;
	const int dif = sk->sk_bound_dev_if;
	const __portpair ports = INET_COMBINED_PORTS(inet->inet_dport, lport);
	struct net *net = sock_net(sk);
	const unsigned int hash = inet6_ehashfn(net, daddr, lport, saddr,
						inet->inet_dport);
	struct inet_ehash_bucket *head = inet_ehash_bucket(hinfo, hash);
	spinlock_t *lock = inet_ehash_lockp(hinfo, hash);
	struct sock *sk2;
	const struct hlist_nulls_node *node;
	struct inet_timewait_sock *tw;
	int twrefcnt = 0;

	spin_lock(lock);

	/* Check TIME-WAIT sockets first. */
	sk_nulls_for_each(sk2, node, &head->twchain) {
		if (sk2->sk_hash != hash)
			continue;

		if (likely(INET6_TW_MATCH(sk2, net, saddr, daddr,
					  ports, dif))) {
			tw = inet_twsk(sk2);
			if (twsk_unique(sk, sk2, twp))
				goto unique;
			else
				goto not_unique;
		}
	}
	tw = NULL;

	/* And established part... */
	sk_nulls_for_each(sk2, node, &head->chain) {
		if (sk2->sk_hash != hash)
			continue;
		if (likely(INET6_MATCH(sk2, net, saddr, daddr, ports, dif)))
			goto not_unique;
	}

unique:
	/* Must record num and sport now. Otherwise we will see
	 * in hash table socket with a funny identity. */
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
		inet_twsk_deschedule(tw, death_row);

		inet_twsk_put(tw);
	}
	return 0;

not_unique:
	spin_unlock(lock);
	return -EADDRNOTAVAIL;
}

static inline u32 inet6_sk_port_offset(const struct sock *sk)
{
	const struct inet_sock *inet = inet_sk(sk);
	const struct ipv6_pinfo *np = inet6_sk(sk);
	return secure_ipv6_port_ephemeral(np->rcv_saddr.s6_addr32,
					  np->daddr.s6_addr32,
					  inet->inet_dport);
}

int inet6_hash_connect(struct inet_timewait_death_row *death_row,
		       struct sock *sk)
{
	return __inet_hash_connect(death_row, sk, inet6_sk_port_offset(sk),
			__inet6_check_established, __inet6_hash);
}

EXPORT_SYMBOL_GPL(inet6_hash_connect);
