/*
   BlueZ - Bluetooth protocol stack for Linux

   Copyright (C) 2010  Nokia Corporation
   Copyright (C) 2011-2012 Intel Corporation

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License version 2 as
   published by the Free Software Foundation;

   THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
   OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
   FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT OF THIRD PARTY RIGHTS.
   IN NO EVENT SHALL THE COPYRIGHT HOLDER(S) AND AUTHOR(S) BE LIABLE FOR ANY
   CLAIM, OR ANY SPECIAL INDIRECT OR CONSEQUENTIAL DAMAGES, OR ANY DAMAGES
   WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
   ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
   OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.

   ALL LIABILITY, INCLUDING LIABILITY FOR INFRINGEMENT OF ANY PATENTS,
   COPYRIGHTS, TRADEMARKS OR OTHER RIGHTS, RELATING TO USE OF THIS
   SOFTWARE IS DISCLAIMED.
*/

/* Bluetooth HCI Management interface */

#include <linux/module.h>
#include <asm/unaligned.h>

#include <net/bluetooth/bluetooth.h>
#include <net/bluetooth/hci_core.h>
#include <net/bluetooth/l2cap.h>
#include <net/bluetooth/mgmt.h>

#include "hci_request.h"
#include "smp.h"

#define MGMT_VERSION	1
#define MGMT_REVISION	8

static const u16 mgmt_commands[] = {
	MGMT_OP_READ_INDEX_LIST,
	MGMT_OP_READ_INFO,
	MGMT_OP_SET_POWERED,
	MGMT_OP_SET_DISCOVERABLE,
	MGMT_OP_SET_CONNECTABLE,
	MGMT_OP_SET_FAST_CONNECTABLE,
	MGMT_OP_SET_BONDABLE,
	MGMT_OP_SET_LINK_SECURITY,
	MGMT_OP_SET_SSP,
	MGMT_OP_SET_HS,
	MGMT_OP_SET_LE,
	MGMT_OP_SET_DEV_CLASS,
	MGMT_OP_SET_LOCAL_NAME,
	MGMT_OP_ADD_UUID,
	MGMT_OP_REMOVE_UUID,
	MGMT_OP_LOAD_LINK_KEYS,
	MGMT_OP_LOAD_LONG_TERM_KEYS,
	MGMT_OP_DISCONNECT,
	MGMT_OP_GET_CONNECTIONS,
	MGMT_OP_PIN_CODE_REPLY,
	MGMT_OP_PIN_CODE_NEG_REPLY,
	MGMT_OP_SET_IO_CAPABILITY,
	MGMT_OP_PAIR_DEVICE,
	MGMT_OP_CANCEL_PAIR_DEVICE,
	MGMT_OP_UNPAIR_DEVICE,
	MGMT_OP_USER_CONFIRM_REPLY,
	MGMT_OP_USER_CONFIRM_NEG_REPLY,
	MGMT_OP_USER_PASSKEY_REPLY,
	MGMT_OP_USER_PASSKEY_NEG_REPLY,
	MGMT_OP_READ_LOCAL_OOB_DATA,
	MGMT_OP_ADD_REMOTE_OOB_DATA,
	MGMT_OP_REMOVE_REMOTE_OOB_DATA,
	MGMT_OP_START_DISCOVERY,
	MGMT_OP_STOP_DISCOVERY,
	MGMT_OP_CONFIRM_NAME,
	MGMT_OP_BLOCK_DEVICE,
	MGMT_OP_UNBLOCK_DEVICE,
	MGMT_OP_SET_DEVICE_ID,
	MGMT_OP_SET_ADVERTISING,
	MGMT_OP_SET_BREDR,
	MGMT_OP_SET_STATIC_ADDRESS,
	MGMT_OP_SET_SCAN_PARAMS,
	MGMT_OP_SET_SECURE_CONN,
	MGMT_OP_SET_DEBUG_KEYS,
	MGMT_OP_SET_PRIVACY,
	MGMT_OP_LOAD_IRKS,
	MGMT_OP_GET_CONN_INFO,
	MGMT_OP_GET_CLOCK_INFO,
	MGMT_OP_ADD_DEVICE,
	MGMT_OP_REMOVE_DEVICE,
	MGMT_OP_LOAD_CONN_PARAM,
	MGMT_OP_READ_UNCONF_INDEX_LIST,
	MGMT_OP_READ_CONFIG_INFO,
	MGMT_OP_SET_EXTERNAL_CONFIG,
	MGMT_OP_SET_PUBLIC_ADDRESS,
	MGMT_OP_START_SERVICE_DISCOVERY,
};

static const u16 mgmt_events[] = {
	MGMT_EV_CONTROLLER_ERROR,
	MGMT_EV_INDEX_ADDED,
	MGMT_EV_INDEX_REMOVED,
	MGMT_EV_NEW_SETTINGS,
	MGMT_EV_CLASS_OF_DEV_CHANGED,
	MGMT_EV_LOCAL_NAME_CHANGED,
	MGMT_EV_NEW_LINK_KEY,
	MGMT_EV_NEW_LONG_TERM_KEY,
	MGMT_EV_DEVICE_CONNECTED,
	MGMT_EV_DEVICE_DISCONNECTED,
	MGMT_EV_CONNECT_FAILED,
	MGMT_EV_PIN_CODE_REQUEST,
	MGMT_EV_USER_CONFIRM_REQUEST,
	MGMT_EV_USER_PASSKEY_REQUEST,
	MGMT_EV_AUTH_FAILED,
	MGMT_EV_DEVICE_FOUND,
	MGMT_EV_DISCOVERING,
	MGMT_EV_DEVICE_BLOCKED,
	MGMT_EV_DEVICE_UNBLOCKED,
	MGMT_EV_DEVICE_UNPAIRED,
	MGMT_EV_PASSKEY_NOTIFY,
	MGMT_EV_NEW_IRK,
	MGMT_EV_NEW_CSRK,
	MGMT_EV_DEVICE_ADDED,
	MGMT_EV_DEVICE_REMOVED,
	MGMT_EV_NEW_CONN_PARAM,
	MGMT_EV_UNCONF_INDEX_ADDED,
	MGMT_EV_UNCONF_INDEX_REMOVED,
	MGMT_EV_NEW_CONFIG_OPTIONS,
};

#define CACHE_TIMEOUT	msecs_to_jiffies(2 * 1000)

struct pending_cmd {
	struct list_head list;
	u16 opcode;
	int index;
	void *param;
	size_t param_len;
	struct sock *sk;
	void *user_data;
	int (*cmd_complete)(struct pending_cmd *cmd, u8 status);
};

/* HCI to MGMT error code conversion table */
static u8 mgmt_status_table[] = {
	MGMT_STATUS_SUCCESS,
	MGMT_STATUS_UNKNOWN_COMMAND,	/* Unknown Command */
	MGMT_STATUS_NOT_CONNECTED,	/* No Connection */
	MGMT_STATUS_FAILED,		/* Hardware Failure */
	MGMT_STATUS_CONNECT_FAILED,	/* Page Timeout */
	MGMT_STATUS_AUTH_FAILED,	/* Authentication Failed */
	MGMT_STATUS_AUTH_FAILED,	/* PIN or Key Missing */
	MGMT_STATUS_NO_RESOURCES,	/* Memory Full */
	MGMT_STATUS_TIMEOUT,		/* Connection Timeout */
	MGMT_STATUS_NO_RESOURCES,	/* Max Number of Connections */
	MGMT_STATUS_NO_RESOURCES,	/* Max Number of SCO Connections */
	MGMT_STATUS_ALREADY_CONNECTED,	/* ACL Connection Exists */
	MGMT_STATUS_BUSY,		/* Command Disallowed */
	MGMT_STATUS_NO_RESOURCES,	/* Rejected Limited Resources */
	MGMT_STATUS_REJECTED,		/* Rejected Security */
	MGMT_STATUS_REJECTED,		/* Rejected Personal */
	MGMT_STATUS_TIMEOUT,		/* Host Timeout */
	MGMT_STATUS_NOT_SUPPORTED,	/* Unsupported Feature */
	MGMT_STATUS_INVALID_PARAMS,	/* Invalid Parameters */
	MGMT_STATUS_DISCONNECTED,	/* OE User Ended Connection */
	MGMT_STATUS_NO_RESOURCES,	/* OE Low Resources */
	MGMT_STATUS_DISCONNECTED,	/* OE Power Off */
	MGMT_STATUS_DISCONNECTED,	/* Connection Terminated */
	MGMT_STATUS_BUSY,		/* Repeated Attempts */
	MGMT_STATUS_REJECTED,		/* Pairing Not Allowed */
	MGMT_STATUS_FAILED,		/* Unknown LMP PDU */
	MGMT_STATUS_NOT_SUPPORTED,	/* Unsupported Remote Feature */
	MGMT_STATUS_REJECTED,		/* SCO Offset Rejected */
	MGMT_STATUS_REJECTED,		/* SCO Interval Rejected */
	MGMT_STATUS_REJECTED,		/* Air Mode Rejected */
	MGMT_STATUS_INVALID_PARAMS,	/* Invalid LMP Parameters */
	MGMT_STATUS_FAILED,		/* Unspecified Error */
	MGMT_STATUS_NOT_SUPPORTED,	/* Unsupported LMP Parameter Value */
	MGMT_STATUS_FAILED,		/* Role Change Not Allowed */
	MGMT_STATUS_TIMEOUT,		/* LMP Response Timeout */
	MGMT_STATUS_FAILED,		/* LMP Error Transaction Collision */
	MGMT_STATUS_FAILED,		/* LMP PDU Not Allowed */
	MGMT_STATUS_REJECTED,		/* Encryption Mode Not Accepted */
	MGMT_STATUS_FAILED,		/* Unit Link Key Used */
	MGMT_STATUS_NOT_SUPPORTED,	/* QoS Not Supported */
	MGMT_STATUS_TIMEOUT,		/* Instant Passed */
	MGMT_STATUS_NOT_SUPPORTED,	/* Pairing Not Supported */
	MGMT_STATUS_FAILED,		/* Transaction Collision */
	MGMT_STATUS_INVALID_PARAMS,	/* Unacceptable Parameter */
	MGMT_STATUS_REJECTED,		/* QoS Rejected */
	MGMT_STATUS_NOT_SUPPORTED,	/* Classification Not Supported */
	MGMT_STATUS_REJECTED,		/* Insufficient Security */
	MGMT_STATUS_INVALID_PARAMS,	/* Parameter Out Of Range */
	MGMT_STATUS_BUSY,		/* Role Switch Pending */
	MGMT_STATUS_FAILED,		/* Slot Violation */
	MGMT_STATUS_FAILED,		/* Role Switch Failed */
	MGMT_STATUS_INVALID_PARAMS,	/* EIR Too Large */
	MGMT_STATUS_NOT_SUPPORTED,	/* Simple Pairing Not Supported */
	MGMT_STATUS_BUSY,		/* Host Busy Pairing */
	MGMT_STATUS_REJECTED,		/* Rejected, No Suitable Channel */
	MGMT_STATUS_BUSY,		/* Controller Busy */
	MGMT_STATUS_INVALID_PARAMS,	/* Unsuitable Connection Interval */
	MGMT_STATUS_TIMEOUT,		/* Directed Advertising Timeout */
	MGMT_STATUS_AUTH_FAILED,	/* Terminated Due to MIC Failure */
	MGMT_STATUS_CONNECT_FAILED,	/* Connection Establishment Failed */
	MGMT_STATUS_CONNECT_FAILED,	/* MAC Connection Failed */
};

static u8 mgmt_status(u8 hci_status)
{
	if (hci_status < ARRAY_SIZE(mgmt_status_table))
		return mgmt_status_table[hci_status];

	return MGMT_STATUS_FAILED;
}

static int mgmt_event(u16 event, struct hci_dev *hdev, void *data, u16 data_len,
		      struct sock *skip_sk)
{
	struct sk_buff *skb;
	struct mgmt_hdr *hdr;

	skb = alloc_skb(sizeof(*hdr) + data_len, GFP_KERNEL);
	if (!skb)
		return -ENOMEM;

	hdr = (void *) skb_put(skb, sizeof(*hdr));
	hdr->opcode = cpu_to_le16(event);
	if (hdev)
		hdr->index = cpu_to_le16(hdev->id);
	else
		hdr->index = cpu_to_le16(MGMT_INDEX_NONE);
	hdr->len = cpu_to_le16(data_len);

	if (data)
		memcpy(skb_put(skb, data_len), data, data_len);

	/* Time stamp */
	__net_timestamp(skb);

	hci_send_to_control(skb, skip_sk);
	kfree_skb(skb);

	return 0;
}

static int cmd_status(struct sock *sk, u16 index, u16 cmd, u8 status)
{
	struct sk_buff *skb;
	struct mgmt_hdr *hdr;
	struct mgmt_ev_cmd_status *ev;
	int err;

	BT_DBG("sock %p, index %u, cmd %u, status %u", sk, index, cmd, status);

	skb = alloc_skb(sizeof(*hdr) + sizeof(*ev), GFP_KERNEL);
	if (!skb)
		return -ENOMEM;

	hdr = (void *) skb_put(skb, sizeof(*hdr));

	hdr->opcode = cpu_to_le16(MGMT_EV_CMD_STATUS);
	hdr->index = cpu_to_le16(index);
	hdr->len = cpu_to_le16(sizeof(*ev));

	ev = (void *) skb_put(skb, sizeof(*ev));
	ev->status = status;
	ev->opcode = cpu_to_le16(cmd);

	err = sock_queue_rcv_skb(sk, skb);
	if (err < 0)
		kfree_skb(skb);

	return err;
}

static int cmd_complete(struct sock *sk, u16 index, u16 cmd, u8 status,
			void *rp, size_t rp_len)
{
	struct sk_buff *skb;
	struct mgmt_hdr *hdr;
	struct mgmt_ev_cmd_complete *ev;
	int err;

	BT_DBG("sock %p", sk);

	skb = alloc_skb(sizeof(*hdr) + sizeof(*ev) + rp_len, GFP_KERNEL);
	if (!skb)
		return -ENOMEM;

	hdr = (void *) skb_put(skb, sizeof(*hdr));

	hdr->opcode = cpu_to_le16(MGMT_EV_CMD_COMPLETE);
	hdr->index = cpu_to_le16(index);
	hdr->len = cpu_to_le16(sizeof(*ev) + rp_len);

	ev = (void *) skb_put(skb, sizeof(*ev) + rp_len);
	ev->opcode = cpu_to_le16(cmd);
	ev->status = status;

	if (rp)
		memcpy(ev->data, rp, rp_len);

	err = sock_queue_rcv_skb(sk, skb);
	if (err < 0)
		kfree_skb(skb);

	return err;
}

static int read_version(struct sock *sk, struct hci_dev *hdev, void *data,
			u16 data_len)
{
	struct mgmt_rp_read_version rp;

	BT_DBG("sock %p", sk);

	rp.version = MGMT_VERSION;
	rp.revision = cpu_to_le16(MGMT_REVISION);

	return cmd_complete(sk, MGMT_INDEX_NONE, MGMT_OP_READ_VERSION, 0, &rp,
			    sizeof(rp));
}

static int read_commands(struct sock *sk, struct hci_dev *hdev, void *data,
			 u16 data_len)
{
	struct mgmt_rp_read_commands *rp;
	const u16 num_commands = ARRAY_SIZE(mgmt_commands);
	const u16 num_events = ARRAY_SIZE(mgmt_events);
	__le16 *opcode;
	size_t rp_size;
	int i, err;

	BT_DBG("sock %p", sk);

	rp_size = sizeof(*rp) + ((num_commands + num_events) * sizeof(u16));

	rp = kmalloc(rp_size, GFP_KERNEL);
	if (!rp)
		return -ENOMEM;

	rp->num_commands = cpu_to_le16(num_commands);
	rp->num_events = cpu_to_le16(num_events);

	for (i = 0, opcode = rp->opcodes; i < num_commands; i++, opcode++)
		put_unaligned_le16(mgmt_commands[i], opcode);

	for (i = 0; i < num_events; i++, opcode++)
		put_unaligned_le16(mgmt_events[i], opcode);

	err = cmd_complete(sk, MGMT_INDEX_NONE, MGMT_OP_READ_COMMANDS, 0, rp,
			   rp_size);
	kfree(rp);

	return err;
}

static int read_index_list(struct sock *sk, struct hci_dev *hdev, void *data,
			   u16 data_len)
{
	struct mgmt_rp_read_index_list *rp;
	struct hci_dev *d;
	size_t rp_len;
	u16 count;
	int err;

	BT_DBG("sock %p", sk);

	read_lock(&hci_dev_list_lock);

	count = 0;
	list_for_each_entry(d, &hci_dev_list, list) {
		if (d->dev_type == HCI_BREDR &&
		    !test_bit(HCI_UNCONFIGURED, &d->dev_flags))
			count++;
	}

	rp_len = sizeof(*rp) + (2 * count);
	rp = kmalloc(rp_len, GFP_ATOMIC);
	if (!rp) {
		read_unlock(&hci_dev_list_lock);
		return -ENOMEM;
	}

	count = 0;
	list_for_each_entry(d, &hci_dev_list, list) {
		if (test_bit(HCI_SETUP, &d->dev_flags) ||
		    test_bit(HCI_CONFIG, &d->dev_flags) ||
		    test_bit(HCI_USER_CHANNEL, &d->dev_flags))
			continue;

		/* Devices marked as raw-only are neither configured
		 * nor unconfigured controllers.
		 */
		if (test_bit(HCI_QUIRK_RAW_DEVICE, &d->quirks))
			continue;

		if (d->dev_type == HCI_BREDR &&
		    !test_bit(HCI_UNCONFIGURED, &d->dev_flags)) {
			rp->index[count++] = cpu_to_le16(d->id);
			BT_DBG("Added hci%u", d->id);
		}
	}

	rp->num_controllers = cpu_to_le16(count);
	rp_len = sizeof(*rp) + (2 * count);

	read_unlock(&hci_dev_list_lock);

	err = cmd_complete(sk, MGMT_INDEX_NONE, MGMT_OP_READ_INDEX_LIST, 0, rp,
			   rp_len);

	kfree(rp);

	return err;
}

static int read_unconf_index_list(struct sock *sk, struct hci_dev *hdev,
				  void *data, u16 data_len)
{
	struct mgmt_rp_read_unconf_index_list *rp;
	struct hci_dev *d;
	size_t rp_len;
	u16 count;
	int err;

	BT_DBG("sock %p", sk);

	read_lock(&hci_dev_list_lock);

	count = 0;
	list_for_each_entry(d, &hci_dev_list, list) {
		if (d->dev_type == HCI_BREDR &&
		    test_bit(HCI_UNCONFIGURED, &d->dev_flags))
			count++;
	}

	rp_len = sizeof(*rp) + (2 * count);
	rp = kmalloc(rp_len, GFP_ATOMIC);
	if (!rp) {
		read_unlock(&hci_dev_list_lock);
		return -ENOMEM;
	}

	count = 0;
	list_for_each_entry(d, &hci_dev_list, list) {
		if (test_bit(HCI_SETUP, &d->dev_flags) ||
		    test_bit(HCI_CONFIG, &d->dev_flags) ||
		    test_bit(HCI_USER_CHANNEL, &d->dev_flags))
			continue;

		/* Devices marked as raw-only are neither configured
		 * nor unconfigured controllers.
		 */
		if (test_bit(HCI_QUIRK_RAW_DEVICE, &d->quirks))
			continue;

		if (d->dev_type == HCI_BREDR &&
		    test_bit(HCI_UNCONFIGURED, &d->dev_flags)) {
			rp->index[count++] = cpu_to_le16(d->id);
			BT_DBG("Added hci%u", d->id);
		}
	}

	rp->num_controllers = cpu_to_le16(count);
	rp_len = sizeof(*rp) + (2 * count);

	read_unlock(&hci_dev_list_lock);

	err = cmd_complete(sk, MGMT_INDEX_NONE, MGMT_OP_READ_UNCONF_INDEX_LIST,
			   0, rp, rp_len);

	kfree(rp);

	return err;
}

static bool is_configured(struct hci_dev *hdev)
{
	if (test_bit(HCI_QUIRK_EXTERNAL_CONFIG, &hdev->quirks) &&
	    !test_bit(HCI_EXT_CONFIGURED, &hdev->dev_flags))
		return false;

	if (test_bit(HCI_QUIRK_INVALID_BDADDR, &hdev->quirks) &&
	    !bacmp(&hdev->public_addr, BDADDR_ANY))
		return false;

	return true;
}

static __le32 get_missing_options(struct hci_dev *hdev)
{
	u32 options = 0;

	if (test_bit(HCI_QUIRK_EXTERNAL_CONFIG, &hdev->quirks) &&
	    !test_bit(HCI_EXT_CONFIGURED, &hdev->dev_flags))
		options |= MGMT_OPTION_EXTERNAL_CONFIG;

	if (test_bit(HCI_QUIRK_INVALID_BDADDR, &hdev->quirks) &&
	    !bacmp(&hdev->public_addr, BDADDR_ANY))
		options |= MGMT_OPTION_PUBLIC_ADDRESS;

	return cpu_to_le32(options);
}

static int new_options(struct hci_dev *hdev, struct sock *skip)
{
	__le32 options = get_missing_options(hdev);

	return mgmt_event(MGMT_EV_NEW_CONFIG_OPTIONS, hdev, &options,
			  sizeof(options), skip);
}

static int send_options_rsp(struct sock *sk, u16 opcode, struct hci_dev *hdev)
{
	__le32 options = get_missing_options(hdev);

	return cmd_complete(sk, hdev->id, opcode, 0, &options,
			    sizeof(options));
}

static int read_config_info(struct sock *sk, struct hci_dev *hdev,
			    void *data, u16 data_len)
{
	struct mgmt_rp_read_config_info rp;
	u32 options = 0;

	BT_DBG("sock %p %s", sk, hdev->name);

	hci_dev_lock(hdev);

	memset(&rp, 0, sizeof(rp));
	rp.manufacturer = cpu_to_le16(hdev->manufacturer);

	if (test_bit(HCI_QUIRK_EXTERNAL_CONFIG, &hdev->quirks))
		options |= MGMT_OPTION_EXTERNAL_CONFIG;

	if (hdev->set_bdaddr)
		options |= MGMT_OPTION_PUBLIC_ADDRESS;

	rp.supported_options = cpu_to_le32(options);
	rp.missing_options = get_missing_options(hdev);

	hci_dev_unlock(hdev);

	return cmd_complete(sk, hdev->id, MGMT_OP_READ_CONFIG_INFO, 0, &rp,
			    sizeof(rp));
}

static u32 get_supported_settings(struct hci_dev *hdev)
{
	u32 settings = 0;

	settings |= MGMT_SETTING_POWERED;
	settings |= MGMT_SETTING_BONDABLE;
	settings |= MGMT_SETTING_DEBUG_KEYS;
	settings |= MGMT_SETTING_CONNECTABLE;
	settings |= MGMT_SETTING_DISCOVERABLE;

	if (lmp_bredr_capable(hdev)) {
		if (hdev->hci_ver >= BLUETOOTH_VER_1_2)
			settings |= MGMT_SETTING_FAST_CONNECTABLE;
		settings |= MGMT_SETTING_BREDR;
		settings |= MGMT_SETTING_LINK_SECURITY;

		if (lmp_ssp_capable(hdev)) {
			settings |= MGMT_SETTING_SSP;
			settings |= MGMT_SETTING_HS;
		}

		if (lmp_sc_capable(hdev))
			settings |= MGMT_SETTING_SECURE_CONN;
	}

	if (lmp_le_capable(hdev)) {
		settings |= MGMT_SETTING_LE;
		settings |= MGMT_SETTING_ADVERTISING;
		settings |= MGMT_SETTING_SECURE_CONN;
		settings |= MGMT_SETTING_PRIVACY;
	}

	if (test_bit(HCI_QUIRK_EXTERNAL_CONFIG, &hdev->quirks) ||
	    hdev->set_bdaddr)
		settings |= MGMT_SETTING_CONFIGURATION;

	return settings;
}

static u32 get_current_settings(struct hci_dev *hdev)
{
	u32 settings = 0;

	if (hdev_is_powered(hdev))
		settings |= MGMT_SETTING_POWERED;

	if (test_bit(HCI_CONNECTABLE, &hdev->dev_flags))
		settings |= MGMT_SETTING_CONNECTABLE;

	if (test_bit(HCI_FAST_CONNECTABLE, &hdev->dev_flags))
		settings |= MGMT_SETTING_FAST_CONNECTABLE;

	if (test_bit(HCI_DISCOVERABLE, &hdev->dev_flags))
		settings |= MGMT_SETTING_DISCOVERABLE;

	if (test_bit(HCI_BONDABLE, &hdev->dev_flags))
		settings |= MGMT_SETTING_BONDABLE;

	if (test_bit(HCI_BREDR_ENABLED, &hdev->dev_flags))
		settings |= MGMT_SETTING_BREDR;

	if (test_bit(HCI_LE_ENABLED, &hdev->dev_flags))
		settings |= MGMT_SETTING_LE;

	if (test_bit(HCI_LINK_SECURITY, &hdev->dev_flags))
		settings |= MGMT_SETTING_LINK_SECURITY;

	if (test_bit(HCI_SSP_ENABLED, &hdev->dev_flags))
		settings |= MGMT_SETTING_SSP;

	if (test_bit(HCI_HS_ENABLED, &hdev->dev_flags))
		settings |= MGMT_SETTING_HS;

	if (test_bit(HCI_ADVERTISING, &hdev->dev_flags))
		settings |= MGMT_SETTING_ADVERTISING;

	if (test_bit(HCI_SC_ENABLED, &hdev->dev_flags))
		settings |= MGMT_SETTING_SECURE_CONN;

	if (test_bit(HCI_KEEP_DEBUG_KEYS, &hdev->dev_flags))
		settings |= MGMT_SETTING_DEBUG_KEYS;

	if (test_bit(HCI_PRIVACY, &hdev->dev_flags))
		settings |= MGMT_SETTING_PRIVACY;

	return settings;
}

#define PNP_INFO_SVCLASS_ID		0x1200

static u8 *create_uuid16_list(struct hci_dev *hdev, u8 *data, ptrdiff_t len)
{
	u8 *ptr = data, *uuids_start = NULL;
	struct bt_uuid *uuid;

	if (len < 4)
		return ptr;

	list_for_each_entry(uuid, &hdev->uuids, list) {
		u16 uuid16;

		if (uuid->size != 16)
			continue;

		uuid16 = get_unaligned_le16(&uuid->uuid[12]);
		if (uuid16 < 0x1100)
			continue;

		if (uuid16 == PNP_INFO_SVCLASS_ID)
			continue;

		if (!uuids_start) {
			uuids_start = ptr;
			uuids_start[0] = 1;
			uuids_start[1] = EIR_UUID16_ALL;
			ptr += 2;
		}

		/* Stop if not enough space to put next UUID */
		if ((ptr - data) + sizeof(u16) > len) {
			uuids_start[1] = EIR_UUID16_SOME;
			break;
		}

		*ptr++ = (uuid16 & 0x00ff);
		*ptr++ = (uuid16 & 0xff00) >> 8;
		uuids_start[0] += sizeof(uuid16);
	}

	return ptr;
}

static u8 *create_uuid32_list(struct hci_dev *hdev, u8 *data, ptrdiff_t len)
{
	u8 *ptr = data, *uuids_start = NULL;
	struct bt_uuid *uuid;

	if (len < 6)
		return ptr;

	list_for_each_entry(uuid, &hdev->uuids, list) {
		if (uuid->size != 32)
			continue;

		if (!uuids_start) {
			uuids_start = ptr;
			uuids_start[0] = 1;
			uuids_start[1] = EIR_UUID32_ALL;
			ptr += 2;
		}

		/* Stop if not enough space to put next UUID */
		if ((ptr - data) + sizeof(u32) > len) {
			uuids_start[1] = EIR_UUID32_SOME;
			break;
		}

		memcpy(ptr, &uuid->uuid[12], sizeof(u32));
		ptr += sizeof(u32);
		uuids_start[0] += sizeof(u32);
	}

	return ptr;
}

static u8 *create_uuid128_list(struct hci_dev *hdev, u8 *data, ptrdiff_t len)
{
	u8 *ptr = data, *uuids_start = NULL;
	struct bt_uuid *uuid;

	if (len < 18)
		return ptr;

	list_for_each_entry(uuid, &hdev->uuids, list) {
		if (uuid->size != 128)
			continue;

		if (!uuids_start) {
			uuids_start = ptr;
			uuids_start[0] = 1;
			uuids_start[1] = EIR_UUID128_ALL;
			ptr += 2;
		}

		/* Stop if not enough space to put next UUID */
		if ((ptr - data) + 16 > len) {
			uuids_start[1] = EIR_UUID128_SOME;
			break;
		}

		memcpy(ptr, uuid->uuid, 16);
		ptr += 16;
		uuids_start[0] += 16;
	}

	return ptr;
}

static struct pending_cmd *mgmt_pending_find(u16 opcode, struct hci_dev *hdev)
{
	struct pending_cmd *cmd;

	list_for_each_entry(cmd, &hdev->mgmt_pending, list) {
		if (cmd->opcode == opcode)
			return cmd;
	}

	return NULL;
}

static struct pending_cmd *mgmt_pending_find_data(u16 opcode,
						  struct hci_dev *hdev,
						  const void *data)
{
	struct pending_cmd *cmd;

	list_for_each_entry(cmd, &hdev->mgmt_pending, list) {
		if (cmd->user_data != data)
			continue;
		if (cmd->opcode == opcode)
			return cmd;
	}

	return NULL;
}

static u8 create_scan_rsp_data(struct hci_dev *hdev, u8 *ptr)
{
	u8 ad_len = 0;
	size_t name_len;

	name_len = strlen(hdev->dev_name);
	if (name_len > 0) {
		size_t max_len = HCI_MAX_AD_LENGTH - ad_len - 2;

		if (name_len > max_len) {
			name_len = max_len;
			ptr[1] = EIR_NAME_SHORT;
		} else
			ptr[1] = EIR_NAME_COMPLETE;

		ptr[0] = name_len + 1;

		memcpy(ptr + 2, hdev->dev_name, name_len);

		ad_len += (name_len + 2);
		ptr += (name_len + 2);
	}

	return ad_len;
}

static void update_scan_rsp_data(struct hci_request *req)
{
	struct hci_dev *hdev = req->hdev;
	struct hci_cp_le_set_scan_rsp_data cp;
	u8 len;

	if (!test_bit(HCI_LE_ENABLED, &hdev->dev_flags))
		return;

	memset(&cp, 0, sizeof(cp));

	len = create_scan_rsp_data(hdev, cp.data);

	if (hdev->scan_rsp_data_len == len &&
	    memcmp(cp.data, hdev->scan_rsp_data, len) == 0)
		return;

	memcpy(hdev->scan_rsp_data, cp.data, sizeof(cp.data));
	hdev->scan_rsp_data_len = len;

	cp.length = len;

	hci_req_add(req, HCI_OP_LE_SET_SCAN_RSP_DATA, sizeof(cp), &cp);
}

