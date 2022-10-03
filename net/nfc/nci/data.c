// SPDX-License-Identifier: GPL-2.0-only
/*
 *  The NFC Controller Interface is the communication protocol between an
 *  NFC Controller (NFCC) and a Device Host (DH).
 *
 *  Copyright (C) 2011 Texas Instruments, Inc.
 *  Copyright (C) 2014 Marvell International Ltd.
 *
 *  Written by Ilan Elias <ilane@ti.com>
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": %s: " fmt, __func__

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
void nci_data_exchange_complete(struct nci_dev *ndev, struct sk_buff *skb,
				__u8 conn_id, int err)
{
	const struct nci_conn_info *conn_info;
	data_exchange_cb_t cb;
	void *cb_context;

	conn_info = nci_get_conn_info_by_conn_id(ndev, conn_id);
	if (!conn_info) {
		kfree_skb(skb);
		goto exit;
	}

	cb = conn_info->data_exchange_cb;
	cb_context = conn_info->data_exchange_cb_context;

	pr_debug("len %d, err %d\n", skb ? skb->len : 0, err);

	/* data exchange is complete, stop the data timer */
	del_timer_sync(&ndev->data_timer);
	clear_bit(NCI_DATA_EXCHANGE_TO, &ndev->flags);

	if (cb) {
		/* forward skb to nfc core */
		cb(cb_context, skb, err);
	} else if (skb) {
		pr_err("no rx callback, dropping rx data...\n");

		/* no waiting callback, free skb */
		kfree_skb(skb);
	}

exit:
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

	hdr = skb_push(skb, NCI_DATA_HDR_SIZE);
	hdr->conn_id = conn_id;
	hdr->rfu = 0;
	hdr->plen = plen;

	nci_mt_set((__u8 *)hdr, NCI_MT_DATA_PKT);
	nci_pbf_set((__u8 *)hdr, pbf);
}

int nci_conn_max_data_pkt_payload_size(struct nci_dev *ndev, __u8 conn_id)
{
	const struct nci_conn_info *conn_info;

	conn_info = nci_get_conn_info_by_conn_id(ndev, conn_id);
	if (!conn_info)
		return -EPROTO;

	return conn_info->max_pkt_payload_len;
}
EXPORT_SYMBOL(nci_conn_max_data_pkt_payload_size);

static int nci_queue_tx_data_frags(struct nci_dev *ndev,
				   __u8 conn_id,
				   struct sk_buff *skb) {
	const struct nci_conn_info *conn_info;
	int total_len = skb->len;
	const unsigned char *data = skb->data;
	unsigned long flags;
	struct sk_buff_head frags_q;
	struct sk_buff *skb_frag;
	int frag_len;
	int rc = 0;

	pr_debug("conn_id 0x%x, total_len %d\n", conn_id, total_len);

	conn_info = nci_get_conn_info_by_conn_id(ndev, conn_id);
	if (!conn_info) {
		rc = -EPROTO;
		goto exit;
	}

	__skb_queue_head_init(&frags_q);

	while (total_len) {
		frag_len =
			min_t(int, total_len, conn_info->max_pkt_payload_len);

		skb_frag = nci_skb_alloc(ndev,
					 (NCI_DATA_HDR_SIZE + frag_len),
					 GFP_ATOMIC);
		if (skb_frag == NULL) {
			rc = -ENOMEM;
			goto free_exit;
		}
		skb_reserve(skb_frag, NCI_DATA_HDR_SIZE);

		/* first, copy the data */
		skb_put_data(skb_frag, data, frag_len);

		/* second, set the header */
		nci_push_data_hdr(ndev, conn_id, skb_frag,
				  ((total_len == frag_len) ?
				   (NCI_PBF_LAST) : (NCI_PBF_CONT)));

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
	const struct nci_conn_info *conn_info;
	int rc = 0;

	pr_debug("conn_id 0x%x, plen %d\n", conn_id, skb->len);

	conn_info = nci_get_conn_info_by_conn_id(ndev, conn_id);
	if (!conn_info) {
		rc = -EPROTO;
		goto free_exit;
	}

	/* check if the packet need to be fragmented */
	if (skb->len <= conn_info->max_pkt_payload_len) {
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

	ndev->cur_conn_id = conn_id;
	queue_work(ndev->tx_wq, &ndev->tx_work);

	goto exit;

free_exit:
	kfree_skb(skb);

exit:
	return rc;
}
EXPORT_SYMBOL(nci_send_data);

/* ----------------- NCI RX Data ----------------- */

static void nci_add_rx_data_frag(struct nci_dev *ndev,
				 struct sk_buff *skb,
				 __u8 pbf, __u8 conn_id, __u8 status)
{
	int reassembly_len;
	int err = 0;

	if (status) {
		err = status;
		goto exit;
	}

	if (ndev->rx_data_reassembly) {
		reassembly_len = ndev->rx_data_reassembly->len;

		/* first, make enough room for the already accumulated data */
		if (skb_cow_head(skb, reassembly_len)) {
			pr_err("error adding room for accumulated rx data\n");

			kfree_skb(skb);
			skb = NULL;

			kfree_skb(ndev->rx_data_reassembly);
			ndev->rx_data_reassembly = NULL;

			err = -ENOMEM;
			goto exit;
		}

		/* second, combine the two fragments */
		memcpy(skb_push(skb, reassembly_len),
		       ndev->rx_data_reassembly->data,
		       reassembly_len);

		/* third, free old reassembly */
		kfree_skb(ndev->rx_data_reassembly);
		ndev->rx_data_reassembly = NULL;
	}

	if (pbf == NCI_PBF_CONT) {
		/* need to wait for next fragment, store skb and exit */
		ndev->rx_data_reassembly = skb;
		return;
	}

exit:
	if (ndev->nfc_dev->rf_mode == NFC_RF_TARGET) {
		/* Data received in Target mode, forward to nfc core */
		err = nfc_tm_data_received(ndev->nfc_dev, skb);
		if (err)
			pr_err("unable to handle received data\n");
	} else {
		nci_data_exchange_complete(ndev, skb, conn_id, err);
	}
}

/* Rx Data packet */
void nci_rx_data_packet(struct nci_dev *ndev, struct sk_buff *skb)
{
	__u8 pbf = nci_pbf(skb->data);
	__u8 status = 0;
	__u8 conn_id = nci_conn_id(skb->data);
	const struct nci_conn_info *conn_info;

	pr_debug("len %d\n", skb->len);

	pr_debug("NCI RX: MT=data, PBF=%d, conn_id=%d, plen=%d\n",
		 nci_pbf(skb->data),
		 nci_conn_id(skb->data),
		 nci_plen(skb->data));

	conn_info = nci_get_conn_info_by_conn_id(ndev, nci_conn_id(skb->data));
	if (!conn_info)
		return;

	/* strip the nci data header */
	skb_pull(skb, NCI_DATA_HDR_SIZE);

	if (ndev->target_active_prot == NFC_PROTO_MIFARE ||
	    ndev->target_active_prot == NFC_PROTO_JEWEL ||
	    ndev->target_active_prot == NFC_PROTO_FELICA ||
	    ndev->target_active_prot == NFC_PROTO_ISO15693) {
		/* frame I/F => remove the status byte */
		pr_debug("frame I/F => remove the status byte\n");
		status = skb->data[skb->len - 1];
		skb_trim(skb, (skb->len - 1));
	}

	nci_add_rx_data_frag(ndev, skb, pbf, conn_id, nci_to_errno(status));
}
