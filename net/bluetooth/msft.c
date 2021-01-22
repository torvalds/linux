// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2020 Google Corporation
 */

#include <net/bluetooth/bluetooth.h>
#include <net/bluetooth/hci_core.h>
#include <net/bluetooth/mgmt.h>

#include "hci_request.h"
#include "mgmt_util.h"
#include "msft.h"

#define MSFT_RSSI_THRESHOLD_VALUE_MIN		-127
#define MSFT_RSSI_THRESHOLD_VALUE_MAX		20
#define MSFT_RSSI_LOW_TIMEOUT_MAX		0x3C

#define MSFT_OP_READ_SUPPORTED_FEATURES		0x00
struct msft_cp_read_supported_features {
	__u8   sub_opcode;
} __packed;

struct msft_rp_read_supported_features {
	__u8   status;
	__u8   sub_opcode;
	__le64 features;
	__u8   evt_prefix_len;
	__u8   evt_prefix[];
} __packed;

#define MSFT_OP_LE_MONITOR_ADVERTISEMENT	0x03
#define MSFT_MONITOR_ADVERTISEMENT_TYPE_PATTERN	0x01
struct msft_le_monitor_advertisement_pattern {
	__u8 length;
	__u8 data_type;
	__u8 start_byte;
	__u8 pattern[0];
};

struct msft_le_monitor_advertisement_pattern_data {
	__u8 count;
	__u8 data[0];
};

struct msft_cp_le_monitor_advertisement {
	__u8 sub_opcode;
	__s8 rssi_high;
	__s8 rssi_low;
	__u8 rssi_low_interval;
	__u8 rssi_sampling_period;
	__u8 cond_type;
	__u8 data[0];
} __packed;

struct msft_rp_le_monitor_advertisement {
	__u8 status;
	__u8 sub_opcode;
	__u8 handle;
} __packed;

struct msft_monitor_advertisement_handle_data {
	__u8  msft_handle;
	__u16 mgmt_handle;
	struct list_head list;
};

struct msft_data {
	__u64 features;
	__u8  evt_prefix_len;
	__u8  *evt_prefix;
	struct list_head handle_map;
	__u16 pending_add_handle;
};

bool msft_monitor_supported(struct hci_dev *hdev)
{
	return !!(msft_get_features(hdev) & MSFT_FEATURE_MASK_LE_ADV_MONITOR);
}

static bool read_supported_features(struct hci_dev *hdev,
				    struct msft_data *msft)
{
	struct msft_cp_read_supported_features cp;
	struct msft_rp_read_supported_features *rp;
	struct sk_buff *skb;

	cp.sub_opcode = MSFT_OP_READ_SUPPORTED_FEATURES;

	skb = __hci_cmd_sync(hdev, hdev->msft_opcode, sizeof(cp), &cp,
			     HCI_CMD_TIMEOUT);
	if (IS_ERR(skb)) {
		bt_dev_err(hdev, "Failed to read MSFT supported features (%ld)",
			   PTR_ERR(skb));
		return false;
	}

	if (skb->len < sizeof(*rp)) {
		bt_dev_err(hdev, "MSFT supported features length mismatch");
		goto failed;
	}

	rp = (struct msft_rp_read_supported_features *)skb->data;

	if (rp->sub_opcode != MSFT_OP_READ_SUPPORTED_FEATURES)
		goto failed;

	if (rp->evt_prefix_len > 0) {
		msft->evt_prefix = kmemdup(rp->evt_prefix, rp->evt_prefix_len,
					   GFP_KERNEL);
		if (!msft->evt_prefix)
			goto failed;
	}

	msft->evt_prefix_len = rp->evt_prefix_len;
	msft->features = __le64_to_cpu(rp->features);

	kfree_skb(skb);
	return true;

failed:
	kfree_skb(skb);
	return false;
}

void msft_do_open(struct hci_dev *hdev)
{
	struct msft_data *msft;

	if (hdev->msft_opcode == HCI_OP_NOP)
		return;

	bt_dev_dbg(hdev, "Initialize MSFT extension");

	msft = kzalloc(sizeof(*msft), GFP_KERNEL);
	if (!msft)
		return;

	if (!read_supported_features(hdev, msft)) {
		kfree(msft);
		return;
	}

	INIT_LIST_HEAD(&msft->handle_map);
	hdev->msft_data = msft;
}

void msft_do_close(struct hci_dev *hdev)
{
	struct msft_data *msft = hdev->msft_data;
	struct msft_monitor_advertisement_handle_data *handle_data, *tmp;

	if (!msft)
		return;

	bt_dev_dbg(hdev, "Cleanup of MSFT extension");

	hdev->msft_data = NULL;

	list_for_each_entry_safe(handle_data, tmp, &msft->handle_map, list) {
		list_del(&handle_data->list);
		kfree(handle_data);
	}

	kfree(msft->evt_prefix);
	kfree(msft);
}

void msft_vendor_evt(struct hci_dev *hdev, struct sk_buff *skb)
{
	struct msft_data *msft = hdev->msft_data;
	u8 event;

	if (!msft)
		return;

	/* When the extension has defined an event prefix, check that it
	 * matches, and otherwise just return.
	 */
	if (msft->evt_prefix_len > 0) {
		if (skb->len < msft->evt_prefix_len)
			return;

		if (memcmp(skb->data, msft->evt_prefix, msft->evt_prefix_len))
			return;

		skb_pull(skb, msft->evt_prefix_len);
	}

	/* Every event starts at least with an event code and the rest of
	 * the data is variable and depends on the event code.
	 */
	if (skb->len < 1)
		return;

	event = *skb->data;
	skb_pull(skb, 1);

	bt_dev_dbg(hdev, "MSFT vendor event %u", event);
}

