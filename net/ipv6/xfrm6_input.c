/*
 * xfrm6_input.c: based on net/ipv4/xfrm4_input.c
 *
 * Authors:
 *	Mitsuru KANDA @USAGI
 * 	Kazunori MIYAZAWA @USAGI
 * 	Kunihiro Ishiguro <kunihiro@ipinfusion.com>
 *	YOSHIFUJI Hideaki @USAGI
 *		IPv6 support
 */

#include <linux/module.h>
#include <linux/string.h>
#include <net/dsfield.h>
#include <net/inet_ecn.h>
#include <net/ip.h>
#include <net/ipv6.h>
#include <net/xfrm.h>

static inline void ipip6_ecn_decapsulate(struct sk_buff *skb)
{
	struct ipv6hdr *outer_iph = skb->nh.ipv6h;
	struct ipv6hdr *inner_iph = skb->h.ipv6h;

	if (INET_ECN_is_ce(ipv6_get_dsfield(outer_iph)))
		IP6_ECN_set_ce(inner_iph);
}

int xfrm6_rcv_spi(struct sk_buff **pskb, unsigned int *nhoffp, u32 spi)
{
	struct sk_buff *skb = *pskb;
	int err;
	u32 seq;
	struct sec_decap_state xfrm_vec[XFRM_MAX_DEPTH];
	struct xfrm_state *x;
	int xfrm_nr = 0;
	int decaps = 0;
	int nexthdr;
	unsigned int nhoff;

	nhoff = *nhoffp;
	nexthdr = skb->nh.raw[nhoff];

	seq = 0;
	if (!spi && (err = xfrm_parse_spi(skb, nexthdr, &spi, &seq)) != 0)
		goto drop;
	
	do {
		struct ipv6hdr *iph = skb->nh.ipv6h;

		if (xfrm_nr == XFRM_MAX_DEPTH)
			goto drop;

		x = xfrm_state_lookup((xfrm_address_t *)&iph->daddr, spi, nexthdr, AF_INET6);
		if (x == NULL)
			goto drop;
		spin_lock(&x->lock);
		if (unlikely(x->km.state != XFRM_STATE_VALID))
			goto drop_unlock;

		if (x->props.replay_window && xfrm_replay_check(x, seq))
			goto drop_unlock;

		if (xfrm_state_check_expire(x))
			goto drop_unlock;

		nexthdr = x->type->input(x, &(xfrm_vec[xfrm_nr].decap), skb);
		if (nexthdr <= 0)
			goto drop_unlock;

		skb->nh.raw[nhoff] = nexthdr;

		if (x->props.replay_window)
			xfrm_replay_advance(x, seq);

		x->curlft.bytes += skb->len;
		x->curlft.packets++;

		spin_unlock(&x->lock);

		xfrm_vec[xfrm_nr++].xvec = x;

		if (x->props.mode) { /* XXX */
			if (nexthdr != IPPROTO_IPV6)
				goto drop;
			if (!pskb_may_pull(skb, sizeof(struct ipv6hdr)))
				goto drop;
			if (skb_cloned(skb) &&
			    pskb_expand_head(skb, 0, 0, GFP_ATOMIC))
				goto drop;
			if (x->props.flags & XFRM_STATE_DECAP_DSCP)
				ipv6_copy_dscp(skb->nh.ipv6h, skb->h.ipv6h);
			if (!(x->props.flags & XFRM_STATE_NOECN))
				ipip6_ecn_decapsulate(skb);
			skb->mac.raw = memmove(skb->data - skb->mac_len,
					       skb->mac.raw, skb->mac_len);
			skb->nh.raw = skb->data;
			decaps = 1;
			break;
		}

		if ((err = xfrm_parse_spi(skb, nexthdr, &spi, &seq)) < 0)
			goto drop;
	} while (!err);

	/* Allocate new secpath or COW existing one. */
	if (!skb->sp || atomic_read(&skb->sp->refcnt) != 1) {
		struct sec_path *sp;
		sp = secpath_dup(skb->sp);
		if (!sp)
			goto drop;
		if (skb->sp)
			secpath_put(skb->sp);
		skb->sp = sp;
	}

	if (xfrm_nr + skb->sp->len > XFRM_MAX_DEPTH)
		goto drop;

	memcpy(skb->sp->x+skb->sp->len, xfrm_vec, xfrm_nr*sizeof(struct sec_decap_state));
	skb->sp->len += xfrm_nr;
	skb->ip_summed = CHECKSUM_NONE;

	if (decaps) {
		if (!(skb->dev->flags&IFF_LOOPBACK)) {
			dst_release(skb->dst);
			skb->dst = NULL;
		}
		netif_rx(skb);
		return -1;
	} else {
		return 1;
	}

drop_unlock:
	spin_unlock(&x->lock);
	xfrm_state_put(x);
drop:
	while (--xfrm_nr >= 0)
		xfrm_state_put(xfrm_vec[xfrm_nr].xvec);
	kfree_skb(skb);
	return -1;
}

EXPORT_SYMBOL(xfrm6_rcv_spi);

int xfrm6_rcv(struct sk_buff **pskb, unsigned int *nhoffp)
{
	return xfrm6_rcv_spi(pskb, nhoffp, 0);
}
