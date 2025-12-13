/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2025 Google Corporation
 */

#ifndef __HCI_DRV_H
#define __HCI_DRV_H

#include <linux/types.h>

#include <net/bluetooth/bluetooth.h>
#include <net/bluetooth/hci.h>

struct hci_drv_cmd_hdr {
	__le16	opcode;
	__le16	len;
} __packed;

struct hci_drv_ev_hdr {
	__le16	opcode;
	__le16	len;
} __packed;

#define HCI_DRV_EV_CMD_STATUS	0x0000
struct hci_drv_ev_cmd_status {
	__le16	opcode;
	__u8	status;
} __packed;

#define HCI_DRV_EV_CMD_COMPLETE	0x0001
struct hci_drv_ev_cmd_complete {
	__le16	opcode;
	__u8	status;
	__u8	data[];
} __packed;

#define HCI_DRV_STATUS_SUCCESS			0x00
#define HCI_DRV_STATUS_UNSPECIFIED_ERROR	0x01
#define HCI_DRV_STATUS_UNKNOWN_COMMAND		0x02
#define HCI_DRV_STATUS_INVALID_PARAMETERS	0x03

#define HCI_DRV_MAX_DRIVER_NAME_LENGTH	32

/* Common commands that make sense on all drivers start from 0x0000 */
#define HCI_DRV_OP_READ_INFO	0x0000
#define HCI_DRV_READ_INFO_SIZE	0
struct hci_drv_rp_read_info {
	__u8	driver_name[HCI_DRV_MAX_DRIVER_NAME_LENGTH];
	__le16	num_supported_commands;
	__le16	supported_commands[] __counted_by_le(num_supported_commands);
} __packed;

/* Driver specific OGF (Opcode Group Field)
 * Commands in this group may have different meanings across different drivers.
 */
#define HCI_DRV_OGF_DRIVER_SPECIFIC	0x01

int hci_drv_cmd_status(struct hci_dev *hdev, u16 cmd, u8 status);
int hci_drv_cmd_complete(struct hci_dev *hdev, u16 cmd, u8 status, void *rp,
			 size_t rp_len);
int hci_drv_process_cmd(struct hci_dev *hdev, struct sk_buff *cmd_skb);

struct hci_drv_handler {
	int (*func)(struct hci_dev *hdev, void *data, u16 data_len);
	size_t data_len;
};

struct hci_drv {
	size_t common_handler_count;
	const struct hci_drv_handler *common_handlers;

	size_t specific_handler_count;
	const struct hci_drv_handler *specific_handlers;
};

#endif /* __HCI_DRV_H */