__u64 msft_get_features(struct hci_dev *hdev)
{
	struct msft_data *msft = hdev->msft_data;

	return msft ? msft->features : 0;
}

static void msft_le_monitor_advertisement_cb(struct hci_dev *hdev,
					     u8 status, u16 opcode,
					     struct sk_buff *skb)
{
	struct msft_rp_le_monitor_advertisement *rp;
	struct adv_monitor *monitor;
	struct msft_monitor_advertisement_handle_data *handle_data;
	struct msft_data *msft = hdev->msft_data;

	hci_dev_lock(hdev);

	monitor = idr_find(&hdev->adv_monitors_idr, msft->pending_add_handle);
	if (!monitor) {
		bt_dev_err(hdev, "msft add advmon: monitor %d is not found!",
			   msft->pending_add_handle);
		status = HCI_ERROR_UNSPECIFIED;
		goto unlock;
	}

	if (status)
		goto unlock;

	rp = (struct msft_rp_le_monitor_advertisement *)skb->data;
	if (skb->len < sizeof(*rp)) {
		status = HCI_ERROR_UNSPECIFIED;
		goto unlock;
	}

	handle_data = kmalloc(sizeof(*handle_data), GFP_KERNEL);
	if (!handle_data) {
		status = HCI_ERROR_UNSPECIFIED;
		goto unlock;
	}

	handle_data->mgmt_handle = monitor->handle;
	handle_data->msft_handle = rp->handle;
	INIT_LIST_HEAD(&handle_data->list);
	list_add(&handle_data->list, &msft->handle_map);

	monitor->state = ADV_MONITOR_STATE_OFFLOADED;

unlock:
	if (status && monitor) {
		idr_remove(&hdev->adv_monitors_idr, monitor->handle);
		hci_free_adv_monitor(monitor);
	}

	hci_dev_unlock(hdev);

	hci_add_adv_patterns_monitor_complete(hdev, status);
}

static bool msft_monitor_rssi_valid(struct adv_monitor *monitor)
{
	struct adv_rssi_thresholds *r = &monitor->rssi;

	if (r->high_threshold < MSFT_RSSI_THRESHOLD_VALUE_MIN ||
	    r->high_threshold > MSFT_RSSI_THRESHOLD_VALUE_MAX ||
	    r->low_threshold < MSFT_RSSI_THRESHOLD_VALUE_MIN ||
	    r->low_threshold > MSFT_RSSI_THRESHOLD_VALUE_MAX)
		return false;

	/* High_threshold_timeout is not supported,
	 * once high_threshold is reached, events are immediately reported.
	 */
	if (r->high_threshold_timeout != 0)
		return false;

	if (r->low_threshold_timeout > MSFT_RSSI_LOW_TIMEOUT_MAX)
		return false;

	/* Sampling period from 0x00 to 0xFF are all allowed */
	return true;
}

static bool msft_monitor_pattern_valid(struct adv_monitor *monitor)
{
	return msft_monitor_rssi_valid(monitor);
	/* No additional check needed for pattern-based monitor */
}

/* This function requires the caller holds hdev->lock */
int msft_add_monitor_pattern(struct hci_dev *hdev, struct adv_monitor *monitor)
{
	struct msft_cp_le_monitor_advertisement *cp;
	struct msft_le_monitor_advertisement_pattern_data *pattern_data;
	struct msft_le_monitor_advertisement_pattern *pattern;
	struct adv_pattern *entry;
	struct hci_request req;
	struct msft_data *msft = hdev->msft_data;
	size_t total_size = sizeof(*cp) + sizeof(*pattern_data);
	ptrdiff_t offset = 0;
	u8 pattern_count = 0;
	int err = 0;

	if (!msft)
		return -EOPNOTSUPP;

	if (!msft_monitor_pattern_valid(monitor))
		return -EINVAL;

	list_for_each_entry(entry, &monitor->patterns, list) {
		pattern_count++;
		total_size += sizeof(*pattern) + entry->length;
	}

	cp = kmalloc(total_size, GFP_KERNEL);
	if (!cp)
		return -ENOMEM;

	cp->sub_opcode = MSFT_OP_LE_MONITOR_ADVERTISEMENT;
	cp->rssi_high = monitor->rssi.high_threshold;
	cp->rssi_low = monitor->rssi.low_threshold;
	cp->rssi_low_interval = (u8)monitor->rssi.low_threshold_timeout;
	cp->rssi_sampling_period = monitor->rssi.sampling_period;

	cp->cond_type = MSFT_MONITOR_ADVERTISEMENT_TYPE_PATTERN;

	pattern_data = (void *)cp->data;
	pattern_data->count = pattern_count;

	list_for_each_entry(entry, &monitor->patterns, list) {
		pattern = (void *)(pattern_data->data + offset);
		/* the length also includes data_type and offset */
		pattern->length = entry->length + 2;
		pattern->data_type = entry->ad_type;
		pattern->start_byte = entry->offset;
		memcpy(pattern->pattern, entry->value, entry->length);
		offset += sizeof(*pattern) + entry->length;
	}

	hci_req_init(&req, hdev);
	hci_req_add(&req, hdev->msft_opcode, total_size, cp);
	err = hci_req_run_skb(&req, msft_le_monitor_advertisement_cb);
	kfree(cp);

	if (!err)
		msft->pending_add_handle = monitor->handle;

	return err;
}
