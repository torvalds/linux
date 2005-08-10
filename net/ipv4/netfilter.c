#include <linux/config.h>

#ifdef CONFIG_NETFILTER

/* IPv4 specific functions of netfilter core */
#include <linux/kernel.h>
#include <linux/netfilter.h>

#include <linux/tcp.h>
#include <linux/udp.h>
#include <linux/icmp.h>
#include <net/route.h>
#include <linux/ip.h>

/* route_me_harder function, used by iptable_nat, iptable_mangle + ip_queue */
int ip_route_me_harder(struct sk_buff **pskb)
{
	struct iphdr *iph = (*pskb)->nh.iph;
	struct rtable *rt;
	struct flowi fl = {};
	struct dst_entry *odst;
	unsigned int hh_len;

	/* some non-standard hacks like ipt_REJECT.c:send_reset() can cause
	 * packets with foreign saddr to appear on the NF_IP_LOCAL_OUT hook.
	 */
	if (inet_addr_type(iph->saddr) == RTN_LOCAL) {
		fl.nl_u.ip4_u.daddr = iph->daddr;
		fl.nl_u.ip4_u.saddr = iph->saddr;
		fl.nl_u.ip4_u.tos = RT_TOS(iph->tos);
		fl.oif = (*pskb)->sk ? (*pskb)->sk->sk_bound_dev_if : 0;
#ifdef CONFIG_IP_ROUTE_FWMARK
		fl.nl_u.ip4_u.fwmark = (*pskb)->nfmark;
#endif
		fl.proto = iph->protocol;
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
#endif /* CONFIG_NETFILTER */
