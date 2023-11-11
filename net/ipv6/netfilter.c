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
#include <net/netfilter/nf_conntrack_bridge.h>
#include <net/netfilter/ipv6/nf_defrag_ipv6.h>
#include "../bridge/br_private.h"

int ip6_route_me_harder(struct net *net, struct sock *sk_partial, struct sk_buff *skb)
{
	const struct ipv6hdr *iph = ipv6_hdr(skb);
	struct sock *sk = sk_to_full_sk(sk_partial);
	struct net_device *dev = skb_dst(skb)->dev;
	struct flow_keys flkeys;
	unsigned int hh_len;
	struct dst_entry *dst;
	int strict = (ipv6_addr_type(&iph->daddr) &
		      (IPV6_ADDR_MULTICAST | IPV6_ADDR_LINKLOCAL));
	struct flowi6 fl6 = {
		.flowi6_l3mdev = l3mdev_master_ifindex(dev),
		.flowi6_mark = skb->mark,
		.flowi6_uid = sock_net_uid(net, sk),
		.daddr = iph->daddr,
		.saddr = iph->saddr,
	};
	int err;

	if (sk && sk->sk_bound_dev_if)
		fl6.flowi6_oif = sk->sk_bound_dev_if;
	else if (strict)
		fl6.flowi6_oif = dev->ifindex;

	fib6_rules_early_flow_dissect(net, skb, &fl6, &flkeys);
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
			return ip6_route_me_harder(entry->state.net, entry->state.sk, skb);
	}
	return 0;
}

int __nf_ip6_route(struct net *net, struct dst_entry **dst,
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
EXPORT_SYMBOL_GPL(__nf_ip6_route);

int br_ip6_fragment(struct net *net, struct sock *sk, struct sk_buff *skb,
		    struct nf_bridge_frag_data *data,
		    int (*output)(struct net *, struct sock *sk,
				  const struct nf_bridge_frag_data *data,
				  struct sk_buff *))
{
	int frag_max_size = BR_INPUT_SKB_CB(skb)->frag_max_size;
	bool mono_delivery_time = skb->mono_delivery_time;
	ktime_t tstamp = skb->tstamp;
	struct ip6_frag_state state;
	u8 *prevhdr, nexthdr = 0;
	unsigned int mtu, hlen;
	int hroom, err = 0;
	__be32 frag_id;

	err = ip6_find_1stfragopt(skb, &prevhdr);
	if (err < 0)
		goto blackhole;
	hlen = err;
	nexthdr = *prevhdr;

	mtu = skb->dev->mtu;
	if (frag_max_size > mtu ||
	    frag_max_size < IPV6_MIN_MTU)
		goto blackhole;

	mtu = frag_max_size;
	if (mtu < hlen + sizeof(struct frag_hdr) + 8)
		goto blackhole;
	mtu -= hlen + sizeof(struct frag_hdr);

	frag_id = ipv6_select_ident(net, &ipv6_hdr(skb)->daddr,
				    &ipv6_hdr(skb)->saddr);

	if (skb->ip_summed == CHECKSUM_PARTIAL &&
	    (err = skb_checksum_help(skb)))
		goto blackhole;

	hroom = LL_RESERVED_SPACE(skb->dev);
	if (skb_has_frag_list(skb)) {
		unsigned int first_len = skb_pagelen(skb);
		struct ip6_fraglist_iter iter;
		struct sk_buff *frag2;

		if (first_len - hlen > mtu ||
		    skb_headroom(skb) < (hroom + sizeof(struct frag_hdr)))
			goto blackhole;

		if (skb_cloned(skb))
			goto slow_path;

		skb_walk_frags(skb, frag2) {
			if (frag2->len > mtu ||
			    skb_headroom(frag2) < (hlen + hroom + sizeof(struct frag_hdr)))
				goto blackhole;

			/* Partially cloned skb? */
			if (skb_shared(frag2))
				goto slow_path;
		}

		err = ip6_fraglist_init(skb, hlen, prevhdr, nexthdr, frag_id,
					&iter);
		if (err < 0)
			goto blackhole;

		for (;;) {
			/* Prepare header of the next frame,
			 * before previous one went down.
			 */
			if (iter.frag)
				ip6_fraglist_prepare(skb, &iter);

			skb_set_delivery_time(skb, tstamp, mono_delivery_time);
			err = output(net, sk, data, skb);
			if (err || !iter.frag)
				break;

			skb = ip6_fraglist_next(&iter);
		}

		kfree(iter.tmp_hdr);
		if (!err)
			return 0;

		kfree_skb_list(iter.frag);
		return err;
	}
slow_path:
	/* This is a linearized skbuff, the original geometry is lost for us.
	 * This may also be a clone skbuff, we could preserve the geometry for
	 * the copies but probably not worth the effort.
	 */
	ip6_frag_init(skb, hlen, mtu, skb->dev->needed_tailroom,
		      LL_RESERVED_SPACE(skb->dev), prevhdr, nexthdr, frag_id,
		      &state);

	while (state.left > 0) {
		struct sk_buff *skb2;

		skb2 = ip6_frag_next(skb, &state);
		if (IS_ERR(skb2)) {
			err = PTR_ERR(skb2);
			goto blackhole;
		}

		skb_set_delivery_time(skb2, tstamp, mono_delivery_time);
		err = output(net, sk, data, skb2);
		if (err)
			goto blackhole;
	}
	consume_skb(skb);
	return err;

blackhole:
	kfree_skb(skb);
	return 0;
}
EXPORT_SYMBOL_GPL(br_ip6_fragment);

static const struct nf_ipv6_ops ipv6ops = {
#if IS_MODULE(CONFIG_IPV6)
	.chk_addr		= ipv6_chk_addr,
	.route_me_harder	= ip6_route_me_harder,
	.dev_get_saddr		= ipv6_dev_get_saddr,
	.route			= __nf_ip6_route,
#if IS_ENABLED(CONFIG_SYN_COOKIES)
	.cookie_init_sequence	= __cookie_v6_init_sequence,
	.cookie_v6_check	= __cookie_v6_check,
#endif
#endif
	.route_input		= ip6_route_input,
	.fragment		= ip6_fragment,
	.reroute		= nf_ip6_reroute,
#if IS_MODULE(CONFIG_IPV6)
	.br_fragment		= br_ip6_fragment,
#endif
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
