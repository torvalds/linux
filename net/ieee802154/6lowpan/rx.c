// SPDX-License-Identifier: GPL-2.0-only

#include <linux/if_arp.h>

#include <net/6lowpan.h>
#include <net/mac802154.h>
#include <net/ieee802154_netdev.h>

#include "6lowpan_i.h"

#define LOWPAN_DISPATCH_FIRST		0xc0
#define LOWPAN_DISPATCH_FRAG_MASK	0xf8

#define LOWPAN_DISPATCH_NALP		0x00
#define LOWPAN_DISPATCH_ESC		0x40
#define LOWPAN_DISPATCH_HC1		0x42
#define LOWPAN_DISPATCH_DFF		0x43
#define LOWPAN_DISPATCH_BC0		0x50
#define LOWPAN_DISPATCH_MESH		0x80

static int lowpan_give_skb_to_device(struct sk_buff *skb)
{
	skb->protocol = htons(ETH_P_IPV6);
	skb->dev->stats.rx_packets++;
	skb->dev->stats.rx_bytes += skb->len;

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
	struct ieee802154_hdr hdr;

	if (ieee802154_hdr_peek_addrs(skb, &hdr) < 0)
		return -EINVAL;

	return lowpan_header_decompress(skb, skb->dev, &hdr.dest, &hdr.source);
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

static inline bool lowpan_is_esc(u8 dispatch)
{
	return dispatch == LOWPAN_DISPATCH_ESC;
}

static lowpan_rx_result lowpan_rx_h_esc(struct sk_buff *skb)
{
	if (!lowpan_is_esc(*skb_network_header(skb)))
		return RX_CONTINUE;

	net_warn_ratelimited("%s: %s\n", skb->dev->name,
			     "6LoWPAN ESC not supported\n");

	return RX_DROP_UNUSABLE;
}

static inline bool lowpan_is_hc1(u8 dispatch)
{
	return dispatch == LOWPAN_DISPATCH_HC1;
}

static lowpan_rx_result lowpan_rx_h_hc1(struct sk_buff *skb)
{
	if (!lowpan_is_hc1(*skb_network_header(skb)))
		return RX_CONTINUE;

	net_warn_ratelimited("%s: %s\n", skb->dev->name,
			     "6LoWPAN HC1 not supported\n");

	return RX_DROP_UNUSABLE;
}

static inline bool lowpan_is_dff(u8 dispatch)
{
	return dispatch == LOWPAN_DISPATCH_DFF;
}

static lowpan_rx_result lowpan_rx_h_dff(struct sk_buff *skb)
{
	if (!lowpan_is_dff(*skb_network_header(skb)))
		return RX_CONTINUE;

	net_warn_ratelimited("%s: %s\n", skb->dev->name,
			     "6LoWPAN DFF not supported\n");

	return RX_DROP_UNUSABLE;
}

static inline bool lowpan_is_bc0(u8 dispatch)
{
	return dispatch == LOWPAN_DISPATCH_BC0;
}

static lowpan_rx_result lowpan_rx_h_bc0(struct sk_buff *skb)
{
	if (!lowpan_is_bc0(*skb_network_header(skb)))
		return RX_CONTINUE;

	net_warn_ratelimited("%s: %s\n", skb->dev->name,
			     "6LoWPAN BC0 not supported\n");

	return RX_DROP_UNUSABLE;
}

static inline bool lowpan_is_mesh(u8 dispatch)
{
	return (dispatch & LOWPAN_DISPATCH_FIRST) == LOWPAN_DISPATCH_MESH;
}

static lowpan_rx_result lowpan_rx_h_mesh(struct sk_buff *skb)
{
	if (!lowpan_is_mesh(*skb_network_header(skb)))
		return RX_CONTINUE;

	net_warn_ratelimited("%s: %s\n", skb->dev->name,
			     "6LoWPAN MESH not supported\n");

	return RX_DROP_UNUSABLE;
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
	CALL_RXH(lowpan_rx_h_esc);
	CALL_RXH(lowpan_rx_h_hc1);
	CALL_RXH(lowpan_rx_h_dff);
	CALL_RXH(lowpan_rx_h_bc0);
	CALL_RXH(lowpan_rx_h_mesh);

rxh_next:
	return lowpan_rx_handlers_result(skb, res);
#undef CALL_RXH
}

static inline bool lowpan_is_nalp(u8 dispatch)
{
	return (dispatch & LOWPAN_DISPATCH_FIRST) == LOWPAN_DISPATCH_NALP;
}

/* Lookup for reserved dispatch values at:
 * https://www.iana.org/assignments/_6lowpan-parameters/_6lowpan-parameters.xhtml#_6lowpan-parameters-1
 *
 * Last Updated: 2015-01-22
 */
static inline bool lowpan_is_reserved(u8 dispatch)
{
	return ((dispatch >= 0x44 && dispatch <= 0x4F) ||
		(dispatch >= 0x51 && dispatch <= 0x5F) ||
		(dispatch >= 0xc8 && dispatch <= 0xdf) ||
		(dispatch >= 0xe8 && dispatch <= 0xff));
}

/* lowpan_rx_h_check checks on generic 6LoWPAN requirements
 * in MAC and 6LoWPAN header.
 *
 * Don't manipulate the skb here, it could be shared buffer.
 */
static inline bool lowpan_rx_h_check(struct sk_buff *skb)
{
	__le16 fc = ieee802154_get_fc_from_skb(skb);

	/* check on ieee802154 conform 6LoWPAN header */
	if (!ieee802154_is_data(fc) ||
	    !ieee802154_skb_is_intra_pan_addressing(fc, skb))
		return false;

	/* check if we can dereference the dispatch */
	if (unlikely(!skb->len))
		return false;

	if (lowpan_is_nalp(*skb_network_header(skb)) ||
	    lowpan_is_reserved(*skb_network_header(skb)))
		return false;

	return true;
}

static int lowpan_rcv(struct sk_buff *skb, struct net_device *wdev,
		      struct packet_type *pt, struct net_device *orig_wdev)
{
	struct net_device *ldev;

	if (wdev->type != ARPHRD_IEEE802154 ||
	    skb->pkt_type == PACKET_OTHERHOST ||
	    !lowpan_rx_h_check(skb))
		goto drop;

	ldev = wdev->ieee802154_ptr->lowpan_dev;
	if (!ldev || !netif_running(ldev))
		goto drop;

	/* Replacing skb->dev and followed rx handlers will manipulate skb. */
	skb = skb_share_check(skb, GFP_ATOMIC);
	if (!skb)
		goto out;
	skb->dev = ldev;

	/* When receive frag1 it's likely that we manipulate the buffer.
	 * When recevie iphc we manipulate the data buffer. So we need
	 * to unshare the buffer.
	 */
	if (lowpan_is_frag1(*skb_network_header(skb)) ||
	    lowpan_is_iphc(*skb_network_header(skb))) {
		skb = skb_unshare(skb, GFP_ATOMIC);
		if (!skb)
			goto out;
	}

	return lowpan_invoke_rx_handlers(skb);

drop:
	kfree_skb(skb);
out:
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
