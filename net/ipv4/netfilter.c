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

/*
 * Extra routing may needed on local out, as the QUEUE target never
 * returns control to the table.
 */

struct ip_rt_info {
	__be32 daddr;
	__be32 saddr;
	u_int8_t tos;
	u_int32_t mark;
};

static void nf_ip_saveroute(const struct sk_buff *skb,
			    struct nf_queue_entry *entry)
{
	struct ip_rt_info *rt_info = nf_queue_entry_reroute(entry);

	if (entry->state.hook == NF_INET_LOCAL_OUT) {
		const struct iphdr *iph = ip_hdr(skb);

		rt_info->tos = iph->tos;
		rt_info->daddr = iph->daddr;
		rt_info->saddr = iph->saddr;
		rt_info->mark = skb->mark;
	}
}

static int nf_ip_reroute(struct net *net, struct sk_buff *skb,
			 const struct nf_queue_entry *entry)
{
	const struct ip_rt_info *rt_info = nf_queue_entry_reroute(entry);

	if (entry->state.hook == NF_INET_LOCAL_OUT) {
		const struct iphdr *iph = ip_hdr(skb);

		if (!(iph->tos == rt_info->tos &&
		      skb->mark == rt_info->mark &&
		      iph->daddr == rt_info->daddr &&
		      iph->saddr == rt_info->saddr))
			return ip_route_me_harder(net, skb, RTN_UNSPEC);
	}
	return 0;
}

__sum16 nf_ip_checksum(struct sk_buff *skb, unsigned int hook,
			    unsigned int dataoff, u_int8_t protocol)
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

static __sum16 nf_ip_checksum_partial(struct sk_buff *skb, unsigned int hook,
				      unsigned int dataoff, unsigned int len,
				      u_int8_t protocol)
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

static int nf_ip_route(struct net *net, struct dst_entry **dst,
		       struct flowi *fl, bool strict __always_unused)
{
	struct rtable *rt = ip_route_output_key(net, &fl->u.ip4);
	if (IS_ERR(rt))
		return PTR_ERR(rt);
	*dst = &rt->dst;
	return 0;
}

static const struct nf_afinfo nf_ip_afinfo = {
	.family			= AF_INET,
	.checksum		= nf_ip_checksum,
	.checksum_partial	= nf_ip_checksum_partial,
	.route			= nf_ip_route,
	.saveroute		= nf_ip_saveroute,
	.reroute		= nf_ip_reroute,
	.route_key_size		= sizeof(struct ip_rt_info),
};

static int __init ipv4_netfilter_init(void)
{
	return nf_register_afinfo(&nf_ip_afinfo);
}
subsys_initcall(ipv4_netfilter_init);
