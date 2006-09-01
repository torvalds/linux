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

/* Add encapsulation header.
 *
 * The IP header will be moved forward to make space for the encapsulation
 * header.
 *
 * On exit, skb->h will be set to the start of the payload to be processed
 * by x->type->output and skb->nh will be set to the top IP header.
 */
static int xfrm4_transport_output(struct xfrm_state *x, struct sk_buff *skb)
{
	struct iphdr *iph;
	int ihl;

	iph = skb->nh.iph;
	skb->h.ipiph = iph;

	ihl = iph->ihl * 4;
	skb->h.raw += ihl;

	skb->nh.raw = memmove(skb_push(skb, x->props.header_len), iph, ihl);
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
static int xfrm4_transport_input(struct xfrm_state *x, struct sk_buff *skb)
{
	int ihl = skb->data - skb->h.raw;

	if (skb->h.raw != skb->nh.raw)
		skb->nh.raw = memmove(skb->h.raw, skb->nh.raw, ihl);
	skb->nh.iph->tot_len = htons(skb->len + ihl);
	skb->h.raw = skb->data;
	return 0;
}

static struct xfrm_mode xfrm4_transport_mode = {
	.input = xfrm4_transport_input,
	.output = xfrm4_transport_output,
	.owner = THIS_MODULE,
	.encap = XFRM_MODE_TRANSPORT,
};

static int __init xfrm4_transport_init(void)
{
	return xfrm_register_mode(&xfrm4_transport_mode, AF_INET);
}

static void __exit xfrm4_transport_exit(void)
{
	int err;

	err = xfrm_unregister_mode(&xfrm4_transport_mode, AF_INET);
	BUG_ON(err);
}

module_init(xfrm4_transport_init);
module_exit(xfrm4_transport_exit);
MODULE_LICENSE("GPL");
MODULE_ALIAS_XFRM_MODE(AF_INET, XFRM_MODE_TRANSPORT);
