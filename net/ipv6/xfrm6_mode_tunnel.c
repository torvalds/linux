/*
 * xfrm6_mode_tunnel.c - Tunnel mode encapsulation for IPv6.
 *
 * Copyright (C) 2002 USAGI/WIDE Project
 * Copyright (c) 2004-2006 Herbert Xu <herbert@gondor.apana.org.au>
 */

#include <linux/gfp.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/skbuff.h>
#include <linux/stringify.h>
#include <net/dsfield.h>
#include <net/dst.h>
#include <net/inet_ecn.h>
#include <net/ip6_route.h>
#include <net/ipv6.h>
#include <net/xfrm.h>

static inline void ipip6_ecn_decapsulate(struct sk_buff *skb)
{
	struct ipv6hdr *inner_iph = ipipv6_hdr(skb);

	if (INET_ECN_is_ce(XFRM_MODE_SKB_CB(skb)->tos))
		IP6_ECN_set_ce(inner_iph);
}

/* Add encapsulation header.
 *
 * The top IP header will be constructed per RFC 2401.
 */
static int xfrm6_mode_tunnel_output(struct xfrm_state *x, struct sk_buff *skb)
{
	struct dst_entry *dst = skb_dst(skb);
	struct ipv6hdr *top_iph;
	int dsfield;

	skb_set_network_header(skb, -x->props.header_len);
	skb->mac_header = skb->network_header +
			  offsetof(struct ipv6hdr, nexthdr);
	skb->transport_header = skb->network_header + sizeof(*top_iph);
	top_iph = ipv6_hdr(skb);

	top_iph->version = 6;

	memcpy(top_iph->flow_lbl, XFRM_MODE_SKB_CB(skb)->flow_lbl,
	       sizeof(top_iph->flow_lbl));
	top_iph->nexthdr = xfrm_af2proto(skb_dst(skb)->ops->family);

	if (x->props.extra_flags & XFRM_SA_XFLAG_DONT_ENCAP_DSCP)
		dsfield = 0;
	else
		dsfield = XFRM_MODE_SKB_CB(skb)->tos;
	dsfield = INET_ECN_encapsulate(dsfield, XFRM_MODE_SKB_CB(skb)->tos);
	if (x->props.flags & XFRM_STATE_NOECN)
		dsfield &= ~INET_ECN_MASK;
	ipv6_change_dsfield(top_iph, 0, dsfield);
	top_iph->hop_limit = ip6_dst_hoplimit(dst->child);
	top_iph->saddr = *(struct in6_addr *)&x->props.saddr;
	top_iph->daddr = *(struct in6_addr *)&x->id.daddr;
	return 0;
}

#define for_each_input_rcu(head, handler)	\
	for (handler = rcu_dereference(head);	\
	     handler != NULL;			\
	     handler = rcu_dereference(handler->next))


static int xfrm6_mode_tunnel_input(struct xfrm_state *x, struct sk_buff *skb)
{
	int err = -EINVAL;

	if (XFRM_MODE_SKB_CB(skb)->protocol != IPPROTO_IPV6)
		goto out;
	if (!pskb_may_pull(skb, sizeof(struct ipv6hdr)))
		goto out;

	err = skb_unclone(skb, GFP_ATOMIC);
	if (err)
		goto out;

	if (x->props.flags & XFRM_STATE_DECAP_DSCP)
		ipv6_copy_dscp(ipv6_get_dsfield(ipv6_hdr(skb)),
			       ipipv6_hdr(skb));
	if (!(x->props.flags & XFRM_STATE_NOECN))
		ipip6_ecn_decapsulate(skb);

	skb_reset_network_header(skb);
	skb_mac_header_rebuild(skb);

	err = 0;

out:
	return err;
}

static struct xfrm_mode xfrm6_tunnel_mode = {
	.input2 = xfrm6_mode_tunnel_input,
	.input = xfrm_prepare_input,
	.output2 = xfrm6_mode_tunnel_output,
	.output = xfrm6_prepare_output,
	.owner = THIS_MODULE,
	.encap = XFRM_MODE_TUNNEL,
	.flags = XFRM_MODE_FLAG_TUNNEL,
};

static int __init xfrm6_mode_tunnel_init(void)
{
	return xfrm_register_mode(&xfrm6_tunnel_mode, AF_INET6);
}

static void __exit xfrm6_mode_tunnel_exit(void)
{
	int err;

	err = xfrm_unregister_mode(&xfrm6_tunnel_mode, AF_INET6);
	BUG_ON(err);
}

module_init(xfrm6_mode_tunnel_init);
module_exit(xfrm6_mode_tunnel_exit);
MODULE_LICENSE("GPL");
MODULE_ALIAS_XFRM_MODE(AF_INET6, XFRM_MODE_TUNNEL);
