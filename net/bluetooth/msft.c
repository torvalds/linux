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
	__u8 pattern[];
};

struct msft_le_monitor_advertisement_pattern_data {
	__u8 count;
	__u8 data[];
};

struct msft_cp_le_monitor_advertisement {
	__u8 sub_opcode;
	__s8 rssi_high;
	__s8 rssi_low;
	__u8 rssi_low_interval;
	__u8 rssi_sampling_period;
	__u8 cond_type;
	__u8 data[];
} __packed;

struct msft_rp_le_monitor_advertisement {
	__u8 status;
	__u8 sub_opcode;
	__u8 handle;
} __packed;

#define MSFT_OP_LE_CANCEL_MONITOR_ADVERTISEMENT	0x04
struct msft_cp_le_cancel_monitor_advertisement {
	__u8 sub_opcode;
	__u8 handle;
} __packed;

struct msft_rp_le_cancel_monitor_advertisement {
	__u8 status;
	__u8 sub_opcode;
} __packed;

#define MSFT_OP_LE_SET_ADVERTISEMENT_FILTER_ENABLE	0x05
struct msft_cp_le_set_advertisement_filter_enable {
	__u8 sub_opcode;
	__u8 enable;
} __packed;

struct msft_rp_le_set_advertisement_filter_enable {
	__u8 status;
	__u8 sub_opcode;
} __packed;

#define MSFT_EV_LE_MONITOR_DEVICE	0x02
struct msft_ev_le_monitor_device {
	__u8     addr_type;
	bdaddr_t bdaddr;
	__u8     monitor_handle;
	__u8     monitor_state;
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
	__u16 pending_remove_handle;
	__u8 resuming;
	__u8 suspending;
	__u8 filter_enabled;
};

static int __msft_add_monitor_pattern(struct hci_dev *hdev,
				      struct adv_monitor *monitor);
static int __msft_remove_monitor(struct hci_dev *hdev,
				 struct adv_monitor *monitor, u16 handle);

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

	if (msft->features & MSFT_FEATURE_MASK_CURVE_VALIDITY)
		hdev->msft_curve_validity = true;

	kfree_skb(skb);
	return true;

failed:
	kfree_skb(skb);
	return false;
}

static void reregister_monitor(struct hci_dev *hdev, int handle)
{
	struct adv_monitor *monitor;
	struct msft_data *msft = hdev->msft_data;
	int err;

	while (1) {
		monitor = idr_get_next(&hdev->adv_monitors_idr, &handle);
		if (!monitor) {
			/* All monitors have been resumed */
			msft->resuming = false;
			hci_update_passive_scan(hdev);
			return;
		}

		msft->pending_add_handle = (u16)handle;
		err = __msft_add_monitor_pattern(hdev, monitor);

		/* If success, we return and wait for monitor added callback */
		if (!err)
			return;

		/* Otherwise remove the monitor and keep registering */
		hci_free_adv_monitor(hdev, monitor);
		handle++;
	}
}

/* is_mgmt = true matches the handle exposed to userspace via mgmt.
 * is_mgmt = false matches the handle used by the msft controller.
 * This function requires the caller holds hdev->lock
 */
static struct msft_monitor_advertisement_handle_data *msft_find_handle_data
				(struct hci_dev *hdev, u16 handle, bool is_mgmt)
{
	struct msft_monitor_advertisement_handle_data *entry;
	struct msft_data *msft = hdev->msft_data;

	list_for_each_entry(entry, &msft->handle_map, list) {
		if (is_mgmt && entry->mgmt_handle == handle)
			return entry;
		if (!is_mgmt && entry->msft_handle == handle)
			return entry;
	}

	return NULL;
}

