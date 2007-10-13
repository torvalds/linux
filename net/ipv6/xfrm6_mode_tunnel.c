/*
 * xfrm6_mode_tunnel.c - Tunnel mode encapsulation for IPv6.
 *
 * Copyright (C) 2002 USAGI/WIDE Project
 * Copyright (c) 2004-2006 Herbert Xu <herbert@gondor.apana.org.au>
 */

#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/skbuff.h>
#include <linux/stringify.h>
#include <net/dsfield.h>
#include <net/dst.h>
#include <net/inet_ecn.h>
#include <net/ipv6.h>
#include <net/xfrm.h>

static inline void ipip6_ecn_decapsulate(struct sk_buff *skb)
{
	struct ipv6hdr *outer_iph = ipv6_hdr(skb);
	struct ipv6hdr *inner_iph = ipipv6_hdr(skb);

	if (INET_ECN_is_ce(ipv6_get_dsfield(outer_iph)))
		IP6_ECN_set_ce(inner_iph);
}

static inline void ip6ip_ecn_decapsulate(struct sk_buff *skb)
{
	if (INET_ECN_is_ce(ipv6_get_dsfield(ipv6_hdr(skb))))
			IP_ECN_set_ce(ipip_hdr(skb));
}

/* Add encapsulation header.
 *
 * The top IP header will be constructed per RFC 2401.
 */
static int xfrm6_tunnel_output(struct xfrm_state *x, struct sk_buff *skb)
{
	struct dst_entry *dst = skb->dst;
	struct xfrm_dst *xdst = (struct xfrm_dst*)dst;
	struct ipv6hdr *iph, *top_iph;
	int dsfield;

	iph = ipv6_hdr(skb);

	skb_set_network_header(skb, -x->props.header_len);
	skb->mac_header = skb->network_header +
			  offsetof(struct ipv6hdr, nexthdr);
	skb->transport_header = skb->network_header + sizeof(*iph);
	top_iph = ipv6_hdr(skb);

	top_iph->version = 6;
	if (xdst->route->ops->family == AF_INET6) {
		top_iph->priority = iph->priority;
		top_iph->flow_lbl[0] = iph->flow_lbl[0];
		top_iph->flow_lbl[1] = iph->flow_lbl[1];
		top_iph->flow_lbl[2] = iph->flow_lbl[2];
		top_iph->nexthdr = IPPROTO_IPV6;
	} else {
		top_iph->priority = 0;
		top_iph->flow_lbl[0] = 0;
		top_iph->flow_lbl[1] = 0;
		top_iph->flow_lbl[2] = 0;
		top_iph->nexthdr = IPPROTO_IPIP;
	}
	dsfield = ipv6_get_dsfield(top_iph);
	dsfield = INET_ECN_encapsulate(dsfield, dsfield);
	if (x->props.flags & XFRM_STATE_NOECN)
		dsfield &= ~INET_ECN_MASK;
	ipv6_change_dsfield(top_iph, 0, dsfield);
	top_iph->hop_limit = dst_metric(dst->child, RTAX_HOPLIMIT);
	ipv6_addr_copy(&top_iph->saddr, (struct in6_addr *)&x->props.saddr);
	ipv6_addr_copy(&top_iph->daddr, (struct in6_addr *)&x->id.daddr);
	skb->protocol = htons(ETH_P_IPV6);
	return 0;
}

static int xfrm6_tunnel_input(struct xfrm_state *x, struct sk_buff *skb)
{
	int err = -EINVAL;
	const unsigned char *old_mac;
	const unsigned char *nh = skb_network_header(skb);

	if (nh[IP6CB(skb)->nhoff] != IPPROTO_IPV6 &&
	    nh[IP6CB(skb)->nhoff] != IPPROTO_IPIP)
		goto out;
	if (!pskb_may_pull(skb, sizeof(struct ipv6hdr)))
		goto out;

	if (skb_cloned(skb) &&
	    (err = pskb_expand_head(skb, 0, 0, GFP_ATOMIC)))
		goto out;

	nh = skb_network_header(skb);
	if (nh[IP6CB(skb)->nhoff] == IPPROTO_IPV6) {
		if (x->props.flags & XFRM_STATE_DECAP_DSCP)
			ipv6_copy_dscp(ipv6_hdr(skb), ipipv6_hdr(skb));
		if (!(x->props.flags & XFRM_STATE_NOECN))
			ipip6_ecn_decapsulate(skb);
	} else {
		if (!(x->props.flags & XFRM_STATE_NOECN))
			ip6ip_ecn_decapsulate(skb);
		skb->protocol = htons(ETH_P_IP);
	}
	old_mac = skb_mac_header(skb);
	skb_set_mac_header(skb, -skb->mac_len);
	memmove(skb_mac_header(skb), old_mac, skb->mac_len);
	skb_reset_network_header(skb);
	err = 0;

out:
	return err;
}

static struct xfrm_mode xfrm6_tunnel_mode = {
	.input = xfrm6_tunnel_input,
	.output = xfrm6_tunnel_output,
	.owner = THIS_MODULE,
	.encap = XFRM_MODE_TUNNEL,
};

static int __init xfrm6_tunnel_init(void)
{
	return xfrm_register_mode(&xfrm6_tunnel_mode, AF_INET6);
}

static void __exit xfrm6_tunnel_exit(void)
{
	int err;

	err = xfrm_unregister_mode(&xfrm6_tunnel_mode, AF_INET6);
	BUG_ON(err);
}

module_init(xfrm6_tunnel_init);
module_exit(xfrm6_tunnel_exit);
MODULE_LICENSE("GPL");
MODULE_ALIAS_XFRM_MODE(AF_INET6, XFRM_MODE_TUNNEL);
