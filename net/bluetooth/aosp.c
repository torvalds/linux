// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2021 Intel Corporation
 */

#include <net/bluetooth/bluetooth.h>
#include <net/bluetooth/hci_core.h>

#include "aosp.h"

/* Command complete parameters of LE_Get_Vendor_Capabilities_Command
 * The parameters grow over time. The base version that declares the
 * version_supported field is v0.95. Refer to
 * https://cs.android.com/android/platform/superproject/+/master:system/
 *         bt/gd/hci/controller.cc;l=452?q=le_get_vendor_capabilities_handler
 */
struct aosp_rp_le_get_vendor_capa {
	/* v0.95: 15 octets */
	__u8	status;
	__u8	max_advt_instances;
	__u8	offloaded_resolution_of_private_address;
	__le16	total_scan_results_storage;
	__u8	max_irk_list_sz;
	__u8	filtering_support;
	__u8	max_filter;
	__u8	activity_energy_info_support;
	__le16	version_supported;
	__le16	total_num_of_advt_tracked;
	__u8	extended_scan_support;
	__u8	debug_logging_supported;
	/* v0.96: 16 octets */
	__u8	le_address_generation_offloading_support;
	/* v0.98: 21 octets */
	__le32	a2dp_source_offload_capability_mask;
	__u8	bluetooth_quality_report_support;
	/* v1.00: 25 octets */
	__le32	dynamic_audio_buffer_support;
} __packed;

#define VENDOR_CAPA_BASE_SIZE		15
#define VENDOR_CAPA_0_98_SIZE		21

void aosp_do_open(struct hci_dev *hdev)
{
	struct sk_buff *skb;
	struct aosp_rp_le_get_vendor_capa *rp;
	u16 version_supported;

	if (!hdev->aosp_capable)
		return;

	bt_dev_dbg(hdev, "Initialize AOSP extension");

	/* LE Get Vendor Capabilities Command */
	skb = __hci_cmd_sync(hdev, hci_opcode_pack(0x3f, 0x153), 0, NULL,
			     HCI_CMD_TIMEOUT);
	if (IS_ERR(skb)) {
		bt_dev_err(hdev, "AOSP get vendor capabilities (%ld)",
			   PTR_ERR(skb));
		return;
	}

	/* A basic length check */
	if (skb->len < VENDOR_CAPA_BASE_SIZE)
		goto length_error;

	rp = (struct aosp_rp_le_get_vendor_capa *)skb->data;

	version_supported = le16_to_cpu(rp->version_supported);
	/* AOSP displays the verion number like v0.98, v1.00, etc. */
	bt_dev_info(hdev, "AOSP extensions version v%u.%02u",
		    version_supported >> 8, version_supported & 0xff);

	/* Do not support very old versions. */
	if (version_supported < 95) {
		bt_dev_warn(hdev, "AOSP capabilities version %u too old",
			    version_supported);
		goto done;
	}

	if (version_supported < 98) {
		bt_dev_warn(hdev, "AOSP quality report is not supported");
		goto done;
	}

	if (skb->len < VENDOR_CAPA_0_98_SIZE)
		goto length_error;

	/* The bluetooth_quality_report_support is defined at version
	 * v0.98. Refer to
	 * https://cs.android.com/android/platform/superproject/+/
	 *         master:system/bt/gd/hci/controller.cc;l=477
	 */
	if (rp->bluetooth_quality_report_support) {
		hdev->aosp_quality_report = true;
		bt_dev_info(hdev, "AOSP quality report is supported");
	}

	goto done;

length_error:
	bt_dev_err(hdev, "AOSP capabilities length %d too short", skb->len);

done:
	kfree_skb(skb);
}

void aosp_do_close(struct hci_dev *hdev)
{
	if (!hdev->aosp_capable)
		return;

	bt_dev_dbg(hdev, "Cleanup of AOSP extension");
}
