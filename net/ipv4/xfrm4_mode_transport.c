/*
 * xfrm4_mode_transport.c - Transport mode encapsulation for IPv4.
 *
 * Copyright (c) 2004-2006 Herbert Xu <herbert@gondor.apana.org.au>
 */

#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/skbuff.h>
#include <linux/stringify.h>
#include <net/dst.h>
#include <net/ip.h>
#include <net/xfrm.h>
#include <net/protocol.h>

/* Add encapsulation header.
 *
 * The IP header will be moved forward to make space for the encapsulation
 * header.
 */
static int xfrm4_transport_output(struct xfrm_state *x, struct sk_buff *skb)
{
	struct iphdr *iph = ip_hdr(skb);
	int ihl = iph->ihl * 4;

	skb_set_inner_transport_header(skb, skb_transport_offset(skb));

	skb_set_network_header(skb, -x->props.header_len);
	skb->mac_header = skb->network_header +
			  offsetof(struct iphdr, protocol);
	skb->transport_header = skb->network_header + ihl;
	__skb_pull(skb, ihl);
	memmove(skb_network_header(skb), iph, ihl);
	return 0;
}

static struct sk_buff *xfrm4_transport_gso_segment(struct xfrm_state *x,
						   struct sk_buff *skb,
						   netdev_features_t features)
{
	const struct net_offload *ops;
	struct sk_buff *segs = ERR_PTR(-EINVAL);
	struct xfrm_offload *xo = xfrm_offload(skb);

	skb->transport_header += x->props.header_len;
	ops = rcu_dereference(inet_offloads[xo->proto]);
	if (likely(ops && ops->callbacks.gso_segment))
		segs = ops->callbacks.gso_segment(skb, features);

	return segs;
}

static void xfrm4_transport_xmit(struct xfrm_state *x, struct sk_buff *skb)
{
	struct xfrm_offload *xo = xfrm_offload(skb);

	skb_reset_mac_len(skb);
	pskb_pull(skb, skb->mac_len + sizeof(struct iphdr) + x->props.header_len);

	if (xo->flags & XFRM_GSO_SEGMENT) {
		 skb_reset_transport_header(skb);
		 skb->transport_header -= x->props.header_len;
	}
}

static struct xfrm_mode xfrm4_transport_mode = {
	.output = xfrm4_transport_output,
	.gso_segment = xfrm4_transport_gso_segment,
	.xmit = xfrm4_transport_xmit,
	.owner = THIS_MODULE,
	.encap = XFRM_MODE_TRANSPORT,
	.family = AF_INET,
};

static int __init xfrm4_transport_init(void)
{
	return xfrm_register_mode(&xfrm4_transport_mode);
}

static void __exit xfrm4_transport_exit(void)
{
	xfrm_unregister_mode(&xfrm4_transport_mode);
}

module_init(xfrm4_transport_init);
module_exit(xfrm4_transport_exit);
MODULE_LICENSE("GPL");
MODULE_ALIAS_XFRM_MODE(AF_INET, XFRM_MODE_TRANSPORT);
