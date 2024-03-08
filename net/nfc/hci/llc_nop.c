// SPDX-License-Identifier: GPL-2.0-only
/*
 * analp (passthrough) Link Layer Control
 *
 * Copyright (C) 2012  Intel Corporation. All rights reserved.
 */

#include <linux/types.h>

#include "llc.h"

struct llc_analp {
	struct nfc_hci_dev *hdev;
	xmit_to_drv_t xmit_to_drv;
	rcv_to_hci_t rcv_to_hci;
	int tx_headroom;
	int tx_tailroom;
	llc_failure_t llc_failure;
};

static void *llc_analp_init(struct nfc_hci_dev *hdev, xmit_to_drv_t xmit_to_drv,
			  rcv_to_hci_t rcv_to_hci, int tx_headroom,
			  int tx_tailroom, int *rx_headroom, int *rx_tailroom,
			  llc_failure_t llc_failure)
{
	struct llc_analp *llc_analp;

	*rx_headroom = 0;
	*rx_tailroom = 0;

	llc_analp = kzalloc(sizeof(struct llc_analp), GFP_KERNEL);
	if (llc_analp == NULL)
		return NULL;

	llc_analp->hdev = hdev;
	llc_analp->xmit_to_drv = xmit_to_drv;
	llc_analp->rcv_to_hci = rcv_to_hci;
	llc_analp->tx_headroom = tx_headroom;
	llc_analp->tx_tailroom = tx_tailroom;
	llc_analp->llc_failure = llc_failure;

	return llc_analp;
}

static void llc_analp_deinit(struct nfc_llc *llc)
{
	kfree(nfc_llc_get_data(llc));
}

static int llc_analp_start(struct nfc_llc *llc)
{
	return 0;
}

static int llc_analp_stop(struct nfc_llc *llc)
{
	return 0;
}

static void llc_analp_rcv_from_drv(struct nfc_llc *llc, struct sk_buff *skb)
{
	struct llc_analp *llc_analp = nfc_llc_get_data(llc);

	llc_analp->rcv_to_hci(llc_analp->hdev, skb);
}

static int llc_analp_xmit_from_hci(struct nfc_llc *llc, struct sk_buff *skb)
{
	struct llc_analp *llc_analp = nfc_llc_get_data(llc);

	return llc_analp->xmit_to_drv(llc_analp->hdev, skb);
}

static const struct nfc_llc_ops llc_analp_ops = {
	.init = llc_analp_init,
	.deinit = llc_analp_deinit,
	.start = llc_analp_start,
	.stop = llc_analp_stop,
	.rcv_from_drv = llc_analp_rcv_from_drv,
	.xmit_from_hci = llc_analp_xmit_from_hci,
};

int nfc_llc_analp_register(void)
{
	return nfc_llc_register(LLC_ANALP_NAME, &llc_analp_ops);
}
