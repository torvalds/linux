/*
 * xfrm4_output.c - Common IPsec encapsulation code for IPv4.
 * Copyright (c) 2004 Herbert Xu <herbert@gondor.apana.org.au>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

#include <linux/if_ether.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/skbuff.h>
#include <linux/netfilter_ipv4.h>
#include <net/dst.h>
#include <net/ip.h>
#include <net/xfrm.h>
#include <net/icmp.h>

static int xfrm4_tunnel_check_size(struct sk_buff *skb)
{
	int mtu, ret = 0;
	struct dst_entry *dst;

	if (IPCB(skb)->flags & IPSKB_XFRM_TUNNEL_SIZE)
		goto out;

	if (!(ip_hdr(skb)->frag_off & htons(IP_DF)) || skb->local_df)
		goto out;

	dst = skb->dst;
	mtu = dst_mtu(dst);
	if (skb->len > mtu) {
		icmp_send(skb, ICMP_DEST_UNREACH, ICMP_FRAG_NEEDED, htonl(mtu));
		ret = -EMSGSIZE;
	}
out:
	return ret;
}

int xfrm4_extract_output(struct xfrm_state *x, struct sk_buff *skb)
{
	int err;

	err = xfrm4_tunnel_check_size(skb);
	if (err)
		return err;

	return xfrm4_extract_header(skb);
}

int xfrm4_prepare_output(struct xfrm_state *x, struct sk_buff *skb)
{
	int err;

	err = x->inner_mode->afinfo->extract_output(x, skb);
	if (err)
		return err;

	memset(IPCB(skb), 0, sizeof(*IPCB(skb)));
	IPCB(skb)->flags |= IPSKB_XFRM_TUNNEL_SIZE;

	skb->protocol = htons(ETH_P_IP);

	return x->outer_mode->output2(x, skb);
}
EXPORT_SYMBOL(xfrm4_prepare_output);

static inline int xfrm4_output_one(struct sk_buff *skb)
{
	int err;

	err = xfrm_output(skb);
	if (err)
		goto error_nolock;

	IPCB(skb)->flags |= IPSKB_XFRM_TRANSFORMED;
	err = 0;

out_exit:
	return err;
error_nolock:
	kfree_skb(skb);
	goto out_exit;
}

static int xfrm4_output_finish2(struct sk_buff *skb)
{
	int err;

	while (likely((err = xfrm4_output_one(skb)) == 0)) {
		nf_reset(skb);

		err = __ip_local_out(skb);
		if (unlikely(err != 1))
			break;

		if (!skb->dst->xfrm)
			return dst_output(skb);

		err = nf_hook(PF_INET, NF_IP_POST_ROUTING, skb, NULL,
			      skb->dst->dev, xfrm4_output_finish2);
		if (unlikely(err != 1))
			break;
	}

	return err;
}

static int xfrm4_output_finish(struct sk_buff *skb)
{
	struct sk_buff *segs;

#ifdef CONFIG_NETFILTER
	if (!skb->dst->xfrm) {
		IPCB(skb)->flags |= IPSKB_REROUTED;
		return dst_output(skb);
	}
#endif

	if (!skb_is_gso(skb))
		return xfrm4_output_finish2(skb);

	skb->protocol = htons(ETH_P_IP);
	segs = skb_gso_segment(skb, 0);
	kfree_skb(skb);
	if (unlikely(IS_ERR(segs)))
		return PTR_ERR(segs);

	do {
		struct sk_buff *nskb = segs->next;
		int err;

		segs->next = NULL;
		err = xfrm4_output_finish2(segs);

		if (unlikely(err)) {
			while ((segs = nskb)) {
				nskb = segs->next;
				segs->next = NULL;
				kfree_skb(segs);
			}
			return err;
		}

		segs = nskb;
	} while (segs);

	return 0;
}

int xfrm4_output(struct sk_buff *skb)
{
	return NF_HOOK_COND(PF_INET, NF_IP_POST_ROUTING, skb, NULL, skb->dst->dev,
			    xfrm4_output_finish,
			    !(IPCB(skb)->flags & IPSKB_REROUTED));
}