static u8 get_adv_discov_flags(struct hci_dev *hdev)
{
	struct pending_cmd *cmd;

	/* If there's a pending mgmt command the flags will not yet have
	 * their final values, so check for this first.
	 */
	cmd = mgmt_pending_find(MGMT_OP_SET_DISCOVERABLE, hdev);
	if (cmd) {
		struct mgmt_mode *cp = cmd->param;
		if (cp->val == 0x01)
			return LE_AD_GENERAL;
		else if (cp->val == 0x02)
			return LE_AD_LIMITED;
	} else {
		if (test_bit(HCI_LIMITED_DISCOVERABLE, &hdev->dev_flags))
			return LE_AD_LIMITED;
		else if (test_bit(HCI_DISCOVERABLE, &hdev->dev_flags))
			return LE_AD_GENERAL;
	}

	return 0;
}

static u8 create_adv_data(struct hci_dev *hdev, u8 *ptr)
{
	u8 ad_len = 0, flags = 0;

	flags |= get_adv_discov_flags(hdev);

	if (!test_bit(HCI_BREDR_ENABLED, &hdev->dev_flags))
		flags |= LE_AD_NO_BREDR;

	if (flags) {
		BT_DBG("adv flags 0x%02x", flags);

		ptr[0] = 2;
		ptr[1] = EIR_FLAGS;
		ptr[2] = flags;

		ad_len += 3;
		ptr += 3;
	}

	if (hdev->adv_tx_power != HCI_TX_POWER_INVALID) {
		ptr[0] = 2;
		ptr[1] = EIR_TX_POWER;
		ptr[2] = (u8) hdev->adv_tx_power;

		ad_len += 3;
		ptr += 3;
	}

	return ad_len;
}

static void update_adv_data(struct hci_request *req)
{
	struct hci_dev *hdev = req->hdev;
	struct hci_cp_le_set_adv_data cp;
	u8 len;

	if (!test_bit(HCI_LE_ENABLED, &hdev->dev_flags))
		return;

	memset(&cp, 0, sizeof(cp));

	len = create_adv_data(hdev, cp.data);

	if (hdev->adv_data_len == len &&
	    memcmp(cp.data, hdev->adv_data, len) == 0)
		return;

	memcpy(hdev->adv_data, cp.data, sizeof(cp.data));
	hdev->adv_data_len = len;

	cp.length = len;

	hci_req_add(req, HCI_OP_LE_SET_ADV_DATA, sizeof(cp), &cp);
}

int mgmt_update_adv_data(struct hci_dev *hdev)
{
	struct hci_request req;

	hci_req_init(&req, hdev);
	update_adv_data(&req);

	return hci_req_run(&req, NULL);
}

static void create_eir(struct hci_dev *hdev, u8 *data)
{
	u8 *ptr = data;
	size_t name_len;

	name_len = strlen(hdev->dev_name);

	if (name_len > 0) {
		/* EIR Data type */
		if (name_len > 48) {
			name_len = 48;
			ptr[1] = EIR_NAME_SHORT;
		} else
			ptr[1] = EIR_NAME_COMPLETE;

		/* EIR Data length */
		ptr[0] = name_len + 1;

		memcpy(ptr + 2, hdev->dev_name, name_len);

		ptr += (name_len + 2);
	}

	if (hdev->inq_tx_power != HCI_TX_POWER_INVALID) {
		ptr[0] = 2;
		ptr[1] = EIR_TX_POWER;
		ptr[2] = (u8) hdev->inq_tx_power;

		ptr += 3;
	}

	if (hdev->devid_source > 0) {
		ptr[0] = 9;
		ptr[1] = EIR_DEVICE_ID;

		put_unaligned_le16(hdev->devid_source, ptr + 2);
		put_unaligned_le16(hdev->devid_vendor, ptr + 4);
		put_unaligned_le16(hdev->devid_product, ptr + 6);
		put_unaligned_le16(hdev->devid_version, ptr + 8);

		ptr += 10;
	}

	ptr = create_uuid16_list(hdev, ptr, HCI_MAX_EIR_LENGTH - (ptr - data));
	ptr = create_uuid32_list(hdev, ptr, HCI_MAX_EIR_LENGTH - (ptr - data));
	ptr = create_uuid128_list(hdev, ptr, HCI_MAX_EIR_LENGTH - (ptr - data));
}

static void update_eir(struct hci_request *req)
{
	struct hci_dev *hdev = req->hdev;
	struct hci_cp_write_eir cp;

	if (!hdev_is_powered(hdev))
		return;

	if (!lmp_ext_inq_capable(hdev))
		return;

	if (!test_bit(HCI_SSP_ENABLED, &hdev->dev_flags))
		return;

	if (test_bit(HCI_SERVICE_CACHE, &hdev->dev_flags))
		return;

	memset(&cp, 0, sizeof(cp));

	create_eir(hdev, cp.data);

	if (memcmp(cp.data, hdev->eir, sizeof(cp.data)) == 0)
		return;

	memcpy(hdev->eir, cp.data, sizeof(cp.data));

	hci_req_add(req, HCI_OP_WRITE_EIR, sizeof(cp), &cp);
}

static u8 get_service_classes(struct hci_dev *hdev)
{
	struct bt_uuid *uuid;
	u8 val = 0;

	list_for_each_entry(uuid, &hdev->uuids, list)
		val |= uuid->svc_hint;

	return val;
}

static void update_class(struct hci_request *req)
{
	struct hci_dev *hdev = req->hdev;
	u8 cod[3];

	BT_DBG("%s", hdev->name);

	if (!hdev_is_powered(hdev))
		return;

	if (!test_bit(HCI_BREDR_ENABLED, &hdev->dev_flags))
		return;

	if (test_bit(HCI_SERVICE_CACHE, &hdev->dev_flags))
		return;

	cod[0] = hdev->minor_class;
	cod[1] = hdev->major_class;
	cod[2] = get_service_classes(hdev);

	if (test_bit(HCI_LIMITED_DISCOVERABLE, &hdev->dev_flags))
		cod[1] |= 0x20;

	if (memcmp(cod, hdev->dev_class, 3) == 0)
		return;

	hci_req_add(req, HCI_OP_WRITE_CLASS_OF_DEV, sizeof(cod), cod);
}

static bool get_connectable(struct hci_dev *hdev)
{
	struct pending_cmd *cmd;

	/* If there's a pending mgmt command the flag will not yet have
	 * it's final value, so check for this first.
	 */
	cmd = mgmt_pending_find(MGMT_OP_SET_CONNECTABLE, hdev);
	if (cmd) {
		struct mgmt_mode *cp = cmd->param;
		return cp->val;
	}

	return test_bit(HCI_CONNECTABLE, &hdev->dev_flags);
}

static void disable_advertising(struct hci_request *req)
{
	u8 enable = 0x00;

	hci_req_add(req, HCI_OP_LE_SET_ADV_ENABLE, sizeof(enable), &enable);
}

static void enable_advertising(struct hci_request *req)
{
	struct hci_dev *hdev = req->hdev;
	struct hci_cp_le_set_adv_param cp;
	u8 own_addr_type, enable = 0x01;
	bool connectable;

	if (hci_conn_num(hdev, LE_LINK) > 0)
		return;

	if (test_bit(HCI_LE_ADV, &hdev->dev_flags))
		disable_advertising(req);

	/* Clear the HCI_LE_ADV bit temporarily so that the
	 * hci_update_random_address knows that it's safe to go ahead
	 * and write a new random address. The flag will be set back on
	 * as soon as the SET_ADV_ENABLE HCI command completes.
	 */
	clear_bit(HCI_LE_ADV, &hdev->dev_flags);

	connectable = get_connectable(hdev);

	/* Set require_privacy to true only when non-connectable
	 * advertising is used. In that case it is fine to use a
	 * non-resolvable private address.
	 */
	if (hci_update_random_address(req, !connectable, &own_addr_type) < 0)
		return;

	memset(&cp, 0, sizeof(cp));
	cp.min_interval = cpu_to_le16(hdev->le_adv_min_interval);
	cp.max_interval = cpu_to_le16(hdev->le_adv_max_interval);
	cp.type = connectable ? LE_ADV_IND : LE_ADV_NONCONN_IND;
	cp.own_address_type = own_addr_type;
	cp.channel_map = hdev->le_adv_channel_map;

	hci_req_add(req, HCI_OP_LE_SET_ADV_PARAM, sizeof(cp), &cp);

	hci_req_add(req, HCI_OP_LE_SET_ADV_ENABLE, sizeof(enable), &enable);
}

static void service_cache_off(struct work_struct *work)
{
	struct hci_dev *hdev = container_of(work, struct hci_dev,
					    service_cache.work);
	struct hci_request req;

	if (!test_and_clear_bit(HCI_SERVICE_CACHE, &hdev->dev_flags))
		return;

	hci_req_init(&req, hdev);

	hci_dev_lock(hdev);

	update_eir(&req);
	update_class(&req);

	hci_dev_unlock(hdev);

	hci_req_run(&req, NULL);
}

static void rpa_expired(struct work_struct *work)
{
	struct hci_dev *hdev = container_of(work, struct hci_dev,
					    rpa_expired.work);
	struct hci_request req;

	BT_DBG("");

	set_bit(HCI_RPA_EXPIRED, &hdev->dev_flags);

	if (!test_bit(HCI_ADVERTISING, &hdev->dev_flags))
		return;

	/* The generation of a new RPA and programming it into the
	 * controller happens in the enable_advertising() function.
	 */
	hci_req_init(&req, hdev);
	enable_advertising(&req);
	hci_req_run(&req, NULL);
}

static void mgmt_init_hdev(struct sock *sk, struct hci_dev *hdev)
{
	if (test_and_set_bit(HCI_MGMT, &hdev->dev_flags))
		return;

	INIT_DELAYED_WORK(&hdev->service_cache, service_cache_off);
	INIT_DELAYED_WORK(&hdev->rpa_expired, rpa_expired);

	/* Non-mgmt controlled devices get this bit set
	 * implicitly so that pairing works for them, however
	 * for mgmt we require user-space to explicitly enable
	 * it
	 */
	clear_bit(HCI_BONDABLE, &hdev->dev_flags);
}

static int read_controller_info(struct sock *sk, struct hci_dev *hdev,
				void *data, u16 data_len)
{
	struct mgmt_rp_read_info rp;

	BT_DBG("sock %p %s", sk, hdev->name);

	hci_dev_lock(hdev);

	memset(&rp, 0, sizeof(rp));

	bacpy(&rp.bdaddr, &hdev->bdaddr);

	rp.version = hdev->hci_ver;
	rp.manufacturer = cpu_to_le16(hdev->manufacturer);

	rp.supported_settings = cpu_to_le32(get_supported_settings(hdev));
	rp.current_settings = cpu_to_le32(get_current_settings(hdev));

	memcpy(rp.dev_class, hdev->dev_class, 3);

	memcpy(rp.name, hdev->dev_name, sizeof(hdev->dev_name));
	memcpy(rp.short_name, hdev->short_name, sizeof(hdev->short_name));

	hci_dev_unlock(hdev);

	return cmd_complete(sk, hdev->id, MGMT_OP_READ_INFO, 0, &rp,
			    sizeof(rp));
}

static void mgmt_pending_free(struct pending_cmd *cmd)
{
	sock_put(cmd->sk);
	kfree(cmd->param);
	kfree(cmd);
}

static struct pending_cmd *mgmt_pending_add(struct sock *sk, u16 opcode,
					    struct hci_dev *hdev, void *data,
					    u16 len)
{
	struct pending_cmd *cmd;

	cmd = kzalloc(sizeof(*cmd), GFP_KERNEL);
	if (!cmd)
		return NULL;

	cmd->opcode = opcode;
	cmd->index = hdev->id;

	cmd->param = kmemdup(data, len, GFP_KERNEL);
	if (!cmd->param) {
		kfree(cmd);
		return NULL;
	}

	cmd->param_len = len;

	cmd->sk = sk;
	sock_hold(sk);

	list_add(&cmd->list, &hdev->mgmt_pending);

	return cmd;
}

static void mgmt_pending_foreach(u16 opcode, struct hci_dev *hdev,
				 void (*cb)(struct pending_cmd *cmd,
					    void *data),
				 void *data)
{
	struct pending_cmd *cmd, *tmp;

	list_for_each_entry_safe(cmd, tmp, &hdev->mgmt_pending, list) {
		if (opcode > 0 && cmd->opcode != opcode)
			continue;

		cb(cmd, data);
	}
}

static void mgmt_pending_remove(struct pending_cmd *cmd)
{
	list_del(&cmd->list);
	mgmt_pending_free(cmd);
}

static int send_settings_rsp(struct sock *sk, u16 opcode, struct hci_dev *hdev)
{
	__le32 settings = cpu_to_le32(get_current_settings(hdev));

	return cmd_complete(sk, hdev->id, opcode, 0, &settings,
			    sizeof(settings));
}

static void clean_up_hci_complete(struct hci_dev *hdev, u8 status)
{
	BT_DBG("%s status 0x%02x", hdev->name, status);

	if (hci_conn_count(hdev) == 0) {
		cancel_delayed_work(&hdev->power_off);
		queue_work(hdev->req_workqueue, &hdev->power_off.work);
	}
}

static bool hci_stop_discovery(struct hci_request *req)
{
	struct hci_dev *hdev = req->hdev;
	struct hci_cp_remote_name_req_cancel cp;
	struct inquiry_entry *e;

	switch (hdev->discovery.state) {
	case DISCOVERY_FINDING:
		if (test_bit(HCI_INQUIRY, &hdev->flags)) {
			hci_req_add(req, HCI_OP_INQUIRY_CANCEL, 0, NULL);
		} else {
			cancel_delayed_work(&hdev->le_scan_disable);
			hci_req_add_le_scan_disable(req);
		}

		return true;

	case DISCOVERY_RESOLVING:
		e = hci_inquiry_cache_lookup_resolve(hdev, BDADDR_ANY,
						     NAME_PENDING);
		if (!e)
			break;

		bacpy(&cp.bdaddr, &e->data.bdaddr);
		hci_req_add(req, HCI_OP_REMOTE_NAME_REQ_CANCEL, sizeof(cp),
			    &cp);

		return true;

	default:
		/* Passive scanning */
		if (test_bit(HCI_LE_SCAN, &hdev->dev_flags)) {
			hci_req_add_le_scan_disable(req);
			return true;
		}

		break;
	}

	return false;
}

static int clean_up_hci_state(struct hci_dev *hdev)
{
	struct hci_request req;
	struct hci_conn *conn;
	bool discov_stopped;
	int err;

	hci_req_init(&req, hdev);

	if (test_bit(HCI_ISCAN, &hdev->flags) ||
	    test_bit(HCI_PSCAN, &hdev->flags)) {
		u8 scan = 0x00;
		hci_req_add(&req, HCI_OP_WRITE_SCAN_ENABLE, 1, &scan);
	}

	if (test_bit(HCI_LE_ADV, &hdev->dev_flags))
		disable_advertising(&req);

	discov_stopped = hci_stop_discovery(&req);

	list_for_each_entry(conn, &hdev->conn_hash.list, list) {
		struct hci_cp_disconnect dc;
		struct hci_cp_reject_conn_req rej;

		switch (conn->state) {
		case BT_CONNECTED:
		case BT_CONFIG:
			dc.handle = cpu_to_le16(conn->handle);
			dc.reason = 0x15; /* Terminated due to Power Off */
			hci_req_add(&req, HCI_OP_DISCONNECT, sizeof(dc), &dc);
			break;
		case BT_CONNECT:
			if (conn->type == LE_LINK)
				hci_req_add(&req, HCI_OP_LE_CREATE_CONN_CANCEL,
					    0, NULL);
			else if (conn->type == ACL_LINK)
				hci_req_add(&req, HCI_OP_CREATE_CONN_CANCEL,
					    6, &conn->dst);
			break;
		case BT_CONNECT2:
			bacpy(&rej.bdaddr, &conn->dst);
			rej.reason = 0x15; /* Terminated due to Power Off */
			if (conn->type == ACL_LINK)
				hci_req_add(&req, HCI_OP_REJECT_CONN_REQ,
					    sizeof(rej), &rej);
			else if (conn->type == SCO_LINK)
				hci_req_add(&req, HCI_OP_REJECT_SYNC_CONN_REQ,
					    sizeof(rej), &rej);
			break;
		}
	}

	err = hci_req_run(&req, clean_up_hci_complete);
	if (!err && discov_stopped)
		hci_discovery_set_state(hdev, DISCOVERY_STOPPING);

	return err;
}

static int set_powered(struct sock *sk, struct hci_dev *hdev, void *data,
		       u16 len)
{
	struct mgmt_mode *cp = data;
	struct pending_cmd *cmd;
	int err;

	BT_DBG("request for %s", hdev->name);

	if (cp->val != 0x00 && cp->val != 0x01)
		return cmd_status(sk, hdev->id, MGMT_OP_SET_POWERED,
				  MGMT_STATUS_INVALID_PARAMS);

	hci_dev_lock(hdev);

	if (mgmt_pending_find(MGMT_OP_SET_POWERED, hdev)) {
		err = cmd_status(sk, hdev->id, MGMT_OP_SET_POWERED,
				 MGMT_STATUS_BUSY);
		goto failed;
	}

	if (test_and_clear_bit(HCI_AUTO_OFF, &hdev->dev_flags)) {
		cancel_delayed_work(&hdev->power_off);

		if (cp->val) {
			mgmt_pending_add(sk, MGMT_OP_SET_POWERED, hdev,
					 data, len);
			err = mgmt_powered(hdev, 1);
			goto failed;
		}
	}

	if (!!cp->val == hdev_is_powered(hdev)) {
		err = send_settings_rsp(sk, MGMT_OP_SET_POWERED, hdev);
		goto failed;
	}

	cmd = mgmt_pending_add(sk, MGMT_OP_SET_POWERED, hdev, data, len);
	if (!cmd) {
		err = -ENOMEM;
		goto failed;
	}

	if (cp->val) {
		queue_work(hdev->req_workqueue, &hdev->power_on);
		err = 0;
	} else {
		/* Disconnect connections, stop scans, etc */
		err = clean_up_hci_state(hdev);
		if (!err)
			queue_delayed_work(hdev->req_workqueue, &hdev->power_off,
					   HCI_POWER_OFF_TIMEOUT);

		/* ENODATA means there were no HCI commands queued */
		if (err == -ENODATA) {
			cancel_delayed_work(&hdev->power_off);
			queue_work(hdev->req_workqueue, &hdev->power_off.work);
			err = 0;
		}
	}

failed:
	hci_dev_unlock(hdev);
	return err;
}

static int new_settings(struct hci_dev *hdev, struct sock *skip)
{
	__le32 ev;

	ev = cpu_to_le32(get_current_settings(hdev));

	return mgmt_event(MGMT_EV_NEW_SETTINGS, hdev, &ev, sizeof(ev), skip);
}

int mgmt_new_settings(struct hci_dev *hdev)
{
	return new_settings(hdev, NULL);
}

struct cmd_lookup {
	struct sock *sk;
	struct hci_dev *hdev;
	u8 mgmt_status;
};

static void settings_rsp(struct pending_cmd *cmd, void *data)
{
	struct cmd_lookup *match = data;

	send_settings_rsp(cmd->sk, cmd->opcode, match->hdev);

	list_del(&cmd->list);

	if (match->sk == NULL) {
		match->sk = cmd->sk;
		sock_hold(match->sk);
	}

	mgmt_pending_free(cmd);
}

static void cmd_status_rsp(struct pending_cmd *cmd, void *data)
{
	u8 *status = data;

	cmd_status(cmd->sk, cmd->index, cmd->opcode, *status);
	mgmt_pending_remove(cmd);
}

static void cmd_complete_rsp(struct pending_cmd *cmd, void *data)
{
	if (cmd->cmd_complete) {
		u8 *status = data;

		cmd->cmd_complete(cmd, *status);
		mgmt_pending_remove(cmd);

		return;
	}

	cmd_status_rsp(cmd, data);
}

static int generic_cmd_complete(struct pending_cmd *cmd, u8 status)
{
	return cmd_complete(cmd->sk, cmd->index, cmd->opcode, status,
			    cmd->param, cmd->param_len);
}

static int addr_cmd_complete(struct pending_cmd *cmd, u8 status)
{
	return cmd_complete(cmd->sk, cmd->index, cmd->opcode, status, cmd->param,
			    sizeof(struct mgmt_addr_info));
}

static u8 mgmt_bredr_support(struct hci_dev *hdev)
{
	if (!lmp_bredr_capable(hdev))
		return MGMT_STATUS_NOT_SUPPORTED;
	else if (!test_bit(HCI_BREDR_ENABLED, &hdev->dev_flags))
		return MGMT_STATUS_REJECTED;
	else
		return MGMT_STATUS_SUCCESS;
}

static u8 mgmt_le_support(struct hci_dev *hdev)
{
	if (!lmp_le_capable(hdev))
		return MGMT_STATUS_NOT_SUPPORTED;
	else if (!test_bit(HCI_LE_ENABLED, &hdev->dev_flags))
		return MGMT_STATUS_REJECTED;
	else
		return MGMT_STATUS_SUCCESS;
}

static void set_discoverable_complete(struct hci_dev *hdev, u8 status)
{
	struct pending_cmd *cmd;
	struct mgmt_mode *cp;
	struct hci_request req;
	bool changed;

	BT_DBG("status 0x%02x", status);

	hci_dev_lock(hdev);

	cmd = mgmt_pending_find(MGMT_OP_SET_DISCOVERABLE, hdev);
	if (!cmd)
		goto unlock;

	if (status) {
		u8 mgmt_err = mgmt_status(status);
		cmd_status(cmd->sk, cmd->index, cmd->opcode, mgmt_err);
		clear_bit(HCI_LIMITED_DISCOVERABLE, &hdev->dev_flags);
		goto remove_cmd;
	}

	cp = cmd->param;
	if (cp->val) {
		changed = !test_and_set_bit(HCI_DISCOVERABLE,
					    &hdev->dev_flags);

		if (hdev->discov_timeout > 0) {
			int to = msecs_to_jiffies(hdev->discov_timeout * 1000);
			queue_delayed_work(hdev->workqueue, &hdev->discov_off,
					   to);
		}
	} else {
		changed = test_and_clear_bit(HCI_DISCOVERABLE,
					     &hdev->dev_flags);
	}

	send_settings_rsp(cmd->sk, MGMT_OP_SET_DISCOVERABLE, hdev);

	if (changed)
		new_settings(hdev, cmd->sk);

	/* When the discoverable mode gets changed, make sure
	 * that class of device has the limited discoverable
	 * bit correctly set. Also update page scan based on whitelist
	 * entries.
	 */
	hci_req_init(&req, hdev);
	__hci_update_page_scan(&req);
	update_class(&req);
	hci_req_run(&req, NULL);

remove_cmd:
	mgmt_pending_remove(cmd);

unlock:
	hci_dev_unlock(hdev);
}

static int set_discoverable(struct sock *sk, struct hci_dev *hdev, void *data,
			    u16 len)
{
	struct mgmt_cp_set_discoverable *cp = data;
	struct pending_cmd *cmd;
	struct hci_request req;
	u16 timeout;
	u8 scan;
	int err;

	BT_DBG("request for %s", hdev->name);

	if (!test_bit(HCI_LE_ENABLED, &hdev->dev_flags) &&
	    !test_bit(HCI_BREDR_ENABLED, &hdev->dev_flags))
		return cmd_status(sk, hdev->id, MGMT_OP_SET_DISCOVERABLE,
				  MGMT_STATUS_REJECTED);

	if (cp->val != 0x00 && cp->val != 0x01 && cp->val != 0x02)
		return cmd_status(sk, hdev->id, MGMT_OP_SET_DISCOVERABLE,
				  MGMT_STATUS_INVALID_PARAMS);

	timeout = __le16_to_cpu(cp->timeout);

	/* Disabling discoverable requires that no timeout is set,
	 * and enabling limited discoverable requires a timeout.
	 */
	if ((cp->val == 0x00 && timeout > 0) ||
	    (cp->val == 0x02 && timeout == 0))
		return cmd_status(sk, hdev->id, MGMT_OP_SET_DISCOVERABLE,
				  MGMT_STATUS_INVALID_PARAMS);

	hci_dev_lock(hdev);

	if (!hdev_is_powered(hdev) && timeout > 0) {
		err = cmd_status(sk, hdev->id, MGMT_OP_SET_DISCOVERABLE,
				 MGMT_STATUS_NOT_POWERED);
		goto failed;
	}

	if (mgmt_pending_find(MGMT_OP_SET_DISCOVERABLE, hdev) ||
	    mgmt_pending_find(MGMT_OP_SET_CONNECTABLE, hdev)) {
		err = cmd_status(sk, hdev->id, MGMT_OP_SET_DISCOVERABLE,
				 MGMT_STATUS_BUSY);
		goto failed;
	}

	if (!test_bit(HCI_CONNECTABLE, &hdev->dev_flags)) {
		err = cmd_status(sk, hdev->id, MGMT_OP_SET_DISCOVERABLE,
				 MGMT_STATUS_REJECTED);
		goto failed;
	}

	if (!hdev_is_powered(hdev)) {
		bool changed = false;

		/* Setting limited discoverable when powered off is
		 * not a valid operation since it requires a timeout
		 * and so no need to check HCI_LIMITED_DISCOVERABLE.
		 */
		if (!!cp->val != test_bit(HCI_DISCOVERABLE, &hdev->dev_flags)) {
			change_bit(HCI_DISCOVERABLE, &hdev->dev_flags);
			changed = true;
		}

		err = send_settings_rsp(sk, MGMT_OP_SET_DISCOVERABLE, hdev);
		if (err < 0)
			goto failed;

		if (changed)
			err = new_settings(hdev, sk);

		goto failed;
	}

	/* If the current mode is the same, then just update the timeout
	 * value with the new value. And if only the timeout gets updated,
	 * then no need for any HCI transactions.
	 */
	if (!!cp->val == test_bit(HCI_DISCOVERABLE, &hdev->dev_flags) &&
	    (cp->val == 0x02) == test_bit(HCI_LIMITED_DISCOVERABLE,
					  &hdev->dev_flags)) {
		cancel_delayed_work(&hdev->discov_off);
		hdev->discov_timeout = timeout;

		if (cp->val && hdev->discov_timeout > 0) {
			int to = msecs_to_jiffies(hdev->discov_timeout * 1000);
			queue_delayed_work(hdev->workqueue, &hdev->discov_off,
					   to);
		}

		err = send_settings_rsp(sk, MGMT_OP_SET_DISCOVERABLE, hdev);
		goto failed;
	}

	cmd = mgmt_pending_add(sk, MGMT_OP_SET_DISCOVERABLE, hdev, data, len);
	if (!cmd) {
		err = -ENOMEM;
		goto failed;
	}

	/* Cancel any potential discoverable timeout that might be
	 * still active and store new timeout value. The arming of
	 * the timeout happens in the complete handler.
	 */
	cancel_delayed_work(&hdev->discov_off);
	hdev->discov_timeout = timeout;

	/* Limited discoverable mode */
	if (cp->val == 0x02)
		set_bit(HCI_LIMITED_DISCOVERABLE, &hdev->dev_flags);
	else
		clear_bit(HCI_LIMITED_DISCOVERABLE, &hdev->dev_flags);

	hci_req_init(&req, hdev);

	/* The procedure for LE-only controllers is much simpler - just
	 * update the advertising data.
	 */
	if (!test_bit(HCI_BREDR_ENABLED, &hdev->dev_flags))
		goto update_ad;

	scan = SCAN_PAGE;

	if (cp->val) {
		struct hci_cp_write_current_iac_lap hci_cp;

		if (cp->val == 0x02) {
			/* Limited discoverable mode */
			hci_cp.num_iac = min_t(u8, hdev->num_iac, 2);
			hci_cp.iac_lap[0] = 0x00;	/* LIAC */
			hci_cp.iac_lap[1] = 0x8b;
			hci_cp.iac_lap[2] = 0x9e;
			hci_cp.iac_lap[3] = 0x33;	/* GIAC */
			hci_cp.iac_lap[4] = 0x8b;
			hci_cp.iac_lap[5] = 0x9e;
		} else {
			/* General discoverable mode */
			hci_cp.num_iac = 1;
			hci_cp.iac_lap[0] = 0x33;	/* GIAC */
			hci_cp.iac_lap[1] = 0x8b;
			hci_cp.iac_lap[2] = 0x9e;
		}

		hci_req_add(&req, HCI_OP_WRITE_CURRENT_IAC_LAP,
			    (hci_cp.num_iac * 3) + 1, &hci_cp);

		scan |= SCAN_INQUIRY;
	} else {
		clear_bit(HCI_LIMITED_DISCOVERABLE, &hdev->dev_flags);
	}

	hci_req_add(&req, HCI_OP_WRITE_SCAN_ENABLE, sizeof(scan), &scan);

update_ad:
	update_adv_data(&req);

	err = hci_req_run(&req, set_discoverable_complete);
	if (err < 0)
		mgmt_pending_remove(cmd);

failed:
	hci_dev_unlock(hdev);
	return err;
}

static void write_fast_connectable(struct hci_request *req, bool enable)
{
	struct hci_dev *hdev = req->hdev;
	struct hci_cp_write_page_scan_activity acp;
	u8 type;

	if (!test_bit(HCI_BREDR_ENABLED, &hdev->dev_flags))
		return;

	if (hdev->hci_ver < BLUETOOTH_VER_1_2)
		return;

	if (enable) {
		type = PAGE_SCAN_TYPE_INTERLACED;

		/* 160 msec page scan interval */
		acp.interval = cpu_to_le16(0x0100);
	} else {
		type = PAGE_SCAN_TYPE_STANDARD;	/* default */

		/* default 1.28 sec page scan */
		acp.interval = cpu_to_le16(0x0800);
	}

	acp.window = cpu_to_le16(0x0012);

	if (__cpu_to_le16(hdev->page_scan_interval) != acp.interval ||
	    __cpu_to_le16(hdev->page_scan_window) != acp.window)
		hci_req_add(req, HCI_OP_WRITE_PAGE_SCAN_ACTIVITY,
			    sizeof(acp), &acp);

	if (hdev->page_scan_type != type)
		hci_req_add(req, HCI_OP_WRITE_PAGE_SCAN_TYPE, 1, &type);
}

static void set_connectable_complete(struct hci_dev *hdev, u8 status)
{
	struct pending_cmd *cmd;
	struct mgmt_mode *cp;
	bool conn_changed, discov_changed;

	BT_DBG("status 0x%02x", status);

	hci_dev_lock(hdev);

	cmd = mgmt_pending_find(MGMT_OP_SET_CONNECTABLE, hdev);
	if (!cmd)
		goto unlock;

	if (status) {
		u8 mgmt_err = mgmt_status(status);
		cmd_status(cmd->sk, cmd->index, cmd->opcode, mgmt_err);
		goto remove_cmd;
	}

	cp = cmd->param;
	if (cp->val) {
		conn_changed = !test_and_set_bit(HCI_CONNECTABLE,
						 &hdev->dev_flags);
		discov_changed = false;
	} else {
		conn_changed = test_and_clear_bit(HCI_CONNECTABLE,
						  &hdev->dev_flags);
		discov_changed = test_and_clear_bit(HCI_DISCOVERABLE,
						    &hdev->dev_flags);
	}

	send_settings_rsp(cmd->sk, MGMT_OP_SET_CONNECTABLE, hdev);

	if (conn_changed || discov_changed) {
		new_settings(hdev, cmd->sk);
		hci_update_page_scan(hdev);
		if (discov_changed)
			mgmt_update_adv_data(hdev);
		hci_update_background_scan(hdev);
	}

remove_cmd:
	mgmt_pending_remove(cmd);

unlock:
	hci_dev_unlock(hdev);
}

