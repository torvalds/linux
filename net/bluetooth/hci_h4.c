// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *  Bluetooth HCI H:4 packet reassembly
 *
 *  Copyright (C) 2000-2001  Qualcomm Incorporated
 *  Copyright (C) 2002-2003  Maxim Krasnyansky <maxk@qualcomm.com>
 *  Copyright (C) 2004-2005  Marcel Holtmann <marcel@holtmann.org>
 */

#include <linux/export.h>
#include <linux/skbuff.h>
#include <linux/unaligned.h>

#include <net/bluetooth/bluetooth.h>
#include <net/bluetooth/hci_core.h>
#include <net/bluetooth/hci_h4.h>

/* h4_recv_skb - Reassemble H:4 framed packets
 * @hdev: HCI device the packets are received on
 * @alignment: optional packet alignment, NULL or 0 means no alignment
 * @padding: optional padding state carried over between calls
 * @skb: partially received packet from a previous call, may be NULL or an
 *	 ERR_PTR returned by a previous call
 * @buffer: buffer holding the received data
 * @count: number of bytes in @buffer
 * @pkts: table of supported packet types
 * @pkts_count: number of entries in @pkts
 *
 * Returns the partially received packet to be passed to the next call, or an
 * ERR_PTR on error. The returned value can be fed back into this function as
 * is, but must be checked with IS_ERR() before being freed.
 */
struct sk_buff *h4_recv_skb(struct hci_dev *hdev, u8 *alignment, u8 *padding,
			    struct sk_buff *skb, const unsigned char *buffer,
			    int count, const struct h4_recv_pkt *pkts,
			    int pkts_count)
{
	u8 align = alignment && *alignment ? *alignment : 1;

	/* Check for error from previous call */
	if (IS_ERR(skb))
		skb = NULL;

	while (count) {
		int i, len;

		/* remove padding bytes from buffer */
		if (padding) {
			for (; (*padding) && count > 0; (*padding)--) {
				count--;
				buffer++;
			}
		}

		if (!count)
			break;

		if (!skb) {
			for (i = 0; i < pkts_count; i++) {
				if (buffer[0] != pkts[i].type)
					continue;

				skb = bt_skb_alloc(pkts[i].maxlen,
						   GFP_ATOMIC);
				if (!skb)
					return ERR_PTR(-ENOMEM);

				hci_skb_pkt_type(skb) = pkts[i].type;
				hci_skb_expect(skb) = pkts[i].hlen;
				break;
			}

			/* Check for invalid packet type */
			if (!skb)
				return ERR_PTR(-EILSEQ);

			count -= 1;
			buffer += 1;
		}

		len = min_t(uint, hci_skb_expect(skb) - skb->len, count);
		skb_put_data(skb, buffer, len);

		count -= len;
		buffer += len;

		/* Check for partial packet */
		if (skb->len < hci_skb_expect(skb))
			continue;

		for (i = 0; i < pkts_count; i++) {
			if (hci_skb_pkt_type(skb) == pkts[i].type)
				break;
		}

		if (i >= pkts_count) {
			kfree_skb(skb);
			return ERR_PTR(-EILSEQ);
		}

		if (skb->len == pkts[i].hlen) {
			u16 dlen;

			switch (pkts[i].lsize) {
			case 0:
				/* No variable data length */
				dlen = 0;
				break;
			case 1:
				/* Single octet variable length */
				dlen = skb->data[pkts[i].loff];
				hci_skb_expect(skb) += dlen;

				if (skb_tailroom(skb) < dlen) {
					kfree_skb(skb);
					return ERR_PTR(-EMSGSIZE);
				}
				break;
			case 2:
				/* Double octet variable length */
				dlen = get_unaligned_le16(skb->data +
							  pkts[i].loff);
				hci_skb_expect(skb) += dlen;

				if (skb_tailroom(skb) < dlen) {
					kfree_skb(skb);
					return ERR_PTR(-EMSGSIZE);
				}
				break;
			default:
				/* Unsupported variable length */
				kfree_skb(skb);
				return ERR_PTR(-EILSEQ);
			}

			if (!dlen) {
				if (padding) {
					*padding = (skb->len + 1) % align;
					*padding = (align - *padding) % align;
				}

				/* No more data, complete frame */
				pkts[i].recv(hdev, skb);
				skb = NULL;
			}
		} else {
			if (padding) {
				*padding = (skb->len + 1) % align;
				*padding = (align - *padding) % align;
			}

			/* Complete frame */
			pkts[i].recv(hdev, skb);
			skb = NULL;
		}
	}

	return skb;
}
EXPORT_SYMBOL_GPL(h4_recv_skb);
