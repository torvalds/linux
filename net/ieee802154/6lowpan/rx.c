/* This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2
 * as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/if_arp.h>

#include <net/6lowpan.h>
#include <net/ieee802154_netdev.h>

#include "6lowpan_i.h"

#define LOWPAN_DISPATCH_FRAG_MASK	0xf8

static int lowpan_give_skb_to_device(struct sk_buff *skb)
{
	skb->protocol = htons(ETH_P_IPV6);
	skb->pkt_type = PACKET_HOST;

	return netif_rx(skb);
}

static int lowpan_rx_handlers_result(struct sk_buff *skb, lowpan_rx_result res)
{
	switch (res) {
	case RX_CONTINUE:
		/* nobody cared about this packet */
		net_warn_ratelimited("%s: received unknown dispatch\n",
				     __func__);

		/* fall-through */
	case RX_DROP_UNUSABLE:
		kfree_skb(skb);

		/* fall-through */
	case RX_DROP:
		return NET_RX_DROP;
	case RX_QUEUED:
		return lowpan_give_skb_to_device(skb);
	default:
		break;
	}

	return NET_RX_DROP;
}

static inline bool lowpan_is_frag1(u8 dispatch)
{
	return (dispatch & LOWPAN_DISPATCH_FRAG_MASK) == LOWPAN_DISPATCH_FRAG1;
}

static inline bool lowpan_is_fragn(u8 dispatch)
{
	return (dispatch & LOWPAN_DISPATCH_FRAG_MASK) == LOWPAN_DISPATCH_FRAGN;
}

static lowpan_rx_result lowpan_rx_h_frag(struct sk_buff *skb)
{
	int ret;

	if (!(lowpan_is_frag1(*skb_network_header(skb)) ||
	      lowpan_is_fragn(*skb_network_header(skb))))
		return RX_CONTINUE;

	ret = lowpan_frag_rcv(skb, *skb_network_header(skb) &
			      LOWPAN_DISPATCH_FRAG_MASK);
	if (ret == 1)
		return RX_QUEUED;

	/* Packet is freed by lowpan_frag_rcv on error or put into the frag
	 * bucket.
	 */
	return RX_DROP;
}

int lowpan_iphc_decompress(struct sk_buff *skb)
{
	struct ieee802154_addr_sa sa, da;
	struct ieee802154_hdr hdr;
	u8 iphc0, iphc1;
	void *sap, *dap;

	if (ieee802154_hdr_peek_addrs(skb, &hdr) < 0)
		return -EINVAL;

	raw_dump_table(__func__, "raw skb data dump", skb->data, skb->len);

	if (lowpan_fetch_skb_u8(skb, &iphc0) ||
	    lowpan_fetch_skb_u8(skb, &iphc1))
		return -EINVAL;

	ieee802154_addr_to_sa(&sa, &hdr.source);
	ieee802154_addr_to_sa(&da, &hdr.dest);

	if (sa.addr_type == IEEE802154_ADDR_SHORT)
		sap = &sa.short_addr;
	else
		sap = &sa.hwaddr;

	if (da.addr_type == IEEE802154_ADDR_SHORT)
		dap = &da.short_addr;
	else
		dap = &da.hwaddr;

	return lowpan_header_decompress(skb, skb->dev, sap, sa.addr_type,
					IEEE802154_ADDR_LEN, dap, da.addr_type,
					IEEE802154_ADDR_LEN, iphc0, iphc1);
}

static lowpan_rx_result lowpan_rx_h_iphc(struct sk_buff *skb)
{
	int ret;

	if (!lowpan_is_iphc(*skb_network_header(skb)))
		return RX_CONTINUE;

	/* Setting datagram_offset to zero indicates non frag handling
	 * while doing lowpan_header_decompress.
	 */
	lowpan_802154_cb(skb)->d_size = 0;

	ret = lowpan_iphc_decompress(skb);
	if (ret < 0)
		return RX_DROP_UNUSABLE;

	return RX_QUEUED;
}

lowpan_rx_result lowpan_rx_h_ipv6(struct sk_buff *skb)
{
	if (!lowpan_is_ipv6(*skb_network_header(skb)))
		return RX_CONTINUE;

	/* Pull off the 1-byte of 6lowpan header. */
	skb_pull(skb, 1);
	return RX_QUEUED;
}

static int lowpan_invoke_rx_handlers(struct sk_buff *skb)
{
	lowpan_rx_result res;

#define CALL_RXH(rxh)			\
	do {				\
		res = rxh(skb);	\
		if (res != RX_CONTINUE)	\
			goto rxh_next;	\
	} while (0)

	/* likely at first */
	CALL_RXH(lowpan_rx_h_iphc);
	CALL_RXH(lowpan_rx_h_frag);
	CALL_RXH(lowpan_rx_h_ipv6);

rxh_next:
	return lowpan_rx_handlers_result(skb, res);
#undef CALL_RXH
}

static int lowpan_rcv(struct sk_buff *skb, struct net_device *wdev,
		      struct packet_type *pt, struct net_device *orig_wdev)
{
	struct net_device *ldev;

	if (wdev->type != ARPHRD_IEEE802154 ||
	    skb->pkt_type == PACKET_OTHERHOST)
		return NET_RX_DROP;

	ldev = wdev->ieee802154_ptr->lowpan_dev;
	if (!ldev || !netif_running(ldev))
		return NET_RX_DROP;

	/* Replacing skb->dev and followed rx handlers will manipulate skb. */
	skb = skb_share_check(skb, GFP_ATOMIC);
	if (!skb)
		return NET_RX_DROP;
	skb->dev = ldev;

	/* When receive frag1 it's likely that we manipulate the buffer.
	 * When recevie iphc we manipulate the data buffer. So we need
	 * to unshare the buffer.
	 */
	if (lowpan_is_frag1(*skb_network_header(skb)) ||
	    lowpan_is_iphc(*skb_network_header(skb))) {
		skb = skb_unshare(skb, GFP_ATOMIC);
		if (!skb)
			return RX_DROP;
	}

	return lowpan_invoke_rx_handlers(skb);
}

static struct packet_type lowpan_packet_type = {
	.type = htons(ETH_P_IEEE802154),
	.func = lowpan_rcv,
};

void lowpan_rx_init(void)
{
	dev_add_pack(&lowpan_packet_type);
}

void lowpan_rx_exit(void)
{
	dev_remove_pack(&lowpan_packet_type);
}
