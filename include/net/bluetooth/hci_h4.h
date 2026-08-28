/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 *  Bluetooth HCI H:4 packet reassembly
 *
 *  Copyright (C) 2000-2001  Qualcomm Incorporated
 *  Copyright (C) 2002-2003  Maxim Krasnyansky <maxk@qualcomm.com>
 *  Copyright (C) 2004-2005  Marcel Holtmann <marcel@holtmann.org>
 */

#ifndef __HCI_H4_H
#define __HCI_H4_H

#include <linux/skbuff.h>
#include <linux/types.h>

struct hci_dev;

struct h4_recv_pkt {
	u8  type;	/* Packet type */
	u8  hlen;	/* Header length */
	u8  loff;	/* Data length offset in header */
	u8  lsize;	/* Data length field size */
	u16 maxlen;	/* Max overall packet length */
	int (*recv)(struct hci_dev *hdev, struct sk_buff *skb);
};

#define H4_RECV_ACL \
	.type = HCI_ACLDATA_PKT, \
	.hlen = HCI_ACL_HDR_SIZE, \
	.loff = 2, \
	.lsize = 2, \
	.maxlen = HCI_MAX_FRAME_SIZE \

#define H4_RECV_SCO \
	.type = HCI_SCODATA_PKT, \
	.hlen = HCI_SCO_HDR_SIZE, \
	.loff = 2, \
	.lsize = 1, \
	.maxlen = HCI_MAX_SCO_SIZE

#define H4_RECV_EVENT \
	.type = HCI_EVENT_PKT, \
	.hlen = HCI_EVENT_HDR_SIZE, \
	.loff = 1, \
	.lsize = 1, \
	.maxlen = HCI_MAX_EVENT_SIZE

#define H4_RECV_ISO \
	.type = HCI_ISODATA_PKT, \
	.hlen = HCI_ISO_HDR_SIZE, \
	.loff = 2, \
	.lsize = 2, \
	.maxlen = HCI_MAX_FRAME_SIZE \

struct sk_buff *h4_recv_skb(struct hci_dev *hdev, u8 *alignment, u8 *padding,
			    struct sk_buff *skb, const unsigned char *buffer,
			    int count, const struct h4_recv_pkt *pkts,
			    int pkts_count);

#endif /* __HCI_H4_H */
