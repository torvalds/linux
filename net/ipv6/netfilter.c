#include <linux/config.h>
#include <linux/init.h>

#ifdef CONFIG_NETFILTER

#include <linux/kernel.h>
#include <linux/ipv6.h>
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
		LIMIT_NETDEBUG(
			printk(KERN_DEBUG "ip6_route_me_harder: No more route.\n"));
		dst_release(dst);
		return -EINVAL;
	}

	/* Drop old route. */
	dst_release(skb->dst);

	skb->dst = dst;
	return 0;
}
EXPORT_SYMBOL(ip6_route_me_harder);

#endif /* CONFIG_NETFILTER */
