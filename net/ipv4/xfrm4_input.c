/*
 * xfrm4_input.c
 *
 * Changes:
 *	YOSHIFUJI Hideaki @USAGI
 *		Split up af-specific portion
 *	Derek Atkins <derek@ihtfp.com>
 *		Add Encapsulation support
 * 	
 */

#include <linux/module.h>
#include <linux/string.h>
#include <net/inet_ecn.h>
#include <net/ip.h>
#include <net/xfrm.h>

int xfrm4_rcv(struct sk_buff *skb)
{
	return xfrm4_rcv_encap(skb, 0);
}

EXPORT_SYMBOL(xfrm4_rcv);

static inline void ipip_ecn_decapsulate(struct sk_buff *skb)
{
	struct iphdr *outer_iph = skb->nh.iph;
	struct iphdr *inner_iph = skb->h.ipiph;

	if (INET_ECN_is_ce(outer_iph->tos))
		IP_ECN_set_ce(inner_iph);
}

static int xfrm4_parse_spi(struct sk_buff *skb, u8 nexthdr, u32 *spi, u32 *seq)
{
	switch (nexthdr) {
	case IPPROTO_IPIP:
		if (!pskb_may_pull(skb, sizeof(struct iphdr)))
			return -EINVAL;
		*spi = skb->nh.iph->saddr;
		*seq = 0;
		return 0;
	}

	return xfrm_parse_spi(skb, nexthdr, spi, seq);
}

int xfrm4_rcv_encap(struct sk_buff *skb, __u16 encap_type)
{
	int err;
	u32 spi, seq;
	struct sec_decap_state xfrm_vec[XFRM_MAX_DEPTH];
	struct xfrm_state *x;
	int xfrm_nr = 0;
	int decaps = 0;

	if ((err = xfrm4_parse_spi(skb, skb->nh.iph->protocol, &spi, &seq)) != 0)
		goto drop;

	do {
		struct iphdr *iph = skb->nh.iph;

		if (xfrm_nr == XFRM_MAX_DEPTH)
			goto drop;

		x = xfrm_state_lookup((xfrm_address_t *)&iph->daddr, spi, iph->protocol, AF_INET);
		if (x == NULL)
			goto drop;

		spin_lock(&x->lock);
		if (unlikely(x->km.state != XFRM_STATE_VALID))
			goto drop_unlock;

		if (x->props.replay_window && xfrm_replay_check(x, seq))
			goto drop_unlock;

		if (xfrm_state_check_expire(x))
			goto drop_unlock;

		xfrm_vec[xfrm_nr].decap.decap_type = encap_type;
		if (x->type->input(x, &(xfrm_vec[xfrm_nr].decap), skb))
			goto drop_unlock;

		/* only the first xfrm gets the encap type */
		encap_type = 0;

		if (x->props.replay_window)
			xfrm_replay_advance(x, seq);

		x->curlft.bytes += skb->len;
		x->curlft.packets++;

		spin_unlock(&x->lock);

		xfrm_vec[xfrm_nr++].xvec = x;

		iph = skb->nh.iph;

		if (x->props.mode) {
			if (iph->protocol != IPPROTO_IPIP)
				goto drop;
			if (!pskb_may_pull(skb, sizeof(struct iphdr)))
				goto drop;
			if (skb_cloned(skb) &&
			    pskb_expand_head(skb, 0, 0, GFP_ATOMIC))
				goto drop;
			if (x->props.flags & XFRM_STATE_DECAP_DSCP)
				ipv4_copy_dscp(iph, skb->h.ipiph);
			if (!(x->props.flags & XFRM_STATE_NOECN))
				ipip_ecn_decapsulate(skb);
			skb->mac.raw = memmove(skb->data - skb->mac_len,
					       skb->mac.raw, skb->mac_len);
			skb->nh.raw = skb->data;
			memset(&(IPCB(skb)->opt), 0, sizeof(struct ip_options));
			decaps = 1;
			break;
		}

		if ((err = xfrm_parse_spi(skb, skb->nh.iph->protocol, &spi, &seq)) < 0)
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

	if (decaps) {
		if (!(skb->dev->flags&IFF_LOOPBACK)) {
			dst_release(skb->dst);
			skb->dst = NULL;
		}
		netif_rx(skb);
		return 0;
	} else {
		return -skb->nh.iph->protocol;
	}

drop_unlock:
	spin_unlock(&x->lock);
	xfrm_state_put(x);
drop:
	while (--xfrm_nr >= 0)
		xfrm_state_put(xfrm_vec[xfrm_nr].xvec);

	kfree_skb(skb);
	return 0;
}
