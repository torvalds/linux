/*
 * INET		An implementation of the TCP/IP protocol suite for the LINUX
 *		operating system.  INET is implemented using the BSD Socket
 *		interface as the means of communication with the user level.
 *
 *		Generic INET6 transport hashtables
 *
 * Authors:	Lotsa people, from code originally in tcp
 *
 *	This program is free software; you can redistribute it and/or
 *      modify it under the terms of the GNU General Public License
 *      as published by the Free Software Foundation; either version
 *      2 of the License, or (at your option) any later version.
 */

#include <linux/config.h>

#include <linux/module.h>

#include <net/inet_connection_sock.h>
#include <net/inet_hashtables.h>
#include <net/inet6_hashtables.h>

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
			  const struct in6_addr *saddr, const u16 sport,
			  const struct in6_addr *daddr, const u16 dport,
			  const int dif)
{
	struct sock *sk;

	local_bh_disable();
	sk = __inet6_lookup(hashinfo, saddr, sport, daddr, ntohs(dport), dif);
	local_bh_enable();

	return sk;
}

EXPORT_SYMBOL_GPL(inet6_lookup);
