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
#include <net/netfilter/nf_queue.h>

int ip6_route_me_harder(struct sk_buff *skb)
{
	struct net *net = dev_net(skb_dst(skb)->dev);
	const struct ipv6hdr *iph = ipv6_hdr(skb);
	struct dst_entry *dst;
	struct flowi6 fl6 = {
		.flowi6_oif = skb->sk ? skb->sk->sk_bound_dev_if : 0,
		.flowi6_mark = skb->mark,
		.daddr = iph->daddr,
		.saddr = iph->saddr,
	};

	dst = ip6_route_output(net, skb->sk, &fl6);
	if (dst->error) {
		IP6_INC_STATS(net, ip6_dst_idev(dst), IPSTATS_MIB_OUTNOROUTES);
		LIMIT_NETDEBUG(KERN_DEBUG "ip6_route_me_harder: No more route.\n");
		dst_release(dst);
		return -EINVAL;
	}

	/* Drop old route. */
	skb_dst_drop(skb);

	skb_dst_set(skb, dst);

#ifdef CONFIG_XFRM
	if (!(IP6CB(skb)->flags & IP6SKB_XFRM_TRANSFORMED) &&
	    xfrm_decode_session(skb, flowi6_to_flowi(&fl6), AF_INET6) == 0) {
		skb_dst_set(skb, NULL);
		dst = xfrm_lookup(net, dst, flowi6_to_flowi(&fl6), skb->sk, 0);
		if (IS_ERR(dst))
			return -1;
		skb_dst_set(skb, dst);
	}
#endif

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
	u_int32_t mark;
};

static void nf_ip6_saveroute(const struct sk_buff *skb,
			     struct nf_queue_entry *entry)
{
	struct ip6_rt_info *rt_info = nf_queue_entry_reroute(entry);

	if (entry->hook == NF_INET_LOCAL_OUT) {
		const struct ipv6hdr *iph = ipv6_hdr(skb);

		rt_info->daddr = iph->daddr;
		rt_info->saddr = iph->saddr;
		rt_info->mark = skb->mark;
	}
}

static int nf_ip6_reroute(struct sk_buff *skb,
			  const struct nf_queue_entry *entry)
{
	struct ip6_rt_info *rt_info = nf_queue_entry_reroute(entry);

	if (entry->hook == NF_INET_LOCAL_OUT) {
		const struct ipv6hdr *iph = ipv6_hdr(skb);
		if (!ipv6_addr_equal(&iph->daddr, &rt_info->daddr) ||
		    !ipv6_addr_equal(&iph->saddr, &rt_info->saddr) ||
		    skb->mark != rt_info->mark)
			return ip6_route_me_harder(skb);
	}
	return 0;
}

static int nf_ip6_route(struct net *net, struct dst_entry **dst,
			struct flowi *fl, bool strict)
{
	static const struct ipv6_pinfo fake_pinfo;
	static const struct inet_sock fake_sk = {
		/* makes ip6_route_output set RT6_LOOKUP_F_IFACE: */
		.sk.sk_bound_dev_if = 1,
		.pinet6 = (struct ipv6_pinfo *) &fake_pinfo,
	};
	const void *sk = strict ? &fake_sk : NULL;
	struct dst_entry *result;
	int err;

	result = ip6_route_output(net, sk, &fl->u.ip6);
	err = result->error;
	if (err)
		dst_release(result);
	else
		*dst = result;
	return err;
}

__sum16 nf_ip6_checksum(struct sk_buff *skb, unsigned int hook,
			     unsigned int dataoff, u_int8_t protocol)
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
				       u_int8_t protocol)
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

static const struct nf_afinfo nf_ip6_afinfo = {
	.family			= AF_INET6,
	.checksum		= nf_ip6_checksum,
	.checksum_partial	= nf_ip6_checksum_partial,
	.route			= nf_ip6_route,
	.saveroute		= nf_ip6_saveroute,
	.reroute		= nf_ip6_reroute,
	.route_key_size		= sizeof(struct ip6_rt_info),
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
