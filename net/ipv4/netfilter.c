/* IPv4 specific functions of netfilter core */
#include <linux/kernel.h>
#include <linux/netfilter.h>
#include <linux/netfilter_ipv4.h>
#include <linux/ip.h>
#include <linux/skbuff.h>
#include <net/route.h>
#include <net/xfrm.h>
#include <net/ip.h>
#include <net/netfilter/nf_queue.h>

/* route_me_harder function, used by iptable_nat, iptable_mangle + ip_queue */
int ip_route_me_harder(struct sk_buff *skb, unsigned addr_type)
{
	const struct iphdr *iph = ip_hdr(skb);
	struct rtable *rt;
	struct flowi fl = {};
	struct dst_entry *odst;
	unsigned int hh_len;
	unsigned int type;

	type = inet_addr_type(&init_net, iph->saddr);
	if (addr_type == RTN_UNSPEC)
		addr_type = type;

	/* some non-standard hacks like ipt_REJECT.c:send_reset() can cause
	 * packets with foreign saddr to appear on the NF_INET_LOCAL_OUT hook.
	 */
	if (addr_type == RTN_LOCAL) {
		fl.nl_u.ip4_u.daddr = iph->daddr;
		if (type == RTN_LOCAL)
			fl.nl_u.ip4_u.saddr = iph->saddr;
		fl.nl_u.ip4_u.tos = RT_TOS(iph->tos);
		fl.oif = skb->sk ? skb->sk->sk_bound_dev_if : 0;
		fl.mark = skb->mark;
		if (ip_route_output_key(&init_net, &rt, &fl) != 0)
			return -1;

		/* Drop old route. */
		dst_release(skb->dst);
		skb->dst = &rt->u.dst;
	} else {
		/* non-local src, find valid iif to satisfy
		 * rp-filter when calling ip_route_input. */
		fl.nl_u.ip4_u.daddr = iph->saddr;
		if (ip_route_output_key(&init_net, &rt, &fl) != 0)
			return -1;

		odst = skb->dst;
		if (ip_route_input(skb, iph->daddr, iph->saddr,
				   RT_TOS(iph->tos), rt->u.dst.dev) != 0) {
			dst_release(&rt->u.dst);
			return -1;
		}
		dst_release(&rt->u.dst);
		dst_release(odst);
	}

	if (skb->dst->error)
		return -1;

#ifdef CONFIG_XFRM
	if (!(IPCB(skb)->flags & IPSKB_XFRM_TRANSFORMED) &&
	    xfrm_decode_session(skb, &fl, AF_INET) == 0)
		if (xfrm_lookup(&skb->dst, &fl, skb->sk, 0))
			return -1;
#endif

	/* Change in oif may mean change in hh_len. */
	hh_len = skb->dst->dev->hard_header_len;
	if (skb_headroom(skb) < hh_len &&
	    pskb_expand_head(skb, hh_len - skb_headroom(skb), 0, GFP_ATOMIC))
		return -1;

	return 0;
}
EXPORT_SYMBOL(ip_route_me_harder);

#ifdef CONFIG_XFRM
int ip_xfrm_me_harder(struct sk_buff *skb)
{
	struct flowi fl;
	unsigned int hh_len;
	struct dst_entry *dst;

	if (IPCB(skb)->flags & IPSKB_XFRM_TRANSFORMED)
		return 0;
	if (xfrm_decode_session(skb, &fl, AF_INET) < 0)
		return -1;

	dst = skb->dst;
	if (dst->xfrm)
		dst = ((struct xfrm_dst *)dst)->route;
	dst_hold(dst);

	if (xfrm_lookup(&dst, &fl, skb->sk, 0) < 0)
		return -1;

	dst_release(skb->dst);
	skb->dst = dst;

	/* Change in oif may mean change in hh_len. */
	hh_len = skb->dst->dev->hard_header_len;
	if (skb_headroom(skb) < hh_len &&
	    pskb_expand_head(skb, hh_len - skb_headroom(skb), 0, GFP_ATOMIC))
		return -1;
	return 0;
}
EXPORT_SYMBOL(ip_xfrm_me_harder);
#endif

void (*ip_nat_decode_session)(struct sk_buff *, struct flowi *);
EXPORT_SYMBOL(ip_nat_decode_session);

/*
 * Extra routing may needed on local out, as the QUEUE target never
 * returns control to the table.
 */

struct ip_rt_info {
	__be32 daddr;
	__be32 saddr;
	u_int8_t tos;
};

static void nf_ip_saveroute(const struct sk_buff *skb,
			    struct nf_queue_entry *entry)
{
	struct ip_rt_info *rt_info = nf_queue_entry_reroute(entry);

	if (entry->hook == NF_INET_LOCAL_OUT) {
		const struct iphdr *iph = ip_hdr(skb);

		rt_info->tos = iph->tos;
		rt_info->daddr = iph->daddr;
		rt_info->saddr = iph->saddr;
	}
}

static int nf_ip_reroute(struct sk_buff *skb,
			 const struct nf_queue_entry *entry)
{
	const struct ip_rt_info *rt_info = nf_queue_entry_reroute(entry);

	if (entry->hook == NF_INET_LOCAL_OUT) {
		const struct iphdr *iph = ip_hdr(skb);

		if (!(iph->tos == rt_info->tos
		      && iph->daddr == rt_info->daddr
		      && iph->saddr == rt_info->saddr))
			return ip_route_me_harder(skb, RTN_UNSPEC);
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

static int nf_ip_route(struct dst_entry **dst, struct flowi *fl)
{
	return ip_route_output_key(&init_net, (struct rtable **)dst, fl);
}

static const struct nf_afinfo nf_ip_afinfo = {
	.family		= AF_INET,
	.checksum	= nf_ip_checksum,
	.route		= nf_ip_route,
	.saveroute	= nf_ip_saveroute,
	.reroute	= nf_ip_reroute,
	.route_key_size	= sizeof(struct ip_rt_info),
};

static int ipv4_netfilter_init(void)
{
	return nf_register_afinfo(&nf_ip_afinfo);
}

static void ipv4_netfilter_fini(void)
{
	nf_unregister_afinfo(&nf_ip_afinfo);
}

module_init(ipv4_netfilter_init);
module_exit(ipv4_netfilter_fini);

#ifdef CONFIG_SYSCTL
struct ctl_path nf_net_ipv4_netfilter_sysctl_path[] = {
	{ .procname = "net", .ctl_name = CTL_NET, },
	{ .procname = "ipv4", .ctl_name = NET_IPV4, },
	{ .procname = "netfilter", .ctl_name = NET_IPV4_NETFILTER, },
	{ }
};
EXPORT_SYMBOL_GPL(nf_net_ipv4_netfilter_sysctl_path);
#endif /* CONFIG_SYSCTL */
