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

static int lowpan_give_skb_to_device(struct sk_buff *skb,
				     struct net_device *dev)
{
	skb->dev = dev->ieee802154_ptr->lowpan_dev;
	skb->protocol = htons(ETH_P_IPV6);
	skb->pkt_type = PACKET_HOST;

	return netif_rx(skb);
}

static int
iphc_decompress(struct sk_buff *skb, const struct ieee802154_hdr *hdr)
{
	u8 iphc0, iphc1;
	struct ieee802154_addr_sa sa, da;
	void *sap, *dap;

	raw_dump_table(__func__, "raw skb data dump", skb->data, skb->len);
	/* at least two bytes will be used for the encoding */
	if (skb->len < 2)
		return -EINVAL;

	if (lowpan_fetch_skb_u8(skb, &iphc0))
		return -EINVAL;

	if (lowpan_fetch_skb_u8(skb, &iphc1))
		return -EINVAL;

	ieee802154_addr_to_sa(&sa, &hdr->source);
	ieee802154_addr_to_sa(&da, &hdr->dest);

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

static int lowpan_rcv(struct sk_buff *skb, struct net_device *dev,
		      struct packet_type *pt, struct net_device *orig_dev)
{
	struct ieee802154_hdr hdr;
	int ret;

	if (dev->type != ARPHRD_IEEE802154 ||
	    !dev->ieee802154_ptr->lowpan_dev)
		goto drop;

	skb = skb_share_check(skb, GFP_ATOMIC);
	if (!skb)
		goto drop;

	if (!netif_running(dev))
		goto drop_skb;

	if (skb->pkt_type == PACKET_OTHERHOST)
		goto drop_skb;

	if (ieee802154_hdr_peek_addrs(skb, &hdr) < 0)
		goto drop_skb;

	/* check that it's our buffer */
	if (skb->data[0] == LOWPAN_DISPATCH_IPV6) {
		/* Pull off the 1-byte of 6lowpan header. */
		skb_pull(skb, 1);
		return lowpan_give_skb_to_device(skb, dev);
	} else {
		switch (skb->data[0] & 0xe0) {
		case LOWPAN_DISPATCH_IPHC:	/* ipv6 datagram */
			ret = iphc_decompress(skb, &hdr);
			if (ret < 0)
				goto drop_skb;

			return lowpan_give_skb_to_device(skb, dev);
		case LOWPAN_DISPATCH_FRAG1:	/* first fragment header */
			ret = lowpan_frag_rcv(skb, LOWPAN_DISPATCH_FRAG1);
			if (ret == 1) {
				ret = iphc_decompress(skb, &hdr);
				if (ret < 0)
					goto drop_skb;

				return lowpan_give_skb_to_device(skb, dev);
			} else if (ret == -1) {
				return NET_RX_DROP;
			} else {
				return NET_RX_SUCCESS;
			}
		case LOWPAN_DISPATCH_FRAGN:	/* next fragments headers */
			ret = lowpan_frag_rcv(skb, LOWPAN_DISPATCH_FRAGN);
			if (ret == 1) {
				ret = iphc_decompress(skb, &hdr);
				if (ret < 0)
					goto drop_skb;

				return lowpan_give_skb_to_device(skb, dev);
			} else if (ret == -1) {
				return NET_RX_DROP;
			} else {
				return NET_RX_SUCCESS;
			}
		default:
			break;
		}
	}

drop_skb:
	kfree_skb(skb);
drop:
	return NET_RX_DROP;
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
