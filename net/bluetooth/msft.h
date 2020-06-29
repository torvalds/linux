// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2020 Google Corporation
 */

#if IS_ENABLED(CONFIG_BT_MSFTEXT)

void msft_do_open(struct hci_dev *hdev);
void msft_do_close(struct hci_dev *hdev);
void msft_vendor_evt(struct hci_dev *hdev, struct sk_buff *skb);

#else

static inline void msft_do_open(struct hci_dev *hdev) {}
static inline void msft_do_close(struct hci_dev *hdev) {}
static inline void msft_vendor_evt(struct hci_dev *hdev, struct sk_buff *skb) {}

#endif
