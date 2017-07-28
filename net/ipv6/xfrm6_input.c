/*
 * xfrm6_input.c: based on net/ipv4/xfrm4_input.c
 *
 * Authors:
 *	Mitsuru KANDA @USAGI
 *	Kazunori MIYAZAWA @USAGI
 *	Kunihiro Ishiguro <kunihiro@ipinfusion.com>
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

int xfrm6_rcv_spi(struct sk_buff *skb, int nexthdr, __be32 spi,
		  struct ip6_tnl *t)
{
	XFRM_TUNNEL_SKB_CB(skb)->tunnel.ip6 = t;
	XFRM_SPI_SKB_CB(skb)->family = AF_INET6;
	XFRM_SPI_SKB_CB(skb)->daddroff = offsetof(struct ipv6hdr, daddr);
	return xfrm_input(skb, nexthdr, spi, 0);
}
EXPORT_SYMBOL(xfrm6_rcv_spi);

int xfrm6_transport_finish(struct sk_buff *skb, int async)
{
	struct xfrm_offload *xo = xfrm_offload(skb);

	skb_network_header(skb)[IP6CB(skb)->nhoff] =
		XFRM_MODE_SKB_CB(skb)->protocol;

#ifndef CONFIG_NETFILTER
	if (!async)
		return 1;
#endif

	__skb_push(skb, skb->data - skb_network_header(skb));
	ipv6_hdr(skb)->payload_len = htons(skb->len - sizeof(struct ipv6hdr));

	if (xo && (xo->flags & XFRM_GRO)) {
		skb_mac_header_rebuild(skb);
		return -1;
	}

	NF_HOOK(NFPROTO_IPV6, NF_INET_PRE_ROUTING,
		dev_net(skb->dev), NULL, skb, skb->dev, NULL,
		ip6_rcv_finish);
	return -1;
}

int xfrm6_rcv_tnl(struct sk_buff *skb, struct ip6_tnl *t)
{
	return xfrm6_rcv_spi(skb, skb_network_header(skb)[IP6CB(skb)->nhoff],
			     0, t);
}
EXPORT_SYMBOL(xfrm6_rcv_tnl);

int xfrm6_rcv(struct sk_buff *skb)
{
	return xfrm6_rcv_tnl(skb, NULL);
}
EXPORT_SYMBOL(xfrm6_rcv);
int xfrm6_input_addr(struct sk_buff *skb, xfrm_address_t *daddr,
		     xfrm_address_t *saddr, u8 proto)
{
	struct net *net = dev_net(skb->dev);
	struct xfrm_state *x = NULL;
	int i = 0;

	if (secpath_set(skb)) {
		XFRM_INC_STATS(net, LINUX_MIB_XFRMINERROR);
		goto drop;
	}

	if (1 + skb->sp->len == XFRM_MAX_DEPTH) {
		XFRM_INC_STATS(net, LINUX_MIB_XFRMINBUFFERERROR);
		goto drop;
	}

	for (i = 0; i < 3; i++) {
		xfrm_address_t *dst, *src;

		switch (i) {
		case 0:
			dst = daddr;
			src = saddr;
			break;
		case 1:
			/* lookup state with wild-card source address */
			dst = daddr;
			src = (xfrm_address_t *)&in6addr_any;
			break;
		default:
			/* lookup state with wild-card addresses */
			dst = (xfrm_address_t *)&in6addr_any;
			src = (xfrm_address_t *)&in6addr_any;
			break;
		}

		x = xfrm_state_lookup_byaddr(net, skb->mark, dst, src, proto, AF_INET6);
		if (!x)
			continue;

		spin_lock(&x->lock);

		if ((!i || (x->props.flags & XFRM_STATE_WILDRECV)) &&
		    likely(x->km.state == XFRM_STATE_VALID) &&
		    !xfrm_state_check_expire(x)) {
			spin_unlock(&x->lock);
			if (x->type->input(x, skb) > 0) {
				/* found a valid state */
				break;
			}
		} else
			spin_unlock(&x->lock);

		xfrm_state_put(x);
		x = NULL;
	}

	if (!x) {
		XFRM_INC_STATS(net, LINUX_MIB_XFRMINNOSTATES);
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
