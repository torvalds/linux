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
	struct iphdr *outer_iph = skb->nh.iph;
	struct iphdr *inner_iph = skb->h.ipiph;

	if (INET_ECN_is_ce(outer_iph->tos))
		IP_ECN_set_ce(inner_iph);
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
static int xfrm4_tunnel_output(struct sk_buff *skb)
{
	struct dst_entry *dst = skb->dst;
	struct xfrm_state *x = dst->xfrm;
	struct iphdr *iph, *top_iph;
	int flags;

	iph = skb->nh.iph;
	skb->h.ipiph = iph;

	skb->nh.raw = skb_push(skb, x->props.header_len);
	top_iph = skb->nh.iph;

	top_iph->ihl = 5;
	top_iph->version = 4;

	/* DS disclosed */
	top_iph->tos = INET_ECN_encapsulate(iph->tos, iph->tos);

	flags = x->props.flags;
	if (flags & XFRM_STATE_NOECN)
		IP_ECN_clear(top_iph);

	top_iph->frag_off = (flags & XFRM_STATE_NOPMTUDISC) ?
		0 : (iph->frag_off & htons(IP_DF));
	if (!top_iph->frag_off)
		__ip_select_ident(top_iph, dst->child, 0);

	top_iph->ttl = dst_metric(dst->child, RTAX_HOPLIMIT);

	top_iph->saddr = x->props.saddr.a4;
	top_iph->daddr = x->id.daddr.a4;
	top_iph->protocol = IPPROTO_IPIP;

	memset(&(IPCB(skb)->opt), 0, sizeof(struct ip_options));
	return 0;
}

static int xfrm4_tunnel_input(struct xfrm_state *x, struct sk_buff *skb)
{
	struct iphdr *iph = skb->nh.iph;
	int err = -EINVAL;

	if (iph->protocol != IPPROTO_IPIP)
		goto out;
	if (!pskb_may_pull(skb, sizeof(struct iphdr)))
		goto out;

	if (skb_cloned(skb) &&
	    (err = pskb_expand_head(skb, 0, 0, GFP_ATOMIC)))
		goto out;

	if (x->props.flags & XFRM_STATE_DECAP_DSCP)
		ipv4_copy_dscp(iph, skb->h.ipiph);
	if (!(x->props.flags & XFRM_STATE_NOECN))
		ipip_ecn_decapsulate(skb);
	skb->mac.raw = memmove(skb->data - skb->mac_len,
			       skb->mac.raw, skb->mac_len);
	skb->nh.raw = skb->data;
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
