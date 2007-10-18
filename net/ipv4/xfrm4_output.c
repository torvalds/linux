/*
 * xfrm4_output.c - Common IPsec encapsulation code for IPv4.
 * Copyright (c) 2004 Herbert Xu <herbert@gondor.apana.org.au>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

#include <linux/compiler.h>
#include <linux/if_ether.h>
#include <linux/kernel.h>
#include <linux/skbuff.h>
#include <linux/netfilter_ipv4.h>
#include <net/ip.h>
#include <net/xfrm.h>
#include <net/icmp.h>

static int xfrm4_tunnel_check_size(struct sk_buff *skb)
{
	int mtu, ret = 0;
	struct dst_entry *dst;

	if (IPCB(skb)->flags & IPSKB_XFRM_TUNNEL_SIZE)
		goto out;

	IPCB(skb)->flags |= IPSKB_XFRM_TUNNEL_SIZE;

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

static inline int xfrm4_output_one(struct sk_buff *skb)
{
	struct dst_entry *dst = skb->dst;
	struct xfrm_state *x = dst->xfrm;
	struct iphdr *iph;
	int err;

	if (x->outer_mode->flags & XFRM_MODE_FLAG_TUNNEL) {
		err = xfrm4_tunnel_check_size(skb);
		if (err)
			goto error_nolock;
	}

	err = xfrm_output(skb);
	if (err)
		goto error_nolock;

	iph = ip_hdr(skb);
	iph->tot_len = htons(skb->len);
	ip_send_check(iph);

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

		err = nf_hook(PF_INET, NF_IP_LOCAL_OUT, skb, NULL,
			      skb->dst->dev, dst_output);
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
