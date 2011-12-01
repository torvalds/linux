/*
 *  The NFC Controller Interface is the communication protocol between an
 *  NFC Controller (NFCC) and a Device Host (DH).
 *
 *  Copyright (C) 2011 Texas Instruments, Inc.
 *
 *  Written by Ilan Elias <ilane@ti.com>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License version 2
 *  as published by the Free Software Foundation
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/types.h>
#include <linux/interrupt.h>
#include <linux/wait.h>
#include <linux/bitops.h>
#include <linux/skbuff.h>

#include "../nfc.h"
#include <net/nfc/nci.h>
#include <net/nfc/nci_core.h>
#include <linux/nfc.h>

/* Complete data exchange transaction and forward skb to nfc core */
void nci_data_exchange_complete(struct nci_dev *ndev,
				struct sk_buff *skb,
				int err)
{
	data_exchange_cb_t cb = ndev->data_exchange_cb;
	void *cb_context = ndev->data_exchange_cb_context;

	pr_debug("len %d, err %d\n", skb ? skb->len : 0, err);

	if (cb) {
		ndev->data_exchange_cb = NULL;
		ndev->data_exchange_cb_context = 0;

		/* forward skb to nfc core */
		cb(cb_context, skb, err);
	} else if (skb) {
		pr_err("no rx callback, dropping rx data...\n");

		/* no waiting callback, free skb */
		kfree_skb(skb);
	}

	clear_bit(NCI_DATA_EXCHANGE, &ndev->flags);
}

/* ----------------- NCI TX Data ----------------- */

static inline void nci_push_data_hdr(struct nci_dev *ndev,
					__u8 conn_id,
					struct sk_buff *skb,
					__u8 pbf)
{
	struct nci_data_hdr *hdr;
	int plen = skb->len;

	hdr = (struct nci_data_hdr *) skb_push(skb, NCI_DATA_HDR_SIZE);
	hdr->conn_id = conn_id;
	hdr->rfu = 0;
	hdr->plen = plen;

	nci_mt_set((__u8 *)hdr, NCI_MT_DATA_PKT);
	nci_pbf_set((__u8 *)hdr, pbf);

	skb->dev = (void *) ndev;
}

static int nci_queue_tx_data_frags(struct nci_dev *ndev,
					__u8 conn_id,
					struct sk_buff *skb) {
	int total_len = skb->len;
	unsigned char *data = skb->data;
	unsigned long flags;
	struct sk_buff_head frags_q;
	struct sk_buff *skb_frag;
	int frag_len;
	int rc = 0;

	pr_debug("conn_id 0x%x, total_len %d\n", conn_id, total_len);

	__skb_queue_head_init(&frags_q);

	while (total_len) {
		frag_len =
			min_t(int, total_len, ndev->max_data_pkt_payload_size);

		skb_frag = nci_skb_alloc(ndev,
					(NCI_DATA_HDR_SIZE + frag_len),
					GFP_KERNEL);
		if (skb_frag == NULL) {
			rc = -ENOMEM;
			goto free_exit;
		}
		skb_reserve(skb_frag, NCI_DATA_HDR_SIZE);

		/* first, copy the data */
		memcpy(skb_put(skb_frag, frag_len), data, frag_len);

		/* second, set the header */
		nci_push_data_hdr(ndev, conn_id, skb_frag,
		((total_len == frag_len) ? (NCI_PBF_LAST) : (NCI_PBF_CONT)));

		__skb_queue_tail(&frags_q, skb_frag);

		data += frag_len;
		total_len -= frag_len;

		pr_debug("frag_len %d, remaining total_len %d\n",
			 frag_len, total_len);
	}

	/* queue all fragments atomically */
	spin_lock_irqsave(&ndev->tx_q.lock, flags);

	while ((skb_frag = __skb_dequeue(&frags_q)) != NULL)
		__skb_queue_tail(&ndev->tx_q, skb_frag);

	spin_unlock_irqrestore(&ndev->tx_q.lock, flags);

	/* free the original skb */
	kfree_skb(skb);

	goto exit;

free_exit:
	while ((skb_frag = __skb_dequeue(&frags_q)) != NULL)
		kfree_skb(skb_frag);

exit:
	return rc;
}

/* Send NCI data */
int nci_send_data(struct nci_dev *ndev, __u8 conn_id, struct sk_buff *skb)
{
	int rc = 0;

	pr_debug("conn_id 0x%x, plen %d\n", conn_id, skb->len);

	/* check if the packet need to be fragmented */
	if (skb->len <= ndev->max_data_pkt_payload_size) {
		/* no need to fragment packet */
		nci_push_data_hdr(ndev, conn_id, skb, NCI_PBF_LAST);

		skb_queue_tail(&ndev->tx_q, skb);
	} else {
		/* fragment packet and queue the fragments */
		rc = nci_queue_tx_data_frags(ndev, conn_id, skb);
		if (rc) {
			pr_err("failed to fragment tx data packet\n");
			goto free_exit;
		}
	}

	queue_work(ndev->tx_wq, &ndev->tx_work);

	goto exit;

free_exit:
	kfree_skb(skb);

exit:
	return rc;
}

/* ----------------- NCI RX Data ----------------- */

static void nci_add_rx_data_frag(struct nci_dev *ndev,
				struct sk_buff *skb,
				__u8 pbf)
{
	int reassembly_len;
	int err = 0;

	if (ndev->rx_data_reassembly) {
		reassembly_len = ndev->rx_data_reassembly->len;

		/* first, make enough room for the already accumulated data */
		if (skb_cow_head(skb, reassembly_len)) {
			pr_err("error adding room for accumulated rx data\n");

			kfree_skb(skb);
			skb = 0;

			kfree_skb(ndev->rx_data_reassembly);
			ndev->rx_data_reassembly = 0;

			err = -ENOMEM;
			goto exit;
		}

		/* second, combine the two fragments */
		memcpy(skb_push(skb, reassembly_len),
				ndev->rx_data_reassembly->data,
				reassembly_len);

		/* third, free old reassembly */
		kfree_skb(ndev->rx_data_reassembly);
		ndev->rx_data_reassembly = 0;
	}

	if (pbf == NCI_PBF_CONT) {
		/* need to wait for next fragment, store skb and exit */
		ndev->rx_data_reassembly = skb;
		return;
	}

exit:
	nci_data_exchange_complete(ndev, skb, err);
}

/* Rx Data packet */
void nci_rx_data_packet(struct nci_dev *ndev, struct sk_buff *skb)
{
	__u8 pbf = nci_pbf(skb->data);

	pr_debug("len %d\n", skb->len);

	pr_debug("NCI RX: MT=data, PBF=%d, conn_id=%d, plen=%d\n",
		 nci_pbf(skb->data),
		 nci_conn_id(skb->data),
		 nci_plen(skb->data));

	/* strip the nci data header */
	skb_pull(skb, NCI_DATA_HDR_SIZE);

	if (ndev->target_active_prot == NFC_PROTO_MIFARE) {
		/* frame I/F => remove the status byte */
		pr_debug("NFC_PROTO_MIFARE => remove the status byte\n");
		skb_trim(skb, (skb->len - 1));
	}

	nci_add_rx_data_frag(ndev, skb, pbf);
}
