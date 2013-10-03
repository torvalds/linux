/*
 * INET		An implementation of the TCP/IP protocol suite for the LINUX
 *		operating system.  INET is implemented using the  BSD Socket
 *		interface as the means of communication with the user level.
 *
 *		Checksumming functions for IPv6
 *
 * Authors:	Jorge Cwik, <jorge@laser.satlink.net>
 *		Arnt Gulbrandsen, <agulbra@nvg.unit.no>
 *		Borrows very liberally from tcp.c and ip.c, see those
 *		files for more names.
 *
 *		This program is free software; you can redistribute it and/or
 *		modify it under the terms of the GNU General Public License
 *		as published by the Free Software Foundation; either version
 *		2 of the License, or (at your option) any later version.
 */

/*
 *	Fixes:
 *
 *	Ralf Baechle			:	generic ipv6 checksum
 *	<ralf@waldorf-gmbh.de>
 */

#ifndef _CHECKSUM_IPV6_H
#define _CHECKSUM_IPV6_H

#include <asm/types.h>
#include <asm/byteorder.h>
#include <net/ip.h>
#include <asm/checksum.h>
#include <linux/in6.h>
#include <linux/tcp.h>
#include <linux/ipv6.h>

#ifndef _HAVE_ARCH_IPV6_CSUM
__sum16 csum_ipv6_magic(const struct in6_addr *saddr,
			const struct in6_addr *daddr,
			__u32 len, unsigned short proto,
			__wsum csum);
#endif

static __inline__ __sum16 tcp_v6_check(int len,
				   const struct in6_addr *saddr,
				   const struct in6_addr *daddr,
				   __wsum base)
{
	return csum_ipv6_magic(saddr, daddr, len, IPPROTO_TCP, base);
}

static inline void __tcp_v6_send_check(struct sk_buff *skb,
				       const struct in6_addr *saddr,
				       const struct in6_addr *daddr)
{
	struct tcphdr *th = tcp_hdr(skb);

	if (skb->ip_summed == CHECKSUM_PARTIAL) {
		th->check = ~tcp_v6_check(skb->len, saddr, daddr, 0);
		skb->csum_start = skb_transport_header(skb) - skb->head;
		skb->csum_offset = offsetof(struct tcphdr, check);
	} else {
		th->check = tcp_v6_check(skb->len, saddr, daddr,
					 csum_partial(th, th->doff << 2,
						      skb->csum));
	}
}

static inline void tcp_v6_send_check(struct sock *sk, struct sk_buff *skb)
{
	struct ipv6_pinfo *np = inet6_sk(sk);

	__tcp_v6_send_check(skb, &np->saddr, &sk->sk_v6_daddr);
}

int udp6_csum_init(struct sk_buff *skb, struct udphdr *uh, int proto);
#endif
