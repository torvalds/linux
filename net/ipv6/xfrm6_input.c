// SPDX-License-Identifier: GPL-2.0
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

int xfrm6_rcv_spi(struct sk_buff *skb, int nexthdr, __be32 spi,
		  struct ip6_tnl *t)
{
	XFRM_TUNNEL_SKB_CB(skb)->tunnel.ip6 = t;
	XFRM_SPI_SKB_CB(skb)->family = AF_INET6;
	XFRM_SPI_SKB_CB(skb)->daddroff = offsetof(struct ipv6hdr, daddr);
	return xfrm_input(skb, nexthdr, spi, 0);
}
EXPORT_SYMBOL(xfrm6_rcv_spi);

static int xfrm6_transport_finish2(struct net *net, struct sock *sk,
				   struct sk_buff *skb)
{
	if (xfrm_trans_queue(skb, ip6_rcv_finish)) {
		kfree_skb(skb);
		return NET_RX_DROP;
	}

	return 0;
}

int xfrm6_transport_finish(struct sk_buff *skb, int async)
{
	struct xfrm_offload *xo = xfrm_offload(skb);
	int nhlen = skb->data - skb_network_header(skb);

	skb_network_header(skb)[IP6CB(skb)->nhoff] =
		XFRM_MODE_SKB_CB(skb)->protocol;

#ifndef CONFIG_NETFILTER
	if (!async)
		return 1;
#endif

	__skb_push(skb, nhlen);
	ipv6_hdr(skb)->payload_len = htons(skb->len - sizeof(struct ipv6hdr));
	skb_postpush_rcsum(skb, skb_network_header(skb), nhlen);

	if (xo && (xo->flags & XFRM_GRO)) {
		/* The full l2 header needs to be preserved so that re-injecting the packet at l2
		 * works correctly in the presence of vlan tags.
		 */
		skb_mac_header_rebuild_full(skb, xo->orig_mac_len);
		skb_reset_network_header(skb);
		skb_reset_transport_header(skb);
		return 0;
	}

	NF_HOOK(NFPROTO_IPV6, NF_INET_PRE_ROUTING,
		dev_net(skb->dev), NULL, skb, skb->dev, NULL,
		xfrm6_transport_finish2);
	return 0;
}

/* If it's a keepalive packet, then just eat it.
 * If it's an encapsulated packet, then pass it to the
 * IPsec xfrm input.
 * Returns 0 if skb passed to xfrm or was dropped.
 * Returns >0 if skb should be passed to UDP.
 * Returns <0 if skb should be resubmitted (-ret is protocol)
 */
int xfrm6_udp_encap_rcv(struct sock *sk, struct sk_buff *skb)
{
	struct udp_sock *up = udp_sk(sk);
	struct udphdr *uh;
	struct ipv6hdr *ip6h;
	int len;
	int ip6hlen = sizeof(struct ipv6hdr);
	__u8 *udpdata;
	__be32 *udpdata32;
	u16 encap_type;

	if (skb->protocol == htons(ETH_P_IP))
		return xfrm4_udp_encap_rcv(sk, skb);

	encap_type = READ_ONCE(up->encap_type);
	/* if this is not encapsulated socket, then just return now */
	if (!encap_type)
		return 1;

	/* If this is a paged skb, make sure we pull up
	 * whatever data we need to look at. */
	len = skb->len - sizeof(struct udphdr);
	if (!pskb_may_pull(skb, sizeof(struct udphdr) + min(len, 8)))
		return 1;

	/* Now we can get the pointers */
	uh = udp_hdr(skb);
	udpdata = (__u8 *)uh + sizeof(struct udphdr);
	udpdata32 = (__be32 *)udpdata;

	switch (encap_type) {
	default:
	case UDP_ENCAP_ESPINUDP:
		/* Check if this is a keepalive packet.  If so, eat it. */
		if (len == 1 && udpdata[0] == 0xff) {
			goto drop;
		} else if (len > sizeof(struct ip_esp_hdr) && udpdata32[0] != 0) {
			/* ESP Packet without Non-ESP header */
			len = sizeof(struct udphdr);
		} else
			/* Must be an IKE packet.. pass it through */
			return 1;
		break;
	case UDP_ENCAP_ESPINUDP_NON_IKE:
		/* Check if this is a keepalive packet.  If so, eat it. */
		if (len == 1 && udpdata[0] == 0xff) {
			goto drop;
		} else if (len > 2 * sizeof(u32) + sizeof(struct ip_esp_hdr) &&
			   udpdata32[0] == 0 && udpdata32[1] == 0) {

			/* ESP Packet with Non-IKE marker */
			len = sizeof(struct udphdr) + 2 * sizeof(u32);
		} else
			/* Must be an IKE packet.. pass it through */
			return 1;
		break;
	}

	/* At this point we are sure that this is an ESPinUDP packet,
	 * so we need to remove 'len' bytes from the packet (the UDP
	 * header and optional ESP marker bytes) and then modify the
	 * protocol to ESP, and then call into the transform receiver.
	 */
	if (skb_unclone(skb, GFP_ATOMIC))
		goto drop;

	/* Now we can update and verify the packet length... */
	ip6h = ipv6_hdr(skb);
	ip6h->payload_len = htons(ntohs(ip6h->payload_len) - len);
	if (skb->len < ip6hlen + len) {
		/* packet is too small!?! */
		goto drop;
	}

	/* pull the data buffer up to the ESP header and set the
	 * transport header to point to ESP.  Keep UDP on the stack
	 * for later.
	 */
	__skb_pull(skb, len);
	skb_reset_transport_header(skb);

	/* process ESP */
	return xfrm6_rcv_encap(skb, IPPROTO_ESP, 0, encap_type);

drop:
	kfree_skb(skb);
	return 0;
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
	struct sec_path *sp;
	int i = 0;

	sp = secpath_set(skb);
	if (!sp) {
		XFRM_INC_STATS(net, LINUX_MIB_XFRMINERROR);
		goto drop;
	}

	if (1 + sp->len == XFRM_MAX_DEPTH) {
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

	sp->xvec[sp->len++] = x;

	spin_lock(&x->lock);

	x->curlft.bytes += skb->len;
	x->curlft.packets++;

	spin_unlock(&x->lock);

	return 1;

drop:
	return -1;
}
EXPORT_SYMBOL(xfrm6_input_addr);
