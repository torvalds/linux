/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Link Layer Control manager public interface
 *
 * Copyright (C) 2012  Intel Corporation. All rights reserved.
 */

#ifndef __NFC_LLC_H_
#define __NFC_LLC_H_

#include <net/nfc/hci.h>
#include <linux/skbuff.h>

#define LLC_NOP_NAME "nop"
#define LLC_SHDLC_NAME "shdlc"

typedef void (*rcv_to_hci_t) (struct nfc_hci_dev *hdev, struct sk_buff *skb);
typedef int (*xmit_to_drv_t) (struct nfc_hci_dev *hdev, struct sk_buff *skb);
typedef void (*llc_failure_t) (struct nfc_hci_dev *hdev, int err);

struct nfc_llc;

struct nfc_llc *nfc_llc_allocate(const char *name, struct nfc_hci_dev *hdev,
				 xmit_to_drv_t xmit_to_drv,
				 rcv_to_hci_t rcv_to_hci, int tx_headroom,
				 int tx_tailroom, llc_failure_t llc_failure);
void nfc_llc_free(struct nfc_llc *llc);

int nfc_llc_start(struct nfc_llc *llc);
int nfc_llc_stop(struct nfc_llc *llc);
void nfc_llc_rcv_from_drv(struct nfc_llc *llc, struct sk_buff *skb);
int nfc_llc_xmit_from_hci(struct nfc_llc *llc, struct sk_buff *skb);

int nfc_llc_init(void);
void nfc_llc_exit(void);

#endif /* __NFC_LLC_H_ */
