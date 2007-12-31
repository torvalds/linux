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

int xfrm6_extract_input(struct xfrm_state *x, struct sk_buff *skb)
{
	return xfrm6_extract_header(skb);
}

int xfrm6_rcv_spi(struct sk_buff *skb, int nexthdr, __be32 spi)
{
	XFRM_SPI_SKB_CB(skb)->family = AF_INET6;
	XFRM_SPI_SKB_CB(skb)->daddroff = offsetof(struct ipv6hdr, daddr);
	return xfrm_input(skb, nexthdr, spi, 0);
}
EXPORT_SYMBOL(xfrm6_rcv_spi);

int xfrm6_transport_finish(struct sk_buff *skb, int async)
{
	skb_network_header(skb)[IP6CB(skb)->nhoff] =
		XFRM_MODE_SKB_CB(skb)->protocol;

#ifndef CONFIG_NETFILTER
	if (!async)
		return 1;
#endif

	ipv6_hdr(skb)->payload_len = htons(skb->len);
	__skb_push(skb, skb->data - skb_network_header(skb));

	NF_HOOK(PF_INET6, NF_INET_PRE_ROUTING, skb, skb->dev, NULL,
		ip6_rcv_finish);
	return -1;
}

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
	int nh = 0;
	int i = 0;

	/* Allocate new secpath or COW existing one. */
	if (!skb->sp || atomic_read(&skb->sp->refcnt) != 1) {
		struct sec_path *sp;

		sp = secpath_dup(skb->sp);
		if (!sp) {
			XFRM_INC_STATS(LINUX_MIB_XFRMINERROR);
			goto drop;
		}
		if (skb->sp)
			secpath_put(skb->sp);
		skb->sp = sp;
	}

	if (1 + skb->sp->len == XFRM_MAX_DEPTH) {
		XFRM_INC_STATS(LINUX_MIB_XFRMINBUFFERERROR);
		goto drop;
	}

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

		spin_unlock(&x->lock);

		nh = x->type->input(x, skb);
		if (nh <= 0) {
			xfrm_state_put(x);
			x = NULL;
			continue;
		}

		/* Found a state */
		break;
	}

	if (!x) {
		XFRM_INC_STATS(LINUX_MIB_XFRMINNOSTATES);
		xfrm_audit_state_notfound_simple(skb, AF_INET6);
		goto drop;
	}

	skb->sp->xvec[skb->sp->len++] = x;

	spin_lock(&x->lock);

	x->curlft.bytes += skb->len;
	x->curlft.packets++;

	spin_unlock(&x->lock);

	return 1;

drop:
	return -1;
}

EXPORT_SYMBOL(xfrm6_input_addr);
