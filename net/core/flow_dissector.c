#include <linux/skbuff.h>
#include <linux/export.h>
#include <linux/ip.h>
#include <linux/ipv6.h>
#include <linux/if_vlan.h>
#include <net/ip.h>
#include <linux/if_tunnel.h>
#include <linux/if_pppox.h>
#include <linux/ppp_defs.h>
#include <net/flow_keys.h>

/* copy saddr & daddr, possibly using 64bit load/store
 * Equivalent to :	flow->src = iph->saddr;
 *			flow->dst = iph->daddr;
 */
static void iph_to_flow_copy_addrs(struct flow_keys *flow, const struct iphdr *iph)
{
	BUILD_BUG_ON(offsetof(typeof(*flow), dst) !=
		     offsetof(typeof(*flow), src) + sizeof(flow->src));
	memcpy(&flow->src, &iph->saddr, sizeof(flow->src) + sizeof(flow->dst));
}

bool skb_flow_dissect(const struct sk_buff *skb, struct flow_keys *flow)
{
	int poff, nhoff = skb_network_offset(skb);
	u8 ip_proto;
	__be16 proto = skb->protocol;

	memset(flow, 0, sizeof(*flow));

again:
	switch (proto) {
	case __constant_htons(ETH_P_IP): {
		const struct iphdr *iph;
		struct iphdr _iph;
ip:
		iph = skb_header_pointer(skb, nhoff, sizeof(_iph), &_iph);
		if (!iph)
			return false;

		if (ip_is_fragment(iph))
			ip_proto = 0;
		else
			ip_proto = iph->protocol;
		iph_to_flow_copy_addrs(flow, iph);
		nhoff += iph->ihl * 4;
		break;
	}
	case __constant_htons(ETH_P_IPV6): {
		const struct ipv6hdr *iph;
		struct ipv6hdr _iph;
ipv6:
		iph = skb_header_pointer(skb, nhoff, sizeof(_iph), &_iph);
		if (!iph)
			return false;

		ip_proto = iph->nexthdr;
		flow->src = iph->saddr.s6_addr32[3];
		flow->dst = iph->daddr.s6_addr32[3];
		nhoff += sizeof(struct ipv6hdr);
		break;
	}
	case __constant_htons(ETH_P_8021Q): {
		const struct vlan_hdr *vlan;
		struct vlan_hdr _vlan;

		vlan = skb_header_pointer(skb, nhoff, sizeof(_vlan), &_vlan);
		if (!vlan)
			return false;

		proto = vlan->h_vlan_encapsulated_proto;
		nhoff += sizeof(*vlan);
		goto again;
	}
	case __constant_htons(ETH_P_PPP_SES): {
		struct {
			struct pppoe_hdr hdr;
			__be16 proto;
		} *hdr, _hdr;
		hdr = skb_header_pointer(skb, nhoff, sizeof(_hdr), &_hdr);
		if (!hdr)
			return false;
		proto = hdr->proto;
		nhoff += PPPOE_SES_HLEN;
		switch (proto) {
		case __constant_htons(PPP_IP):
			goto ip;
		case __constant_htons(PPP_IPV6):
			goto ipv6;
		default:
			return false;
		}
	}
	default:
		return false;
	}

	switch (ip_proto) {
	case IPPROTO_GRE: {
		struct gre_hdr {
			__be16 flags;
			__be16 proto;
		} *hdr, _hdr;

		hdr = skb_header_pointer(skb, nhoff, sizeof(_hdr), &_hdr);
		if (!hdr)
			return false;
		/*
		 * Only look inside GRE if version zero and no
		 * routing
		 */
		if (!(hdr->flags & (GRE_VERSION|GRE_ROUTING))) {
			proto = hdr->proto;
			nhoff += 4;
			if (hdr->flags & GRE_CSUM)
				nhoff += 4;
			if (hdr->flags & GRE_KEY)
				nhoff += 4;
			if (hdr->flags & GRE_SEQ)
				nhoff += 4;
			goto again;
		}
		break;
	}
	case IPPROTO_IPIP:
		goto again;
	default:
		break;
	}

	flow->ip_proto = ip_proto;
	poff = proto_ports_offset(ip_proto);
	if (poff >= 0) {
		__be32 *ports, _ports;

		nhoff += poff;
		ports = skb_header_pointer(skb, nhoff, sizeof(_ports), &_ports);
		if (ports)
			flow->ports = *ports;
	}

	return true;
}
EXPORT_SYMBOL(skb_flow_dissect);
