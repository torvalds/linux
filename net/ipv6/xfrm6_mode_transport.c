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

/* Add encapsulation header.
 *
 * The IP header and mutable extension headers will be moved forward to make
 * space for the encapsulation header.
 *
 * On exit, skb->h will be set to the start of the encapsulation header to be
 * filled in by x->type->output and skb->nh will be set to the nextheader field
 * of the extension header directly preceding the encapsulation header, or in
 * its absence, that of the top IP header.  The value of skb->data will always
 * point to the top IP header.
 */
static int xfrm6_transport_output(struct xfrm_state *x, struct sk_buff *skb)
{
	struct ipv6hdr *iph;
	u8 *prevhdr;
	int hdr_len;

	skb_push(skb, x->props.header_len);
	iph = skb->nh.ipv6h;

	hdr_len = x->type->hdr_offset(x, skb, &prevhdr);
	skb->nh.raw = prevhdr - x->props.header_len;
	skb->h.raw = skb->data + hdr_len;
	memmove(skb->data, iph, hdr_len);
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
	int ihl = skb->data - skb->h.raw;

	if (skb->h.raw != skb->nh.raw)
		skb->nh.raw = memmove(skb->h.raw, skb->nh.raw, ihl);
	skb->nh.ipv6h->payload_len = htons(skb->len + ihl -
					   sizeof(struct ipv6hdr));
	skb->h.raw = skb->data;
	return 0;
}

static struct xfrm_mode xfrm6_transport_mode = {
	.input = xfrm6_transport_input,
	.output = xfrm6_transport_output,
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
