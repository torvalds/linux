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
 * The top IP header will be constructed per RFC 2401.
 */
static int xfrm4_tunnel_output(struct xfrm_state *x, struct sk_buff *skb)
{
	struct dst_entry *dst = skb->dst;
	struct iphdr *top_iph;
	int flags;

	skb_set_network_header(skb, -x->props.header_len);
	skb->mac_header = skb->network_header +
			  offsetof(struct iphdr, protocol);
	skb->transport_header = skb->network_header + sizeof(*top_iph);
	top_iph = ip_hdr(skb);

	top_iph->ihl = 5;
	top_iph->version = 4;

	top_iph->protocol = x->inner_mode->afinfo->proto;

	/* DS disclosed */
	top_iph->tos = INET_ECN_encapsulate(XFRM_MODE_SKB_CB(skb)->tos,
					    XFRM_MODE_SKB_CB(skb)->tos);

	flags = x->props.flags;
	if (flags & XFRM_STATE_NOECN)
		IP_ECN_clear(top_iph);

	top_iph->frag_off = (flags & XFRM_STATE_NOPMTUDISC) ?
			    0 : XFRM_MODE_SKB_CB(skb)->frag_off;
	ip_select_ident(top_iph, dst->child, NULL);

	top_iph->ttl = dst_metric(dst->child, RTAX_HOPLIMIT);

	top_iph->saddr = x->props.saddr.a4;
	top_iph->daddr = x->id.daddr.a4;

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
			ipv4_copy_dscp(ipv4_get_dsfield(iph), ipip_hdr(skb));
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
	.output2 = xfrm4_tunnel_output,
	.output = xfrm4_prepare_output,
	.owner = THIS_MODULE,
	.encap = XFRM_MODE_TUNNEL,
	.flags = XFRM_MODE_FLAG_TUNNEL,
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
