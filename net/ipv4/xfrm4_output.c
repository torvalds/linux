/*
 * xfrm4_output.c - Common IPsec encapsulation code for IPv4.
 * Copyright (c) 2004 Herbert Xu <herbert@gondor.apana.org.au>
 * 
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

#include <linux/skbuff.h>
#include <linux/spinlock.h>
#include <net/inet_ecn.h>
#include <net/ip.h>
#include <net/xfrm.h>
#include <net/icmp.h>

/* Add encapsulation header.
 *
 * In transport mode, the IP header will be moved forward to make space
 * for the encapsulation header.
 *
 * In tunnel mode, the top IP header will be constructed per RFC 2401.
 * The following fields in it shall be filled in by x->type->output:
 *	tot_len
 *	check
 *
 * On exit, skb->h will be set to the start of the payload to be processed
 * by x->type->output and skb->nh will be set to the top IP header.
 */
static void xfrm4_encap(struct sk_buff *skb)
{
	struct dst_entry *dst = skb->dst;
	struct xfrm_state *x = dst->xfrm;
	struct iphdr *iph, *top_iph;
	int flags;

	iph = skb->nh.iph;
	skb->h.ipiph = iph;

	skb->nh.raw = skb_push(skb, x->props.header_len);
	top_iph = skb->nh.iph;

	if (!x->props.mode) {
		skb->h.raw += iph->ihl*4;
		memmove(top_iph, iph, iph->ihl*4);
		return;
	}

	top_iph->ihl = 5;
	top_iph->version = 4;

	/* DS disclosed */
	top_iph->tos = INET_ECN_encapsulate(iph->tos, iph->tos);

	flags = x->props.flags;
	if (flags & XFRM_STATE_NOECN)
		IP_ECN_clear(top_iph);

	top_iph->frag_off = (flags & XFRM_STATE_NOPMTUDISC) ?
		0 : (iph->frag_off & htons(IP_DF));
	if (!top_iph->frag_off)
		__ip_select_ident(top_iph, dst, 0);

	top_iph->ttl = dst_metric(dst->child, RTAX_HOPLIMIT);

	top_iph->saddr = x->props.saddr.a4;
	top_iph->daddr = x->id.daddr.a4;
	top_iph->protocol = IPPROTO_IPIP;

	memset(&(IPCB(skb)->opt), 0, sizeof(struct ip_options));
}

static int xfrm4_tunnel_check_size(struct sk_buff *skb)
{
	int mtu, ret = 0;
	struct dst_entry *dst;
	struct iphdr *iph = skb->nh.iph;

	if (IPCB(skb)->flags & IPSKB_XFRM_TUNNEL_SIZE)
		goto out;

	IPCB(skb)->flags |= IPSKB_XFRM_TUNNEL_SIZE;
	
	if (!(iph->frag_off & htons(IP_DF)) || skb->local_df)
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

int xfrm4_output(struct sk_buff *skb)
{
	struct dst_entry *dst = skb->dst;
	struct xfrm_state *x = dst->xfrm;
	int err;
	
	if (skb->ip_summed == CHECKSUM_HW) {
		err = skb_checksum_help(skb, 0);
		if (err)
			goto error_nolock;
	}

	if (x->props.mode) {
		err = xfrm4_tunnel_check_size(skb);
		if (err)
			goto error_nolock;
	}

	spin_lock_bh(&x->lock);
	err = xfrm_state_check(x, skb);
	if (err)
		goto error;

	xfrm4_encap(skb);

	err = x->type->output(x, skb);
	if (err)
		goto error;

	x->curlft.bytes += skb->len;
	x->curlft.packets++;

	spin_unlock_bh(&x->lock);
	
	if (!(skb->dst = dst_pop(dst))) {
		err = -EHOSTUNREACH;
		goto error_nolock;
	}
	err = NET_XMIT_BYPASS;

out_exit:
	return err;
error:
	spin_unlock_bh(&x->lock);
error_nolock:
	kfree_skb(skb);
	goto out_exit;
}