static int set_connectable_update_settings(struct hci_dev *hdev,
					   struct sock *sk, u8 val)
{
	bool changed = false;
	int err;

	if (!!val != test_bit(HCI_CONNECTABLE, &hdev->dev_flags))
		changed = true;

	if (val) {
		set_bit(HCI_CONNECTABLE, &hdev->dev_flags);
	} else {
		clear_bit(HCI_CONNECTABLE, &hdev->dev_flags);
		clear_bit(HCI_DISCOVERABLE, &hdev->dev_flags);
	}

	err = send_settings_rsp(sk, MGMT_OP_SET_CONNECTABLE, hdev);
	if (err < 0)
		return err;

	if (changed) {
		hci_update_page_scan(hdev);
		hci_update_background_scan(hdev);
		return new_settings(hdev, sk);
	}

	return 0;
}

static int set_connectable(struct sock *sk, struct hci_dev *hdev, void *data,
			   u16 len)
{
	struct mgmt_mode *cp = data;
	struct pending_cmd *cmd;
	struct hci_request req;
	u8 scan;
	int err;

	BT_DBG("request for %s", hdev->name);

	if (!test_bit(HCI_LE_ENABLED, &hdev->dev_flags) &&
	    !test_bit(HCI_BREDR_ENABLED, &hdev->dev_flags))
		return cmd_status(sk, hdev->id, MGMT_OP_SET_CONNECTABLE,
				  MGMT_STATUS_REJECTED);

	if (cp->val != 0x00 && cp->val != 0x01)
		return cmd_status(sk, hdev->id, MGMT_OP_SET_CONNECTABLE,
				  MGMT_STATUS_INVALID_PARAMS);

	hci_dev_lock(hdev);

	if (!hdev_is_powered(hdev)) {
		err = set_connectable_update_settings(hdev, sk, cp->val);
		goto failed;
	}

	if (mgmt_pending_find(MGMT_OP_SET_DISCOVERABLE, hdev) ||
	    mgmt_pending_find(MGMT_OP_SET_CONNECTABLE, hdev)) {
		err = cmd_status(sk, hdev->id, MGMT_OP_SET_CONNECTABLE,
				 MGMT_STATUS_BUSY);
		goto failed;
	}

	cmd = mgmt_pending_add(sk, MGMT_OP_SET_CONNECTABLE, hdev, data, len);
	if (!cmd) {
		err = -ENOMEM;
		goto failed;
	}

	hci_req_init(&req, hdev);

	/* If BR/EDR is not enabled and we disable advertising as a
	 * by-product of disabling connectable, we need to update the
	 * advertising flags.
	 */
	if (!test_bit(HCI_BREDR_ENABLED, &hdev->dev_flags)) {
		if (!cp->val) {
			clear_bit(HCI_LIMITED_DISCOVERABLE, &hdev->dev_flags);
			clear_bit(HCI_DISCOVERABLE, &hdev->dev_flags);
		}
		update_adv_data(&req);
	} else if (cp->val != test_bit(HCI_PSCAN, &hdev->flags)) {
		if (cp->val) {
			scan = SCAN_PAGE;
		} else {
			/* If we don't have any whitelist entries just
			 * disable all scanning. If there are entries
			 * and we had both page and inquiry scanning
			 * enabled then fall back to only page scanning.
			 * Otherwise no changes are needed.
			 */
			if (list_empty(&hdev->whitelist))
				scan = SCAN_DISABLED;
			else if (test_bit(HCI_ISCAN, &hdev->flags))
				scan = SCAN_PAGE;
			else
				goto no_scan_update;

			if (test_bit(HCI_ISCAN, &hdev->flags) &&
			    hdev->discov_timeout > 0)
				cancel_delayed_work(&hdev->discov_off);
		}

		hci_req_add(&req, HCI_OP_WRITE_SCAN_ENABLE, 1, &scan);
	}

no_scan_update:
	/* If we're going from non-connectable to connectable or
	 * vice-versa when fast connectable is enabled ensure that fast
	 * connectable gets disabled. write_fast_connectable won't do
	 * anything if the page scan parameters are already what they
	 * should be.
	 */
	if (cp->val || test_bit(HCI_FAST_CONNECTABLE, &hdev->dev_flags))
		write_fast_connectable(&req, false);

	/* Update the advertising parameters if necessary */
	if (test_bit(HCI_ADVERTISING, &hdev->dev_flags))
		enable_advertising(&req);

	err = hci_req_run(&req, set_connectable_complete);
	if (err < 0) {
		mgmt_pending_remove(cmd);
		if (err == -ENODATA)
			err = set_connectable_update_settings(hdev, sk,
							      cp->val);
		goto failed;
	}

failed:
	hci_dev_unlock(hdev);
	return err;
}

static int set_bondable(struct sock *sk, struct hci_dev *hdev, void *data,
			u16 len)
{
	struct mgmt_mode *cp = data;
	bool changed;
	int err;

	BT_DBG("request for %s", hdev->name);

	if (cp->val != 0x00 && cp->val != 0x01)
		return cmd_status(sk, hdev->id, MGMT_OP_SET_BONDABLE,
				  MGMT_STATUS_INVALID_PARAMS);

	hci_dev_lock(hdev);

	if (cp->val)
		changed = !test_and_set_bit(HCI_BONDABLE, &hdev->dev_flags);
	else
		changed = test_and_clear_bit(HCI_BONDABLE, &hdev->dev_flags);

	err = send_settings_rsp(sk, MGMT_OP_SET_BONDABLE, hdev);
	if (err < 0)
		goto unlock;

	if (changed)
		err = new_settings(hdev, sk);

unlock:
	hci_dev_unlock(hdev);
	return err;
}

static int set_link_security(struct sock *sk, struct hci_dev *hdev, void *data,
			     u16 len)
{
	struct mgmt_mode *cp = data;
	struct pending_cmd *cmd;
	u8 val, status;
	int err;

	BT_DBG("request for %s", hdev->name);

	status = mgmt_bredr_support(hdev);
	if (status)
		return cmd_status(sk, hdev->id, MGMT_OP_SET_LINK_SECURITY,
				  status);

	if (cp->val != 0x00 && cp->val != 0x01)
		return cmd_status(sk, hdev->id, MGMT_OP_SET_LINK_SECURITY,
				  MGMT_STATUS_INVALID_PARAMS);

	hci_dev_lock(hdev);

	if (!hdev_is_powered(hdev)) {
		bool changed = false;

		if (!!cp->val != test_bit(HCI_LINK_SECURITY,
					  &hdev->dev_flags)) {
			change_bit(HCI_LINK_SECURITY, &hdev->dev_flags);
			changed = true;
		}

		err = send_settings_rsp(sk, MGMT_OP_SET_LINK_SECURITY, hdev);
		if (err < 0)
			goto failed;

		if (changed)
			err = new_settings(hdev, sk);

		goto failed;
	}

	if (mgmt_pending_find(MGMT_OP_SET_LINK_SECURITY, hdev)) {
		err = cmd_status(sk, hdev->id, MGMT_OP_SET_LINK_SECURITY,
				 MGMT_STATUS_BUSY);
		goto failed;
	}

	val = !!cp->val;

	if (test_bit(HCI_AUTH, &hdev->flags) == val) {
		err = send_settings_rsp(sk, MGMT_OP_SET_LINK_SECURITY, hdev);
		goto failed;
	}

	cmd = mgmt_pending_add(sk, MGMT_OP_SET_LINK_SECURITY, hdev, data, len);
	if (!cmd) {
		err = -ENOMEM;
		goto failed;
	}

	err = hci_send_cmd(hdev, HCI_OP_WRITE_AUTH_ENABLE, sizeof(val), &val);
	if (err < 0) {
		mgmt_pending_remove(cmd);
		goto failed;
	}

failed:
	hci_dev_unlock(hdev);
	return err;
}

static int set_ssp(struct sock *sk, struct hci_dev *hdev, void *data, u16 len)
{
	struct mgmt_mode *cp = data;
	struct pending_cmd *cmd;
	u8 status;
	int err;

	BT_DBG("request for %s", hdev->name);

	status = mgmt_bredr_support(hdev);
	if (status)
		return cmd_status(sk, hdev->id, MGMT_OP_SET_SSP, status);

	if (!lmp_ssp_capable(hdev))
		return cmd_status(sk, hdev->id, MGMT_OP_SET_SSP,
				  MGMT_STATUS_NOT_SUPPORTED);

	if (cp->val != 0x00 && cp->val != 0x01)
		return cmd_status(sk, hdev->id, MGMT_OP_SET_SSP,
				  MGMT_STATUS_INVALID_PARAMS);

	hci_dev_lock(hdev);

	if (!hdev_is_powered(hdev)) {
		bool changed;

		if (cp->val) {
			changed = !test_and_set_bit(HCI_SSP_ENABLED,
						    &hdev->dev_flags);
		} else {
			changed = test_and_clear_bit(HCI_SSP_ENABLED,
						     &hdev->dev_flags);
			if (!changed)
				changed = test_and_clear_bit(HCI_HS_ENABLED,
							     &hdev->dev_flags);
			else
				clear_bit(HCI_HS_ENABLED, &hdev->dev_flags);
		}

		err = send_settings_rsp(sk, MGMT_OP_SET_SSP, hdev);
		if (err < 0)
			goto failed;

		if (changed)
			err = new_settings(hdev, sk);

		goto failed;
	}

	if (mgmt_pending_find(MGMT_OP_SET_SSP, hdev) ||
	    mgmt_pending_find(MGMT_OP_SET_HS, hdev)) {
		err = cmd_status(sk, hdev->id, MGMT_OP_SET_SSP,
				 MGMT_STATUS_BUSY);
		goto failed;
	}

	if (!!cp->val == test_bit(HCI_SSP_ENABLED, &hdev->dev_flags)) {
		err = send_settings_rsp(sk, MGMT_OP_SET_SSP, hdev);
		goto failed;
	}

	cmd = mgmt_pending_add(sk, MGMT_OP_SET_SSP, hdev, data, len);
	if (!cmd) {
		err = -ENOMEM;
		goto failed;
	}

	if (!cp->val && test_bit(HCI_USE_DEBUG_KEYS, &hdev->dev_flags))
		hci_send_cmd(hdev, HCI_OP_WRITE_SSP_DEBUG_MODE,
			     sizeof(cp->val), &cp->val);

	err = hci_send_cmd(hdev, HCI_OP_WRITE_SSP_MODE, 1, &cp->val);
	if (err < 0) {
		mgmt_pending_remove(cmd);
		goto failed;
	}

failed:
	hci_dev_unlock(hdev);
	return err;
}

static int set_hs(struct sock *sk, struct hci_dev *hdev, void *data, u16 len)
{
	struct mgmt_mode *cp = data;
	bool changed;
	u8 status;
	int err;

	BT_DBG("request for %s", hdev->name);

	status = mgmt_bredr_support(hdev);
	if (status)
		return cmd_status(sk, hdev->id, MGMT_OP_SET_HS, status);

	if (!lmp_ssp_capable(hdev))
		return cmd_status(sk, hdev->id, MGMT_OP_SET_HS,
				  MGMT_STATUS_NOT_SUPPORTED);

	if (!test_bit(HCI_SSP_ENABLED, &hdev->dev_flags))
		return cmd_status(sk, hdev->id, MGMT_OP_SET_HS,
				  MGMT_STATUS_REJECTED);

	if (cp->val != 0x00 && cp->val != 0x01)
		return cmd_status(sk, hdev->id, MGMT_OP_SET_HS,
				  MGMT_STATUS_INVALID_PARAMS);

	hci_dev_lock(hdev);

	if (cp->val) {
		changed = !test_and_set_bit(HCI_HS_ENABLED, &hdev->dev_flags);
	} else {
		if (hdev_is_powered(hdev)) {
			err = cmd_status(sk, hdev->id, MGMT_OP_SET_HS,
					 MGMT_STATUS_REJECTED);
			goto unlock;
		}

		changed = test_and_clear_bit(HCI_HS_ENABLED, &hdev->dev_flags);
	}

	err = send_settings_rsp(sk, MGMT_OP_SET_HS, hdev);
	if (err < 0)
		goto unlock;

	if (changed)
		err = new_settings(hdev, sk);

unlock:
	hci_dev_unlock(hdev);
	return err;
}

static void le_enable_complete(struct hci_dev *hdev, u8 status)
{
	struct cmd_lookup match = { NULL, hdev };

	hci_dev_lock(hdev);

	if (status) {
		u8 mgmt_err = mgmt_status(status);

		mgmt_pending_foreach(MGMT_OP_SET_LE, hdev, cmd_status_rsp,
				     &mgmt_err);
		goto unlock;
	}

	mgmt_pending_foreach(MGMT_OP_SET_LE, hdev, settings_rsp, &match);

	new_settings(hdev, match.sk);

	if (match.sk)
		sock_put(match.sk);

	/* Make sure the controller has a good default for
	 * advertising data. Restrict the update to when LE
	 * has actually been enabled. During power on, the
	 * update in powered_update_hci will take care of it.
	 */
	if (test_bit(HCI_LE_ENABLED, &hdev->dev_flags)) {
		struct hci_request req;

		hci_req_init(&req, hdev);
		update_adv_data(&req);
		update_scan_rsp_data(&req);
		__hci_update_background_scan(&req);
		hci_req_run(&req, NULL);
	}

unlock:
	hci_dev_unlock(hdev);
}

static int set_le(struct sock *sk, struct hci_dev *hdev, void *data, u16 len)
{
	struct mgmt_mode *cp = data;
	struct hci_cp_write_le_host_supported hci_cp;
	struct pending_cmd *cmd;
	struct hci_request req;
	int err;
	u8 val, enabled;

	BT_DBG("request for %s", hdev->name);

	if (!lmp_le_capable(hdev))
		return cmd_status(sk, hdev->id, MGMT_OP_SET_LE,
				  MGMT_STATUS_NOT_SUPPORTED);

	if (cp->val != 0x00 && cp->val != 0x01)
		return cmd_status(sk, hdev->id, MGMT_OP_SET_LE,
				  MGMT_STATUS_INVALID_PARAMS);

	/* LE-only devices do not allow toggling LE on/off */
	if (!test_bit(HCI_BREDR_ENABLED, &hdev->dev_flags))
		return cmd_status(sk, hdev->id, MGMT_OP_SET_LE,
				  MGMT_STATUS_REJECTED);

	hci_dev_lock(hdev);

	val = !!cp->val;
	enabled = lmp_host_le_capable(hdev);

	if (!hdev_is_powered(hdev) || val == enabled) {
		bool changed = false;

		if (val != test_bit(HCI_LE_ENABLED, &hdev->dev_flags)) {
			change_bit(HCI_LE_ENABLED, &hdev->dev_flags);
			changed = true;
		}

		if (!val && test_bit(HCI_ADVERTISING, &hdev->dev_flags)) {
			clear_bit(HCI_ADVERTISING, &hdev->dev_flags);
			changed = true;
		}

		err = send_settings_rsp(sk, MGMT_OP_SET_LE, hdev);
		if (err < 0)
			goto unlock;

		if (changed)
			err = new_settings(hdev, sk);

		goto unlock;
	}

	if (mgmt_pending_find(MGMT_OP_SET_LE, hdev) ||
	    mgmt_pending_find(MGMT_OP_SET_ADVERTISING, hdev)) {
		err = cmd_status(sk, hdev->id, MGMT_OP_SET_LE,
				 MGMT_STATUS_BUSY);
		goto unlock;
	}

	cmd = mgmt_pending_add(sk, MGMT_OP_SET_LE, hdev, data, len);
	if (!cmd) {
		err = -ENOMEM;
		goto unlock;
	}

	hci_req_init(&req, hdev);

	memset(&hci_cp, 0, sizeof(hci_cp));

	if (val) {
		hci_cp.le = val;
		hci_cp.simul = 0x00;
	} else {
		if (test_bit(HCI_LE_ADV, &hdev->dev_flags))
			disable_advertising(&req);
	}

	hci_req_add(&req, HCI_OP_WRITE_LE_HOST_SUPPORTED, sizeof(hci_cp),
		    &hci_cp);

	err = hci_req_run(&req, le_enable_complete);
	if (err < 0)
		mgmt_pending_remove(cmd);

unlock:
	hci_dev_unlock(hdev);
	return err;
}

/* This is a helper function to test for pending mgmt commands that can
 * cause CoD or EIR HCI commands. We can only allow one such pending
 * mgmt command at a time since otherwise we cannot easily track what
 * the current values are, will be, and based on that calculate if a new
 * HCI command needs to be sent and if yes with what value.
 */
static bool pending_eir_or_class(struct hci_dev *hdev)
{
	struct pending_cmd *cmd;

	list_for_each_entry(cmd, &hdev->mgmt_pending, list) {
		switch (cmd->opcode) {
		case MGMT_OP_ADD_UUID:
		case MGMT_OP_REMOVE_UUID:
		case MGMT_OP_SET_DEV_CLASS:
		case MGMT_OP_SET_POWERED:
			return true;
		}
	}

	return false;
}

static const u8 bluetooth_base_uuid[] = {
			0xfb, 0x34, 0x9b, 0x5f, 0x80, 0x00, 0x00, 0x80,
			0x00, 0x10, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
};

static u8 get_uuid_size(const u8 *uuid)
{
	u32 val;

	if (memcmp(uuid, bluetooth_base_uuid, 12))
		return 128;

	val = get_unaligned_le32(&uuid[12]);
	if (val > 0xffff)
		return 32;

	return 16;
}

static void mgmt_class_complete(struct hci_dev *hdev, u16 mgmt_op, u8 status)
{
	struct pending_cmd *cmd;

	hci_dev_lock(hdev);

	cmd = mgmt_pending_find(mgmt_op, hdev);
	if (!cmd)
		goto unlock;

	cmd_complete(cmd->sk, cmd->index, cmd->opcode, mgmt_status(status),
		     hdev->dev_class, 3);

	mgmt_pending_remove(cmd);

unlock:
	hci_dev_unlock(hdev);
}

static void add_uuid_complete(struct hci_dev *hdev, u8 status)
{
	BT_DBG("status 0x%02x", status);

	mgmt_class_complete(hdev, MGMT_OP_ADD_UUID, status);
}

static int add_uuid(struct sock *sk, struct hci_dev *hdev, void *data, u16 len)
{
	struct mgmt_cp_add_uuid *cp = data;
	struct pending_cmd *cmd;
	struct hci_request req;
	struct bt_uuid *uuid;
	int err;

	BT_DBG("request for %s", hdev->name);

	hci_dev_lock(hdev);

	if (pending_eir_or_class(hdev)) {
		err = cmd_status(sk, hdev->id, MGMT_OP_ADD_UUID,
				 MGMT_STATUS_BUSY);
		goto failed;
	}

	uuid = kmalloc(sizeof(*uuid), GFP_KERNEL);
	if (!uuid) {
		err = -ENOMEM;
		goto failed;
	}

	memcpy(uuid->uuid, cp->uuid, 16);
	uuid->svc_hint = cp->svc_hint;
	uuid->size = get_uuid_size(cp->uuid);

	list_add_tail(&uuid->list, &hdev->uuids);

	hci_req_init(&req, hdev);

	update_class(&req);
	update_eir(&req);

	err = hci_req_run(&req, add_uuid_complete);
	if (err < 0) {
		if (err != -ENODATA)
			goto failed;

		err = cmd_complete(sk, hdev->id, MGMT_OP_ADD_UUID, 0,
				   hdev->dev_class, 3);
		goto failed;
	}

	cmd = mgmt_pending_add(sk, MGMT_OP_ADD_UUID, hdev, data, len);
	if (!cmd) {
		err = -ENOMEM;
		goto failed;
	}

	err = 0;

failed:
	hci_dev_unlock(hdev);
	return err;
}

static bool enable_service_cache(struct hci_dev *hdev)
{
	if (!hdev_is_powered(hdev))
		return false;

	if (!test_and_set_bit(HCI_SERVICE_CACHE, &hdev->dev_flags)) {
		queue_delayed_work(hdev->workqueue, &hdev->service_cache,
				   CACHE_TIMEOUT);
		return true;
	}

	return false;
}

static void remove_uuid_complete(struct hci_dev *hdev, u8 status)
{
	BT_DBG("status 0x%02x", status);

	mgmt_class_complete(hdev, MGMT_OP_REMOVE_UUID, status);
}

static int remove_uuid(struct sock *sk, struct hci_dev *hdev, void *data,
		       u16 len)
{
	struct mgmt_cp_remove_uuid *cp = data;
	struct pending_cmd *cmd;
	struct bt_uuid *match, *tmp;
	u8 bt_uuid_any[] = { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };
	struct hci_request req;
	int err, found;

	BT_DBG("request for %s", hdev->name);

	hci_dev_lock(hdev);

	if (pending_eir_or_class(hdev)) {
		err = cmd_status(sk, hdev->id, MGMT_OP_REMOVE_UUID,
				 MGMT_STATUS_BUSY);
		goto unlock;
	}

	if (memcmp(cp->uuid, bt_uuid_any, 16) == 0) {
		hci_uuids_clear(hdev);

		if (enable_service_cache(hdev)) {
			err = cmd_complete(sk, hdev->id, MGMT_OP_REMOVE_UUID,
					   0, hdev->dev_class, 3);
			goto unlock;
		}

		goto update_class;
	}

	found = 0;

	list_for_each_entry_safe(match, tmp, &hdev->uuids, list) {
		if (memcmp(match->uuid, cp->uuid, 16) != 0)
			continue;

		list_del(&match->list);
		kfree(match);
		found++;
	}

	if (found == 0) {
		err = cmd_status(sk, hdev->id, MGMT_OP_REMOVE_UUID,
				 MGMT_STATUS_INVALID_PARAMS);
		goto unlock;
	}

update_class:
	hci_req_init(&req, hdev);

	update_class(&req);
	update_eir(&req);

	err = hci_req_run(&req, remove_uuid_complete);
	if (err < 0) {
		if (err != -ENODATA)
			goto unlock;

		err = cmd_complete(sk, hdev->id, MGMT_OP_REMOVE_UUID, 0,
				   hdev->dev_class, 3);
		goto unlock;
	}

	cmd = mgmt_pending_add(sk, MGMT_OP_REMOVE_UUID, hdev, data, len);
	if (!cmd) {
		err = -ENOMEM;
		goto unlock;
	}

	err = 0;

unlock:
	hci_dev_unlock(hdev);
	return err;
}

static void set_class_complete(struct hci_dev *hdev, u8 status)
{
	BT_DBG("status 0x%02x", status);

	mgmt_class_complete(hdev, MGMT_OP_SET_DEV_CLASS, status);
}

static int set_dev_class(struct sock *sk, struct hci_dev *hdev, void *data,
			 u16 len)
{
	struct mgmt_cp_set_dev_class *cp = data;
	struct pending_cmd *cmd;
	struct hci_request req;
	int err;

	BT_DBG("request for %s", hdev->name);

	if (!lmp_bredr_capable(hdev))
		return cmd_status(sk, hdev->id, MGMT_OP_SET_DEV_CLASS,
				  MGMT_STATUS_NOT_SUPPORTED);

	hci_dev_lock(hdev);

	if (pending_eir_or_class(hdev)) {
		err = cmd_status(sk, hdev->id, MGMT_OP_SET_DEV_CLASS,
				 MGMT_STATUS_BUSY);
		goto unlock;
	}

	if ((cp->minor & 0x03) != 0 || (cp->major & 0xe0) != 0) {
		err = cmd_status(sk, hdev->id, MGMT_OP_SET_DEV_CLASS,
				 MGMT_STATUS_INVALID_PARAMS);
		goto unlock;
	}

	hdev->major_class = cp->major;
	hdev->minor_class = cp->minor;

	if (!hdev_is_powered(hdev)) {
		err = cmd_complete(sk, hdev->id, MGMT_OP_SET_DEV_CLASS, 0,
				   hdev->dev_class, 3);
		goto unlock;
	}

	hci_req_init(&req, hdev);

	if (test_and_clear_bit(HCI_SERVICE_CACHE, &hdev->dev_flags)) {
		hci_dev_unlock(hdev);
		cancel_delayed_work_sync(&hdev->service_cache);
		hci_dev_lock(hdev);
		update_eir(&req);
	}

	update_class(&req);

	err = hci_req_run(&req, set_class_complete);
	if (err < 0) {
		if (err != -ENODATA)
			goto unlock;

		err = cmd_complete(sk, hdev->id, MGMT_OP_SET_DEV_CLASS, 0,
				   hdev->dev_class, 3);
		goto unlock;
	}

	cmd = mgmt_pending_add(sk, MGMT_OP_SET_DEV_CLASS, hdev, data, len);
	if (!cmd) {
		err = -ENOMEM;
		goto unlock;
	}

	err = 0;

unlock:
	hci_dev_unlock(hdev);
	return err;
}

static int load_link_keys(struct sock *sk, struct hci_dev *hdev, void *data,
			  u16 len)
{
	struct mgmt_cp_load_link_keys *cp = data;
	const u16 max_key_count = ((U16_MAX - sizeof(*cp)) /
				   sizeof(struct mgmt_link_key_info));
	u16 key_count, expected_len;
	bool changed;
	int i;

	BT_DBG("request for %s", hdev->name);

	if (!lmp_bredr_capable(hdev))
		return cmd_status(sk, hdev->id, MGMT_OP_LOAD_LINK_KEYS,
				  MGMT_STATUS_NOT_SUPPORTED);

	key_count = __le16_to_cpu(cp->key_count);
	if (key_count > max_key_count) {
		BT_ERR("load_link_keys: too big key_count value %u",
		       key_count);
		return cmd_status(sk, hdev->id, MGMT_OP_LOAD_LINK_KEYS,
				  MGMT_STATUS_INVALID_PARAMS);
	}

	expected_len = sizeof(*cp) + key_count *
					sizeof(struct mgmt_link_key_info);
	if (expected_len != len) {
		BT_ERR("load_link_keys: expected %u bytes, got %u bytes",
		       expected_len, len);
		return cmd_status(sk, hdev->id, MGMT_OP_LOAD_LINK_KEYS,
				  MGMT_STATUS_INVALID_PARAMS);
	}

	if (cp->debug_keys != 0x00 && cp->debug_keys != 0x01)
		return cmd_status(sk, hdev->id, MGMT_OP_LOAD_LINK_KEYS,
				  MGMT_STATUS_INVALID_PARAMS);

	BT_DBG("%s debug_keys %u key_count %u", hdev->name, cp->debug_keys,
	       key_count);

	for (i = 0; i < key_count; i++) {
		struct mgmt_link_key_info *key = &cp->keys[i];

		if (key->addr.type != BDADDR_BREDR || key->type > 0x08)
			return cmd_status(sk, hdev->id, MGMT_OP_LOAD_LINK_KEYS,
					  MGMT_STATUS_INVALID_PARAMS);
	}

	hci_dev_lock(hdev);

	hci_link_keys_clear(hdev);

	if (cp->debug_keys)
		changed = !test_and_set_bit(HCI_KEEP_DEBUG_KEYS,
					    &hdev->dev_flags);
	else
		changed = test_and_clear_bit(HCI_KEEP_DEBUG_KEYS,
					     &hdev->dev_flags);

	if (changed)
		new_settings(hdev, NULL);

	for (i = 0; i < key_count; i++) {
		struct mgmt_link_key_info *key = &cp->keys[i];

		/* Always ignore debug keys and require a new pairing if
		 * the user wants to use them.
		 */
		if (key->type == HCI_LK_DEBUG_COMBINATION)
			continue;

		hci_add_link_key(hdev, NULL, &key->addr.bdaddr, key->val,
				 key->type, key->pin_len, NULL);
	}

	cmd_complete(sk, hdev->id, MGMT_OP_LOAD_LINK_KEYS, 0, NULL, 0);

	hci_dev_unlock(hdev);

	return 0;
}

static int device_unpaired(struct hci_dev *hdev, bdaddr_t *bdaddr,
			   u8 addr_type, struct sock *skip_sk)
{
	struct mgmt_ev_device_unpaired ev;

	bacpy(&ev.addr.bdaddr, bdaddr);
	ev.addr.type = addr_type;

	return mgmt_event(MGMT_EV_DEVICE_UNPAIRED, hdev, &ev, sizeof(ev),
			  skip_sk);
}

static int unpair_device(struct sock *sk, struct hci_dev *hdev, void *data,
			 u16 len)
{
	struct mgmt_cp_unpair_device *cp = data;
	struct mgmt_rp_unpair_device rp;
	struct hci_cp_disconnect dc;
	struct pending_cmd *cmd;
	struct hci_conn *conn;
	int err;

	memset(&rp, 0, sizeof(rp));
	bacpy(&rp.addr.bdaddr, &cp->addr.bdaddr);
	rp.addr.type = cp->addr.type;

	if (!bdaddr_type_is_valid(cp->addr.type))
		return cmd_complete(sk, hdev->id, MGMT_OP_UNPAIR_DEVICE,
				    MGMT_STATUS_INVALID_PARAMS,
				    &rp, sizeof(rp));

	if (cp->disconnect != 0x00 && cp->disconnect != 0x01)
		return cmd_complete(sk, hdev->id, MGMT_OP_UNPAIR_DEVICE,
				    MGMT_STATUS_INVALID_PARAMS,
				    &rp, sizeof(rp));

	hci_dev_lock(hdev);

	if (!hdev_is_powered(hdev)) {
		err = cmd_complete(sk, hdev->id, MGMT_OP_UNPAIR_DEVICE,
				   MGMT_STATUS_NOT_POWERED, &rp, sizeof(rp));
		goto unlock;
	}

	if (cp->addr.type == BDADDR_BREDR) {
		/* If disconnection is requested, then look up the
		 * connection. If the remote device is connected, it
		 * will be later used to terminate the link.
		 *
		 * Setting it to NULL explicitly will cause no
		 * termination of the link.
		 */
		if (cp->disconnect)
			conn = hci_conn_hash_lookup_ba(hdev, ACL_LINK,
						       &cp->addr.bdaddr);
		else
			conn = NULL;

		err = hci_remove_link_key(hdev, &cp->addr.bdaddr);
	} else {
		u8 addr_type;

		conn = hci_conn_hash_lookup_ba(hdev, LE_LINK,
					       &cp->addr.bdaddr);
		if (conn) {
			/* Defer clearing up the connection parameters
			 * until closing to give a chance of keeping
			 * them if a repairing happens.
			 */
			set_bit(HCI_CONN_PARAM_REMOVAL_PEND, &conn->flags);

			/* If disconnection is not requested, then
			 * clear the connection variable so that the
			 * link is not terminated.
			 */
			if (!cp->disconnect)
				conn = NULL;
		}

		if (cp->addr.type == BDADDR_LE_PUBLIC)
			addr_type = ADDR_LE_DEV_PUBLIC;
		else
			addr_type = ADDR_LE_DEV_RANDOM;

		hci_remove_irk(hdev, &cp->addr.bdaddr, addr_type);

		err = hci_remove_ltk(hdev, &cp->addr.bdaddr, addr_type);
	}

	if (err < 0) {
		err = cmd_complete(sk, hdev->id, MGMT_OP_UNPAIR_DEVICE,
				   MGMT_STATUS_NOT_PAIRED, &rp, sizeof(rp));
		goto unlock;
	}

	/* If the connection variable is set, then termination of the
	 * link is requested.
	 */
	if (!conn) {
		err = cmd_complete(sk, hdev->id, MGMT_OP_UNPAIR_DEVICE, 0,
				   &rp, sizeof(rp));
		device_unpaired(hdev, &cp->addr.bdaddr, cp->addr.type, sk);
		goto unlock;
	}

	cmd = mgmt_pending_add(sk, MGMT_OP_UNPAIR_DEVICE, hdev, cp,
			       sizeof(*cp));
	if (!cmd) {
		err = -ENOMEM;
		goto unlock;
	}

	cmd->cmd_complete = addr_cmd_complete;

	dc.handle = cpu_to_le16(conn->handle);
	dc.reason = 0x13; /* Remote User Terminated Connection */
	err = hci_send_cmd(hdev, HCI_OP_DISCONNECT, sizeof(dc), &dc);
	if (err < 0)
		mgmt_pending_remove(cmd);

unlock:
	hci_dev_unlock(hdev);
	return err;
}

