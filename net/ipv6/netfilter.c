#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/ipv6.h>
#include <linux/netfilter.h>
#include <linux/netfilter_ipv6.h>
#include <net/dst.h>
#include <net/ipv6.h>
#include <net/ip6_route.h>
#include <net/xfrm.h>
#include <net/ip6_checksum.h>

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
	};

	dst = ip6_route_output(skb->sk, &fl);

#ifdef CONFIG_XFRM
	if (!(IP6CB(skb)->flags & IP6SKB_XFRM_TRANSFORMED) &&
	    xfrm_decode_session(skb, &fl, AF_INET6) == 0)
		if (xfrm_lookup(&skb->dst, &fl, skb->sk, 0))
			return -1;
#endif

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

static void nf_ip6_saveroute(const struct sk_buff *skb, struct nf_info *info)
{
	struct ip6_rt_info *rt_info = nf_info_reroute(info);

	if (info->hook == NF_IP6_LOCAL_OUT) {
		struct ipv6hdr *iph = skb->nh.ipv6h;

		rt_info->daddr = iph->daddr;
		rt_info->saddr = iph->saddr;
	}
}

static int nf_ip6_reroute(struct sk_buff **pskb, const struct nf_info *info)
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

unsigned int nf_ip6_checksum(struct sk_buff *skb, unsigned int hook,
			     unsigned int dataoff, u_int8_t protocol)
{
	struct ipv6hdr *ip6h = skb->nh.ipv6h;
	unsigned int csum = 0;

	switch (skb->ip_summed) {
	case CHECKSUM_HW:
		if (hook != NF_IP6_PRE_ROUTING && hook != NF_IP6_LOCAL_IN)
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
		skb->csum = ~csum_ipv6_magic(&ip6h->saddr, &ip6h->daddr,
					     skb->len - dataoff,
					     protocol,
					     csum_sub(0,
						      skb_checksum(skb, 0,
							           dataoff, 0)));
		csum = __skb_checksum_complete(skb);
	}
	return csum;
}

EXPORT_SYMBOL(nf_ip6_checksum);

static struct nf_afinfo nf_ip6_afinfo = {
	.family		= AF_INET6,
	.checksum	= nf_ip6_checksum,
	.saveroute	= nf_ip6_saveroute,
	.reroute	= nf_ip6_reroute,
	.route_key_size	= sizeof(struct ip6_rt_info),
};

int __init ipv6_netfilter_init(void)
{
	return nf_register_afinfo(&nf_ip6_afinfo);
}

/* This can be called from inet6_init() on errors, so it cannot
 * be marked __exit. -DaveM
 */
void ipv6_netfilter_fini(void)
{
	nf_unregister_afinfo(&nf_ip6_afinfo);
}
