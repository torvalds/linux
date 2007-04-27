/*
 * xfrm4_mode_tunnel.c - Tunnel mode encapsulation for IPv4.
 *
 * Copyright (c) 2004-2006 Herbert Xu <herbert@gondor.apana.org.au>
 */

#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/skbuff.h>
#include <linux/stringify.h>
#include <net/dst.h>
#include <net/inet_ecn.h>
#include <net/ip.h>
#include <net/xfrm.h>

static inline void ipip_ecn_decapsulate(struct sk_buff *skb)
{
	struct iphdr *outer_iph = ip_hdr(skb);
	struct iphdr *inner_iph = ipip_hdr(skb);

	if (INET_ECN_is_ce(outer_iph->tos))
		IP_ECN_set_ce(inner_iph);
}

static inline void ipip6_ecn_decapsulate(struct iphdr *iph, struct sk_buff *skb)
{
	if (INET_ECN_is_ce(iph->tos))
		IP6_ECN_set_ce(ipv6_hdr(skb));
}

/* Add encapsulation header.
 *
 * The top IP header will be constructed per RFC 2401.  The following fields
 * in it shall be filled in by x->type->output:
 *      tot_len
 *      check
 *
 * On exit, skb->h will be set to the start of the payload to be processed
 * by x->type->output and skb->nh will be set to the top IP header.
 */
static int xfrm4_tunnel_output(struct xfrm_state *x, struct sk_buff *skb)
{
	struct dst_entry *dst = skb->dst;
	struct xfrm_dst *xdst = (struct xfrm_dst*)dst;
	struct iphdr *iph, *top_iph;
	int flags;

	iph = ip_hdr(skb);
	skb->transport_header = skb->network_header;

	skb_push(skb, x->props.header_len);
	skb_reset_network_header(skb);
	top_iph = ip_hdr(skb);

	top_iph->ihl = 5;
	top_iph->version = 4;

	flags = x->props.flags;

	/* DS disclosed */
	if (xdst->route->ops->family == AF_INET) {
		top_iph->protocol = IPPROTO_IPIP;
		top_iph->tos = INET_ECN_encapsulate(iph->tos, iph->tos);
		top_iph->frag_off = (flags & XFRM_STATE_NOPMTUDISC) ?
			0 : (iph->frag_off & htons(IP_DF));
	}
#if defined(CONFIG_IPV6) || defined (CONFIG_IPV6_MODULE)
	else {
		struct ipv6hdr *ipv6h = (struct ipv6hdr*)iph;
		top_iph->protocol = IPPROTO_IPV6;
		top_iph->tos = INET_ECN_encapsulate(iph->tos, ipv6_get_dsfield(ipv6h));
		top_iph->frag_off = 0;
	}
#endif

	if (flags & XFRM_STATE_NOECN)
		IP_ECN_clear(top_iph);

	if (!top_iph->frag_off)
		__ip_select_ident(top_iph, dst->child, 0);

	top_iph->ttl = dst_metric(dst->child, RTAX_HOPLIMIT);

	top_iph->saddr = x->props.saddr.a4;
	top_iph->daddr = x->id.daddr.a4;

	memset(&(IPCB(skb)->opt), 0, sizeof(struct ip_options));
	return 0;
}

static int xfrm4_tunnel_input(struct xfrm_state *x, struct sk_buff *skb)
{
	struct iphdr *iph = ip_hdr(skb);
	const unsigned char *old_mac;
	int err = -EINVAL;

	switch (iph->protocol){
		case IPPROTO_IPIP:
			break;
#if defined(CONFIG_IPV6) || defined (CONFIG_IPV6_MODULE)
		case IPPROTO_IPV6:
			break;
#endif
		default:
			goto out;
	}

	if (!pskb_may_pull(skb, sizeof(struct iphdr)))
		goto out;

	if (skb_cloned(skb) &&
	    (err = pskb_expand_head(skb, 0, 0, GFP_ATOMIC)))
		goto out;

	iph = ip_hdr(skb);
	if (iph->protocol == IPPROTO_IPIP) {
		if (x->props.flags & XFRM_STATE_DECAP_DSCP)
			ipv4_copy_dscp(iph, ipip_hdr(skb));
		if (!(x->props.flags & XFRM_STATE_NOECN))
			ipip_ecn_decapsulate(skb);
	}
#if defined(CONFIG_IPV6) || defined (CONFIG_IPV6_MODULE)
	else {
		if (!(x->props.flags & XFRM_STATE_NOECN))
			ipip6_ecn_decapsulate(iph, skb);
		skb->protocol = htons(ETH_P_IPV6);
	}
#endif
	old_mac = skb_mac_header(skb);
	skb_set_mac_header(skb, -skb->mac_len);
	memmove(skb_mac_header(skb), old_mac, skb->mac_len);
	skb_reset_network_header(skb);
	err = 0;

out:
	return err;
}

static struct xfrm_mode xfrm4_tunnel_mode = {
	.input = xfrm4_tunnel_input,
	.output = xfrm4_tunnel_output,
	.owner = THIS_MODULE,
	.encap = XFRM_MODE_TUNNEL,
};

static int __init xfrm4_tunnel_init(void)
{
	return xfrm_register_mode(&xfrm4_tunnel_mode, AF_INET);
}

static void __exit xfrm4_tunnel_exit(void)
{
	int err;

	err = xfrm_unregister_mode(&xfrm4_tunnel_mode, AF_INET);
	BUG_ON(err);
}

module_init(xfrm4_tunnel_init);
module_exit(xfrm4_tunnel_exit);
MODULE_LICENSE("GPL");
MODULE_ALIAS_XFRM_MODE(AF_INET, XFRM_MODE_TUNNEL);