/* This function requires the caller holds hdev->lock */
static int msft_monitor_device_del(struct hci_dev *hdev, __u16 mgmt_handle,
				   bdaddr_t *bdaddr, __u8 addr_type,
				   bool notify)
{
	struct monitored_device *dev, *tmp;
	int count = 0;

	list_for_each_entry_safe(dev, tmp, &hdev->monitored_devices, list) {
		/* mgmt_handle == 0 indicates remove all devices, whereas,
		 * bdaddr == NULL indicates remove all devices matching the
		 * mgmt_handle.
		 */
		if ((!mgmt_handle || dev->handle == mgmt_handle) &&
		    (!bdaddr || (!bacmp(bdaddr, &dev->bdaddr) &&
				 addr_type == dev->addr_type))) {
			if (notify && dev->notified) {
				mgmt_adv_monitor_device_lost(hdev, dev->handle,
							     &dev->bdaddr,
							     dev->addr_type);
			}

			list_del(&dev->list);
			kfree(dev);
			count++;
		}
	}

	return count;
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
		bt_dev_err(hdev, "msft add advmon: monitor %u is not found!",
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
	if (status && monitor)
		hci_free_adv_monitor(hdev, monitor);

	hci_dev_unlock(hdev);

	if (!msft->resuming)
		hci_add_adv_patterns_monitor_complete(hdev, status);
}

static void msft_le_cancel_monitor_advertisement_cb(struct hci_dev *hdev,
						    u8 status, u16 opcode,
						    struct sk_buff *skb)
{
	struct msft_cp_le_cancel_monitor_advertisement *cp;
	struct msft_rp_le_cancel_monitor_advertisement *rp;
	struct adv_monitor *monitor;
	struct msft_monitor_advertisement_handle_data *handle_data;
	struct msft_data *msft = hdev->msft_data;
	int err;
	bool pending;

	if (status)
		goto done;

	rp = (struct msft_rp_le_cancel_monitor_advertisement *)skb->data;
	if (skb->len < sizeof(*rp)) {
		status = HCI_ERROR_UNSPECIFIED;
		goto done;
	}

	hci_dev_lock(hdev);

	cp = hci_sent_cmd_data(hdev, hdev->msft_opcode);
	handle_data = msft_find_handle_data(hdev, cp->handle, false);

	if (handle_data) {
		monitor = idr_find(&hdev->adv_monitors_idr,
				   handle_data->mgmt_handle);

		if (monitor && monitor->state == ADV_MONITOR_STATE_OFFLOADED)
			monitor->state = ADV_MONITOR_STATE_REGISTERED;

		/* Do not free the monitor if it is being removed due to
		 * suspend. It will be re-monitored on resume.
		 */
		if (monitor && !msft->suspending)
			hci_free_adv_monitor(hdev, monitor);

		/* Clear any monitored devices by this Adv Monitor */
		msft_monitor_device_del(hdev, handle_data->mgmt_handle, NULL,
					0, false);

		list_del(&handle_data->list);
		kfree(handle_data);
	}

	/* If remove all monitors is required, we need to continue the process
	 * here because the earlier it was paused when waiting for the
	 * response from controller.
	 */
	if (msft->pending_remove_handle == 0) {
		pending = hci_remove_all_adv_monitor(hdev, &err);
		if (pending) {
			hci_dev_unlock(hdev);
			return;
		}

		if (err)
			status = HCI_ERROR_UNSPECIFIED;
	}

	hci_dev_unlock(hdev);

done:
	if (!msft->suspending)
		hci_remove_adv_monitor_complete(hdev, status);
}

static int msft_remove_monitor_sync(struct hci_dev *hdev,
				    struct adv_monitor *monitor)
{
	struct msft_cp_le_cancel_monitor_advertisement cp;
	struct msft_monitor_advertisement_handle_data *handle_data;
	struct sk_buff *skb;
	u8 status;

	handle_data = msft_find_handle_data(hdev, monitor->handle, true);

	/* If no matched handle, just remove without telling controller */
	if (!handle_data)
		return -ENOENT;

	cp.sub_opcode = MSFT_OP_LE_CANCEL_MONITOR_ADVERTISEMENT;
	cp.handle = handle_data->msft_handle;

	skb = __hci_cmd_sync(hdev, hdev->msft_opcode, sizeof(cp), &cp,
			     HCI_CMD_TIMEOUT);
	if (IS_ERR(skb))
		return PTR_ERR(skb);

	status = skb->data[0];
	skb_pull(skb, 1);

	msft_le_cancel_monitor_advertisement_cb(hdev, status, hdev->msft_opcode,
						skb);

	return status;
}

/* This function requires the caller holds hci_req_sync_lock */
int msft_suspend_sync(struct hci_dev *hdev)
{
	struct msft_data *msft = hdev->msft_data;
	struct adv_monitor *monitor;
	int handle = 0;

	if (!msft || !msft_monitor_supported(hdev))
		return 0;

	msft->suspending = true;

	while (1) {
		monitor = idr_get_next(&hdev->adv_monitors_idr, &handle);
		if (!monitor)
			break;

		msft_remove_monitor_sync(hdev, monitor);

		handle++;
	}

	/* All monitors have been removed */
	msft->suspending = false;

	return 0;
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

static int msft_add_monitor_sync(struct hci_dev *hdev,
				 struct adv_monitor *monitor)
{
	struct msft_cp_le_monitor_advertisement *cp;
	struct msft_le_monitor_advertisement_pattern_data *pattern_data;
	struct msft_le_monitor_advertisement_pattern *pattern;
	struct adv_pattern *entry;
	size_t total_size = sizeof(*cp) + sizeof(*pattern_data);
	ptrdiff_t offset = 0;
	u8 pattern_count = 0;
	struct sk_buff *skb;
	u8 status;

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

	skb = __hci_cmd_sync(hdev, hdev->msft_opcode, total_size, cp,
			     HCI_CMD_TIMEOUT);
	kfree(cp);

	if (IS_ERR(skb))
		return PTR_ERR(skb);

	status = skb->data[0];
	skb_pull(skb, 1);

	msft_le_monitor_advertisement_cb(hdev, status, hdev->msft_opcode, skb);

	return status;
}

/* This function requires the caller holds hci_req_sync_lock */
int msft_resume_sync(struct hci_dev *hdev)
{
	struct msft_data *msft = hdev->msft_data;
	struct adv_monitor *monitor;
	int handle = 0;

	if (!msft || !msft_monitor_supported(hdev))
		return 0;

	msft->resuming = true;

	while (1) {
		monitor = idr_get_next(&hdev->adv_monitors_idr, &handle);
		if (!monitor)
			break;

		msft_add_monitor_sync(hdev, monitor);

		handle++;
	}

	/* All monitors have been resumed */
	msft->resuming = false;

	return 0;
}

void msft_do_open(struct hci_dev *hdev)
{
	struct msft_data *msft = hdev->msft_data;

	if (hdev->msft_opcode == HCI_OP_NOP)
		return;

	if (!msft) {
		bt_dev_err(hdev, "MSFT extension not registered");
		return;
	}

	bt_dev_dbg(hdev, "Initialize MSFT extension");

	/* Reset existing MSFT data before re-reading */
	kfree(msft->evt_prefix);
	msft->evt_prefix = NULL;
	msft->evt_prefix_len = 0;
	msft->features = 0;

	if (!read_supported_features(hdev, msft)) {
		hdev->msft_data = NULL;
		kfree(msft);
		return;
	}

	if (msft_monitor_supported(hdev)) {
		msft->resuming = true;
		msft_set_filter_enable(hdev, true);
		/* Monitors get removed on power off, so we need to explicitly
		 * tell the controller to re-monitor.
		 */
		reregister_monitor(hdev, 0);
	}
}

void msft_do_close(struct hci_dev *hdev)
{
	struct msft_data *msft = hdev->msft_data;
	struct msft_monitor_advertisement_handle_data *handle_data, *tmp;
	struct adv_monitor *monitor;

	if (!msft)
		return;

	bt_dev_dbg(hdev, "Cleanup of MSFT extension");

	/* The controller will silently remove all monitors on power off.
	 * Therefore, remove handle_data mapping and reset monitor state.
	 */
	list_for_each_entry_safe(handle_data, tmp, &msft->handle_map, list) {
		monitor = idr_find(&hdev->adv_monitors_idr,
				   handle_data->mgmt_handle);

		if (monitor && monitor->state == ADV_MONITOR_STATE_OFFLOADED)
			monitor->state = ADV_MONITOR_STATE_REGISTERED;

		list_del(&handle_data->list);
		kfree(handle_data);
	}

	hci_dev_lock(hdev);

	/* Clear any devices that are being monitored and notify device lost */
	hdev->advmon_pend_notify = false;
	msft_monitor_device_del(hdev, 0, NULL, 0, true);

	hci_dev_unlock(hdev);
}

void msft_register(struct hci_dev *hdev)
{
	struct msft_data *msft = NULL;

	bt_dev_dbg(hdev, "Register MSFT extension");

	msft = kzalloc(sizeof(*msft), GFP_KERNEL);
	if (!msft) {
		bt_dev_err(hdev, "Failed to register MSFT extension");
		return;
	}

	INIT_LIST_HEAD(&msft->handle_map);
	hdev->msft_data = msft;
}

void msft_unregister(struct hci_dev *hdev)
{
	struct msft_data *msft = hdev->msft_data;

	if (!msft)
		return;

	bt_dev_dbg(hdev, "Unregister MSFT extension");

	hdev->msft_data = NULL;

	kfree(msft->evt_prefix);
	kfree(msft);
}

/* This function requires the caller holds hdev->lock */
static void msft_device_found(struct hci_dev *hdev, bdaddr_t *bdaddr,
			      __u8 addr_type, __u16 mgmt_handle)
{
	struct monitored_device *dev;

	dev = kmalloc(sizeof(*dev), GFP_KERNEL);
	if (!dev) {
		bt_dev_err(hdev, "MSFT vendor event %u: no memory",
			   MSFT_EV_LE_MONITOR_DEVICE);
		return;
	}

	bacpy(&dev->bdaddr, bdaddr);
	dev->addr_type = addr_type;
	dev->handle = mgmt_handle;
	dev->notified = false;

	INIT_LIST_HEAD(&dev->list);
	list_add(&dev->list, &hdev->monitored_devices);
	hdev->advmon_pend_notify = true;
}

/* This function requires the caller holds hdev->lock */
static void msft_device_lost(struct hci_dev *hdev, bdaddr_t *bdaddr,
			     __u8 addr_type, __u16 mgmt_handle)
{
	if (!msft_monitor_device_del(hdev, mgmt_handle, bdaddr, addr_type,
				     true)) {
		bt_dev_err(hdev, "MSFT vendor event %u: dev %pMR not in list",
			   MSFT_EV_LE_MONITOR_DEVICE, bdaddr);
	}
}

static void *msft_skb_pull(struct hci_dev *hdev, struct sk_buff *skb,
			   u8 ev, size_t len)
{
	void *data;

	data = skb_pull_data(skb, len);
	if (!data)
		bt_dev_err(hdev, "Malformed MSFT vendor event: 0x%02x", ev);

	return data;
}

/* This function requires the caller holds hdev->lock */
static void msft_monitor_device_evt(struct hci_dev *hdev, struct sk_buff *skb)
{
	struct msft_ev_le_monitor_device *ev;
	struct msft_monitor_advertisement_handle_data *handle_data;
	u8 addr_type;

	ev = msft_skb_pull(hdev, skb, MSFT_EV_LE_MONITOR_DEVICE, sizeof(*ev));
	if (!ev)
		return;

	bt_dev_dbg(hdev,
		   "MSFT vendor event 0x%02x: handle 0x%04x state %d addr %pMR",
		   MSFT_EV_LE_MONITOR_DEVICE, ev->monitor_handle,
		   ev->monitor_state, &ev->bdaddr);

	handle_data = msft_find_handle_data(hdev, ev->monitor_handle, false);
	if (!handle_data)
		return;

	switch (ev->addr_type) {
	case ADDR_LE_DEV_PUBLIC:
		addr_type = BDADDR_LE_PUBLIC;
		break;

	case ADDR_LE_DEV_RANDOM:
		addr_type = BDADDR_LE_RANDOM;
		break;

	default:
		bt_dev_err(hdev,
			   "MSFT vendor event 0x%02x: unknown addr type 0x%02x",
			   MSFT_EV_LE_MONITOR_DEVICE, ev->addr_type);
		return;
	}

	if (ev->monitor_state)
		msft_device_found(hdev, &ev->bdaddr, addr_type,
				  handle_data->mgmt_handle);
	else
		msft_device_lost(hdev, &ev->bdaddr, addr_type,
				 handle_data->mgmt_handle);
}

void msft_vendor_evt(struct hci_dev *hdev, void *data, struct sk_buff *skb)
{
	struct msft_data *msft = hdev->msft_data;
	u8 *evt_prefix;
	u8 *evt;

	if (!msft)
		return;

	/* When the extension has defined an event prefix, check that it
	 * matches, and otherwise just return.
	 */
	if (msft->evt_prefix_len > 0) {
		evt_prefix = msft_skb_pull(hdev, skb, 0, msft->evt_prefix_len);
		if (!evt_prefix)
			return;

		if (memcmp(evt_prefix, msft->evt_prefix, msft->evt_prefix_len))
			return;
	}

	/* Every event starts at least with an event code and the rest of
	 * the data is variable and depends on the event code.
	 */
	if (skb->len < 1)
		return;

	evt = msft_skb_pull(hdev, skb, 0, sizeof(*evt));
	if (!evt)
		return;

	hci_dev_lock(hdev);

	switch (*evt) {
	case MSFT_EV_LE_MONITOR_DEVICE:
		msft_monitor_device_evt(hdev, skb);
		break;

	default:
		bt_dev_dbg(hdev, "MSFT vendor event 0x%02x", *evt);
		break;
	}

	hci_dev_unlock(hdev);
}

__u64 msft_get_features(struct hci_dev *hdev)
{
	struct msft_data *msft = hdev->msft_data;

	return msft ? msft->features : 0;
}

static void msft_le_set_advertisement_filter_enable_cb(struct hci_dev *hdev,
						       u8 status, u16 opcode,
						       struct sk_buff *skb)
{
	struct msft_cp_le_set_advertisement_filter_enable *cp;
	struct msft_rp_le_set_advertisement_filter_enable *rp;
	struct msft_data *msft = hdev->msft_data;

	rp = (struct msft_rp_le_set_advertisement_filter_enable *)skb->data;
	if (skb->len < sizeof(*rp))
		return;

	/* Error 0x0C would be returned if the filter enabled status is
	 * already set to whatever we were trying to set.
	 * Although the default state should be disabled, some controller set
	 * the initial value to enabled. Because there is no way to know the
	 * actual initial value before sending this command, here we also treat
	 * error 0x0C as success.
	 */
	if (status != 0x00 && status != 0x0C)
		return;

	hci_dev_lock(hdev);

	cp = hci_sent_cmd_data(hdev, hdev->msft_opcode);
	msft->filter_enabled = cp->enable;

	if (status == 0x0C)
		bt_dev_warn(hdev, "MSFT filter_enable is already %s",
			    cp->enable ? "on" : "off");

	hci_dev_unlock(hdev);
}

/* This function requires the caller holds hdev->lock */
static int __msft_add_monitor_pattern(struct hci_dev *hdev,
				      struct adv_monitor *monitor)
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

/* This function requires the caller holds hdev->lock */
int msft_add_monitor_pattern(struct hci_dev *hdev, struct adv_monitor *monitor)
{
	struct msft_data *msft = hdev->msft_data;

	if (!msft)
		return -EOPNOTSUPP;

	if (msft->resuming || msft->suspending)
		return -EBUSY;

	return __msft_add_monitor_pattern(hdev, monitor);
}

/* This function requires the caller holds hdev->lock */
static int __msft_remove_monitor(struct hci_dev *hdev,
				 struct adv_monitor *monitor, u16 handle)
{
	struct msft_cp_le_cancel_monitor_advertisement cp;
	struct msft_monitor_advertisement_handle_data *handle_data;
	struct hci_request req;
	struct msft_data *msft = hdev->msft_data;
	int err = 0;

	handle_data = msft_find_handle_data(hdev, monitor->handle, true);

	/* If no matched handle, just remove without telling controller */
	if (!handle_data)
		return -ENOENT;

	cp.sub_opcode = MSFT_OP_LE_CANCEL_MONITOR_ADVERTISEMENT;
	cp.handle = handle_data->msft_handle;

	hci_req_init(&req, hdev);
	hci_req_add(&req, hdev->msft_opcode, sizeof(cp), &cp);
	err = hci_req_run_skb(&req, msft_le_cancel_monitor_advertisement_cb);

	if (!err)
		msft->pending_remove_handle = handle;

	return err;
}

/* This function requires the caller holds hdev->lock */
int msft_remove_monitor(struct hci_dev *hdev, struct adv_monitor *monitor,
			u16 handle)
{
	struct msft_data *msft = hdev->msft_data;

	if (!msft)
		return -EOPNOTSUPP;

	if (msft->resuming || msft->suspending)
		return -EBUSY;

	return __msft_remove_monitor(hdev, monitor, handle);
}

void msft_req_add_set_filter_enable(struct hci_request *req, bool enable)
{
	struct hci_dev *hdev = req->hdev;
	struct msft_cp_le_set_advertisement_filter_enable cp;

	cp.sub_opcode = MSFT_OP_LE_SET_ADVERTISEMENT_FILTER_ENABLE;
	cp.enable = enable;

	hci_req_add(req, hdev->msft_opcode, sizeof(cp), &cp);
}

int msft_set_filter_enable(struct hci_dev *hdev, bool enable)
{
	struct hci_request req;
	struct msft_data *msft = hdev->msft_data;
	int err;

	if (!msft)
		return -EOPNOTSUPP;

	hci_req_init(&req, hdev);
	msft_req_add_set_filter_enable(&req, enable);
	err = hci_req_run_skb(&req, msft_le_set_advertisement_filter_enable_cb);

	return err;
}

bool msft_curve_validity(struct hci_dev *hdev)
{
	return hdev->msft_curve_validity;
}
