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
#include <linux/netfilter.h>
#include <linux/netfilter_ipv6.h>
#include <net/ipv6.h>
#include <net/xfrm.h>

int xfrm6_rcv_spi(struct sk_buff *skb, int nexthdr, __be32 spi)
{
	int err;
	__be32 seq;
	struct xfrm_state *xfrm_vec[XFRM_MAX_DEPTH];
	struct xfrm_state *x;
	int xfrm_nr = 0;
	int decaps = 0;
	unsigned int nhoff;

	nhoff = IP6CB(skb)->nhoff;

	seq = 0;
	if (!spi && (err = xfrm_parse_spi(skb, nexthdr, &spi, &seq)) != 0)
		goto drop;

	do {
		struct ipv6hdr *iph = ipv6_hdr(skb);

		if (xfrm_nr == XFRM_MAX_DEPTH)
			goto drop;

		x = xfrm_state_lookup((xfrm_address_t *)&iph->daddr, spi,
				      nexthdr, AF_INET6);
		if (x == NULL)
			goto drop;
		spin_lock(&x->lock);
		if (unlikely(x->km.state != XFRM_STATE_VALID))
			goto drop_unlock;

		if (x->props.replay_window && xfrm_replay_check(x, seq))
			goto drop_unlock;

		if (xfrm_state_check_expire(x))
			goto drop_unlock;

		nexthdr = x->type->input(x, skb);
		if (nexthdr <= 0)
			goto drop_unlock;

		skb_network_header(skb)[nhoff] = nexthdr;

		if (x->props.replay_window)
			xfrm_replay_advance(x, seq);

		x->curlft.bytes += skb->len;
		x->curlft.packets++;

		spin_unlock(&x->lock);

		xfrm_vec[xfrm_nr++] = x;

		if (x->outer_mode->input(x, skb))
			goto drop;

		if (x->outer_mode->flags & XFRM_MODE_FLAG_TUNNEL) {
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

	memcpy(skb->sp->xvec + skb->sp->len, xfrm_vec,
	       xfrm_nr * sizeof(xfrm_vec[0]));
	skb->sp->len += xfrm_nr;

	nf_reset(skb);

	if (decaps) {
		dst_release(skb->dst);
		skb->dst = NULL;
		netif_rx(skb);
		return -1;
	} else {
#ifdef CONFIG_NETFILTER
		ipv6_hdr(skb)->payload_len = htons(skb->len);
		__skb_push(skb, skb->data - skb_network_header(skb));

		NF_HOOK(PF_INET6, NF_IP6_PRE_ROUTING, skb, skb->dev, NULL,
			ip6_rcv_finish);
		return -1;
#else
		return 1;
#endif
	}

drop_unlock:
	spin_unlock(&x->lock);
	xfrm_state_put(x);
drop:
	while (--xfrm_nr >= 0)
		xfrm_state_put(xfrm_vec[xfrm_nr]);
	kfree_skb(skb);
	return -1;
}

EXPORT_SYMBOL(xfrm6_rcv_spi);

int xfrm6_rcv(struct sk_buff *skb)
{
	return xfrm6_rcv_spi(skb, skb_network_header(skb)[IP6CB(skb)->nhoff],
			     0);
}

EXPORT_SYMBOL(xfrm6_rcv);

int xfrm6_input_addr(struct sk_buff *skb, xfrm_address_t *daddr,
		     xfrm_address_t *saddr, u8 proto)
{
	struct xfrm_state *x = NULL;
	int wildcard = 0;
	xfrm_address_t *xany;
	struct xfrm_state *xfrm_vec_one = NULL;
	int nh = 0;
	int i = 0;

	xany = (xfrm_address_t *)&in6addr_any;

	for (i = 0; i < 3; i++) {
		xfrm_address_t *dst, *src;
		switch (i) {
		case 0:
			dst = daddr;
			src = saddr;
			break;
		case 1:
			/* lookup state with wild-card source address */
			wildcard = 1;
			dst = daddr;
			src = xany;
			break;
		case 2:
		default:
			/* lookup state with wild-card addresses */
			wildcard = 1; /* XXX */
			dst = xany;
			src = xany;
			break;
		}

		x = xfrm_state_lookup_byaddr(dst, src, proto, AF_INET6);
		if (!x)
			continue;

		spin_lock(&x->lock);

		if (wildcard) {
			if ((x->props.flags & XFRM_STATE_WILDRECV) == 0) {
				spin_unlock(&x->lock);
				xfrm_state_put(x);
				x = NULL;
				continue;
			}
		}

		if (unlikely(x->km.state != XFRM_STATE_VALID)) {
			spin_unlock(&x->lock);
			xfrm_state_put(x);
			x = NULL;
			continue;
		}
		if (xfrm_state_check_expire(x)) {
			spin_unlock(&x->lock);
			xfrm_state_put(x);
			x = NULL;
			continue;
		}

		nh = x->type->input(x, skb);
		if (nh <= 0) {
			spin_unlock(&x->lock);
			xfrm_state_put(x);
			x = NULL;
			continue;
		}

		x->curlft.bytes += skb->len;
		x->curlft.packets++;

		spin_unlock(&x->lock);

		xfrm_vec_one = x;
		break;
	}

	if (!xfrm_vec_one)
		goto drop;

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

	if (1 + skb->sp->len > XFRM_MAX_DEPTH)
		goto drop;

	skb->sp->xvec[skb->sp->len] = xfrm_vec_one;
	skb->sp->len ++;

	return 1;
drop:
	if (xfrm_vec_one)
		xfrm_state_put(xfrm_vec_one);
	return -1;
}

EXPORT_SYMBOL(xfrm6_input_addr);
