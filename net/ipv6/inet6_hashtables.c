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
#include <net/ip.h>

void __inet6_hash(struct sock *sk)
{
	struct inet_hashinfo *hashinfo = sk->sk_prot->h.hashinfo;
	struct hlist_head *list;
	rwlock_t *lock;

	WARN_ON(!sk_unhashed(sk));

	if (sk->sk_state == TCP_LISTEN) {
		list = &hashinfo->listening_hash[inet_sk_listen_hashfn(sk)];
		lock = &hashinfo->lhash_lock;
		inet_listen_wlock(hashinfo);
	} else {
		unsigned int hash;
		sk->sk_hash = hash = inet6_sk_ehashfn(sk);
		list = &inet_ehash_bucket(hashinfo, hash)->chain;
		lock = inet_ehash_lockp(hashinfo, hash);
		write_lock(lock);
	}

	__sk_add_node(sk, list);
	sock_prot_inuse_add(sock_net(sk), sk->sk_prot, 1);
	write_unlock(lock);
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
	const struct hlist_node *node;
	const __portpair ports = INET_COMBINED_PORTS(sport, hnum);
	/* Optimize here for direct hit, only listening connections can
	 * have wildcards anyways.
	 */
	unsigned int hash = inet6_ehashfn(net, daddr, hnum, saddr, sport);
	struct inet_ehash_bucket *head = inet_ehash_bucket(hashinfo, hash);
	rwlock_t *lock = inet_ehash_lockp(hashinfo, hash);

	prefetch(head->chain.first);
	read_lock(lock);
	sk_for_each(sk, node, &head->chain) {
		/* For IPV6 do the cheaper port and family tests first. */
		if (INET6_MATCH(sk, net, hash, saddr, daddr, ports, dif))
			goto hit; /* You sunk my battleship! */
	}
	/* Must check for a TIME_WAIT'er before going to listener hash. */
	sk_for_each(sk, node, &head->twchain) {
		if (INET6_TW_MATCH(sk, net, hash, saddr, daddr, ports, dif))
			goto hit;
	}
	read_unlock(lock);
	return NULL;

hit:
	sock_hold(sk);
	read_unlock(lock);
	return sk;
}
EXPORT_SYMBOL(__inet6_lookup_established);

struct sock *inet6_lookup_listener(struct net *net,
		struct inet_hashinfo *hashinfo, const struct in6_addr *daddr,
		const unsigned short hnum, const int dif)
{
	struct sock *sk;
	const struct hlist_node *node;
	struct sock *result = NULL;
	int score, hiscore = 0;

	read_lock(&hashinfo->lhash_lock);
	sk_for_each(sk, node,
			&hashinfo->listening_hash[inet_lhashfn(net, hnum)]) {
		if (net_eq(sock_net(sk), net) && inet_sk(sk)->num == hnum &&
				sk->sk_family == PF_INET6) {
			const struct ipv6_pinfo *np = inet6_sk(sk);

			score = 1;
			if (!ipv6_addr_any(&np->rcv_saddr)) {
				if (!ipv6_addr_equal(&np->rcv_saddr, daddr))
					continue;
				score++;
			}
			if (sk->sk_bound_dev_if) {
				if (sk->sk_bound_dev_if != dif)
					continue;
				score++;
			}
			if (score == 3) {
				result = sk;
				break;
			}
			if (score > hiscore) {
				hiscore = score;
				result = sk;
			}
		}
	}
	if (result)
		sock_hold(result);
	read_unlock(&hashinfo->lhash_lock);
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
	const __portpair ports = INET_COMBINED_PORTS(inet->dport, lport);
	struct net *net = sock_net(sk);
	const unsigned int hash = inet6_ehashfn(net, daddr, lport, saddr,
						inet->dport);
	struct inet_ehash_bucket *head = inet_ehash_bucket(hinfo, hash);
	rwlock_t *lock = inet_ehash_lockp(hinfo, hash);
	struct sock *sk2;
	const struct hlist_node *node;
	struct inet_timewait_sock *tw;

	prefetch(head->chain.first);
	write_lock(lock);

	/* Check TIME-WAIT sockets first. */
	sk_for_each(sk2, node, &head->twchain) {
		tw = inet_twsk(sk2);

		if (INET6_TW_MATCH(sk2, net, hash, saddr, daddr, ports, dif)) {
			if (twsk_unique(sk, sk2, twp))
				goto unique;
			else
				goto not_unique;
		}
	}
	tw = NULL;

	/* And established part... */
	sk_for_each(sk2, node, &head->chain) {
		if (INET6_MATCH(sk2, net, hash, saddr, daddr, ports, dif))
			goto not_unique;
	}

unique:
	/* Must record num and sport now. Otherwise we will see
	 * in hash table socket with a funny identity. */
	inet->num = lport;
	inet->sport = htons(lport);
	WARN_ON(!sk_unhashed(sk));
	__sk_add_node(sk, &head->chain);
	sk->sk_hash = hash;
	sock_prot_inuse_add(sock_net(sk), sk->sk_prot, 1);
	write_unlock(lock);

	if (twp != NULL) {
		*twp = tw;
		NET_INC_STATS_BH(twsk_net(tw), LINUX_MIB_TIMEWAITRECYCLED);
	} else if (tw != NULL) {
		/* Silly. Should hash-dance instead... */
		inet_twsk_deschedule(tw, death_row);
		NET_INC_STATS_BH(twsk_net(tw), LINUX_MIB_TIMEWAITRECYCLED);

		inet_twsk_put(tw);
	}
	return 0;

not_unique:
	write_unlock(lock);
	return -EADDRNOTAVAIL;
}

static inline u32 inet6_sk_port_offset(const struct sock *sk)
{
	const struct inet_sock *inet = inet_sk(sk);
	const struct ipv6_pinfo *np = inet6_sk(sk);
	return secure_ipv6_port_ephemeral(np->rcv_saddr.s6_addr32,
					  np->daddr.s6_addr32,
					  inet->dport);
}

int inet6_hash_connect(struct inet_timewait_death_row *death_row,
		       struct sock *sk)
{
	return __inet_hash_connect(death_row, sk, inet6_sk_port_offset(sk),
			__inet6_check_established, __inet6_hash);
}

EXPORT_SYMBOL_GPL(inet6_hash_connect);
