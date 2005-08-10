#include <linux/config.h>
#include <linux/init.h>

#ifdef CONFIG_NETFILTER

#include <linux/kernel.h>
#include <linux/ipv6.h>
#include <linux/netfilter.h>
#include <linux/netfilter_ipv6.h>
#include <net/dst.h>
#include <net/ipv6.h>
#include <net/ip6_route.h>

int ip6_route_me_harder(struct sk_buff *skb)
{
	struct ipv6hdr *iph = skb->nh.ipv6h;
	struct dst_entry *dst;
	struct flowi fl = {
		.oif = skb->sk ? skb->sk->sk_bound_dev_if : 0,
		.nl_u =
		{ .ip6_u =
		  { .daddr = iph->daddr,
		    .saddr = iph->saddr, } },
		.proto = iph->nexthdr,
	};

	dst = ip6_route_output(skb->sk, &fl);

	if (dst->error) {
		IP6_INC_STATS(IPSTATS_MIB_OUTNOROUTES);
		LIMIT_NETDEBUG(KERN_DEBUG "ip6_route_me_harder: No more route.\n");
		dst_release(dst);
		return -EINVAL;
	}

	/* Drop old route. */
	dst_release(skb->dst);

	skb->dst = dst;
	return 0;
}
EXPORT_SYMBOL(ip6_route_me_harder);

/*
 * Extra routing may needed on local out, as the QUEUE target never
 * returns control to the table.
 */

struct ip6_rt_info {
	struct in6_addr daddr;
	struct in6_addr saddr;
};

static void save(const struct sk_buff *skb, struct nf_info *info)
{
	struct ip6_rt_info *rt_info = nf_info_reroute(info);

	if (info->hook == NF_IP6_LOCAL_OUT) {
		struct ipv6hdr *iph = skb->nh.ipv6h;

		rt_info->daddr = iph->daddr;
		rt_info->saddr = iph->saddr;
	}
}

static int reroute(struct sk_buff **pskb, const struct nf_info *info)
{
	struct ip6_rt_info *rt_info = nf_info_reroute(info);

	if (info->hook == NF_IP6_LOCAL_OUT) {
		struct ipv6hdr *iph = (*pskb)->nh.ipv6h;
		if (!ipv6_addr_equal(&iph->daddr, &rt_info->daddr) ||
		    !ipv6_addr_equal(&iph->saddr, &rt_info->saddr))
			return ip6_route_me_harder(*pskb);
	}
	return 0;
}

static struct nf_queue_rerouter ip6_reroute = {
	.rer_size	= sizeof(struct ip6_rt_info),
	.save 		= &save,
	.reroute	= &reroute,
};

int __init ipv6_netfilter_init(void)
{
	return nf_register_queue_rerouter(PF_INET6, &ip6_reroute);
}

void ipv6_netfilter_fini(void)
{
	nf_unregister_queue_rerouter(PF_INET6);
}

#else /* CONFIG_NETFILTER */
int __init ipv6_netfilter_init(void)
{
	return 0;
}

void ipv6_netfilter_fini(void)
{
}
#endif /* CONFIG_NETFILTER */
