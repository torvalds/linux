/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Link Layer Control manager
 *
 * Copyright (C) 2012  Intel Corporation. All rights reserved.
 */

#ifndef __LOCAL_LLC_H_
#define __LOCAL_LLC_H_

#include <net/nfc/hci.h>
#include <net/nfc/llc.h>
#include <linux/skbuff.h>

struct nfc_llc_ops {
	void *(*init) (struct nfc_hci_dev *hdev, xmit_to_drv_t xmit_to_drv,
		       rcv_to_hci_t rcv_to_hci, int tx_headroom,
		       int tx_tailroom, int *rx_headroom, int *rx_tailroom,
		       llc_failure_t llc_failure);
	void (*deinit) (struct nfc_llc *llc);
	int (*start) (struct nfc_llc *llc);
	int (*stop) (struct nfc_llc *llc);
	void (*rcv_from_drv) (struct nfc_llc *llc, struct sk_buff *skb);
	int (*xmit_from_hci) (struct nfc_llc *llc, struct sk_buff *skb);
};

struct nfc_llc_engine {
	const char *name;
	struct nfc_llc_ops *ops;
	struct list_head entry;
};

struct nfc_llc {
	void *data;
	struct nfc_llc_ops *ops;
	int rx_headroom;
	int rx_tailroom;
};

void *nfc_llc_get_data(struct nfc_llc *llc);

int nfc_llc_register(const char *name, struct nfc_llc_ops *ops);
void nfc_llc_unregister(const char *name);

int nfc_llc_nop_register(void);

#if defined(CONFIG_NFC_SHDLC)
int nfc_llc_shdlc_register(void);
#else
static inline int nfc_llc_shdlc_register(void)
{
	return 0;
}
#endif

#endif /* __LOCAL_LLC_H_ */
