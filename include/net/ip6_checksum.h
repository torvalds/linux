/* SPDX-License-Identifier: GPL-2.0-or-later */
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
			__u32 len, __u8 proto, __wsum csum);
#endif

static inline __wsum ip6_compute_pseudo(struct sk_buff *skb, int proto)
{
	return ~csum_unfold(csum_ipv6_magic(&ipv6_hdr(skb)->saddr,
					    &ipv6_hdr(skb)->daddr,
					    skb->len, proto, 0));
}

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

	th->check = ~tcp_v6_check(skb->len, saddr, daddr, 0);
	skb->csum_start = skb_transport_header(skb) - skb->head;
	skb->csum_offset = offsetof(struct tcphdr, check);
}

static inline void tcp_v6_gso_csum_prep(struct sk_buff *skb)
{
	struct ipv6hdr *ipv6h = ipv6_hdr(skb);
	struct tcphdr *th = tcp_hdr(skb);

	ipv6h->payload_len = 0;
	th->check = ~tcp_v6_check(0, &ipv6h->saddr, &ipv6h->daddr, 0);
}

static inline __sum16 udp_v6_check(int len,
				   const struct in6_addr *saddr,
				   const struct in6_addr *daddr,
				   __wsum base)
{
	return csum_ipv6_magic(saddr, daddr, len, IPPROTO_UDP, base);
}

void udp6_set_csum(bool nocheck, struct sk_buff *skb,
		   const struct in6_addr *saddr,
		   const struct in6_addr *daddr, int len);

int udp6_csum_init(struct sk_buff *skb, struct udphdr *uh, int proto);
#endif