static int disconnect(struct sock *sk, struct hci_dev *hdev, void *data,
		      u16 len)
{
	struct mgmt_cp_disconnect *cp = data;
	struct mgmt_rp_disconnect rp;
	struct pending_cmd *cmd;
	struct hci_conn *conn;
	int err;

	BT_DBG("");

	memset(&rp, 0, sizeof(rp));
	bacpy(&rp.addr.bdaddr, &cp->addr.bdaddr);
	rp.addr.type = cp->addr.type;

	if (!bdaddr_type_is_valid(cp->addr.type))
		return cmd_complete(sk, hdev->id, MGMT_OP_DISCONNECT,
				    MGMT_STATUS_INVALID_PARAMS,
				    &rp, sizeof(rp));

	hci_dev_lock(hdev);

	if (!test_bit(HCI_UP, &hdev->flags)) {
		err = cmd_complete(sk, hdev->id, MGMT_OP_DISCONNECT,
				   MGMT_STATUS_NOT_POWERED, &rp, sizeof(rp));
		goto failed;
	}

	if (mgmt_pending_find(MGMT_OP_DISCONNECT, hdev)) {
		err = cmd_complete(sk, hdev->id, MGMT_OP_DISCONNECT,
				   MGMT_STATUS_BUSY, &rp, sizeof(rp));
		goto failed;
	}

	if (cp->addr.type == BDADDR_BREDR)
		conn = hci_conn_hash_lookup_ba(hdev, ACL_LINK,
					       &cp->addr.bdaddr);
	else
		conn = hci_conn_hash_lookup_ba(hdev, LE_LINK, &cp->addr.bdaddr);

	if (!conn || conn->state == BT_OPEN || conn->state == BT_CLOSED) {
		err = cmd_complete(sk, hdev->id, MGMT_OP_DISCONNECT,
				   MGMT_STATUS_NOT_CONNECTED, &rp, sizeof(rp));
		goto failed;
	}

	cmd = mgmt_pending_add(sk, MGMT_OP_DISCONNECT, hdev, data, len);
	if (!cmd) {
		err = -ENOMEM;
		goto failed;
	}

	cmd->cmd_complete = generic_cmd_complete;

	err = hci_disconnect(conn, HCI_ERROR_REMOTE_USER_TERM);
	if (err < 0)
		mgmt_pending_remove(cmd);

failed:
	hci_dev_unlock(hdev);
	return err;
}

static u8 link_to_bdaddr(u8 link_type, u8 addr_type)
{
	switch (link_type) {
	case LE_LINK:
		switch (addr_type) {
		case ADDR_LE_DEV_PUBLIC:
			return BDADDR_LE_PUBLIC;

		default:
			/* Fallback to LE Random address type */
			return BDADDR_LE_RANDOM;
		}

	default:
		/* Fallback to BR/EDR type */
		return BDADDR_BREDR;
	}
}

static int get_connections(struct sock *sk, struct hci_dev *hdev, void *data,
			   u16 data_len)
{
	struct mgmt_rp_get_connections *rp;
	struct hci_conn *c;
	size_t rp_len;
	int err;
	u16 i;

	BT_DBG("");

	hci_dev_lock(hdev);

	if (!hdev_is_powered(hdev)) {
		err = cmd_status(sk, hdev->id, MGMT_OP_GET_CONNECTIONS,
				 MGMT_STATUS_NOT_POWERED);
		goto unlock;
	}

	i = 0;
	list_for_each_entry(c, &hdev->conn_hash.list, list) {
		if (test_bit(HCI_CONN_MGMT_CONNECTED, &c->flags))
			i++;
	}

	rp_len = sizeof(*rp) + (i * sizeof(struct mgmt_addr_info));
	rp = kmalloc(rp_len, GFP_KERNEL);
	if (!rp) {
		err = -ENOMEM;
		goto unlock;
	}

	i = 0;
	list_for_each_entry(c, &hdev->conn_hash.list, list) {
		if (!test_bit(HCI_CONN_MGMT_CONNECTED, &c->flags))
			continue;
		bacpy(&rp->addr[i].bdaddr, &c->dst);
		rp->addr[i].type = link_to_bdaddr(c->type, c->dst_type);
		if (c->type == SCO_LINK || c->type == ESCO_LINK)
			continue;
		i++;
	}

	rp->conn_count = cpu_to_le16(i);

	/* Recalculate length in case of filtered SCO connections, etc */
	rp_len = sizeof(*rp) + (i * sizeof(struct mgmt_addr_info));

	err = cmd_complete(sk, hdev->id, MGMT_OP_GET_CONNECTIONS, 0, rp,
			   rp_len);

	kfree(rp);

unlock:
	hci_dev_unlock(hdev);
	return err;
}

static int send_pin_code_neg_reply(struct sock *sk, struct hci_dev *hdev,
				   struct mgmt_cp_pin_code_neg_reply *cp)
{
	struct pending_cmd *cmd;
	int err;

	cmd = mgmt_pending_add(sk, MGMT_OP_PIN_CODE_NEG_REPLY, hdev, cp,
			       sizeof(*cp));
	if (!cmd)
		return -ENOMEM;

	err = hci_send_cmd(hdev, HCI_OP_PIN_CODE_NEG_REPLY,
			   sizeof(cp->addr.bdaddr), &cp->addr.bdaddr);
	if (err < 0)
		mgmt_pending_remove(cmd);

	return err;
}

static int pin_code_reply(struct sock *sk, struct hci_dev *hdev, void *data,
			  u16 len)
{
	struct hci_conn *conn;
	struct mgmt_cp_pin_code_reply *cp = data;
	struct hci_cp_pin_code_reply reply;
	struct pending_cmd *cmd;
	int err;

	BT_DBG("");

	hci_dev_lock(hdev);

	if (!hdev_is_powered(hdev)) {
		err = cmd_status(sk, hdev->id, MGMT_OP_PIN_CODE_REPLY,
				 MGMT_STATUS_NOT_POWERED);
		goto failed;
	}

	conn = hci_conn_hash_lookup_ba(hdev, ACL_LINK, &cp->addr.bdaddr);
	if (!conn) {
		err = cmd_status(sk, hdev->id, MGMT_OP_PIN_CODE_REPLY,
				 MGMT_STATUS_NOT_CONNECTED);
		goto failed;
	}

	if (conn->pending_sec_level == BT_SECURITY_HIGH && cp->pin_len != 16) {
		struct mgmt_cp_pin_code_neg_reply ncp;

		memcpy(&ncp.addr, &cp->addr, sizeof(ncp.addr));

		BT_ERR("PIN code is not 16 bytes long");

		err = send_pin_code_neg_reply(sk, hdev, &ncp);
		if (err >= 0)
			err = cmd_status(sk, hdev->id, MGMT_OP_PIN_CODE_REPLY,
					 MGMT_STATUS_INVALID_PARAMS);

		goto failed;
	}

	cmd = mgmt_pending_add(sk, MGMT_OP_PIN_CODE_REPLY, hdev, data, len);
	if (!cmd) {
		err = -ENOMEM;
		goto failed;
	}

	cmd->cmd_complete = addr_cmd_complete;

	bacpy(&reply.bdaddr, &cp->addr.bdaddr);
	reply.pin_len = cp->pin_len;
	memcpy(reply.pin_code, cp->pin_code, sizeof(reply.pin_code));

	err = hci_send_cmd(hdev, HCI_OP_PIN_CODE_REPLY, sizeof(reply), &reply);
	if (err < 0)
		mgmt_pending_remove(cmd);

failed:
	hci_dev_unlock(hdev);
	return err;
}

static int set_io_capability(struct sock *sk, struct hci_dev *hdev, void *data,
			     u16 len)
{
	struct mgmt_cp_set_io_capability *cp = data;

	BT_DBG("");

	if (cp->io_capability > SMP_IO_KEYBOARD_DISPLAY)
		return cmd_complete(sk, hdev->id, MGMT_OP_SET_IO_CAPABILITY,
				    MGMT_STATUS_INVALID_PARAMS, NULL, 0);

	hci_dev_lock(hdev);

	hdev->io_capability = cp->io_capability;

	BT_DBG("%s IO capability set to 0x%02x", hdev->name,
	       hdev->io_capability);

	hci_dev_unlock(hdev);

	return cmd_complete(sk, hdev->id, MGMT_OP_SET_IO_CAPABILITY, 0, NULL,
			    0);
}

static struct pending_cmd *find_pairing(struct hci_conn *conn)
{
	struct hci_dev *hdev = conn->hdev;
	struct pending_cmd *cmd;

	list_for_each_entry(cmd, &hdev->mgmt_pending, list) {
		if (cmd->opcode != MGMT_OP_PAIR_DEVICE)
			continue;

		if (cmd->user_data != conn)
			continue;

		return cmd;
	}

	return NULL;
}

static int pairing_complete(struct pending_cmd *cmd, u8 status)
{
	struct mgmt_rp_pair_device rp;
	struct hci_conn *conn = cmd->user_data;
	int err;

	bacpy(&rp.addr.bdaddr, &conn->dst);
	rp.addr.type = link_to_bdaddr(conn->type, conn->dst_type);

	err = cmd_complete(cmd->sk, cmd->index, MGMT_OP_PAIR_DEVICE, status,
			   &rp, sizeof(rp));

	/* So we don't get further callbacks for this connection */
	conn->connect_cfm_cb = NULL;
	conn->security_cfm_cb = NULL;
	conn->disconn_cfm_cb = NULL;

	hci_conn_drop(conn);

	/* The device is paired so there is no need to remove
	 * its connection parameters anymore.
	 */
	clear_bit(HCI_CONN_PARAM_REMOVAL_PEND, &conn->flags);

	hci_conn_put(conn);

	return err;
}

void mgmt_smp_complete(struct hci_conn *conn, bool complete)
{
	u8 status = complete ? MGMT_STATUS_SUCCESS : MGMT_STATUS_FAILED;
	struct pending_cmd *cmd;

	cmd = find_pairing(conn);
	if (cmd) {
		cmd->cmd_complete(cmd, status);
		mgmt_pending_remove(cmd);
	}
}

static void pairing_complete_cb(struct hci_conn *conn, u8 status)
{
	struct pending_cmd *cmd;

	BT_DBG("status %u", status);

	cmd = find_pairing(conn);
	if (!cmd) {
		BT_DBG("Unable to find a pending command");
		return;
	}

	cmd->cmd_complete(cmd, mgmt_status(status));
	mgmt_pending_remove(cmd);
}

static void le_pairing_complete_cb(struct hci_conn *conn, u8 status)
{
	struct pending_cmd *cmd;

	BT_DBG("status %u", status);

	if (!status)
		return;

	cmd = find_pairing(conn);
	if (!cmd) {
		BT_DBG("Unable to find a pending command");
		return;
	}

	cmd->cmd_complete(cmd, mgmt_status(status));
	mgmt_pending_remove(cmd);
}

static int pair_device(struct sock *sk, struct hci_dev *hdev, void *data,
		       u16 len)
{
	struct mgmt_cp_pair_device *cp = data;
	struct mgmt_rp_pair_device rp;
	struct pending_cmd *cmd;
	u8 sec_level, auth_type;
	struct hci_conn *conn;
	int err;

	BT_DBG("");

	memset(&rp, 0, sizeof(rp));
	bacpy(&rp.addr.bdaddr, &cp->addr.bdaddr);
	rp.addr.type = cp->addr.type;

	if (!bdaddr_type_is_valid(cp->addr.type))
		return cmd_complete(sk, hdev->id, MGMT_OP_PAIR_DEVICE,
				    MGMT_STATUS_INVALID_PARAMS,
				    &rp, sizeof(rp));

	if (cp->io_cap > SMP_IO_KEYBOARD_DISPLAY)
		return cmd_complete(sk, hdev->id, MGMT_OP_PAIR_DEVICE,
				    MGMT_STATUS_INVALID_PARAMS,
				    &rp, sizeof(rp));

	hci_dev_lock(hdev);

	if (!hdev_is_powered(hdev)) {
		err = cmd_complete(sk, hdev->id, MGMT_OP_PAIR_DEVICE,
				   MGMT_STATUS_NOT_POWERED, &rp, sizeof(rp));
		goto unlock;
	}

	sec_level = BT_SECURITY_MEDIUM;
	auth_type = HCI_AT_DEDICATED_BONDING;

	if (cp->addr.type == BDADDR_BREDR) {
		conn = hci_connect_acl(hdev, &cp->addr.bdaddr, sec_level,
				       auth_type);
	} else {
		u8 addr_type;

		/* Convert from L2CAP channel address type to HCI address type
		 */
		if (cp->addr.type == BDADDR_LE_PUBLIC)
			addr_type = ADDR_LE_DEV_PUBLIC;
		else
			addr_type = ADDR_LE_DEV_RANDOM;

		/* When pairing a new device, it is expected to remember
		 * this device for future connections. Adding the connection
		 * parameter information ahead of time allows tracking
		 * of the slave preferred values and will speed up any
		 * further connection establishment.
		 *
		 * If connection parameters already exist, then they
		 * will be kept and this function does nothing.
		 */
		hci_conn_params_add(hdev, &cp->addr.bdaddr, addr_type);

		conn = hci_connect_le(hdev, &cp->addr.bdaddr, addr_type,
				      sec_level, HCI_LE_CONN_TIMEOUT,
				      HCI_ROLE_MASTER);
	}

	if (IS_ERR(conn)) {
		int status;

		if (PTR_ERR(conn) == -EBUSY)
			status = MGMT_STATUS_BUSY;
		else
			status = MGMT_STATUS_CONNECT_FAILED;

		err = cmd_complete(sk, hdev->id, MGMT_OP_PAIR_DEVICE,
				   status, &rp,
				   sizeof(rp));
		goto unlock;
	}

	if (conn->connect_cfm_cb) {
		hci_conn_drop(conn);
		err = cmd_complete(sk, hdev->id, MGMT_OP_PAIR_DEVICE,
				   MGMT_STATUS_BUSY, &rp, sizeof(rp));
		goto unlock;
	}

	cmd = mgmt_pending_add(sk, MGMT_OP_PAIR_DEVICE, hdev, data, len);
	if (!cmd) {
		err = -ENOMEM;
		hci_conn_drop(conn);
		goto unlock;
	}

	cmd->cmd_complete = pairing_complete;

	/* For LE, just connecting isn't a proof that the pairing finished */
	if (cp->addr.type == BDADDR_BREDR) {
		conn->connect_cfm_cb = pairing_complete_cb;
		conn->security_cfm_cb = pairing_complete_cb;
		conn->disconn_cfm_cb = pairing_complete_cb;
	} else {
		conn->connect_cfm_cb = le_pairing_complete_cb;
		conn->security_cfm_cb = le_pairing_complete_cb;
		conn->disconn_cfm_cb = le_pairing_complete_cb;
	}

	conn->io_capability = cp->io_cap;
	cmd->user_data = hci_conn_get(conn);

	if ((conn->state == BT_CONNECTED || conn->state == BT_CONFIG) &&
	    hci_conn_security(conn, sec_level, auth_type, true)) {
		cmd->cmd_complete(cmd, 0);
		mgmt_pending_remove(cmd);
	}

	err = 0;

unlock:
	hci_dev_unlock(hdev);
	return err;
}

static int cancel_pair_device(struct sock *sk, struct hci_dev *hdev, void *data,
			      u16 len)
{
	struct mgmt_addr_info *addr = data;
	struct pending_cmd *cmd;
	struct hci_conn *conn;
	int err;

	BT_DBG("");

	hci_dev_lock(hdev);

	if (!hdev_is_powered(hdev)) {
		err = cmd_status(sk, hdev->id, MGMT_OP_CANCEL_PAIR_DEVICE,
				 MGMT_STATUS_NOT_POWERED);
		goto unlock;
	}

	cmd = mgmt_pending_find(MGMT_OP_PAIR_DEVICE, hdev);
	if (!cmd) {
		err = cmd_status(sk, hdev->id, MGMT_OP_CANCEL_PAIR_DEVICE,
				 MGMT_STATUS_INVALID_PARAMS);
		goto unlock;
	}

	conn = cmd->user_data;

	if (bacmp(&addr->bdaddr, &conn->dst) != 0) {
		err = cmd_status(sk, hdev->id, MGMT_OP_CANCEL_PAIR_DEVICE,
				 MGMT_STATUS_INVALID_PARAMS);
		goto unlock;
	}

	cmd->cmd_complete(cmd, MGMT_STATUS_CANCELLED);
	mgmt_pending_remove(cmd);

	err = cmd_complete(sk, hdev->id, MGMT_OP_CANCEL_PAIR_DEVICE, 0,
			   addr, sizeof(*addr));
unlock:
	hci_dev_unlock(hdev);
	return err;
}

static int user_pairing_resp(struct sock *sk, struct hci_dev *hdev,
			     struct mgmt_addr_info *addr, u16 mgmt_op,
			     u16 hci_op, __le32 passkey)
{
	struct pending_cmd *cmd;
	struct hci_conn *conn;
	int err;

	hci_dev_lock(hdev);

	if (!hdev_is_powered(hdev)) {
		err = cmd_complete(sk, hdev->id, mgmt_op,
				   MGMT_STATUS_NOT_POWERED, addr,
				   sizeof(*addr));
		goto done;
	}

	if (addr->type == BDADDR_BREDR)
		conn = hci_conn_hash_lookup_ba(hdev, ACL_LINK, &addr->bdaddr);
	else
		conn = hci_conn_hash_lookup_ba(hdev, LE_LINK, &addr->bdaddr);

	if (!conn) {
		err = cmd_complete(sk, hdev->id, mgmt_op,
				   MGMT_STATUS_NOT_CONNECTED, addr,
				   sizeof(*addr));
		goto done;
	}

	if (addr->type == BDADDR_LE_PUBLIC || addr->type == BDADDR_LE_RANDOM) {
		err = smp_user_confirm_reply(conn, mgmt_op, passkey);
		if (!err)
			err = cmd_complete(sk, hdev->id, mgmt_op,
					   MGMT_STATUS_SUCCESS, addr,
					   sizeof(*addr));
		else
			err = cmd_complete(sk, hdev->id, mgmt_op,
					   MGMT_STATUS_FAILED, addr,
					   sizeof(*addr));

		goto done;
	}

	cmd = mgmt_pending_add(sk, mgmt_op, hdev, addr, sizeof(*addr));
	if (!cmd) {
		err = -ENOMEM;
		goto done;
	}

	cmd->cmd_complete = addr_cmd_complete;

	/* Continue with pairing via HCI */
	if (hci_op == HCI_OP_USER_PASSKEY_REPLY) {
		struct hci_cp_user_passkey_reply cp;

		bacpy(&cp.bdaddr, &addr->bdaddr);
		cp.passkey = passkey;
		err = hci_send_cmd(hdev, hci_op, sizeof(cp), &cp);
	} else
		err = hci_send_cmd(hdev, hci_op, sizeof(addr->bdaddr),
				   &addr->bdaddr);

	if (err < 0)
		mgmt_pending_remove(cmd);

done:
	hci_dev_unlock(hdev);
	return err;
}

static int pin_code_neg_reply(struct sock *sk, struct hci_dev *hdev,
			      void *data, u16 len)
{
	struct mgmt_cp_pin_code_neg_reply *cp = data;

	BT_DBG("");

	return user_pairing_resp(sk, hdev, &cp->addr,
				MGMT_OP_PIN_CODE_NEG_REPLY,
				HCI_OP_PIN_CODE_NEG_REPLY, 0);
}

static int user_confirm_reply(struct sock *sk, struct hci_dev *hdev, void *data,
			      u16 len)
{
	struct mgmt_cp_user_confirm_reply *cp = data;

	BT_DBG("");

	if (len != sizeof(*cp))
		return cmd_status(sk, hdev->id, MGMT_OP_USER_CONFIRM_REPLY,
				  MGMT_STATUS_INVALID_PARAMS);

	return user_pairing_resp(sk, hdev, &cp->addr,
				 MGMT_OP_USER_CONFIRM_REPLY,
				 HCI_OP_USER_CONFIRM_REPLY, 0);
}

static int user_confirm_neg_reply(struct sock *sk, struct hci_dev *hdev,
				  void *data, u16 len)
{
	struct mgmt_cp_user_confirm_neg_reply *cp = data;

	BT_DBG("");

	return user_pairing_resp(sk, hdev, &cp->addr,
				 MGMT_OP_USER_CONFIRM_NEG_REPLY,
				 HCI_OP_USER_CONFIRM_NEG_REPLY, 0);
}

static int user_passkey_reply(struct sock *sk, struct hci_dev *hdev, void *data,
			      u16 len)
{
	struct mgmt_cp_user_passkey_reply *cp = data;

	BT_DBG("");

	return user_pairing_resp(sk, hdev, &cp->addr,
				 MGMT_OP_USER_PASSKEY_REPLY,
				 HCI_OP_USER_PASSKEY_REPLY, cp->passkey);
}

static int user_passkey_neg_reply(struct sock *sk, struct hci_dev *hdev,
				  void *data, u16 len)
{
	struct mgmt_cp_user_passkey_neg_reply *cp = data;

	BT_DBG("");

	return user_pairing_resp(sk, hdev, &cp->addr,
				 MGMT_OP_USER_PASSKEY_NEG_REPLY,
				 HCI_OP_USER_PASSKEY_NEG_REPLY, 0);
}

static void update_name(struct hci_request *req)
{
	struct hci_dev *hdev = req->hdev;
	struct hci_cp_write_local_name cp;

	memcpy(cp.name, hdev->dev_name, sizeof(cp.name));

	hci_req_add(req, HCI_OP_WRITE_LOCAL_NAME, sizeof(cp), &cp);
}

static void set_name_complete(struct hci_dev *hdev, u8 status)
{
	struct mgmt_cp_set_local_name *cp;
	struct pending_cmd *cmd;

	BT_DBG("status 0x%02x", status);

	hci_dev_lock(hdev);

	cmd = mgmt_pending_find(MGMT_OP_SET_LOCAL_NAME, hdev);
	if (!cmd)
		goto unlock;

	cp = cmd->param;

	if (status)
		cmd_status(cmd->sk, hdev->id, MGMT_OP_SET_LOCAL_NAME,
			   mgmt_status(status));
	else
		cmd_complete(cmd->sk, hdev->id, MGMT_OP_SET_LOCAL_NAME, 0,
			     cp, sizeof(*cp));

	mgmt_pending_remove(cmd);

unlock:
	hci_dev_unlock(hdev);
}

static int set_local_name(struct sock *sk, struct hci_dev *hdev, void *data,
			  u16 len)
{
	struct mgmt_cp_set_local_name *cp = data;
	struct pending_cmd *cmd;
	struct hci_request req;
	int err;

	BT_DBG("");

	hci_dev_lock(hdev);

	/* If the old values are the same as the new ones just return a
	 * direct command complete event.
	 */
	if (!memcmp(hdev->dev_name, cp->name, sizeof(hdev->dev_name)) &&
	    !memcmp(hdev->short_name, cp->short_name,
		    sizeof(hdev->short_name))) {
		err = cmd_complete(sk, hdev->id, MGMT_OP_SET_LOCAL_NAME, 0,
				   data, len);
		goto failed;
	}

	memcpy(hdev->short_name, cp->short_name, sizeof(hdev->short_name));

	if (!hdev_is_powered(hdev)) {
		memcpy(hdev->dev_name, cp->name, sizeof(hdev->dev_name));

		err = cmd_complete(sk, hdev->id, MGMT_OP_SET_LOCAL_NAME, 0,
				   data, len);
		if (err < 0)
			goto failed;

		err = mgmt_event(MGMT_EV_LOCAL_NAME_CHANGED, hdev, data, len,
				 sk);

		goto failed;
	}

	cmd = mgmt_pending_add(sk, MGMT_OP_SET_LOCAL_NAME, hdev, data, len);
	if (!cmd) {
		err = -ENOMEM;
		goto failed;
	}

	memcpy(hdev->dev_name, cp->name, sizeof(hdev->dev_name));

	hci_req_init(&req, hdev);

	if (lmp_bredr_capable(hdev)) {
		update_name(&req);
		update_eir(&req);
	}

	/* The name is stored in the scan response data and so
	 * no need to udpate the advertising data here.
	 */
	if (lmp_le_capable(hdev))
		update_scan_rsp_data(&req);

	err = hci_req_run(&req, set_name_complete);
	if (err < 0)
		mgmt_pending_remove(cmd);

failed:
	hci_dev_unlock(hdev);
	return err;
}

static int read_local_oob_data(struct sock *sk, struct hci_dev *hdev,
			       void *data, u16 data_len)
{
	struct pending_cmd *cmd;
	int err;

	BT_DBG("%s", hdev->name);

	hci_dev_lock(hdev);

	if (!hdev_is_powered(hdev)) {
		err = cmd_status(sk, hdev->id, MGMT_OP_READ_LOCAL_OOB_DATA,
				 MGMT_STATUS_NOT_POWERED);
		goto unlock;
	}

	if (!lmp_ssp_capable(hdev)) {
		err = cmd_status(sk, hdev->id, MGMT_OP_READ_LOCAL_OOB_DATA,
				 MGMT_STATUS_NOT_SUPPORTED);
		goto unlock;
	}

	if (mgmt_pending_find(MGMT_OP_READ_LOCAL_OOB_DATA, hdev)) {
		err = cmd_status(sk, hdev->id, MGMT_OP_READ_LOCAL_OOB_DATA,
				 MGMT_STATUS_BUSY);
		goto unlock;
	}

	cmd = mgmt_pending_add(sk, MGMT_OP_READ_LOCAL_OOB_DATA, hdev, NULL, 0);
	if (!cmd) {
		err = -ENOMEM;
		goto unlock;
	}

	if (bredr_sc_enabled(hdev))
		err = hci_send_cmd(hdev, HCI_OP_READ_LOCAL_OOB_EXT_DATA,
				   0, NULL);
	else
		err = hci_send_cmd(hdev, HCI_OP_READ_LOCAL_OOB_DATA, 0, NULL);

	if (err < 0)
		mgmt_pending_remove(cmd);

unlock:
	hci_dev_unlock(hdev);
	return err;
}

static int add_remote_oob_data(struct sock *sk, struct hci_dev *hdev,
			       void *data, u16 len)
{
	int err;

	BT_DBG("%s ", hdev->name);

	hci_dev_lock(hdev);

	if (len == MGMT_ADD_REMOTE_OOB_DATA_SIZE) {
		struct mgmt_cp_add_remote_oob_data *cp = data;
		u8 status;

		if (cp->addr.type != BDADDR_BREDR) {
			err = cmd_complete(sk, hdev->id,
					   MGMT_OP_ADD_REMOTE_OOB_DATA,
					   MGMT_STATUS_INVALID_PARAMS,
					   &cp->addr, sizeof(cp->addr));
			goto unlock;
		}

		err = hci_add_remote_oob_data(hdev, &cp->addr.bdaddr,
					      cp->addr.type, cp->hash,
					      cp->rand, NULL, NULL);
		if (err < 0)
			status = MGMT_STATUS_FAILED;
		else
			status = MGMT_STATUS_SUCCESS;

		err = cmd_complete(sk, hdev->id, MGMT_OP_ADD_REMOTE_OOB_DATA,
				   status, &cp->addr, sizeof(cp->addr));
	} else if (len == MGMT_ADD_REMOTE_OOB_EXT_DATA_SIZE) {
		struct mgmt_cp_add_remote_oob_ext_data *cp = data;
		u8 *rand192, *hash192;
		u8 status;

		if (cp->addr.type != BDADDR_BREDR) {
			err = cmd_complete(sk, hdev->id,
					   MGMT_OP_ADD_REMOTE_OOB_DATA,
					   MGMT_STATUS_INVALID_PARAMS,
					   &cp->addr, sizeof(cp->addr));
			goto unlock;
		}

		if (bdaddr_type_is_le(cp->addr.type)) {
			rand192 = NULL;
			hash192 = NULL;
		} else {
			rand192 = cp->rand192;
			hash192 = cp->hash192;
		}

		err = hci_add_remote_oob_data(hdev, &cp->addr.bdaddr,
					      cp->addr.type, hash192, rand192,
					      cp->hash256, cp->rand256);
		if (err < 0)
			status = MGMT_STATUS_FAILED;
		else
			status = MGMT_STATUS_SUCCESS;

		err = cmd_complete(sk, hdev->id, MGMT_OP_ADD_REMOTE_OOB_DATA,
				   status, &cp->addr, sizeof(cp->addr));
	} else {
		BT_ERR("add_remote_oob_data: invalid length of %u bytes", len);
		err = cmd_status(sk, hdev->id, MGMT_OP_ADD_REMOTE_OOB_DATA,
				 MGMT_STATUS_INVALID_PARAMS);
	}

unlock:
	hci_dev_unlock(hdev);
	return err;
}

static int remove_remote_oob_data(struct sock *sk, struct hci_dev *hdev,
				  void *data, u16 len)
{
	struct mgmt_cp_remove_remote_oob_data *cp = data;
	u8 status;
	int err;

	BT_DBG("%s", hdev->name);

	if (cp->addr.type != BDADDR_BREDR)
		return cmd_complete(sk, hdev->id, MGMT_OP_REMOVE_REMOTE_OOB_DATA,
				    MGMT_STATUS_INVALID_PARAMS,
				    &cp->addr, sizeof(cp->addr));

	hci_dev_lock(hdev);

	if (!bacmp(&cp->addr.bdaddr, BDADDR_ANY)) {
		hci_remote_oob_data_clear(hdev);
		status = MGMT_STATUS_SUCCESS;
		goto done;
	}

	err = hci_remove_remote_oob_data(hdev, &cp->addr.bdaddr, cp->addr.type);
	if (err < 0)
		status = MGMT_STATUS_INVALID_PARAMS;
	else
		status = MGMT_STATUS_SUCCESS;

done:
	err = cmd_complete(sk, hdev->id, MGMT_OP_REMOVE_REMOTE_OOB_DATA,
			   status, &cp->addr, sizeof(cp->addr));

	hci_dev_unlock(hdev);
	return err;
}

