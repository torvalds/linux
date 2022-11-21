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

bool msft_monitor_supported(struct hci_dev *hdev);
void msft_register(struct hci_dev *hdev);
void msft_unregister(struct hci_dev *hdev);
void msft_do_open(struct hci_dev *hdev);
void msft_do_close(struct hci_dev *hdev);
void msft_vendor_evt(struct hci_dev *hdev, void *data, struct sk_buff *skb);
__u64 msft_get_features(struct hci_dev *hdev);
int msft_add_monitor_pattern(struct hci_dev *hdev, struct adv_monitor *monitor);
int msft_remove_monitor(struct hci_dev *hdev, struct adv_monitor *monitor,
			u16 handle);
void msft_req_add_set_filter_enable(struct hci_request *req, bool enable);
int msft_set_filter_enable(struct hci_dev *hdev, bool enable);
int msft_suspend_sync(struct hci_dev *hdev);
int msft_resume_sync(struct hci_dev *hdev);
bool msft_curve_validity(struct hci_dev *hdev);

#else

static inline bool msft_monitor_supported(struct hci_dev *hdev)
{
	return false;
}

static inline void msft_register(struct hci_dev *hdev) {}
static inline void msft_unregister(struct hci_dev *hdev) {}
static inline void msft_do_open(struct hci_dev *hdev) {}
static inline void msft_do_close(struct hci_dev *hdev) {}
static inline void msft_vendor_evt(struct hci_dev *hdev, void *data,
				   struct sk_buff *skb) {}
static inline __u64 msft_get_features(struct hci_dev *hdev) { return 0; }
static inline int msft_add_monitor_pattern(struct hci_dev *hdev,
					   struct adv_monitor *monitor)
{
	return -EOPNOTSUPP;
}

static inline int msft_remove_monitor(struct hci_dev *hdev,
				      struct adv_monitor *monitor,
				      u16 handle)
{
	return -EOPNOTSUPP;
}

static inline void msft_req_add_set_filter_enable(struct hci_request *req,
						  bool enable) {}
static inline int msft_set_filter_enable(struct hci_dev *hdev, bool enable)
{
	return -EOPNOTSUPP;
}

static inline int msft_suspend_sync(struct hci_dev *hdev)
{
	return -EOPNOTSUPP;
}

static inline int msft_resume_sync(struct hci_dev *hdev)
{
	return -EOPNOTSUPP;
}

static inline bool msft_curve_validity(struct hci_dev *hdev)
{
	return false;
}

#endif
