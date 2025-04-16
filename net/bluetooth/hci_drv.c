// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2025 Google Corporation
 */

#include <linux/skbuff.h>
#include <linux/types.h>

#include <net/bluetooth/bluetooth.h>
#include <net/bluetooth/hci.h>
#include <net/bluetooth/hci_core.h>
#include <net/bluetooth/hci_drv.h>

int hci_drv_cmd_status(struct hci_dev *hdev, u16 cmd, u8 status)
{
	struct hci_drv_ev_hdr *hdr;
	struct hci_drv_ev_cmd_status *ev;
	struct sk_buff *skb;

	skb = bt_skb_alloc(sizeof(*hdr) + sizeof(*ev), GFP_KERNEL);
	if (!skb)
		return -ENOMEM;

	hdr = skb_put(skb, sizeof(*hdr));
	hdr->opcode = __cpu_to_le16(HCI_DRV_EV_CMD_STATUS);
	hdr->len = __cpu_to_le16(sizeof(*ev));

	ev = skb_put(skb, sizeof(*ev));
	ev->opcode = __cpu_to_le16(cmd);
	ev->status = status;

	hci_skb_pkt_type(skb) = HCI_DRV_PKT;

	return hci_recv_frame(hdev, skb);
}
EXPORT_SYMBOL(hci_drv_cmd_status);

int hci_drv_cmd_complete(struct hci_dev *hdev, u16 cmd, u8 status, void *rp,
			 size_t rp_len)
{
	struct hci_drv_ev_hdr *hdr;
	struct hci_drv_ev_cmd_complete *ev;
	struct sk_buff *skb;

	skb = bt_skb_alloc(sizeof(*hdr) + sizeof(*ev) + rp_len, GFP_KERNEL);
	if (!skb)
		return -ENOMEM;

	hdr = skb_put(skb, sizeof(*hdr));
	hdr->opcode = __cpu_to_le16(HCI_DRV_EV_CMD_COMPLETE);
	hdr->len = __cpu_to_le16(sizeof(*ev) + rp_len);

	ev = skb_put(skb, sizeof(*ev));
	ev->opcode = __cpu_to_le16(cmd);
	ev->status = status;

	skb_put_data(skb, rp, rp_len);

	hci_skb_pkt_type(skb) = HCI_DRV_PKT;

	return hci_recv_frame(hdev, skb);
}
EXPORT_SYMBOL(hci_drv_cmd_complete);

int hci_drv_process_cmd(struct hci_dev *hdev, struct sk_buff *skb)
{
	struct hci_drv_cmd_hdr *hdr;
	const struct hci_drv_handler *handler = NULL;
	u16 opcode, len, ogf, ocf;

	hdr = skb_pull_data(skb, sizeof(*hdr));
	if (!hdr)
		return -EILSEQ;

	opcode = __le16_to_cpu(hdr->opcode);
	len = __le16_to_cpu(hdr->len);
	if (len != skb->len)
		return -EILSEQ;

	ogf = hci_opcode_ogf(opcode);
	ocf = hci_opcode_ocf(opcode);

	if (!hdev->hci_drv)
		return hci_drv_cmd_status(hdev, opcode,
					  HCI_DRV_STATUS_UNKNOWN_COMMAND);

	if (ogf != HCI_DRV_OGF_DRIVER_SPECIFIC) {
		if (opcode < hdev->hci_drv->common_handler_count)
			handler = &hdev->hci_drv->common_handlers[opcode];
	} else {
		if (ocf < hdev->hci_drv->specific_handler_count)
			handler = &hdev->hci_drv->specific_handlers[ocf];
	}

	if (!handler || !handler->func)
		return hci_drv_cmd_status(hdev, opcode,
					  HCI_DRV_STATUS_UNKNOWN_COMMAND);

	if (len != handler->data_len)
		return hci_drv_cmd_status(hdev, opcode,
					  HCI_DRV_STATUS_INVALID_PARAMETERS);

	return handler->func(hdev, skb->data, len);
}
EXPORT_SYMBOL(hci_drv_process_cmd);