static bool trigger_discovery(struct hci_request *req, u8 *status)
{
	struct hci_dev *hdev = req->hdev;
	struct hci_cp_le_set_scan_param param_cp;
	struct hci_cp_le_set_scan_enable enable_cp;
	struct hci_cp_inquiry inq_cp;
	/* General inquiry access code (GIAC) */
	u8 lap[3] = { 0x33, 0x8b, 0x9e };
	u8 own_addr_type;
	int err;

	switch (hdev->discovery.type) {
	case DISCOV_TYPE_BREDR:
		*status = mgmt_bredr_support(hdev);
		if (*status)
			return false;

		if (test_bit(HCI_INQUIRY, &hdev->flags)) {
			*status = MGMT_STATUS_BUSY;
			return false;
		}

		hci_inquiry_cache_flush(hdev);

		memset(&inq_cp, 0, sizeof(inq_cp));
		memcpy(&inq_cp.lap, lap, sizeof(inq_cp.lap));
		inq_cp.length = DISCOV_BREDR_INQUIRY_LEN;
		hci_req_add(req, HCI_OP_INQUIRY, sizeof(inq_cp), &inq_cp);
		break;

	case DISCOV_TYPE_LE:
	case DISCOV_TYPE_INTERLEAVED:
		*status = mgmt_le_support(hdev);
		if (*status)
			return false;

		if (hdev->discovery.type == DISCOV_TYPE_INTERLEAVED &&
		    !test_bit(HCI_BREDR_ENABLED, &hdev->dev_flags)) {
			*status = MGMT_STATUS_NOT_SUPPORTED;
			return false;
		}

		if (test_bit(HCI_LE_ADV, &hdev->dev_flags)) {
			/* Don't let discovery abort an outgoing
			 * connection attempt that's using directed
			 * advertising.
			 */
			if (hci_conn_hash_lookup_state(hdev, LE_LINK,
						       BT_CONNECT)) {
				*status = MGMT_STATUS_REJECTED;
				return false;
			}

			disable_advertising(req);
		}

		/* If controller is scanning, it means the background scanning
		 * is running. Thus, we should temporarily stop it in order to
		 * set the discovery scanning parameters.
		 */
		if (test_bit(HCI_LE_SCAN, &hdev->dev_flags))
			hci_req_add_le_scan_disable(req);

		memset(&param_cp, 0, sizeof(param_cp));

		/* All active scans will be done with either a resolvable
		 * private address (when privacy feature has been enabled)
		 * or non-resolvable private address.
		 */
		err = hci_update_random_address(req, true, &own_addr_type);
		if (err < 0) {
			*status = MGMT_STATUS_FAILED;
			return false;
		}

		param_cp.type = LE_SCAN_ACTIVE;
		param_cp.interval = cpu_to_le16(DISCOV_LE_SCAN_INT);
		param_cp.window = cpu_to_le16(DISCOV_LE_SCAN_WIN);
		param_cp.own_address_type = own_addr_type;
		hci_req_add(req, HCI_OP_LE_SET_SCAN_PARAM, sizeof(param_cp),
			    &param_cp);

		memset(&enable_cp, 0, sizeof(enable_cp));
		enable_cp.enable = LE_SCAN_ENABLE;
		enable_cp.filter_dup = LE_SCAN_FILTER_DUP_ENABLE;
		hci_req_add(req, HCI_OP_LE_SET_SCAN_ENABLE, sizeof(enable_cp),
			    &enable_cp);
		break;

	default:
		*status = MGMT_STATUS_INVALID_PARAMS;
		return false;
	}

	return true;
}

static void start_discovery_complete(struct hci_dev *hdev, u8 status)
{
	struct pending_cmd *cmd;
	unsigned long timeout;

	BT_DBG("status %d", status);

	hci_dev_lock(hdev);

	cmd = mgmt_pending_find(MGMT_OP_START_DISCOVERY, hdev);
	if (!cmd)
		cmd = mgmt_pending_find(MGMT_OP_START_SERVICE_DISCOVERY, hdev);

	if (cmd) {
		cmd->cmd_complete(cmd, mgmt_status(status));
		mgmt_pending_remove(cmd);
	}

	if (status) {
		hci_discovery_set_state(hdev, DISCOVERY_STOPPED);
		goto unlock;
	}

	hci_discovery_set_state(hdev, DISCOVERY_FINDING);

	switch (hdev->discovery.type) {
	case DISCOV_TYPE_LE:
		timeout = msecs_to_jiffies(DISCOV_LE_TIMEOUT);
		break;
	case DISCOV_TYPE_INTERLEAVED:
		timeout = msecs_to_jiffies(hdev->discov_interleaved_timeout);
		break;
	case DISCOV_TYPE_BREDR:
		timeout = 0;
		break;
	default:
		BT_ERR("Invalid discovery type %d", hdev->discovery.type);
		timeout = 0;
		break;
	}

	if (timeout)
		queue_delayed_work(hdev->workqueue,
				   &hdev->le_scan_disable, timeout);

unlock:
	hci_dev_unlock(hdev);
}

static int start_discovery(struct sock *sk, struct hci_dev *hdev,
			   void *data, u16 len)
{
	struct mgmt_cp_start_discovery *cp = data;
	struct pending_cmd *cmd;
	struct hci_request req;
	u8 status;
	int err;

	BT_DBG("%s", hdev->name);

	hci_dev_lock(hdev);

	if (!hdev_is_powered(hdev)) {
		err = cmd_complete(sk, hdev->id, MGMT_OP_START_DISCOVERY,
				   MGMT_STATUS_NOT_POWERED,
				   &cp->type, sizeof(cp->type));
		goto failed;
	}

	if (hdev->discovery.state != DISCOVERY_STOPPED ||
	    test_bit(HCI_PERIODIC_INQ, &hdev->dev_flags)) {
		err = cmd_complete(sk, hdev->id, MGMT_OP_START_DISCOVERY,
				   MGMT_STATUS_BUSY, &cp->type,
				   sizeof(cp->type));
		goto failed;
	}

	cmd = mgmt_pending_add(sk, MGMT_OP_START_DISCOVERY, hdev, data, len);
	if (!cmd) {
		err = -ENOMEM;
		goto failed;
	}

	cmd->cmd_complete = generic_cmd_complete;

	/* Clear the discovery filter first to free any previously
	 * allocated memory for the UUID list.
	 */
	hci_discovery_filter_clear(hdev);

	hdev->discovery.type = cp->type;
	hdev->discovery.report_invalid_rssi = false;

	hci_req_init(&req, hdev);

	if (!trigger_discovery(&req, &status)) {
		err = cmd_complete(sk, hdev->id, MGMT_OP_START_DISCOVERY,
				   status, &cp->type, sizeof(cp->type));
		mgmt_pending_remove(cmd);
		goto failed;
	}

	err = hci_req_run(&req, start_discovery_complete);
	if (err < 0) {
		mgmt_pending_remove(cmd);
		goto failed;
	}

	hci_discovery_set_state(hdev, DISCOVERY_STARTING);

failed:
	hci_dev_unlock(hdev);
	return err;
}

static int service_discovery_cmd_complete(struct pending_cmd *cmd, u8 status)
{
	return cmd_complete(cmd->sk, cmd->index, cmd->opcode, status,
			    cmd->param, 1);
}

static int start_service_discovery(struct sock *sk, struct hci_dev *hdev,
				   void *data, u16 len)
{
	struct mgmt_cp_start_service_discovery *cp = data;
	struct pending_cmd *cmd;
	struct hci_request req;
	const u16 max_uuid_count = ((U16_MAX - sizeof(*cp)) / 16);
	u16 uuid_count, expected_len;
	u8 status;
	int err;

	BT_DBG("%s", hdev->name);

	hci_dev_lock(hdev);

	if (!hdev_is_powered(hdev)) {
		err = cmd_complete(sk, hdev->id,
				   MGMT_OP_START_SERVICE_DISCOVERY,
				   MGMT_STATUS_NOT_POWERED,
				   &cp->type, sizeof(cp->type));
		goto failed;
	}

	if (hdev->discovery.state != DISCOVERY_STOPPED ||
	    test_bit(HCI_PERIODIC_INQ, &hdev->dev_flags)) {
		err = cmd_complete(sk, hdev->id,
				   MGMT_OP_START_SERVICE_DISCOVERY,
				   MGMT_STATUS_BUSY, &cp->type,
				   sizeof(cp->type));
		goto failed;
	}

	uuid_count = __le16_to_cpu(cp->uuid_count);
	if (uuid_count > max_uuid_count) {
		BT_ERR("service_discovery: too big uuid_count value %u",
		       uuid_count);
		err = cmd_complete(sk, hdev->id,
				   MGMT_OP_START_SERVICE_DISCOVERY,
				   MGMT_STATUS_INVALID_PARAMS, &cp->type,
				   sizeof(cp->type));
		goto failed;
	}

	expected_len = sizeof(*cp) + uuid_count * 16;
	if (expected_len != len) {
		BT_ERR("service_discovery: expected %u bytes, got %u bytes",
		       expected_len, len);
		err = cmd_complete(sk, hdev->id,
				   MGMT_OP_START_SERVICE_DISCOVERY,
				   MGMT_STATUS_INVALID_PARAMS, &cp->type,
				   sizeof(cp->type));
		goto failed;
	}

	cmd = mgmt_pending_add(sk, MGMT_OP_START_SERVICE_DISCOVERY,
			       hdev, data, len);
	if (!cmd) {
		err = -ENOMEM;
		goto failed;
	}

	cmd->cmd_complete = service_discovery_cmd_complete;

	/* Clear the discovery filter first to free any previously
	 * allocated memory for the UUID list.
	 */
	hci_discovery_filter_clear(hdev);

	hdev->discovery.type = cp->type;
	hdev->discovery.rssi = cp->rssi;
	hdev->discovery.uuid_count = uuid_count;

	if (uuid_count > 0) {
		hdev->discovery.uuids = kmemdup(cp->uuids, uuid_count * 16,
						GFP_KERNEL);
		if (!hdev->discovery.uuids) {
			err = cmd_complete(sk, hdev->id,
					   MGMT_OP_START_SERVICE_DISCOVERY,
					   MGMT_STATUS_FAILED,
					   &cp->type, sizeof(cp->type));
			mgmt_pending_remove(cmd);
			goto failed;
		}
	}

	hci_req_init(&req, hdev);

	if (!trigger_discovery(&req, &status)) {
		err = cmd_complete(sk, hdev->id,
				   MGMT_OP_START_SERVICE_DISCOVERY,
				   status, &cp->type, sizeof(cp->type));
		mgmt_pending_remove(cmd);
		goto failed;
	}

	err = hci_req_run(&req, start_discovery_complete);
	if (err < 0) {
		mgmt_pending_remove(cmd);
		goto failed;
	}

	hci_discovery_set_state(hdev, DISCOVERY_STARTING);

failed:
	hci_dev_unlock(hdev);
	return err;
}

static void stop_discovery_complete(struct hci_dev *hdev, u8 status)
{
	struct pending_cmd *cmd;

	BT_DBG("status %d", status);

	hci_dev_lock(hdev);

	cmd = mgmt_pending_find(MGMT_OP_STOP_DISCOVERY, hdev);
	if (cmd) {
		cmd->cmd_complete(cmd, mgmt_status(status));
		mgmt_pending_remove(cmd);
	}

	if (!status)
		hci_discovery_set_state(hdev, DISCOVERY_STOPPED);

	hci_dev_unlock(hdev);
}

static int stop_discovery(struct sock *sk, struct hci_dev *hdev, void *data,
			  u16 len)
{
	struct mgmt_cp_stop_discovery *mgmt_cp = data;
	struct pending_cmd *cmd;
	struct hci_request req;
	int err;

	BT_DBG("%s", hdev->name);

	hci_dev_lock(hdev);

	if (!hci_discovery_active(hdev)) {
		err = cmd_complete(sk, hdev->id, MGMT_OP_STOP_DISCOVERY,
				   MGMT_STATUS_REJECTED, &mgmt_cp->type,
				   sizeof(mgmt_cp->type));
		goto unlock;
	}

	if (hdev->discovery.type != mgmt_cp->type) {
		err = cmd_complete(sk, hdev->id, MGMT_OP_STOP_DISCOVERY,
				   MGMT_STATUS_INVALID_PARAMS, &mgmt_cp->type,
				   sizeof(mgmt_cp->type));
		goto unlock;
	}

	cmd = mgmt_pending_add(sk, MGMT_OP_STOP_DISCOVERY, hdev, data, len);
	if (!cmd) {
		err = -ENOMEM;
		goto unlock;
	}

	cmd->cmd_complete = generic_cmd_complete;

	hci_req_init(&req, hdev);

	hci_stop_discovery(&req);

	err = hci_req_run(&req, stop_discovery_complete);
	if (!err) {
		hci_discovery_set_state(hdev, DISCOVERY_STOPPING);
		goto unlock;
	}

	mgmt_pending_remove(cmd);

	/* If no HCI commands were sent we're done */
	if (err == -ENODATA) {
		err = cmd_complete(sk, hdev->id, MGMT_OP_STOP_DISCOVERY, 0,
				   &mgmt_cp->type, sizeof(mgmt_cp->type));
		hci_discovery_set_state(hdev, DISCOVERY_STOPPED);
	}

unlock:
	hci_dev_unlock(hdev);
	return err;
}

static int confirm_name(struct sock *sk, struct hci_dev *hdev, void *data,
			u16 len)
{
	struct mgmt_cp_confirm_name *cp = data;
	struct inquiry_entry *e;
	int err;

	BT_DBG("%s", hdev->name);

	hci_dev_lock(hdev);

	if (!hci_discovery_active(hdev)) {
		err = cmd_complete(sk, hdev->id, MGMT_OP_CONFIRM_NAME,
				   MGMT_STATUS_FAILED, &cp->addr,
				   sizeof(cp->addr));
		goto failed;
	}

	e = hci_inquiry_cache_lookup_unknown(hdev, &cp->addr.bdaddr);
	if (!e) {
		err = cmd_complete(sk, hdev->id, MGMT_OP_CONFIRM_NAME,
				   MGMT_STATUS_INVALID_PARAMS, &cp->addr,
				   sizeof(cp->addr));
		goto failed;
	}

	if (cp->name_known) {
		e->name_state = NAME_KNOWN;
		list_del(&e->list);
	} else {
		e->name_state = NAME_NEEDED;
		hci_inquiry_cache_update_resolve(hdev, e);
	}

	err = cmd_complete(sk, hdev->id, MGMT_OP_CONFIRM_NAME, 0, &cp->addr,
			   sizeof(cp->addr));

failed:
	hci_dev_unlock(hdev);
	return err;
}

static int block_device(struct sock *sk, struct hci_dev *hdev, void *data,
			u16 len)
{
	struct mgmt_cp_block_device *cp = data;
	u8 status;
	int err;

	BT_DBG("%s", hdev->name);

	if (!bdaddr_type_is_valid(cp->addr.type))
		return cmd_complete(sk, hdev->id, MGMT_OP_BLOCK_DEVICE,
				    MGMT_STATUS_INVALID_PARAMS,
				    &cp->addr, sizeof(cp->addr));

	hci_dev_lock(hdev);

	err = hci_bdaddr_list_add(&hdev->blacklist, &cp->addr.bdaddr,
				  cp->addr.type);
	if (err < 0) {
		status = MGMT_STATUS_FAILED;
		goto done;
	}

	mgmt_event(MGMT_EV_DEVICE_BLOCKED, hdev, &cp->addr, sizeof(cp->addr),
		   sk);
	status = MGMT_STATUS_SUCCESS;

done:
	err = cmd_complete(sk, hdev->id, MGMT_OP_BLOCK_DEVICE, status,
			   &cp->addr, sizeof(cp->addr));

	hci_dev_unlock(hdev);

	return err;
}

static int unblock_device(struct sock *sk, struct hci_dev *hdev, void *data,
			  u16 len)
{
	struct mgmt_cp_unblock_device *cp = data;
	u8 status;
	int err;

	BT_DBG("%s", hdev->name);

	if (!bdaddr_type_is_valid(cp->addr.type))
		return cmd_complete(sk, hdev->id, MGMT_OP_UNBLOCK_DEVICE,
				    MGMT_STATUS_INVALID_PARAMS,
				    &cp->addr, sizeof(cp->addr));

	hci_dev_lock(hdev);

	err = hci_bdaddr_list_del(&hdev->blacklist, &cp->addr.bdaddr,
				  cp->addr.type);
	if (err < 0) {
		status = MGMT_STATUS_INVALID_PARAMS;
		goto done;
	}

	mgmt_event(MGMT_EV_DEVICE_UNBLOCKED, hdev, &cp->addr, sizeof(cp->addr),
		   sk);
	status = MGMT_STATUS_SUCCESS;

done:
	err = cmd_complete(sk, hdev->id, MGMT_OP_UNBLOCK_DEVICE, status,
			   &cp->addr, sizeof(cp->addr));

	hci_dev_unlock(hdev);

	return err;
}

static int set_device_id(struct sock *sk, struct hci_dev *hdev, void *data,
			 u16 len)
{
	struct mgmt_cp_set_device_id *cp = data;
	struct hci_request req;
	int err;
	__u16 source;

	BT_DBG("%s", hdev->name);

	source = __le16_to_cpu(cp->source);

	if (source > 0x0002)
		return cmd_status(sk, hdev->id, MGMT_OP_SET_DEVICE_ID,
				  MGMT_STATUS_INVALID_PARAMS);

	hci_dev_lock(hdev);

	hdev->devid_source = source;
	hdev->devid_vendor = __le16_to_cpu(cp->vendor);
	hdev->devid_product = __le16_to_cpu(cp->product);
	hdev->devid_version = __le16_to_cpu(cp->version);

	err = cmd_complete(sk, hdev->id, MGMT_OP_SET_DEVICE_ID, 0, NULL, 0);

	hci_req_init(&req, hdev);
	update_eir(&req);
	hci_req_run(&req, NULL);

	hci_dev_unlock(hdev);

	return err;
}

static void set_advertising_complete(struct hci_dev *hdev, u8 status)
{
	struct cmd_lookup match = { NULL, hdev };

	hci_dev_lock(hdev);

	if (status) {
		u8 mgmt_err = mgmt_status(status);

		mgmt_pending_foreach(MGMT_OP_SET_ADVERTISING, hdev,
				     cmd_status_rsp, &mgmt_err);
		goto unlock;
	}

	if (test_bit(HCI_LE_ADV, &hdev->dev_flags))
		set_bit(HCI_ADVERTISING, &hdev->dev_flags);
	else
		clear_bit(HCI_ADVERTISING, &hdev->dev_flags);

	mgmt_pending_foreach(MGMT_OP_SET_ADVERTISING, hdev, settings_rsp,
			     &match);

	new_settings(hdev, match.sk);

	if (match.sk)
		sock_put(match.sk);

unlock:
	hci_dev_unlock(hdev);
}

static int set_advertising(struct sock *sk, struct hci_dev *hdev, void *data,
			   u16 len)
{
	struct mgmt_mode *cp = data;
	struct pending_cmd *cmd;
	struct hci_request req;
	u8 val, enabled, status;
	int err;

	BT_DBG("request for %s", hdev->name);

	status = mgmt_le_support(hdev);
	if (status)
		return cmd_status(sk, hdev->id, MGMT_OP_SET_ADVERTISING,
				  status);

	if (cp->val != 0x00 && cp->val != 0x01)
		return cmd_status(sk, hdev->id, MGMT_OP_SET_ADVERTISING,
				  MGMT_STATUS_INVALID_PARAMS);

	hci_dev_lock(hdev);

	val = !!cp->val;
	enabled = test_bit(HCI_ADVERTISING, &hdev->dev_flags);

	/* The following conditions are ones which mean that we should
	 * not do any HCI communication but directly send a mgmt
	 * response to user space (after toggling the flag if
	 * necessary).
	 */
	if (!hdev_is_powered(hdev) || val == enabled ||
	    hci_conn_num(hdev, LE_LINK) > 0 ||
	    (test_bit(HCI_LE_SCAN, &hdev->dev_flags) &&
	     hdev->le_scan_type == LE_SCAN_ACTIVE)) {
		bool changed = false;

		if (val != test_bit(HCI_ADVERTISING, &hdev->dev_flags)) {
			change_bit(HCI_ADVERTISING, &hdev->dev_flags);
			changed = true;
		}

		err = send_settings_rsp(sk, MGMT_OP_SET_ADVERTISING, hdev);
		if (err < 0)
			goto unlock;

		if (changed)
			err = new_settings(hdev, sk);

		goto unlock;
	}

	if (mgmt_pending_find(MGMT_OP_SET_ADVERTISING, hdev) ||
	    mgmt_pending_find(MGMT_OP_SET_LE, hdev)) {
		err = cmd_status(sk, hdev->id, MGMT_OP_SET_ADVERTISING,
				 MGMT_STATUS_BUSY);
		goto unlock;
	}

	cmd = mgmt_pending_add(sk, MGMT_OP_SET_ADVERTISING, hdev, data, len);
	if (!cmd) {
		err = -ENOMEM;
		goto unlock;
	}

	hci_req_init(&req, hdev);

	if (val)
		enable_advertising(&req);
	else
		disable_advertising(&req);

	err = hci_req_run(&req, set_advertising_complete);
	if (err < 0)
		mgmt_pending_remove(cmd);

unlock:
	hci_dev_unlock(hdev);
	return err;
}

static int set_static_address(struct sock *sk, struct hci_dev *hdev,
			      void *data, u16 len)
{
	struct mgmt_cp_set_static_address *cp = data;
	int err;

	BT_DBG("%s", hdev->name);

	if (!lmp_le_capable(hdev))
		return cmd_status(sk, hdev->id, MGMT_OP_SET_STATIC_ADDRESS,
				  MGMT_STATUS_NOT_SUPPORTED);

	if (hdev_is_powered(hdev))
		return cmd_status(sk, hdev->id, MGMT_OP_SET_STATIC_ADDRESS,
				  MGMT_STATUS_REJECTED);

	if (bacmp(&cp->bdaddr, BDADDR_ANY)) {
		if (!bacmp(&cp->bdaddr, BDADDR_NONE))
			return cmd_status(sk, hdev->id,
					  MGMT_OP_SET_STATIC_ADDRESS,
					  MGMT_STATUS_INVALID_PARAMS);

		/* Two most significant bits shall be set */
		if ((cp->bdaddr.b[5] & 0xc0) != 0xc0)
			return cmd_status(sk, hdev->id,
					  MGMT_OP_SET_STATIC_ADDRESS,
					  MGMT_STATUS_INVALID_PARAMS);
	}

	hci_dev_lock(hdev);

	bacpy(&hdev->static_addr, &cp->bdaddr);

	err = cmd_complete(sk, hdev->id, MGMT_OP_SET_STATIC_ADDRESS, 0, NULL, 0);

	hci_dev_unlock(hdev);

	return err;
}

static int set_scan_params(struct sock *sk, struct hci_dev *hdev,
			   void *data, u16 len)
{
	struct mgmt_cp_set_scan_params *cp = data;
	__u16 interval, window;
	int err;

	BT_DBG("%s", hdev->name);

	if (!lmp_le_capable(hdev))
		return cmd_status(sk, hdev->id, MGMT_OP_SET_SCAN_PARAMS,
				  MGMT_STATUS_NOT_SUPPORTED);

	interval = __le16_to_cpu(cp->interval);

	if (interval < 0x0004 || interval > 0x4000)
		return cmd_status(sk, hdev->id, MGMT_OP_SET_SCAN_PARAMS,
				  MGMT_STATUS_INVALID_PARAMS);

	window = __le16_to_cpu(cp->window);

	if (window < 0x0004 || window > 0x4000)
		return cmd_status(sk, hdev->id, MGMT_OP_SET_SCAN_PARAMS,
				  MGMT_STATUS_INVALID_PARAMS);

	if (window > interval)
		return cmd_status(sk, hdev->id, MGMT_OP_SET_SCAN_PARAMS,
				  MGMT_STATUS_INVALID_PARAMS);

	hci_dev_lock(hdev);

	hdev->le_scan_interval = interval;
	hdev->le_scan_window = window;

	err = cmd_complete(sk, hdev->id, MGMT_OP_SET_SCAN_PARAMS, 0, NULL, 0);

	/* If background scan is running, restart it so new parameters are
	 * loaded.
	 */
	if (test_bit(HCI_LE_SCAN, &hdev->dev_flags) &&
	    hdev->discovery.state == DISCOVERY_STOPPED) {
		struct hci_request req;

		hci_req_init(&req, hdev);

		hci_req_add_le_scan_disable(&req);
		hci_req_add_le_passive_scan(&req);

		hci_req_run(&req, NULL);
	}

	hci_dev_unlock(hdev);

	return err;
}

static void fast_connectable_complete(struct hci_dev *hdev, u8 status)
{
	struct pending_cmd *cmd;

	BT_DBG("status 0x%02x", status);

	hci_dev_lock(hdev);

	cmd = mgmt_pending_find(MGMT_OP_SET_FAST_CONNECTABLE, hdev);
	if (!cmd)
		goto unlock;

	if (status) {
		cmd_status(cmd->sk, hdev->id, MGMT_OP_SET_FAST_CONNECTABLE,
			   mgmt_status(status));
	} else {
		struct mgmt_mode *cp = cmd->param;

		if (cp->val)
			set_bit(HCI_FAST_CONNECTABLE, &hdev->dev_flags);
		else
			clear_bit(HCI_FAST_CONNECTABLE, &hdev->dev_flags);

		send_settings_rsp(cmd->sk, MGMT_OP_SET_FAST_CONNECTABLE, hdev);
		new_settings(hdev, cmd->sk);
	}

	mgmt_pending_remove(cmd);

unlock:
	hci_dev_unlock(hdev);
}

static int set_fast_connectable(struct sock *sk, struct hci_dev *hdev,
				void *data, u16 len)
{
	struct mgmt_mode *cp = data;
	struct pending_cmd *cmd;
	struct hci_request req;
	int err;

	BT_DBG("%s", hdev->name);

	if (!test_bit(HCI_BREDR_ENABLED, &hdev->dev_flags) ||
	    hdev->hci_ver < BLUETOOTH_VER_1_2)
		return cmd_status(sk, hdev->id, MGMT_OP_SET_FAST_CONNECTABLE,
				  MGMT_STATUS_NOT_SUPPORTED);

	if (cp->val != 0x00 && cp->val != 0x01)
		return cmd_status(sk, hdev->id, MGMT_OP_SET_FAST_CONNECTABLE,
				  MGMT_STATUS_INVALID_PARAMS);

	if (!hdev_is_powered(hdev))
		return cmd_status(sk, hdev->id, MGMT_OP_SET_FAST_CONNECTABLE,
				  MGMT_STATUS_NOT_POWERED);

	if (!test_bit(HCI_CONNECTABLE, &hdev->dev_flags))
		return cmd_status(sk, hdev->id, MGMT_OP_SET_FAST_CONNECTABLE,
				  MGMT_STATUS_REJECTED);

	hci_dev_lock(hdev);

	if (mgmt_pending_find(MGMT_OP_SET_FAST_CONNECTABLE, hdev)) {
		err = cmd_status(sk, hdev->id, MGMT_OP_SET_FAST_CONNECTABLE,
				 MGMT_STATUS_BUSY);
		goto unlock;
	}

	if (!!cp->val == test_bit(HCI_FAST_CONNECTABLE, &hdev->dev_flags)) {
		err = send_settings_rsp(sk, MGMT_OP_SET_FAST_CONNECTABLE,
					hdev);
		goto unlock;
	}

	cmd = mgmt_pending_add(sk, MGMT_OP_SET_FAST_CONNECTABLE, hdev,
			       data, len);
	if (!cmd) {
		err = -ENOMEM;
		goto unlock;
	}

	hci_req_init(&req, hdev);

	write_fast_connectable(&req, cp->val);

	err = hci_req_run(&req, fast_connectable_complete);
	if (err < 0) {
		err = cmd_status(sk, hdev->id, MGMT_OP_SET_FAST_CONNECTABLE,
				 MGMT_STATUS_FAILED);
		mgmt_pending_remove(cmd);
	}

unlock:
	hci_dev_unlock(hdev);

	return err;
}

static void set_bredr_complete(struct hci_dev *hdev, u8 status)
{
	struct pending_cmd *cmd;

	BT_DBG("status 0x%02x", status);

	hci_dev_lock(hdev);

	cmd = mgmt_pending_find(MGMT_OP_SET_BREDR, hdev);
	if (!cmd)
		goto unlock;

	if (status) {
		u8 mgmt_err = mgmt_status(status);

		/* We need to restore the flag if related HCI commands
		 * failed.
		 */
		clear_bit(HCI_BREDR_ENABLED, &hdev->dev_flags);

		cmd_status(cmd->sk, cmd->index, cmd->opcode, mgmt_err);
	} else {
		send_settings_rsp(cmd->sk, MGMT_OP_SET_BREDR, hdev);
		new_settings(hdev, cmd->sk);
	}

	mgmt_pending_remove(cmd);

unlock:
	hci_dev_unlock(hdev);
}

static int set_bredr(struct sock *sk, struct hci_dev *hdev, void *data, u16 len)
{
	struct mgmt_mode *cp = data;
	struct pending_cmd *cmd;
	struct hci_request req;
	int err;

	BT_DBG("request for %s", hdev->name);

	if (!lmp_bredr_capable(hdev) || !lmp_le_capable(hdev))
		return cmd_status(sk, hdev->id, MGMT_OP_SET_BREDR,
				  MGMT_STATUS_NOT_SUPPORTED);

	if (!test_bit(HCI_LE_ENABLED, &hdev->dev_flags))
		return cmd_status(sk, hdev->id, MGMT_OP_SET_BREDR,
				  MGMT_STATUS_REJECTED);

	if (cp->val != 0x00 && cp->val != 0x01)
		return cmd_status(sk, hdev->id, MGMT_OP_SET_BREDR,
				  MGMT_STATUS_INVALID_PARAMS);

	hci_dev_lock(hdev);

	if (cp->val == test_bit(HCI_BREDR_ENABLED, &hdev->dev_flags)) {
		err = send_settings_rsp(sk, MGMT_OP_SET_BREDR, hdev);
		goto unlock;
	}

	if (!hdev_is_powered(hdev)) {
		if (!cp->val) {
			clear_bit(HCI_DISCOVERABLE, &hdev->dev_flags);
			clear_bit(HCI_SSP_ENABLED, &hdev->dev_flags);
			clear_bit(HCI_LINK_SECURITY, &hdev->dev_flags);
			clear_bit(HCI_FAST_CONNECTABLE, &hdev->dev_flags);
			clear_bit(HCI_HS_ENABLED, &hdev->dev_flags);
		}

		change_bit(HCI_BREDR_ENABLED, &hdev->dev_flags);

		err = send_settings_rsp(sk, MGMT_OP_SET_BREDR, hdev);
		if (err < 0)
			goto unlock;

		err = new_settings(hdev, sk);
		goto unlock;
	}

	/* Reject disabling when powered on */
	if (!cp->val) {
		err = cmd_status(sk, hdev->id, MGMT_OP_SET_BREDR,
				 MGMT_STATUS_REJECTED);
		goto unlock;
	}

	if (mgmt_pending_find(MGMT_OP_SET_BREDR, hdev)) {
		err = cmd_status(sk, hdev->id, MGMT_OP_SET_BREDR,
				 MGMT_STATUS_BUSY);
		goto unlock;
	}

	cmd = mgmt_pending_add(sk, MGMT_OP_SET_BREDR, hdev, data, len);
	if (!cmd) {
		err = -ENOMEM;
		goto unlock;
	}

	/* We need to flip the bit already here so that update_adv_data
	 * generates the correct flags.
	 */
	set_bit(HCI_BREDR_ENABLED, &hdev->dev_flags);

	hci_req_init(&req, hdev);

	write_fast_connectable(&req, false);
	__hci_update_page_scan(&req);

	/* Since only the advertising data flags will change, there
	 * is no need to update the scan response data.
	 */
	update_adv_data(&req);

	err = hci_req_run(&req, set_bredr_complete);
	if (err < 0)
		mgmt_pending_remove(cmd);

unlock:
	hci_dev_unlock(hdev);
	return err;
}

