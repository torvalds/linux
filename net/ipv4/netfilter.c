/*
 * IPv4 specific functions of netfilter core
 *
 * Rusty Russell (C) 2000 -- This code is GPL.
 * Patrick McHardy (C) 2006-2012
 */
#include <linux/kernel.h>
#include <linux/netfilter.h>
#include <linux/netfilter_ipv4.h>
#include <linux/ip.h>
#include <linux/skbuff.h>
#include <linux/gfp.h>
#include <linux/export.h>
#include <net/route.h>
#include <net/xfrm.h>
#include <net/ip.h>
#include <net/netfilter/nf_queue.h>

/* route_me_harder function, used by iptable_nat, iptable_mangle + ip_queue */
int ip_route_me_harder(struct net *net, struct sk_buff *skb, unsigned int addr_type)
{
	const struct iphdr *iph = ip_hdr(skb);
	struct rtable *rt;
	struct flowi4 fl4 = {};
	__be32 saddr = iph->saddr;
	const struct sock *sk = skb_to_full_sk(skb);
	__u8 flags = sk ? inet_sk_flowi_flags(sk) : 0;
	struct net_device *dev = skb_dst(skb)->dev;
	unsigned int hh_len;

	if (addr_type == RTN_UNSPEC)
		addr_type = inet_addr_type_dev_table(net, dev, saddr);
	if (addr_type == RTN_LOCAL || addr_type == RTN_UNICAST)
		flags |= FLOWI_FLAG_ANYSRC;
	else
		saddr = 0;

	/* some non-standard hacks like ipt_REJECT.c:send_reset() can cause
	 * packets with foreign saddr to appear on the NF_INET_LOCAL_OUT hook.
	 */
	fl4.daddr = iph->daddr;
	fl4.saddr = saddr;
	fl4.flowi4_tos = RT_TOS(iph->tos);
	fl4.flowi4_oif = sk ? sk->sk_bound_dev_if : 0;
	if (!fl4.flowi4_oif)
		fl4.flowi4_oif = l3mdev_master_ifindex(dev);
	fl4.flowi4_mark = skb->mark;
	fl4.flowi4_flags = flags;
	rt = ip_route_output_key(net, &fl4);
	if (IS_ERR(rt))
		return PTR_ERR(rt);

	/* Drop old route. */
	skb_dst_drop(skb);
	skb_dst_set(skb, &rt->dst);

	if (skb_dst(skb)->error)
		return skb_dst(skb)->error;

#ifdef CONFIG_XFRM
	if (!(IPCB(skb)->flags & IPSKB_XFRM_TRANSFORMED) &&
	    xfrm_decode_session(skb, flowi4_to_flowi(&fl4), AF_INET) == 0) {
		struct dst_entry *dst = skb_dst(skb);
		skb_dst_set(skb, NULL);
		dst = xfrm_lookup(net, dst, flowi4_to_flowi(&fl4), sk, 0);
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
EXPORT_SYMBOL(ip_route_me_harder);

int nf_ip_route(struct net *net, struct dst_entry **dst, struct flowi *fl,
		bool strict __always_unused)
{
	struct rtable *rt = ip_route_output_key(net, &fl->u.ip4);
	if (IS_ERR(rt))
		return PTR_ERR(rt);
	*dst = &rt->dst;
	return 0;
}
EXPORT_SYMBOL_GPL(nf_ip_route);
