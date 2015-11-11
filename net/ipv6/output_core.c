/*
 * IPv6 library code, needed by static components when full IPv6 support is
 * not configured or static.  These functions are needed by GSO/GRO implementation.
 */
#include <linux/export.h>
#include <net/ipv6.h>
#include <net/ip6_fib.h>

void ipv6_select_ident(struct frag_hdr *fhdr, struct rt6_info *rt)
{
	static atomic_t ipv6_fragmentation_id;
	int old, new;

#if IS_ENABLED(CONFIG_IPV6)
	if (rt && !(rt->dst.flags & DST_NOPEER)) {
		struct inet_peer *peer;
		struct net *net;

		net = dev_net(rt->dst.dev);
		peer = inet_getpeer_v6(net->ipv6.peers, &rt->rt6i_dst.addr, 1);
		if (peer) {
			fhdr->identification = htonl(inet_getid(peer, 0));
			inet_putpeer(peer);
			return;
		}
	}
#endif
	do {
		old = atomic_read(&ipv6_fragmentation_id);
		new = old + 1;
		if (!new)
			new = 1;
	} while (atomic_cmpxchg(&ipv6_fragmentation_id, old, new) != old);
	fhdr->identification = htonl(new);
}
EXPORT_SYMBOL(ipv6_select_ident);

int ip6_find_1stfragopt(struct sk_buff *skb, u8 **nexthdr)
{
	u16 offset = sizeof(struct ipv6hdr);
	struct ipv6_opt_hdr *exthdr =
				(struct ipv6_opt_hdr *)(ipv6_hdr(skb) + 1);
	unsigned int packet_len = skb->tail - skb->network_header;
	int found_rhdr = 0;
	*nexthdr = &ipv6_hdr(skb)->nexthdr;

	while (offset + 1 <= packet_len) {

		switch (**nexthdr) {

		case NEXTHDR_HOP:
			break;
		case NEXTHDR_ROUTING:
			found_rhdr = 1;
			break;
		case NEXTHDR_DEST:
#if IS_ENABLED(CONFIG_IPV6_MIP6)
			if (ipv6_find_tlv(skb, offset, IPV6_TLV_HAO) >= 0)
				break;
#endif
			if (found_rhdr)
				return offset;
			break;
		default :
			return offset;
		}

		offset += ipv6_optlen(exthdr);
		*nexthdr = &exthdr->nexthdr;
		exthdr = (struct ipv6_opt_hdr *)(skb_network_header(skb) +
						 offset);
	}

	return offset;
}
EXPORT_SYMBOL(ip6_find_1stfragopt);
