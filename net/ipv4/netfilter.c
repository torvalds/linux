/* IPv4 specific functions of netfilter core */
#include <linux/kernel.h>
#include <linux/netfilter.h>
#include <linux/netfilter_ipv4.h>
#include <linux/ip.h>
#include <net/route.h>
#include <net/xfrm.h>
#include <net/ip.h>

/* route_me_harder function, used by iptable_nat, iptable_mangle + ip_queue */
int ip_route_me_harder(struct sk_buff **pskb, unsigned addr_type)
{
	struct iphdr *iph = (*pskb)->nh.iph;
	struct rtable *rt;
	struct flowi fl = {};
	struct dst_entry *odst;
	unsigned int hh_len;

	if (addr_type == RTN_UNSPEC)
		addr_type = inet_addr_type(iph->saddr);

	/* some non-standard hacks like ipt_REJECT.c:send_reset() can cause
	 * packets with foreign saddr to appear on the NF_IP_LOCAL_OUT hook.
	 */
	if (addr_type == RTN_LOCAL) {
		fl.nl_u.ip4_u.daddr = iph->daddr;
		fl.nl_u.ip4_u.saddr = iph->saddr;
		fl.nl_u.ip4_u.tos = RT_TOS(iph->tos);
		fl.oif = (*pskb)->sk ? (*pskb)->sk->sk_bound_dev_if : 0;
#ifdef CONFIG_IP_ROUTE_FWMARK
		fl.nl_u.ip4_u.fwmark = (*pskb)->nfmark;
#endif
		if (ip_route_output_key(&rt, &fl) != 0)
			return -1;

		/* Drop old route. */
		dst_release((*pskb)->dst);
		(*pskb)->dst = &rt->u.dst;
	} else {
		/* non-local src, find valid iif to satisfy
		 * rp-filter when calling ip_route_input. */
		fl.nl_u.ip4_u.daddr = iph->saddr;
		if (ip_route_output_key(&rt, &fl) != 0)
			return -1;

		odst = (*pskb)->dst;
		if (ip_route_input(*pskb, iph->daddr, iph->saddr,
				   RT_TOS(iph->tos), rt->u.dst.dev) != 0) {
			dst_release(&rt->u.dst);
			return -1;
		}
		dst_release(&rt->u.dst);
		dst_release(odst);
	}
	
	if ((*pskb)->dst->error)
		return -1;

#ifdef CONFIG_XFRM
	if (!(IPCB(*pskb)->flags & IPSKB_XFRM_TRANSFORMED) &&
	    xfrm_decode_session(*pskb, &fl, AF_INET) == 0)
		if (xfrm_lookup(&(*pskb)->dst, &fl, (*pskb)->sk, 0))
			return -1;
#endif

	/* Change in oif may mean change in hh_len. */
	hh_len = (*pskb)->dst->dev->hard_header_len;
	if (skb_headroom(*pskb) < hh_len) {
		struct sk_buff *nskb;

		nskb = skb_realloc_headroom(*pskb, hh_len);
		if (!nskb) 
			return -1;
		if ((*pskb)->sk)
			skb_set_owner_w(nskb, (*pskb)->sk);
		kfree_skb(*pskb);
		*pskb = nskb;
	}

	return 0;
}
EXPORT_SYMBOL(ip_route_me_harder);

#ifdef CONFIG_XFRM
int ip_xfrm_me_harder(struct sk_buff **pskb)
{
	struct flowi fl;
	unsigned int hh_len;
	struct dst_entry *dst;

	if (IPCB(*pskb)->flags & IPSKB_XFRM_TRANSFORMED)
		return 0;
	if (xfrm_decode_session(*pskb, &fl, AF_INET) < 0)
		return -1;

	dst = (*pskb)->dst;
	if (dst->xfrm)
		dst = ((struct xfrm_dst *)dst)->route;
	dst_hold(dst);

	if (xfrm_lookup(&dst, &fl, (*pskb)->sk, 0) < 0)
		return -1;

	dst_release((*pskb)->dst);
	(*pskb)->dst = dst;

	/* Change in oif may mean change in hh_len. */
	hh_len = (*pskb)->dst->dev->hard_header_len;
	if (skb_headroom(*pskb) < hh_len) {
		struct sk_buff *nskb;

		nskb = skb_realloc_headroom(*pskb, hh_len);
		if (!nskb)
			return -1;
		if ((*pskb)->sk)
			skb_set_owner_w(nskb, (*pskb)->sk);
		kfree_skb(*pskb);
		*pskb = nskb;
	}
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

static void nf_ip_saveroute(const struct sk_buff *skb, struct nf_info *info)
{
	struct ip_rt_info *rt_info = nf_info_reroute(info);

	if (info->hook == NF_IP_LOCAL_OUT) {
		const struct iphdr *iph = skb->nh.iph;

		rt_info->tos = iph->tos;
		rt_info->daddr = iph->daddr;
		rt_info->saddr = iph->saddr;
	}
}

static int nf_ip_reroute(struct sk_buff **pskb, const struct nf_info *info)
{
	const struct ip_rt_info *rt_info = nf_info_reroute(info);

	if (info->hook == NF_IP_LOCAL_OUT) {
		struct iphdr *iph = (*pskb)->nh.iph;

		if (!(iph->tos == rt_info->tos
		      && iph->daddr == rt_info->daddr
		      && iph->saddr == rt_info->saddr))
			return ip_route_me_harder(pskb, RTN_UNSPEC);
	}
	return 0;
}

unsigned int nf_ip_checksum(struct sk_buff *skb, unsigned int hook,
			    unsigned int dataoff, u_int8_t protocol)
{
	struct iphdr *iph = skb->nh.iph;
	unsigned int csum = 0;

	switch (skb->ip_summed) {
	case CHECKSUM_COMPLETE:
		if (hook != NF_IP_PRE_ROUTING && hook != NF_IP_LOCAL_IN)
			break;
		if ((protocol == 0 && !(u16)csum_fold(skb->csum)) ||
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

static struct nf_afinfo nf_ip_afinfo = {
	.family		= AF_INET,
	.checksum	= nf_ip_checksum,
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
