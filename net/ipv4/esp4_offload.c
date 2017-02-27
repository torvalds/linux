/*
 * IPV4 GSO/GRO offload support
 * Linux INET implementation
 *
 * Copyright (C) 2016 secunet Security Networks AG
 * Author: Steffen Klassert <steffen.klassert@secunet.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * ESP GRO support
 */

#include <linux/skbuff.h>
#include <linux/init.h>
#include <net/protocol.h>
#include <crypto/aead.h>
#include <crypto/authenc.h>
#include <linux/err.h>
#include <linux/module.h>
#include <net/ip.h>
#include <net/xfrm.h>
#include <net/esp.h>
#include <linux/scatterlist.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <net/udp.h>

static struct sk_buff **esp4_gro_receive(struct sk_buff **head,
					 struct sk_buff *skb)
{
	int offset = skb_gro_offset(skb);
	struct xfrm_offload *xo;
	struct xfrm_state *x;
	__be32 seq;
	__be32 spi;
	int err;

	skb_pull(skb, offset);

	if ((err = xfrm_parse_spi(skb, IPPROTO_ESP, &spi, &seq)) != 0)
		goto out;

	err = secpath_set(skb);
	if (err)
		goto out;

	if (skb->sp->len == XFRM_MAX_DEPTH)
		goto out;

	x = xfrm_state_lookup(dev_net(skb->dev), skb->mark,
			      (xfrm_address_t *)&ip_hdr(skb)->daddr,
			      spi, IPPROTO_ESP, AF_INET);
	if (!x)
		goto out;

	skb->sp->xvec[skb->sp->len++] = x;
	skb->sp->olen++;

	xo = xfrm_offload(skb);
	if (!xo) {
		xfrm_state_put(x);
		goto out;
	}
	xo->flags |= XFRM_GRO;

	XFRM_TUNNEL_SKB_CB(skb)->tunnel.ip4 = NULL;
	XFRM_SPI_SKB_CB(skb)->family = AF_INET;
	XFRM_SPI_SKB_CB(skb)->daddroff = offsetof(struct iphdr, daddr);
	XFRM_SPI_SKB_CB(skb)->seq = seq;

	/* We don't need to handle errors from xfrm_input, it does all
	 * the error handling and frees the resources on error. */
	xfrm_input(skb, IPPROTO_ESP, spi, -2);

	return ERR_PTR(-EINPROGRESS);
out:
	skb_push(skb, offset);
	NAPI_GRO_CB(skb)->same_flow = 0;
	NAPI_GRO_CB(skb)->flush = 1;

	return NULL;
}

static const struct net_offload esp4_offload = {
	.callbacks = {
		.gro_receive = esp4_gro_receive,
	},
};

static int __init esp4_offload_init(void)
{
	return inet_add_offload(&esp4_offload, IPPROTO_ESP);
}

static void __exit esp4_offload_exit(void)
{
	inet_del_offload(&esp4_offload, IPPROTO_ESP);
}

module_init(esp4_offload_init);
module_exit(esp4_offload_exit);
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Steffen Klassert <steffen.klassert@secunet.com>");
