/* SPDX-License-Identifier: GPL-2.0 */
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

extern int sysctl_tunnel_ecn_log;

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

/*
 * RFC 3168 9.1.1
 *  The full-functionality option for ECN encapsulation is to copy the
 *  ECN codepoint of the inside header to the outside header on
 *  encapsulation if the inside header is not-ECT or ECT, and to set the
 *  ECN codepoint of the outside header to ECT(0) if the ECN codepoint of
 *  the inside header is CE.
 */
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

/* Note:
 * IP_ECN_set_ce() has to tweak IPV4 checksum when setting CE,
 * meaning both changes have no effect on skb->csum if/when CHECKSUM_COMPLETE
 * In IPv6 case, no checksum compensates the change in IPv6 header,
 * so we have to update skb->csum.
 */
static inline int IP6_ECN_set_ce(struct sk_buff *skb, struct ipv6hdr *iph)
{
	__be32 from, to;

	if (INET_ECN_is_not_ect(ipv6_get_dsfield(iph)))
		return 0;

	from = *(__be32 *)iph;
	to = from | htonl(INET_ECN_CE << 20);
	*(__be32 *)iph = to;
	if (skb->ip_summed == CHECKSUM_COMPLETE)
		skb->csum = csum_add(csum_sub(skb->csum, (__force __wsum)from),
				     (__force __wsum)to);
	return 1;
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
		if (skb_network_header(skb) + sizeof(struct iphdr) <=
		    skb_tail_pointer(skb))
			return IP_ECN_set_ce(ip_hdr(skb));
		break;

	case cpu_to_be16(ETH_P_IPV6):
		if (skb_network_header(skb) + sizeof(struct ipv6hdr) <=
		    skb_tail_pointer(skb))
			return IP6_ECN_set_ce(skb, ipv6_hdr(skb));
		break;
	}

	return 0;
}

/*
 * RFC 6040 4.2
 *  To decapsulate the inner header at the tunnel egress, a compliant
 *  tunnel egress MUST set the outgoing ECN field to the codepoint at the
 *  intersection of the appropriate arriving inner header (row) and outer
 *  header (column) in Figure 4
 *
 *      +---------+------------------------------------------------+
 *      |Arriving |            Arriving Outer Header               |
 *      |   Inner +---------+------------+------------+------------+
 *      |  Header | Not-ECT | ECT(0)     | ECT(1)     |     CE     |
 *      +---------+---------+------------+------------+------------+
 *      | Not-ECT | Not-ECT |Not-ECT(!!!)|Not-ECT(!!!)| <drop>(!!!)|
 *      |  ECT(0) |  ECT(0) | ECT(0)     | ECT(1)     |     CE     |
 *      |  ECT(1) |  ECT(1) | ECT(1) (!) | ECT(1)     |     CE     |
 *      |    CE   |      CE |     CE     |     CE(!!!)|     CE     |
 *      +---------+---------+------------+------------+------------+
 *
 *             Figure 4: New IP in IP Decapsulation Behaviour
 *
 *  returns 0 on success
 *          1 if something is broken and should be logged (!!! above)
 *          2 if packet should be dropped
 */
static inline int __INET_ECN_decapsulate(__u8 outer, __u8 inner, bool *set_ce)
{
	if (INET_ECN_is_not_ect(inner)) {
		switch (outer & INET_ECN_MASK) {
		case INET_ECN_NOT_ECT:
			return 0;
		case INET_ECN_ECT_0:
		case INET_ECN_ECT_1:
			return 1;
		case INET_ECN_CE:
			return 2;
		}
	}

	*set_ce = INET_ECN_is_ce(outer);
	return 0;
}

static inline int INET_ECN_decapsulate(struct sk_buff *skb,
				       __u8 outer, __u8 inner)
{
	bool set_ce = false;
	int rc;

	rc = __INET_ECN_decapsulate(outer, inner, &set_ce);
	if (!rc && set_ce)
		INET_ECN_set_ce(skb);

	return rc;
}

static inline int IP_ECN_decapsulate(const struct iphdr *oiph,
				     struct sk_buff *skb)
{
	__u8 inner;

	if (skb->protocol == htons(ETH_P_IP))
		inner = ip_hdr(skb)->tos;
	else if (skb->protocol == htons(ETH_P_IPV6))
		inner = ipv6_get_dsfield(ipv6_hdr(skb));
	else
		return 0;

	return INET_ECN_decapsulate(skb, oiph->tos, inner);
}

static inline int IP6_ECN_decapsulate(const struct ipv6hdr *oipv6h,
				      struct sk_buff *skb)
{
	__u8 inner;

	if (skb->protocol == htons(ETH_P_IP))
		inner = ip_hdr(skb)->tos;
	else if (skb->protocol == htons(ETH_P_IPV6))
		inner = ipv6_get_dsfield(ipv6_hdr(skb));
	else
		return 0;

	return INET_ECN_decapsulate(skb, ipv6_get_dsfield(oipv6h), inner);
}
#endif
