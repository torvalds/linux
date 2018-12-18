/*
 * IPv6 specific functions of netfilter core
 *
 * Rusty Russell (C) 2000 -- This code is GPL.
 * Patrick McHardy (C) 2006-2012
 */
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/ipv6.h>
#include <linux/netfilter.h>
#include <linux/netfilter_ipv6.h>
#include <linux/export.h>
#include <net/addrconf.h>
#include <net/dst.h>
#include <net/ipv6.h>
#include <net/ip6_route.h>
#include <net/xfrm.h>
#include <net/netfilter/nf_queue.h>

int ip6_route_me_harder(struct net *net, struct sk_buff *skb)
{
	const struct ipv6hdr *iph = ipv6_hdr(skb);
	struct sock *sk = sk_to_full_sk(skb->sk);
	unsigned int hh_len;
	struct dst_entry *dst;
	struct flowi6 fl6 = {
		.flowi6_oif = sk ? sk->sk_bound_dev_if : 0,
		.flowi6_mark = skb->mark,
		.flowi6_uid = sock_net_uid(net, sk),
		.daddr = iph->daddr,
		.saddr = iph->saddr,
	};
	int err;

	dst = ip6_route_output(net, sk, &fl6);
	err = dst->error;
	if (err) {
		IP6_INC_STATS(net, ip6_dst_idev(dst), IPSTATS_MIB_OUTNOROUTES);
		net_dbg_ratelimited("ip6_route_me_harder: No more route\n");
		dst_release(dst);
		return err;
	}

	/* Drop old route. */
	skb_dst_drop(skb);

	skb_dst_set(skb, dst);

#ifdef CONFIG_XFRM
	if (!(IP6CB(skb)->flags & IP6SKB_XFRM_TRANSFORMED) &&
	    xfrm_decode_session(skb, flowi6_to_flowi(&fl6), AF_INET6) == 0) {
		skb_dst_set(skb, NULL);
		dst = xfrm_lookup(net, dst, flowi6_to_flowi(&fl6), sk, 0);
		if (IS_ERR(dst))
			return PTR_ERR(dst);
		skb_dst_set(skb, dst);
	}
#endif

	/* Change in oif may mean change in hh_len. */
	hh_len = skb_dst(skb)->dev->hard_header_len;
	if (skb_headroom(skb) < hh_len &&
	    pskb_expand_head(skb, HH_DATA_ALIGN(hh_len - skb_headroom(skb)),
			     0, GFP_ATOMIC))
		return -ENOMEM;

	return 0;
}
EXPORT_SYMBOL(ip6_route_me_harder);

static int nf_ip6_reroute(struct sk_buff *skb,
			  const struct nf_queue_entry *entry)
{
	struct ip6_rt_info *rt_info = nf_queue_entry_reroute(entry);

	if (entry->state.hook == NF_INET_LOCAL_OUT) {
		const struct ipv6hdr *iph = ipv6_hdr(skb);
		if (!ipv6_addr_equal(&iph->daddr, &rt_info->daddr) ||
		    !ipv6_addr_equal(&iph->saddr, &rt_info->saddr) ||
		    skb->mark != rt_info->mark)
			return ip6_route_me_harder(entry->state.net, skb);
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

static const struct nf_ipv6_ops ipv6ops = {
	.chk_addr		= ipv6_chk_addr,
	.route_input    	= ip6_route_input,
	.fragment		= ip6_fragment,
	.route			= nf_ip6_route,
	.reroute		= nf_ip6_reroute,
};

int __init ipv6_netfilter_init(void)
{
	RCU_INIT_POINTER(nf_ipv6_ops, &ipv6ops);
	return 0;
}

/* This can be called from inet6_init() on errors, so it cannot
 * be marked __exit. -DaveM
 */
void ipv6_netfilter_fini(void)
{
	RCU_INIT_POINTER(nf_ipv6_ops, NULL);
}
