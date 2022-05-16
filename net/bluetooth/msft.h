// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2020 Google Corporation
 */

#define MSFT_FEATURE_MASK_BREDR_RSSI_MONITOR		BIT(0)
#define MSFT_FEATURE_MASK_LE_CONN_RSSI_MONITOR		BIT(1)
#define MSFT_FEATURE_MASK_LE_ADV_RSSI_MONITOR		BIT(2)
#define MSFT_FEATURE_MASK_LE_ADV_MONITOR		BIT(3)
#define MSFT_FEATURE_MASK_CURVE_VALIDITY		BIT(4)
#define MSFT_FEATURE_MASK_CONCURRENT_ADV_MONITOR	BIT(5)

#if IS_ENABLED(CONFIG_BT_MSFTEXT)

void msft_do_open(struct hci_dev *hdev);
void msft_do_close(struct hci_dev *hdev);
void msft_vendor_evt(struct hci_dev *hdev, struct sk_buff *skb);
__u64 msft_get_features(struct hci_dev *hdev);

#else

static inline void msft_do_open(struct hci_dev *hdev) {}
static inline void msft_do_close(struct hci_dev *hdev) {}
static inline void msft_vendor_evt(struct hci_dev *hdev, struct sk_buff *skb) {}
static inline __u64 msft_get_features(struct hci_dev *hdev) { return 0; }

#endif