static int set_secure_conn(struct sock *sk, struct hci_dev *hdev,
			   void *data, u16 len)
{
	struct mgmt_mode *cp = data;
	struct pending_cmd *cmd;
	u8 val;
	int err;

	BT_DBG("request for %s", hdev->name);

	if (!lmp_sc_capable(hdev) &&
	    !test_bit(HCI_LE_ENABLED, &hdev->dev_flags))
		return cmd_status(sk, hdev->id, MGMT_OP_SET_SECURE_CONN,
				  MGMT_STATUS_NOT_SUPPORTED);

	if (cp->val != 0x00 && cp->val != 0x01 && cp->val != 0x02)
		return cmd_status(sk, hdev->id, MGMT_OP_SET_SECURE_CONN,
				  MGMT_STATUS_INVALID_PARAMS);

	hci_dev_lock(hdev);

	if (!hdev_is_powered(hdev) || !lmp_sc_capable(hdev) ||
	    !test_bit(HCI_BREDR_ENABLED, &hdev->dev_flags)) {
		bool changed;

		if (cp->val) {
			changed = !test_and_set_bit(HCI_SC_ENABLED,
						    &hdev->dev_flags);
			if (cp->val == 0x02)
				set_bit(HCI_SC_ONLY, &hdev->dev_flags);
			else
				clear_bit(HCI_SC_ONLY, &hdev->dev_flags);
		} else {
			changed = test_and_clear_bit(HCI_SC_ENABLED,
						     &hdev->dev_flags);
			clear_bit(HCI_SC_ONLY, &hdev->dev_flags);
		}

		err = send_settings_rsp(sk, MGMT_OP_SET_SECURE_CONN, hdev);
		if (err < 0)
			goto failed;

		if (changed)
			err = new_settings(hdev, sk);

		goto failed;
	}

	if (mgmt_pending_find(MGMT_OP_SET_SECURE_CONN, hdev)) {
		err = cmd_status(sk, hdev->id, MGMT_OP_SET_SECURE_CONN,
				 MGMT_STATUS_BUSY);
		goto failed;
	}

	val = !!cp->val;

	if (val == test_bit(HCI_SC_ENABLED, &hdev->dev_flags) &&
	    (cp->val == 0x02) == test_bit(HCI_SC_ONLY, &hdev->dev_flags)) {
		err = send_settings_rsp(sk, MGMT_OP_SET_SECURE_CONN, hdev);
		goto failed;
	}

	cmd = mgmt_pending_add(sk, MGMT_OP_SET_SECURE_CONN, hdev, data, len);
	if (!cmd) {
		err = -ENOMEM;
		goto failed;
	}

	err = hci_send_cmd(hdev, HCI_OP_WRITE_SC_SUPPORT, 1, &val);
	if (err < 0) {
		mgmt_pending_remove(cmd);
		goto failed;
	}

	if (cp->val == 0x02)
		set_bit(HCI_SC_ONLY, &hdev->dev_flags);
	else
		clear_bit(HCI_SC_ONLY, &hdev->dev_flags);

failed:
	hci_dev_unlock(hdev);
	return err;
}

static int set_debug_keys(struct sock *sk, struct hci_dev *hdev,
			  void *data, u16 len)
{
	struct mgmt_mode *cp = data;
	bool changed, use_changed;
	int err;

	BT_DBG("request for %s", hdev->name);

	if (cp->val != 0x00 && cp->val != 0x01 && cp->val != 0x02)
		return cmd_status(sk, hdev->id, MGMT_OP_SET_DEBUG_KEYS,
				  MGMT_STATUS_INVALID_PARAMS);

	hci_dev_lock(hdev);

	if (cp->val)
		changed = !test_and_set_bit(HCI_KEEP_DEBUG_KEYS,
					    &hdev->dev_flags);
	else
		changed = test_and_clear_bit(HCI_KEEP_DEBUG_KEYS,
					     &hdev->dev_flags);

	if (cp->val == 0x02)
		use_changed = !test_and_set_bit(HCI_USE_DEBUG_KEYS,
						&hdev->dev_flags);
	else
		use_changed = test_and_clear_bit(HCI_USE_DEBUG_KEYS,
						 &hdev->dev_flags);

	if (hdev_is_powered(hdev) && use_changed &&
	    test_bit(HCI_SSP_ENABLED, &hdev->dev_flags)) {
		u8 mode = (cp->val == 0x02) ? 0x01 : 0x00;
		hci_send_cmd(hdev, HCI_OP_WRITE_SSP_DEBUG_MODE,
			     sizeof(mode), &mode);
	}

	err = send_settings_rsp(sk, MGMT_OP_SET_DEBUG_KEYS, hdev);
	if (err < 0)
		goto unlock;

	if (changed)
		err = new_settings(hdev, sk);

unlock:
	hci_dev_unlock(hdev);
	return err;
}

static int set_privacy(struct sock *sk, struct hci_dev *hdev, void *cp_data,
		       u16 len)
{
	struct mgmt_cp_set_privacy *cp = cp_data;
	bool changed;
	int err;

	BT_DBG("request for %s", hdev->name);

	if (!lmp_le_capable(hdev))
		return cmd_status(sk, hdev->id, MGMT_OP_SET_PRIVACY,
				  MGMT_STATUS_NOT_SUPPORTED);

	if (cp->privacy != 0x00 && cp->privacy != 0x01)
		return cmd_status(sk, hdev->id, MGMT_OP_SET_PRIVACY,
				  MGMT_STATUS_INVALID_PARAMS);

	if (hdev_is_powered(hdev))
		return cmd_status(sk, hdev->id, MGMT_OP_SET_PRIVACY,
				  MGMT_STATUS_REJECTED);

	hci_dev_lock(hdev);

	/* If user space supports this command it is also expected to
	 * handle IRKs. Therefore, set the HCI_RPA_RESOLVING flag.
	 */
	set_bit(HCI_RPA_RESOLVING, &hdev->dev_flags);

	if (cp->privacy) {
		changed = !test_and_set_bit(HCI_PRIVACY, &hdev->dev_flags);
		memcpy(hdev->irk, cp->irk, sizeof(hdev->irk));
		set_bit(HCI_RPA_EXPIRED, &hdev->dev_flags);
	} else {
		changed = test_and_clear_bit(HCI_PRIVACY, &hdev->dev_flags);
		memset(hdev->irk, 0, sizeof(hdev->irk));
		clear_bit(HCI_RPA_EXPIRED, &hdev->dev_flags);
	}

	err = send_settings_rsp(sk, MGMT_OP_SET_PRIVACY, hdev);
	if (err < 0)
		goto unlock;

	if (changed)
		err = new_settings(hdev, sk);

unlock:
	hci_dev_unlock(hdev);
	return err;
}

static bool irk_is_valid(struct mgmt_irk_info *irk)
{
	switch (irk->addr.type) {
	case BDADDR_LE_PUBLIC:
		return true;

	case BDADDR_LE_RANDOM:
		/* Two most significant bits shall be set */
		if ((irk->addr.bdaddr.b[5] & 0xc0) != 0xc0)
			return false;
		return true;
	}

	return false;
}

static int load_irks(struct sock *sk, struct hci_dev *hdev, void *cp_data,
		     u16 len)
{
	struct mgmt_cp_load_irks *cp = cp_data;
	const u16 max_irk_count = ((U16_MAX - sizeof(*cp)) /
				   sizeof(struct mgmt_irk_info));
	u16 irk_count, expected_len;
	int i, err;

	BT_DBG("request for %s", hdev->name);

	if (!lmp_le_capable(hdev))
		return cmd_status(sk, hdev->id, MGMT_OP_LOAD_IRKS,
				  MGMT_STATUS_NOT_SUPPORTED);

	irk_count = __le16_to_cpu(cp->irk_count);
	if (irk_count > max_irk_count) {
		BT_ERR("load_irks: too big irk_count value %u", irk_count);
		return cmd_status(sk, hdev->id, MGMT_OP_LOAD_IRKS,
				  MGMT_STATUS_INVALID_PARAMS);
	}

	expected_len = sizeof(*cp) + irk_count * sizeof(struct mgmt_irk_info);
	if (expected_len != len) {
		BT_ERR("load_irks: expected %u bytes, got %u bytes",
		       expected_len, len);
		return cmd_status(sk, hdev->id, MGMT_OP_LOAD_IRKS,
				  MGMT_STATUS_INVALID_PARAMS);
	}

	BT_DBG("%s irk_count %u", hdev->name, irk_count);

	for (i = 0; i < irk_count; i++) {
		struct mgmt_irk_info *key = &cp->irks[i];

		if (!irk_is_valid(key))
			return cmd_status(sk, hdev->id,
					  MGMT_OP_LOAD_IRKS,
					  MGMT_STATUS_INVALID_PARAMS);
	}

	hci_dev_lock(hdev);

	hci_smp_irks_clear(hdev);

	for (i = 0; i < irk_count; i++) {
		struct mgmt_irk_info *irk = &cp->irks[i];
		u8 addr_type;

		if (irk->addr.type == BDADDR_LE_PUBLIC)
			addr_type = ADDR_LE_DEV_PUBLIC;
		else
			addr_type = ADDR_LE_DEV_RANDOM;

		hci_add_irk(hdev, &irk->addr.bdaddr, addr_type, irk->val,
			    BDADDR_ANY);
	}

	set_bit(HCI_RPA_RESOLVING, &hdev->dev_flags);

	err = cmd_complete(sk, hdev->id, MGMT_OP_LOAD_IRKS, 0, NULL, 0);

	hci_dev_unlock(hdev);

	return err;
}

static bool ltk_is_valid(struct mgmt_ltk_info *key)
{
	if (key->master != 0x00 && key->master != 0x01)
		return false;

	switch (key->addr.type) {
	case BDADDR_LE_PUBLIC:
		return true;

	case BDADDR_LE_RANDOM:
		/* Two most significant bits shall be set */
		if ((key->addr.bdaddr.b[5] & 0xc0) != 0xc0)
			return false;
		return true;
	}

	return false;
}

static int load_long_term_keys(struct sock *sk, struct hci_dev *hdev,
			       void *cp_data, u16 len)
{
	struct mgmt_cp_load_long_term_keys *cp = cp_data;
	const u16 max_key_count = ((U16_MAX - sizeof(*cp)) /
				   sizeof(struct mgmt_ltk_info));
	u16 key_count, expected_len;
	int i, err;

	BT_DBG("request for %s", hdev->name);

	if (!lmp_le_capable(hdev))
		return cmd_status(sk, hdev->id, MGMT_OP_LOAD_LONG_TERM_KEYS,
				  MGMT_STATUS_NOT_SUPPORTED);

	key_count = __le16_to_cpu(cp->key_count);
	if (key_count > max_key_count) {
		BT_ERR("load_ltks: too big key_count value %u", key_count);
		return cmd_status(sk, hdev->id, MGMT_OP_LOAD_LONG_TERM_KEYS,
				  MGMT_STATUS_INVALID_PARAMS);
	}

	expected_len = sizeof(*cp) + key_count *
					sizeof(struct mgmt_ltk_info);
	if (expected_len != len) {
		BT_ERR("load_keys: expected %u bytes, got %u bytes",
		       expected_len, len);
		return cmd_status(sk, hdev->id, MGMT_OP_LOAD_LONG_TERM_KEYS,
				  MGMT_STATUS_INVALID_PARAMS);
	}

	BT_DBG("%s key_count %u", hdev->name, key_count);

	for (i = 0; i < key_count; i++) {
		struct mgmt_ltk_info *key = &cp->keys[i];

		if (!ltk_is_valid(key))
			return cmd_status(sk, hdev->id,
					  MGMT_OP_LOAD_LONG_TERM_KEYS,
					  MGMT_STATUS_INVALID_PARAMS);
	}

	hci_dev_lock(hdev);

	hci_smp_ltks_clear(hdev);

	for (i = 0; i < key_count; i++) {
		struct mgmt_ltk_info *key = &cp->keys[i];
		u8 type, addr_type, authenticated;

		if (key->addr.type == BDADDR_LE_PUBLIC)
			addr_type = ADDR_LE_DEV_PUBLIC;
		else
			addr_type = ADDR_LE_DEV_RANDOM;

		switch (key->type) {
		case MGMT_LTK_UNAUTHENTICATED:
			authenticated = 0x00;
			type = key->master ? SMP_LTK : SMP_LTK_SLAVE;
			break;
		case MGMT_LTK_AUTHENTICATED:
			authenticated = 0x01;
			type = key->master ? SMP_LTK : SMP_LTK_SLAVE;
			break;
		case MGMT_LTK_P256_UNAUTH:
			authenticated = 0x00;
			type = SMP_LTK_P256;
			break;
		case MGMT_LTK_P256_AUTH:
			authenticated = 0x01;
			type = SMP_LTK_P256;
			break;
		case MGMT_LTK_P256_DEBUG:
			authenticated = 0x00;
			type = SMP_LTK_P256_DEBUG;
		default:
			continue;
		}

		hci_add_ltk(hdev, &key->addr.bdaddr, addr_type, type,
			    authenticated, key->val, key->enc_size, key->ediv,
			    key->rand);
	}

	err = cmd_complete(sk, hdev->id, MGMT_OP_LOAD_LONG_TERM_KEYS, 0,
			   NULL, 0);

	hci_dev_unlock(hdev);

	return err;
}

static int conn_info_cmd_complete(struct pending_cmd *cmd, u8 status)
{
	struct hci_conn *conn = cmd->user_data;
	struct mgmt_rp_get_conn_info rp;
	int err;

	memcpy(&rp.addr, cmd->param, sizeof(rp.addr));

	if (status == MGMT_STATUS_SUCCESS) {
		rp.rssi = conn->rssi;
		rp.tx_power = conn->tx_power;
		rp.max_tx_power = conn->max_tx_power;
	} else {
		rp.rssi = HCI_RSSI_INVALID;
		rp.tx_power = HCI_TX_POWER_INVALID;
		rp.max_tx_power = HCI_TX_POWER_INVALID;
	}

	err = cmd_complete(cmd->sk, cmd->index, MGMT_OP_GET_CONN_INFO, status,
			   &rp, sizeof(rp));

	hci_conn_drop(conn);
	hci_conn_put(conn);

	return err;
}

static void conn_info_refresh_complete(struct hci_dev *hdev, u8 hci_status)
{
	struct hci_cp_read_rssi *cp;
	struct pending_cmd *cmd;
	struct hci_conn *conn;
	u16 handle;
	u8 status;

	BT_DBG("status 0x%02x", hci_status);

	hci_dev_lock(hdev);

	/* Commands sent in request are either Read RSSI or Read Transmit Power
	 * Level so we check which one was last sent to retrieve connection
	 * handle.  Both commands have handle as first parameter so it's safe to
	 * cast data on the same command struct.
	 *
	 * First command sent is always Read RSSI and we fail only if it fails.
	 * In other case we simply override error to indicate success as we
	 * already remembered if TX power value is actually valid.
	 */
	cp = hci_sent_cmd_data(hdev, HCI_OP_READ_RSSI);
	if (!cp) {
		cp = hci_sent_cmd_data(hdev, HCI_OP_READ_TX_POWER);
		status = MGMT_STATUS_SUCCESS;
	} else {
		status = mgmt_status(hci_status);
	}

	if (!cp) {
		BT_ERR("invalid sent_cmd in conn_info response");
		goto unlock;
	}

	handle = __le16_to_cpu(cp->handle);
	conn = hci_conn_hash_lookup_handle(hdev, handle);
	if (!conn) {
		BT_ERR("unknown handle (%d) in conn_info response", handle);
		goto unlock;
	}

	cmd = mgmt_pending_find_data(MGMT_OP_GET_CONN_INFO, hdev, conn);
	if (!cmd)
		goto unlock;

	cmd->cmd_complete(cmd, status);
	mgmt_pending_remove(cmd);

unlock:
	hci_dev_unlock(hdev);
}

static int get_conn_info(struct sock *sk, struct hci_dev *hdev, void *data,
			 u16 len)
{
	struct mgmt_cp_get_conn_info *cp = data;
	struct mgmt_rp_get_conn_info rp;
	struct hci_conn *conn;
	unsigned long conn_info_age;
	int err = 0;

	BT_DBG("%s", hdev->name);

	memset(&rp, 0, sizeof(rp));
	bacpy(&rp.addr.bdaddr, &cp->addr.bdaddr);
	rp.addr.type = cp->addr.type;

	if (!bdaddr_type_is_valid(cp->addr.type))
		return cmd_complete(sk, hdev->id, MGMT_OP_GET_CONN_INFO,
				    MGMT_STATUS_INVALID_PARAMS,
				    &rp, sizeof(rp));

	hci_dev_lock(hdev);

	if (!hdev_is_powered(hdev)) {
		err = cmd_complete(sk, hdev->id, MGMT_OP_GET_CONN_INFO,
				   MGMT_STATUS_NOT_POWERED, &rp, sizeof(rp));
		goto unlock;
	}

	if (cp->addr.type == BDADDR_BREDR)
		conn = hci_conn_hash_lookup_ba(hdev, ACL_LINK,
					       &cp->addr.bdaddr);
	else
		conn = hci_conn_hash_lookup_ba(hdev, LE_LINK, &cp->addr.bdaddr);

	if (!conn || conn->state != BT_CONNECTED) {
		err = cmd_complete(sk, hdev->id, MGMT_OP_GET_CONN_INFO,
				   MGMT_STATUS_NOT_CONNECTED, &rp, sizeof(rp));
		goto unlock;
	}

	if (mgmt_pending_find_data(MGMT_OP_GET_CONN_INFO, hdev, conn)) {
		err = cmd_complete(sk, hdev->id, MGMT_OP_GET_CONN_INFO,
				   MGMT_STATUS_BUSY, &rp, sizeof(rp));
		goto unlock;
	}

	/* To avoid client trying to guess when to poll again for information we
	 * calculate conn info age as random value between min/max set in hdev.
	 */
	conn_info_age = hdev->conn_info_min_age +
			prandom_u32_max(hdev->conn_info_max_age -
					hdev->conn_info_min_age);

	/* Query controller to refresh cached values if they are too old or were
	 * never read.
	 */
	if (time_after(jiffies, conn->conn_info_timestamp +
		       msecs_to_jiffies(conn_info_age)) ||
	    !conn->conn_info_timestamp) {
		struct hci_request req;
		struct hci_cp_read_tx_power req_txp_cp;
		struct hci_cp_read_rssi req_rssi_cp;
		struct pending_cmd *cmd;

		hci_req_init(&req, hdev);
		req_rssi_cp.handle = cpu_to_le16(conn->handle);
		hci_req_add(&req, HCI_OP_READ_RSSI, sizeof(req_rssi_cp),
			    &req_rssi_cp);

		/* For LE links TX power does not change thus we don't need to
		 * query for it once value is known.
		 */
		if (!bdaddr_type_is_le(cp->addr.type) ||
		    conn->tx_power == HCI_TX_POWER_INVALID) {
			req_txp_cp.handle = cpu_to_le16(conn->handle);
			req_txp_cp.type = 0x00;
			hci_req_add(&req, HCI_OP_READ_TX_POWER,
				    sizeof(req_txp_cp), &req_txp_cp);
		}

		/* Max TX power needs to be read only once per connection */
		if (conn->max_tx_power == HCI_TX_POWER_INVALID) {
			req_txp_cp.handle = cpu_to_le16(conn->handle);
			req_txp_cp.type = 0x01;
			hci_req_add(&req, HCI_OP_READ_TX_POWER,
				    sizeof(req_txp_cp), &req_txp_cp);
		}

		err = hci_req_run(&req, conn_info_refresh_complete);
		if (err < 0)
			goto unlock;

		cmd = mgmt_pending_add(sk, MGMT_OP_GET_CONN_INFO, hdev,
				       data, len);
		if (!cmd) {
			err = -ENOMEM;
			goto unlock;
		}

		hci_conn_hold(conn);
		cmd->user_data = hci_conn_get(conn);
		cmd->cmd_complete = conn_info_cmd_complete;

		conn->conn_info_timestamp = jiffies;
	} else {
		/* Cache is valid, just reply with values cached in hci_conn */
		rp.rssi = conn->rssi;
		rp.tx_power = conn->tx_power;
		rp.max_tx_power = conn->max_tx_power;

		err = cmd_complete(sk, hdev->id, MGMT_OP_GET_CONN_INFO,
				   MGMT_STATUS_SUCCESS, &rp, sizeof(rp));
	}

unlock:
	hci_dev_unlock(hdev);
	return err;
}

static int clock_info_cmd_complete(struct pending_cmd *cmd, u8 status)
{
	struct hci_conn *conn = cmd->user_data;
	struct mgmt_rp_get_clock_info rp;
	struct hci_dev *hdev;
	int err;

	memset(&rp, 0, sizeof(rp));
	memcpy(&rp.addr, &cmd->param, sizeof(rp.addr));

	if (status)
		goto complete;

	hdev = hci_dev_get(cmd->index);
	if (hdev) {
		rp.local_clock = cpu_to_le32(hdev->clock);
		hci_dev_put(hdev);
	}

	if (conn) {
		rp.piconet_clock = cpu_to_le32(conn->clock);
		rp.accuracy = cpu_to_le16(conn->clock_accuracy);
	}

complete:
	err = cmd_complete(cmd->sk, cmd->index, cmd->opcode, status, &rp,
			   sizeof(rp));

	if (conn) {
		hci_conn_drop(conn);
		hci_conn_put(conn);
	}

	return err;
}

static void get_clock_info_complete(struct hci_dev *hdev, u8 status)
{
	struct hci_cp_read_clock *hci_cp;
	struct pending_cmd *cmd;
	struct hci_conn *conn;

	BT_DBG("%s status %u", hdev->name, status);

	hci_dev_lock(hdev);

	hci_cp = hci_sent_cmd_data(hdev, HCI_OP_READ_CLOCK);
	if (!hci_cp)
		goto unlock;

	if (hci_cp->which) {
		u16 handle = __le16_to_cpu(hci_cp->handle);
		conn = hci_conn_hash_lookup_handle(hdev, handle);
	} else {
		conn = NULL;
	}

	cmd = mgmt_pending_find_data(MGMT_OP_GET_CLOCK_INFO, hdev, conn);
	if (!cmd)
		goto unlock;

	cmd->cmd_complete(cmd, mgmt_status(status));
	mgmt_pending_remove(cmd);

unlock:
	hci_dev_unlock(hdev);
}

static int get_clock_info(struct sock *sk, struct hci_dev *hdev, void *data,
			 u16 len)
{
	struct mgmt_cp_get_clock_info *cp = data;
	struct mgmt_rp_get_clock_info rp;
	struct hci_cp_read_clock hci_cp;
	struct pending_cmd *cmd;
	struct hci_request req;
	struct hci_conn *conn;
	int err;

	BT_DBG("%s", hdev->name);

	memset(&rp, 0, sizeof(rp));
	bacpy(&rp.addr.bdaddr, &cp->addr.bdaddr);
	rp.addr.type = cp->addr.type;

	if (cp->addr.type != BDADDR_BREDR)
		return cmd_complete(sk, hdev->id, MGMT_OP_GET_CLOCK_INFO,
				    MGMT_STATUS_INVALID_PARAMS,
				    &rp, sizeof(rp));

	hci_dev_lock(hdev);

	if (!hdev_is_powered(hdev)) {
		err = cmd_complete(sk, hdev->id, MGMT_OP_GET_CLOCK_INFO,
				   MGMT_STATUS_NOT_POWERED, &rp, sizeof(rp));
		goto unlock;
	}

	if (bacmp(&cp->addr.bdaddr, BDADDR_ANY)) {
		conn = hci_conn_hash_lookup_ba(hdev, ACL_LINK,
					       &cp->addr.bdaddr);
		if (!conn || conn->state != BT_CONNECTED) {
			err = cmd_complete(sk, hdev->id,
					   MGMT_OP_GET_CLOCK_INFO,
					   MGMT_STATUS_NOT_CONNECTED,
					   &rp, sizeof(rp));
			goto unlock;
		}
	} else {
		conn = NULL;
	}

	cmd = mgmt_pending_add(sk, MGMT_OP_GET_CLOCK_INFO, hdev, data, len);
	if (!cmd) {
		err = -ENOMEM;
		goto unlock;
	}

	cmd->cmd_complete = clock_info_cmd_complete;

	hci_req_init(&req, hdev);

	memset(&hci_cp, 0, sizeof(hci_cp));
	hci_req_add(&req, HCI_OP_READ_CLOCK, sizeof(hci_cp), &hci_cp);

	if (conn) {
		hci_conn_hold(conn);
		cmd->user_data = hci_conn_get(conn);

		hci_cp.handle = cpu_to_le16(conn->handle);
		hci_cp.which = 0x01; /* Piconet clock */
		hci_req_add(&req, HCI_OP_READ_CLOCK, sizeof(hci_cp), &hci_cp);
	}

	err = hci_req_run(&req, get_clock_info_complete);
	if (err < 0)
		mgmt_pending_remove(cmd);

unlock:
	hci_dev_unlock(hdev);
	return err;
}

static bool is_connected(struct hci_dev *hdev, bdaddr_t *addr, u8 type)
{
	struct hci_conn *conn;

	conn = hci_conn_hash_lookup_ba(hdev, LE_LINK, addr);
	if (!conn)
		return false;

	if (conn->dst_type != type)
		return false;

	if (conn->state != BT_CONNECTED)
		return false;

	return true;
}

/* This function requires the caller holds hdev->lock */
static int hci_conn_params_set(struct hci_request *req, bdaddr_t *addr,
			       u8 addr_type, u8 auto_connect)
{
	struct hci_dev *hdev = req->hdev;
	struct hci_conn_params *params;

	params = hci_conn_params_add(hdev, addr, addr_type);
	if (!params)
		return -EIO;

	if (params->auto_connect == auto_connect)
		return 0;

	list_del_init(&params->action);

	switch (auto_connect) {
	case HCI_AUTO_CONN_DISABLED:
	case HCI_AUTO_CONN_LINK_LOSS:
		__hci_update_background_scan(req);
		break;
	case HCI_AUTO_CONN_REPORT:
		list_add(&params->action, &hdev->pend_le_reports);
		__hci_update_background_scan(req);
		break;
	case HCI_AUTO_CONN_DIRECT:
	case HCI_AUTO_CONN_ALWAYS:
		if (!is_connected(hdev, addr, addr_type)) {
			list_add(&params->action, &hdev->pend_le_conns);
			__hci_update_background_scan(req);
		}
		break;
	}

	params->auto_connect = auto_connect;

	BT_DBG("addr %pMR (type %u) auto_connect %u", addr, addr_type,
	       auto_connect);

	return 0;
}

static void device_added(struct sock *sk, struct hci_dev *hdev,
			 bdaddr_t *bdaddr, u8 type, u8 action)
{
	struct mgmt_ev_device_added ev;

	bacpy(&ev.addr.bdaddr, bdaddr);
	ev.addr.type = type;
	ev.action = action;

	mgmt_event(MGMT_EV_DEVICE_ADDED, hdev, &ev, sizeof(ev), sk);
}

static void add_device_complete(struct hci_dev *hdev, u8 status)
{
	struct pending_cmd *cmd;

	BT_DBG("status 0x%02x", status);

	hci_dev_lock(hdev);

	cmd = mgmt_pending_find(MGMT_OP_ADD_DEVICE, hdev);
	if (!cmd)
		goto unlock;

	cmd->cmd_complete(cmd, mgmt_status(status));
	mgmt_pending_remove(cmd);

unlock:
	hci_dev_unlock(hdev);
}

static int add_device(struct sock *sk, struct hci_dev *hdev,
		      void *data, u16 len)
{
	struct mgmt_cp_add_device *cp = data;
	struct pending_cmd *cmd;
	struct hci_request req;
	u8 auto_conn, addr_type;
	int err;

	BT_DBG("%s", hdev->name);

	if (!bdaddr_type_is_valid(cp->addr.type) ||
	    !bacmp(&cp->addr.bdaddr, BDADDR_ANY))
		return cmd_complete(sk, hdev->id, MGMT_OP_ADD_DEVICE,
				    MGMT_STATUS_INVALID_PARAMS,
				    &cp->addr, sizeof(cp->addr));

	if (cp->action != 0x00 && cp->action != 0x01 && cp->action != 0x02)
		return cmd_complete(sk, hdev->id, MGMT_OP_ADD_DEVICE,
				    MGMT_STATUS_INVALID_PARAMS,
				    &cp->addr, sizeof(cp->addr));

	hci_req_init(&req, hdev);

	hci_dev_lock(hdev);

	cmd = mgmt_pending_add(sk, MGMT_OP_ADD_DEVICE, hdev, data, len);
	if (!cmd) {
		err = -ENOMEM;
		goto unlock;
	}

	cmd->cmd_complete = addr_cmd_complete;

	if (cp->addr.type == BDADDR_BREDR) {
		/* Only incoming connections action is supported for now */
		if (cp->action != 0x01) {
			err = cmd->cmd_complete(cmd,
						MGMT_STATUS_INVALID_PARAMS);
			mgmt_pending_remove(cmd);
			goto unlock;
		}

		err = hci_bdaddr_list_add(&hdev->whitelist, &cp->addr.bdaddr,
					  cp->addr.type);
		if (err)
			goto unlock;

		__hci_update_page_scan(&req);

		goto added;
	}

	if (cp->addr.type == BDADDR_LE_PUBLIC)
		addr_type = ADDR_LE_DEV_PUBLIC;
	else
		addr_type = ADDR_LE_DEV_RANDOM;

	if (cp->action == 0x02)
		auto_conn = HCI_AUTO_CONN_ALWAYS;
	else if (cp->action == 0x01)
		auto_conn = HCI_AUTO_CONN_DIRECT;
	else
		auto_conn = HCI_AUTO_CONN_REPORT;

	/* If the connection parameters don't exist for this device,
	 * they will be created and configured with defaults.
	 */
	if (hci_conn_params_set(&req, &cp->addr.bdaddr, addr_type,
				auto_conn) < 0) {
		err = cmd->cmd_complete(cmd, MGMT_STATUS_FAILED);
		mgmt_pending_remove(cmd);
		goto unlock;
	}

added:
	device_added(sk, hdev, &cp->addr.bdaddr, cp->addr.type, cp->action);

	err = hci_req_run(&req, add_device_complete);
	if (err < 0) {
		/* ENODATA means no HCI commands were needed (e.g. if
		 * the adapter is powered off).
		 */
		if (err == -ENODATA)
			err = cmd->cmd_complete(cmd, MGMT_STATUS_SUCCESS);
		mgmt_pending_remove(cmd);
	}

unlock:
	hci_dev_unlock(hdev);
	return err;
}

