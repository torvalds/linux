#ifndef _INET_ECN_H_
#define _INET_ECN_H_

#include <linux/ip.h>
#include <linux/skbuff.h>

#include <net/inet_sock.h>
#include <net/dsfield.h>

enum {
	INET_ECN_NOT_ECT = 0,
	INET_ECN_ECT_1 = 1,
	INET_ECN_ECT_0 = 2,
	INET_ECN_CE = 3,
	INET_ECN_MASK = 3,
};

static inline int INET_ECN_is_ce(__u8 dsfield)
{
	return (dsfield & INET_ECN_MASK) == INET_ECN_CE;
}

static inline int INET_ECN_is_not_ect(__u8 dsfield)
{
	return (dsfield & INET_ECN_MASK) == INET_ECN_NOT_ECT;
}

static inline int INET_ECN_is_capable(__u8 dsfield)
{
	return dsfield & INET_ECN_ECT_0;
}

static inline __u8 INET_ECN_encapsulate(__u8 outer, __u8 inner)
{
	outer &= ~INET_ECN_MASK;
	outer |= !INET_ECN_is_ce(inner) ? (inner & INET_ECN_MASK) :
					  INET_ECN_ECT_0;
	return outer;
}

static inline void INET_ECN_xmit(struct sock *sk)
{
	inet_sk(sk)->tos |= INET_ECN_ECT_0;
	if (inet6_sk(sk) != NULL)
		inet6_sk(sk)->tclass |= INET_ECN_ECT_0;
}

static inline void INET_ECN_dontxmit(struct sock *sk)
{
	inet_sk(sk)->tos &= ~INET_ECN_MASK;
	if (inet6_sk(sk) != NULL)
		inet6_sk(sk)->tclass &= ~INET_ECN_MASK;
}

#define IP6_ECN_flow_init(label) do {		\
      (label) &= ~htonl(INET_ECN_MASK << 20);	\
    } while (0)

#define	IP6_ECN_flow_xmit(sk, label) do {				\
	if (INET_ECN_is_capable(inet6_sk(sk)->tclass))			\
		(label) |= htonl(INET_ECN_ECT_0 << 20);			\
    } while (0)

static inline int IP_ECN_set_ce(struct iphdr *iph)
{
	u32 check = (__force u32)iph->check;
	u32 ecn = (iph->tos + 1) & INET_ECN_MASK;

	/*
	 * After the last operation we have (in binary):
	 * INET_ECN_NOT_ECT => 01
	 * INET_ECN_ECT_1   => 10
	 * INET_ECN_ECT_0   => 11
	 * INET_ECN_CE      => 00
	 */
	if (!(ecn & 2))
		return !ecn;

	/*
	 * The following gives us:
	 * INET_ECN_ECT_1 => check += htons(0xFFFD)
	 * INET_ECN_ECT_0 => check += htons(0xFFFE)
	 */
	check += (__force u16)htons(0xFFFB) + (__force u16)htons(ecn);

	iph->check = (__force __sum16)(check + (check>=0xFFFF));
	iph->tos |= INET_ECN_CE;
	return 1;
}

static inline void IP_ECN_clear(struct iphdr *iph)
{
	iph->tos &= ~INET_ECN_MASK;
}

static inline void ipv4_copy_dscp(unsigned int dscp, struct iphdr *inner)
{
	dscp &= ~INET_ECN_MASK;
	ipv4_change_dsfield(inner, INET_ECN_MASK, dscp);
}

struct ipv6hdr;

static inline int IP6_ECN_set_ce(struct ipv6hdr *iph)
{
	if (INET_ECN_is_not_ect(ipv6_get_dsfield(iph)))
		return 0;
	*(__be32*)iph |= htonl(INET_ECN_CE << 20);
	return 1;
}

static inline void IP6_ECN_clear(struct ipv6hdr *iph)
{
	*(__be32*)iph &= ~htonl(INET_ECN_MASK << 20);
}

static inline void ipv6_copy_dscp(unsigned int dscp, struct ipv6hdr *inner)
{
	dscp &= ~INET_ECN_MASK;
	ipv6_change_dsfield(inner, INET_ECN_MASK, dscp);
}

static inline int INET_ECN_set_ce(struct sk_buff *skb)
{
	switch (skb->protocol) {
	case cpu_to_be16(ETH_P_IP):
		if (skb->network_header + sizeof(struct iphdr) <= skb->tail)
			return IP_ECN_set_ce(ip_hdr(skb));
		break;

	case cpu_to_be16(ETH_P_IPV6):
		if (skb->network_header + sizeof(struct ipv6hdr) <= skb->tail)
			return IP6_ECN_set_ce(ipv6_hdr(skb));
		break;
	}

	return 0;
}

#endif
