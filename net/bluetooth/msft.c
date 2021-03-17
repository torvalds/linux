// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2020 Google Corporation
 */

#include <net/bluetooth/bluetooth.h>
#include <net/bluetooth/hci_core.h>

#include "msft.h"

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

struct msft_data {
	__u64 features;
	__u8  evt_prefix_len;
	__u8  *evt_prefix;
};

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

	hdev->msft_data = msft;
}

void msft_do_close(struct hci_dev *hdev)
{
	struct msft_data *msft = hdev->msft_data;

	if (!msft)
		return;

	bt_dev_dbg(hdev, "Cleanup of MSFT extension");

	hdev->msft_data = NULL;

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

	return  msft ? msft->features : 0;
}