static void device_removed(struct sock *sk, struct hci_dev *hdev,
			   bdaddr_t *bdaddr, u8 type)
{
	struct mgmt_ev_device_removed ev;

	bacpy(&ev.addr.bdaddr, bdaddr);
	ev.addr.type = type;

	mgmt_event(MGMT_EV_DEVICE_REMOVED, hdev, &ev, sizeof(ev), sk);
}

static void remove_device_complete(struct hci_dev *hdev, u8 status)
{
	struct pending_cmd *cmd;

	BT_DBG("status 0x%02x", status);

	hci_dev_lock(hdev);

	cmd = mgmt_pending_find(MGMT_OP_REMOVE_DEVICE, hdev);
	if (!cmd)
		goto unlock;

	cmd->cmd_complete(cmd, mgmt_status(status));
	mgmt_pending_remove(cmd);

unlock:
	hci_dev_unlock(hdev);
}

static int remove_device(struct sock *sk, struct hci_dev *hdev,
			 void *data, u16 len)
{
	struct mgmt_cp_remove_device *cp = data;
	struct pending_cmd *cmd;
	struct hci_request req;
	int err;

	BT_DBG("%s", hdev->name);

	hci_req_init(&req, hdev);

	hci_dev_lock(hdev);

	cmd = mgmt_pending_add(sk, MGMT_OP_REMOVE_DEVICE, hdev, data, len);
	if (!cmd) {
		err = -ENOMEM;
		goto unlock;
	}

	cmd->cmd_complete = addr_cmd_complete;

	if (bacmp(&cp->addr.bdaddr, BDADDR_ANY)) {
		struct hci_conn_params *params;
		u8 addr_type;

		if (!bdaddr_type_is_valid(cp->addr.type)) {
			err = cmd->cmd_complete(cmd,
						MGMT_STATUS_INVALID_PARAMS);
			mgmt_pending_remove(cmd);
			goto unlock;
		}

		if (cp->addr.type == BDADDR_BREDR) {
			err = hci_bdaddr_list_del(&hdev->whitelist,
						  &cp->addr.bdaddr,
						  cp->addr.type);
			if (err) {
				err = cmd->cmd_complete(cmd,
							MGMT_STATUS_INVALID_PARAMS);
				mgmt_pending_remove(cmd);
				goto unlock;
			}

			__hci_update_page_scan(&req);

			device_removed(sk, hdev, &cp->addr.bdaddr,
				       cp->addr.type);
			goto complete;
		}

		if (cp->addr.type == BDADDR_LE_PUBLIC)
			addr_type = ADDR_LE_DEV_PUBLIC;
		else
			addr_type = ADDR_LE_DEV_RANDOM;

		params = hci_conn_params_lookup(hdev, &cp->addr.bdaddr,
						addr_type);
		if (!params) {
			err = cmd->cmd_complete(cmd,
						MGMT_STATUS_INVALID_PARAMS);
			mgmt_pending_remove(cmd);
			goto unlock;
		}

		if (params->auto_connect == HCI_AUTO_CONN_DISABLED) {
			err = cmd->cmd_complete(cmd,
						MGMT_STATUS_INVALID_PARAMS);
			mgmt_pending_remove(cmd);
			goto unlock;
		}

		list_del(&params->action);
		list_del(&params->list);
		kfree(params);
		__hci_update_background_scan(&req);

		device_removed(sk, hdev, &cp->addr.bdaddr, cp->addr.type);
	} else {
		struct hci_conn_params *p, *tmp;
		struct bdaddr_list *b, *btmp;

		if (cp->addr.type) {
			err = cmd->cmd_complete(cmd,
						MGMT_STATUS_INVALID_PARAMS);
			mgmt_pending_remove(cmd);
			goto unlock;
		}

		list_for_each_entry_safe(b, btmp, &hdev->whitelist, list) {
			device_removed(sk, hdev, &b->bdaddr, b->bdaddr_type);
			list_del(&b->list);
			kfree(b);
		}

		__hci_update_page_scan(&req);

		list_for_each_entry_safe(p, tmp, &hdev->le_conn_params, list) {
			if (p->auto_connect == HCI_AUTO_CONN_DISABLED)
				continue;
			device_removed(sk, hdev, &p->addr, p->addr_type);
			list_del(&p->action);
			list_del(&p->list);
			kfree(p);
		}

		BT_DBG("All LE connection parameters were removed");

		__hci_update_background_scan(&req);
	}

complete:
	err = hci_req_run(&req, remove_device_complete);
	if (err < 0) {
		/* ENODATA means no HCI commands were needed (e.g. if
		 * the adapter is powered off).
		 */
		if (err == -ENODATA)
			err = cmd->cmd_complete(cmd, MGMT_STATUS_SUCCESS);
		mgmt_pending_remove(cmd);
	}

unlock:
	hci_dev_unlock(hdev);
	return err;
}

static int load_conn_param(struct sock *sk, struct hci_dev *hdev, void *data,
			   u16 len)
{
	struct mgmt_cp_load_conn_param *cp = data;
	const u16 max_param_count = ((U16_MAX - sizeof(*cp)) /
				     sizeof(struct mgmt_conn_param));
	u16 param_count, expected_len;
	int i;

	if (!lmp_le_capable(hdev))
		return cmd_status(sk, hdev->id, MGMT_OP_LOAD_CONN_PARAM,
				  MGMT_STATUS_NOT_SUPPORTED);

	param_count = __le16_to_cpu(cp->param_count);
	if (param_count > max_param_count) {
		BT_ERR("load_conn_param: too big param_count value %u",
		       param_count);
		return cmd_status(sk, hdev->id, MGMT_OP_LOAD_CONN_PARAM,
				  MGMT_STATUS_INVALID_PARAMS);
	}

	expected_len = sizeof(*cp) + param_count *
					sizeof(struct mgmt_conn_param);
	if (expected_len != len) {
		BT_ERR("load_conn_param: expected %u bytes, got %u bytes",
		       expected_len, len);
		return cmd_status(sk, hdev->id, MGMT_OP_LOAD_CONN_PARAM,
				  MGMT_STATUS_INVALID_PARAMS);
	}

	BT_DBG("%s param_count %u", hdev->name, param_count);

	hci_dev_lock(hdev);

	hci_conn_params_clear_disabled(hdev);

	for (i = 0; i < param_count; i++) {
		struct mgmt_conn_param *param = &cp->params[i];
		struct hci_conn_params *hci_param;
		u16 min, max, latency, timeout;
		u8 addr_type;

		BT_DBG("Adding %pMR (type %u)", &param->addr.bdaddr,
		       param->addr.type);

		if (param->addr.type == BDADDR_LE_PUBLIC) {
			addr_type = ADDR_LE_DEV_PUBLIC;
		} else if (param->addr.type == BDADDR_LE_RANDOM) {
			addr_type = ADDR_LE_DEV_RANDOM;
		} else {
			BT_ERR("Ignoring invalid connection parameters");
			continue;
		}

		min = le16_to_cpu(param->min_interval);
		max = le16_to_cpu(param->max_interval);
		latency = le16_to_cpu(param->latency);
		timeout = le16_to_cpu(param->timeout);

		BT_DBG("min 0x%04x max 0x%04x latency 0x%04x timeout 0x%04x",
		       min, max, latency, timeout);

		if (hci_check_conn_params(min, max, latency, timeout) < 0) {
			BT_ERR("Ignoring invalid connection parameters");
			continue;
		}

		hci_param = hci_conn_params_add(hdev, &param->addr.bdaddr,
						addr_type);
		if (!hci_param) {
			BT_ERR("Failed to add connection parameters");
			continue;
		}

		hci_param->conn_min_interval = min;
		hci_param->conn_max_interval = max;
		hci_param->conn_latency = latency;
		hci_param->supervision_timeout = timeout;
	}

	hci_dev_unlock(hdev);

	return cmd_complete(sk, hdev->id, MGMT_OP_LOAD_CONN_PARAM, 0, NULL, 0);
}

static int set_external_config(struct sock *sk, struct hci_dev *hdev,
			       void *data, u16 len)
{
	struct mgmt_cp_set_external_config *cp = data;
	bool changed;
	int err;

	BT_DBG("%s", hdev->name);

	if (hdev_is_powered(hdev))
		return cmd_status(sk, hdev->id, MGMT_OP_SET_EXTERNAL_CONFIG,
				  MGMT_STATUS_REJECTED);

	if (cp->config != 0x00 && cp->config != 0x01)
		return cmd_status(sk, hdev->id, MGMT_OP_SET_EXTERNAL_CONFIG,
				    MGMT_STATUS_INVALID_PARAMS);

	if (!test_bit(HCI_QUIRK_EXTERNAL_CONFIG, &hdev->quirks))
		return cmd_status(sk, hdev->id, MGMT_OP_SET_EXTERNAL_CONFIG,
				  MGMT_STATUS_NOT_SUPPORTED);

	hci_dev_lock(hdev);

	if (cp->config)
		changed = !test_and_set_bit(HCI_EXT_CONFIGURED,
					    &hdev->dev_flags);
	else
		changed = test_and_clear_bit(HCI_EXT_CONFIGURED,
					     &hdev->dev_flags);

	err = send_options_rsp(sk, MGMT_OP_SET_EXTERNAL_CONFIG, hdev);
	if (err < 0)
		goto unlock;

	if (!changed)
		goto unlock;

	err = new_options(hdev, sk);

	if (test_bit(HCI_UNCONFIGURED, &hdev->dev_flags) == is_configured(hdev)) {
		mgmt_index_removed(hdev);

		if (test_and_change_bit(HCI_UNCONFIGURED, &hdev->dev_flags)) {
			set_bit(HCI_CONFIG, &hdev->dev_flags);
			set_bit(HCI_AUTO_OFF, &hdev->dev_flags);

			queue_work(hdev->req_workqueue, &hdev->power_on);
		} else {
			set_bit(HCI_RAW, &hdev->flags);
			mgmt_index_added(hdev);
		}
	}

unlock:
	hci_dev_unlock(hdev);
	return err;
}

static int set_public_address(struct sock *sk, struct hci_dev *hdev,
			      void *data, u16 len)
{
	struct mgmt_cp_set_public_address *cp = data;
	bool changed;
	int err;

	BT_DBG("%s", hdev->name);

	if (hdev_is_powered(hdev))
		return cmd_status(sk, hdev->id, MGMT_OP_SET_PUBLIC_ADDRESS,
				  MGMT_STATUS_REJECTED);

	if (!bacmp(&cp->bdaddr, BDADDR_ANY))
		return cmd_status(sk, hdev->id, MGMT_OP_SET_PUBLIC_ADDRESS,
				  MGMT_STATUS_INVALID_PARAMS);

	if (!hdev->set_bdaddr)
		return cmd_status(sk, hdev->id, MGMT_OP_SET_PUBLIC_ADDRESS,
				  MGMT_STATUS_NOT_SUPPORTED);

	hci_dev_lock(hdev);

	changed = !!bacmp(&hdev->public_addr, &cp->bdaddr);
	bacpy(&hdev->public_addr, &cp->bdaddr);

	err = send_options_rsp(sk, MGMT_OP_SET_PUBLIC_ADDRESS, hdev);
	if (err < 0)
		goto unlock;

	if (!changed)
		goto unlock;

	if (test_bit(HCI_UNCONFIGURED, &hdev->dev_flags))
		err = new_options(hdev, sk);

	if (is_configured(hdev)) {
		mgmt_index_removed(hdev);

		clear_bit(HCI_UNCONFIGURED, &hdev->dev_flags);

		set_bit(HCI_CONFIG, &hdev->dev_flags);
		set_bit(HCI_AUTO_OFF, &hdev->dev_flags);

		queue_work(hdev->req_workqueue, &hdev->power_on);
	}

unlock:
	hci_dev_unlock(hdev);
	return err;
}

static const struct mgmt_handler {
	int (*func) (struct sock *sk, struct hci_dev *hdev, void *data,
		     u16 data_len);
	bool var_len;
	size_t data_len;
} mgmt_handlers[] = {
	{ NULL }, /* 0x0000 (no command) */
	{ read_version,           false, MGMT_READ_VERSION_SIZE },
	{ read_commands,          false, MGMT_READ_COMMANDS_SIZE },
	{ read_index_list,        false, MGMT_READ_INDEX_LIST_SIZE },
	{ read_controller_info,   false, MGMT_READ_INFO_SIZE },
	{ set_powered,            false, MGMT_SETTING_SIZE },
	{ set_discoverable,       false, MGMT_SET_DISCOVERABLE_SIZE },
	{ set_connectable,        false, MGMT_SETTING_SIZE },
	{ set_fast_connectable,   false, MGMT_SETTING_SIZE },
	{ set_bondable,           false, MGMT_SETTING_SIZE },
	{ set_link_security,      false, MGMT_SETTING_SIZE },
	{ set_ssp,                false, MGMT_SETTING_SIZE },
	{ set_hs,                 false, MGMT_SETTING_SIZE },
	{ set_le,                 false, MGMT_SETTING_SIZE },
	{ set_dev_class,          false, MGMT_SET_DEV_CLASS_SIZE },
	{ set_local_name,         false, MGMT_SET_LOCAL_NAME_SIZE },
	{ add_uuid,               false, MGMT_ADD_UUID_SIZE },
	{ remove_uuid,            false, MGMT_REMOVE_UUID_SIZE },
	{ load_link_keys,         true,  MGMT_LOAD_LINK_KEYS_SIZE },
	{ load_long_term_keys,    true,  MGMT_LOAD_LONG_TERM_KEYS_SIZE },
	{ disconnect,             false, MGMT_DISCONNECT_SIZE },
	{ get_connections,        false, MGMT_GET_CONNECTIONS_SIZE },
	{ pin_code_reply,         false, MGMT_PIN_CODE_REPLY_SIZE },
	{ pin_code_neg_reply,     false, MGMT_PIN_CODE_NEG_REPLY_SIZE },
	{ set_io_capability,      false, MGMT_SET_IO_CAPABILITY_SIZE },
	{ pair_device,            false, MGMT_PAIR_DEVICE_SIZE },
	{ cancel_pair_device,     false, MGMT_CANCEL_PAIR_DEVICE_SIZE },
	{ unpair_device,          false, MGMT_UNPAIR_DEVICE_SIZE },
	{ user_confirm_reply,     false, MGMT_USER_CONFIRM_REPLY_SIZE },
	{ user_confirm_neg_reply, false, MGMT_USER_CONFIRM_NEG_REPLY_SIZE },
	{ user_passkey_reply,     false, MGMT_USER_PASSKEY_REPLY_SIZE },
	{ user_passkey_neg_reply, false, MGMT_USER_PASSKEY_NEG_REPLY_SIZE },
	{ read_local_oob_data,    false, MGMT_READ_LOCAL_OOB_DATA_SIZE },
	{ add_remote_oob_data,    true,  MGMT_ADD_REMOTE_OOB_DATA_SIZE },
	{ remove_remote_oob_data, false, MGMT_REMOVE_REMOTE_OOB_DATA_SIZE },
	{ start_discovery,        false, MGMT_START_DISCOVERY_SIZE },
	{ stop_discovery,         false, MGMT_STOP_DISCOVERY_SIZE },
	{ confirm_name,           false, MGMT_CONFIRM_NAME_SIZE },
	{ block_device,           false, MGMT_BLOCK_DEVICE_SIZE },
	{ unblock_device,         false, MGMT_UNBLOCK_DEVICE_SIZE },
	{ set_device_id,          false, MGMT_SET_DEVICE_ID_SIZE },
	{ set_advertising,        false, MGMT_SETTING_SIZE },
	{ set_bredr,              false, MGMT_SETTING_SIZE },
	{ set_static_address,     false, MGMT_SET_STATIC_ADDRESS_SIZE },
	{ set_scan_params,        false, MGMT_SET_SCAN_PARAMS_SIZE },
	{ set_secure_conn,        false, MGMT_SETTING_SIZE },
	{ set_debug_keys,         false, MGMT_SETTING_SIZE },
	{ set_privacy,            false, MGMT_SET_PRIVACY_SIZE },
	{ load_irks,              true,  MGMT_LOAD_IRKS_SIZE },
	{ get_conn_info,          false, MGMT_GET_CONN_INFO_SIZE },
	{ get_clock_info,         false, MGMT_GET_CLOCK_INFO_SIZE },
	{ add_device,             false, MGMT_ADD_DEVICE_SIZE },
	{ remove_device,          false, MGMT_REMOVE_DEVICE_SIZE },
	{ load_conn_param,        true,  MGMT_LOAD_CONN_PARAM_SIZE },
	{ read_unconf_index_list, false, MGMT_READ_UNCONF_INDEX_LIST_SIZE },
	{ read_config_info,       false, MGMT_READ_CONFIG_INFO_SIZE },
	{ set_external_config,    false, MGMT_SET_EXTERNAL_CONFIG_SIZE },
	{ set_public_address,     false, MGMT_SET_PUBLIC_ADDRESS_SIZE },
	{ start_service_discovery,true,  MGMT_START_SERVICE_DISCOVERY_SIZE },
};

int mgmt_control(struct sock *sk, struct msghdr *msg, size_t msglen)
{
	void *buf;
	u8 *cp;
	struct mgmt_hdr *hdr;
	u16 opcode, index, len;
	struct hci_dev *hdev = NULL;
	const struct mgmt_handler *handler;
	int err;

	BT_DBG("got %zu bytes", msglen);

	if (msglen < sizeof(*hdr))
		return -EINVAL;

	buf = kmalloc(msglen, GFP_KERNEL);
	if (!buf)
		return -ENOMEM;

	if (memcpy_from_msg(buf, msg, msglen)) {
		err = -EFAULT;
		goto done;
	}

	hdr = buf;
	opcode = __le16_to_cpu(hdr->opcode);
	index = __le16_to_cpu(hdr->index);
	len = __le16_to_cpu(hdr->len);

	if (len != msglen - sizeof(*hdr)) {
		err = -EINVAL;
		goto done;
	}

	if (index != MGMT_INDEX_NONE) {
		hdev = hci_dev_get(index);
		if (!hdev) {
			err = cmd_status(sk, index, opcode,
					 MGMT_STATUS_INVALID_INDEX);
			goto done;
		}

		if (test_bit(HCI_SETUP, &hdev->dev_flags) ||
		    test_bit(HCI_CONFIG, &hdev->dev_flags) ||
		    test_bit(HCI_USER_CHANNEL, &hdev->dev_flags)) {
			err = cmd_status(sk, index, opcode,
					 MGMT_STATUS_INVALID_INDEX);
			goto done;
		}

		if (test_bit(HCI_UNCONFIGURED, &hdev->dev_flags) &&
		    opcode != MGMT_OP_READ_CONFIG_INFO &&
		    opcode != MGMT_OP_SET_EXTERNAL_CONFIG &&
		    opcode != MGMT_OP_SET_PUBLIC_ADDRESS) {
			err = cmd_status(sk, index, opcode,
					 MGMT_STATUS_INVALID_INDEX);
			goto done;
		}
	}

	if (opcode >= ARRAY_SIZE(mgmt_handlers) ||
	    mgmt_handlers[opcode].func == NULL) {
		BT_DBG("Unknown op %u", opcode);
		err = cmd_status(sk, index, opcode,
				 MGMT_STATUS_UNKNOWN_COMMAND);
		goto done;
	}

	if (hdev && (opcode <= MGMT_OP_READ_INDEX_LIST ||
		     opcode == MGMT_OP_READ_UNCONF_INDEX_LIST)) {
		err = cmd_status(sk, index, opcode,
				 MGMT_STATUS_INVALID_INDEX);
		goto done;
	}

	if (!hdev && (opcode > MGMT_OP_READ_INDEX_LIST &&
		      opcode != MGMT_OP_READ_UNCONF_INDEX_LIST)) {
		err = cmd_status(sk, index, opcode,
				 MGMT_STATUS_INVALID_INDEX);
		goto done;
	}

	handler = &mgmt_handlers[opcode];

	if ((handler->var_len && len < handler->data_len) ||
	    (!handler->var_len && len != handler->data_len)) {
		err = cmd_status(sk, index, opcode,
				 MGMT_STATUS_INVALID_PARAMS);
		goto done;
	}

	if (hdev)
		mgmt_init_hdev(sk, hdev);

	cp = buf + sizeof(*hdr);

	err = handler->func(sk, hdev, cp, len);
	if (err < 0)
		goto done;

	err = msglen;

done:
	if (hdev)
		hci_dev_put(hdev);

	kfree(buf);
	return err;
}

void mgmt_index_added(struct hci_dev *hdev)
{
	if (hdev->dev_type != HCI_BREDR)
		return;

	if (test_bit(HCI_QUIRK_RAW_DEVICE, &hdev->quirks))
		return;

	if (test_bit(HCI_UNCONFIGURED, &hdev->dev_flags))
		mgmt_event(MGMT_EV_UNCONF_INDEX_ADDED, hdev, NULL, 0, NULL);
	else
		mgmt_event(MGMT_EV_INDEX_ADDED, hdev, NULL, 0, NULL);
}

void mgmt_index_removed(struct hci_dev *hdev)
{
	u8 status = MGMT_STATUS_INVALID_INDEX;

	if (hdev->dev_type != HCI_BREDR)
		return;

	if (test_bit(HCI_QUIRK_RAW_DEVICE, &hdev->quirks))
		return;

	mgmt_pending_foreach(0, hdev, cmd_complete_rsp, &status);

	if (test_bit(HCI_UNCONFIGURED, &hdev->dev_flags))
		mgmt_event(MGMT_EV_UNCONF_INDEX_REMOVED, hdev, NULL, 0, NULL);
	else
		mgmt_event(MGMT_EV_INDEX_REMOVED, hdev, NULL, 0, NULL);
}

/* This function requires the caller holds hdev->lock */
static void restart_le_actions(struct hci_request *req)
{
	struct hci_dev *hdev = req->hdev;
	struct hci_conn_params *p;

	list_for_each_entry(p, &hdev->le_conn_params, list) {
		/* Needed for AUTO_OFF case where might not "really"
		 * have been powered off.
		 */
		list_del_init(&p->action);

		switch (p->auto_connect) {
		case HCI_AUTO_CONN_DIRECT:
		case HCI_AUTO_CONN_ALWAYS:
			list_add(&p->action, &hdev->pend_le_conns);
			break;
		case HCI_AUTO_CONN_REPORT:
			list_add(&p->action, &hdev->pend_le_reports);
			break;
		default:
			break;
		}
	}

	__hci_update_background_scan(req);
}

static void powered_complete(struct hci_dev *hdev, u8 status)
{
	struct cmd_lookup match = { NULL, hdev };

	BT_DBG("status 0x%02x", status);

	hci_dev_lock(hdev);

	mgmt_pending_foreach(MGMT_OP_SET_POWERED, hdev, settings_rsp, &match);

	new_settings(hdev, match.sk);

	hci_dev_unlock(hdev);

	if (match.sk)
		sock_put(match.sk);
}

static int powered_update_hci(struct hci_dev *hdev)
{
	struct hci_request req;
	u8 link_sec;

	hci_req_init(&req, hdev);

	if (test_bit(HCI_SSP_ENABLED, &hdev->dev_flags) &&
	    !lmp_host_ssp_capable(hdev)) {
		u8 ssp = 1;

		hci_req_add(&req, HCI_OP_WRITE_SSP_MODE, 1, &ssp);
	}

	if (bredr_sc_enabled(hdev) && !lmp_host_sc_capable(hdev)) {
		u8 sc = 0x01;
		hci_req_add(&req, HCI_OP_WRITE_SC_SUPPORT, sizeof(sc), &sc);
	}

	if (test_bit(HCI_LE_ENABLED, &hdev->dev_flags) &&
	    lmp_bredr_capable(hdev)) {
		struct hci_cp_write_le_host_supported cp;

		cp.le = 0x01;
		cp.simul = 0x00;

		/* Check first if we already have the right
		 * host state (host features set)
		 */
		if (cp.le != lmp_host_le_capable(hdev) ||
		    cp.simul != lmp_host_le_br_capable(hdev))
			hci_req_add(&req, HCI_OP_WRITE_LE_HOST_SUPPORTED,
				    sizeof(cp), &cp);
	}

	if (lmp_le_capable(hdev)) {
		/* Make sure the controller has a good default for
		 * advertising data. This also applies to the case
		 * where BR/EDR was toggled during the AUTO_OFF phase.
		 */
		if (test_bit(HCI_LE_ENABLED, &hdev->dev_flags)) {
			update_adv_data(&req);
			update_scan_rsp_data(&req);
		}

		if (test_bit(HCI_ADVERTISING, &hdev->dev_flags))
			enable_advertising(&req);

		restart_le_actions(&req);
	}

	link_sec = test_bit(HCI_LINK_SECURITY, &hdev->dev_flags);
	if (link_sec != test_bit(HCI_AUTH, &hdev->flags))
		hci_req_add(&req, HCI_OP_WRITE_AUTH_ENABLE,
			    sizeof(link_sec), &link_sec);

	if (lmp_bredr_capable(hdev)) {
		write_fast_connectable(&req, false);
		__hci_update_page_scan(&req);
		update_class(&req);
		update_name(&req);
		update_eir(&req);
	}

	return hci_req_run(&req, powered_complete);
}

int mgmt_powered(struct hci_dev *hdev, u8 powered)
{
	struct cmd_lookup match = { NULL, hdev };
	u8 status, zero_cod[] = { 0, 0, 0 };
	int err;

	if (!test_bit(HCI_MGMT, &hdev->dev_flags))
		return 0;

	if (powered) {
		if (powered_update_hci(hdev) == 0)
			return 0;

		mgmt_pending_foreach(MGMT_OP_SET_POWERED, hdev, settings_rsp,
				     &match);
		goto new_settings;
	}

	mgmt_pending_foreach(MGMT_OP_SET_POWERED, hdev, settings_rsp, &match);

	/* If the power off is because of hdev unregistration let
	 * use the appropriate INVALID_INDEX status. Otherwise use
	 * NOT_POWERED. We cover both scenarios here since later in
	 * mgmt_index_removed() any hci_conn callbacks will have already
	 * been triggered, potentially causing misleading DISCONNECTED
	 * status responses.
	 */
	if (test_bit(HCI_UNREGISTER, &hdev->dev_flags))
		status = MGMT_STATUS_INVALID_INDEX;
	else
		status = MGMT_STATUS_NOT_POWERED;

	mgmt_pending_foreach(0, hdev, cmd_complete_rsp, &status);

	if (memcmp(hdev->dev_class, zero_cod, sizeof(zero_cod)) != 0)
		mgmt_event(MGMT_EV_CLASS_OF_DEV_CHANGED, hdev,
			   zero_cod, sizeof(zero_cod), NULL);

new_settings:
	err = new_settings(hdev, match.sk);

	if (match.sk)
		sock_put(match.sk);

	return err;
}

void mgmt_set_powered_failed(struct hci_dev *hdev, int err)
{
	struct pending_cmd *cmd;
	u8 status;

	cmd = mgmt_pending_find(MGMT_OP_SET_POWERED, hdev);
	if (!cmd)
		return;

	if (err == -ERFKILL)
		status = MGMT_STATUS_RFKILLED;
	else
		status = MGMT_STATUS_FAILED;

	cmd_status(cmd->sk, hdev->id, MGMT_OP_SET_POWERED, status);

	mgmt_pending_remove(cmd);
}

void mgmt_discoverable_timeout(struct hci_dev *hdev)
{
	struct hci_request req;

	hci_dev_lock(hdev);

	/* When discoverable timeout triggers, then just make sure
	 * the limited discoverable flag is cleared. Even in the case
	 * of a timeout triggered from general discoverable, it is
	 * safe to unconditionally clear the flag.
	 */
	clear_bit(HCI_LIMITED_DISCOVERABLE, &hdev->dev_flags);
	clear_bit(HCI_DISCOVERABLE, &hdev->dev_flags);

	hci_req_init(&req, hdev);
	if (test_bit(HCI_BREDR_ENABLED, &hdev->dev_flags)) {
		u8 scan = SCAN_PAGE;
		hci_req_add(&req, HCI_OP_WRITE_SCAN_ENABLE,
			    sizeof(scan), &scan);
	}
	update_class(&req);
	update_adv_data(&req);
	hci_req_run(&req, NULL);

	hdev->discov_timeout = 0;

	new_settings(hdev, NULL);

	hci_dev_unlock(hdev);
}

void mgmt_new_link_key(struct hci_dev *hdev, struct link_key *key,
		       bool persistent)
{
	struct mgmt_ev_new_link_key ev;

	memset(&ev, 0, sizeof(ev));

	ev.store_hint = persistent;
	bacpy(&ev.key.addr.bdaddr, &key->bdaddr);
	ev.key.addr.type = BDADDR_BREDR;
	ev.key.type = key->type;
	memcpy(ev.key.val, key->val, HCI_LINK_KEY_SIZE);
	ev.key.pin_len = key->pin_len;

	mgmt_event(MGMT_EV_NEW_LINK_KEY, hdev, &ev, sizeof(ev), NULL);
}

static u8 mgmt_ltk_type(struct smp_ltk *ltk)
{
	switch (ltk->type) {
	case SMP_LTK:
	case SMP_LTK_SLAVE:
		if (ltk->authenticated)
			return MGMT_LTK_AUTHENTICATED;
		return MGMT_LTK_UNAUTHENTICATED;
	case SMP_LTK_P256:
		if (ltk->authenticated)
			return MGMT_LTK_P256_AUTH;
		return MGMT_LTK_P256_UNAUTH;
	case SMP_LTK_P256_DEBUG:
		return MGMT_LTK_P256_DEBUG;
	}

	return MGMT_LTK_UNAUTHENTICATED;
}

