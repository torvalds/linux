/*
 * IPv6 library code, needed by static components when full IPv6 support is
 * not configured or static.  These functions are needed by GSO/GRO implementation.
 */
#include <linux/export.h>
#include <net/ipv6.h>
#include <net/ip6_fib.h>
#include <net/addrconf.h>

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
	unsigned int packet_len = skb_tail_pointer(skb) -
		skb_network_header(skb);
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

#if IS_ENABLED(CONFIG_IPV6)
int ip6_dst_hoplimit(struct dst_entry *dst)
{
	int hoplimit = dst_metric_raw(dst, RTAX_HOPLIMIT);
	if (hoplimit == 0) {
		struct net_device *dev = dst->dev;
		struct inet6_dev *idev;

		rcu_read_lock();
		idev = __in6_dev_get(dev);
		if (idev)
			hoplimit = idev->cnf.hop_limit;
		else
			hoplimit = dev_net(dev)->ipv6.devconf_all->hop_limit;
		rcu_read_unlock();
	}
	return hoplimit;
}
EXPORT_SYMBOL(ip6_dst_hoplimit);
#endif

int __ip6_local_out(struct sk_buff *skb)
{
	int len;

	len = skb->len - sizeof(struct ipv6hdr);
	if (len > IPV6_MAXPLEN)
		len = 0;
	ipv6_hdr(skb)->payload_len = htons(len);

	return nf_hook(NFPROTO_IPV6, NF_INET_LOCAL_OUT, skb, NULL,
		       skb_dst(skb)->dev, dst_output);
}
EXPORT_SYMBOL_GPL(__ip6_local_out);

int ip6_local_out(struct sk_buff *skb)
{
	int err;

	err = __ip6_local_out(skb);
	if (likely(err == 1))
		err = dst_output(skb);

	return err;
}
EXPORT_SYMBOL_GPL(ip6_local_out);
