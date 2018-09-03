/*
 * xfrm6_mode_transport.c - Transport mode encapsulation for IPv6.
 *
 * Copyright (C) 2002 USAGI/WIDE Project
 * Copyright (c) 2004-2006 Herbert Xu <herbert@gondor.apana.org.au>
 */

#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/skbuff.h>
#include <linux/stringify.h>
#include <net/dst.h>
#include <net/ipv6.h>
#include <net/xfrm.h>
#include <net/protocol.h>

/* Add encapsulation header.
 *
 * The IP header and mutable extension headers will be moved forward to make
 * space for the encapsulation header.
 */
static int xfrm6_transport_output(struct xfrm_state *x, struct sk_buff *skb)
{
	struct ipv6hdr *iph;
	u8 *prevhdr;
	int hdr_len;

	iph = ipv6_hdr(skb);
	skb_set_inner_transport_header(skb, skb_transport_offset(skb));

	hdr_len = x->type->hdr_offset(x, skb, &prevhdr);
	if (hdr_len < 0)
		return hdr_len;
	skb_set_mac_header(skb, (prevhdr - x->props.header_len) - skb->data);
	skb_set_network_header(skb, -x->props.header_len);
	skb->transport_header = skb->network_header + hdr_len;
	__skb_pull(skb, hdr_len);
	memmove(ipv6_hdr(skb), iph, hdr_len);
	return 0;
}

/* Remove encapsulation header.
 *
 * The IP header will be moved over the top of the encapsulation header.
 *
 * On entry, skb->h shall point to where the IP header should be and skb->nh
 * shall be set to where the IP header currently is.  skb->data shall point
 * to the start of the payload.
 */
static int xfrm6_transport_input(struct xfrm_state *x, struct sk_buff *skb)
{
	int ihl = skb->data - skb_transport_header(skb);

	if (skb->transport_header != skb->network_header) {
		memmove(skb_transport_header(skb),
			skb_network_header(skb), ihl);
		skb->network_header = skb->transport_header;
	}
	ipv6_hdr(skb)->payload_len = htons(skb->len + ihl -
					   sizeof(struct ipv6hdr));
	skb_reset_transport_header(skb);
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
	ops = rcu_dereference(inet6_offloads[xo->proto]);
	if (likely(ops && ops->callbacks.gso_segment))
		segs = ops->callbacks.gso_segment(skb, features);

	return segs;
}

static void xfrm6_transport_xmit(struct xfrm_state *x, struct sk_buff *skb)
{
	struct xfrm_offload *xo = xfrm_offload(skb);

	skb_reset_mac_len(skb);
	pskb_pull(skb, skb->mac_len + sizeof(struct ipv6hdr) + x->props.header_len);

	if (xo->flags & XFRM_GSO_SEGMENT) {
		 skb_reset_transport_header(skb);
		 skb->transport_header -= x->props.header_len;
	}
}


static struct xfrm_mode xfrm6_transport_mode = {
	.input = xfrm6_transport_input,
	.output = xfrm6_transport_output,
	.gso_segment = xfrm4_transport_gso_segment,
	.xmit = xfrm6_transport_xmit,
	.owner = THIS_MODULE,
	.encap = XFRM_MODE_TRANSPORT,
};

static int __init xfrm6_transport_init(void)
{
	return xfrm_register_mode(&xfrm6_transport_mode, AF_INET6);
}

static void __exit xfrm6_transport_exit(void)
{
	int err;

	err = xfrm_unregister_mode(&xfrm6_transport_mode, AF_INET6);
	BUG_ON(err);
}

module_init(xfrm6_transport_init);
module_exit(xfrm6_transport_exit);
MODULE_LICENSE("GPL");
MODULE_ALIAS_XFRM_MODE(AF_INET6, XFRM_MODE_TRANSPORT);