void mgmt_new_ltk(struct hci_dev *hdev, struct smp_ltk *key, bool persistent)
{
	struct mgmt_ev_new_long_term_key ev;

	memset(&ev, 0, sizeof(ev));

	/* Devices using resolvable or non-resolvable random addresses
	 * without providing an indentity resolving key don't require
	 * to store long term keys. Their addresses will change the
	 * next time around.
	 *
	 * Only when a remote device provides an identity address
	 * make sure the long term key is stored. If the remote
	 * identity is known, the long term keys are internally
	 * mapped to the identity address. So allow static random
	 * and public addresses here.
	 */
	if (key->bdaddr_type == ADDR_LE_DEV_RANDOM &&
	    (key->bdaddr.b[5] & 0xc0) != 0xc0)
		ev.store_hint = 0x00;
	else
		ev.store_hint = persistent;

	bacpy(&ev.key.addr.bdaddr, &key->bdaddr);
	ev.key.addr.type = link_to_bdaddr(LE_LINK, key->bdaddr_type);
	ev.key.type = mgmt_ltk_type(key);
	ev.key.enc_size = key->enc_size;
	ev.key.ediv = key->ediv;
	ev.key.rand = key->rand;

	if (key->type == SMP_LTK)
		ev.key.master = 1;

	memcpy(ev.key.val, key->val, sizeof(key->val));

	mgmt_event(MGMT_EV_NEW_LONG_TERM_KEY, hdev, &ev, sizeof(ev), NULL);
}

void mgmt_new_irk(struct hci_dev *hdev, struct smp_irk *irk)
{
	struct mgmt_ev_new_irk ev;

	memset(&ev, 0, sizeof(ev));

	/* For identity resolving keys from devices that are already
	 * using a public address or static random address, do not
	 * ask for storing this key. The identity resolving key really
	 * is only mandatory for devices using resovlable random
	 * addresses.
	 *
	 * Storing all identity resolving keys has the downside that
	 * they will be also loaded on next boot of they system. More
	 * identity resolving keys, means more time during scanning is
	 * needed to actually resolve these addresses.
	 */
	if (bacmp(&irk->rpa, BDADDR_ANY))
		ev.store_hint = 0x01;
	else
		ev.store_hint = 0x00;

	bacpy(&ev.rpa, &irk->rpa);
	bacpy(&ev.irk.addr.bdaddr, &irk->bdaddr);
	ev.irk.addr.type = link_to_bdaddr(LE_LINK, irk->addr_type);
	memcpy(ev.irk.val, irk->val, sizeof(irk->val));

	mgmt_event(MGMT_EV_NEW_IRK, hdev, &ev, sizeof(ev), NULL);
}

void mgmt_new_csrk(struct hci_dev *hdev, struct smp_csrk *csrk,
		   bool persistent)
{
	struct mgmt_ev_new_csrk ev;

	memset(&ev, 0, sizeof(ev));

	/* Devices using resolvable or non-resolvable random addresses
	 * without providing an indentity resolving key don't require
	 * to store signature resolving keys. Their addresses will change
	 * the next time around.
	 *
	 * Only when a remote device provides an identity address
	 * make sure the signature resolving key is stored. So allow
	 * static random and public addresses here.
	 */
	if (csrk->bdaddr_type == ADDR_LE_DEV_RANDOM &&
	    (csrk->bdaddr.b[5] & 0xc0) != 0xc0)
		ev.store_hint = 0x00;
	else
		ev.store_hint = persistent;

	bacpy(&ev.key.addr.bdaddr, &csrk->bdaddr);
	ev.key.addr.type = link_to_bdaddr(LE_LINK, csrk->bdaddr_type);
	ev.key.master = csrk->master;
	memcpy(ev.key.val, csrk->val, sizeof(csrk->val));

	mgmt_event(MGMT_EV_NEW_CSRK, hdev, &ev, sizeof(ev), NULL);
}

void mgmt_new_conn_param(struct hci_dev *hdev, bdaddr_t *bdaddr,
			 u8 bdaddr_type, u8 store_hint, u16 min_interval,
			 u16 max_interval, u16 latency, u16 timeout)
{
	struct mgmt_ev_new_conn_param ev;

	if (!hci_is_identity_address(bdaddr, bdaddr_type))
		return;

	memset(&ev, 0, sizeof(ev));
	bacpy(&ev.addr.bdaddr, bdaddr);
	ev.addr.type = link_to_bdaddr(LE_LINK, bdaddr_type);
	ev.store_hint = store_hint;
	ev.min_interval = cpu_to_le16(min_interval);
	ev.max_interval = cpu_to_le16(max_interval);
	ev.latency = cpu_to_le16(latency);
	ev.timeout = cpu_to_le16(timeout);

	mgmt_event(MGMT_EV_NEW_CONN_PARAM, hdev, &ev, sizeof(ev), NULL);
}

static inline u16 eir_append_data(u8 *eir, u16 eir_len, u8 type, u8 *data,
				  u8 data_len)
{
	eir[eir_len++] = sizeof(type) + data_len;
	eir[eir_len++] = type;
	memcpy(&eir[eir_len], data, data_len);
	eir_len += data_len;

	return eir_len;
}

void mgmt_device_connected(struct hci_dev *hdev, struct hci_conn *conn,
			   u32 flags, u8 *name, u8 name_len)
{
	char buf[512];
	struct mgmt_ev_device_connected *ev = (void *) buf;
	u16 eir_len = 0;

	bacpy(&ev->addr.bdaddr, &conn->dst);
	ev->addr.type = link_to_bdaddr(conn->type, conn->dst_type);

	ev->flags = __cpu_to_le32(flags);

	/* We must ensure that the EIR Data fields are ordered and
	 * unique. Keep it simple for now and avoid the problem by not
	 * adding any BR/EDR data to the LE adv.
	 */
	if (conn->le_adv_data_len > 0) {
		memcpy(&ev->eir[eir_len],
		       conn->le_adv_data, conn->le_adv_data_len);
		eir_len = conn->le_adv_data_len;
	} else {
		if (name_len > 0)
			eir_len = eir_append_data(ev->eir, 0, EIR_NAME_COMPLETE,
						  name, name_len);

		if (memcmp(conn->dev_class, "\0\0\0", 3) != 0)
			eir_len = eir_append_data(ev->eir, eir_len,
						  EIR_CLASS_OF_DEV,
						  conn->dev_class, 3);
	}

	ev->eir_len = cpu_to_le16(eir_len);

	mgmt_event(MGMT_EV_DEVICE_CONNECTED, hdev, buf,
		    sizeof(*ev) + eir_len, NULL);
}

static void disconnect_rsp(struct pending_cmd *cmd, void *data)
{
	struct sock **sk = data;

	cmd->cmd_complete(cmd, 0);

	*sk = cmd->sk;
	sock_hold(*sk);

	mgmt_pending_remove(cmd);
}

static void unpair_device_rsp(struct pending_cmd *cmd, void *data)
{
	struct hci_dev *hdev = data;
	struct mgmt_cp_unpair_device *cp = cmd->param;

	device_unpaired(hdev, &cp->addr.bdaddr, cp->addr.type, cmd->sk);

	cmd->cmd_complete(cmd, 0);
	mgmt_pending_remove(cmd);
}

bool mgmt_powering_down(struct hci_dev *hdev)
{
	struct pending_cmd *cmd;
	struct mgmt_mode *cp;

	cmd = mgmt_pending_find(MGMT_OP_SET_POWERED, hdev);
	if (!cmd)
		return false;

	cp = cmd->param;
	if (!cp->val)
		return true;

	return false;
}

void mgmt_device_disconnected(struct hci_dev *hdev, bdaddr_t *bdaddr,
			      u8 link_type, u8 addr_type, u8 reason,
			      bool mgmt_connected)
{
	struct mgmt_ev_device_disconnected ev;
	struct sock *sk = NULL;

	/* The connection is still in hci_conn_hash so test for 1
	 * instead of 0 to know if this is the last one.
	 */
	if (mgmt_powering_down(hdev) && hci_conn_count(hdev) == 1) {
		cancel_delayed_work(&hdev->power_off);
		queue_work(hdev->req_workqueue, &hdev->power_off.work);
	}

	if (!mgmt_connected)
		return;

	if (link_type != ACL_LINK && link_type != LE_LINK)
		return;

	mgmt_pending_foreach(MGMT_OP_DISCONNECT, hdev, disconnect_rsp, &sk);

	bacpy(&ev.addr.bdaddr, bdaddr);
	ev.addr.type = link_to_bdaddr(link_type, addr_type);
	ev.reason = reason;

	mgmt_event(MGMT_EV_DEVICE_DISCONNECTED, hdev, &ev, sizeof(ev), sk);

	if (sk)
		sock_put(sk);

	mgmt_pending_foreach(MGMT_OP_UNPAIR_DEVICE, hdev, unpair_device_rsp,
			     hdev);
}

void mgmt_disconnect_failed(struct hci_dev *hdev, bdaddr_t *bdaddr,
			    u8 link_type, u8 addr_type, u8 status)
{
	u8 bdaddr_type = link_to_bdaddr(link_type, addr_type);
	struct mgmt_cp_disconnect *cp;
	struct pending_cmd *cmd;

	mgmt_pending_foreach(MGMT_OP_UNPAIR_DEVICE, hdev, unpair_device_rsp,
			     hdev);

	cmd = mgmt_pending_find(MGMT_OP_DISCONNECT, hdev);
	if (!cmd)
		return;

	cp = cmd->param;

	if (bacmp(bdaddr, &cp->addr.bdaddr))
		return;

	if (cp->addr.type != bdaddr_type)
		return;

	cmd->cmd_complete(cmd, mgmt_status(status));
	mgmt_pending_remove(cmd);
}

void mgmt_connect_failed(struct hci_dev *hdev, bdaddr_t *bdaddr, u8 link_type,
			 u8 addr_type, u8 status)
{
	struct mgmt_ev_connect_failed ev;

	/* The connection is still in hci_conn_hash so test for 1
	 * instead of 0 to know if this is the last one.
	 */
	if (mgmt_powering_down(hdev) && hci_conn_count(hdev) == 1) {
		cancel_delayed_work(&hdev->power_off);
		queue_work(hdev->req_workqueue, &hdev->power_off.work);
	}

	bacpy(&ev.addr.bdaddr, bdaddr);
	ev.addr.type = link_to_bdaddr(link_type, addr_type);
	ev.status = mgmt_status(status);

	mgmt_event(MGMT_EV_CONNECT_FAILED, hdev, &ev, sizeof(ev), NULL);
}

void mgmt_pin_code_request(struct hci_dev *hdev, bdaddr_t *bdaddr, u8 secure)
{
	struct mgmt_ev_pin_code_request ev;

	bacpy(&ev.addr.bdaddr, bdaddr);
	ev.addr.type = BDADDR_BREDR;
	ev.secure = secure;

	mgmt_event(MGMT_EV_PIN_CODE_REQUEST, hdev, &ev, sizeof(ev), NULL);
}

void mgmt_pin_code_reply_complete(struct hci_dev *hdev, bdaddr_t *bdaddr,
				  u8 status)
{
	struct pending_cmd *cmd;

	cmd = mgmt_pending_find(MGMT_OP_PIN_CODE_REPLY, hdev);
	if (!cmd)
		return;

	cmd->cmd_complete(cmd, mgmt_status(status));
	mgmt_pending_remove(cmd);
}

void mgmt_pin_code_neg_reply_complete(struct hci_dev *hdev, bdaddr_t *bdaddr,
				      u8 status)
{
	struct pending_cmd *cmd;

	cmd = mgmt_pending_find(MGMT_OP_PIN_CODE_NEG_REPLY, hdev);
	if (!cmd)
		return;

	cmd->cmd_complete(cmd, mgmt_status(status));
	mgmt_pending_remove(cmd);
}

int mgmt_user_confirm_request(struct hci_dev *hdev, bdaddr_t *bdaddr,
			      u8 link_type, u8 addr_type, u32 value,
			      u8 confirm_hint)
{
	struct mgmt_ev_user_confirm_request ev;

	BT_DBG("%s", hdev->name);

	bacpy(&ev.addr.bdaddr, bdaddr);
	ev.addr.type = link_to_bdaddr(link_type, addr_type);
	ev.confirm_hint = confirm_hint;
	ev.value = cpu_to_le32(value);

	return mgmt_event(MGMT_EV_USER_CONFIRM_REQUEST, hdev, &ev, sizeof(ev),
			  NULL);
}

int mgmt_user_passkey_request(struct hci_dev *hdev, bdaddr_t *bdaddr,
			      u8 link_type, u8 addr_type)
{
	struct mgmt_ev_user_passkey_request ev;

	BT_DBG("%s", hdev->name);

	bacpy(&ev.addr.bdaddr, bdaddr);
	ev.addr.type = link_to_bdaddr(link_type, addr_type);

	return mgmt_event(MGMT_EV_USER_PASSKEY_REQUEST, hdev, &ev, sizeof(ev),
			  NULL);
}

static int user_pairing_resp_complete(struct hci_dev *hdev, bdaddr_t *bdaddr,
				      u8 link_type, u8 addr_type, u8 status,
				      u8 opcode)
{
	struct pending_cmd *cmd;

	cmd = mgmt_pending_find(opcode, hdev);
	if (!cmd)
		return -ENOENT;

	cmd->cmd_complete(cmd, mgmt_status(status));
	mgmt_pending_remove(cmd);

	return 0;
}

int mgmt_user_confirm_reply_complete(struct hci_dev *hdev, bdaddr_t *bdaddr,
				     u8 link_type, u8 addr_type, u8 status)
{
	return user_pairing_resp_complete(hdev, bdaddr, link_type, addr_type,
					  status, MGMT_OP_USER_CONFIRM_REPLY);
}

int mgmt_user_confirm_neg_reply_complete(struct hci_dev *hdev, bdaddr_t *bdaddr,
					 u8 link_type, u8 addr_type, u8 status)
{
	return user_pairing_resp_complete(hdev, bdaddr, link_type, addr_type,
					  status,
					  MGMT_OP_USER_CONFIRM_NEG_REPLY);
}

int mgmt_user_passkey_reply_complete(struct hci_dev *hdev, bdaddr_t *bdaddr,
				     u8 link_type, u8 addr_type, u8 status)
{
	return user_pairing_resp_complete(hdev, bdaddr, link_type, addr_type,
					  status, MGMT_OP_USER_PASSKEY_REPLY);
}

int mgmt_user_passkey_neg_reply_complete(struct hci_dev *hdev, bdaddr_t *bdaddr,
					 u8 link_type, u8 addr_type, u8 status)
{
	return user_pairing_resp_complete(hdev, bdaddr, link_type, addr_type,
					  status,
					  MGMT_OP_USER_PASSKEY_NEG_REPLY);
}

int mgmt_user_passkey_notify(struct hci_dev *hdev, bdaddr_t *bdaddr,
			     u8 link_type, u8 addr_type, u32 passkey,
			     u8 entered)
{
	struct mgmt_ev_passkey_notify ev;

	BT_DBG("%s", hdev->name);

	bacpy(&ev.addr.bdaddr, bdaddr);
	ev.addr.type = link_to_bdaddr(link_type, addr_type);
	ev.passkey = __cpu_to_le32(passkey);
	ev.entered = entered;

	return mgmt_event(MGMT_EV_PASSKEY_NOTIFY, hdev, &ev, sizeof(ev), NULL);
}

void mgmt_auth_failed(struct hci_conn *conn, u8 hci_status)
{
	struct mgmt_ev_auth_failed ev;
	struct pending_cmd *cmd;
	u8 status = mgmt_status(hci_status);

	bacpy(&ev.addr.bdaddr, &conn->dst);
	ev.addr.type = link_to_bdaddr(conn->type, conn->dst_type);
	ev.status = status;

	cmd = find_pairing(conn);

	mgmt_event(MGMT_EV_AUTH_FAILED, conn->hdev, &ev, sizeof(ev),
		    cmd ? cmd->sk : NULL);

	if (cmd) {
		cmd->cmd_complete(cmd, status);
		mgmt_pending_remove(cmd);
	}
}

void mgmt_auth_enable_complete(struct hci_dev *hdev, u8 status)
{
	struct cmd_lookup match = { NULL, hdev };
	bool changed;

	if (status) {
		u8 mgmt_err = mgmt_status(status);
		mgmt_pending_foreach(MGMT_OP_SET_LINK_SECURITY, hdev,
				     cmd_status_rsp, &mgmt_err);
		return;
	}

	if (test_bit(HCI_AUTH, &hdev->flags))
		changed = !test_and_set_bit(HCI_LINK_SECURITY,
					    &hdev->dev_flags);
	else
		changed = test_and_clear_bit(HCI_LINK_SECURITY,
					     &hdev->dev_flags);

	mgmt_pending_foreach(MGMT_OP_SET_LINK_SECURITY, hdev, settings_rsp,
			     &match);

	if (changed)
		new_settings(hdev, match.sk);

	if (match.sk)
		sock_put(match.sk);
}

static void clear_eir(struct hci_request *req)
{
	struct hci_dev *hdev = req->hdev;
	struct hci_cp_write_eir cp;

	if (!lmp_ext_inq_capable(hdev))
		return;

	memset(hdev->eir, 0, sizeof(hdev->eir));

	memset(&cp, 0, sizeof(cp));

	hci_req_add(req, HCI_OP_WRITE_EIR, sizeof(cp), &cp);
}

void mgmt_ssp_enable_complete(struct hci_dev *hdev, u8 enable, u8 status)
{
	struct cmd_lookup match = { NULL, hdev };
	struct hci_request req;
	bool changed = false;

	if (status) {
		u8 mgmt_err = mgmt_status(status);

		if (enable && test_and_clear_bit(HCI_SSP_ENABLED,
						 &hdev->dev_flags)) {
			clear_bit(HCI_HS_ENABLED, &hdev->dev_flags);
			new_settings(hdev, NULL);
		}

		mgmt_pending_foreach(MGMT_OP_SET_SSP, hdev, cmd_status_rsp,
				     &mgmt_err);
		return;
	}

	if (enable) {
		changed = !test_and_set_bit(HCI_SSP_ENABLED, &hdev->dev_flags);
	} else {
		changed = test_and_clear_bit(HCI_SSP_ENABLED, &hdev->dev_flags);
		if (!changed)
			changed = test_and_clear_bit(HCI_HS_ENABLED,
						     &hdev->dev_flags);
		else
			clear_bit(HCI_HS_ENABLED, &hdev->dev_flags);
	}

	mgmt_pending_foreach(MGMT_OP_SET_SSP, hdev, settings_rsp, &match);

	if (changed)
		new_settings(hdev, match.sk);

	if (match.sk)
		sock_put(match.sk);

	hci_req_init(&req, hdev);

	if (test_bit(HCI_SSP_ENABLED, &hdev->dev_flags)) {
		if (test_bit(HCI_USE_DEBUG_KEYS, &hdev->dev_flags))
			hci_req_add(&req, HCI_OP_WRITE_SSP_DEBUG_MODE,
				    sizeof(enable), &enable);
		update_eir(&req);
	} else {
		clear_eir(&req);
	}

	hci_req_run(&req, NULL);
}

void mgmt_sc_enable_complete(struct hci_dev *hdev, u8 enable, u8 status)
{
	struct cmd_lookup match = { NULL, hdev };
	bool changed = false;

	if (status) {
		u8 mgmt_err = mgmt_status(status);

		if (enable) {
			if (test_and_clear_bit(HCI_SC_ENABLED,
					       &hdev->dev_flags))
				new_settings(hdev, NULL);
			clear_bit(HCI_SC_ONLY, &hdev->dev_flags);
		}

		mgmt_pending_foreach(MGMT_OP_SET_SECURE_CONN, hdev,
				     cmd_status_rsp, &mgmt_err);
		return;
	}

	if (enable) {
		changed = !test_and_set_bit(HCI_SC_ENABLED, &hdev->dev_flags);
	} else {
		changed = test_and_clear_bit(HCI_SC_ENABLED, &hdev->dev_flags);
		clear_bit(HCI_SC_ONLY, &hdev->dev_flags);
	}

	mgmt_pending_foreach(MGMT_OP_SET_SECURE_CONN, hdev,
			     settings_rsp, &match);

	if (changed)
		new_settings(hdev, match.sk);

	if (match.sk)
		sock_put(match.sk);
}

static void sk_lookup(struct pending_cmd *cmd, void *data)
{
	struct cmd_lookup *match = data;

	if (match->sk == NULL) {
		match->sk = cmd->sk;
		sock_hold(match->sk);
	}
}

void mgmt_set_class_of_dev_complete(struct hci_dev *hdev, u8 *dev_class,
				    u8 status)
{
	struct cmd_lookup match = { NULL, hdev, mgmt_status(status) };

	mgmt_pending_foreach(MGMT_OP_SET_DEV_CLASS, hdev, sk_lookup, &match);
	mgmt_pending_foreach(MGMT_OP_ADD_UUID, hdev, sk_lookup, &match);
	mgmt_pending_foreach(MGMT_OP_REMOVE_UUID, hdev, sk_lookup, &match);

	if (!status)
		mgmt_event(MGMT_EV_CLASS_OF_DEV_CHANGED, hdev, dev_class, 3,
			   NULL);

	if (match.sk)
		sock_put(match.sk);
}

void mgmt_set_local_name_complete(struct hci_dev *hdev, u8 *name, u8 status)
{
	struct mgmt_cp_set_local_name ev;
	struct pending_cmd *cmd;

	if (status)
		return;

	memset(&ev, 0, sizeof(ev));
	memcpy(ev.name, name, HCI_MAX_NAME_LENGTH);
	memcpy(ev.short_name, hdev->short_name, HCI_MAX_SHORT_NAME_LENGTH);

	cmd = mgmt_pending_find(MGMT_OP_SET_LOCAL_NAME, hdev);
	if (!cmd) {
		memcpy(hdev->dev_name, name, sizeof(hdev->dev_name));

		/* If this is a HCI command related to powering on the
		 * HCI dev don't send any mgmt signals.
		 */
		if (mgmt_pending_find(MGMT_OP_SET_POWERED, hdev))
			return;
	}

	mgmt_event(MGMT_EV_LOCAL_NAME_CHANGED, hdev, &ev, sizeof(ev),
		   cmd ? cmd->sk : NULL);
}

void mgmt_read_local_oob_data_complete(struct hci_dev *hdev, u8 *hash192,
				       u8 *rand192, u8 *hash256, u8 *rand256,
				       u8 status)
{
	struct pending_cmd *cmd;

	BT_DBG("%s status %u", hdev->name, status);

	cmd = mgmt_pending_find(MGMT_OP_READ_LOCAL_OOB_DATA, hdev);
	if (!cmd)
		return;

	if (status) {
		cmd_status(cmd->sk, hdev->id, MGMT_OP_READ_LOCAL_OOB_DATA,
			   mgmt_status(status));
	} else {
		if (bredr_sc_enabled(hdev) && hash256 && rand256) {
			struct mgmt_rp_read_local_oob_ext_data rp;

			memcpy(rp.hash192, hash192, sizeof(rp.hash192));
			memcpy(rp.rand192, rand192, sizeof(rp.rand192));

			memcpy(rp.hash256, hash256, sizeof(rp.hash256));
			memcpy(rp.rand256, rand256, sizeof(rp.rand256));

			cmd_complete(cmd->sk, hdev->id,
				     MGMT_OP_READ_LOCAL_OOB_DATA, 0,
				     &rp, sizeof(rp));
		} else {
			struct mgmt_rp_read_local_oob_data rp;

			memcpy(rp.hash, hash192, sizeof(rp.hash));
			memcpy(rp.rand, rand192, sizeof(rp.rand));

			cmd_complete(cmd->sk, hdev->id,
				     MGMT_OP_READ_LOCAL_OOB_DATA, 0,
				     &rp, sizeof(rp));
		}
	}

	mgmt_pending_remove(cmd);
}

static inline bool has_uuid(u8 *uuid, u16 uuid_count, u8 (*uuids)[16])
{
	int i;

	for (i = 0; i < uuid_count; i++) {
		if (!memcmp(uuid, uuids[i], 16))
			return true;
	}

	return false;
}

static bool eir_has_uuids(u8 *eir, u16 eir_len, u16 uuid_count, u8 (*uuids)[16])
{
	u16 parsed = 0;

	while (parsed < eir_len) {
		u8 field_len = eir[0];
		u8 uuid[16];
		int i;

		if (field_len == 0)
			break;

		if (eir_len - parsed < field_len + 1)
			break;

		switch (eir[1]) {
		case EIR_UUID16_ALL:
		case EIR_UUID16_SOME:
			for (i = 0; i + 3 <= field_len; i += 2) {
				memcpy(uuid, bluetooth_base_uuid, 16);
				uuid[13] = eir[i + 3];
				uuid[12] = eir[i + 2];
				if (has_uuid(uuid, uuid_count, uuids))
					return true;
			}
			break;
		case EIR_UUID32_ALL:
		case EIR_UUID32_SOME:
			for (i = 0; i + 5 <= field_len; i += 4) {
				memcpy(uuid, bluetooth_base_uuid, 16);
				uuid[15] = eir[i + 5];
				uuid[14] = eir[i + 4];
				uuid[13] = eir[i + 3];
				uuid[12] = eir[i + 2];
				if (has_uuid(uuid, uuid_count, uuids))
					return true;
			}
			break;
		case EIR_UUID128_ALL:
		case EIR_UUID128_SOME:
			for (i = 0; i + 17 <= field_len; i += 16) {
				memcpy(uuid, eir + i + 2, 16);
				if (has_uuid(uuid, uuid_count, uuids))
					return true;
			}
			break;
		}

		parsed += field_len + 1;
		eir += field_len + 1;
	}

	return false;
}

void mgmt_device_found(struct hci_dev *hdev, bdaddr_t *bdaddr, u8 link_type,
		       u8 addr_type, u8 *dev_class, s8 rssi, u32 flags,
		       u8 *eir, u16 eir_len, u8 *scan_rsp, u8 scan_rsp_len)
{
	char buf[512];
	struct mgmt_ev_device_found *ev = (void *) buf;
	size_t ev_size;
	bool match;

	/* Don't send events for a non-kernel initiated discovery. With
	 * LE one exception is if we have pend_le_reports > 0 in which
	 * case we're doing passive scanning and want these events.
	 */
	if (!hci_discovery_active(hdev)) {
		if (link_type == ACL_LINK)
			return;
		if (link_type == LE_LINK && list_empty(&hdev->pend_le_reports))
			return;
	}

	/* When using service discovery with a RSSI threshold, then check
	 * if such a RSSI threshold is specified. If a RSSI threshold has
	 * been specified, then all results with a RSSI smaller than the
	 * RSSI threshold will be dropped.
	 *
	 * For BR/EDR devices (pre 1.2) providing no RSSI during inquiry,
	 * the results are also dropped.
	 */
	if (hdev->discovery.rssi != HCI_RSSI_INVALID &&
	    (rssi < hdev->discovery.rssi || rssi == HCI_RSSI_INVALID))
		return;

	/* Make sure that the buffer is big enough. The 5 extra bytes
	 * are for the potential CoD field.
	 */
	if (sizeof(*ev) + eir_len + scan_rsp_len + 5 > sizeof(buf))
		return;

	memset(buf, 0, sizeof(buf));

	/* In case of device discovery with BR/EDR devices (pre 1.2), the
	 * RSSI value was reported as 0 when not available. This behavior
	 * is kept when using device discovery. This is required for full
	 * backwards compatibility with the API.
	 *
	 * However when using service discovery, the value 127 will be
	 * returned when the RSSI is not available.
	 */
	if (rssi == HCI_RSSI_INVALID && !hdev->discovery.report_invalid_rssi)
		rssi = 0;

	bacpy(&ev->addr.bdaddr, bdaddr);
	ev->addr.type = link_to_bdaddr(link_type, addr_type);
	ev->rssi = rssi;
	ev->flags = cpu_to_le32(flags);

	if (eir_len > 0) {
		/* When using service discovery and a list of UUID is
		 * provided, results with no matching UUID should be
		 * dropped. In case there is a match the result is
		 * kept and checking possible scan response data
		 * will be skipped.
		 */
		if (hdev->discovery.uuid_count > 0)
			match = eir_has_uuids(eir, eir_len,
					      hdev->discovery.uuid_count,
					      hdev->discovery.uuids);
		else
			match = true;

		if (!match && !scan_rsp_len)
			return;

		/* Copy EIR or advertising data into event */
		memcpy(ev->eir, eir, eir_len);
	} else {
		/* When using service discovery and a list of UUID is
		 * provided, results with empty EIR or advertising data
		 * should be dropped since they do not match any UUID.
		 */
		if (hdev->discovery.uuid_count > 0 && !scan_rsp_len)
			return;

		match = false;
	}

	if (dev_class && !eir_has_data_type(ev->eir, eir_len, EIR_CLASS_OF_DEV))
		eir_len = eir_append_data(ev->eir, eir_len, EIR_CLASS_OF_DEV,
					  dev_class, 3);

	if (scan_rsp_len > 0) {
		/* When using service discovery and a list of UUID is
		 * provided, results with no matching UUID should be
		 * dropped if there is no previous match from the
		 * advertising data.
		 */
		if (hdev->discovery.uuid_count > 0) {
			if (!match && !eir_has_uuids(scan_rsp, scan_rsp_len,
						     hdev->discovery.uuid_count,
						     hdev->discovery.uuids))
				return;
		}

		/* Append scan response data to event */
		memcpy(ev->eir + eir_len, scan_rsp, scan_rsp_len);
	} else {
		/* When using service discovery and a list of UUID is
		 * provided, results with empty scan response and no
		 * previous matched advertising data should be dropped.
		 */
		if (hdev->discovery.uuid_count > 0 && !match)
			return;
	}

	ev->eir_len = cpu_to_le16(eir_len + scan_rsp_len);
	ev_size = sizeof(*ev) + eir_len + scan_rsp_len;

	mgmt_event(MGMT_EV_DEVICE_FOUND, hdev, ev, ev_size, NULL);
}

void mgmt_remote_name(struct hci_dev *hdev, bdaddr_t *bdaddr, u8 link_type,
		      u8 addr_type, s8 rssi, u8 *name, u8 name_len)
{
	struct mgmt_ev_device_found *ev;
	char buf[sizeof(*ev) + HCI_MAX_NAME_LENGTH + 2];
	u16 eir_len;

	ev = (struct mgmt_ev_device_found *) buf;

	memset(buf, 0, sizeof(buf));

	bacpy(&ev->addr.bdaddr, bdaddr);
	ev->addr.type = link_to_bdaddr(link_type, addr_type);
	ev->rssi = rssi;

	eir_len = eir_append_data(ev->eir, 0, EIR_NAME_COMPLETE, name,
				  name_len);

	ev->eir_len = cpu_to_le16(eir_len);

	mgmt_event(MGMT_EV_DEVICE_FOUND, hdev, ev, sizeof(*ev) + eir_len, NULL);
}

void mgmt_discovering(struct hci_dev *hdev, u8 discovering)
{
	struct mgmt_ev_discovering ev;

	BT_DBG("%s discovering %u", hdev->name, discovering);

	memset(&ev, 0, sizeof(ev));
	ev.type = hdev->discovery.type;
	ev.discovering = discovering;

	mgmt_event(MGMT_EV_DISCOVERING, hdev, &ev, sizeof(ev), NULL);
}

static void adv_enable_complete(struct hci_dev *hdev, u8 status)
{
	BT_DBG("%s status %u", hdev->name, status);
}

void mgmt_reenable_advertising(struct hci_dev *hdev)
{
	struct hci_request req;

	if (!test_bit(HCI_ADVERTISING, &hdev->dev_flags))
		return;

	hci_req_init(&req, hdev);
	enable_advertising(&req);
	hci_req_run(&req, adv_enable_complete);
}
