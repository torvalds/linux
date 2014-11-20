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


#if IS_ENABLED(CONFIG_IPV6)
#include <linux/in6.h>
#include <linux/ipv6.h>
#include <linux/types.h>
#include <linux/jhash.h>

#include <net/inet_sock.h>

#include <net/ipv6.h>
#include <net/netns/hash.h>

struct inet_hashinfo;

static inline unsigned int __inet6_ehashfn(const u32 lhash,
				    const u16 lport,
				    const u32 fhash,
				    const __be16 fport,
				    const u32 initval)
{
	const u32 ports = (((u32)lport) << 16) | (__force u32)fport;
	return jhash_3words(lhash, fhash, ports, initval);
}

int __inet6_hash(struct sock *sk, struct inet_timewait_sock *twp);

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
					const u16 hnum, const int dif);

struct sock *inet6_lookup_listener(struct net *net,
				   struct inet_hashinfo *hashinfo,
				   const struct in6_addr *saddr,
				   const __be16 sport,
				   const struct in6_addr *daddr,
				   const unsigned short hnum, const int dif);

static inline struct sock *__inet6_lookup(struct net *net,
					  struct inet_hashinfo *hashinfo,
					  const struct in6_addr *saddr,
					  const __be16 sport,
					  const struct in6_addr *daddr,
					  const u16 hnum,
					  const int dif)
{
	struct sock *sk = __inet6_lookup_established(net, hashinfo, saddr,
						sport, daddr, hnum, dif);
	if (sk)
		return sk;

	return inet6_lookup_listener(net, hashinfo, saddr, sport,
				     daddr, hnum, dif);
}

static inline struct sock *__inet6_lookup_skb(struct inet_hashinfo *hashinfo,
					      struct sk_buff *skb,
					      const __be16 sport,
					      const __be16 dport,
					      int iif)
{
	struct sock *sk = skb_steal_sock(skb);

	if (sk)
		return sk;

	return __inet6_lookup(dev_net(skb_dst(skb)->dev), hashinfo,
			      &ipv6_hdr(skb)->saddr, sport,
			      &ipv6_hdr(skb)->daddr, ntohs(dport),
			      iif);
}

struct sock *inet6_lookup(struct net *net, struct inet_hashinfo *hashinfo,
			  const struct in6_addr *saddr, const __be16 sport,
			  const struct in6_addr *daddr, const __be16 dport,
			  const int dif);
#endif /* IS_ENABLED(CONFIG_IPV6) */
#endif /* _INET6_HASHTABLES_H */
