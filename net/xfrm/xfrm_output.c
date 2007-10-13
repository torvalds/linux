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
#include <net/dst.h>
#include <net/xfrm.h>

static int xfrm_state_check_space(struct xfrm_state *x, struct sk_buff *skb)
{
	int nhead = x->props.header_len + LL_RESERVED_SPACE(skb->dst->dev)
		- skb_headroom(skb);

	if (nhead > 0)
		return pskb_expand_head(skb, nhead, 0, GFP_ATOMIC);

	/* Check tail too... */
	return 0;
}

static int xfrm_state_check(struct xfrm_state *x, struct sk_buff *skb)
{
	int err = xfrm_state_check_expire(x);
	if (err < 0)
		goto err;
	err = xfrm_state_check_space(x, skb);
err:
	return err;
}

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

		if (x->type->flags & XFRM_TYPE_REPLAY_PROT) {
			XFRM_SKB_CB(skb)->seq = ++x->replay.oseq;
			if (xfrm_aevent_is_on())
				xfrm_replay_notify(x, XFRM_REPLAY_UPDATE);
		}

		err = x->mode->output(x, skb);
		if (err)
			goto error;

		x->curlft.bytes += skb->len;
		x->curlft.packets++;

		spin_unlock_bh(&x->lock);

		err = x->type->output(x, skb);
		if (err)
			goto error_nolock;

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
