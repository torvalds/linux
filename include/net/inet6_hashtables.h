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

#ifndef _INET6_HASHTABLES_H
#define _INET6_HASHTABLES_H

#include <linux/config.h>

#if defined(CONFIG_IPV6) || defined (CONFIG_IPV6_MODULE)
#include <linux/in6.h>
#include <linux/ipv6.h>
#include <linux/types.h>

#include <net/ipv6.h>

struct inet_hashinfo;

/* I have no idea if this is a good hash for v6 or not. -DaveM */
static inline unsigned int inet6_ehashfn(const struct in6_addr *laddr, const u16 lport,
				const struct in6_addr *faddr, const u16 fport)
{
	unsigned int hashent = (lport ^ fport);

	hashent ^= (laddr->s6_addr32[3] ^ faddr->s6_addr32[3]);
	hashent ^= hashent >> 16;
	hashent ^= hashent >> 8;
	return hashent;
}

static inline int inet6_sk_ehashfn(const struct sock *sk)
{
	const struct inet_sock *inet = inet_sk(sk);
	const struct ipv6_pinfo *np = inet6_sk(sk);
	const struct in6_addr *laddr = &np->rcv_saddr;
	const struct in6_addr *faddr = &np->daddr;
	const __u16 lport = inet->num;
	const __u16 fport = inet->dport;
	return inet6_ehashfn(laddr, lport, faddr, fport);
}

/*
 * Sockets in TCP_CLOSE state are _always_ taken out of the hash, so
 * we need not check it for TCP lookups anymore, thanks Alexey. -DaveM
 *
 * The sockhash lock must be held as a reader here.
 */
static inline struct sock *
		__inet6_lookup_established(struct inet_hashinfo *hashinfo,
					   const struct in6_addr *saddr,
					   const u16 sport,
					   const struct in6_addr *daddr,
					   const u16 hnum,
					   const int dif)
{
	struct sock *sk;
	const struct hlist_node *node;
	const __u32 ports = INET_COMBINED_PORTS(sport, hnum);
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
	sk_for_each(sk, node, &(head + hashinfo->ehash_size)->chain) {
		const struct inet_timewait_sock *tw = inet_twsk(sk);

		if(*((__u32 *)&(tw->tw_dport))	== ports	&&
		   sk->sk_family		== PF_INET6) {
			const struct tcp6_timewait_sock *tcp6tw = tcp6_twsk(sk);

			if (ipv6_addr_equal(&tcp6tw->tw_v6_daddr, saddr)	&&
			    ipv6_addr_equal(&tcp6tw->tw_v6_rcv_saddr, daddr)	&&
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

extern struct sock *inet6_lookup_listener(struct inet_hashinfo *hashinfo,
					  const struct in6_addr *daddr,
					  const unsigned short hnum,
					  const int dif);

static inline struct sock *__inet6_lookup(struct inet_hashinfo *hashinfo,
					  const struct in6_addr *saddr,
					  const u16 sport,
					  const struct in6_addr *daddr,
					  const u16 hnum,
					  const int dif)
{
	struct sock *sk = __inet6_lookup_established(hashinfo, saddr, sport,
						     daddr, hnum, dif);
	if (sk)
		return sk;

	return inet6_lookup_listener(hashinfo, daddr, hnum, dif);
}

extern struct sock *inet6_lookup(struct inet_hashinfo *hashinfo,
				 const struct in6_addr *saddr, const u16 sport,
				 const struct in6_addr *daddr, const u16 dport,
				 const int dif);
#endif /* defined(CONFIG_IPV6) || defined (CONFIG_IPV6_MODULE) */
#endif /* _INET6_HASHTABLES_H */
