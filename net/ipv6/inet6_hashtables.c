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

void __inet6_hash(struct inet_hashinfo *hashinfo,
				struct sock *sk)
{
	struct hlist_head *list;
	rwlock_t *lock;

	BUG_TRAP(sk_unhashed(sk));

	if (sk->sk_state == TCP_LISTEN) {
		list = &hashinfo->listening_hash[inet_sk_listen_hashfn(sk)];
		lock = &hashinfo->lhash_lock;
		inet_listen_wlock(hashinfo);
	} else {
		unsigned int hash;
		sk->sk_hash = hash = inet6_sk_ehashfn(sk);
		hash &= (hashinfo->ehash_size - 1);
		list = &hashinfo->ehash[hash].chain;
		lock = &hashinfo->ehash[hash].lock;
		write_lock(lock);
	}

	__sk_add_node(sk, list);
	sock_prot_inc_use(sk->sk_prot);
	write_unlock(lock);
}
EXPORT_SYMBOL(__inet6_hash);

/*
 * Sockets in TCP_CLOSE state are _always_ taken out of the hash, so
 * we need not check it for TCP lookups anymore, thanks Alexey. -DaveM
 *
 * The sockhash lock must be held as a reader here.
 */
struct sock *__inet6_lookup_established(struct inet_hashinfo *hashinfo,
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
	unsigned int hash = inet6_ehashfn(daddr, hnum, saddr, sport);
	struct inet_ehash_bucket *head = inet_ehash_bucket(hashinfo, hash);

	prefetch(head->chain.first);
	read_lock(&head->lock);
	sk_for_each(sk, node, &head->chain) {
		/* For IPV6 do the cheaper port and family tests first. */
		if (INET6_MATCH(sk, hash, saddr, daddr, ports, dif))
			goto hit; /* You sunk my battleship! */
	}
	/* Must check for a TIME_WAIT'er before going to listener hash. */
	sk_for_each(sk, node, &head->twchain) {
		const struct inet_timewait_sock *tw = inet_twsk(sk);

		if(*((__portpair *)&(tw->tw_dport))	== ports	&&
		   sk->sk_family		== PF_INET6) {
			const struct inet6_timewait_sock *tw6 = inet6_twsk(sk);

			if (ipv6_addr_equal(&tw6->tw_v6_daddr, saddr)	&&
			    ipv6_addr_equal(&tw6->tw_v6_rcv_saddr, daddr)	&&
			    (!sk->sk_bound_dev_if || sk->sk_bound_dev_if == dif))
				goto hit;
		}
	}
	read_unlock(&head->lock);
	return NULL;

hit:
	sock_hold(sk);
	read_unlock(&head->lock);
	return sk;
}
EXPORT_SYMBOL(__inet6_lookup_established);

