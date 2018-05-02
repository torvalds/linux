#include <linux/kernel.h>
#include <linux/netfilter.h>
#include <linux/netfilter_ipv4.h>
#include <linux/netfilter_ipv6.h>
#include <net/netfilter/nf_queue.h>

__sum16 nf_checksum(struct sk_buff *skb, unsigned int hook,
		    unsigned int dataoff, u_int8_t protocol,
		    unsigned short family)
{
	const struct nf_ipv6_ops *v6ops;
	__sum16 csum = 0;

	switch (family) {
	case AF_INET:
		csum = nf_ip_checksum(skb, hook, dataoff, protocol);
		break;
	case AF_INET6:
		v6ops = rcu_dereference(nf_ipv6_ops);
		if (v6ops)
			csum = v6ops->checksum(skb, hook, dataoff, protocol);
		break;
	}

	return csum;
}
EXPORT_SYMBOL_GPL(nf_checksum);

__sum16 nf_checksum_partial(struct sk_buff *skb, unsigned int hook,
			    unsigned int dataoff, unsigned int len,
			    u_int8_t protocol, unsigned short family)
{
	const struct nf_ipv6_ops *v6ops;
	__sum16 csum = 0;

	switch (family) {
	case AF_INET:
		csum = nf_ip_checksum_partial(skb, hook, dataoff, len,
					      protocol);
		break;
	case AF_INET6:
		v6ops = rcu_dereference(nf_ipv6_ops);
		if (v6ops)
			csum = v6ops->checksum_partial(skb, hook, dataoff, len,
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
