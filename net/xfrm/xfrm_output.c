/*
 * xfrm_output.c - Common IPsec encapsulation code.
 *
 * Copyright (c) 2007 Herbert Xu <herbert@gondor.apana.org.au>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

#include <linux/errno.h>
#include <linux/module.h>
#include <linux/netdevice.h>
#include <linux/skbuff.h>
#include <linux/spinlock.h>
#include <linux/time.h>
#include <net/dst.h>
#include <net/xfrm.h>

int xfrm_output(struct sk_buff *skb)
{
	struct dst_entry *dst = skb->dst;
	struct xfrm_state *x = dst->xfrm;
	int err;

	if (skb->ip_summed == CHECKSUM_PARTIAL) {
		err = skb_checksum_help(skb);
		if (err)
			goto error_nolock;
	}

	do {
		spin_lock_bh(&x->lock);
		err = xfrm_state_check(x, skb);
		if (err)
			goto error;

		err = x->mode->output(x, skb);
		if (err)
			goto error;

		err = x->type->output(x, skb);
		if (err)
			goto error;

		x->curlft.bytes += skb->len;
		x->curlft.packets++;

		if (x->props.mode == XFRM_MODE_ROUTEOPTIMIZATION)
			x->lastused = get_seconds();

		spin_unlock_bh(&x->lock);

		skb_reset_network_header(skb);

		if (!(skb->dst = dst_pop(dst))) {
			err = -EHOSTUNREACH;
			goto error_nolock;
		}
		dst = skb->dst;
		x = dst->xfrm;
	} while (x && (x->props.mode != XFRM_MODE_TUNNEL));

	err = 0;

error_nolock:
	return err;
error:
	spin_unlock_bh(&x->lock);
	goto error_nolock;
}
EXPORT_SYMBOL_GPL(xfrm_output);
