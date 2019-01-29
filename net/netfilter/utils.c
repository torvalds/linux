// SPDX-License-Identifier: GPL-2.0
#include <linux/kernel.h>
#include <linux/netfilter.h>
#include <linux/netfilter_ipv4.h>
#include <linux/netfilter_ipv6.h>
#include <net/netfilter/nf_queue.h>
#include <net/ip6_checksum.h>

#ifdef CONFIG_INET
__sum16 nf_ip_checksum(struct sk_buff *skb, unsigned int hook,
		       unsigned int dataoff, u8 protocol)
{
	const struct iphdr *iph = ip_hdr(skb);
	__sum16 csum = 0;

	switch (skb->ip_summed) {
	case CHECKSUM_COMPLETE:
		if (hook != NF_INET_PRE_ROUTING && hook != NF_INET_LOCAL_IN)
			break;
		if ((protocol == 0 && !csum_fold(skb->csum)) ||
		    !csum_tcpudp_magic(iph->saddr, iph->daddr,
				       skb->len - dataoff, protocol,
				       skb->csum)) {
			skb->ip_summed = CHECKSUM_UNNECESSARY;
			break;
		}
		/* fall through */
	case CHECKSUM_NONE:
		if (protocol == 0)
			skb->csum = 0;
		else
			skb->csum = csum_tcpudp_nofold(iph->saddr, iph->daddr,
						       skb->len - dataoff,
						       protocol, 0);
		csum = __skb_checksum_complete(skb);
	}
	return csum;
}
EXPORT_SYMBOL(nf_ip_checksum);
#endif

static __sum16 nf_ip_checksum_partial(struct sk_buff *skb, unsigned int hook,
				      unsigned int dataoff, unsigned int len,
				      u8 protocol)
{
	const struct iphdr *iph = ip_hdr(skb);
	__sum16 csum = 0;

	switch (skb->ip_summed) {
	case CHECKSUM_COMPLETE:
		if (len == skb->len - dataoff)
			return nf_ip_checksum(skb, hook, dataoff, protocol);
		/* fall through */
	case CHECKSUM_NONE:
		skb->csum = csum_tcpudp_nofold(iph->saddr, iph->daddr, protocol,
					       skb->len - dataoff, 0);
		skb->ip_summed = CHECKSUM_NONE;
		return __skb_checksum_complete_head(skb, dataoff + len);
	}
	return csum;
}

__sum16 nf_ip6_checksum(struct sk_buff *skb, unsigned int hook,
			unsigned int dataoff, u8 protocol)
{
	const struct ipv6hdr *ip6h = ipv6_hdr(skb);
	__sum16 csum = 0;

	switch (skb->ip_summed) {
	case CHECKSUM_COMPLETE:
		if (hook != NF_INET_PRE_ROUTING && hook != NF_INET_LOCAL_IN)
			break;
		if (!csum_ipv6_magic(&ip6h->saddr, &ip6h->daddr,
				     skb->len - dataoff, protocol,
				     csum_sub(skb->csum,
					      skb_checksum(skb, 0,
							   dataoff, 0)))) {
			skb->ip_summed = CHECKSUM_UNNECESSARY;
			break;
		}
		/* fall through */
	case CHECKSUM_NONE:
		skb->csum = ~csum_unfold(
				csum_ipv6_magic(&ip6h->saddr, &ip6h->daddr,
					     skb->len - dataoff,
					     protocol,
					     csum_sub(0,
						      skb_checksum(skb, 0,
								   dataoff, 0))));
		csum = __skb_checksum_complete(skb);
	}
	return csum;
}
EXPORT_SYMBOL(nf_ip6_checksum);

static __sum16 nf_ip6_checksum_partial(struct sk_buff *skb, unsigned int hook,
				       unsigned int dataoff, unsigned int len,
				       u8 protocol)
{
	const struct ipv6hdr *ip6h = ipv6_hdr(skb);
	__wsum hsum;
	__sum16 csum = 0;

	switch (skb->ip_summed) {
	case CHECKSUM_COMPLETE:
		if (len == skb->len - dataoff)
			return nf_ip6_checksum(skb, hook, dataoff, protocol);
		/* fall through */
	case CHECKSUM_NONE:
		hsum = skb_checksum(skb, 0, dataoff, 0);
		skb->csum = ~csum_unfold(csum_ipv6_magic(&ip6h->saddr,
							 &ip6h->daddr,
							 skb->len - dataoff,
							 protocol,
							 csum_sub(0, hsum)));
		skb->ip_summed = CHECKSUM_NONE;
		return __skb_checksum_complete_head(skb, dataoff + len);
	}
	return csum;
};

__sum16 nf_checksum(struct sk_buff *skb, unsigned int hook,
		    unsigned int dataoff, u8 protocol,
		    unsigned short family)
{
	__sum16 csum = 0;

	switch (family) {
	case AF_INET:
		csum = nf_ip_checksum(skb, hook, dataoff, protocol);
		break;
	case AF_INET6:
		csum = nf_ip6_checksum(skb, hook, dataoff, protocol);
		break;
	}

	return csum;
}
EXPORT_SYMBOL_GPL(nf_checksum);

__sum16 nf_checksum_partial(struct sk_buff *skb, unsigned int hook,
			    unsigned int dataoff, unsigned int len,
			    u8 protocol, unsigned short family)
{
	__sum16 csum = 0;

	switch (family) {
	case AF_INET:
		csum = nf_ip_checksum_partial(skb, hook, dataoff, len,
					      protocol);
		break;
	case AF_INET6:
		csum = nf_ip6_checksum_partial(skb, hook, dataoff, len,
					       protocol);
		break;
	}

	return csum;
}
EXPORT_SYMBOL_GPL(nf_checksum_partial);

int nf_route(struct net *net, struct dst_entry **dst, struct flowi *fl,
	     bool strict, unsigned short family)
{
	const struct nf_ipv6_ops *v6ops;
	int ret = 0;

	switch (family) {
	case AF_INET:
		ret = nf_ip_route(net, dst, fl, strict);
		break;
	case AF_INET6:
		v6ops = rcu_dereference(nf_ipv6_ops);
		if (v6ops)
			ret = v6ops->route(net, dst, fl, strict);
		break;
	}

	return ret;
}
EXPORT_SYMBOL_GPL(nf_route);

static int nf_ip_reroute(struct sk_buff *skb, const struct nf_queue_entry *entry)
{
#ifdef CONFIG_INET
	const struct ip_rt_info *rt_info = nf_queue_entry_reroute(entry);

	if (entry->state.hook == NF_INET_LOCAL_OUT) {
		const struct iphdr *iph = ip_hdr(skb);

		if (!(iph->tos == rt_info->tos &&
		      skb->mark == rt_info->mark &&
		      iph->daddr == rt_info->daddr &&
		      iph->saddr == rt_info->saddr))
			return ip_route_me_harder(entry->state.net, skb,
						  RTN_UNSPEC);
	}
#endif
	return 0;
}

int nf_reroute(struct sk_buff *skb, struct nf_queue_entry *entry)
{
	const struct nf_ipv6_ops *v6ops;
	int ret = 0;

	switch (entry->state.pf) {
	case AF_INET:
		ret = nf_ip_reroute(skb, entry);
		break;
	case AF_INET6:
		v6ops = rcu_dereference(nf_ipv6_ops);
		if (v6ops)
			ret = v6ops->reroute(skb, entry);
		break;
	}
	return ret;
}