struct sock *inet6_lookup_listener(struct inet_hashinfo *hashinfo,
				   const struct in6_addr *daddr,
				   const unsigned short hnum, const int dif)
{
	struct sock *sk;
	const struct hlist_node *node;
	struct sock *result = NULL;
	int score, hiscore = 0;

	read_lock(&hashinfo->lhash_lock);
	sk_for_each(sk, node, &hashinfo->listening_hash[inet_lhashfn(hnum)]) {
		if (inet_sk(sk)->num == hnum && sk->sk_family == PF_INET6) {
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

struct sock *inet6_lookup(struct inet_hashinfo *hashinfo,
			  const struct in6_addr *saddr, const __be16 sport,
			  const struct in6_addr *daddr, const __be16 dport,
			  const int dif)
{
	struct sock *sk;

	local_bh_disable();
	sk = __inet6_lookup(hashinfo, saddr, sport, daddr, ntohs(dport), dif);
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
	const unsigned int hash = inet6_ehashfn(daddr, lport, saddr,
						inet->dport);
	struct inet_ehash_bucket *head = inet_ehash_bucket(hinfo, hash);
	struct sock *sk2;
	const struct hlist_node *node;
	struct inet_timewait_sock *tw;

	prefetch(head->chain.first);
	write_lock(&head->lock);

	/* Check TIME-WAIT sockets first. */
	sk_for_each(sk2, node, &head->twchain) {
		const struct inet6_timewait_sock *tw6 = inet6_twsk(sk2);

		tw = inet_twsk(sk2);

		if(*((__portpair *)&(tw->tw_dport)) == ports		 &&
		   sk2->sk_family	       == PF_INET6	 &&
		   ipv6_addr_equal(&tw6->tw_v6_daddr, saddr)	 &&
		   ipv6_addr_equal(&tw6->tw_v6_rcv_saddr, daddr) &&
		   sk2->sk_bound_dev_if == sk->sk_bound_dev_if) {
			if (twsk_unique(sk, sk2, twp))
				goto unique;
			else
				goto not_unique;
		}
	}
	tw = NULL;

	/* And established part... */
	sk_for_each(sk2, node, &head->chain) {
		if (INET6_MATCH(sk2, hash, saddr, daddr, ports, dif))
			goto not_unique;
	}

unique:
	/* Must record num and sport now. Otherwise we will see
	 * in hash table socket with a funny identity. */
	inet->num = lport;
	inet->sport = htons(lport);
	BUG_TRAP(sk_unhashed(sk));
	__sk_add_node(sk, &head->chain);
	sk->sk_hash = hash;
	sock_prot_inc_use(sk->sk_prot);
	write_unlock(&head->lock);

	if (twp != NULL) {
		*twp = tw;
		NET_INC_STATS_BH(LINUX_MIB_TIMEWAITRECYCLED);
	} else if (tw != NULL) {
		/* Silly. Should hash-dance instead... */
		inet_twsk_deschedule(tw, death_row);
		NET_INC_STATS_BH(LINUX_MIB_TIMEWAITRECYCLED);

		inet_twsk_put(tw);
	}
	return 0;

not_unique:
	write_unlock(&head->lock);
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
	struct inet_hashinfo *hinfo = death_row->hashinfo;
	const unsigned short snum = inet_sk(sk)->num;
	struct inet_bind_hashbucket *head;
	struct inet_bind_bucket *tb;
	int ret;

	if (snum == 0) {
		const int low = sysctl_local_port_range[0];
		const int high = sysctl_local_port_range[1];
		const int range = high - low;
		int i, port;
		static u32 hint;
		const u32 offset = hint + inet6_sk_port_offset(sk);
		struct hlist_node *node;
		struct inet_timewait_sock *tw = NULL;

		local_bh_disable();
		for (i = 1; i <= range; i++) {
			port = low + (i + offset) % range;
			head = &hinfo->bhash[inet_bhashfn(port, hinfo->bhash_size)];
			spin_lock(&head->lock);

			/* Does not bother with rcv_saddr checks,
			 * because the established check is already
			 * unique enough.
			 */
			inet_bind_bucket_for_each(tb, node, &head->chain) {
				if (tb->port == port) {
					BUG_TRAP(!hlist_empty(&tb->owners));
					if (tb->fastreuse >= 0)
						goto next_port;
					if (!__inet6_check_established(death_row,
								       sk, port,
								       &tw))
						goto ok;
					goto next_port;
				}
			}

			tb = inet_bind_bucket_create(hinfo->bind_bucket_cachep,
						     head, port);
			if (!tb) {
				spin_unlock(&head->lock);
				break;
			}
			tb->fastreuse = -1;
			goto ok;

		next_port:
			spin_unlock(&head->lock);
		}
		local_bh_enable();

		return -EADDRNOTAVAIL;

ok:
		hint += i;

		/* Head lock still held and bh's disabled */
		inet_bind_hash(sk, tb, port);
		if (sk_unhashed(sk)) {
			inet_sk(sk)->sport = htons(port);
			__inet6_hash(hinfo, sk);
		}
		spin_unlock(&head->lock);

		if (tw) {
			inet_twsk_deschedule(tw, death_row);
			inet_twsk_put(tw);
		}

		ret = 0;
		goto out;
	}

	head = &hinfo->bhash[inet_bhashfn(snum, hinfo->bhash_size)];
	tb   = inet_csk(sk)->icsk_bind_hash;
	spin_lock_bh(&head->lock);

	if (sk_head(&tb->owners) == sk && sk->sk_bind_node.next == NULL) {
		__inet6_hash(hinfo, sk);
		spin_unlock_bh(&head->lock);
		return 0;
	} else {
		spin_unlock(&head->lock);
		/* No definite answer... Walk to established hash table */
		ret = __inet6_check_established(death_row, sk, snum, NULL);
out:
		local_bh_enable();
		return ret;
	}
}

EXPORT_SYMBOL_GPL(inet6_hash_connect);
