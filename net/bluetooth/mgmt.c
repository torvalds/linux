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
#include <net/bluetooth/hci_sock.h>
#include <net/bluetooth/l2cap.h>
#include <net/bluetooth/mgmt.h>

#include "hci_request.h"
#include "smp.h"
#include "mgmt_util.h"
#include "mgmt_config.h"
#include "msft.h"
#include "eir.h"

#define MGMT_VERSION	1
#define MGMT_REVISION	21

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
	MGMT_OP_READ_LOCAL_OOB_EXT_DATA,
	MGMT_OP_READ_EXT_INDEX_LIST,
	MGMT_OP_READ_ADV_FEATURES,
	MGMT_OP_ADD_ADVERTISING,
	MGMT_OP_REMOVE_ADVERTISING,
	MGMT_OP_GET_ADV_SIZE_INFO,
	MGMT_OP_START_LIMITED_DISCOVERY,
	MGMT_OP_READ_EXT_INFO,
	MGMT_OP_SET_APPEARANCE,
	MGMT_OP_GET_PHY_CONFIGURATION,
	MGMT_OP_SET_PHY_CONFIGURATION,
	MGMT_OP_SET_BLOCKED_KEYS,
	MGMT_OP_SET_WIDEBAND_SPEECH,
	MGMT_OP_READ_CONTROLLER_CAP,
	MGMT_OP_READ_EXP_FEATURES_INFO,
	MGMT_OP_SET_EXP_FEATURE,
	MGMT_OP_READ_DEF_SYSTEM_CONFIG,
	MGMT_OP_SET_DEF_SYSTEM_CONFIG,
	MGMT_OP_READ_DEF_RUNTIME_CONFIG,
	MGMT_OP_SET_DEF_RUNTIME_CONFIG,
	MGMT_OP_GET_DEVICE_FLAGS,
	MGMT_OP_SET_DEVICE_FLAGS,
	MGMT_OP_READ_ADV_MONITOR_FEATURES,
	MGMT_OP_ADD_ADV_PATTERNS_MONITOR,
	MGMT_OP_REMOVE_ADV_MONITOR,
	MGMT_OP_ADD_EXT_ADV_PARAMS,
	MGMT_OP_ADD_EXT_ADV_DATA,
	MGMT_OP_ADD_ADV_PATTERNS_MONITOR_RSSI,
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
	MGMT_EV_EXT_INDEX_ADDED,
	MGMT_EV_EXT_INDEX_REMOVED,
	MGMT_EV_LOCAL_OOB_DATA_UPDATED,
	MGMT_EV_ADVERTISING_ADDED,
	MGMT_EV_ADVERTISING_REMOVED,
	MGMT_EV_EXT_INFO_CHANGED,
	MGMT_EV_PHY_CONFIGURATION_CHANGED,
	MGMT_EV_EXP_FEATURE_CHANGED,
	MGMT_EV_DEVICE_FLAGS_CHANGED,
	MGMT_EV_ADV_MONITOR_ADDED,
	MGMT_EV_ADV_MONITOR_REMOVED,
	MGMT_EV_CONTROLLER_SUSPEND,
	MGMT_EV_CONTROLLER_RESUME,
};

static const u16 mgmt_untrusted_commands[] = {
	MGMT_OP_READ_INDEX_LIST,
	MGMT_OP_READ_INFO,
	MGMT_OP_READ_UNCONF_INDEX_LIST,
	MGMT_OP_READ_CONFIG_INFO,
	MGMT_OP_READ_EXT_INDEX_LIST,
	MGMT_OP_READ_EXT_INFO,
	MGMT_OP_READ_CONTROLLER_CAP,
	MGMT_OP_READ_EXP_FEATURES_INFO,
	MGMT_OP_READ_DEF_SYSTEM_CONFIG,
	MGMT_OP_READ_DEF_RUNTIME_CONFIG,
};

static const u16 mgmt_untrusted_events[] = {
	MGMT_EV_INDEX_ADDED,
	MGMT_EV_INDEX_REMOVED,
	MGMT_EV_NEW_SETTINGS,
	MGMT_EV_CLASS_OF_DEV_CHANGED,
	MGMT_EV_LOCAL_NAME_CHANGED,
	MGMT_EV_UNCONF_INDEX_ADDED,
	MGMT_EV_UNCONF_INDEX_REMOVED,
	MGMT_EV_NEW_CONFIG_OPTIONS,
	MGMT_EV_EXT_INDEX_ADDED,
	MGMT_EV_EXT_INDEX_REMOVED,
	MGMT_EV_EXT_INFO_CHANGED,
	MGMT_EV_EXP_FEATURE_CHANGED,
};

#define CACHE_TIMEOUT	msecs_to_jiffies(2 * 1000)

#define ZERO_KEY "\x00\x00\x00\x00\x00\x00\x00\x00" \
		 "\x00\x00\x00\x00\x00\x00\x00\x00"

/* HCI to MGMT error code conversion table */
static const u8 mgmt_status_table[] = {
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
	MGMT_STATUS_FAILED,		/* Reserved for future use */
	MGMT_STATUS_INVALID_PARAMS,	/* Unacceptable Parameter */
	MGMT_STATUS_REJECTED,		/* QoS Rejected */
	MGMT_STATUS_NOT_SUPPORTED,	/* Classification Not Supported */
	MGMT_STATUS_REJECTED,		/* Insufficient Security */
	MGMT_STATUS_INVALID_PARAMS,	/* Parameter Out Of Range */
	MGMT_STATUS_FAILED,		/* Reserved for future use */
	MGMT_STATUS_BUSY,		/* Role Switch Pending */
	MGMT_STATUS_FAILED,		/* Reserved for future use */
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

static u8 mgmt_errno_status(int err)
{
	switch (err) {
	case 0:
		return MGMT_STATUS_SUCCESS;
	case -EPERM:
		return MGMT_STATUS_REJECTED;
	case -EINVAL:
		return MGMT_STATUS_INVALID_PARAMS;
	case -EOPNOTSUPP:
		return MGMT_STATUS_NOT_SUPPORTED;
	case -EBUSY:
		return MGMT_STATUS_BUSY;
	case -ETIMEDOUT:
		return MGMT_STATUS_AUTH_FAILED;
	case -ENOMEM:
		return MGMT_STATUS_NO_RESOURCES;
	case -EISCONN:
		return MGMT_STATUS_ALREADY_CONNECTED;
	case -ENOTCONN:
		return MGMT_STATUS_DISCONNECTED;
	}

	return MGMT_STATUS_FAILED;
}

static u8 mgmt_status(int err)
{
	if (err < 0)
		return mgmt_errno_status(err);

	if (err < ARRAY_SIZE(mgmt_status_table))
		return mgmt_status_table[err];

	return MGMT_STATUS_FAILED;
}

static int mgmt_index_event(u16 event, struct hci_dev *hdev, void *data,
			    u16 len, int flag)
{
	return mgmt_send_event(event, hdev, HCI_CHANNEL_CONTROL, data, len,
			       flag, NULL);
}

static int mgmt_limited_event(u16 event, struct hci_dev *hdev, void *data,
			      u16 len, int flag, struct sock *skip_sk)
{
	return mgmt_send_event(event, hdev, HCI_CHANNEL_CONTROL, data, len,
			       flag, skip_sk);
}

static int mgmt_event(u16 event, struct hci_dev *hdev, void *data, u16 len,
		      struct sock *skip_sk)
{
	return mgmt_send_event(event, hdev, HCI_CHANNEL_CONTROL, data, len,
			       HCI_SOCK_TRUSTED, skip_sk);
}

static u8 le_addr_type(u8 mgmt_addr_type)
{
	if (mgmt_addr_type == BDADDR_LE_PUBLIC)
		return ADDR_LE_DEV_PUBLIC;
	else
		return ADDR_LE_DEV_RANDOM;
}

void mgmt_fill_version_info(void *ver)
{
	struct mgmt_rp_read_version *rp = ver;

	rp->version = MGMT_VERSION;
	rp->revision = cpu_to_le16(MGMT_REVISION);
}

static int read_version(struct sock *sk, struct hci_dev *hdev, void *data,
			u16 data_len)
{
	struct mgmt_rp_read_version rp;

	bt_dev_dbg(hdev, "sock %p", sk);

	mgmt_fill_version_info(&rp);

	return mgmt_cmd_complete(sk, MGMT_INDEX_NONE, MGMT_OP_READ_VERSION, 0,
				 &rp, sizeof(rp));
}

static int read_commands(struct sock *sk, struct hci_dev *hdev, void *data,
			 u16 data_len)
{
	struct mgmt_rp_read_commands *rp;
	u16 num_commands, num_events;
	size_t rp_size;
	int i, err;

	bt_dev_dbg(hdev, "sock %p", sk);

	if (hci_sock_test_flag(sk, HCI_SOCK_TRUSTED)) {
		num_commands = ARRAY_SIZE(mgmt_commands);
		num_events = ARRAY_SIZE(mgmt_events);
	} else {
		num_commands = ARRAY_SIZE(mgmt_untrusted_commands);
		num_events = ARRAY_SIZE(mgmt_untrusted_events);
	}

	rp_size = sizeof(*rp) + ((num_commands + num_events) * sizeof(u16));

	rp = kmalloc(rp_size, GFP_KERNEL);
	if (!rp)
		return -ENOMEM;

	rp->num_commands = cpu_to_le16(num_commands);
	rp->num_events = cpu_to_le16(num_events);

	if (hci_sock_test_flag(sk, HCI_SOCK_TRUSTED)) {
		__le16 *opcode = rp->opcodes;

		for (i = 0; i < num_commands; i++, opcode++)
			put_unaligned_le16(mgmt_commands[i], opcode);

		for (i = 0; i < num_events; i++, opcode++)
			put_unaligned_le16(mgmt_events[i], opcode);
	} else {
		__le16 *opcode = rp->opcodes;

		for (i = 0; i < num_commands; i++, opcode++)
			put_unaligned_le16(mgmt_untrusted_commands[i], opcode);

		for (i = 0; i < num_events; i++, opcode++)
			put_unaligned_le16(mgmt_untrusted_events[i], opcode);
	}

	err = mgmt_cmd_complete(sk, MGMT_INDEX_NONE, MGMT_OP_READ_COMMANDS, 0,
				rp, rp_size);
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

	bt_dev_dbg(hdev, "sock %p", sk);

	read_lock(&hci_dev_list_lock);

	count = 0;
	list_for_each_entry(d, &hci_dev_list, list) {
		if (d->dev_type == HCI_PRIMARY &&
		    !hci_dev_test_flag(d, HCI_UNCONFIGURED))
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
		if (hci_dev_test_flag(d, HCI_SETUP) ||
		    hci_dev_test_flag(d, HCI_CONFIG) ||
		    hci_dev_test_flag(d, HCI_USER_CHANNEL))
			continue;

		/* Devices marked as raw-only are neither configured
		 * nor unconfigured controllers.
		 */
		if (test_bit(HCI_QUIRK_RAW_DEVICE, &d->quirks))
			continue;

		if (d->dev_type == HCI_PRIMARY &&
		    !hci_dev_test_flag(d, HCI_UNCONFIGURED)) {
			rp->index[count++] = cpu_to_le16(d->id);
			bt_dev_dbg(hdev, "Added hci%u", d->id);
		}
	}

	rp->num_controllers = cpu_to_le16(count);
	rp_len = sizeof(*rp) + (2 * count);

	read_unlock(&hci_dev_list_lock);

	err = mgmt_cmd_complete(sk, MGMT_INDEX_NONE, MGMT_OP_READ_INDEX_LIST,
				0, rp, rp_len);

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

	bt_dev_dbg(hdev, "sock %p", sk);

	read_lock(&hci_dev_list_lock);

	count = 0;
	list_for_each_entry(d, &hci_dev_list, list) {
		if (d->dev_type == HCI_PRIMARY &&
		    hci_dev_test_flag(d, HCI_UNCONFIGURED))
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
		if (hci_dev_test_flag(d, HCI_SETUP) ||
		    hci_dev_test_flag(d, HCI_CONFIG) ||
		    hci_dev_test_flag(d, HCI_USER_CHANNEL))
			continue;

		/* Devices marked as raw-only are neither configured
		 * nor unconfigured controllers.
		 */
		if (test_bit(HCI_QUIRK_RAW_DEVICE, &d->quirks))
			continue;

		if (d->dev_type == HCI_PRIMARY &&
		    hci_dev_test_flag(d, HCI_UNCONFIGURED)) {
			rp->index[count++] = cpu_to_le16(d->id);
			bt_dev_dbg(hdev, "Added hci%u", d->id);
		}
	}

	rp->num_controllers = cpu_to_le16(count);
	rp_len = sizeof(*rp) + (2 * count);

	read_unlock(&hci_dev_list_lock);

	err = mgmt_cmd_complete(sk, MGMT_INDEX_NONE,
				MGMT_OP_READ_UNCONF_INDEX_LIST, 0, rp, rp_len);

	kfree(rp);

	return err;
}

static int read_ext_index_list(struct sock *sk, struct hci_dev *hdev,
			       void *data, u16 data_len)
{
	struct mgmt_rp_read_ext_index_list *rp;
	struct hci_dev *d;
	u16 count;
	int err;

	bt_dev_dbg(hdev, "sock %p", sk);

	read_lock(&hci_dev_list_lock);

	count = 0;
	list_for_each_entry(d, &hci_dev_list, list) {
		if (d->dev_type == HCI_PRIMARY || d->dev_type == HCI_AMP)
			count++;
	}

	rp = kmalloc(struct_size(rp, entry, count), GFP_ATOMIC);
	if (!rp) {
		read_unlock(&hci_dev_list_lock);
		return -ENOMEM;
	}

	count = 0;
	list_for_each_entry(d, &hci_dev_list, list) {
		if (hci_dev_test_flag(d, HCI_SETUP) ||
		    hci_dev_test_flag(d, HCI_CONFIG) ||
		    hci_dev_test_flag(d, HCI_USER_CHANNEL))
			continue;

		/* Devices marked as raw-only are neither configured
		 * nor unconfigured controllers.
		 */
		if (test_bit(HCI_QUIRK_RAW_DEVICE, &d->quirks))
			continue;

		if (d->dev_type == HCI_PRIMARY) {
			if (hci_dev_test_flag(d, HCI_UNCONFIGURED))
				rp->entry[count].type = 0x01;
			else
				rp->entry[count].type = 0x00;
		} else if (d->dev_type == HCI_AMP) {
			rp->entry[count].type = 0x02;
		} else {
			continue;
		}

		rp->entry[count].bus = d->bus;
		rp->entry[count++].index = cpu_to_le16(d->id);
		bt_dev_dbg(hdev, "Added hci%u", d->id);
	}

	rp->num_controllers = cpu_to_le16(count);

	read_unlock(&hci_dev_list_lock);

	/* If this command is called at least once, then all the
	 * default index and unconfigured index events are disabled
	 * and from now on only extended index events are used.
	 */
	hci_sock_set_flag(sk, HCI_MGMT_EXT_INDEX_EVENTS);
	hci_sock_clear_flag(sk, HCI_MGMT_INDEX_EVENTS);
	hci_sock_clear_flag(sk, HCI_MGMT_UNCONF_INDEX_EVENTS);

	err = mgmt_cmd_complete(sk, MGMT_INDEX_NONE,
				MGMT_OP_READ_EXT_INDEX_LIST, 0, rp,
				struct_size(rp, entry, count));

	kfree(rp);

	return err;
}

static bool is_configured(struct hci_dev *hdev)
{
	if (test_bit(HCI_QUIRK_EXTERNAL_CONFIG, &hdev->quirks) &&
	    !hci_dev_test_flag(hdev, HCI_EXT_CONFIGURED))
		return false;

	if ((test_bit(HCI_QUIRK_INVALID_BDADDR, &hdev->quirks) ||
	     test_bit(HCI_QUIRK_USE_BDADDR_PROPERTY, &hdev->quirks)) &&
	    !bacmp(&hdev->public_addr, BDADDR_ANY))
		return false;

	return true;
}

static __le32 get_missing_options(struct hci_dev *hdev)
{
	u32 options = 0;

	if (test_bit(HCI_QUIRK_EXTERNAL_CONFIG, &hdev->quirks) &&
	    !hci_dev_test_flag(hdev, HCI_EXT_CONFIGURED))
		options |= MGMT_OPTION_EXTERNAL_CONFIG;

	if ((test_bit(HCI_QUIRK_INVALID_BDADDR, &hdev->quirks) ||
	     test_bit(HCI_QUIRK_USE_BDADDR_PROPERTY, &hdev->quirks)) &&
	    !bacmp(&hdev->public_addr, BDADDR_ANY))
		options |= MGMT_OPTION_PUBLIC_ADDRESS;

	return cpu_to_le32(options);
}

static int new_options(struct hci_dev *hdev, struct sock *skip)
{
	__le32 options = get_missing_options(hdev);

	return mgmt_limited_event(MGMT_EV_NEW_CONFIG_OPTIONS, hdev, &options,
				  sizeof(options), HCI_MGMT_OPTION_EVENTS, skip);
}

static int send_options_rsp(struct sock *sk, u16 opcode, struct hci_dev *hdev)
{
	__le32 options = get_missing_options(hdev);

	return mgmt_cmd_complete(sk, hdev->id, opcode, 0, &options,
				 sizeof(options));
}

static int read_config_info(struct sock *sk, struct hci_dev *hdev,
			    void *data, u16 data_len)
{
	struct mgmt_rp_read_config_info rp;
	u32 options = 0;

	bt_dev_dbg(hdev, "sock %p", sk);

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

	return mgmt_cmd_complete(sk, hdev->id, MGMT_OP_READ_CONFIG_INFO, 0,
				 &rp, sizeof(rp));
}

static u32 get_supported_phys(struct hci_dev *hdev)
{
	u32 supported_phys = 0;

	if (lmp_bredr_capable(hdev)) {
		supported_phys |= MGMT_PHY_BR_1M_1SLOT;

		if (hdev->features[0][0] & LMP_3SLOT)
			supported_phys |= MGMT_PHY_BR_1M_3SLOT;

		if (hdev->features[0][0] & LMP_5SLOT)
			supported_phys |= MGMT_PHY_BR_1M_5SLOT;

		if (lmp_edr_2m_capable(hdev)) {
			supported_phys |= MGMT_PHY_EDR_2M_1SLOT;

			if (lmp_edr_3slot_capable(hdev))
				supported_phys |= MGMT_PHY_EDR_2M_3SLOT;

			if (lmp_edr_5slot_capable(hdev))
				supported_phys |= MGMT_PHY_EDR_2M_5SLOT;

			if (lmp_edr_3m_capable(hdev)) {
				supported_phys |= MGMT_PHY_EDR_3M_1SLOT;

				if (lmp_edr_3slot_capable(hdev))
					supported_phys |= MGMT_PHY_EDR_3M_3SLOT;

				if (lmp_edr_5slot_capable(hdev))
					supported_phys |= MGMT_PHY_EDR_3M_5SLOT;
			}
		}
	}

	if (lmp_le_capable(hdev)) {
		supported_phys |= MGMT_PHY_LE_1M_TX;
		supported_phys |= MGMT_PHY_LE_1M_RX;

		if (hdev->le_features[1] & HCI_LE_PHY_2M) {
			supported_phys |= MGMT_PHY_LE_2M_TX;
			supported_phys |= MGMT_PHY_LE_2M_RX;
		}

		if (hdev->le_features[1] & HCI_LE_PHY_CODED) {
			supported_phys |= MGMT_PHY_LE_CODED_TX;
			supported_phys |= MGMT_PHY_LE_CODED_RX;
		}
	}

	return supported_phys;
}

static u32 get_selected_phys(struct hci_dev *hdev)
{
	u32 selected_phys = 0;

	if (lmp_bredr_capable(hdev)) {
		selected_phys |= MGMT_PHY_BR_1M_1SLOT;

		if (hdev->pkt_type & (HCI_DM3 | HCI_DH3))
			selected_phys |= MGMT_PHY_BR_1M_3SLOT;

		if (hdev->pkt_type & (HCI_DM5 | HCI_DH5))
			selected_phys |= MGMT_PHY_BR_1M_5SLOT;

		if (lmp_edr_2m_capable(hdev)) {
			if (!(hdev->pkt_type & HCI_2DH1))
				selected_phys |= MGMT_PHY_EDR_2M_1SLOT;

			if (lmp_edr_3slot_capable(hdev) &&
			    !(hdev->pkt_type & HCI_2DH3))
				selected_phys |= MGMT_PHY_EDR_2M_3SLOT;

			if (lmp_edr_5slot_capable(hdev) &&
			    !(hdev->pkt_type & HCI_2DH5))
				selected_phys |= MGMT_PHY_EDR_2M_5SLOT;

			if (lmp_edr_3m_capable(hdev)) {
				if (!(hdev->pkt_type & HCI_3DH1))
					selected_phys |= MGMT_PHY_EDR_3M_1SLOT;

				if (lmp_edr_3slot_capable(hdev) &&
				    !(hdev->pkt_type & HCI_3DH3))
					selected_phys |= MGMT_PHY_EDR_3M_3SLOT;

				if (lmp_edr_5slot_capable(hdev) &&
				    !(hdev->pkt_type & HCI_3DH5))
					selected_phys |= MGMT_PHY_EDR_3M_5SLOT;
			}
		}
	}

	if (lmp_le_capable(hdev)) {
		if (hdev->le_tx_def_phys & HCI_LE_SET_PHY_1M)
			selected_phys |= MGMT_PHY_LE_1M_TX;

		if (hdev->le_rx_def_phys & HCI_LE_SET_PHY_1M)
			selected_phys |= MGMT_PHY_LE_1M_RX;

		if (hdev->le_tx_def_phys & HCI_LE_SET_PHY_2M)
			selected_phys |= MGMT_PHY_LE_2M_TX;

		if (hdev->le_rx_def_phys & HCI_LE_SET_PHY_2M)
			selected_phys |= MGMT_PHY_LE_2M_RX;

		if (hdev->le_tx_def_phys & HCI_LE_SET_PHY_CODED)
			selected_phys |= MGMT_PHY_LE_CODED_TX;

		if (hdev->le_rx_def_phys & HCI_LE_SET_PHY_CODED)
			selected_phys |= MGMT_PHY_LE_CODED_RX;
	}

	return selected_phys;
}

static u32 get_configurable_phys(struct hci_dev *hdev)
{
	return (get_supported_phys(hdev) & ~MGMT_PHY_BR_1M_1SLOT &
		~MGMT_PHY_LE_1M_TX & ~MGMT_PHY_LE_1M_RX);
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
			if (IS_ENABLED(CONFIG_BT_HS))
				settings |= MGMT_SETTING_HS;
		}

		if (lmp_sc_capable(hdev))
			settings |= MGMT_SETTING_SECURE_CONN;

		if (test_bit(HCI_QUIRK_WIDEBAND_SPEECH_SUPPORTED,
			     &hdev->quirks))
			settings |= MGMT_SETTING_WIDEBAND_SPEECH;
	}

	if (lmp_le_capable(hdev)) {
		settings |= MGMT_SETTING_LE;
		settings |= MGMT_SETTING_SECURE_CONN;
		settings |= MGMT_SETTING_PRIVACY;
		settings |= MGMT_SETTING_STATIC_ADDRESS;
		settings |= MGMT_SETTING_ADVERTISING;
	}

	if (test_bit(HCI_QUIRK_EXTERNAL_CONFIG, &hdev->quirks) ||
	    hdev->set_bdaddr)
		settings |= MGMT_SETTING_CONFIGURATION;

	settings |= MGMT_SETTING_PHY_CONFIGURATION;

	return settings;
}

static u32 get_current_settings(struct hci_dev *hdev)
{
	u32 settings = 0;

	if (hdev_is_powered(hdev))
		settings |= MGMT_SETTING_POWERED;

	if (hci_dev_test_flag(hdev, HCI_CONNECTABLE))
		settings |= MGMT_SETTING_CONNECTABLE;

	if (hci_dev_test_flag(hdev, HCI_FAST_CONNECTABLE))
		settings |= MGMT_SETTING_FAST_CONNECTABLE;

	if (hci_dev_test_flag(hdev, HCI_DISCOVERABLE))
		settings |= MGMT_SETTING_DISCOVERABLE;

	if (hci_dev_test_flag(hdev, HCI_BONDABLE))
		settings |= MGMT_SETTING_BONDABLE;

	if (hci_dev_test_flag(hdev, HCI_BREDR_ENABLED))
		settings |= MGMT_SETTING_BREDR;

	if (hci_dev_test_flag(hdev, HCI_LE_ENABLED))
		settings |= MGMT_SETTING_LE;

	if (hci_dev_test_flag(hdev, HCI_LINK_SECURITY))
		settings |= MGMT_SETTING_LINK_SECURITY;

	if (hci_dev_test_flag(hdev, HCI_SSP_ENABLED))
		settings |= MGMT_SETTING_SSP;

	if (hci_dev_test_flag(hdev, HCI_HS_ENABLED))
		settings |= MGMT_SETTING_HS;

	if (hci_dev_test_flag(hdev, HCI_ADVERTISING))
		settings |= MGMT_SETTING_ADVERTISING;

	if (hci_dev_test_flag(hdev, HCI_SC_ENABLED))
		settings |= MGMT_SETTING_SECURE_CONN;

	if (hci_dev_test_flag(hdev, HCI_KEEP_DEBUG_KEYS))
		settings |= MGMT_SETTING_DEBUG_KEYS;

	if (hci_dev_test_flag(hdev, HCI_PRIVACY))
		settings |= MGMT_SETTING_PRIVACY;

	/* The current setting for static address has two purposes. The
	 * first is to indicate if the static address will be used and
	 * the second is to indicate if it is actually set.
	 *
	 * This means if the static address is not configured, this flag
	 * will never be set. If the address is configured, then if the
	 * address is actually used decides if the flag is set or not.
	 *
	 * For single mode LE only controllers and dual-mode controllers
	 * with BR/EDR disabled, the existence of the static address will
	 * be evaluated.
	 */
	if (hci_dev_test_flag(hdev, HCI_FORCE_STATIC_ADDR) ||
	    !hci_dev_test_flag(hdev, HCI_BREDR_ENABLED) ||
	    !bacmp(&hdev->bdaddr, BDADDR_ANY)) {
		if (bacmp(&hdev->static_addr, BDADDR_ANY))
			settings |= MGMT_SETTING_STATIC_ADDRESS;
	}

	if (hci_dev_test_flag(hdev, HCI_WIDEBAND_SPEECH_ENABLED))
		settings |= MGMT_SETTING_WIDEBAND_SPEECH;

	return settings;
}

static struct mgmt_pending_cmd *pending_find(u16 opcode, struct hci_dev *hdev)
{
	return mgmt_pending_find(HCI_CHANNEL_CONTROL, opcode, hdev);
}

u8 mgmt_get_adv_discov_flags(struct hci_dev *hdev)
{
	struct mgmt_pending_cmd *cmd;

	/* If there's a pending mgmt command the flags will not yet have
	 * their final values, so check for this first.
	 */
	cmd = pending_find(MGMT_OP_SET_DISCOVERABLE, hdev);
	if (cmd) {
		struct mgmt_mode *cp = cmd->param;
		if (cp->val == 0x01)
			return LE_AD_GENERAL;
		else if (cp->val == 0x02)
			return LE_AD_LIMITED;
	} else {
		if (hci_dev_test_flag(hdev, HCI_LIMITED_DISCOVERABLE))
			return LE_AD_LIMITED;
		else if (hci_dev_test_flag(hdev, HCI_DISCOVERABLE))
			return LE_AD_GENERAL;
	}

	return 0;
}

bool mgmt_get_connectable(struct hci_dev *hdev)
{
	struct mgmt_pending_cmd *cmd;

	/* If there's a pending mgmt command the flag will not yet have
	 * it's final value, so check for this first.
	 */
	cmd = pending_find(MGMT_OP_SET_CONNECTABLE, hdev);
	if (cmd) {
		struct mgmt_mode *cp = cmd->param;

		return cp->val;
	}

	return hci_dev_test_flag(hdev, HCI_CONNECTABLE);
}

static int service_cache_sync(struct hci_dev *hdev, void *data)
{
	hci_update_eir_sync(hdev);
	hci_update_class_sync(hdev);

	return 0;
}

static void service_cache_off(struct work_struct *work)
{
	struct hci_dev *hdev = container_of(work, struct hci_dev,
					    service_cache.work);

	if (!hci_dev_test_and_clear_flag(hdev, HCI_SERVICE_CACHE))
		return;

	hci_cmd_sync_queue(hdev, service_cache_sync, NULL, NULL);
}

static int rpa_expired_sync(struct hci_dev *hdev, void *data)
{
	/* The generation of a new RPA and programming it into the
	 * controller happens in the hci_req_enable_advertising()
	 * function.
	 */
	if (ext_adv_capable(hdev))
		return hci_start_ext_adv_sync(hdev, hdev->cur_adv_instance);
	else
		return hci_enable_advertising_sync(hdev);
}

static void rpa_expired(struct work_struct *work)
{
	struct hci_dev *hdev = container_of(work, struct hci_dev,
					    rpa_expired.work);

	bt_dev_dbg(hdev, "");

	hci_dev_set_flag(hdev, HCI_RPA_EXPIRED);

	if (!hci_dev_test_flag(hdev, HCI_ADVERTISING))
		return;

	hci_cmd_sync_queue(hdev, rpa_expired_sync, NULL, NULL);
}

static void mgmt_init_hdev(struct sock *sk, struct hci_dev *hdev)
{
	if (hci_dev_test_and_set_flag(hdev, HCI_MGMT))
		return;

	INIT_DELAYED_WORK(&hdev->service_cache, service_cache_off);
	INIT_DELAYED_WORK(&hdev->rpa_expired, rpa_expired);

	/* Non-mgmt controlled devices get this bit set
	 * implicitly so that pairing works for them, however
	 * for mgmt we require user-space to explicitly enable
	 * it
	 */
	hci_dev_clear_flag(hdev, HCI_BONDABLE);
}

static int read_controller_info(struct sock *sk, struct hci_dev *hdev,
				void *data, u16 data_len)
{
	struct mgmt_rp_read_info rp;

	bt_dev_dbg(hdev, "sock %p", sk);

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

	return mgmt_cmd_complete(sk, hdev->id, MGMT_OP_READ_INFO, 0, &rp,
				 sizeof(rp));
}

static u16 append_eir_data_to_buf(struct hci_dev *hdev, u8 *eir)
{
	u16 eir_len = 0;
	size_t name_len;

	if (hci_dev_test_flag(hdev, HCI_BREDR_ENABLED))
		eir_len = eir_append_data(eir, eir_len, EIR_CLASS_OF_DEV,
					  hdev->dev_class, 3);

	if (hci_dev_test_flag(hdev, HCI_LE_ENABLED))
		eir_len = eir_append_le16(eir, eir_len, EIR_APPEARANCE,
					  hdev->appearance);

	name_len = strlen(hdev->dev_name);
	eir_len = eir_append_data(eir, eir_len, EIR_NAME_COMPLETE,
				  hdev->dev_name, name_len);

	name_len = strlen(hdev->short_name);
	eir_len = eir_append_data(eir, eir_len, EIR_NAME_SHORT,
				  hdev->short_name, name_len);

	return eir_len;
}

static int read_ext_controller_info(struct sock *sk, struct hci_dev *hdev,
				    void *data, u16 data_len)
{
	char buf[512];
	struct mgmt_rp_read_ext_info *rp = (void *)buf;
	u16 eir_len;

	bt_dev_dbg(hdev, "sock %p", sk);

	memset(&buf, 0, sizeof(buf));

	hci_dev_lock(hdev);

	bacpy(&rp->bdaddr, &hdev->bdaddr);

	rp->version = hdev->hci_ver;
	rp->manufacturer = cpu_to_le16(hdev->manufacturer);

	rp->supported_settings = cpu_to_le32(get_supported_settings(hdev));
	rp->current_settings = cpu_to_le32(get_current_settings(hdev));


	eir_len = append_eir_data_to_buf(hdev, rp->eir);
	rp->eir_len = cpu_to_le16(eir_len);

	hci_dev_unlock(hdev);

	/* If this command is called at least once, then the events
	 * for class of device and local name changes are disabled
	 * and only the new extended controller information event
	 * is used.
	 */
	hci_sock_set_flag(sk, HCI_MGMT_EXT_INFO_EVENTS);
	hci_sock_clear_flag(sk, HCI_MGMT_DEV_CLASS_EVENTS);
	hci_sock_clear_flag(sk, HCI_MGMT_LOCAL_NAME_EVENTS);

	return mgmt_cmd_complete(sk, hdev->id, MGMT_OP_READ_EXT_INFO, 0, rp,
				 sizeof(*rp) + eir_len);
}

static int ext_info_changed(struct hci_dev *hdev, struct sock *skip)
{
	char buf[512];
	struct mgmt_ev_ext_info_changed *ev = (void *)buf;
	u16 eir_len;

	memset(buf, 0, sizeof(buf));

	eir_len = append_eir_data_to_buf(hdev, ev->eir);
	ev->eir_len = cpu_to_le16(eir_len);

	return mgmt_limited_event(MGMT_EV_EXT_INFO_CHANGED, hdev, ev,
				  sizeof(*ev) + eir_len,
				  HCI_MGMT_EXT_INFO_EVENTS, skip);
}

static int send_settings_rsp(struct sock *sk, u16 opcode, struct hci_dev *hdev)
{
	__le32 settings = cpu_to_le32(get_current_settings(hdev));

	return mgmt_cmd_complete(sk, hdev->id, opcode, 0, &settings,
				 sizeof(settings));
}

void mgmt_advertising_added(struct sock *sk, struct hci_dev *hdev, u8 instance)
{
	struct mgmt_ev_advertising_added ev;

	ev.instance = instance;

	mgmt_event(MGMT_EV_ADVERTISING_ADDED, hdev, &ev, sizeof(ev), sk);
}

void mgmt_advertising_removed(struct sock *sk, struct hci_dev *hdev,
			      u8 instance)
{
	struct mgmt_ev_advertising_removed ev;

	ev.instance = instance;

	mgmt_event(MGMT_EV_ADVERTISING_REMOVED, hdev, &ev, sizeof(ev), sk);
}

static void cancel_adv_timeout(struct hci_dev *hdev)
{
	if (hdev->adv_instance_timeout) {
		hdev->adv_instance_timeout = 0;
		cancel_delayed_work(&hdev->adv_instance_expire);
	}
}

/* This function requires the caller holds hdev->lock */
static void restart_le_actions(struct hci_dev *hdev)
{
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
}

static int new_settings(struct hci_dev *hdev, struct sock *skip)
{
	__le32 ev = cpu_to_le32(get_current_settings(hdev));

	return mgmt_limited_event(MGMT_EV_NEW_SETTINGS, hdev, &ev,
				  sizeof(ev), HCI_MGMT_SETTING_EVENTS, skip);
}

static void mgmt_set_powered_complete(struct hci_dev *hdev, void *data, int err)
{
	struct mgmt_pending_cmd *cmd = data;
	struct mgmt_mode *cp = cmd->param;

	bt_dev_dbg(hdev, "err %d", err);

	if (!err) {
		if (cp->val) {
			hci_dev_lock(hdev);
			restart_le_actions(hdev);
			hci_update_passive_scan(hdev);
			hci_dev_unlock(hdev);
		}

		send_settings_rsp(cmd->sk, cmd->opcode, hdev);

		/* Only call new_setting for power on as power off is deferred
		 * to hdev->power_off work which does call hci_dev_do_close.
		 */
		if (cp->val)
			new_settings(hdev, cmd->sk);
	} else {
		mgmt_cmd_status(cmd->sk, hdev->id, MGMT_OP_SET_POWERED,
				mgmt_status(err));
	}

	mgmt_pending_free(cmd);
}

static int set_powered_sync(struct hci_dev *hdev, void *data)
{
	struct mgmt_pending_cmd *cmd = data;
	struct mgmt_mode *cp = cmd->param;

	BT_DBG("%s", hdev->name);

	return hci_set_powered_sync(hdev, cp->val);
}

static int set_powered(struct sock *sk, struct hci_dev *hdev, void *data,
		       u16 len)
{
	struct mgmt_mode *cp = data;
	struct mgmt_pending_cmd *cmd;
	int err;

	bt_dev_dbg(hdev, "sock %p", sk);

	if (cp->val != 0x00 && cp->val != 0x01)
		return mgmt_cmd_status(sk, hdev->id, MGMT_OP_SET_POWERED,
				       MGMT_STATUS_INVALID_PARAMS);

	hci_dev_lock(hdev);

	if (pending_find(MGMT_OP_SET_POWERED, hdev)) {
		err = mgmt_cmd_status(sk, hdev->id, MGMT_OP_SET_POWERED,
				      MGMT_STATUS_BUSY);
		goto failed;
	}

	if (!!cp->val == hdev_is_powered(hdev)) {
		err = send_settings_rsp(sk, MGMT_OP_SET_POWERED, hdev);
		goto failed;
	}

	cmd = mgmt_pending_new(sk, MGMT_OP_SET_POWERED, hdev, data, len);
	if (!cmd) {
		err = -ENOMEM;
		goto failed;
	}

	err = hci_cmd_sync_queue(hdev, set_powered_sync, cmd,
				 mgmt_set_powered_complete);

failed:
	hci_dev_unlock(hdev);
	return err;
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

static void settings_rsp(struct mgmt_pending_cmd *cmd, void *data)
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

static void cmd_status_rsp(struct mgmt_pending_cmd *cmd, void *data)
{
	u8 *status = data;

	mgmt_cmd_status(cmd->sk, cmd->index, cmd->opcode, *status);
	mgmt_pending_remove(cmd);
}

static void cmd_complete_rsp(struct mgmt_pending_cmd *cmd, void *data)
{
	if (cmd->cmd_complete) {
		u8 *status = data;

		cmd->cmd_complete(cmd, *status);
		mgmt_pending_remove(cmd);

		return;
	}

	cmd_status_rsp(cmd, data);
}

static int generic_cmd_complete(struct mgmt_pending_cmd *cmd, u8 status)
{
	return mgmt_cmd_complete(cmd->sk, cmd->index, cmd->opcode, status,
				 cmd->param, cmd->param_len);
}

static int addr_cmd_complete(struct mgmt_pending_cmd *cmd, u8 status)
{
	return mgmt_cmd_complete(cmd->sk, cmd->index, cmd->opcode, status,
				 cmd->param, sizeof(struct mgmt_addr_info));
}

static u8 mgmt_bredr_support(struct hci_dev *hdev)
{
	if (!lmp_bredr_capable(hdev))
		return MGMT_STATUS_NOT_SUPPORTED;
	else if (!hci_dev_test_flag(hdev, HCI_BREDR_ENABLED))
		return MGMT_STATUS_REJECTED;
	else
		return MGMT_STATUS_SUCCESS;
}

static u8 mgmt_le_support(struct hci_dev *hdev)
{
	if (!lmp_le_capable(hdev))
		return MGMT_STATUS_NOT_SUPPORTED;
	else if (!hci_dev_test_flag(hdev, HCI_LE_ENABLED))
		return MGMT_STATUS_REJECTED;
	else
		return MGMT_STATUS_SUCCESS;
}

void mgmt_set_discoverable_complete(struct hci_dev *hdev, u8 status)
{
	struct mgmt_pending_cmd *cmd;

	bt_dev_dbg(hdev, "status 0x%02x", status);

	hci_dev_lock(hdev);

	cmd = pending_find(MGMT_OP_SET_DISCOVERABLE, hdev);
	if (!cmd)
		goto unlock;

	if (status) {
		u8 mgmt_err = mgmt_status(status);
		mgmt_cmd_status(cmd->sk, cmd->index, cmd->opcode, mgmt_err);
		hci_dev_clear_flag(hdev, HCI_LIMITED_DISCOVERABLE);
		goto remove_cmd;
	}

	if (hci_dev_test_flag(hdev, HCI_DISCOVERABLE) &&
	    hdev->discov_timeout > 0) {
		int to = msecs_to_jiffies(hdev->discov_timeout * 1000);
		queue_delayed_work(hdev->req_workqueue, &hdev->discov_off, to);
	}

	send_settings_rsp(cmd->sk, MGMT_OP_SET_DISCOVERABLE, hdev);
	new_settings(hdev, cmd->sk);

remove_cmd:
	mgmt_pending_remove(cmd);

unlock:
	hci_dev_unlock(hdev);
}

static int set_discoverable(struct sock *sk, struct hci_dev *hdev, void *data,
			    u16 len)
{
	struct mgmt_cp_set_discoverable *cp = data;
	struct mgmt_pending_cmd *cmd;
	u16 timeout;
	int err;

	bt_dev_dbg(hdev, "sock %p", sk);

	if (!hci_dev_test_flag(hdev, HCI_LE_ENABLED) &&
	    !hci_dev_test_flag(hdev, HCI_BREDR_ENABLED))
		return mgmt_cmd_status(sk, hdev->id, MGMT_OP_SET_DISCOVERABLE,
				       MGMT_STATUS_REJECTED);

	if (cp->val != 0x00 && cp->val != 0x01 && cp->val != 0x02)
		return mgmt_cmd_status(sk, hdev->id, MGMT_OP_SET_DISCOVERABLE,
				       MGMT_STATUS_INVALID_PARAMS);

	timeout = __le16_to_cpu(cp->timeout);

	/* Disabling discoverable requires that no timeout is set,
	 * and enabling limited discoverable requires a timeout.
	 */
	if ((cp->val == 0x00 && timeout > 0) ||
	    (cp->val == 0x02 && timeout == 0))
		return mgmt_cmd_status(sk, hdev->id, MGMT_OP_SET_DISCOVERABLE,
				       MGMT_STATUS_INVALID_PARAMS);

	hci_dev_lock(hdev);

	if (!hdev_is_powered(hdev) && timeout > 0) {
		err = mgmt_cmd_status(sk, hdev->id, MGMT_OP_SET_DISCOVERABLE,
				      MGMT_STATUS_NOT_POWERED);
		goto failed;
	}

	if (pending_find(MGMT_OP_SET_DISCOVERABLE, hdev) ||
	    pending_find(MGMT_OP_SET_CONNECTABLE, hdev)) {
		err = mgmt_cmd_status(sk, hdev->id, MGMT_OP_SET_DISCOVERABLE,
				      MGMT_STATUS_BUSY);
		goto failed;
	}

	if (!hci_dev_test_flag(hdev, HCI_CONNECTABLE)) {
		err = mgmt_cmd_status(sk, hdev->id, MGMT_OP_SET_DISCOVERABLE,
				      MGMT_STATUS_REJECTED);
		goto failed;
	}

	if (hdev->advertising_paused) {
		err = mgmt_cmd_status(sk, hdev->id, MGMT_OP_SET_DISCOVERABLE,
				      MGMT_STATUS_BUSY);
		goto failed;
	}

	if (!hdev_is_powered(hdev)) {
		bool changed = false;

		/* Setting limited discoverable when powered off is
		 * not a valid operation since it requires a timeout
		 * and so no need to check HCI_LIMITED_DISCOVERABLE.
		 */
		if (!!cp->val != hci_dev_test_flag(hdev, HCI_DISCOVERABLE)) {
			hci_dev_change_flag(hdev, HCI_DISCOVERABLE);
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
	if (!!cp->val == hci_dev_test_flag(hdev, HCI_DISCOVERABLE) &&
	    (cp->val == 0x02) == hci_dev_test_flag(hdev,
						   HCI_LIMITED_DISCOVERABLE)) {
		cancel_delayed_work(&hdev->discov_off);
		hdev->discov_timeout = timeout;

		if (cp->val && hdev->discov_timeout > 0) {
			int to = msecs_to_jiffies(hdev->discov_timeout * 1000);
			queue_delayed_work(hdev->req_workqueue,
					   &hdev->discov_off, to);
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

	if (cp->val)
		hci_dev_set_flag(hdev, HCI_DISCOVERABLE);
	else
		hci_dev_clear_flag(hdev, HCI_DISCOVERABLE);

	/* Limited discoverable mode */
	if (cp->val == 0x02)
		hci_dev_set_flag(hdev, HCI_LIMITED_DISCOVERABLE);
	else
		hci_dev_clear_flag(hdev, HCI_LIMITED_DISCOVERABLE);

	queue_work(hdev->req_workqueue, &hdev->discoverable_update);
	err = 0;

failed:
	hci_dev_unlock(hdev);
	return err;
}

void mgmt_set_connectable_complete(struct hci_dev *hdev, u8 status)
{
	struct mgmt_pending_cmd *cmd;

	bt_dev_dbg(hdev, "status 0x%02x", status);

	hci_dev_lock(hdev);

	cmd = pending_find(MGMT_OP_SET_CONNECTABLE, hdev);
	if (!cmd)
		goto unlock;

	if (status) {
		u8 mgmt_err = mgmt_status(status);
		mgmt_cmd_status(cmd->sk, cmd->index, cmd->opcode, mgmt_err);
		goto remove_cmd;
	}

	send_settings_rsp(cmd->sk, MGMT_OP_SET_CONNECTABLE, hdev);
	new_settings(hdev, cmd->sk);

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

	if (!!val != hci_dev_test_flag(hdev, HCI_CONNECTABLE))
		changed = true;

	if (val) {
		hci_dev_set_flag(hdev, HCI_CONNECTABLE);
	} else {
		hci_dev_clear_flag(hdev, HCI_CONNECTABLE);
		hci_dev_clear_flag(hdev, HCI_DISCOVERABLE);
	}

	err = send_settings_rsp(sk, MGMT_OP_SET_CONNECTABLE, hdev);
	if (err < 0)
		return err;

	if (changed) {
		hci_req_update_scan(hdev);
		hci_update_passive_scan(hdev);
		return new_settings(hdev, sk);
	}

	return 0;
}

static int set_connectable(struct sock *sk, struct hci_dev *hdev, void *data,
			   u16 len)
{
	struct mgmt_mode *cp = data;
	struct mgmt_pending_cmd *cmd;
	int err;

	bt_dev_dbg(hdev, "sock %p", sk);

	if (!hci_dev_test_flag(hdev, HCI_LE_ENABLED) &&
	    !hci_dev_test_flag(hdev, HCI_BREDR_ENABLED))
		return mgmt_cmd_status(sk, hdev->id, MGMT_OP_SET_CONNECTABLE,
				       MGMT_STATUS_REJECTED);

	if (cp->val != 0x00 && cp->val != 0x01)
		return mgmt_cmd_status(sk, hdev->id, MGMT_OP_SET_CONNECTABLE,
				       MGMT_STATUS_INVALID_PARAMS);

	hci_dev_lock(hdev);

	if (!hdev_is_powered(hdev)) {
		err = set_connectable_update_settings(hdev, sk, cp->val);
		goto failed;
	}

	if (pending_find(MGMT_OP_SET_DISCOVERABLE, hdev) ||
	    pending_find(MGMT_OP_SET_CONNECTABLE, hdev)) {
		err = mgmt_cmd_status(sk, hdev->id, MGMT_OP_SET_CONNECTABLE,
				      MGMT_STATUS_BUSY);
		goto failed;
	}

	cmd = mgmt_pending_add(sk, MGMT_OP_SET_CONNECTABLE, hdev, data, len);
	if (!cmd) {
		err = -ENOMEM;
		goto failed;
	}

	if (cp->val) {
		hci_dev_set_flag(hdev, HCI_CONNECTABLE);
	} else {
		if (hdev->discov_timeout > 0)
			cancel_delayed_work(&hdev->discov_off);

		hci_dev_clear_flag(hdev, HCI_LIMITED_DISCOVERABLE);
		hci_dev_clear_flag(hdev, HCI_DISCOVERABLE);
		hci_dev_clear_flag(hdev, HCI_CONNECTABLE);
	}

	queue_work(hdev->req_workqueue, &hdev->connectable_update);
	err = 0;

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

	bt_dev_dbg(hdev, "sock %p", sk);

	if (cp->val != 0x00 && cp->val != 0x01)
		return mgmt_cmd_status(sk, hdev->id, MGMT_OP_SET_BONDABLE,
				       MGMT_STATUS_INVALID_PARAMS);

	hci_dev_lock(hdev);

	if (cp->val)
		changed = !hci_dev_test_and_set_flag(hdev, HCI_BONDABLE);
	else
		changed = hci_dev_test_and_clear_flag(hdev, HCI_BONDABLE);

	err = send_settings_rsp(sk, MGMT_OP_SET_BONDABLE, hdev);
	if (err < 0)
		goto unlock;

	if (changed) {
		/* In limited privacy mode the change of bondable mode
		 * may affect the local advertising address.
		 */
		if (hdev_is_powered(hdev) &&
		    hci_dev_test_flag(hdev, HCI_ADVERTISING) &&
		    hci_dev_test_flag(hdev, HCI_DISCOVERABLE) &&
		    hci_dev_test_flag(hdev, HCI_LIMITED_PRIVACY))
			queue_work(hdev->req_workqueue,
				   &hdev->discoverable_update);

		err = new_settings(hdev, sk);
	}

unlock:
	hci_dev_unlock(hdev);
	return err;
}

static int set_link_security(struct sock *sk, struct hci_dev *hdev, void *data,
			     u16 len)
{
	struct mgmt_mode *cp = data;
	struct mgmt_pending_cmd *cmd;
	u8 val, status;
	int err;

	bt_dev_dbg(hdev, "sock %p", sk);

	status = mgmt_bredr_support(hdev);
	if (status)
		return mgmt_cmd_status(sk, hdev->id, MGMT_OP_SET_LINK_SECURITY,
				       status);

	if (cp->val != 0x00 && cp->val != 0x01)
		return mgmt_cmd_status(sk, hdev->id, MGMT_OP_SET_LINK_SECURITY,
				       MGMT_STATUS_INVALID_PARAMS);

	hci_dev_lock(hdev);

	if (!hdev_is_powered(hdev)) {
		bool changed = false;

		if (!!cp->val != hci_dev_test_flag(hdev, HCI_LINK_SECURITY)) {
			hci_dev_change_flag(hdev, HCI_LINK_SECURITY);
			changed = true;
		}

		err = send_settings_rsp(sk, MGMT_OP_SET_LINK_SECURITY, hdev);
		if (err < 0)
			goto failed;

		if (changed)
			err = new_settings(hdev, sk);

		goto failed;
	}

	if (pending_find(MGMT_OP_SET_LINK_SECURITY, hdev)) {
		err = mgmt_cmd_status(sk, hdev->id, MGMT_OP_SET_LINK_SECURITY,
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

static void set_ssp_complete(struct hci_dev *hdev, void *data, int err)
{
	struct cmd_lookup match = { NULL, hdev };
	struct mgmt_pending_cmd *cmd = data;
	struct mgmt_mode *cp = cmd->param;
	u8 enable = cp->val;
	bool changed;

	if (err) {
		u8 mgmt_err = mgmt_status(err);

		if (enable && hci_dev_test_and_clear_flag(hdev,
							  HCI_SSP_ENABLED)) {
			hci_dev_clear_flag(hdev, HCI_HS_ENABLED);
			new_settings(hdev, NULL);
		}

		mgmt_pending_foreach(MGMT_OP_SET_SSP, hdev, cmd_status_rsp,
				     &mgmt_err);
		return;
	}

	if (enable) {
		changed = !hci_dev_test_and_set_flag(hdev, HCI_SSP_ENABLED);
	} else {
		changed = hci_dev_test_and_clear_flag(hdev, HCI_SSP_ENABLED);

		if (!changed)
			changed = hci_dev_test_and_clear_flag(hdev,
							      HCI_HS_ENABLED);
		else
			hci_dev_clear_flag(hdev, HCI_HS_ENABLED);
	}

	mgmt_pending_foreach(MGMT_OP_SET_SSP, hdev, settings_rsp, &match);

	if (changed)
		new_settings(hdev, match.sk);

	if (match.sk)
		sock_put(match.sk);

	hci_update_eir_sync(hdev);
}

static int set_ssp_sync(struct hci_dev *hdev, void *data)
{
	struct mgmt_pending_cmd *cmd = data;
	struct mgmt_mode *cp = cmd->param;
	bool changed = false;
	int err;

	if (cp->val)
		changed = !hci_dev_test_and_set_flag(hdev, HCI_SSP_ENABLED);

	err = hci_write_ssp_mode_sync(hdev, cp->val);

	if (!err && changed)
		hci_dev_clear_flag(hdev, HCI_SSP_ENABLED);

	return err;
}

static int set_ssp(struct sock *sk, struct hci_dev *hdev, void *data, u16 len)
{
	struct mgmt_mode *cp = data;
	struct mgmt_pending_cmd *cmd;
	u8 status;
	int err;

	bt_dev_dbg(hdev, "sock %p", sk);

	status = mgmt_bredr_support(hdev);
	if (status)
		return mgmt_cmd_status(sk, hdev->id, MGMT_OP_SET_SSP, status);

	if (!lmp_ssp_capable(hdev))
		return mgmt_cmd_status(sk, hdev->id, MGMT_OP_SET_SSP,
				       MGMT_STATUS_NOT_SUPPORTED);

	if (cp->val != 0x00 && cp->val != 0x01)
		return mgmt_cmd_status(sk, hdev->id, MGMT_OP_SET_SSP,
				       MGMT_STATUS_INVALID_PARAMS);

	hci_dev_lock(hdev);

	if (!hdev_is_powered(hdev)) {
		bool changed;

		if (cp->val) {
			changed = !hci_dev_test_and_set_flag(hdev,
							     HCI_SSP_ENABLED);
		} else {
			changed = hci_dev_test_and_clear_flag(hdev,
							      HCI_SSP_ENABLED);
			if (!changed)
				changed = hci_dev_test_and_clear_flag(hdev,
								      HCI_HS_ENABLED);
			else
				hci_dev_clear_flag(hdev, HCI_HS_ENABLED);
		}

		err = send_settings_rsp(sk, MGMT_OP_SET_SSP, hdev);
		if (err < 0)
			goto failed;

		if (changed)
			err = new_settings(hdev, sk);

		goto failed;
	}

	if (pending_find(MGMT_OP_SET_SSP, hdev)) {
		err = mgmt_cmd_status(sk, hdev->id, MGMT_OP_SET_SSP,
				      MGMT_STATUS_BUSY);
		goto failed;
	}

	if (!!cp->val == hci_dev_test_flag(hdev, HCI_SSP_ENABLED)) {
		err = send_settings_rsp(sk, MGMT_OP_SET_SSP, hdev);
		goto failed;
	}

	cmd = mgmt_pending_add(sk, MGMT_OP_SET_SSP, hdev, data, len);
	if (!cmd)
		err = -ENOMEM;
	else
		err = hci_cmd_sync_queue(hdev, set_ssp_sync, cmd,
					 set_ssp_complete);

	if (err < 0) {
		err = mgmt_cmd_status(sk, hdev->id, MGMT_OP_SET_SSP,
				      MGMT_STATUS_FAILED);

		if (cmd)
			mgmt_pending_remove(cmd);
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

	bt_dev_dbg(hdev, "sock %p", sk);

	if (!IS_ENABLED(CONFIG_BT_HS))
		return mgmt_cmd_status(sk, hdev->id, MGMT_OP_SET_HS,
				       MGMT_STATUS_NOT_SUPPORTED);

	status = mgmt_bredr_support(hdev);
	if (status)
		return mgmt_cmd_status(sk, hdev->id, MGMT_OP_SET_HS, status);

	if (!lmp_ssp_capable(hdev))
		return mgmt_cmd_status(sk, hdev->id, MGMT_OP_SET_HS,
				       MGMT_STATUS_NOT_SUPPORTED);

	if (!hci_dev_test_flag(hdev, HCI_SSP_ENABLED))
		return mgmt_cmd_status(sk, hdev->id, MGMT_OP_SET_HS,
				       MGMT_STATUS_REJECTED);

	if (cp->val != 0x00 && cp->val != 0x01)
		return mgmt_cmd_status(sk, hdev->id, MGMT_OP_SET_HS,
				       MGMT_STATUS_INVALID_PARAMS);

	hci_dev_lock(hdev);

	if (pending_find(MGMT_OP_SET_SSP, hdev)) {
		err = mgmt_cmd_status(sk, hdev->id, MGMT_OP_SET_HS,
				      MGMT_STATUS_BUSY);
		goto unlock;
	}

	if (cp->val) {
		changed = !hci_dev_test_and_set_flag(hdev, HCI_HS_ENABLED);
	} else {
		if (hdev_is_powered(hdev)) {
			err = mgmt_cmd_status(sk, hdev->id, MGMT_OP_SET_HS,
					      MGMT_STATUS_REJECTED);
			goto unlock;
		}

		changed = hci_dev_test_and_clear_flag(hdev, HCI_HS_ENABLED);
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

static void set_le_complete(struct hci_dev *hdev, void *data, int err)
{
	struct cmd_lookup match = { NULL, hdev };
	u8 status = mgmt_status(err);

	bt_dev_dbg(hdev, "err %d", err);

	if (status) {
		mgmt_pending_foreach(MGMT_OP_SET_LE, hdev, cmd_status_rsp,
							&status);
		return;
	}

	mgmt_pending_foreach(MGMT_OP_SET_LE, hdev, settings_rsp, &match);

	new_settings(hdev, match.sk);

	if (match.sk)
		sock_put(match.sk);
}

static int set_le_sync(struct hci_dev *hdev, void *data)
{
	struct mgmt_pending_cmd *cmd = data;
	struct mgmt_mode *cp = cmd->param;
	u8 val = !!cp->val;
	int err;

	if (!val) {
		if (hci_dev_test_flag(hdev, HCI_LE_ADV))
			hci_disable_advertising_sync(hdev);

		if (ext_adv_capable(hdev))
			hci_remove_ext_adv_instance_sync(hdev, 0, cmd->sk);
	} else {
		hci_dev_set_flag(hdev, HCI_LE_ENABLED);
	}

	err = hci_write_le_host_supported_sync(hdev, val, 0);

	/* Make sure the controller has a good default for
	 * advertising data. Restrict the update to when LE
	 * has actually been enabled. During power on, the
	 * update in powered_update_hci will take care of it.
	 */
	if (!err && hci_dev_test_flag(hdev, HCI_LE_ENABLED)) {
		if (ext_adv_capable(hdev)) {
			int status;

			status = hci_setup_ext_adv_instance_sync(hdev, 0x00);
			if (!status)
				hci_update_scan_rsp_data_sync(hdev, 0x00);
		} else {
			hci_update_adv_data_sync(hdev, 0x00);
			hci_update_scan_rsp_data_sync(hdev, 0x00);
		}

		hci_update_passive_scan(hdev);
	}

	return err;
}

static int set_le(struct sock *sk, struct hci_dev *hdev, void *data, u16 len)
{
	struct mgmt_mode *cp = data;
	struct mgmt_pending_cmd *cmd;
	int err;
	u8 val, enabled;

	bt_dev_dbg(hdev, "sock %p", sk);

	if (!lmp_le_capable(hdev))
		return mgmt_cmd_status(sk, hdev->id, MGMT_OP_SET_LE,
				       MGMT_STATUS_NOT_SUPPORTED);

	if (cp->val != 0x00 && cp->val != 0x01)
		return mgmt_cmd_status(sk, hdev->id, MGMT_OP_SET_LE,
				       MGMT_STATUS_INVALID_PARAMS);

	/* Bluetooth single mode LE only controllers or dual-mode
	 * controllers configured as LE only devices, do not allow
	 * switching LE off. These have either LE enabled explicitly
	 * or BR/EDR has been previously switched off.
	 *
	 * When trying to enable an already enabled LE, then gracefully
	 * send a positive response. Trying to disable it however will
	 * result into rejection.
	 */
	if (!hci_dev_test_flag(hdev, HCI_BREDR_ENABLED)) {
		if (cp->val == 0x01)
			return send_settings_rsp(sk, MGMT_OP_SET_LE, hdev);

		return mgmt_cmd_status(sk, hdev->id, MGMT_OP_SET_LE,
				       MGMT_STATUS_REJECTED);
	}

	hci_dev_lock(hdev);

	val = !!cp->val;
	enabled = lmp_host_le_capable(hdev);

	if (!val)
		hci_req_clear_adv_instance(hdev, NULL, NULL, 0x00, true);

	if (!hdev_is_powered(hdev) || val == enabled) {
		bool changed = false;

		if (val != hci_dev_test_flag(hdev, HCI_LE_ENABLED)) {
			hci_dev_change_flag(hdev, HCI_LE_ENABLED);
			changed = true;
		}

		if (!val && hci_dev_test_flag(hdev, HCI_ADVERTISING)) {
			hci_dev_clear_flag(hdev, HCI_ADVERTISING);
			changed = true;
		}

		err = send_settings_rsp(sk, MGMT_OP_SET_LE, hdev);
		if (err < 0)
			goto unlock;

		if (changed)
			err = new_settings(hdev, sk);

		goto unlock;
	}

	if (pending_find(MGMT_OP_SET_LE, hdev) ||
	    pending_find(MGMT_OP_SET_ADVERTISING, hdev)) {
		err = mgmt_cmd_status(sk, hdev->id, MGMT_OP_SET_LE,
				      MGMT_STATUS_BUSY);
		goto unlock;
	}

	cmd = mgmt_pending_add(sk, MGMT_OP_SET_LE, hdev, data, len);
	if (!cmd)
		err = -ENOMEM;
	else
		err = hci_cmd_sync_queue(hdev, set_le_sync, cmd,
					 set_le_complete);

	if (err < 0) {
		err = mgmt_cmd_status(sk, hdev->id, MGMT_OP_SET_LE,
				      MGMT_STATUS_FAILED);

		if (cmd)
			mgmt_pending_remove(cmd);
	}

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
	struct mgmt_pending_cmd *cmd;

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

static void mgmt_class_complete(struct hci_dev *hdev, void *data, int err)
{
	struct mgmt_pending_cmd *cmd = data;

	bt_dev_dbg(hdev, "err %d", err);

	mgmt_cmd_complete(cmd->sk, cmd->index, cmd->opcode,
			  mgmt_status(err), hdev->dev_class, 3);

	mgmt_pending_free(cmd);
}

static int add_uuid_sync(struct hci_dev *hdev, void *data)
{
	int err;

	err = hci_update_class_sync(hdev);
	if (err)
		return err;

	return hci_update_eir_sync(hdev);
}

static int add_uuid(struct sock *sk, struct hci_dev *hdev, void *data, u16 len)
{
	struct mgmt_cp_add_uuid *cp = data;
	struct mgmt_pending_cmd *cmd;
	struct bt_uuid *uuid;
	int err;

	bt_dev_dbg(hdev, "sock %p", sk);

	hci_dev_lock(hdev);

	if (pending_eir_or_class(hdev)) {
		err = mgmt_cmd_status(sk, hdev->id, MGMT_OP_ADD_UUID,
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

	cmd = mgmt_pending_new(sk, MGMT_OP_ADD_UUID, hdev, data, len);
	if (!cmd) {
		err = -ENOMEM;
		goto failed;
	}

	err = hci_cmd_sync_queue(hdev, add_uuid_sync, cmd, mgmt_class_complete);
	if (err < 0) {
		mgmt_pending_free(cmd);
		goto failed;
	}

failed:
	hci_dev_unlock(hdev);
	return err;
}

static bool enable_service_cache(struct hci_dev *hdev)
{
	if (!hdev_is_powered(hdev))
		return false;

	if (!hci_dev_test_and_set_flag(hdev, HCI_SERVICE_CACHE)) {
		queue_delayed_work(hdev->workqueue, &hdev->service_cache,
				   CACHE_TIMEOUT);
		return true;
	}

	return false;
}

static int remove_uuid_sync(struct hci_dev *hdev, void *data)
{
	int err;

	err = hci_update_class_sync(hdev);
	if (err)
		return err;

	return hci_update_eir_sync(hdev);
}

static int remove_uuid(struct sock *sk, struct hci_dev *hdev, void *data,
		       u16 len)
{
	struct mgmt_cp_remove_uuid *cp = data;
	struct mgmt_pending_cmd *cmd;
	struct bt_uuid *match, *tmp;
	u8 bt_uuid_any[] = { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };
	int err, found;

	bt_dev_dbg(hdev, "sock %p", sk);

	hci_dev_lock(hdev);

	if (pending_eir_or_class(hdev)) {
		err = mgmt_cmd_status(sk, hdev->id, MGMT_OP_REMOVE_UUID,
				      MGMT_STATUS_BUSY);
		goto unlock;
	}

	if (memcmp(cp->uuid, bt_uuid_any, 16) == 0) {
		hci_uuids_clear(hdev);

		if (enable_service_cache(hdev)) {
			err = mgmt_cmd_complete(sk, hdev->id,
						MGMT_OP_REMOVE_UUID,
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
		err = mgmt_cmd_status(sk, hdev->id, MGMT_OP_REMOVE_UUID,
				      MGMT_STATUS_INVALID_PARAMS);
		goto unlock;
	}

update_class:
	cmd = mgmt_pending_new(sk, MGMT_OP_REMOVE_UUID, hdev, data, len);
	if (!cmd) {
		err = -ENOMEM;
		goto unlock;
	}

	err = hci_cmd_sync_queue(hdev, remove_uuid_sync, cmd,
				 mgmt_class_complete);
	if (err < 0)
		mgmt_pending_free(cmd);

unlock:
	hci_dev_unlock(hdev);
	return err;
}

static int set_class_sync(struct hci_dev *hdev, void *data)
{
	int err = 0;

	if (hci_dev_test_and_clear_flag(hdev, HCI_SERVICE_CACHE)) {
		cancel_delayed_work_sync(&hdev->service_cache);
		err = hci_update_eir_sync(hdev);
	}

	if (err)
		return err;

	return hci_update_class_sync(hdev);
}

static int set_dev_class(struct sock *sk, struct hci_dev *hdev, void *data,
			 u16 len)
{
	struct mgmt_cp_set_dev_class *cp = data;
	struct mgmt_pending_cmd *cmd;
	int err;

	bt_dev_dbg(hdev, "sock %p", sk);

	if (!lmp_bredr_capable(hdev))
		return mgmt_cmd_status(sk, hdev->id, MGMT_OP_SET_DEV_CLASS,
				       MGMT_STATUS_NOT_SUPPORTED);

	hci_dev_lock(hdev);

	if (pending_eir_or_class(hdev)) {
		err = mgmt_cmd_status(sk, hdev->id, MGMT_OP_SET_DEV_CLASS,
				      MGMT_STATUS_BUSY);
		goto unlock;
	}

	if ((cp->minor & 0x03) != 0 || (cp->major & 0xe0) != 0) {
		err = mgmt_cmd_status(sk, hdev->id, MGMT_OP_SET_DEV_CLASS,
				      MGMT_STATUS_INVALID_PARAMS);
		goto unlock;
	}

	hdev->major_class = cp->major;
	hdev->minor_class = cp->minor;

	if (!hdev_is_powered(hdev)) {
		err = mgmt_cmd_complete(sk, hdev->id, MGMT_OP_SET_DEV_CLASS, 0,
					hdev->dev_class, 3);
		goto unlock;
	}

	cmd = mgmt_pending_new(sk, MGMT_OP_SET_DEV_CLASS, hdev, data, len);
	if (!cmd) {
		err = -ENOMEM;
		goto unlock;
	}

	err = hci_cmd_sync_queue(hdev, set_class_sync, cmd,
				 mgmt_class_complete);
	if (err < 0)
		mgmt_pending_free(cmd);

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

	bt_dev_dbg(hdev, "sock %p", sk);

	if (!lmp_bredr_capable(hdev))
		return mgmt_cmd_status(sk, hdev->id, MGMT_OP_LOAD_LINK_KEYS,
				       MGMT_STATUS_NOT_SUPPORTED);

	key_count = __le16_to_cpu(cp->key_count);
	if (key_count > max_key_count) {
		bt_dev_err(hdev, "load_link_keys: too big key_count value %u",
			   key_count);
		return mgmt_cmd_status(sk, hdev->id, MGMT_OP_LOAD_LINK_KEYS,
				       MGMT_STATUS_INVALID_PARAMS);
	}

	expected_len = struct_size(cp, keys, key_count);
	if (expected_len != len) {
		bt_dev_err(hdev, "load_link_keys: expected %u bytes, got %u bytes",
			   expected_len, len);
		return mgmt_cmd_status(sk, hdev->id, MGMT_OP_LOAD_LINK_KEYS,
				       MGMT_STATUS_INVALID_PARAMS);
	}

	if (cp->debug_keys != 0x00 && cp->debug_keys != 0x01)
		return mgmt_cmd_status(sk, hdev->id, MGMT_OP_LOAD_LINK_KEYS,
				       MGMT_STATUS_INVALID_PARAMS);

	bt_dev_dbg(hdev, "debug_keys %u key_count %u", cp->debug_keys,
		   key_count);

	for (i = 0; i < key_count; i++) {
		struct mgmt_link_key_info *key = &cp->keys[i];

		if (key->addr.type != BDADDR_BREDR || key->type > 0x08)
			return mgmt_cmd_status(sk, hdev->id,
					       MGMT_OP_LOAD_LINK_KEYS,
					       MGMT_STATUS_INVALID_PARAMS);
	}

	hci_dev_lock(hdev);

	hci_link_keys_clear(hdev);

	if (cp->debug_keys)
		changed = !hci_dev_test_and_set_flag(hdev, HCI_KEEP_DEBUG_KEYS);
	else
		changed = hci_dev_test_and_clear_flag(hdev,
						      HCI_KEEP_DEBUG_KEYS);

	if (changed)
		new_settings(hdev, NULL);

	for (i = 0; i < key_count; i++) {
		struct mgmt_link_key_info *key = &cp->keys[i];

		if (hci_is_blocked_key(hdev,
				       HCI_BLOCKED_KEY_TYPE_LINKKEY,
				       key->val)) {
			bt_dev_warn(hdev, "Skipping blocked link key for %pMR",
				    &key->addr.bdaddr);
			continue;
		}

		/* Always ignore debug keys and require a new pairing if
		 * the user wants to use them.
		 */
		if (key->type == HCI_LK_DEBUG_COMBINATION)
			continue;

		hci_add_link_key(hdev, NULL, &key->addr.bdaddr, key->val,
				 key->type, key->pin_len, NULL);
	}

	mgmt_cmd_complete(sk, hdev->id, MGMT_OP_LOAD_LINK_KEYS, 0, NULL, 0);

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
	struct hci_conn_params *params;
	struct mgmt_pending_cmd *cmd;
	struct hci_conn *conn;
	u8 addr_type;
	int err;

	memset(&rp, 0, sizeof(rp));
	bacpy(&rp.addr.bdaddr, &cp->addr.bdaddr);
	rp.addr.type = cp->addr.type;

	if (!bdaddr_type_is_valid(cp->addr.type))
		return mgmt_cmd_complete(sk, hdev->id, MGMT_OP_UNPAIR_DEVICE,
					 MGMT_STATUS_INVALID_PARAMS,
					 &rp, sizeof(rp));

	if (cp->disconnect != 0x00 && cp->disconnect != 0x01)
		return mgmt_cmd_complete(sk, hdev->id, MGMT_OP_UNPAIR_DEVICE,
					 MGMT_STATUS_INVALID_PARAMS,
					 &rp, sizeof(rp));

	hci_dev_lock(hdev);

	if (!hdev_is_powered(hdev)) {
		err = mgmt_cmd_complete(sk, hdev->id, MGMT_OP_UNPAIR_DEVICE,
					MGMT_STATUS_NOT_POWERED, &rp,
					sizeof(rp));
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
		if (err < 0) {
			err = mgmt_cmd_complete(sk, hdev->id,
						MGMT_OP_UNPAIR_DEVICE,
						MGMT_STATUS_NOT_PAIRED, &rp,
						sizeof(rp));
			goto unlock;
		}

		goto done;
	}

	/* LE address type */
	addr_type = le_addr_type(cp->addr.type);

	/* Abort any ongoing SMP pairing. Removes ltk and irk if they exist. */
	err = smp_cancel_and_remove_pairing(hdev, &cp->addr.bdaddr, addr_type);
	if (err < 0) {
		err = mgmt_cmd_complete(sk, hdev->id, MGMT_OP_UNPAIR_DEVICE,
					MGMT_STATUS_NOT_PAIRED, &rp,
					sizeof(rp));
		goto unlock;
	}

	conn = hci_conn_hash_lookup_le(hdev, &cp->addr.bdaddr, addr_type);
	if (!conn) {
		hci_conn_params_del(hdev, &cp->addr.bdaddr, addr_type);
		goto done;
	}


	/* Defer clearing up the connection parameters until closing to
	 * give a chance of keeping them if a repairing happens.
	 */
	set_bit(HCI_CONN_PARAM_REMOVAL_PEND, &conn->flags);

	/* Disable auto-connection parameters if present */
	params = hci_conn_params_lookup(hdev, &cp->addr.bdaddr, addr_type);
	if (params) {
		if (params->explicit_connect)
			params->auto_connect = HCI_AUTO_CONN_EXPLICIT;
		else
			params->auto_connect = HCI_AUTO_CONN_DISABLED;
	}

	/* If disconnection is not requested, then clear the connection
	 * variable so that the link is not terminated.
	 */
	if (!cp->disconnect)
		conn = NULL;

done:
	/* If the connection variable is set, then termination of the
	 * link is requested.
	 */
	if (!conn) {
		err = mgmt_cmd_complete(sk, hdev->id, MGMT_OP_UNPAIR_DEVICE, 0,
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

	err = hci_abort_conn(conn, HCI_ERROR_REMOTE_USER_TERM);
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
	struct mgmt_pending_cmd *cmd;
	struct hci_conn *conn;
	int err;

	bt_dev_dbg(hdev, "sock %p", sk);

	memset(&rp, 0, sizeof(rp));
	bacpy(&rp.addr.bdaddr, &cp->addr.bdaddr);
	rp.addr.type = cp->addr.type;

	if (!bdaddr_type_is_valid(cp->addr.type))
		return mgmt_cmd_complete(sk, hdev->id, MGMT_OP_DISCONNECT,
					 MGMT_STATUS_INVALID_PARAMS,
					 &rp, sizeof(rp));

	hci_dev_lock(hdev);

	if (!test_bit(HCI_UP, &hdev->flags)) {
		err = mgmt_cmd_complete(sk, hdev->id, MGMT_OP_DISCONNECT,
					MGMT_STATUS_NOT_POWERED, &rp,
					sizeof(rp));
		goto failed;
	}

	if (pending_find(MGMT_OP_DISCONNECT, hdev)) {
		err = mgmt_cmd_complete(sk, hdev->id, MGMT_OP_DISCONNECT,
					MGMT_STATUS_BUSY, &rp, sizeof(rp));
		goto failed;
	}

	if (cp->addr.type == BDADDR_BREDR)
		conn = hci_conn_hash_lookup_ba(hdev, ACL_LINK,
					       &cp->addr.bdaddr);
	else
		conn = hci_conn_hash_lookup_le(hdev, &cp->addr.bdaddr,
					       le_addr_type(cp->addr.type));

	if (!conn || conn->state == BT_OPEN || conn->state == BT_CLOSED) {
		err = mgmt_cmd_complete(sk, hdev->id, MGMT_OP_DISCONNECT,
					MGMT_STATUS_NOT_CONNECTED, &rp,
					sizeof(rp));
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
	int err;
	u16 i;

	bt_dev_dbg(hdev, "sock %p", sk);

	hci_dev_lock(hdev);

	if (!hdev_is_powered(hdev)) {
		err = mgmt_cmd_status(sk, hdev->id, MGMT_OP_GET_CONNECTIONS,
				      MGMT_STATUS_NOT_POWERED);
		goto unlock;
	}

	i = 0;
	list_for_each_entry(c, &hdev->conn_hash.list, list) {
		if (test_bit(HCI_CONN_MGMT_CONNECTED, &c->flags))
			i++;
	}

	rp = kmalloc(struct_size(rp, addr, i), GFP_KERNEL);
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
	err = mgmt_cmd_complete(sk, hdev->id, MGMT_OP_GET_CONNECTIONS, 0, rp,
				struct_size(rp, addr, i));

	kfree(rp);

unlock:
	hci_dev_unlock(hdev);
	return err;
}

static int send_pin_code_neg_reply(struct sock *sk, struct hci_dev *hdev,
				   struct mgmt_cp_pin_code_neg_reply *cp)
{
	struct mgmt_pending_cmd *cmd;
	int err;

	cmd = mgmt_pending_add(sk, MGMT_OP_PIN_CODE_NEG_REPLY, hdev, cp,
			       sizeof(*cp));
	if (!cmd)
		return -ENOMEM;

	cmd->cmd_complete = addr_cmd_complete;

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
	struct mgmt_pending_cmd *cmd;
	int err;

	bt_dev_dbg(hdev, "sock %p", sk);

	hci_dev_lock(hdev);

	if (!hdev_is_powered(hdev)) {
		err = mgmt_cmd_status(sk, hdev->id, MGMT_OP_PIN_CODE_REPLY,
				      MGMT_STATUS_NOT_POWERED);
		goto failed;
	}

	conn = hci_conn_hash_lookup_ba(hdev, ACL_LINK, &cp->addr.bdaddr);
	if (!conn) {
		err = mgmt_cmd_status(sk, hdev->id, MGMT_OP_PIN_CODE_REPLY,
				      MGMT_STATUS_NOT_CONNECTED);
		goto failed;
	}

	if (conn->pending_sec_level == BT_SECURITY_HIGH && cp->pin_len != 16) {
		struct mgmt_cp_pin_code_neg_reply ncp;

		memcpy(&ncp.addr, &cp->addr, sizeof(ncp.addr));

		bt_dev_err(hdev, "PIN code is not 16 bytes long");

		err = send_pin_code_neg_reply(sk, hdev, &ncp);
		if (err >= 0)
			err = mgmt_cmd_status(sk, hdev->id, MGMT_OP_PIN_CODE_REPLY,
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

	bt_dev_dbg(hdev, "sock %p", sk);

	if (cp->io_capability > SMP_IO_KEYBOARD_DISPLAY)
		return mgmt_cmd_status(sk, hdev->id, MGMT_OP_SET_IO_CAPABILITY,
				       MGMT_STATUS_INVALID_PARAMS);

	hci_dev_lock(hdev);

	hdev->io_capability = cp->io_capability;

	bt_dev_dbg(hdev, "IO capability set to 0x%02x", hdev->io_capability);

	hci_dev_unlock(hdev);

	return mgmt_cmd_complete(sk, hdev->id, MGMT_OP_SET_IO_CAPABILITY, 0,
				 NULL, 0);
}

static struct mgmt_pending_cmd *find_pairing(struct hci_conn *conn)
{
	struct hci_dev *hdev = conn->hdev;
	struct mgmt_pending_cmd *cmd;

	list_for_each_entry(cmd, &hdev->mgmt_pending, list) {
		if (cmd->opcode != MGMT_OP_PAIR_DEVICE)
			continue;

		if (cmd->user_data != conn)
			continue;

		return cmd;
	}

	return NULL;
}

static int pairing_complete(struct mgmt_pending_cmd *cmd, u8 status)
{
	struct mgmt_rp_pair_device rp;
	struct hci_conn *conn = cmd->user_data;
	int err;

	bacpy(&rp.addr.bdaddr, &conn->dst);
	rp.addr.type = link_to_bdaddr(conn->type, conn->dst_type);

	err = mgmt_cmd_complete(cmd->sk, cmd->index, MGMT_OP_PAIR_DEVICE,
				status, &rp, sizeof(rp));

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
	struct mgmt_pending_cmd *cmd;

	cmd = find_pairing(conn);
	if (cmd) {
		cmd->cmd_complete(cmd, status);
		mgmt_pending_remove(cmd);
	}
}

static void pairing_complete_cb(struct hci_conn *conn, u8 status)
{
	struct mgmt_pending_cmd *cmd;

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
	struct mgmt_pending_cmd *cmd;

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
	struct mgmt_pending_cmd *cmd;
	u8 sec_level, auth_type;
	struct hci_conn *conn;
	int err;

	bt_dev_dbg(hdev, "sock %p", sk);

	memset(&rp, 0, sizeof(rp));
	bacpy(&rp.addr.bdaddr, &cp->addr.bdaddr);
	rp.addr.type = cp->addr.type;

	if (!bdaddr_type_is_valid(cp->addr.type))
		return mgmt_cmd_complete(sk, hdev->id, MGMT_OP_PAIR_DEVICE,
					 MGMT_STATUS_INVALID_PARAMS,
					 &rp, sizeof(rp));

	if (cp->io_cap > SMP_IO_KEYBOARD_DISPLAY)
		return mgmt_cmd_complete(sk, hdev->id, MGMT_OP_PAIR_DEVICE,
					 MGMT_STATUS_INVALID_PARAMS,
					 &rp, sizeof(rp));

	hci_dev_lock(hdev);

	if (!hdev_is_powered(hdev)) {
		err = mgmt_cmd_complete(sk, hdev->id, MGMT_OP_PAIR_DEVICE,
					MGMT_STATUS_NOT_POWERED, &rp,
					sizeof(rp));
		goto unlock;
	}

	if (hci_bdaddr_is_paired(hdev, &cp->addr.bdaddr, cp->addr.type)) {
		err = mgmt_cmd_complete(sk, hdev->id, MGMT_OP_PAIR_DEVICE,
					MGMT_STATUS_ALREADY_PAIRED, &rp,
					sizeof(rp));
		goto unlock;
	}

	sec_level = BT_SECURITY_MEDIUM;
	auth_type = HCI_AT_DEDICATED_BONDING;

	if (cp->addr.type == BDADDR_BREDR) {
		conn = hci_connect_acl(hdev, &cp->addr.bdaddr, sec_level,
				       auth_type, CONN_REASON_PAIR_DEVICE);
	} else {
		u8 addr_type = le_addr_type(cp->addr.type);
		struct hci_conn_params *p;

		/* When pairing a new device, it is expected to remember
		 * this device for future connections. Adding the connection
		 * parameter information ahead of time allows tracking
		 * of the peripheral preferred values and will speed up any
		 * further connection establishment.
		 *
		 * If connection parameters already exist, then they
		 * will be kept and this function does nothing.
		 */
		p = hci_conn_params_add(hdev, &cp->addr.bdaddr, addr_type);

		if (p->auto_connect == HCI_AUTO_CONN_EXPLICIT)
			p->auto_connect = HCI_AUTO_CONN_DISABLED;

		conn = hci_connect_le_scan(hdev, &cp->addr.bdaddr, addr_type,
					   sec_level, HCI_LE_CONN_TIMEOUT,
					   CONN_REASON_PAIR_DEVICE);
	}

	if (IS_ERR(conn)) {
		int status;

		if (PTR_ERR(conn) == -EBUSY)
			status = MGMT_STATUS_BUSY;
		else if (PTR_ERR(conn) == -EOPNOTSUPP)
			status = MGMT_STATUS_NOT_SUPPORTED;
		else if (PTR_ERR(conn) == -ECONNREFUSED)
			status = MGMT_STATUS_REJECTED;
		else
			status = MGMT_STATUS_CONNECT_FAILED;

		err = mgmt_cmd_complete(sk, hdev->id, MGMT_OP_PAIR_DEVICE,
					status, &rp, sizeof(rp));
		goto unlock;
	}

	if (conn->connect_cfm_cb) {
		hci_conn_drop(conn);
		err = mgmt_cmd_complete(sk, hdev->id, MGMT_OP_PAIR_DEVICE,
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
	struct mgmt_pending_cmd *cmd;
	struct hci_conn *conn;
	int err;

	bt_dev_dbg(hdev, "sock %p", sk);

	hci_dev_lock(hdev);

	if (!hdev_is_powered(hdev)) {
		err = mgmt_cmd_status(sk, hdev->id, MGMT_OP_CANCEL_PAIR_DEVICE,
				      MGMT_STATUS_NOT_POWERED);
		goto unlock;
	}

	cmd = pending_find(MGMT_OP_PAIR_DEVICE, hdev);
	if (!cmd) {
		err = mgmt_cmd_status(sk, hdev->id, MGMT_OP_CANCEL_PAIR_DEVICE,
				      MGMT_STATUS_INVALID_PARAMS);
		goto unlock;
	}

	conn = cmd->user_data;

	if (bacmp(&addr->bdaddr, &conn->dst) != 0) {
		err = mgmt_cmd_status(sk, hdev->id, MGMT_OP_CANCEL_PAIR_DEVICE,
				      MGMT_STATUS_INVALID_PARAMS);
		goto unlock;
	}

	cmd->cmd_complete(cmd, MGMT_STATUS_CANCELLED);
	mgmt_pending_remove(cmd);

	err = mgmt_cmd_complete(sk, hdev->id, MGMT_OP_CANCEL_PAIR_DEVICE, 0,
				addr, sizeof(*addr));

	/* Since user doesn't want to proceed with the connection, abort any
	 * ongoing pairing and then terminate the link if it was created
	 * because of the pair device action.
	 */
	if (addr->type == BDADDR_BREDR)
		hci_remove_link_key(hdev, &addr->bdaddr);
	else
		smp_cancel_and_remove_pairing(hdev, &addr->bdaddr,
					      le_addr_type(addr->type));

	if (conn->conn_reason == CONN_REASON_PAIR_DEVICE)
		hci_abort_conn(conn, HCI_ERROR_REMOTE_USER_TERM);

unlock:
	hci_dev_unlock(hdev);
	return err;
}

static int user_pairing_resp(struct sock *sk, struct hci_dev *hdev,
			     struct mgmt_addr_info *addr, u16 mgmt_op,
			     u16 hci_op, __le32 passkey)
{
	struct mgmt_pending_cmd *cmd;
	struct hci_conn *conn;
	int err;

	hci_dev_lock(hdev);

	if (!hdev_is_powered(hdev)) {
		err = mgmt_cmd_complete(sk, hdev->id, mgmt_op,
					MGMT_STATUS_NOT_POWERED, addr,
					sizeof(*addr));
		goto done;
	}

	if (addr->type == BDADDR_BREDR)
		conn = hci_conn_hash_lookup_ba(hdev, ACL_LINK, &addr->bdaddr);
	else
		conn = hci_conn_hash_lookup_le(hdev, &addr->bdaddr,
					       le_addr_type(addr->type));

	if (!conn) {
		err = mgmt_cmd_complete(sk, hdev->id, mgmt_op,
					MGMT_STATUS_NOT_CONNECTED, addr,
					sizeof(*addr));
		goto done;
	}

	if (addr->type == BDADDR_LE_PUBLIC || addr->type == BDADDR_LE_RANDOM) {
		err = smp_user_confirm_reply(conn, mgmt_op, passkey);
		if (!err)
			err = mgmt_cmd_complete(sk, hdev->id, mgmt_op,
						MGMT_STATUS_SUCCESS, addr,
						sizeof(*addr));
		else
			err = mgmt_cmd_complete(sk, hdev->id, mgmt_op,
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

	bt_dev_dbg(hdev, "sock %p", sk);

	return user_pairing_resp(sk, hdev, &cp->addr,
				MGMT_OP_PIN_CODE_NEG_REPLY,
				HCI_OP_PIN_CODE_NEG_REPLY, 0);
}

static int user_confirm_reply(struct sock *sk, struct hci_dev *hdev, void *data,
			      u16 len)
{
	struct mgmt_cp_user_confirm_reply *cp = data;

	bt_dev_dbg(hdev, "sock %p", sk);

	if (len != sizeof(*cp))
		return mgmt_cmd_status(sk, hdev->id, MGMT_OP_USER_CONFIRM_REPLY,
				       MGMT_STATUS_INVALID_PARAMS);

	return user_pairing_resp(sk, hdev, &cp->addr,
				 MGMT_OP_USER_CONFIRM_REPLY,
				 HCI_OP_USER_CONFIRM_REPLY, 0);
}

static int user_confirm_neg_reply(struct sock *sk, struct hci_dev *hdev,
				  void *data, u16 len)
{
	struct mgmt_cp_user_confirm_neg_reply *cp = data;

	bt_dev_dbg(hdev, "sock %p", sk);

	return user_pairing_resp(sk, hdev, &cp->addr,
				 MGMT_OP_USER_CONFIRM_NEG_REPLY,
				 HCI_OP_USER_CONFIRM_NEG_REPLY, 0);
}

static int user_passkey_reply(struct sock *sk, struct hci_dev *hdev, void *data,
			      u16 len)
{
	struct mgmt_cp_user_passkey_reply *cp = data;

	bt_dev_dbg(hdev, "sock %p", sk);

	return user_pairing_resp(sk, hdev, &cp->addr,
				 MGMT_OP_USER_PASSKEY_REPLY,
				 HCI_OP_USER_PASSKEY_REPLY, cp->passkey);
}

static int user_passkey_neg_reply(struct sock *sk, struct hci_dev *hdev,
				  void *data, u16 len)
{
	struct mgmt_cp_user_passkey_neg_reply *cp = data;

	bt_dev_dbg(hdev, "sock %p", sk);

	return user_pairing_resp(sk, hdev, &cp->addr,
				 MGMT_OP_USER_PASSKEY_NEG_REPLY,
				 HCI_OP_USER_PASSKEY_NEG_REPLY, 0);
}

static int adv_expire_sync(struct hci_dev *hdev, u32 flags)
{
	struct adv_info *adv_instance;

	adv_instance = hci_find_adv_instance(hdev, hdev->cur_adv_instance);
	if (!adv_instance)
		return 0;

	/* stop if current instance doesn't need to be changed */
	if (!(adv_instance->flags & flags))
		return 0;

	cancel_adv_timeout(hdev);

	adv_instance = hci_get_next_instance(hdev, adv_instance->instance);
	if (!adv_instance)
		return 0;

	hci_schedule_adv_instance_sync(hdev, adv_instance->instance, true);

	return 0;
}

static int name_changed_sync(struct hci_dev *hdev, void *data)
{
	return adv_expire_sync(hdev, MGMT_ADV_FLAG_LOCAL_NAME);
}

static void set_name_complete(struct hci_dev *hdev, void *data, int err)
{
	struct mgmt_pending_cmd *cmd = data;
	struct mgmt_cp_set_local_name *cp = cmd->param;
	u8 status = mgmt_status(err);

	bt_dev_dbg(hdev, "err %d", err);

	if (status) {
		mgmt_cmd_status(cmd->sk, hdev->id, MGMT_OP_SET_LOCAL_NAME,
				status);
	} else {
		mgmt_cmd_complete(cmd->sk, hdev->id, MGMT_OP_SET_LOCAL_NAME, 0,
				  cp, sizeof(*cp));

		if (hci_dev_test_flag(hdev, HCI_LE_ADV))
			hci_cmd_sync_queue(hdev, name_changed_sync, NULL, NULL);
	}

	mgmt_pending_remove(cmd);
}

static int set_name_sync(struct hci_dev *hdev, void *data)
{
	if (lmp_bredr_capable(hdev)) {
		hci_update_name_sync(hdev);
		hci_update_eir_sync(hdev);
	}

	/* The name is stored in the scan response data and so
	 * no need to update the advertising data here.
	 */
	if (lmp_le_capable(hdev) && hci_dev_test_flag(hdev, HCI_ADVERTISING))
		hci_update_scan_rsp_data_sync(hdev, hdev->cur_adv_instance);

	return 0;
}

static int set_local_name(struct sock *sk, struct hci_dev *hdev, void *data,
			  u16 len)
{
	struct mgmt_cp_set_local_name *cp = data;
	struct mgmt_pending_cmd *cmd;
	int err;

	bt_dev_dbg(hdev, "sock %p", sk);

	hci_dev_lock(hdev);

	/* If the old values are the same as the new ones just return a
	 * direct command complete event.
	 */
	if (!memcmp(hdev->dev_name, cp->name, sizeof(hdev->dev_name)) &&
	    !memcmp(hdev->short_name, cp->short_name,
		    sizeof(hdev->short_name))) {
		err = mgmt_cmd_complete(sk, hdev->id, MGMT_OP_SET_LOCAL_NAME, 0,
					data, len);
		goto failed;
	}

	memcpy(hdev->short_name, cp->short_name, sizeof(hdev->short_name));

	if (!hdev_is_powered(hdev)) {
		memcpy(hdev->dev_name, cp->name, sizeof(hdev->dev_name));

		err = mgmt_cmd_complete(sk, hdev->id, MGMT_OP_SET_LOCAL_NAME, 0,
					data, len);
		if (err < 0)
			goto failed;

		err = mgmt_limited_event(MGMT_EV_LOCAL_NAME_CHANGED, hdev, data,
					 len, HCI_MGMT_LOCAL_NAME_EVENTS, sk);
		ext_info_changed(hdev, sk);

		goto failed;
	}

	cmd = mgmt_pending_add(sk, MGMT_OP_SET_LOCAL_NAME, hdev, data, len);
	if (!cmd)
		err = -ENOMEM;
	else
		err = hci_cmd_sync_queue(hdev, set_name_sync, cmd,
					 set_name_complete);

	if (err < 0) {
		err = mgmt_cmd_status(sk, hdev->id, MGMT_OP_SET_LOCAL_NAME,
				      MGMT_STATUS_FAILED);

		if (cmd)
			mgmt_pending_remove(cmd);

		goto failed;
	}

	memcpy(hdev->dev_name, cp->name, sizeof(hdev->dev_name));

failed:
	hci_dev_unlock(hdev);
	return err;
}

static int appearance_changed_sync(struct hci_dev *hdev, void *data)
{
	return adv_expire_sync(hdev, MGMT_ADV_FLAG_APPEARANCE);
}

static int set_appearance(struct sock *sk, struct hci_dev *hdev, void *data,
			  u16 len)
{
	struct mgmt_cp_set_appearance *cp = data;
	u16 appearance;
	int err;

	bt_dev_dbg(hdev, "sock %p", sk);

	if (!lmp_le_capable(hdev))
		return mgmt_cmd_status(sk, hdev->id, MGMT_OP_SET_APPEARANCE,
				       MGMT_STATUS_NOT_SUPPORTED);

	appearance = le16_to_cpu(cp->appearance);

	hci_dev_lock(hdev);

	if (hdev->appearance != appearance) {
		hdev->appearance = appearance;

		if (hci_dev_test_flag(hdev, HCI_LE_ADV))
			hci_cmd_sync_queue(hdev, appearance_changed_sync, NULL,
					   NULL);

		ext_info_changed(hdev, sk);
	}

	err = mgmt_cmd_complete(sk, hdev->id, MGMT_OP_SET_APPEARANCE, 0, NULL,
				0);

	hci_dev_unlock(hdev);

	return err;
}

static int get_phy_configuration(struct sock *sk, struct hci_dev *hdev,
				 void *data, u16 len)
{
	struct mgmt_rp_get_phy_configuration rp;

	bt_dev_dbg(hdev, "sock %p", sk);

	hci_dev_lock(hdev);

	memset(&rp, 0, sizeof(rp));

	rp.supported_phys = cpu_to_le32(get_supported_phys(hdev));
	rp.selected_phys = cpu_to_le32(get_selected_phys(hdev));
	rp.configurable_phys = cpu_to_le32(get_configurable_phys(hdev));

	hci_dev_unlock(hdev);

	return mgmt_cmd_complete(sk, hdev->id, MGMT_OP_GET_PHY_CONFIGURATION, 0,
				 &rp, sizeof(rp));
}

int mgmt_phy_configuration_changed(struct hci_dev *hdev, struct sock *skip)
{
	struct mgmt_ev_phy_configuration_changed ev;

	memset(&ev, 0, sizeof(ev));

	ev.selected_phys = cpu_to_le32(get_selected_phys(hdev));

	return mgmt_event(MGMT_EV_PHY_CONFIGURATION_CHANGED, hdev, &ev,
			  sizeof(ev), skip);
}

static void set_default_phy_complete(struct hci_dev *hdev, void *data, int err)
{
	struct mgmt_pending_cmd *cmd = data;
	struct sk_buff *skb = cmd->skb;
	u8 status = mgmt_status(err);

	if (!status) {
		if (!skb)
			status = MGMT_STATUS_FAILED;
		else if (IS_ERR(skb))
			status = mgmt_status(PTR_ERR(skb));
		else
			status = mgmt_status(skb->data[0]);
	}

	bt_dev_dbg(hdev, "status %d", status);

	if (status) {
		mgmt_cmd_status(cmd->sk, hdev->id,
				MGMT_OP_SET_PHY_CONFIGURATION, status);
	} else {
		mgmt_cmd_complete(cmd->sk, hdev->id,
				  MGMT_OP_SET_PHY_CONFIGURATION, 0,
				  NULL, 0);

		mgmt_phy_configuration_changed(hdev, cmd->sk);
	}

	if (skb && !IS_ERR(skb))
		kfree_skb(skb);

	mgmt_pending_remove(cmd);
}

static int set_default_phy_sync(struct hci_dev *hdev, void *data)
{
	struct mgmt_pending_cmd *cmd = data;
	struct mgmt_cp_set_phy_configuration *cp = cmd->param;
	struct hci_cp_le_set_default_phy cp_phy;
	u32 selected_phys = __le32_to_cpu(cp->selected_phys);

	memset(&cp_phy, 0, sizeof(cp_phy));

	if (!(selected_phys & MGMT_PHY_LE_TX_MASK))
		cp_phy.all_phys |= 0x01;

	if (!(selected_phys & MGMT_PHY_LE_RX_MASK))
		cp_phy.all_phys |= 0x02;

	if (selected_phys & MGMT_PHY_LE_1M_TX)
		cp_phy.tx_phys |= HCI_LE_SET_PHY_1M;

	if (selected_phys & MGMT_PHY_LE_2M_TX)
		cp_phy.tx_phys |= HCI_LE_SET_PHY_2M;

	if (selected_phys & MGMT_PHY_LE_CODED_TX)
		cp_phy.tx_phys |= HCI_LE_SET_PHY_CODED;

	if (selected_phys & MGMT_PHY_LE_1M_RX)
		cp_phy.rx_phys |= HCI_LE_SET_PHY_1M;

	if (selected_phys & MGMT_PHY_LE_2M_RX)
		cp_phy.rx_phys |= HCI_LE_SET_PHY_2M;

	if (selected_phys & MGMT_PHY_LE_CODED_RX)
		cp_phy.rx_phys |= HCI_LE_SET_PHY_CODED;

	cmd->skb =  __hci_cmd_sync(hdev, HCI_OP_LE_SET_DEFAULT_PHY,
				   sizeof(cp_phy), &cp_phy, HCI_CMD_TIMEOUT);

	return 0;
}

static int set_phy_configuration(struct sock *sk, struct hci_dev *hdev,
				 void *data, u16 len)
{
	struct mgmt_cp_set_phy_configuration *cp = data;
	struct mgmt_pending_cmd *cmd;
	u32 selected_phys, configurable_phys, supported_phys, unconfigure_phys;
	u16 pkt_type = (HCI_DH1 | HCI_DM1);
	bool changed = false;
	int err;

	bt_dev_dbg(hdev, "sock %p", sk);

	configurable_phys = get_configurable_phys(hdev);
	supported_phys = get_supported_phys(hdev);
	selected_phys = __le32_to_cpu(cp->selected_phys);

	if (selected_phys & ~supported_phys)
		return mgmt_cmd_status(sk, hdev->id,
				       MGMT_OP_SET_PHY_CONFIGURATION,
				       MGMT_STATUS_INVALID_PARAMS);

	unconfigure_phys = supported_phys & ~configurable_phys;

	if ((selected_phys & unconfigure_phys) != unconfigure_phys)
		return mgmt_cmd_status(sk, hdev->id,
				       MGMT_OP_SET_PHY_CONFIGURATION,
				       MGMT_STATUS_INVALID_PARAMS);

	if (selected_phys == get_selected_phys(hdev))
		return mgmt_cmd_complete(sk, hdev->id,
					 MGMT_OP_SET_PHY_CONFIGURATION,
					 0, NULL, 0);

	hci_dev_lock(hdev);

	if (!hdev_is_powered(hdev)) {
		err = mgmt_cmd_status(sk, hdev->id,
				      MGMT_OP_SET_PHY_CONFIGURATION,
				      MGMT_STATUS_REJECTED);
		goto unlock;
	}

	if (pending_find(MGMT_OP_SET_PHY_CONFIGURATION, hdev)) {
		err = mgmt_cmd_status(sk, hdev->id,
				      MGMT_OP_SET_PHY_CONFIGURATION,
				      MGMT_STATUS_BUSY);
		goto unlock;
	}

	if (selected_phys & MGMT_PHY_BR_1M_3SLOT)
		pkt_type |= (HCI_DH3 | HCI_DM3);
	else
		pkt_type &= ~(HCI_DH3 | HCI_DM3);

	if (selected_phys & MGMT_PHY_BR_1M_5SLOT)
		pkt_type |= (HCI_DH5 | HCI_DM5);
	else
		pkt_type &= ~(HCI_DH5 | HCI_DM5);

	if (selected_phys & MGMT_PHY_EDR_2M_1SLOT)
		pkt_type &= ~HCI_2DH1;
	else
		pkt_type |= HCI_2DH1;

	if (selected_phys & MGMT_PHY_EDR_2M_3SLOT)
		pkt_type &= ~HCI_2DH3;
	else
		pkt_type |= HCI_2DH3;

	if (selected_phys & MGMT_PHY_EDR_2M_5SLOT)
		pkt_type &= ~HCI_2DH5;
	else
		pkt_type |= HCI_2DH5;

	if (selected_phys & MGMT_PHY_EDR_3M_1SLOT)
		pkt_type &= ~HCI_3DH1;
	else
		pkt_type |= HCI_3DH1;

	if (selected_phys & MGMT_PHY_EDR_3M_3SLOT)
		pkt_type &= ~HCI_3DH3;
	else
		pkt_type |= HCI_3DH3;

	if (selected_phys & MGMT_PHY_EDR_3M_5SLOT)
		pkt_type &= ~HCI_3DH5;
	else
		pkt_type |= HCI_3DH5;

	if (pkt_type != hdev->pkt_type) {
		hdev->pkt_type = pkt_type;
		changed = true;
	}

	if ((selected_phys & MGMT_PHY_LE_MASK) ==
	    (get_selected_phys(hdev) & MGMT_PHY_LE_MASK)) {
		if (changed)
			mgmt_phy_configuration_changed(hdev, sk);

		err = mgmt_cmd_complete(sk, hdev->id,
					MGMT_OP_SET_PHY_CONFIGURATION,
					0, NULL, 0);

		goto unlock;
	}

	cmd = mgmt_pending_add(sk, MGMT_OP_SET_PHY_CONFIGURATION, hdev, data,
			       len);
	if (!cmd)
		err = -ENOMEM;
	else
		err = hci_cmd_sync_queue(hdev, set_default_phy_sync, cmd,
					 set_default_phy_complete);

	if (err < 0) {
		err = mgmt_cmd_status(sk, hdev->id,
				      MGMT_OP_SET_PHY_CONFIGURATION,
				      MGMT_STATUS_FAILED);

		if (cmd)
			mgmt_pending_remove(cmd);
	}

unlock:
	hci_dev_unlock(hdev);

	return err;
}

static int set_blocked_keys(struct sock *sk, struct hci_dev *hdev, void *data,
			    u16 len)
{
	int err = MGMT_STATUS_SUCCESS;
	struct mgmt_cp_set_blocked_keys *keys = data;
	const u16 max_key_count = ((U16_MAX - sizeof(*keys)) /
				   sizeof(struct mgmt_blocked_key_info));
	u16 key_count, expected_len;
	int i;

	bt_dev_dbg(hdev, "sock %p", sk);

	key_count = __le16_to_cpu(keys->key_count);
	if (key_count > max_key_count) {
		bt_dev_err(hdev, "too big key_count value %u", key_count);
		return mgmt_cmd_status(sk, hdev->id, MGMT_OP_SET_BLOCKED_KEYS,
				       MGMT_STATUS_INVALID_PARAMS);
	}

	expected_len = struct_size(keys, keys, key_count);
	if (expected_len != len) {
		bt_dev_err(hdev, "expected %u bytes, got %u bytes",
			   expected_len, len);
		return mgmt_cmd_status(sk, hdev->id, MGMT_OP_SET_BLOCKED_KEYS,
				       MGMT_STATUS_INVALID_PARAMS);
	}

	hci_dev_lock(hdev);

	hci_blocked_keys_clear(hdev);

	for (i = 0; i < keys->key_count; ++i) {
		struct blocked_key *b = kzalloc(sizeof(*b), GFP_KERNEL);

		if (!b) {
			err = MGMT_STATUS_NO_RESOURCES;
			break;
		}

		b->type = keys->keys[i].type;
		memcpy(b->val, keys->keys[i].val, sizeof(b->val));
		list_add_rcu(&b->list, &hdev->blocked_keys);
	}
	hci_dev_unlock(hdev);

	return mgmt_cmd_complete(sk, hdev->id, MGMT_OP_SET_BLOCKED_KEYS,
				err, NULL, 0);
}

static int set_wideband_speech(struct sock *sk, struct hci_dev *hdev,
			       void *data, u16 len)
{
	struct mgmt_mode *cp = data;
	int err;
	bool changed = false;

	bt_dev_dbg(hdev, "sock %p", sk);

	if (!test_bit(HCI_QUIRK_WIDEBAND_SPEECH_SUPPORTED, &hdev->quirks))
		return mgmt_cmd_status(sk, hdev->id,
				       MGMT_OP_SET_WIDEBAND_SPEECH,
				       MGMT_STATUS_NOT_SUPPORTED);

	if (cp->val != 0x00 && cp->val != 0x01)
		return mgmt_cmd_status(sk, hdev->id,
				       MGMT_OP_SET_WIDEBAND_SPEECH,
				       MGMT_STATUS_INVALID_PARAMS);

	hci_dev_lock(hdev);

	if (pending_find(MGMT_OP_SET_WIDEBAND_SPEECH, hdev)) {
		err = mgmt_cmd_status(sk, hdev->id,
				      MGMT_OP_SET_WIDEBAND_SPEECH,
				      MGMT_STATUS_BUSY);
		goto unlock;
	}

	if (hdev_is_powered(hdev) &&
	    !!cp->val != hci_dev_test_flag(hdev,
					   HCI_WIDEBAND_SPEECH_ENABLED)) {
		err = mgmt_cmd_status(sk, hdev->id,
				      MGMT_OP_SET_WIDEBAND_SPEECH,
				      MGMT_STATUS_REJECTED);
		goto unlock;
	}

	if (cp->val)
		changed = !hci_dev_test_and_set_flag(hdev,
						   HCI_WIDEBAND_SPEECH_ENABLED);
	else
		changed = hci_dev_test_and_clear_flag(hdev,
						   HCI_WIDEBAND_SPEECH_ENABLED);

	err = send_settings_rsp(sk, MGMT_OP_SET_WIDEBAND_SPEECH, hdev);
	if (err < 0)
		goto unlock;

	if (changed)
		err = new_settings(hdev, sk);

unlock:
	hci_dev_unlock(hdev);
	return err;
}

static int read_controller_cap(struct sock *sk, struct hci_dev *hdev,
			       void *data, u16 data_len)
{
	char buf[20];
	struct mgmt_rp_read_controller_cap *rp = (void *)buf;
	u16 cap_len = 0;
	u8 flags = 0;
	u8 tx_power_range[2];

	bt_dev_dbg(hdev, "sock %p", sk);

	memset(&buf, 0, sizeof(buf));

	hci_dev_lock(hdev);

	/* When the Read Simple Pairing Options command is supported, then
	 * the remote public key validation is supported.
	 *
	 * Alternatively, when Microsoft extensions are available, they can
	 * indicate support for public key validation as well.
	 */
	if ((hdev->commands[41] & 0x08) || msft_curve_validity(hdev))
		flags |= 0x01;	/* Remote public key validation (BR/EDR) */

	flags |= 0x02;		/* Remote public key validation (LE) */

	/* When the Read Encryption Key Size command is supported, then the
	 * encryption key size is enforced.
	 */
	if (hdev->commands[20] & 0x10)
		flags |= 0x04;	/* Encryption key size enforcement (BR/EDR) */

	flags |= 0x08;		/* Encryption key size enforcement (LE) */

	cap_len = eir_append_data(rp->cap, cap_len, MGMT_CAP_SEC_FLAGS,
				  &flags, 1);

	/* When the Read Simple Pairing Options command is supported, then
	 * also max encryption key size information is provided.
	 */
	if (hdev->commands[41] & 0x08)
		cap_len = eir_append_le16(rp->cap, cap_len,
					  MGMT_CAP_MAX_ENC_KEY_SIZE,
					  hdev->max_enc_key_size);

	cap_len = eir_append_le16(rp->cap, cap_len,
				  MGMT_CAP_SMP_MAX_ENC_KEY_SIZE,
				  SMP_MAX_ENC_KEY_SIZE);

	/* Append the min/max LE tx power parameters if we were able to fetch
	 * it from the controller
	 */
	if (hdev->commands[38] & 0x80) {
		memcpy(&tx_power_range[0], &hdev->min_le_tx_power, 1);
		memcpy(&tx_power_range[1], &hdev->max_le_tx_power, 1);
		cap_len = eir_append_data(rp->cap, cap_len, MGMT_CAP_LE_TX_PWR,
					  tx_power_range, 2);
	}

	rp->cap_len = cpu_to_le16(cap_len);

	hci_dev_unlock(hdev);

	return mgmt_cmd_complete(sk, hdev->id, MGMT_OP_READ_CONTROLLER_CAP, 0,
				 rp, sizeof(*rp) + cap_len);
}

#ifdef CONFIG_BT_FEATURE_DEBUG
/* d4992530-b9ec-469f-ab01-6c481c47da1c */
static const u8 debug_uuid[16] = {
	0x1c, 0xda, 0x47, 0x1c, 0x48, 0x6c, 0x01, 0xab,
	0x9f, 0x46, 0xec, 0xb9, 0x30, 0x25, 0x99, 0xd4,
};
#endif

/* 330859bc-7506-492d-9370-9a6f0614037f */
static const u8 quality_report_uuid[16] = {
	0x7f, 0x03, 0x14, 0x06, 0x6f, 0x9a, 0x70, 0x93,
	0x2d, 0x49, 0x06, 0x75, 0xbc, 0x59, 0x08, 0x33,
};

/* a6695ace-ee7f-4fb9-881a-5fac66c629af */
static const u8 offload_codecs_uuid[16] = {
	0xaf, 0x29, 0xc6, 0x66, 0xac, 0x5f, 0x1a, 0x88,
	0xb9, 0x4f, 0x7f, 0xee, 0xce, 0x5a, 0x69, 0xa6,
};

/* 671b10b5-42c0-4696-9227-eb28d1b049d6 */
static const u8 simult_central_periph_uuid[16] = {
	0xd6, 0x49, 0xb0, 0xd1, 0x28, 0xeb, 0x27, 0x92,
	0x96, 0x46, 0xc0, 0x42, 0xb5, 0x10, 0x1b, 0x67,
};

/* 15c0a148-c273-11ea-b3de-0242ac130004 */
static const u8 rpa_resolution_uuid[16] = {
	0x04, 0x00, 0x13, 0xac, 0x42, 0x02, 0xde, 0xb3,
	0xea, 0x11, 0x73, 0xc2, 0x48, 0xa1, 0xc0, 0x15,
};

static int read_exp_features_info(struct sock *sk, struct hci_dev *hdev,
				  void *data, u16 data_len)
{
	char buf[102];   /* Enough space for 5 features: 2 + 20 * 5 */
	struct mgmt_rp_read_exp_features_info *rp = (void *)buf;
	u16 idx = 0;
	u32 flags;

	bt_dev_dbg(hdev, "sock %p", sk);

	memset(&buf, 0, sizeof(buf));

#ifdef CONFIG_BT_FEATURE_DEBUG
	if (!hdev) {
		flags = bt_dbg_get() ? BIT(0) : 0;

		memcpy(rp->features[idx].uuid, debug_uuid, 16);
		rp->features[idx].flags = cpu_to_le32(flags);
		idx++;
	}
#endif

	if (hdev) {
		if (test_bit(HCI_QUIRK_VALID_LE_STATES, &hdev->quirks) &&
		    (hdev->le_states[4] & 0x08) &&	/* Central */
		    (hdev->le_states[4] & 0x40) &&	/* Peripheral */
		    (hdev->le_states[3] & 0x10))	/* Simultaneous */
			flags = BIT(0);
		else
			flags = 0;

		memcpy(rp->features[idx].uuid, simult_central_periph_uuid, 16);
		rp->features[idx].flags = cpu_to_le32(flags);
		idx++;
	}

	if (hdev && ll_privacy_capable(hdev)) {
		if (hci_dev_test_flag(hdev, HCI_ENABLE_LL_PRIVACY))
			flags = BIT(0) | BIT(1);
		else
			flags = BIT(1);

		memcpy(rp->features[idx].uuid, rpa_resolution_uuid, 16);
		rp->features[idx].flags = cpu_to_le32(flags);
		idx++;
	}

	if (hdev && hdev->set_quality_report) {
		if (hci_dev_test_flag(hdev, HCI_QUALITY_REPORT))
			flags = BIT(0);
		else
			flags = 0;

		memcpy(rp->features[idx].uuid, quality_report_uuid, 16);
		rp->features[idx].flags = cpu_to_le32(flags);
		idx++;
	}

	if (hdev && hdev->get_data_path_id) {
		if (hci_dev_test_flag(hdev, HCI_OFFLOAD_CODECS_ENABLED))
			flags = BIT(0);
		else
			flags = 0;

		memcpy(rp->features[idx].uuid, offload_codecs_uuid, 16);
		rp->features[idx].flags = cpu_to_le32(flags);
		idx++;
	}

	rp->feature_count = cpu_to_le16(idx);

	/* After reading the experimental features information, enable
	 * the events to update client on any future change.
	 */
	hci_sock_set_flag(sk, HCI_MGMT_EXP_FEATURE_EVENTS);

	return mgmt_cmd_complete(sk, hdev ? hdev->id : MGMT_INDEX_NONE,
				 MGMT_OP_READ_EXP_FEATURES_INFO,
				 0, rp, sizeof(*rp) + (20 * idx));
}

static int exp_ll_privacy_feature_changed(bool enabled, struct hci_dev *hdev,
					  struct sock *skip)
{
	struct mgmt_ev_exp_feature_changed ev;

	memset(&ev, 0, sizeof(ev));
	memcpy(ev.uuid, rpa_resolution_uuid, 16);
	ev.flags = cpu_to_le32((enabled ? BIT(0) : 0) | BIT(1));

	return mgmt_limited_event(MGMT_EV_EXP_FEATURE_CHANGED, hdev,
				  &ev, sizeof(ev),
				  HCI_MGMT_EXP_FEATURE_EVENTS, skip);

}

#ifdef CONFIG_BT_FEATURE_DEBUG
static int exp_debug_feature_changed(bool enabled, struct sock *skip)
{
	struct mgmt_ev_exp_feature_changed ev;

	memset(&ev, 0, sizeof(ev));
	memcpy(ev.uuid, debug_uuid, 16);
	ev.flags = cpu_to_le32(enabled ? BIT(0) : 0);

	return mgmt_limited_event(MGMT_EV_EXP_FEATURE_CHANGED, NULL,
				  &ev, sizeof(ev),
				  HCI_MGMT_EXP_FEATURE_EVENTS, skip);
}
#endif

static int exp_quality_report_feature_changed(bool enabled,
					      struct hci_dev *hdev,
					      struct sock *skip)
{
	struct mgmt_ev_exp_feature_changed ev;

	memset(&ev, 0, sizeof(ev));
	memcpy(ev.uuid, quality_report_uuid, 16);
	ev.flags = cpu_to_le32(enabled ? BIT(0) : 0);

	return mgmt_limited_event(MGMT_EV_EXP_FEATURE_CHANGED, hdev,
				  &ev, sizeof(ev),
				  HCI_MGMT_EXP_FEATURE_EVENTS, skip);
}

#define EXP_FEAT(_uuid, _set_func)	\
{					\
	.uuid = _uuid,			\
	.set_func = _set_func,		\
}

/* The zero key uuid is special. Multiple exp features are set through it. */
static int set_zero_key_func(struct sock *sk, struct hci_dev *hdev,
			     struct mgmt_cp_set_exp_feature *cp, u16 data_len)
{
	struct mgmt_rp_set_exp_feature rp;

	memset(rp.uuid, 0, 16);
	rp.flags = cpu_to_le32(0);

#ifdef CONFIG_BT_FEATURE_DEBUG
	if (!hdev) {
		bool changed = bt_dbg_get();

		bt_dbg_set(false);

		if (changed)
			exp_debug_feature_changed(false, sk);
	}
#endif

	if (hdev && use_ll_privacy(hdev) && !hdev_is_powered(hdev)) {
		bool changed = hci_dev_test_flag(hdev, HCI_ENABLE_LL_PRIVACY);

		hci_dev_clear_flag(hdev, HCI_ENABLE_LL_PRIVACY);

		if (changed)
			exp_ll_privacy_feature_changed(false, hdev, sk);
	}

	hci_sock_set_flag(sk, HCI_MGMT_EXP_FEATURE_EVENTS);

	return mgmt_cmd_complete(sk, hdev ? hdev->id : MGMT_INDEX_NONE,
				 MGMT_OP_SET_EXP_FEATURE, 0,
				 &rp, sizeof(rp));
}

#ifdef CONFIG_BT_FEATURE_DEBUG
static int set_debug_func(struct sock *sk, struct hci_dev *hdev,
			  struct mgmt_cp_set_exp_feature *cp, u16 data_len)
{
	struct mgmt_rp_set_exp_feature rp;

	bool val, changed;
	int err;

	/* Command requires to use the non-controller index */
	if (hdev)
		return mgmt_cmd_status(sk, hdev->id,
				       MGMT_OP_SET_EXP_FEATURE,
				       MGMT_STATUS_INVALID_INDEX);

	/* Parameters are limited to a single octet */
	if (data_len != MGMT_SET_EXP_FEATURE_SIZE + 1)
		return mgmt_cmd_status(sk, MGMT_INDEX_NONE,
				       MGMT_OP_SET_EXP_FEATURE,
				       MGMT_STATUS_INVALID_PARAMS);

	/* Only boolean on/off is supported */
	if (cp->param[0] != 0x00 && cp->param[0] != 0x01)
		return mgmt_cmd_status(sk, MGMT_INDEX_NONE,
				       MGMT_OP_SET_EXP_FEATURE,
				       MGMT_STATUS_INVALID_PARAMS);

	val = !!cp->param[0];
	changed = val ? !bt_dbg_get() : bt_dbg_get();
	bt_dbg_set(val);

	memcpy(rp.uuid, debug_uuid, 16);
	rp.flags = cpu_to_le32(val ? BIT(0) : 0);

	hci_sock_set_flag(sk, HCI_MGMT_EXP_FEATURE_EVENTS);

	err = mgmt_cmd_complete(sk, MGMT_INDEX_NONE,
				MGMT_OP_SET_EXP_FEATURE, 0,
				&rp, sizeof(rp));

	if (changed)
		exp_debug_feature_changed(val, sk);

	return err;
}
#endif

static int set_rpa_resolution_func(struct sock *sk, struct hci_dev *hdev,
				   struct mgmt_cp_set_exp_feature *cp,
				   u16 data_len)
{
	struct mgmt_rp_set_exp_feature rp;
	bool val, changed;
	int err;
	u32 flags;

	/* Command requires to use the controller index */
	if (!hdev)
		return mgmt_cmd_status(sk, MGMT_INDEX_NONE,
				       MGMT_OP_SET_EXP_FEATURE,
				       MGMT_STATUS_INVALID_INDEX);

	/* Changes can only be made when controller is powered down */
	if (hdev_is_powered(hdev))
		return mgmt_cmd_status(sk, hdev->id,
				       MGMT_OP_SET_EXP_FEATURE,
				       MGMT_STATUS_REJECTED);

	/* Parameters are limited to a single octet */
	if (data_len != MGMT_SET_EXP_FEATURE_SIZE + 1)
		return mgmt_cmd_status(sk, hdev->id,
				       MGMT_OP_SET_EXP_FEATURE,
				       MGMT_STATUS_INVALID_PARAMS);

	/* Only boolean on/off is supported */
	if (cp->param[0] != 0x00 && cp->param[0] != 0x01)
		return mgmt_cmd_status(sk, hdev->id,
				       MGMT_OP_SET_EXP_FEATURE,
				       MGMT_STATUS_INVALID_PARAMS);

	val = !!cp->param[0];

	if (val) {
		changed = !hci_dev_test_flag(hdev, HCI_ENABLE_LL_PRIVACY);
		hci_dev_set_flag(hdev, HCI_ENABLE_LL_PRIVACY);
		hci_dev_clear_flag(hdev, HCI_ADVERTISING);

		/* Enable LL privacy + supported settings changed */
		flags = BIT(0) | BIT(1);
	} else {
		changed = hci_dev_test_flag(hdev, HCI_ENABLE_LL_PRIVACY);
		hci_dev_clear_flag(hdev, HCI_ENABLE_LL_PRIVACY);

		/* Disable LL privacy + supported settings changed */
		flags = BIT(1);
	}

	memcpy(rp.uuid, rpa_resolution_uuid, 16);
	rp.flags = cpu_to_le32(flags);

	hci_sock_set_flag(sk, HCI_MGMT_EXP_FEATURE_EVENTS);

	err = mgmt_cmd_complete(sk, hdev->id,
				MGMT_OP_SET_EXP_FEATURE, 0,
				&rp, sizeof(rp));

	if (changed)
		exp_ll_privacy_feature_changed(val, hdev, sk);

	return err;
}

static int set_quality_report_func(struct sock *sk, struct hci_dev *hdev,
				   struct mgmt_cp_set_exp_feature *cp,
				   u16 data_len)
{
	struct mgmt_rp_set_exp_feature rp;
	bool val, changed;
	int err;

	/* Command requires to use a valid controller index */
	if (!hdev)
		return mgmt_cmd_status(sk, MGMT_INDEX_NONE,
				       MGMT_OP_SET_EXP_FEATURE,
				       MGMT_STATUS_INVALID_INDEX);

	/* Parameters are limited to a single octet */
	if (data_len != MGMT_SET_EXP_FEATURE_SIZE + 1)
		return mgmt_cmd_status(sk, hdev->id,
				       MGMT_OP_SET_EXP_FEATURE,
				       MGMT_STATUS_INVALID_PARAMS);

	/* Only boolean on/off is supported */
	if (cp->param[0] != 0x00 && cp->param[0] != 0x01)
		return mgmt_cmd_status(sk, hdev->id,
				       MGMT_OP_SET_EXP_FEATURE,
				       MGMT_STATUS_INVALID_PARAMS);

	hci_req_sync_lock(hdev);

	val = !!cp->param[0];
	changed = (val != hci_dev_test_flag(hdev, HCI_QUALITY_REPORT));

	if (!hdev->set_quality_report) {
		err = mgmt_cmd_status(sk, hdev->id,
				      MGMT_OP_SET_EXP_FEATURE,
				      MGMT_STATUS_NOT_SUPPORTED);
		goto unlock_quality_report;
	}

	if (changed) {
		err = hdev->set_quality_report(hdev, val);
		if (err) {
			err = mgmt_cmd_status(sk, hdev->id,
					      MGMT_OP_SET_EXP_FEATURE,
					      MGMT_STATUS_FAILED);
			goto unlock_quality_report;
		}
		if (val)
			hci_dev_set_flag(hdev, HCI_QUALITY_REPORT);
		else
			hci_dev_clear_flag(hdev, HCI_QUALITY_REPORT);
	}

	bt_dev_dbg(hdev, "quality report enable %d changed %d", val, changed);

	memcpy(rp.uuid, quality_report_uuid, 16);
	rp.flags = cpu_to_le32(val ? BIT(0) : 0);
	hci_sock_set_flag(sk, HCI_MGMT_EXP_FEATURE_EVENTS);
	err = mgmt_cmd_complete(sk, hdev->id,
				MGMT_OP_SET_EXP_FEATURE, 0,
				&rp, sizeof(rp));

	if (changed)
		exp_quality_report_feature_changed(val, hdev, sk);

unlock_quality_report:
	hci_req_sync_unlock(hdev);
	return err;
}

static int exp_offload_codec_feature_changed(bool enabled, struct hci_dev *hdev,
					     struct sock *skip)
{
	struct mgmt_ev_exp_feature_changed ev;

	memset(&ev, 0, sizeof(ev));
	memcpy(ev.uuid, offload_codecs_uuid, 16);
	ev.flags = cpu_to_le32(enabled ? BIT(0) : 0);

	return mgmt_limited_event(MGMT_EV_EXP_FEATURE_CHANGED, hdev,
				  &ev, sizeof(ev),
				  HCI_MGMT_EXP_FEATURE_EVENTS, skip);
}

static int set_offload_codec_func(struct sock *sk, struct hci_dev *hdev,
				  struct mgmt_cp_set_exp_feature *cp,
				  u16 data_len)
{
	bool val, changed;
	int err;
	struct mgmt_rp_set_exp_feature rp;

	/* Command requires to use a valid controller index */
	if (!hdev)
		return mgmt_cmd_status(sk, MGMT_INDEX_NONE,
				       MGMT_OP_SET_EXP_FEATURE,
				       MGMT_STATUS_INVALID_INDEX);

	/* Parameters are limited to a single octet */
	if (data_len != MGMT_SET_EXP_FEATURE_SIZE + 1)
		return mgmt_cmd_status(sk, hdev->id,
				       MGMT_OP_SET_EXP_FEATURE,
				       MGMT_STATUS_INVALID_PARAMS);

	/* Only boolean on/off is supported */
	if (cp->param[0] != 0x00 && cp->param[0] != 0x01)
		return mgmt_cmd_status(sk, hdev->id,
				       MGMT_OP_SET_EXP_FEATURE,
				       MGMT_STATUS_INVALID_PARAMS);

	val = !!cp->param[0];
	changed = (val != hci_dev_test_flag(hdev, HCI_OFFLOAD_CODECS_ENABLED));

	if (!hdev->get_data_path_id) {
		return mgmt_cmd_status(sk, hdev->id,
				       MGMT_OP_SET_EXP_FEATURE,
				       MGMT_STATUS_NOT_SUPPORTED);
	}

	if (changed) {
		if (val)
			hci_dev_set_flag(hdev, HCI_OFFLOAD_CODECS_ENABLED);
		else
			hci_dev_clear_flag(hdev, HCI_OFFLOAD_CODECS_ENABLED);
	}

	bt_dev_info(hdev, "offload codecs enable %d changed %d",
		    val, changed);

	memcpy(rp.uuid, offload_codecs_uuid, 16);
	rp.flags = cpu_to_le32(val ? BIT(0) : 0);
	hci_sock_set_flag(sk, HCI_MGMT_EXP_FEATURE_EVENTS);
	err = mgmt_cmd_complete(sk, hdev->id,
				MGMT_OP_SET_EXP_FEATURE, 0,
				&rp, sizeof(rp));

	if (changed)
		exp_offload_codec_feature_changed(val, hdev, sk);

	return err;
}

static const struct mgmt_exp_feature {
	const u8 *uuid;
	int (*set_func)(struct sock *sk, struct hci_dev *hdev,
			struct mgmt_cp_set_exp_feature *cp, u16 data_len);
} exp_features[] = {
	EXP_FEAT(ZERO_KEY, set_zero_key_func),
#ifdef CONFIG_BT_FEATURE_DEBUG
	EXP_FEAT(debug_uuid, set_debug_func),
#endif
	EXP_FEAT(rpa_resolution_uuid, set_rpa_resolution_func),
	EXP_FEAT(quality_report_uuid, set_quality_report_func),
	EXP_FEAT(offload_codecs_uuid, set_offload_codec_func),

	/* end with a null feature */
	EXP_FEAT(NULL, NULL)
};

static int set_exp_feature(struct sock *sk, struct hci_dev *hdev,
			   void *data, u16 data_len)
{
	struct mgmt_cp_set_exp_feature *cp = data;
	size_t i = 0;

	bt_dev_dbg(hdev, "sock %p", sk);

	for (i = 0; exp_features[i].uuid; i++) {
		if (!memcmp(cp->uuid, exp_features[i].uuid, 16))
			return exp_features[i].set_func(sk, hdev, cp, data_len);
	}

	return mgmt_cmd_status(sk, hdev ? hdev->id : MGMT_INDEX_NONE,
			       MGMT_OP_SET_EXP_FEATURE,
			       MGMT_STATUS_NOT_SUPPORTED);
}

#define SUPPORTED_DEVICE_FLAGS() ((1U << HCI_CONN_FLAG_MAX) - 1)

static int get_device_flags(struct sock *sk, struct hci_dev *hdev, void *data,
			    u16 data_len)
{
	struct mgmt_cp_get_device_flags *cp = data;
	struct mgmt_rp_get_device_flags rp;
	struct bdaddr_list_with_flags *br_params;
	struct hci_conn_params *params;
	u32 supported_flags = SUPPORTED_DEVICE_FLAGS();
	u32 current_flags = 0;
	u8 status = MGMT_STATUS_INVALID_PARAMS;

	bt_dev_dbg(hdev, "Get device flags %pMR (type 0x%x)\n",
		   &cp->addr.bdaddr, cp->addr.type);

	hci_dev_lock(hdev);

	memset(&rp, 0, sizeof(rp));

	if (cp->addr.type == BDADDR_BREDR) {
		br_params = hci_bdaddr_list_lookup_with_flags(&hdev->accept_list,
							      &cp->addr.bdaddr,
							      cp->addr.type);
		if (!br_params)
			goto done;

		current_flags = br_params->current_flags;
	} else {
		params = hci_conn_params_lookup(hdev, &cp->addr.bdaddr,
						le_addr_type(cp->addr.type));

		if (!params)
			goto done;

		current_flags = params->current_flags;
	}

	bacpy(&rp.addr.bdaddr, &cp->addr.bdaddr);
	rp.addr.type = cp->addr.type;
	rp.supported_flags = cpu_to_le32(supported_flags);
	rp.current_flags = cpu_to_le32(current_flags);

	status = MGMT_STATUS_SUCCESS;

done:
	hci_dev_unlock(hdev);

	return mgmt_cmd_complete(sk, hdev->id, MGMT_OP_GET_DEVICE_FLAGS, status,
				&rp, sizeof(rp));
}

static void device_flags_changed(struct sock *sk, struct hci_dev *hdev,
				 bdaddr_t *bdaddr, u8 bdaddr_type,
				 u32 supported_flags, u32 current_flags)
{
	struct mgmt_ev_device_flags_changed ev;

	bacpy(&ev.addr.bdaddr, bdaddr);
	ev.addr.type = bdaddr_type;
	ev.supported_flags = cpu_to_le32(supported_flags);
	ev.current_flags = cpu_to_le32(current_flags);

	mgmt_event(MGMT_EV_DEVICE_FLAGS_CHANGED, hdev, &ev, sizeof(ev), sk);
}

static int set_device_flags(struct sock *sk, struct hci_dev *hdev, void *data,
			    u16 len)
{
	struct mgmt_cp_set_device_flags *cp = data;
	struct bdaddr_list_with_flags *br_params;
	struct hci_conn_params *params;
	u8 status = MGMT_STATUS_INVALID_PARAMS;
	u32 supported_flags = SUPPORTED_DEVICE_FLAGS();
	u32 current_flags = __le32_to_cpu(cp->current_flags);

	bt_dev_dbg(hdev, "Set device flags %pMR (type 0x%x) = 0x%x",
		   &cp->addr.bdaddr, cp->addr.type,
		   __le32_to_cpu(current_flags));

	if ((supported_flags | current_flags) != supported_flags) {
		bt_dev_warn(hdev, "Bad flag given (0x%x) vs supported (0x%0x)",
			    current_flags, supported_flags);
		goto done;
	}

	hci_dev_lock(hdev);

	if (cp->addr.type == BDADDR_BREDR) {
		br_params = hci_bdaddr_list_lookup_with_flags(&hdev->accept_list,
							      &cp->addr.bdaddr,
							      cp->addr.type);

		if (br_params) {
			br_params->current_flags = current_flags;
			status = MGMT_STATUS_SUCCESS;
		} else {
			bt_dev_warn(hdev, "No such BR/EDR device %pMR (0x%x)",
				    &cp->addr.bdaddr, cp->addr.type);
		}
	} else {
		params = hci_conn_params_lookup(hdev, &cp->addr.bdaddr,
						le_addr_type(cp->addr.type));
		if (params) {
			params->current_flags = current_flags;
			status = MGMT_STATUS_SUCCESS;
		} else {
			bt_dev_warn(hdev, "No such LE device %pMR (0x%x)",
				    &cp->addr.bdaddr,
				    le_addr_type(cp->addr.type));
		}
	}

done:
	hci_dev_unlock(hdev);

	if (status == MGMT_STATUS_SUCCESS)
		device_flags_changed(sk, hdev, &cp->addr.bdaddr, cp->addr.type,
				     supported_flags, current_flags);

	return mgmt_cmd_complete(sk, hdev->id, MGMT_OP_SET_DEVICE_FLAGS, status,
				 &cp->addr, sizeof(cp->addr));
}

static void mgmt_adv_monitor_added(struct sock *sk, struct hci_dev *hdev,
				   u16 handle)
{
	struct mgmt_ev_adv_monitor_added ev;

	ev.monitor_handle = cpu_to_le16(handle);

	mgmt_event(MGMT_EV_ADV_MONITOR_ADDED, hdev, &ev, sizeof(ev), sk);
}

void mgmt_adv_monitor_removed(struct hci_dev *hdev, u16 handle)
{
	struct mgmt_ev_adv_monitor_removed ev;
	struct mgmt_pending_cmd *cmd;
	struct sock *sk_skip = NULL;
	struct mgmt_cp_remove_adv_monitor *cp;

	cmd = pending_find(MGMT_OP_REMOVE_ADV_MONITOR, hdev);
	if (cmd) {
		cp = cmd->param;

		if (cp->monitor_handle)
			sk_skip = cmd->sk;
	}

	ev.monitor_handle = cpu_to_le16(handle);

	mgmt_event(MGMT_EV_ADV_MONITOR_REMOVED, hdev, &ev, sizeof(ev), sk_skip);
}

static int read_adv_mon_features(struct sock *sk, struct hci_dev *hdev,
				 void *data, u16 len)
{
	struct adv_monitor *monitor = NULL;
	struct mgmt_rp_read_adv_monitor_features *rp = NULL;
	int handle, err;
	size_t rp_size = 0;
	__u32 supported = 0;
	__u32 enabled = 0;
	__u16 num_handles = 0;
	__u16 handles[HCI_MAX_ADV_MONITOR_NUM_HANDLES];

	BT_DBG("request for %s", hdev->name);

	hci_dev_lock(hdev);

	if (msft_monitor_supported(hdev))
		supported |= MGMT_ADV_MONITOR_FEATURE_MASK_OR_PATTERNS;

	idr_for_each_entry(&hdev->adv_monitors_idr, monitor, handle)
		handles[num_handles++] = monitor->handle;

	hci_dev_unlock(hdev);

	rp_size = sizeof(*rp) + (num_handles * sizeof(u16));
	rp = kmalloc(rp_size, GFP_KERNEL);
	if (!rp)
		return -ENOMEM;

	/* All supported features are currently enabled */
	enabled = supported;

	rp->supported_features = cpu_to_le32(supported);
	rp->enabled_features = cpu_to_le32(enabled);
	rp->max_num_handles = cpu_to_le16(HCI_MAX_ADV_MONITOR_NUM_HANDLES);
	rp->max_num_patterns = HCI_MAX_ADV_MONITOR_NUM_PATTERNS;
	rp->num_handles = cpu_to_le16(num_handles);
	if (num_handles)
		memcpy(&rp->handles, &handles, (num_handles * sizeof(u16)));

	err = mgmt_cmd_complete(sk, hdev->id,
				MGMT_OP_READ_ADV_MONITOR_FEATURES,
				MGMT_STATUS_SUCCESS, rp, rp_size);

	kfree(rp);

	return err;
}

int mgmt_add_adv_patterns_monitor_complete(struct hci_dev *hdev, u8 status)
{
	struct mgmt_rp_add_adv_patterns_monitor rp;
	struct mgmt_pending_cmd *cmd;
	struct adv_monitor *monitor;
	int err = 0;

	hci_dev_lock(hdev);

	cmd = pending_find(MGMT_OP_ADD_ADV_PATTERNS_MONITOR_RSSI, hdev);
	if (!cmd) {
		cmd = pending_find(MGMT_OP_ADD_ADV_PATTERNS_MONITOR, hdev);
		if (!cmd)
			goto done;
	}

	monitor = cmd->user_data;
	rp.monitor_handle = cpu_to_le16(monitor->handle);

	if (!status) {
		mgmt_adv_monitor_added(cmd->sk, hdev, monitor->handle);
		hdev->adv_monitors_cnt++;
		if (monitor->state == ADV_MONITOR_STATE_NOT_REGISTERED)
			monitor->state = ADV_MONITOR_STATE_REGISTERED;
		hci_update_passive_scan(hdev);
	}

	err = mgmt_cmd_complete(cmd->sk, cmd->index, cmd->opcode,
				mgmt_status(status), &rp, sizeof(rp));
	mgmt_pending_remove(cmd);

done:
	hci_dev_unlock(hdev);
	bt_dev_dbg(hdev, "add monitor %d complete, status %u",
		   rp.monitor_handle, status);

	return err;
}

static int __add_adv_patterns_monitor(struct sock *sk, struct hci_dev *hdev,
				      struct adv_monitor *m, u8 status,
				      void *data, u16 len, u16 op)
{
	struct mgmt_rp_add_adv_patterns_monitor rp;
	struct mgmt_pending_cmd *cmd;
	int err;
	bool pending;

	hci_dev_lock(hdev);

	if (status)
		goto unlock;

	if (pending_find(MGMT_OP_SET_LE, hdev) ||
	    pending_find(MGMT_OP_ADD_ADV_PATTERNS_MONITOR, hdev) ||
	    pending_find(MGMT_OP_ADD_ADV_PATTERNS_MONITOR_RSSI, hdev) ||
	    pending_find(MGMT_OP_REMOVE_ADV_MONITOR, hdev)) {
		status = MGMT_STATUS_BUSY;
		goto unlock;
	}

	cmd = mgmt_pending_add(sk, op, hdev, data, len);
	if (!cmd) {
		status = MGMT_STATUS_NO_RESOURCES;
		goto unlock;
	}

	cmd->user_data = m;
	pending = hci_add_adv_monitor(hdev, m, &err);
	if (err) {
		if (err == -ENOSPC || err == -ENOMEM)
			status = MGMT_STATUS_NO_RESOURCES;
		else if (err == -EINVAL)
			status = MGMT_STATUS_INVALID_PARAMS;
		else
			status = MGMT_STATUS_FAILED;

		mgmt_pending_remove(cmd);
		goto unlock;
	}

	if (!pending) {
		mgmt_pending_remove(cmd);
		rp.monitor_handle = cpu_to_le16(m->handle);
		mgmt_adv_monitor_added(sk, hdev, m->handle);
		m->state = ADV_MONITOR_STATE_REGISTERED;
		hdev->adv_monitors_cnt++;

		hci_dev_unlock(hdev);
		return mgmt_cmd_complete(sk, hdev->id, op, MGMT_STATUS_SUCCESS,
					 &rp, sizeof(rp));
	}

	hci_dev_unlock(hdev);

	return 0;

unlock:
	hci_free_adv_monitor(hdev, m);
	hci_dev_unlock(hdev);
	return mgmt_cmd_status(sk, hdev->id, op, status);
}

static void parse_adv_monitor_rssi(struct adv_monitor *m,
				   struct mgmt_adv_rssi_thresholds *rssi)
{
	if (rssi) {
		m->rssi.low_threshold = rssi->low_threshold;
		m->rssi.low_threshold_timeout =
		    __le16_to_cpu(rssi->low_threshold_timeout);
		m->rssi.high_threshold = rssi->high_threshold;
		m->rssi.high_threshold_timeout =
		    __le16_to_cpu(rssi->high_threshold_timeout);
		m->rssi.sampling_period = rssi->sampling_period;
	} else {
		/* Default values. These numbers are the least constricting
		 * parameters for MSFT API to work, so it behaves as if there
		 * are no rssi parameter to consider. May need to be changed
		 * if other API are to be supported.
		 */
		m->rssi.low_threshold = -127;
		m->rssi.low_threshold_timeout = 60;
		m->rssi.high_threshold = -127;
		m->rssi.high_threshold_timeout = 0;
		m->rssi.sampling_period = 0;
	}
}

static u8 parse_adv_monitor_pattern(struct adv_monitor *m, u8 pattern_count,
				    struct mgmt_adv_pattern *patterns)
{
	u8 offset = 0, length = 0;
	struct adv_pattern *p = NULL;
	int i;

	for (i = 0; i < pattern_count; i++) {
		offset = patterns[i].offset;
		length = patterns[i].length;
		if (offset >= HCI_MAX_AD_LENGTH ||
		    length > HCI_MAX_AD_LENGTH ||
		    (offset + length) > HCI_MAX_AD_LENGTH)
			return MGMT_STATUS_INVALID_PARAMS;

		p = kmalloc(sizeof(*p), GFP_KERNEL);
		if (!p)
			return MGMT_STATUS_NO_RESOURCES;

		p->ad_type = patterns[i].ad_type;
		p->offset = patterns[i].offset;
		p->length = patterns[i].length;
		memcpy(p->value, patterns[i].value, p->length);

		INIT_LIST_HEAD(&p->list);
		list_add(&p->list, &m->patterns);
	}

	return MGMT_STATUS_SUCCESS;
}

static int add_adv_patterns_monitor(struct sock *sk, struct hci_dev *hdev,
				    void *data, u16 len)
{
	struct mgmt_cp_add_adv_patterns_monitor *cp = data;
	struct adv_monitor *m = NULL;
	u8 status = MGMT_STATUS_SUCCESS;
	size_t expected_size = sizeof(*cp);

	BT_DBG("request for %s", hdev->name);

	if (len <= sizeof(*cp)) {
		status = MGMT_STATUS_INVALID_PARAMS;
		goto done;
	}

	expected_size += cp->pattern_count * sizeof(struct mgmt_adv_pattern);
	if (len != expected_size) {
		status = MGMT_STATUS_INVALID_PARAMS;
		goto done;
	}

	m = kzalloc(sizeof(*m), GFP_KERNEL);
	if (!m) {
		status = MGMT_STATUS_NO_RESOURCES;
		goto done;
	}

	INIT_LIST_HEAD(&m->patterns);

	parse_adv_monitor_rssi(m, NULL);
	status = parse_adv_monitor_pattern(m, cp->pattern_count, cp->patterns);

done:
	return __add_adv_patterns_monitor(sk, hdev, m, status, data, len,
					  MGMT_OP_ADD_ADV_PATTERNS_MONITOR);
}

static int add_adv_patterns_monitor_rssi(struct sock *sk, struct hci_dev *hdev,
					 void *data, u16 len)
{
	struct mgmt_cp_add_adv_patterns_monitor_rssi *cp = data;
	struct adv_monitor *m = NULL;
	u8 status = MGMT_STATUS_SUCCESS;
	size_t expected_size = sizeof(*cp);

	BT_DBG("request for %s", hdev->name);

	if (len <= sizeof(*cp)) {
		status = MGMT_STATUS_INVALID_PARAMS;
		goto done;
	}

	expected_size += cp->pattern_count * sizeof(struct mgmt_adv_pattern);
	if (len != expected_size) {
		status = MGMT_STATUS_INVALID_PARAMS;
		goto done;
	}

	m = kzalloc(sizeof(*m), GFP_KERNEL);
	if (!m) {
		status = MGMT_STATUS_NO_RESOURCES;
		goto done;
	}

	INIT_LIST_HEAD(&m->patterns);

	parse_adv_monitor_rssi(m, &cp->rssi);
	status = parse_adv_monitor_pattern(m, cp->pattern_count, cp->patterns);

done:
	return __add_adv_patterns_monitor(sk, hdev, m, status, data, len,
					 MGMT_OP_ADD_ADV_PATTERNS_MONITOR_RSSI);
}

int mgmt_remove_adv_monitor_complete(struct hci_dev *hdev, u8 status)
{
	struct mgmt_rp_remove_adv_monitor rp;
	struct mgmt_cp_remove_adv_monitor *cp;
	struct mgmt_pending_cmd *cmd;
	int err = 0;

	hci_dev_lock(hdev);

	cmd = pending_find(MGMT_OP_REMOVE_ADV_MONITOR, hdev);
	if (!cmd)
		goto done;

	cp = cmd->param;
	rp.monitor_handle = cp->monitor_handle;

	if (!status)
		hci_update_passive_scan(hdev);

	err = mgmt_cmd_complete(cmd->sk, cmd->index, cmd->opcode,
				mgmt_status(status), &rp, sizeof(rp));
	mgmt_pending_remove(cmd);

done:
	hci_dev_unlock(hdev);
	bt_dev_dbg(hdev, "remove monitor %d complete, status %u",
		   rp.monitor_handle, status);

	return err;
}

static int remove_adv_monitor(struct sock *sk, struct hci_dev *hdev,
			      void *data, u16 len)
{
	struct mgmt_cp_remove_adv_monitor *cp = data;
	struct mgmt_rp_remove_adv_monitor rp;
	struct mgmt_pending_cmd *cmd;
	u16 handle = __le16_to_cpu(cp->monitor_handle);
	int err, status;
	bool pending;

	BT_DBG("request for %s", hdev->name);
	rp.monitor_handle = cp->monitor_handle;

	hci_dev_lock(hdev);

	if (pending_find(MGMT_OP_SET_LE, hdev) ||
	    pending_find(MGMT_OP_REMOVE_ADV_MONITOR, hdev) ||
	    pending_find(MGMT_OP_ADD_ADV_PATTERNS_MONITOR, hdev) ||
	    pending_find(MGMT_OP_ADD_ADV_PATTERNS_MONITOR_RSSI, hdev)) {
		status = MGMT_STATUS_BUSY;
		goto unlock;
	}

	cmd = mgmt_pending_add(sk, MGMT_OP_REMOVE_ADV_MONITOR, hdev, data, len);
	if (!cmd) {
		status = MGMT_STATUS_NO_RESOURCES;
		goto unlock;
	}

	if (handle)
		pending = hci_remove_single_adv_monitor(hdev, handle, &err);
	else
		pending = hci_remove_all_adv_monitor(hdev, &err);

	if (err) {
		mgmt_pending_remove(cmd);

		if (err == -ENOENT)
			status = MGMT_STATUS_INVALID_INDEX;
		else
			status = MGMT_STATUS_FAILED;

		goto unlock;
	}

	/* monitor can be removed without forwarding request to controller */
	if (!pending) {
		mgmt_pending_remove(cmd);
		hci_dev_unlock(hdev);

		return mgmt_cmd_complete(sk, hdev->id,
					 MGMT_OP_REMOVE_ADV_MONITOR,
					 MGMT_STATUS_SUCCESS,
					 &rp, sizeof(rp));
	}

	hci_dev_unlock(hdev);
	return 0;

unlock:
	hci_dev_unlock(hdev);
	return mgmt_cmd_status(sk, hdev->id, MGMT_OP_REMOVE_ADV_MONITOR,
			       status);
}

static void read_local_oob_data_complete(struct hci_dev *hdev, void *data, int err)
{
	struct mgmt_rp_read_local_oob_data mgmt_rp;
	size_t rp_size = sizeof(mgmt_rp);
	struct mgmt_pending_cmd *cmd = data;
	struct sk_buff *skb = cmd->skb;
	u8 status = mgmt_status(err);

	if (!status) {
		if (!skb)
			status = MGMT_STATUS_FAILED;
		else if (IS_ERR(skb))
			status = mgmt_status(PTR_ERR(skb));
		else
			status = mgmt_status(skb->data[0]);
	}

	bt_dev_dbg(hdev, "status %d", status);

	if (status) {
		mgmt_cmd_status(cmd->sk, hdev->id, MGMT_OP_READ_LOCAL_OOB_DATA, status);
		goto remove;
	}

	memset(&mgmt_rp, 0, sizeof(mgmt_rp));

	if (!bredr_sc_enabled(hdev)) {
		struct hci_rp_read_local_oob_data *rp = (void *) skb->data;

		if (skb->len < sizeof(*rp)) {
			mgmt_cmd_status(cmd->sk, hdev->id,
					MGMT_OP_READ_LOCAL_OOB_DATA,
					MGMT_STATUS_FAILED);
			goto remove;
		}

		memcpy(mgmt_rp.hash192, rp->hash, sizeof(rp->hash));
		memcpy(mgmt_rp.rand192, rp->rand, sizeof(rp->rand));

		rp_size -= sizeof(mgmt_rp.hash256) + sizeof(mgmt_rp.rand256);
	} else {
		struct hci_rp_read_local_oob_ext_data *rp = (void *) skb->data;

		if (skb->len < sizeof(*rp)) {
			mgmt_cmd_status(cmd->sk, hdev->id,
					MGMT_OP_READ_LOCAL_OOB_DATA,
					MGMT_STATUS_FAILED);
			goto remove;
		}

		memcpy(mgmt_rp.hash192, rp->hash192, sizeof(rp->hash192));
		memcpy(mgmt_rp.rand192, rp->rand192, sizeof(rp->rand192));

		memcpy(mgmt_rp.hash256, rp->hash256, sizeof(rp->hash256));
		memcpy(mgmt_rp.rand256, rp->rand256, sizeof(rp->rand256));
	}

	mgmt_cmd_complete(cmd->sk, hdev->id, MGMT_OP_READ_LOCAL_OOB_DATA,
			  MGMT_STATUS_SUCCESS, &mgmt_rp, rp_size);

remove:
	if (skb && !IS_ERR(skb))
		kfree_skb(skb);

	mgmt_pending_free(cmd);
}

static int read_local_oob_data_sync(struct hci_dev *hdev, void *data)
{
	struct mgmt_pending_cmd *cmd = data;

	if (bredr_sc_enabled(hdev))
		cmd->skb = hci_read_local_oob_data_sync(hdev, true, cmd->sk);
	else
		cmd->skb = hci_read_local_oob_data_sync(hdev, false, cmd->sk);

	if (IS_ERR(cmd->skb))
		return PTR_ERR(cmd->skb);
	else
		return 0;
}

static int read_local_oob_data(struct sock *sk, struct hci_dev *hdev,
			       void *data, u16 data_len)
{
	struct mgmt_pending_cmd *cmd;
	int err;

	bt_dev_dbg(hdev, "sock %p", sk);

	hci_dev_lock(hdev);

	if (!hdev_is_powered(hdev)) {
		err = mgmt_cmd_status(sk, hdev->id, MGMT_OP_READ_LOCAL_OOB_DATA,
				      MGMT_STATUS_NOT_POWERED);
		goto unlock;
	}

	if (!lmp_ssp_capable(hdev)) {
		err = mgmt_cmd_status(sk, hdev->id, MGMT_OP_READ_LOCAL_OOB_DATA,
				      MGMT_STATUS_NOT_SUPPORTED);
		goto unlock;
	}

	if (pending_find(MGMT_OP_READ_LOCAL_OOB_DATA, hdev)) {
		err = mgmt_cmd_status(sk, hdev->id, MGMT_OP_READ_LOCAL_OOB_DATA,
				      MGMT_STATUS_BUSY);
		goto unlock;
	}

	cmd = mgmt_pending_new(sk, MGMT_OP_READ_LOCAL_OOB_DATA, hdev, NULL, 0);
	if (!cmd)
		err = -ENOMEM;
	else
		err = hci_cmd_sync_queue(hdev, read_local_oob_data_sync, cmd,
					 read_local_oob_data_complete);

	if (err < 0) {
		err = mgmt_cmd_status(sk, hdev->id, MGMT_OP_READ_LOCAL_OOB_DATA,
				      MGMT_STATUS_FAILED);

		if (cmd)
			mgmt_pending_free(cmd);
	}

unlock:
	hci_dev_unlock(hdev);
	return err;
}

static int add_remote_oob_data(struct sock *sk, struct hci_dev *hdev,
			       void *data, u16 len)
{
	struct mgmt_addr_info *addr = data;
	int err;

	bt_dev_dbg(hdev, "sock %p", sk);

	if (!bdaddr_type_is_valid(addr->type))
		return mgmt_cmd_complete(sk, hdev->id,
					 MGMT_OP_ADD_REMOTE_OOB_DATA,
					 MGMT_STATUS_INVALID_PARAMS,
					 addr, sizeof(*addr));

	hci_dev_lock(hdev);

	if (len == MGMT_ADD_REMOTE_OOB_DATA_SIZE) {
		struct mgmt_cp_add_remote_oob_data *cp = data;
		u8 status;

		if (cp->addr.type != BDADDR_BREDR) {
			err = mgmt_cmd_complete(sk, hdev->id,
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

		err = mgmt_cmd_complete(sk, hdev->id,
					MGMT_OP_ADD_REMOTE_OOB_DATA, status,
					&cp->addr, sizeof(cp->addr));
	} else if (len == MGMT_ADD_REMOTE_OOB_EXT_DATA_SIZE) {
		struct mgmt_cp_add_remote_oob_ext_data *cp = data;
		u8 *rand192, *hash192, *rand256, *hash256;
		u8 status;

		if (bdaddr_type_is_le(cp->addr.type)) {
			/* Enforce zero-valued 192-bit parameters as
			 * long as legacy SMP OOB isn't implemented.
			 */
			if (memcmp(cp->rand192, ZERO_KEY, 16) ||
			    memcmp(cp->hash192, ZERO_KEY, 16)) {
				err = mgmt_cmd_complete(sk, hdev->id,
							MGMT_OP_ADD_REMOTE_OOB_DATA,
							MGMT_STATUS_INVALID_PARAMS,
							addr, sizeof(*addr));
				goto unlock;
			}

			rand192 = NULL;
			hash192 = NULL;
		} else {
			/* In case one of the P-192 values is set to zero,
			 * then just disable OOB data for P-192.
			 */
			if (!memcmp(cp->rand192, ZERO_KEY, 16) ||
			    !memcmp(cp->hash192, ZERO_KEY, 16)) {
				rand192 = NULL;
				hash192 = NULL;
			} else {
				rand192 = cp->rand192;
				hash192 = cp->hash192;
			}
		}

		/* In case one of the P-256 values is set to zero, then just
		 * disable OOB data for P-256.
		 */
		if (!memcmp(cp->rand256, ZERO_KEY, 16) ||
		    !memcmp(cp->hash256, ZERO_KEY, 16)) {
			rand256 = NULL;
			hash256 = NULL;
		} else {
			rand256 = cp->rand256;
			hash256 = cp->hash256;
		}

		err = hci_add_remote_oob_data(hdev, &cp->addr.bdaddr,
					      cp->addr.type, hash192, rand192,
					      hash256, rand256);
		if (err < 0)
			status = MGMT_STATUS_FAILED;
		else
			status = MGMT_STATUS_SUCCESS;

		err = mgmt_cmd_complete(sk, hdev->id,
					MGMT_OP_ADD_REMOTE_OOB_DATA,
					status, &cp->addr, sizeof(cp->addr));
	} else {
		bt_dev_err(hdev, "add_remote_oob_data: invalid len of %u bytes",
			   len);
		err = mgmt_cmd_status(sk, hdev->id, MGMT_OP_ADD_REMOTE_OOB_DATA,
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

	bt_dev_dbg(hdev, "sock %p", sk);

	if (cp->addr.type != BDADDR_BREDR)
		return mgmt_cmd_complete(sk, hdev->id,
					 MGMT_OP_REMOVE_REMOTE_OOB_DATA,
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
	err = mgmt_cmd_complete(sk, hdev->id, MGMT_OP_REMOVE_REMOTE_OOB_DATA,
				status, &cp->addr, sizeof(cp->addr));

	hci_dev_unlock(hdev);
	return err;
}

void mgmt_start_discovery_complete(struct hci_dev *hdev, u8 status)
{
	struct mgmt_pending_cmd *cmd;

	bt_dev_dbg(hdev, "status %u", status);

	hci_dev_lock(hdev);

	cmd = pending_find(MGMT_OP_START_DISCOVERY, hdev);
	if (!cmd)
		cmd = pending_find(MGMT_OP_START_SERVICE_DISCOVERY, hdev);

	if (!cmd)
		cmd = pending_find(MGMT_OP_START_LIMITED_DISCOVERY, hdev);

	if (cmd) {
		cmd->cmd_complete(cmd, mgmt_status(status));
		mgmt_pending_remove(cmd);
	}

	hci_dev_unlock(hdev);
}

static bool discovery_type_is_valid(struct hci_dev *hdev, uint8_t type,
				    uint8_t *mgmt_status)
{
	switch (type) {
	case DISCOV_TYPE_LE:
		*mgmt_status = mgmt_le_support(hdev);
		if (*mgmt_status)
			return false;
		break;
	case DISCOV_TYPE_INTERLEAVED:
		*mgmt_status = mgmt_le_support(hdev);
		if (*mgmt_status)
			return false;
		fallthrough;
	case DISCOV_TYPE_BREDR:
		*mgmt_status = mgmt_bredr_support(hdev);
		if (*mgmt_status)
			return false;
		break;
	default:
		*mgmt_status = MGMT_STATUS_INVALID_PARAMS;
		return false;
	}

	return true;
}

static void start_discovery_complete(struct hci_dev *hdev, void *data, int err)
{
	struct mgmt_pending_cmd *cmd = data;

	bt_dev_dbg(hdev, "err %d", err);

	mgmt_cmd_complete(cmd->sk, cmd->index, cmd->opcode, mgmt_status(err),
			  cmd->param, 1);
	mgmt_pending_free(cmd);

	hci_discovery_set_state(hdev, err ? DISCOVERY_STOPPED:
				DISCOVERY_FINDING);
}

static int start_discovery_sync(struct hci_dev *hdev, void *data)
{
	return hci_start_discovery_sync(hdev);
}

static int start_discovery_internal(struct sock *sk, struct hci_dev *hdev,
				    u16 op, void *data, u16 len)
{
	struct mgmt_cp_start_discovery *cp = data;
	struct mgmt_pending_cmd *cmd;
	u8 status;
	int err;

	bt_dev_dbg(hdev, "sock %p", sk);

	hci_dev_lock(hdev);

	if (!hdev_is_powered(hdev)) {
		err = mgmt_cmd_complete(sk, hdev->id, op,
					MGMT_STATUS_NOT_POWERED,
					&cp->type, sizeof(cp->type));
		goto failed;
	}

	if (hdev->discovery.state != DISCOVERY_STOPPED ||
	    hci_dev_test_flag(hdev, HCI_PERIODIC_INQ)) {
		err = mgmt_cmd_complete(sk, hdev->id, op, MGMT_STATUS_BUSY,
					&cp->type, sizeof(cp->type));
		goto failed;
	}

	if (!discovery_type_is_valid(hdev, cp->type, &status)) {
		err = mgmt_cmd_complete(sk, hdev->id, op, status,
					&cp->type, sizeof(cp->type));
		goto failed;
	}

	/* Can't start discovery when it is paused */
	if (hdev->discovery_paused) {
		err = mgmt_cmd_complete(sk, hdev->id, op, MGMT_STATUS_BUSY,
					&cp->type, sizeof(cp->type));
		goto failed;
	}

	/* Clear the discovery filter first to free any previously
	 * allocated memory for the UUID list.
	 */
	hci_discovery_filter_clear(hdev);

	hdev->discovery.type = cp->type;
	hdev->discovery.report_invalid_rssi = false;
	if (op == MGMT_OP_START_LIMITED_DISCOVERY)
		hdev->discovery.limited = true;
	else
		hdev->discovery.limited = false;

	cmd = mgmt_pending_new(sk, op, hdev, data, len);
	if (!cmd) {
		err = -ENOMEM;
		goto failed;
	}

	err = hci_cmd_sync_queue(hdev, start_discovery_sync, cmd,
				 start_discovery_complete);
	if (err < 0) {
		mgmt_pending_free(cmd);
		goto failed;
	}

	hci_discovery_set_state(hdev, DISCOVERY_STARTING);

failed:
	hci_dev_unlock(hdev);
	return err;
}

static int start_discovery(struct sock *sk, struct hci_dev *hdev,
			   void *data, u16 len)
{
	return start_discovery_internal(sk, hdev, MGMT_OP_START_DISCOVERY,
					data, len);
}

static int start_limited_discovery(struct sock *sk, struct hci_dev *hdev,
				   void *data, u16 len)
{
	return start_discovery_internal(sk, hdev,
					MGMT_OP_START_LIMITED_DISCOVERY,
					data, len);
}

static int start_service_discovery(struct sock *sk, struct hci_dev *hdev,
				   void *data, u16 len)
{
	struct mgmt_cp_start_service_discovery *cp = data;
	struct mgmt_pending_cmd *cmd;
	const u16 max_uuid_count = ((U16_MAX - sizeof(*cp)) / 16);
	u16 uuid_count, expected_len;
	u8 status;
	int err;

	bt_dev_dbg(hdev, "sock %p", sk);

	hci_dev_lock(hdev);

	if (!hdev_is_powered(hdev)) {
		err = mgmt_cmd_complete(sk, hdev->id,
					MGMT_OP_START_SERVICE_DISCOVERY,
					MGMT_STATUS_NOT_POWERED,
					&cp->type, sizeof(cp->type));
		goto failed;
	}

	if (hdev->discovery.state != DISCOVERY_STOPPED ||
	    hci_dev_test_flag(hdev, HCI_PERIODIC_INQ)) {
		err = mgmt_cmd_complete(sk, hdev->id,
					MGMT_OP_START_SERVICE_DISCOVERY,
					MGMT_STATUS_BUSY, &cp->type,
					sizeof(cp->type));
		goto failed;
	}

	if (hdev->discovery_paused) {
		err = mgmt_cmd_complete(sk, hdev->id,
					MGMT_OP_START_SERVICE_DISCOVERY,
					MGMT_STATUS_BUSY, &cp->type,
					sizeof(cp->type));
		goto failed;
	}

	uuid_count = __le16_to_cpu(cp->uuid_count);
	if (uuid_count > max_uuid_count) {
		bt_dev_err(hdev, "service_discovery: too big uuid_count value %u",
			   uuid_count);
		err = mgmt_cmd_complete(sk, hdev->id,
					MGMT_OP_START_SERVICE_DISCOVERY,
					MGMT_STATUS_INVALID_PARAMS, &cp->type,
					sizeof(cp->type));
		goto failed;
	}

	expected_len = sizeof(*cp) + uuid_count * 16;
	if (expected_len != len) {
		bt_dev_err(hdev, "service_discovery: expected %u bytes, got %u bytes",
			   expected_len, len);
		err = mgmt_cmd_complete(sk, hdev->id,
					MGMT_OP_START_SERVICE_DISCOVERY,
					MGMT_STATUS_INVALID_PARAMS, &cp->type,
					sizeof(cp->type));
		goto failed;
	}

	if (!discovery_type_is_valid(hdev, cp->type, &status)) {
		err = mgmt_cmd_complete(sk, hdev->id,
					MGMT_OP_START_SERVICE_DISCOVERY,
					status, &cp->type, sizeof(cp->type));
		goto failed;
	}

	cmd = mgmt_pending_new(sk, MGMT_OP_START_SERVICE_DISCOVERY,
			       hdev, data, len);
	if (!cmd) {
		err = -ENOMEM;
		goto failed;
	}

	/* Clear the discovery filter first to free any previously
	 * allocated memory for the UUID list.
	 */
	hci_discovery_filter_clear(hdev);

	hdev->discovery.result_filtering = true;
	hdev->discovery.type = cp->type;
	hdev->discovery.rssi = cp->rssi;
	hdev->discovery.uuid_count = uuid_count;

	if (uuid_count > 0) {
		hdev->discovery.uuids = kmemdup(cp->uuids, uuid_count * 16,
						GFP_KERNEL);
		if (!hdev->discovery.uuids) {
			err = mgmt_cmd_complete(sk, hdev->id,
						MGMT_OP_START_SERVICE_DISCOVERY,
						MGMT_STATUS_FAILED,
						&cp->type, sizeof(cp->type));
			mgmt_pending_remove(cmd);
			goto failed;
		}
	}

	err = hci_cmd_sync_queue(hdev, start_discovery_sync, cmd,
				 start_discovery_complete);
	if (err < 0) {
		mgmt_pending_free(cmd);
		goto failed;
	}

	hci_discovery_set_state(hdev, DISCOVERY_STARTING);

failed:
	hci_dev_unlock(hdev);
	return err;
}

void mgmt_stop_discovery_complete(struct hci_dev *hdev, u8 status)
{
	struct mgmt_pending_cmd *cmd;

	bt_dev_dbg(hdev, "status %u", status);

	hci_dev_lock(hdev);

	cmd = pending_find(MGMT_OP_STOP_DISCOVERY, hdev);
	if (cmd) {
		cmd->cmd_complete(cmd, mgmt_status(status));
		mgmt_pending_remove(cmd);
	}

	hci_dev_unlock(hdev);
}

static void stop_discovery_complete(struct hci_dev *hdev, void *data, int err)
{
	struct mgmt_pending_cmd *cmd = data;

	bt_dev_dbg(hdev, "err %d", err);

	mgmt_cmd_complete(cmd->sk, cmd->index, cmd->opcode, mgmt_status(err),
			  cmd->param, 1);
	mgmt_pending_free(cmd);

	if (!err)
		hci_discovery_set_state(hdev, DISCOVERY_STOPPED);
}

static int stop_discovery_sync(struct hci_dev *hdev, void *data)
{
	return hci_stop_discovery_sync(hdev);
}

static int stop_discovery(struct sock *sk, struct hci_dev *hdev, void *data,
			  u16 len)
{
	struct mgmt_cp_stop_discovery *mgmt_cp = data;
	struct mgmt_pending_cmd *cmd;
	int err;

	bt_dev_dbg(hdev, "sock %p", sk);

	hci_dev_lock(hdev);

	if (!hci_discovery_active(hdev)) {
		err = mgmt_cmd_complete(sk, hdev->id, MGMT_OP_STOP_DISCOVERY,
					MGMT_STATUS_REJECTED, &mgmt_cp->type,
					sizeof(mgmt_cp->type));
		goto unlock;
	}

	if (hdev->discovery.type != mgmt_cp->type) {
		err = mgmt_cmd_complete(sk, hdev->id, MGMT_OP_STOP_DISCOVERY,
					MGMT_STATUS_INVALID_PARAMS,
					&mgmt_cp->type, sizeof(mgmt_cp->type));
		goto unlock;
	}

	cmd = mgmt_pending_new(sk, MGMT_OP_STOP_DISCOVERY, hdev, data, len);
	if (!cmd) {
		err = -ENOMEM;
		goto unlock;
	}

	err = hci_cmd_sync_queue(hdev, stop_discovery_sync, cmd,
				 stop_discovery_complete);
	if (err < 0) {
		mgmt_pending_free(cmd);
		goto unlock;
	}

	hci_discovery_set_state(hdev, DISCOVERY_STOPPING);

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

	bt_dev_dbg(hdev, "sock %p", sk);

	hci_dev_lock(hdev);

	if (!hci_discovery_active(hdev)) {
		err = mgmt_cmd_complete(sk, hdev->id, MGMT_OP_CONFIRM_NAME,
					MGMT_STATUS_FAILED, &cp->addr,
					sizeof(cp->addr));
		goto failed;
	}

	e = hci_inquiry_cache_lookup_unknown(hdev, &cp->addr.bdaddr);
	if (!e) {
		err = mgmt_cmd_complete(sk, hdev->id, MGMT_OP_CONFIRM_NAME,
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

	err = mgmt_cmd_complete(sk, hdev->id, MGMT_OP_CONFIRM_NAME, 0,
				&cp->addr, sizeof(cp->addr));

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

	bt_dev_dbg(hdev, "sock %p", sk);

	if (!bdaddr_type_is_valid(cp->addr.type))
		return mgmt_cmd_complete(sk, hdev->id, MGMT_OP_BLOCK_DEVICE,
					 MGMT_STATUS_INVALID_PARAMS,
					 &cp->addr, sizeof(cp->addr));

	hci_dev_lock(hdev);

	err = hci_bdaddr_list_add(&hdev->reject_list, &cp->addr.bdaddr,
				  cp->addr.type);
	if (err < 0) {
		status = MGMT_STATUS_FAILED;
		goto done;
	}

	mgmt_event(MGMT_EV_DEVICE_BLOCKED, hdev, &cp->addr, sizeof(cp->addr),
		   sk);
	status = MGMT_STATUS_SUCCESS;

done:
	err = mgmt_cmd_complete(sk, hdev->id, MGMT_OP_BLOCK_DEVICE, status,
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

	bt_dev_dbg(hdev, "sock %p", sk);

	if (!bdaddr_type_is_valid(cp->addr.type))
		return mgmt_cmd_complete(sk, hdev->id, MGMT_OP_UNBLOCK_DEVICE,
					 MGMT_STATUS_INVALID_PARAMS,
					 &cp->addr, sizeof(cp->addr));

	hci_dev_lock(hdev);

	err = hci_bdaddr_list_del(&hdev->reject_list, &cp->addr.bdaddr,
				  cp->addr.type);
	if (err < 0) {
		status = MGMT_STATUS_INVALID_PARAMS;
		goto done;
	}

	mgmt_event(MGMT_EV_DEVICE_UNBLOCKED, hdev, &cp->addr, sizeof(cp->addr),
		   sk);
	status = MGMT_STATUS_SUCCESS;

done:
	err = mgmt_cmd_complete(sk, hdev->id, MGMT_OP_UNBLOCK_DEVICE, status,
				&cp->addr, sizeof(cp->addr));

	hci_dev_unlock(hdev);

	return err;
}

static int set_device_id_sync(struct hci_dev *hdev, void *data)
{
	return hci_update_eir_sync(hdev);
}

static int set_device_id(struct sock *sk, struct hci_dev *hdev, void *data,
			 u16 len)
{
	struct mgmt_cp_set_device_id *cp = data;
	int err;
	__u16 source;

	bt_dev_dbg(hdev, "sock %p", sk);

	source = __le16_to_cpu(cp->source);

	if (source > 0x0002)
		return mgmt_cmd_status(sk, hdev->id, MGMT_OP_SET_DEVICE_ID,
				       MGMT_STATUS_INVALID_PARAMS);

	hci_dev_lock(hdev);

	hdev->devid_source = source;
	hdev->devid_vendor = __le16_to_cpu(cp->vendor);
	hdev->devid_product = __le16_to_cpu(cp->product);
	hdev->devid_version = __le16_to_cpu(cp->version);

	err = mgmt_cmd_complete(sk, hdev->id, MGMT_OP_SET_DEVICE_ID, 0,
				NULL, 0);

	hci_cmd_sync_queue(hdev, set_device_id_sync, NULL, NULL);

	hci_dev_unlock(hdev);

	return err;
}

static void enable_advertising_instance(struct hci_dev *hdev, int err)
{
	if (err)
		bt_dev_err(hdev, "failed to re-configure advertising %d", err);
	else
		bt_dev_dbg(hdev, "status %d", err);
}

static void set_advertising_complete(struct hci_dev *hdev, void *data, int err)
{
	struct cmd_lookup match = { NULL, hdev };
	u8 instance;
	struct adv_info *adv_instance;
	u8 status = mgmt_status(err);

	if (status) {
		mgmt_pending_foreach(MGMT_OP_SET_ADVERTISING, hdev,
				     cmd_status_rsp, &status);
		return;
	}

	if (hci_dev_test_flag(hdev, HCI_LE_ADV))
		hci_dev_set_flag(hdev, HCI_ADVERTISING);
	else
		hci_dev_clear_flag(hdev, HCI_ADVERTISING);

	mgmt_pending_foreach(MGMT_OP_SET_ADVERTISING, hdev, settings_rsp,
			     &match);

	new_settings(hdev, match.sk);

	if (match.sk)
		sock_put(match.sk);

	/* If "Set Advertising" was just disabled and instance advertising was
	 * set up earlier, then re-enable multi-instance advertising.
	 */
	if (hci_dev_test_flag(hdev, HCI_ADVERTISING) ||
	    list_empty(&hdev->adv_instances))
		return;

	instance = hdev->cur_adv_instance;
	if (!instance) {
		adv_instance = list_first_entry_or_null(&hdev->adv_instances,
							struct adv_info, list);
		if (!adv_instance)
			return;

		instance = adv_instance->instance;
	}

	err = hci_schedule_adv_instance_sync(hdev, instance, true);

	enable_advertising_instance(hdev, err);
}

static int set_adv_sync(struct hci_dev *hdev, void *data)
{
	struct mgmt_pending_cmd *cmd = data;
	struct mgmt_mode *cp = cmd->param;
	u8 val = !!cp->val;

	if (cp->val == 0x02)
		hci_dev_set_flag(hdev, HCI_ADVERTISING_CONNECTABLE);
	else
		hci_dev_clear_flag(hdev, HCI_ADVERTISING_CONNECTABLE);

	cancel_adv_timeout(hdev);

	if (val) {
		/* Switch to instance "0" for the Set Advertising setting.
		 * We cannot use update_[adv|scan_rsp]_data() here as the
		 * HCI_ADVERTISING flag is not yet set.
		 */
		hdev->cur_adv_instance = 0x00;

		if (ext_adv_capable(hdev)) {
			hci_start_ext_adv_sync(hdev, 0x00);
		} else {
			hci_update_adv_data_sync(hdev, 0x00);
			hci_update_scan_rsp_data_sync(hdev, 0x00);
			hci_enable_advertising_sync(hdev);
		}
	} else {
		hci_disable_advertising_sync(hdev);
	}

	return 0;
}

static int set_advertising(struct sock *sk, struct hci_dev *hdev, void *data,
			   u16 len)
{
	struct mgmt_mode *cp = data;
	struct mgmt_pending_cmd *cmd;
	u8 val, status;
	int err;

	bt_dev_dbg(hdev, "sock %p", sk);

	status = mgmt_le_support(hdev);
	if (status)
		return mgmt_cmd_status(sk, hdev->id, MGMT_OP_SET_ADVERTISING,
				       status);

	if (cp->val != 0x00 && cp->val != 0x01 && cp->val != 0x02)
		return mgmt_cmd_status(sk, hdev->id, MGMT_OP_SET_ADVERTISING,
				       MGMT_STATUS_INVALID_PARAMS);

	if (hdev->advertising_paused)
		return mgmt_cmd_status(sk, hdev->id, MGMT_OP_SET_ADVERTISING,
				       MGMT_STATUS_BUSY);

	hci_dev_lock(hdev);

	val = !!cp->val;

	/* The following conditions are ones which mean that we should
	 * not do any HCI communication but directly send a mgmt
	 * response to user space (after toggling the flag if
	 * necessary).
	 */
	if (!hdev_is_powered(hdev) ||
	    (val == hci_dev_test_flag(hdev, HCI_ADVERTISING) &&
	     (cp->val == 0x02) == hci_dev_test_flag(hdev, HCI_ADVERTISING_CONNECTABLE)) ||
	    hci_conn_num(hdev, LE_LINK) > 0 ||
	    (hci_dev_test_flag(hdev, HCI_LE_SCAN) &&
	     hdev->le_scan_type == LE_SCAN_ACTIVE)) {
		bool changed;

		if (cp->val) {
			hdev->cur_adv_instance = 0x00;
			changed = !hci_dev_test_and_set_flag(hdev, HCI_ADVERTISING);
			if (cp->val == 0x02)
				hci_dev_set_flag(hdev, HCI_ADVERTISING_CONNECTABLE);
			else
				hci_dev_clear_flag(hdev, HCI_ADVERTISING_CONNECTABLE);
		} else {
			changed = hci_dev_test_and_clear_flag(hdev, HCI_ADVERTISING);
			hci_dev_clear_flag(hdev, HCI_ADVERTISING_CONNECTABLE);
		}

		err = send_settings_rsp(sk, MGMT_OP_SET_ADVERTISING, hdev);
		if (err < 0)
			goto unlock;

		if (changed)
			err = new_settings(hdev, sk);

		goto unlock;
	}

	if (pending_find(MGMT_OP_SET_ADVERTISING, hdev) ||
	    pending_find(MGMT_OP_SET_LE, hdev)) {
		err = mgmt_cmd_status(sk, hdev->id, MGMT_OP_SET_ADVERTISING,
				      MGMT_STATUS_BUSY);
		goto unlock;
	}

	cmd = mgmt_pending_add(sk, MGMT_OP_SET_ADVERTISING, hdev, data, len);
	if (!cmd)
		err = -ENOMEM;
	else
		err = hci_cmd_sync_queue(hdev, set_adv_sync, cmd,
					 set_advertising_complete);

	if (err < 0 && cmd)
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

	bt_dev_dbg(hdev, "sock %p", sk);

	if (!lmp_le_capable(hdev))
		return mgmt_cmd_status(sk, hdev->id, MGMT_OP_SET_STATIC_ADDRESS,
				       MGMT_STATUS_NOT_SUPPORTED);

	if (hdev_is_powered(hdev))
		return mgmt_cmd_status(sk, hdev->id, MGMT_OP_SET_STATIC_ADDRESS,
				       MGMT_STATUS_REJECTED);

	if (bacmp(&cp->bdaddr, BDADDR_ANY)) {
		if (!bacmp(&cp->bdaddr, BDADDR_NONE))
			return mgmt_cmd_status(sk, hdev->id,
					       MGMT_OP_SET_STATIC_ADDRESS,
					       MGMT_STATUS_INVALID_PARAMS);

		/* Two most significant bits shall be set */
		if ((cp->bdaddr.b[5] & 0xc0) != 0xc0)
			return mgmt_cmd_status(sk, hdev->id,
					       MGMT_OP_SET_STATIC_ADDRESS,
					       MGMT_STATUS_INVALID_PARAMS);
	}

	hci_dev_lock(hdev);

	bacpy(&hdev->static_addr, &cp->bdaddr);

	err = send_settings_rsp(sk, MGMT_OP_SET_STATIC_ADDRESS, hdev);
	if (err < 0)
		goto unlock;

	err = new_settings(hdev, sk);

unlock:
	hci_dev_unlock(hdev);
	return err;
}

static int set_scan_params(struct sock *sk, struct hci_dev *hdev,
			   void *data, u16 len)
{
	struct mgmt_cp_set_scan_params *cp = data;
	__u16 interval, window;
	int err;

	bt_dev_dbg(hdev, "sock %p", sk);

	if (!lmp_le_capable(hdev))
		return mgmt_cmd_status(sk, hdev->id, MGMT_OP_SET_SCAN_PARAMS,
				       MGMT_STATUS_NOT_SUPPORTED);

	interval = __le16_to_cpu(cp->interval);

	if (interval < 0x0004 || interval > 0x4000)
		return mgmt_cmd_status(sk, hdev->id, MGMT_OP_SET_SCAN_PARAMS,
				       MGMT_STATUS_INVALID_PARAMS);

	window = __le16_to_cpu(cp->window);

	if (window < 0x0004 || window > 0x4000)
		return mgmt_cmd_status(sk, hdev->id, MGMT_OP_SET_SCAN_PARAMS,
				       MGMT_STATUS_INVALID_PARAMS);

	if (window > interval)
		return mgmt_cmd_status(sk, hdev->id, MGMT_OP_SET_SCAN_PARAMS,
				       MGMT_STATUS_INVALID_PARAMS);

	hci_dev_lock(hdev);

	hdev->le_scan_interval = interval;
	hdev->le_scan_window = window;

	err = mgmt_cmd_complete(sk, hdev->id, MGMT_OP_SET_SCAN_PARAMS, 0,
				NULL, 0);

	/* If background scan is running, restart it so new parameters are
	 * loaded.
	 */
	if (hci_dev_test_flag(hdev, HCI_LE_SCAN) &&
	    hdev->discovery.state == DISCOVERY_STOPPED)
		hci_update_passive_scan(hdev);

	hci_dev_unlock(hdev);

	return err;
}

static void fast_connectable_complete(struct hci_dev *hdev, void *data, int err)
{
	struct mgmt_pending_cmd *cmd = data;

	bt_dev_dbg(hdev, "err %d", err);

	if (err) {
		mgmt_cmd_status(cmd->sk, hdev->id, MGMT_OP_SET_FAST_CONNECTABLE,
				mgmt_status(err));
	} else {
		struct mgmt_mode *cp = cmd->param;

		if (cp->val)
			hci_dev_set_flag(hdev, HCI_FAST_CONNECTABLE);
		else
			hci_dev_clear_flag(hdev, HCI_FAST_CONNECTABLE);

		send_settings_rsp(cmd->sk, MGMT_OP_SET_FAST_CONNECTABLE, hdev);
		new_settings(hdev, cmd->sk);
	}

	mgmt_pending_free(cmd);
}

static int write_fast_connectable_sync(struct hci_dev *hdev, void *data)
{
	struct mgmt_pending_cmd *cmd = data;
	struct mgmt_mode *cp = cmd->param;

	return hci_write_fast_connectable_sync(hdev, cp->val);
}

static int set_fast_connectable(struct sock *sk, struct hci_dev *hdev,
				void *data, u16 len)
{
	struct mgmt_mode *cp = data;
	struct mgmt_pending_cmd *cmd;
	int err;

	bt_dev_dbg(hdev, "sock %p", sk);

	if (!hci_dev_test_flag(hdev, HCI_BREDR_ENABLED) ||
	    hdev->hci_ver < BLUETOOTH_VER_1_2)
		return mgmt_cmd_status(sk, hdev->id,
				       MGMT_OP_SET_FAST_CONNECTABLE,
				       MGMT_STATUS_NOT_SUPPORTED);

	if (cp->val != 0x00 && cp->val != 0x01)
		return mgmt_cmd_status(sk, hdev->id,
				       MGMT_OP_SET_FAST_CONNECTABLE,
				       MGMT_STATUS_INVALID_PARAMS);

	hci_dev_lock(hdev);

	if (!!cp->val == hci_dev_test_flag(hdev, HCI_FAST_CONNECTABLE)) {
		err = send_settings_rsp(sk, MGMT_OP_SET_FAST_CONNECTABLE, hdev);
		goto unlock;
	}

	if (!hdev_is_powered(hdev)) {
		hci_dev_change_flag(hdev, HCI_FAST_CONNECTABLE);
		err = send_settings_rsp(sk, MGMT_OP_SET_FAST_CONNECTABLE, hdev);
		new_settings(hdev, sk);
		goto unlock;
	}

	cmd = mgmt_pending_new(sk, MGMT_OP_SET_FAST_CONNECTABLE, hdev, data,
			       len);
	if (!cmd)
		err = -ENOMEM;
	else
		err = hci_cmd_sync_queue(hdev, write_fast_connectable_sync, cmd,
					 fast_connectable_complete);

	if (err < 0) {
		mgmt_cmd_status(sk, hdev->id, MGMT_OP_SET_FAST_CONNECTABLE,
				MGMT_STATUS_FAILED);

		if (cmd)
			mgmt_pending_free(cmd);
	}

unlock:
	hci_dev_unlock(hdev);

	return err;
}

static void set_bredr_complete(struct hci_dev *hdev, void *data, int err)
{
	struct mgmt_pending_cmd *cmd = data;

	bt_dev_dbg(hdev, "err %d", err);

	if (err) {
		u8 mgmt_err = mgmt_status(err);

		/* We need to restore the flag if related HCI commands
		 * failed.
		 */
		hci_dev_clear_flag(hdev, HCI_BREDR_ENABLED);

		mgmt_cmd_status(cmd->sk, cmd->index, cmd->opcode, mgmt_err);
	} else {
		send_settings_rsp(cmd->sk, MGMT_OP_SET_BREDR, hdev);
		new_settings(hdev, cmd->sk);
	}

	mgmt_pending_free(cmd);
}

static int set_bredr_sync(struct hci_dev *hdev, void *data)
{
	int status;

	status = hci_write_fast_connectable_sync(hdev, false);

	if (!status)
		status = hci_update_scan_sync(hdev);

	/* Since only the advertising data flags will change, there
	 * is no need to update the scan response data.
	 */
	if (!status)
		status = hci_update_adv_data_sync(hdev, hdev->cur_adv_instance);

	return status;
}

static int set_bredr(struct sock *sk, struct hci_dev *hdev, void *data, u16 len)
{
	struct mgmt_mode *cp = data;
	struct mgmt_pending_cmd *cmd;
	int err;

	bt_dev_dbg(hdev, "sock %p", sk);

	if (!lmp_bredr_capable(hdev) || !lmp_le_capable(hdev))
		return mgmt_cmd_status(sk, hdev->id, MGMT_OP_SET_BREDR,
				       MGMT_STATUS_NOT_SUPPORTED);

	if (!hci_dev_test_flag(hdev, HCI_LE_ENABLED))
		return mgmt_cmd_status(sk, hdev->id, MGMT_OP_SET_BREDR,
				       MGMT_STATUS_REJECTED);

	if (cp->val != 0x00 && cp->val != 0x01)
		return mgmt_cmd_status(sk, hdev->id, MGMT_OP_SET_BREDR,
				       MGMT_STATUS_INVALID_PARAMS);

	hci_dev_lock(hdev);

	if (cp->val == hci_dev_test_flag(hdev, HCI_BREDR_ENABLED)) {
		err = send_settings_rsp(sk, MGMT_OP_SET_BREDR, hdev);
		goto unlock;
	}

	if (!hdev_is_powered(hdev)) {
		if (!cp->val) {
			hci_dev_clear_flag(hdev, HCI_DISCOVERABLE);
			hci_dev_clear_flag(hdev, HCI_SSP_ENABLED);
			hci_dev_clear_flag(hdev, HCI_LINK_SECURITY);
			hci_dev_clear_flag(hdev, HCI_FAST_CONNECTABLE);
			hci_dev_clear_flag(hdev, HCI_HS_ENABLED);
		}

		hci_dev_change_flag(hdev, HCI_BREDR_ENABLED);

		err = send_settings_rsp(sk, MGMT_OP_SET_BREDR, hdev);
		if (err < 0)
			goto unlock;

		err = new_settings(hdev, sk);
		goto unlock;
	}

	/* Reject disabling when powered on */
	if (!cp->val) {
		err = mgmt_cmd_status(sk, hdev->id, MGMT_OP_SET_BREDR,
				      MGMT_STATUS_REJECTED);
		goto unlock;
	} else {
		/* When configuring a dual-mode controller to operate
		 * with LE only and using a static address, then switching
		 * BR/EDR back on is not allowed.
		 *
		 * Dual-mode controllers shall operate with the public
		 * address as its identity address for BR/EDR and LE. So
		 * reject the attempt to create an invalid configuration.
		 *
		 * The same restrictions applies when secure connections
		 * has been enabled. For BR/EDR this is a controller feature
		 * while for LE it is a host stack feature. This means that
		 * switching BR/EDR back on when secure connections has been
		 * enabled is not a supported transaction.
		 */
		if (!hci_dev_test_flag(hdev, HCI_BREDR_ENABLED) &&
		    (bacmp(&hdev->static_addr, BDADDR_ANY) ||
		     hci_dev_test_flag(hdev, HCI_SC_ENABLED))) {
			err = mgmt_cmd_status(sk, hdev->id, MGMT_OP_SET_BREDR,
					      MGMT_STATUS_REJECTED);
			goto unlock;
		}
	}

	cmd = mgmt_pending_new(sk, MGMT_OP_SET_BREDR, hdev, data, len);
	if (!cmd)
		err = -ENOMEM;
	else
		err = hci_cmd_sync_queue(hdev, set_bredr_sync, cmd,
					 set_bredr_complete);

	if (err < 0) {
		mgmt_cmd_status(sk, hdev->id, MGMT_OP_SET_BREDR,
				MGMT_STATUS_FAILED);
		if (cmd)
			mgmt_pending_free(cmd);

		goto unlock;
	}

	/* We need to flip the bit already here so that
	 * hci_req_update_adv_data generates the correct flags.
	 */
	hci_dev_set_flag(hdev, HCI_BREDR_ENABLED);

unlock:
	hci_dev_unlock(hdev);
	return err;
}

static void set_secure_conn_complete(struct hci_dev *hdev, void *data, int err)
{
	struct mgmt_pending_cmd *cmd = data;
	struct mgmt_mode *cp;

	bt_dev_dbg(hdev, "err %d", err);

	if (err) {
		u8 mgmt_err = mgmt_status(err);

		mgmt_cmd_status(cmd->sk, cmd->index, cmd->opcode, mgmt_err);
		goto done;
	}

	cp = cmd->param;

	switch (cp->val) {
	case 0x00:
		hci_dev_clear_flag(hdev, HCI_SC_ENABLED);
		hci_dev_clear_flag(hdev, HCI_SC_ONLY);
		break;
	case 0x01:
		hci_dev_set_flag(hdev, HCI_SC_ENABLED);
		hci_dev_clear_flag(hdev, HCI_SC_ONLY);
		break;
	case 0x02:
		hci_dev_set_flag(hdev, HCI_SC_ENABLED);
		hci_dev_set_flag(hdev, HCI_SC_ONLY);
		break;
	}

	send_settings_rsp(cmd->sk, cmd->opcode, hdev);
	new_settings(hdev, cmd->sk);

done:
	mgmt_pending_free(cmd);
}

static int set_secure_conn_sync(struct hci_dev *hdev, void *data)
{
	struct mgmt_pending_cmd *cmd = data;
	struct mgmt_mode *cp = cmd->param;
	u8 val = !!cp->val;

	/* Force write of val */
	hci_dev_set_flag(hdev, HCI_SC_ENABLED);

	return hci_write_sc_support_sync(hdev, val);
}

static int set_secure_conn(struct sock *sk, struct hci_dev *hdev,
			   void *data, u16 len)
{
	struct mgmt_mode *cp = data;
	struct mgmt_pending_cmd *cmd;
	u8 val;
	int err;

	bt_dev_dbg(hdev, "sock %p", sk);

	if (!lmp_sc_capable(hdev) &&
	    !hci_dev_test_flag(hdev, HCI_LE_ENABLED))
		return mgmt_cmd_status(sk, hdev->id, MGMT_OP_SET_SECURE_CONN,
				       MGMT_STATUS_NOT_SUPPORTED);

	if (hci_dev_test_flag(hdev, HCI_BREDR_ENABLED) &&
	    lmp_sc_capable(hdev) &&
	    !hci_dev_test_flag(hdev, HCI_SSP_ENABLED))
		return mgmt_cmd_status(sk, hdev->id, MGMT_OP_SET_SECURE_CONN,
				       MGMT_STATUS_REJECTED);

	if (cp->val != 0x00 && cp->val != 0x01 && cp->val != 0x02)
		return mgmt_cmd_status(sk, hdev->id, MGMT_OP_SET_SECURE_CONN,
				       MGMT_STATUS_INVALID_PARAMS);

	hci_dev_lock(hdev);

	if (!hdev_is_powered(hdev) || !lmp_sc_capable(hdev) ||
	    !hci_dev_test_flag(hdev, HCI_BREDR_ENABLED)) {
		bool changed;

		if (cp->val) {
			changed = !hci_dev_test_and_set_flag(hdev,
							     HCI_SC_ENABLED);
			if (cp->val == 0x02)
				hci_dev_set_flag(hdev, HCI_SC_ONLY);
			else
				hci_dev_clear_flag(hdev, HCI_SC_ONLY);
		} else {
			changed = hci_dev_test_and_clear_flag(hdev,
							      HCI_SC_ENABLED);
			hci_dev_clear_flag(hdev, HCI_SC_ONLY);
		}

		err = send_settings_rsp(sk, MGMT_OP_SET_SECURE_CONN, hdev);
		if (err < 0)
			goto failed;

		if (changed)
			err = new_settings(hdev, sk);

		goto failed;
	}

	val = !!cp->val;

	if (val == hci_dev_test_flag(hdev, HCI_SC_ENABLED) &&
	    (cp->val == 0x02) == hci_dev_test_flag(hdev, HCI_SC_ONLY)) {
		err = send_settings_rsp(sk, MGMT_OP_SET_SECURE_CONN, hdev);
		goto failed;
	}

	cmd = mgmt_pending_new(sk, MGMT_OP_SET_SECURE_CONN, hdev, data, len);
	if (!cmd)
		err = -ENOMEM;
	else
		err = hci_cmd_sync_queue(hdev, set_secure_conn_sync, cmd,
					 set_secure_conn_complete);

	if (err < 0) {
		mgmt_cmd_status(sk, hdev->id, MGMT_OP_SET_SECURE_CONN,
				MGMT_STATUS_FAILED);
		if (cmd)
			mgmt_pending_free(cmd);
	}

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

	bt_dev_dbg(hdev, "sock %p", sk);

	if (cp->val != 0x00 && cp->val != 0x01 && cp->val != 0x02)
		return mgmt_cmd_status(sk, hdev->id, MGMT_OP_SET_DEBUG_KEYS,
				       MGMT_STATUS_INVALID_PARAMS);

	hci_dev_lock(hdev);

	if (cp->val)
		changed = !hci_dev_test_and_set_flag(hdev, HCI_KEEP_DEBUG_KEYS);
	else
		changed = hci_dev_test_and_clear_flag(hdev,
						      HCI_KEEP_DEBUG_KEYS);

	if (cp->val == 0x02)
		use_changed = !hci_dev_test_and_set_flag(hdev,
							 HCI_USE_DEBUG_KEYS);
	else
		use_changed = hci_dev_test_and_clear_flag(hdev,
							  HCI_USE_DEBUG_KEYS);

	if (hdev_is_powered(hdev) && use_changed &&
	    hci_dev_test_flag(hdev, HCI_SSP_ENABLED)) {
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

	bt_dev_dbg(hdev, "sock %p", sk);

	if (!lmp_le_capable(hdev))
		return mgmt_cmd_status(sk, hdev->id, MGMT_OP_SET_PRIVACY,
				       MGMT_STATUS_NOT_SUPPORTED);

	if (cp->privacy != 0x00 && cp->privacy != 0x01 && cp->privacy != 0x02)
		return mgmt_cmd_status(sk, hdev->id, MGMT_OP_SET_PRIVACY,
				       MGMT_STATUS_INVALID_PARAMS);

	if (hdev_is_powered(hdev))
		return mgmt_cmd_status(sk, hdev->id, MGMT_OP_SET_PRIVACY,
				       MGMT_STATUS_REJECTED);

	hci_dev_lock(hdev);

	/* If user space supports this command it is also expected to
	 * handle IRKs. Therefore, set the HCI_RPA_RESOLVING flag.
	 */
	hci_dev_set_flag(hdev, HCI_RPA_RESOLVING);

	if (cp->privacy) {
		changed = !hci_dev_test_and_set_flag(hdev, HCI_PRIVACY);
		memcpy(hdev->irk, cp->irk, sizeof(hdev->irk));
		hci_dev_set_flag(hdev, HCI_RPA_EXPIRED);
		hci_adv_instances_set_rpa_expired(hdev, true);
		if (cp->privacy == 0x02)
			hci_dev_set_flag(hdev, HCI_LIMITED_PRIVACY);
		else
			hci_dev_clear_flag(hdev, HCI_LIMITED_PRIVACY);
	} else {
		changed = hci_dev_test_and_clear_flag(hdev, HCI_PRIVACY);
		memset(hdev->irk, 0, sizeof(hdev->irk));
		hci_dev_clear_flag(hdev, HCI_RPA_EXPIRED);
		hci_adv_instances_set_rpa_expired(hdev, false);
		hci_dev_clear_flag(hdev, HCI_LIMITED_PRIVACY);
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

	bt_dev_dbg(hdev, "sock %p", sk);

	if (!lmp_le_capable(hdev))
		return mgmt_cmd_status(sk, hdev->id, MGMT_OP_LOAD_IRKS,
				       MGMT_STATUS_NOT_SUPPORTED);

	irk_count = __le16_to_cpu(cp->irk_count);
	if (irk_count > max_irk_count) {
		bt_dev_err(hdev, "load_irks: too big irk_count value %u",
			   irk_count);
		return mgmt_cmd_status(sk, hdev->id, MGMT_OP_LOAD_IRKS,
				       MGMT_STATUS_INVALID_PARAMS);
	}

	expected_len = struct_size(cp, irks, irk_count);
	if (expected_len != len) {
		bt_dev_err(hdev, "load_irks: expected %u bytes, got %u bytes",
			   expected_len, len);
		return mgmt_cmd_status(sk, hdev->id, MGMT_OP_LOAD_IRKS,
				       MGMT_STATUS_INVALID_PARAMS);
	}

	bt_dev_dbg(hdev, "irk_count %u", irk_count);

	for (i = 0; i < irk_count; i++) {
		struct mgmt_irk_info *key = &cp->irks[i];

		if (!irk_is_valid(key))
			return mgmt_cmd_status(sk, hdev->id,
					       MGMT_OP_LOAD_IRKS,
					       MGMT_STATUS_INVALID_PARAMS);
	}

	hci_dev_lock(hdev);

	hci_smp_irks_clear(hdev);

	for (i = 0; i < irk_count; i++) {
		struct mgmt_irk_info *irk = &cp->irks[i];

		if (hci_is_blocked_key(hdev,
				       HCI_BLOCKED_KEY_TYPE_IRK,
				       irk->val)) {
			bt_dev_warn(hdev, "Skipping blocked IRK for %pMR",
				    &irk->addr.bdaddr);
			continue;
		}

		hci_add_irk(hdev, &irk->addr.bdaddr,
			    le_addr_type(irk->addr.type), irk->val,
			    BDADDR_ANY);
	}

	hci_dev_set_flag(hdev, HCI_RPA_RESOLVING);

	err = mgmt_cmd_complete(sk, hdev->id, MGMT_OP_LOAD_IRKS, 0, NULL, 0);

	hci_dev_unlock(hdev);

	return err;
}

static bool ltk_is_valid(struct mgmt_ltk_info *key)
{
	if (key->initiator != 0x00 && key->initiator != 0x01)
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

	bt_dev_dbg(hdev, "sock %p", sk);

	if (!lmp_le_capable(hdev))
		return mgmt_cmd_status(sk, hdev->id, MGMT_OP_LOAD_LONG_TERM_KEYS,
				       MGMT_STATUS_NOT_SUPPORTED);

	key_count = __le16_to_cpu(cp->key_count);
	if (key_count > max_key_count) {
		bt_dev_err(hdev, "load_ltks: too big key_count value %u",
			   key_count);
		return mgmt_cmd_status(sk, hdev->id, MGMT_OP_LOAD_LONG_TERM_KEYS,
				       MGMT_STATUS_INVALID_PARAMS);
	}

	expected_len = struct_size(cp, keys, key_count);
	if (expected_len != len) {
		bt_dev_err(hdev, "load_keys: expected %u bytes, got %u bytes",
			   expected_len, len);
		return mgmt_cmd_status(sk, hdev->id, MGMT_OP_LOAD_LONG_TERM_KEYS,
				       MGMT_STATUS_INVALID_PARAMS);
	}

	bt_dev_dbg(hdev, "key_count %u", key_count);

	for (i = 0; i < key_count; i++) {
		struct mgmt_ltk_info *key = &cp->keys[i];

		if (!ltk_is_valid(key))
			return mgmt_cmd_status(sk, hdev->id,
					       MGMT_OP_LOAD_LONG_TERM_KEYS,
					       MGMT_STATUS_INVALID_PARAMS);
	}

	hci_dev_lock(hdev);

	hci_smp_ltks_clear(hdev);

	for (i = 0; i < key_count; i++) {
		struct mgmt_ltk_info *key = &cp->keys[i];
		u8 type, authenticated;

		if (hci_is_blocked_key(hdev,
				       HCI_BLOCKED_KEY_TYPE_LTK,
				       key->val)) {
			bt_dev_warn(hdev, "Skipping blocked LTK for %pMR",
				    &key->addr.bdaddr);
			continue;
		}

		switch (key->type) {
		case MGMT_LTK_UNAUTHENTICATED:
			authenticated = 0x00;
			type = key->initiator ? SMP_LTK : SMP_LTK_RESPONDER;
			break;
		case MGMT_LTK_AUTHENTICATED:
			authenticated = 0x01;
			type = key->initiator ? SMP_LTK : SMP_LTK_RESPONDER;
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
			fallthrough;
		default:
			continue;
		}

		hci_add_ltk(hdev, &key->addr.bdaddr,
			    le_addr_type(key->addr.type), type, authenticated,
			    key->val, key->enc_size, key->ediv, key->rand);
	}

	err = mgmt_cmd_complete(sk, hdev->id, MGMT_OP_LOAD_LONG_TERM_KEYS, 0,
			   NULL, 0);

	hci_dev_unlock(hdev);

	return err;
}

static void get_conn_info_complete(struct hci_dev *hdev, void *data, int err)
{
	struct mgmt_pending_cmd *cmd = data;
	struct hci_conn *conn = cmd->user_data;
	struct mgmt_cp_get_conn_info *cp = cmd->param;
	struct mgmt_rp_get_conn_info rp;
	u8 status;

	bt_dev_dbg(hdev, "err %d", err);

	memcpy(&rp.addr, &cp->addr.bdaddr, sizeof(rp.addr));

	status = mgmt_status(err);
	if (status == MGMT_STATUS_SUCCESS) {
		rp.rssi = conn->rssi;
		rp.tx_power = conn->tx_power;
		rp.max_tx_power = conn->max_tx_power;
	} else {
		rp.rssi = HCI_RSSI_INVALID;
		rp.tx_power = HCI_TX_POWER_INVALID;
		rp.max_tx_power = HCI_TX_POWER_INVALID;
	}

	mgmt_cmd_complete(cmd->sk, cmd->index, MGMT_OP_GET_CONN_INFO, status,
			  &rp, sizeof(rp));

	if (conn) {
		hci_conn_drop(conn);
		hci_conn_put(conn);
	}

	mgmt_pending_free(cmd);
}

static int get_conn_info_sync(struct hci_dev *hdev, void *data)
{
	struct mgmt_pending_cmd *cmd = data;
	struct mgmt_cp_get_conn_info *cp = cmd->param;
	struct hci_conn *conn;
	int err;
	__le16   handle;

	/* Make sure we are still connected */
	if (cp->addr.type == BDADDR_BREDR)
		conn = hci_conn_hash_lookup_ba(hdev, ACL_LINK,
					       &cp->addr.bdaddr);
	else
		conn = hci_conn_hash_lookup_ba(hdev, LE_LINK, &cp->addr.bdaddr);

	if (!conn || conn != cmd->user_data || conn->state != BT_CONNECTED) {
		if (cmd->user_data) {
			hci_conn_drop(cmd->user_data);
			hci_conn_put(cmd->user_data);
			cmd->user_data = NULL;
		}
		return MGMT_STATUS_NOT_CONNECTED;
	}

	handle = cpu_to_le16(conn->handle);

	/* Refresh RSSI each time */
	err = hci_read_rssi_sync(hdev, handle);

	/* For LE links TX power does not change thus we don't need to
	 * query for it once value is known.
	 */
	if (!err && (!bdaddr_type_is_le(cp->addr.type) ||
		     conn->tx_power == HCI_TX_POWER_INVALID))
		err = hci_read_tx_power_sync(hdev, handle, 0x00);

	/* Max TX power needs to be read only once per connection */
	if (!err && conn->max_tx_power == HCI_TX_POWER_INVALID)
		err = hci_read_tx_power_sync(hdev, handle, 0x01);

	return err;
}

static int get_conn_info(struct sock *sk, struct hci_dev *hdev, void *data,
			 u16 len)
{
	struct mgmt_cp_get_conn_info *cp = data;
	struct mgmt_rp_get_conn_info rp;
	struct hci_conn *conn;
	unsigned long conn_info_age;
	int err = 0;

	bt_dev_dbg(hdev, "sock %p", sk);

	memset(&rp, 0, sizeof(rp));
	bacpy(&rp.addr.bdaddr, &cp->addr.bdaddr);
	rp.addr.type = cp->addr.type;

	if (!bdaddr_type_is_valid(cp->addr.type))
		return mgmt_cmd_complete(sk, hdev->id, MGMT_OP_GET_CONN_INFO,
					 MGMT_STATUS_INVALID_PARAMS,
					 &rp, sizeof(rp));

	hci_dev_lock(hdev);

	if (!hdev_is_powered(hdev)) {
		err = mgmt_cmd_complete(sk, hdev->id, MGMT_OP_GET_CONN_INFO,
					MGMT_STATUS_NOT_POWERED, &rp,
					sizeof(rp));
		goto unlock;
	}

	if (cp->addr.type == BDADDR_BREDR)
		conn = hci_conn_hash_lookup_ba(hdev, ACL_LINK,
					       &cp->addr.bdaddr);
	else
		conn = hci_conn_hash_lookup_ba(hdev, LE_LINK, &cp->addr.bdaddr);

	if (!conn || conn->state != BT_CONNECTED) {
		err = mgmt_cmd_complete(sk, hdev->id, MGMT_OP_GET_CONN_INFO,
					MGMT_STATUS_NOT_CONNECTED, &rp,
					sizeof(rp));
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
		struct mgmt_pending_cmd *cmd;

		cmd = mgmt_pending_new(sk, MGMT_OP_GET_CONN_INFO, hdev, data,
				       len);
		if (!cmd)
			err = -ENOMEM;
		else
			err = hci_cmd_sync_queue(hdev, get_conn_info_sync,
						 cmd, get_conn_info_complete);

		if (err < 0) {
			mgmt_cmd_complete(sk, hdev->id, MGMT_OP_GET_CONN_INFO,
					  MGMT_STATUS_FAILED, &rp, sizeof(rp));

			if (cmd)
				mgmt_pending_free(cmd);

			goto unlock;
		}

		hci_conn_hold(conn);
		cmd->user_data = hci_conn_get(conn);

		conn->conn_info_timestamp = jiffies;
	} else {
		/* Cache is valid, just reply with values cached in hci_conn */
		rp.rssi = conn->rssi;
		rp.tx_power = conn->tx_power;
		rp.max_tx_power = conn->max_tx_power;

		err = mgmt_cmd_complete(sk, hdev->id, MGMT_OP_GET_CONN_INFO,
					MGMT_STATUS_SUCCESS, &rp, sizeof(rp));
	}

unlock:
	hci_dev_unlock(hdev);
	return err;
}

static void get_clock_info_complete(struct hci_dev *hdev, void *data, int err)
{
	struct mgmt_pending_cmd *cmd = data;
	struct mgmt_cp_get_clock_info *cp = cmd->param;
	struct mgmt_rp_get_clock_info rp;
	struct hci_conn *conn = cmd->user_data;
	u8 status = mgmt_status(err);

	bt_dev_dbg(hdev, "err %d", err);

	memset(&rp, 0, sizeof(rp));
	bacpy(&rp.addr.bdaddr, &cp->addr.bdaddr);
	rp.addr.type = cp->addr.type;

	if (err)
		goto complete;

	rp.local_clock = cpu_to_le32(hdev->clock);

	if (conn) {
		rp.piconet_clock = cpu_to_le32(conn->clock);
		rp.accuracy = cpu_to_le16(conn->clock_accuracy);
		hci_conn_drop(conn);
		hci_conn_put(conn);
	}

complete:
	mgmt_cmd_complete(cmd->sk, cmd->index, cmd->opcode, status, &rp,
			  sizeof(rp));

	mgmt_pending_free(cmd);
}

static int get_clock_info_sync(struct hci_dev *hdev, void *data)
{
	struct mgmt_pending_cmd *cmd = data;
	struct mgmt_cp_get_clock_info *cp = cmd->param;
	struct hci_cp_read_clock hci_cp;
	struct hci_conn *conn = cmd->user_data;
	int err;

	memset(&hci_cp, 0, sizeof(hci_cp));
	err = hci_read_clock_sync(hdev, &hci_cp);

	if (conn) {
		/* Make sure connection still exists */
		conn = hci_conn_hash_lookup_ba(hdev, ACL_LINK,
					       &cp->addr.bdaddr);

		if (conn && conn == cmd->user_data &&
		    conn->state == BT_CONNECTED) {
			hci_cp.handle = cpu_to_le16(conn->handle);
			hci_cp.which = 0x01; /* Piconet clock */
			err = hci_read_clock_sync(hdev, &hci_cp);
		} else if (cmd->user_data) {
			hci_conn_drop(cmd->user_data);
			hci_conn_put(cmd->user_data);
			cmd->user_data = NULL;
		}
	}

	return err;
}

static int get_clock_info(struct sock *sk, struct hci_dev *hdev, void *data,
								u16 len)
{
	struct mgmt_cp_get_clock_info *cp = data;
	struct mgmt_rp_get_clock_info rp;
	struct mgmt_pending_cmd *cmd;
	struct hci_conn *conn;
	int err;

	bt_dev_dbg(hdev, "sock %p", sk);

	memset(&rp, 0, sizeof(rp));
	bacpy(&rp.addr.bdaddr, &cp->addr.bdaddr);
	rp.addr.type = cp->addr.type;

	if (cp->addr.type != BDADDR_BREDR)
		return mgmt_cmd_complete(sk, hdev->id, MGMT_OP_GET_CLOCK_INFO,
					 MGMT_STATUS_INVALID_PARAMS,
					 &rp, sizeof(rp));

	hci_dev_lock(hdev);

	if (!hdev_is_powered(hdev)) {
		err = mgmt_cmd_complete(sk, hdev->id, MGMT_OP_GET_CLOCK_INFO,
					MGMT_STATUS_NOT_POWERED, &rp,
					sizeof(rp));
		goto unlock;
	}

	if (bacmp(&cp->addr.bdaddr, BDADDR_ANY)) {
		conn = hci_conn_hash_lookup_ba(hdev, ACL_LINK,
					       &cp->addr.bdaddr);
		if (!conn || conn->state != BT_CONNECTED) {
			err = mgmt_cmd_complete(sk, hdev->id,
						MGMT_OP_GET_CLOCK_INFO,
						MGMT_STATUS_NOT_CONNECTED,
						&rp, sizeof(rp));
			goto unlock;
		}
	} else {
		conn = NULL;
	}

	cmd = mgmt_pending_new(sk, MGMT_OP_GET_CLOCK_INFO, hdev, data, len);
	if (!cmd)
		err = -ENOMEM;
	else
		err = hci_cmd_sync_queue(hdev, get_clock_info_sync, cmd,
					 get_clock_info_complete);

	if (err < 0) {
		err = mgmt_cmd_complete(sk, hdev->id, MGMT_OP_GET_CLOCK_INFO,
					MGMT_STATUS_FAILED, &rp, sizeof(rp));

		if (cmd)
			mgmt_pending_free(cmd);

	} else if (conn) {
		hci_conn_hold(conn);
		cmd->user_data = hci_conn_get(conn);
	}


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
static int hci_conn_params_set(struct hci_dev *hdev, bdaddr_t *addr,
			       u8 addr_type, u8 auto_connect)
{
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
		/* If auto connect is being disabled when we're trying to
		 * connect to device, keep connecting.
		 */
		if (params->explicit_connect)
			list_add(&params->action, &hdev->pend_le_conns);
		break;
	case HCI_AUTO_CONN_REPORT:
		if (params->explicit_connect)
			list_add(&params->action, &hdev->pend_le_conns);
		else
			list_add(&params->action, &hdev->pend_le_reports);
		break;
	case HCI_AUTO_CONN_DIRECT:
	case HCI_AUTO_CONN_ALWAYS:
		if (!is_connected(hdev, addr, addr_type))
			list_add(&params->action, &hdev->pend_le_conns);
		break;
	}

	params->auto_connect = auto_connect;

	bt_dev_dbg(hdev, "addr %pMR (type %u) auto_connect %u",
		   addr, addr_type, auto_connect);

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

static int add_device_sync(struct hci_dev *hdev, void *data)
{
	return hci_update_passive_scan_sync(hdev);
}

static int add_device(struct sock *sk, struct hci_dev *hdev,
		      void *data, u16 len)
{
	struct mgmt_cp_add_device *cp = data;
	u8 auto_conn, addr_type;
	struct hci_conn_params *params;
	int err;
	u32 current_flags = 0;

	bt_dev_dbg(hdev, "sock %p", sk);

	if (!bdaddr_type_is_valid(cp->addr.type) ||
	    !bacmp(&cp->addr.bdaddr, BDADDR_ANY))
		return mgmt_cmd_complete(sk, hdev->id, MGMT_OP_ADD_DEVICE,
					 MGMT_STATUS_INVALID_PARAMS,
					 &cp->addr, sizeof(cp->addr));

	if (cp->action != 0x00 && cp->action != 0x01 && cp->action != 0x02)
		return mgmt_cmd_complete(sk, hdev->id, MGMT_OP_ADD_DEVICE,
					 MGMT_STATUS_INVALID_PARAMS,
					 &cp->addr, sizeof(cp->addr));

	hci_dev_lock(hdev);

	if (cp->addr.type == BDADDR_BREDR) {
		/* Only incoming connections action is supported for now */
		if (cp->action != 0x01) {
			err = mgmt_cmd_complete(sk, hdev->id,
						MGMT_OP_ADD_DEVICE,
						MGMT_STATUS_INVALID_PARAMS,
						&cp->addr, sizeof(cp->addr));
			goto unlock;
		}

		err = hci_bdaddr_list_add_with_flags(&hdev->accept_list,
						     &cp->addr.bdaddr,
						     cp->addr.type, 0);
		if (err)
			goto unlock;

		hci_req_update_scan(hdev);

		goto added;
	}

	addr_type = le_addr_type(cp->addr.type);

	if (cp->action == 0x02)
		auto_conn = HCI_AUTO_CONN_ALWAYS;
	else if (cp->action == 0x01)
		auto_conn = HCI_AUTO_CONN_DIRECT;
	else
		auto_conn = HCI_AUTO_CONN_REPORT;

	/* Kernel internally uses conn_params with resolvable private
	 * address, but Add Device allows only identity addresses.
	 * Make sure it is enforced before calling
	 * hci_conn_params_lookup.
	 */
	if (!hci_is_identity_address(&cp->addr.bdaddr, addr_type)) {
		err = mgmt_cmd_complete(sk, hdev->id, MGMT_OP_ADD_DEVICE,
					MGMT_STATUS_INVALID_PARAMS,
					&cp->addr, sizeof(cp->addr));
		goto unlock;
	}

	/* If the connection parameters don't exist for this device,
	 * they will be created and configured with defaults.
	 */
	if (hci_conn_params_set(hdev, &cp->addr.bdaddr, addr_type,
				auto_conn) < 0) {
		err = mgmt_cmd_complete(sk, hdev->id, MGMT_OP_ADD_DEVICE,
					MGMT_STATUS_FAILED, &cp->addr,
					sizeof(cp->addr));
		goto unlock;
	} else {
		params = hci_conn_params_lookup(hdev, &cp->addr.bdaddr,
						addr_type);
		if (params)
			current_flags = params->current_flags;
	}

	err = hci_cmd_sync_queue(hdev, add_device_sync, NULL, NULL);
	if (err < 0)
		goto unlock;

added:
	device_added(sk, hdev, &cp->addr.bdaddr, cp->addr.type, cp->action);
	device_flags_changed(NULL, hdev, &cp->addr.bdaddr, cp->addr.type,
			     SUPPORTED_DEVICE_FLAGS(), current_flags);

	err = mgmt_cmd_complete(sk, hdev->id, MGMT_OP_ADD_DEVICE,
				MGMT_STATUS_SUCCESS, &cp->addr,
				sizeof(cp->addr));

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

static int remove_device_sync(struct hci_dev *hdev, void *data)
{
	return hci_update_passive_scan_sync(hdev);
}

static int remove_device(struct sock *sk, struct hci_dev *hdev,
			 void *data, u16 len)
{
	struct mgmt_cp_remove_device *cp = data;
	int err;

	bt_dev_dbg(hdev, "sock %p", sk);

	hci_dev_lock(hdev);

	if (bacmp(&cp->addr.bdaddr, BDADDR_ANY)) {
		struct hci_conn_params *params;
		u8 addr_type;

		if (!bdaddr_type_is_valid(cp->addr.type)) {
			err = mgmt_cmd_complete(sk, hdev->id,
						MGMT_OP_REMOVE_DEVICE,
						MGMT_STATUS_INVALID_PARAMS,
						&cp->addr, sizeof(cp->addr));
			goto unlock;
		}

		if (cp->addr.type == BDADDR_BREDR) {
			err = hci_bdaddr_list_del(&hdev->accept_list,
						  &cp->addr.bdaddr,
						  cp->addr.type);
			if (err) {
				err = mgmt_cmd_complete(sk, hdev->id,
							MGMT_OP_REMOVE_DEVICE,
							MGMT_STATUS_INVALID_PARAMS,
							&cp->addr,
							sizeof(cp->addr));
				goto unlock;
			}

			hci_req_update_scan(hdev);

			device_removed(sk, hdev, &cp->addr.bdaddr,
				       cp->addr.type);
			goto complete;
		}

		addr_type = le_addr_type(cp->addr.type);

		/* Kernel internally uses conn_params with resolvable private
		 * address, but Remove Device allows only identity addresses.
		 * Make sure it is enforced before calling
		 * hci_conn_params_lookup.
		 */
		if (!hci_is_identity_address(&cp->addr.bdaddr, addr_type)) {
			err = mgmt_cmd_complete(sk, hdev->id,
						MGMT_OP_REMOVE_DEVICE,
						MGMT_STATUS_INVALID_PARAMS,
						&cp->addr, sizeof(cp->addr));
			goto unlock;
		}

		params = hci_conn_params_lookup(hdev, &cp->addr.bdaddr,
						addr_type);
		if (!params) {
			err = mgmt_cmd_complete(sk, hdev->id,
						MGMT_OP_REMOVE_DEVICE,
						MGMT_STATUS_INVALID_PARAMS,
						&cp->addr, sizeof(cp->addr));
			goto unlock;
		}

		if (params->auto_connect == HCI_AUTO_CONN_DISABLED ||
		    params->auto_connect == HCI_AUTO_CONN_EXPLICIT) {
			err = mgmt_cmd_complete(sk, hdev->id,
						MGMT_OP_REMOVE_DEVICE,
						MGMT_STATUS_INVALID_PARAMS,
						&cp->addr, sizeof(cp->addr));
			goto unlock;
		}

		list_del(&params->action);
		list_del(&params->list);
		kfree(params);

		device_removed(sk, hdev, &cp->addr.bdaddr, cp->addr.type);
	} else {
		struct hci_conn_params *p, *tmp;
		struct bdaddr_list *b, *btmp;

		if (cp->addr.type) {
			err = mgmt_cmd_complete(sk, hdev->id,
						MGMT_OP_REMOVE_DEVICE,
						MGMT_STATUS_INVALID_PARAMS,
						&cp->addr, sizeof(cp->addr));
			goto unlock;
		}

		list_for_each_entry_safe(b, btmp, &hdev->accept_list, list) {
			device_removed(sk, hdev, &b->bdaddr, b->bdaddr_type);
			list_del(&b->list);
			kfree(b);
		}

		hci_req_update_scan(hdev);

		list_for_each_entry_safe(p, tmp, &hdev->le_conn_params, list) {
			if (p->auto_connect == HCI_AUTO_CONN_DISABLED)
				continue;
			device_removed(sk, hdev, &p->addr, p->addr_type);
			if (p->explicit_connect) {
				p->auto_connect = HCI_AUTO_CONN_EXPLICIT;
				continue;
			}
			list_del(&p->action);
			list_del(&p->list);
			kfree(p);
		}

		bt_dev_dbg(hdev, "All LE connection parameters were removed");
	}

	hci_cmd_sync_queue(hdev, remove_device_sync, NULL, NULL);

complete:
	err = mgmt_cmd_complete(sk, hdev->id, MGMT_OP_REMOVE_DEVICE,
				MGMT_STATUS_SUCCESS, &cp->addr,
				sizeof(cp->addr));
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
		return mgmt_cmd_status(sk, hdev->id, MGMT_OP_LOAD_CONN_PARAM,
				       MGMT_STATUS_NOT_SUPPORTED);

	param_count = __le16_to_cpu(cp->param_count);
	if (param_count > max_param_count) {
		bt_dev_err(hdev, "load_conn_param: too big param_count value %u",
			   param_count);
		return mgmt_cmd_status(sk, hdev->id, MGMT_OP_LOAD_CONN_PARAM,
				       MGMT_STATUS_INVALID_PARAMS);
	}

	expected_len = struct_size(cp, params, param_count);
	if (expected_len != len) {
		bt_dev_err(hdev, "load_conn_param: expected %u bytes, got %u bytes",
			   expected_len, len);
		return mgmt_cmd_status(sk, hdev->id, MGMT_OP_LOAD_CONN_PARAM,
				       MGMT_STATUS_INVALID_PARAMS);
	}

	bt_dev_dbg(hdev, "param_count %u", param_count);

	hci_dev_lock(hdev);

	hci_conn_params_clear_disabled(hdev);

	for (i = 0; i < param_count; i++) {
		struct mgmt_conn_param *param = &cp->params[i];
		struct hci_conn_params *hci_param;
		u16 min, max, latency, timeout;
		u8 addr_type;

		bt_dev_dbg(hdev, "Adding %pMR (type %u)", &param->addr.bdaddr,
			   param->addr.type);

		if (param->addr.type == BDADDR_LE_PUBLIC) {
			addr_type = ADDR_LE_DEV_PUBLIC;
		} else if (param->addr.type == BDADDR_LE_RANDOM) {
			addr_type = ADDR_LE_DEV_RANDOM;
		} else {
			bt_dev_err(hdev, "ignoring invalid connection parameters");
			continue;
		}

		min = le16_to_cpu(param->min_interval);
		max = le16_to_cpu(param->max_interval);
		latency = le16_to_cpu(param->latency);
		timeout = le16_to_cpu(param->timeout);

		bt_dev_dbg(hdev, "min 0x%04x max 0x%04x latency 0x%04x timeout 0x%04x",
			   min, max, latency, timeout);

		if (hci_check_conn_params(min, max, latency, timeout) < 0) {
			bt_dev_err(hdev, "ignoring invalid connection parameters");
			continue;
		}

		hci_param = hci_conn_params_add(hdev, &param->addr.bdaddr,
						addr_type);
		if (!hci_param) {
			bt_dev_err(hdev, "failed to add connection parameters");
			continue;
		}

		hci_param->conn_min_interval = min;
		hci_param->conn_max_interval = max;
		hci_param->conn_latency = latency;
		hci_param->supervision_timeout = timeout;
	}

	hci_dev_unlock(hdev);

	return mgmt_cmd_complete(sk, hdev->id, MGMT_OP_LOAD_CONN_PARAM, 0,
				 NULL, 0);
}

static int set_external_config(struct sock *sk, struct hci_dev *hdev,
			       void *data, u16 len)
{
	struct mgmt_cp_set_external_config *cp = data;
	bool changed;
	int err;

	bt_dev_dbg(hdev, "sock %p", sk);

	if (hdev_is_powered(hdev))
		return mgmt_cmd_status(sk, hdev->id, MGMT_OP_SET_EXTERNAL_CONFIG,
				       MGMT_STATUS_REJECTED);

	if (cp->config != 0x00 && cp->config != 0x01)
		return mgmt_cmd_status(sk, hdev->id, MGMT_OP_SET_EXTERNAL_CONFIG,
				         MGMT_STATUS_INVALID_PARAMS);

	if (!test_bit(HCI_QUIRK_EXTERNAL_CONFIG, &hdev->quirks))
		return mgmt_cmd_status(sk, hdev->id, MGMT_OP_SET_EXTERNAL_CONFIG,
				       MGMT_STATUS_NOT_SUPPORTED);

	hci_dev_lock(hdev);

	if (cp->config)
		changed = !hci_dev_test_and_set_flag(hdev, HCI_EXT_CONFIGURED);
	else
		changed = hci_dev_test_and_clear_flag(hdev, HCI_EXT_CONFIGURED);

	err = send_options_rsp(sk, MGMT_OP_SET_EXTERNAL_CONFIG, hdev);
	if (err < 0)
		goto unlock;

	if (!changed)
		goto unlock;

	err = new_options(hdev, sk);

	if (hci_dev_test_flag(hdev, HCI_UNCONFIGURED) == is_configured(hdev)) {
		mgmt_index_removed(hdev);

		if (hci_dev_test_and_change_flag(hdev, HCI_UNCONFIGURED)) {
			hci_dev_set_flag(hdev, HCI_CONFIG);
			hci_dev_set_flag(hdev, HCI_AUTO_OFF);

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

	bt_dev_dbg(hdev, "sock %p", sk);

	if (hdev_is_powered(hdev))
		return mgmt_cmd_status(sk, hdev->id, MGMT_OP_SET_PUBLIC_ADDRESS,
				       MGMT_STATUS_REJECTED);

	if (!bacmp(&cp->bdaddr, BDADDR_ANY))
		return mgmt_cmd_status(sk, hdev->id, MGMT_OP_SET_PUBLIC_ADDRESS,
				       MGMT_STATUS_INVALID_PARAMS);

	if (!hdev->set_bdaddr)
		return mgmt_cmd_status(sk, hdev->id, MGMT_OP_SET_PUBLIC_ADDRESS,
				       MGMT_STATUS_NOT_SUPPORTED);

	hci_dev_lock(hdev);

	changed = !!bacmp(&hdev->public_addr, &cp->bdaddr);
	bacpy(&hdev->public_addr, &cp->bdaddr);

	err = send_options_rsp(sk, MGMT_OP_SET_PUBLIC_ADDRESS, hdev);
	if (err < 0)
		goto unlock;

	if (!changed)
		goto unlock;

	if (hci_dev_test_flag(hdev, HCI_UNCONFIGURED))
		err = new_options(hdev, sk);

	if (is_configured(hdev)) {
		mgmt_index_removed(hdev);

		hci_dev_clear_flag(hdev, HCI_UNCONFIGURED);

		hci_dev_set_flag(hdev, HCI_CONFIG);
		hci_dev_set_flag(hdev, HCI_AUTO_OFF);

		queue_work(hdev->req_workqueue, &hdev->power_on);
	}

unlock:
	hci_dev_unlock(hdev);
	return err;
}

static void read_local_oob_ext_data_complete(struct hci_dev *hdev, void *data,
					     int err)
{
	const struct mgmt_cp_read_local_oob_ext_data *mgmt_cp;
	struct mgmt_rp_read_local_oob_ext_data *mgmt_rp;
	u8 *h192, *r192, *h256, *r256;
	struct mgmt_pending_cmd *cmd = data;
	struct sk_buff *skb = cmd->skb;
	u8 status = mgmt_status(err);
	u16 eir_len;

	if (!status) {
		if (!skb)
			status = MGMT_STATUS_FAILED;
		else if (IS_ERR(skb))
			status = mgmt_status(PTR_ERR(skb));
		else
			status = mgmt_status(skb->data[0]);
	}

	bt_dev_dbg(hdev, "status %u", status);

	mgmt_cp = cmd->param;

	if (status) {
		status = mgmt_status(status);
		eir_len = 0;

		h192 = NULL;
		r192 = NULL;
		h256 = NULL;
		r256 = NULL;
	} else if (!bredr_sc_enabled(hdev)) {
		struct hci_rp_read_local_oob_data *rp;

		if (skb->len != sizeof(*rp)) {
			status = MGMT_STATUS_FAILED;
			eir_len = 0;
		} else {
			status = MGMT_STATUS_SUCCESS;
			rp = (void *)skb->data;

			eir_len = 5 + 18 + 18;
			h192 = rp->hash;
			r192 = rp->rand;
			h256 = NULL;
			r256 = NULL;
		}
	} else {
		struct hci_rp_read_local_oob_ext_data *rp;

		if (skb->len != sizeof(*rp)) {
			status = MGMT_STATUS_FAILED;
			eir_len = 0;
		} else {
			status = MGMT_STATUS_SUCCESS;
			rp = (void *)skb->data;

			if (hci_dev_test_flag(hdev, HCI_SC_ONLY)) {
				eir_len = 5 + 18 + 18;
				h192 = NULL;
				r192 = NULL;
			} else {
				eir_len = 5 + 18 + 18 + 18 + 18;
				h192 = rp->hash192;
				r192 = rp->rand192;
			}

			h256 = rp->hash256;
			r256 = rp->rand256;
		}
	}

	mgmt_rp = kmalloc(sizeof(*mgmt_rp) + eir_len, GFP_KERNEL);
	if (!mgmt_rp)
		goto done;

	if (eir_len == 0)
		goto send_rsp;

	eir_len = eir_append_data(mgmt_rp->eir, 0, EIR_CLASS_OF_DEV,
				  hdev->dev_class, 3);

	if (h192 && r192) {
		eir_len = eir_append_data(mgmt_rp->eir, eir_len,
					  EIR_SSP_HASH_C192, h192, 16);
		eir_len = eir_append_data(mgmt_rp->eir, eir_len,
					  EIR_SSP_RAND_R192, r192, 16);
	}

	if (h256 && r256) {
		eir_len = eir_append_data(mgmt_rp->eir, eir_len,
					  EIR_SSP_HASH_C256, h256, 16);
		eir_len = eir_append_data(mgmt_rp->eir, eir_len,
					  EIR_SSP_RAND_R256, r256, 16);
	}

send_rsp:
	mgmt_rp->type = mgmt_cp->type;
	mgmt_rp->eir_len = cpu_to_le16(eir_len);

	err = mgmt_cmd_complete(cmd->sk, hdev->id,
				MGMT_OP_READ_LOCAL_OOB_EXT_DATA, status,
				mgmt_rp, sizeof(*mgmt_rp) + eir_len);
	if (err < 0 || status)
		goto done;

	hci_sock_set_flag(cmd->sk, HCI_MGMT_OOB_DATA_EVENTS);

	err = mgmt_limited_event(MGMT_EV_LOCAL_OOB_DATA_UPDATED, hdev,
				 mgmt_rp, sizeof(*mgmt_rp) + eir_len,
				 HCI_MGMT_OOB_DATA_EVENTS, cmd->sk);
done:
	if (skb && !IS_ERR(skb))
		kfree_skb(skb);

	kfree(mgmt_rp);
	mgmt_pending_remove(cmd);
}

static int read_local_ssp_oob_req(struct hci_dev *hdev, struct sock *sk,
				  struct mgmt_cp_read_local_oob_ext_data *cp)
{
	struct mgmt_pending_cmd *cmd;
	int err;

	cmd = mgmt_pending_add(sk, MGMT_OP_READ_LOCAL_OOB_EXT_DATA, hdev,
			       cp, sizeof(*cp));
	if (!cmd)
		return -ENOMEM;

	err = hci_cmd_sync_queue(hdev, read_local_oob_data_sync, cmd,
				 read_local_oob_ext_data_complete);

	if (err < 0) {
		mgmt_pending_remove(cmd);
		return err;
	}

	return 0;
}

static int read_local_oob_ext_data(struct sock *sk, struct hci_dev *hdev,
				   void *data, u16 data_len)
{
	struct mgmt_cp_read_local_oob_ext_data *cp = data;
	struct mgmt_rp_read_local_oob_ext_data *rp;
	size_t rp_len;
	u16 eir_len;
	u8 status, flags, role, addr[7], hash[16], rand[16];
	int err;

	bt_dev_dbg(hdev, "sock %p", sk);

	if (hdev_is_powered(hdev)) {
		switch (cp->type) {
		case BIT(BDADDR_BREDR):
			status = mgmt_bredr_support(hdev);
			if (status)
				eir_len = 0;
			else
				eir_len = 5;
			break;
		case (BIT(BDADDR_LE_PUBLIC) | BIT(BDADDR_LE_RANDOM)):
			status = mgmt_le_support(hdev);
			if (status)
				eir_len = 0;
			else
				eir_len = 9 + 3 + 18 + 18 + 3;
			break;
		default:
			status = MGMT_STATUS_INVALID_PARAMS;
			eir_len = 0;
			break;
		}
	} else {
		status = MGMT_STATUS_NOT_POWERED;
		eir_len = 0;
	}

	rp_len = sizeof(*rp) + eir_len;
	rp = kmalloc(rp_len, GFP_ATOMIC);
	if (!rp)
		return -ENOMEM;

	if (!status && !lmp_ssp_capable(hdev)) {
		status = MGMT_STATUS_NOT_SUPPORTED;
		eir_len = 0;
	}

	if (status)
		goto complete;

	hci_dev_lock(hdev);

	eir_len = 0;
	switch (cp->type) {
	case BIT(BDADDR_BREDR):
		if (hci_dev_test_flag(hdev, HCI_SSP_ENABLED)) {
			err = read_local_ssp_oob_req(hdev, sk, cp);
			hci_dev_unlock(hdev);
			if (!err)
				goto done;

			status = MGMT_STATUS_FAILED;
			goto complete;
		} else {
			eir_len = eir_append_data(rp->eir, eir_len,
						  EIR_CLASS_OF_DEV,
						  hdev->dev_class, 3);
		}
		break;
	case (BIT(BDADDR_LE_PUBLIC) | BIT(BDADDR_LE_RANDOM)):
		if (hci_dev_test_flag(hdev, HCI_SC_ENABLED) &&
		    smp_generate_oob(hdev, hash, rand) < 0) {
			hci_dev_unlock(hdev);
			status = MGMT_STATUS_FAILED;
			goto complete;
		}

		/* This should return the active RPA, but since the RPA
		 * is only programmed on demand, it is really hard to fill
		 * this in at the moment. For now disallow retrieving
		 * local out-of-band data when privacy is in use.
		 *
		 * Returning the identity address will not help here since
		 * pairing happens before the identity resolving key is
		 * known and thus the connection establishment happens
		 * based on the RPA and not the identity address.
		 */
		if (hci_dev_test_flag(hdev, HCI_PRIVACY)) {
			hci_dev_unlock(hdev);
			status = MGMT_STATUS_REJECTED;
			goto complete;
		}

		if (hci_dev_test_flag(hdev, HCI_FORCE_STATIC_ADDR) ||
		   !bacmp(&hdev->bdaddr, BDADDR_ANY) ||
		   (!hci_dev_test_flag(hdev, HCI_BREDR_ENABLED) &&
		    bacmp(&hdev->static_addr, BDADDR_ANY))) {
			memcpy(addr, &hdev->static_addr, 6);
			addr[6] = 0x01;
		} else {
			memcpy(addr, &hdev->bdaddr, 6);
			addr[6] = 0x00;
		}

		eir_len = eir_append_data(rp->eir, eir_len, EIR_LE_BDADDR,
					  addr, sizeof(addr));

		if (hci_dev_test_flag(hdev, HCI_ADVERTISING))
			role = 0x02;
		else
			role = 0x01;

		eir_len = eir_append_data(rp->eir, eir_len, EIR_LE_ROLE,
					  &role, sizeof(role));

		if (hci_dev_test_flag(hdev, HCI_SC_ENABLED)) {
			eir_len = eir_append_data(rp->eir, eir_len,
						  EIR_LE_SC_CONFIRM,
						  hash, sizeof(hash));

			eir_len = eir_append_data(rp->eir, eir_len,
						  EIR_LE_SC_RANDOM,
						  rand, sizeof(rand));
		}

		flags = mgmt_get_adv_discov_flags(hdev);

		if (!hci_dev_test_flag(hdev, HCI_BREDR_ENABLED))
			flags |= LE_AD_NO_BREDR;

		eir_len = eir_append_data(rp->eir, eir_len, EIR_FLAGS,
					  &flags, sizeof(flags));
		break;
	}

	hci_dev_unlock(hdev);

	hci_sock_set_flag(sk, HCI_MGMT_OOB_DATA_EVENTS);

	status = MGMT_STATUS_SUCCESS;

complete:
	rp->type = cp->type;
	rp->eir_len = cpu_to_le16(eir_len);

	err = mgmt_cmd_complete(sk, hdev->id, MGMT_OP_READ_LOCAL_OOB_EXT_DATA,
				status, rp, sizeof(*rp) + eir_len);
	if (err < 0 || status)
		goto done;

	err = mgmt_limited_event(MGMT_EV_LOCAL_OOB_DATA_UPDATED, hdev,
				 rp, sizeof(*rp) + eir_len,
				 HCI_MGMT_OOB_DATA_EVENTS, sk);

done:
	kfree(rp);

	return err;
}

static u32 get_supported_adv_flags(struct hci_dev *hdev)
{
	u32 flags = 0;

	flags |= MGMT_ADV_FLAG_CONNECTABLE;
	flags |= MGMT_ADV_FLAG_DISCOV;
	flags |= MGMT_ADV_FLAG_LIMITED_DISCOV;
	flags |= MGMT_ADV_FLAG_MANAGED_FLAGS;
	flags |= MGMT_ADV_FLAG_APPEARANCE;
	flags |= MGMT_ADV_FLAG_LOCAL_NAME;
	flags |= MGMT_ADV_PARAM_DURATION;
	flags |= MGMT_ADV_PARAM_TIMEOUT;
	flags |= MGMT_ADV_PARAM_INTERVALS;
	flags |= MGMT_ADV_PARAM_TX_POWER;
	flags |= MGMT_ADV_PARAM_SCAN_RSP;

	/* In extended adv TX_POWER returned from Set Adv Param
	 * will be always valid.
	 */
	if ((hdev->adv_tx_power != HCI_TX_POWER_INVALID) ||
	    ext_adv_capable(hdev))
		flags |= MGMT_ADV_FLAG_TX_POWER;

	if (ext_adv_capable(hdev)) {
		flags |= MGMT_ADV_FLAG_SEC_1M;
		flags |= MGMT_ADV_FLAG_HW_OFFLOAD;
		flags |= MGMT_ADV_FLAG_CAN_SET_TX_POWER;

		if (hdev->le_features[1] & HCI_LE_PHY_2M)
			flags |= MGMT_ADV_FLAG_SEC_2M;

		if (hdev->le_features[1] & HCI_LE_PHY_CODED)
			flags |= MGMT_ADV_FLAG_SEC_CODED;
	}

	return flags;
}

static int read_adv_features(struct sock *sk, struct hci_dev *hdev,
			     void *data, u16 data_len)
{
	struct mgmt_rp_read_adv_features *rp;
	size_t rp_len;
	int err;
	struct adv_info *adv_instance;
	u32 supported_flags;
	u8 *instance;

	bt_dev_dbg(hdev, "sock %p", sk);

	if (!lmp_le_capable(hdev))
		return mgmt_cmd_status(sk, hdev->id, MGMT_OP_READ_ADV_FEATURES,
				       MGMT_STATUS_REJECTED);

	hci_dev_lock(hdev);

	rp_len = sizeof(*rp) + hdev->adv_instance_cnt;
	rp = kmalloc(rp_len, GFP_ATOMIC);
	if (!rp) {
		hci_dev_unlock(hdev);
		return -ENOMEM;
	}

	supported_flags = get_supported_adv_flags(hdev);

	rp->supported_flags = cpu_to_le32(supported_flags);
	rp->max_adv_data_len = HCI_MAX_AD_LENGTH;
	rp->max_scan_rsp_len = HCI_MAX_AD_LENGTH;
	rp->max_instances = hdev->le_num_of_adv_sets;
	rp->num_instances = hdev->adv_instance_cnt;

	instance = rp->instance;
	list_for_each_entry(adv_instance, &hdev->adv_instances, list) {
		*instance = adv_instance->instance;
		instance++;
	}

	hci_dev_unlock(hdev);

	err = mgmt_cmd_complete(sk, hdev->id, MGMT_OP_READ_ADV_FEATURES,
				MGMT_STATUS_SUCCESS, rp, rp_len);

	kfree(rp);

	return err;
}

static u8 calculate_name_len(struct hci_dev *hdev)
{
	u8 buf[HCI_MAX_SHORT_NAME_LENGTH + 3];

	return eir_append_local_name(hdev, buf, 0);
}

static u8 tlv_data_max_len(struct hci_dev *hdev, u32 adv_flags,
			   bool is_adv_data)
{
	u8 max_len = HCI_MAX_AD_LENGTH;

	if (is_adv_data) {
		if (adv_flags & (MGMT_ADV_FLAG_DISCOV |
				 MGMT_ADV_FLAG_LIMITED_DISCOV |
				 MGMT_ADV_FLAG_MANAGED_FLAGS))
			max_len -= 3;

		if (adv_flags & MGMT_ADV_FLAG_TX_POWER)
			max_len -= 3;
	} else {
		if (adv_flags & MGMT_ADV_FLAG_LOCAL_NAME)
			max_len -= calculate_name_len(hdev);

		if (adv_flags & (MGMT_ADV_FLAG_APPEARANCE))
			max_len -= 4;
	}

	return max_len;
}

static bool flags_managed(u32 adv_flags)
{
	return adv_flags & (MGMT_ADV_FLAG_DISCOV |
			    MGMT_ADV_FLAG_LIMITED_DISCOV |
			    MGMT_ADV_FLAG_MANAGED_FLAGS);
}

static bool tx_power_managed(u32 adv_flags)
{
	return adv_flags & MGMT_ADV_FLAG_TX_POWER;
}

static bool name_managed(u32 adv_flags)
{
	return adv_flags & MGMT_ADV_FLAG_LOCAL_NAME;
}

static bool appearance_managed(u32 adv_flags)
{
	return adv_flags & MGMT_ADV_FLAG_APPEARANCE;
}

static bool tlv_data_is_valid(struct hci_dev *hdev, u32 adv_flags, u8 *data,
			      u8 len, bool is_adv_data)
{
	int i, cur_len;
	u8 max_len;

	max_len = tlv_data_max_len(hdev, adv_flags, is_adv_data);

	if (len > max_len)
		return false;

	/* Make sure that the data is correctly formatted. */
	for (i = 0, cur_len = 0; i < len; i += (cur_len + 1)) {
		cur_len = data[i];

		if (!cur_len)
			continue;

		if (data[i + 1] == EIR_FLAGS &&
		    (!is_adv_data || flags_managed(adv_flags)))
			return false;

		if (data[i + 1] == EIR_TX_POWER && tx_power_managed(adv_flags))
			return false;

		if (data[i + 1] == EIR_NAME_COMPLETE && name_managed(adv_flags))
			return false;

		if (data[i + 1] == EIR_NAME_SHORT && name_managed(adv_flags))
			return false;

		if (data[i + 1] == EIR_APPEARANCE &&
		    appearance_managed(adv_flags))
			return false;

		/* If the current field length would exceed the total data
		 * length, then it's invalid.
		 */
		if (i + cur_len >= len)
			return false;
	}

	return true;
}

static bool requested_adv_flags_are_valid(struct hci_dev *hdev, u32 adv_flags)
{
	u32 supported_flags, phy_flags;

	/* The current implementation only supports a subset of the specified
	 * flags. Also need to check mutual exclusiveness of sec flags.
	 */
	supported_flags = get_supported_adv_flags(hdev);
	phy_flags = adv_flags & MGMT_ADV_FLAG_SEC_MASK;
	if (adv_flags & ~supported_flags ||
	    ((phy_flags && (phy_flags ^ (phy_flags & -phy_flags)))))
		return false;

	return true;
}

static bool adv_busy(struct hci_dev *hdev)
{
	return (pending_find(MGMT_OP_ADD_ADVERTISING, hdev) ||
		pending_find(MGMT_OP_REMOVE_ADVERTISING, hdev) ||
		pending_find(MGMT_OP_SET_LE, hdev) ||
		pending_find(MGMT_OP_ADD_EXT_ADV_PARAMS, hdev) ||
		pending_find(MGMT_OP_ADD_EXT_ADV_DATA, hdev));
}

static void add_adv_complete(struct hci_dev *hdev, struct sock *sk, u8 instance,
			     int err)
{
	struct adv_info *adv, *n;

	bt_dev_dbg(hdev, "err %d", err);

	hci_dev_lock(hdev);

	list_for_each_entry_safe(adv, n, &hdev->adv_instances, list) {
		u8 instance;

		if (!adv->pending)
			continue;

		if (!err) {
			adv->pending = false;
			continue;
		}

		instance = adv->instance;

		if (hdev->cur_adv_instance == instance)
			cancel_adv_timeout(hdev);

		hci_remove_adv_instance(hdev, instance);
		mgmt_advertising_removed(sk, hdev, instance);
	}

	hci_dev_unlock(hdev);
}

static void add_advertising_complete(struct hci_dev *hdev, void *data, int err)
{
	struct mgmt_pending_cmd *cmd = data;
	struct mgmt_cp_add_advertising *cp = cmd->param;
	struct mgmt_rp_add_advertising rp;

	memset(&rp, 0, sizeof(rp));

	rp.instance = cp->instance;

	if (err)
		mgmt_cmd_status(cmd->sk, cmd->index, cmd->opcode,
				mgmt_status(err));
	else
		mgmt_cmd_complete(cmd->sk, cmd->index, cmd->opcode,
				  mgmt_status(err), &rp, sizeof(rp));

	add_adv_complete(hdev, cmd->sk, cp->instance, err);

	mgmt_pending_free(cmd);
}

static int add_advertising_sync(struct hci_dev *hdev, void *data)
{
	struct mgmt_pending_cmd *cmd = data;
	struct mgmt_cp_add_advertising *cp = cmd->param;

	return hci_schedule_adv_instance_sync(hdev, cp->instance, true);
}

static int add_advertising(struct sock *sk, struct hci_dev *hdev,
			   void *data, u16 data_len)
{
	struct mgmt_cp_add_advertising *cp = data;
	struct mgmt_rp_add_advertising rp;
	u32 flags;
	u8 status;
	u16 timeout, duration;
	unsigned int prev_instance_cnt = hdev->adv_instance_cnt;
	u8 schedule_instance = 0;
	struct adv_info *next_instance;
	int err;
	struct mgmt_pending_cmd *cmd;

	bt_dev_dbg(hdev, "sock %p", sk);

	status = mgmt_le_support(hdev);
	if (status)
		return mgmt_cmd_status(sk, hdev->id, MGMT_OP_ADD_ADVERTISING,
				       status);

	if (cp->instance < 1 || cp->instance > hdev->le_num_of_adv_sets)
		return mgmt_cmd_status(sk, hdev->id, MGMT_OP_ADD_ADVERTISING,
				       MGMT_STATUS_INVALID_PARAMS);

	if (data_len != sizeof(*cp) + cp->adv_data_len + cp->scan_rsp_len)
		return mgmt_cmd_status(sk, hdev->id, MGMT_OP_ADD_ADVERTISING,
				       MGMT_STATUS_INVALID_PARAMS);

	flags = __le32_to_cpu(cp->flags);
	timeout = __le16_to_cpu(cp->timeout);
	duration = __le16_to_cpu(cp->duration);

	if (!requested_adv_flags_are_valid(hdev, flags))
		return mgmt_cmd_status(sk, hdev->id, MGMT_OP_ADD_ADVERTISING,
				       MGMT_STATUS_INVALID_PARAMS);

	hci_dev_lock(hdev);

	if (timeout && !hdev_is_powered(hdev)) {
		err = mgmt_cmd_status(sk, hdev->id, MGMT_OP_ADD_ADVERTISING,
				      MGMT_STATUS_REJECTED);
		goto unlock;
	}

	if (adv_busy(hdev)) {
		err = mgmt_cmd_status(sk, hdev->id, MGMT_OP_ADD_ADVERTISING,
				      MGMT_STATUS_BUSY);
		goto unlock;
	}

	if (!tlv_data_is_valid(hdev, flags, cp->data, cp->adv_data_len, true) ||
	    !tlv_data_is_valid(hdev, flags, cp->data + cp->adv_data_len,
			       cp->scan_rsp_len, false)) {
		err = mgmt_cmd_status(sk, hdev->id, MGMT_OP_ADD_ADVERTISING,
				      MGMT_STATUS_INVALID_PARAMS);
		goto unlock;
	}

	err = hci_add_adv_instance(hdev, cp->instance, flags,
				   cp->adv_data_len, cp->data,
				   cp->scan_rsp_len,
				   cp->data + cp->adv_data_len,
				   timeout, duration,
				   HCI_ADV_TX_POWER_NO_PREFERENCE,
				   hdev->le_adv_min_interval,
				   hdev->le_adv_max_interval);
	if (err < 0) {
		err = mgmt_cmd_status(sk, hdev->id, MGMT_OP_ADD_ADVERTISING,
				      MGMT_STATUS_FAILED);
		goto unlock;
	}

	/* Only trigger an advertising added event if a new instance was
	 * actually added.
	 */
	if (hdev->adv_instance_cnt > prev_instance_cnt)
		mgmt_advertising_added(sk, hdev, cp->instance);

	if (hdev->cur_adv_instance == cp->instance) {
		/* If the currently advertised instance is being changed then
		 * cancel the current advertising and schedule the next
		 * instance. If there is only one instance then the overridden
		 * advertising data will be visible right away.
		 */
		cancel_adv_timeout(hdev);

		next_instance = hci_get_next_instance(hdev, cp->instance);
		if (next_instance)
			schedule_instance = next_instance->instance;
	} else if (!hdev->adv_instance_timeout) {
		/* Immediately advertise the new instance if no other
		 * instance is currently being advertised.
		 */
		schedule_instance = cp->instance;
	}

	/* If the HCI_ADVERTISING flag is set or the device isn't powered or
	 * there is no instance to be advertised then we have no HCI
	 * communication to make. Simply return.
	 */
	if (!hdev_is_powered(hdev) ||
	    hci_dev_test_flag(hdev, HCI_ADVERTISING) ||
	    !schedule_instance) {
		rp.instance = cp->instance;
		err = mgmt_cmd_complete(sk, hdev->id, MGMT_OP_ADD_ADVERTISING,
					MGMT_STATUS_SUCCESS, &rp, sizeof(rp));
		goto unlock;
	}

	/* We're good to go, update advertising data, parameters, and start
	 * advertising.
	 */
	cmd = mgmt_pending_new(sk, MGMT_OP_ADD_ADVERTISING, hdev, data,
			       data_len);
	if (!cmd) {
		err = -ENOMEM;
		goto unlock;
	}

	cp->instance = schedule_instance;

	err = hci_cmd_sync_queue(hdev, add_advertising_sync, cmd,
				 add_advertising_complete);
	if (err < 0)
		mgmt_pending_free(cmd);

unlock:
	hci_dev_unlock(hdev);

	return err;
}

static void add_ext_adv_params_complete(struct hci_dev *hdev, void *data,
					int err)
{
	struct mgmt_pending_cmd *cmd = data;
	struct mgmt_cp_add_ext_adv_params *cp = cmd->param;
	struct mgmt_rp_add_ext_adv_params rp;
	struct adv_info *adv;
	u32 flags;

	BT_DBG("%s", hdev->name);

	hci_dev_lock(hdev);

	adv = hci_find_adv_instance(hdev, cp->instance);
	if (!adv)
		goto unlock;

	rp.instance = cp->instance;
	rp.tx_power = adv->tx_power;

	/* While we're at it, inform userspace of the available space for this
	 * advertisement, given the flags that will be used.
	 */
	flags = __le32_to_cpu(cp->flags);
	rp.max_adv_data_len = tlv_data_max_len(hdev, flags, true);
	rp.max_scan_rsp_len = tlv_data_max_len(hdev, flags, false);

	if (err) {
		/* If this advertisement was previously advertising and we
		 * failed to update it, we signal that it has been removed and
		 * delete its structure
		 */
		if (!adv->pending)
			mgmt_advertising_removed(cmd->sk, hdev, cp->instance);

		hci_remove_adv_instance(hdev, cp->instance);

		mgmt_cmd_status(cmd->sk, cmd->index, cmd->opcode,
				mgmt_status(err));
	} else {
		mgmt_cmd_complete(cmd->sk, cmd->index, cmd->opcode,
				  mgmt_status(err), &rp, sizeof(rp));
	}

unlock:
	if (cmd)
		mgmt_pending_free(cmd);

	hci_dev_unlock(hdev);
}

static int add_ext_adv_params_sync(struct hci_dev *hdev, void *data)
{
	struct mgmt_pending_cmd *cmd = data;
	struct mgmt_cp_add_ext_adv_params *cp = cmd->param;

	return hci_setup_ext_adv_instance_sync(hdev, cp->instance);
}

static int add_ext_adv_params(struct sock *sk, struct hci_dev *hdev,
			      void *data, u16 data_len)
{
	struct mgmt_cp_add_ext_adv_params *cp = data;
	struct mgmt_rp_add_ext_adv_params rp;
	struct mgmt_pending_cmd *cmd = NULL;
	u32 flags, min_interval, max_interval;
	u16 timeout, duration;
	u8 status;
	s8 tx_power;
	int err;

	BT_DBG("%s", hdev->name);

	status = mgmt_le_support(hdev);
	if (status)
		return mgmt_cmd_status(sk, hdev->id, MGMT_OP_ADD_EXT_ADV_PARAMS,
				       status);

	if (cp->instance < 1 || cp->instance > hdev->le_num_of_adv_sets)
		return mgmt_cmd_status(sk, hdev->id, MGMT_OP_ADD_EXT_ADV_PARAMS,
				       MGMT_STATUS_INVALID_PARAMS);

	/* The purpose of breaking add_advertising into two separate MGMT calls
	 * for params and data is to allow more parameters to be added to this
	 * structure in the future. For this reason, we verify that we have the
	 * bare minimum structure we know of when the interface was defined. Any
	 * extra parameters we don't know about will be ignored in this request.
	 */
	if (data_len < MGMT_ADD_EXT_ADV_PARAMS_MIN_SIZE)
		return mgmt_cmd_status(sk, hdev->id, MGMT_OP_ADD_ADVERTISING,
				       MGMT_STATUS_INVALID_PARAMS);

	flags = __le32_to_cpu(cp->flags);

	if (!requested_adv_flags_are_valid(hdev, flags))
		return mgmt_cmd_status(sk, hdev->id, MGMT_OP_ADD_EXT_ADV_PARAMS,
				       MGMT_STATUS_INVALID_PARAMS);

	hci_dev_lock(hdev);

	/* In new interface, we require that we are powered to register */
	if (!hdev_is_powered(hdev)) {
		err = mgmt_cmd_status(sk, hdev->id, MGMT_OP_ADD_EXT_ADV_PARAMS,
				      MGMT_STATUS_REJECTED);
		goto unlock;
	}

	if (adv_busy(hdev)) {
		err = mgmt_cmd_status(sk, hdev->id, MGMT_OP_ADD_EXT_ADV_PARAMS,
				      MGMT_STATUS_BUSY);
		goto unlock;
	}

	/* Parse defined parameters from request, use defaults otherwise */
	timeout = (flags & MGMT_ADV_PARAM_TIMEOUT) ?
		  __le16_to_cpu(cp->timeout) : 0;

	duration = (flags & MGMT_ADV_PARAM_DURATION) ?
		   __le16_to_cpu(cp->duration) :
		   hdev->def_multi_adv_rotation_duration;

	min_interval = (flags & MGMT_ADV_PARAM_INTERVALS) ?
		       __le32_to_cpu(cp->min_interval) :
		       hdev->le_adv_min_interval;

	max_interval = (flags & MGMT_ADV_PARAM_INTERVALS) ?
		       __le32_to_cpu(cp->max_interval) :
		       hdev->le_adv_max_interval;

	tx_power = (flags & MGMT_ADV_PARAM_TX_POWER) ?
		   cp->tx_power :
		   HCI_ADV_TX_POWER_NO_PREFERENCE;

	/* Create advertising instance with no advertising or response data */
	err = hci_add_adv_instance(hdev, cp->instance, flags,
				   0, NULL, 0, NULL, timeout, duration,
				   tx_power, min_interval, max_interval);

	if (err < 0) {
		err = mgmt_cmd_status(sk, hdev->id, MGMT_OP_ADD_EXT_ADV_PARAMS,
				      MGMT_STATUS_FAILED);
		goto unlock;
	}

	/* Submit request for advertising params if ext adv available */
	if (ext_adv_capable(hdev)) {
		cmd = mgmt_pending_new(sk, MGMT_OP_ADD_EXT_ADV_PARAMS, hdev,
				       data, data_len);
		if (!cmd) {
			err = -ENOMEM;
			hci_remove_adv_instance(hdev, cp->instance);
			goto unlock;
		}

		err = hci_cmd_sync_queue(hdev, add_ext_adv_params_sync, cmd,
					 add_ext_adv_params_complete);
		if (err < 0)
			mgmt_pending_free(cmd);
	} else {
		rp.instance = cp->instance;
		rp.tx_power = HCI_ADV_TX_POWER_NO_PREFERENCE;
		rp.max_adv_data_len = tlv_data_max_len(hdev, flags, true);
		rp.max_scan_rsp_len = tlv_data_max_len(hdev, flags, false);
		err = mgmt_cmd_complete(sk, hdev->id,
					MGMT_OP_ADD_EXT_ADV_PARAMS,
					MGMT_STATUS_SUCCESS, &rp, sizeof(rp));
	}

unlock:
	hci_dev_unlock(hdev);

	return err;
}

static void add_ext_adv_data_complete(struct hci_dev *hdev, void *data, int err)
{
	struct mgmt_pending_cmd *cmd = data;
	struct mgmt_cp_add_ext_adv_data *cp = cmd->param;
	struct mgmt_rp_add_advertising rp;

	add_adv_complete(hdev, cmd->sk, cp->instance, err);

	memset(&rp, 0, sizeof(rp));

	rp.instance = cp->instance;

	if (err)
		mgmt_cmd_status(cmd->sk, cmd->index, cmd->opcode,
				mgmt_status(err));
	else
		mgmt_cmd_complete(cmd->sk, cmd->index, cmd->opcode,
				  mgmt_status(err), &rp, sizeof(rp));

	mgmt_pending_free(cmd);
}

static int add_ext_adv_data_sync(struct hci_dev *hdev, void *data)
{
	struct mgmt_pending_cmd *cmd = data;
	struct mgmt_cp_add_ext_adv_data *cp = cmd->param;
	int err;

	if (ext_adv_capable(hdev)) {
		err = hci_update_adv_data_sync(hdev, cp->instance);
		if (err)
			return err;

		err = hci_update_scan_rsp_data_sync(hdev, cp->instance);
		if (err)
			return err;

		return hci_enable_ext_advertising_sync(hdev, cp->instance);
	}

	return hci_schedule_adv_instance_sync(hdev, cp->instance, true);
}

static int add_ext_adv_data(struct sock *sk, struct hci_dev *hdev, void *data,
			    u16 data_len)
{
	struct mgmt_cp_add_ext_adv_data *cp = data;
	struct mgmt_rp_add_ext_adv_data rp;
	u8 schedule_instance = 0;
	struct adv_info *next_instance;
	struct adv_info *adv_instance;
	int err = 0;
	struct mgmt_pending_cmd *cmd;

	BT_DBG("%s", hdev->name);

	hci_dev_lock(hdev);

	adv_instance = hci_find_adv_instance(hdev, cp->instance);

	if (!adv_instance) {
		err = mgmt_cmd_status(sk, hdev->id, MGMT_OP_ADD_EXT_ADV_DATA,
				      MGMT_STATUS_INVALID_PARAMS);
		goto unlock;
	}

	/* In new interface, we require that we are powered to register */
	if (!hdev_is_powered(hdev)) {
		err = mgmt_cmd_status(sk, hdev->id, MGMT_OP_ADD_EXT_ADV_DATA,
				      MGMT_STATUS_REJECTED);
		goto clear_new_instance;
	}

	if (adv_busy(hdev)) {
		err = mgmt_cmd_status(sk, hdev->id, MGMT_OP_ADD_EXT_ADV_DATA,
				      MGMT_STATUS_BUSY);
		goto clear_new_instance;
	}

	/* Validate new data */
	if (!tlv_data_is_valid(hdev, adv_instance->flags, cp->data,
			       cp->adv_data_len, true) ||
	    !tlv_data_is_valid(hdev, adv_instance->flags, cp->data +
			       cp->adv_data_len, cp->scan_rsp_len, false)) {
		err = mgmt_cmd_status(sk, hdev->id, MGMT_OP_ADD_EXT_ADV_DATA,
				      MGMT_STATUS_INVALID_PARAMS);
		goto clear_new_instance;
	}

	/* Set the data in the advertising instance */
	hci_set_adv_instance_data(hdev, cp->instance, cp->adv_data_len,
				  cp->data, cp->scan_rsp_len,
				  cp->data + cp->adv_data_len);

	/* If using software rotation, determine next instance to use */
	if (hdev->cur_adv_instance == cp->instance) {
		/* If the currently advertised instance is being changed
		 * then cancel the current advertising and schedule the
		 * next instance. If there is only one instance then the
		 * overridden advertising data will be visible right
		 * away
		 */
		cancel_adv_timeout(hdev);

		next_instance = hci_get_next_instance(hdev, cp->instance);
		if (next_instance)
			schedule_instance = next_instance->instance;
	} else if (!hdev->adv_instance_timeout) {
		/* Immediately advertise the new instance if no other
		 * instance is currently being advertised.
		 */
		schedule_instance = cp->instance;
	}

	/* If the HCI_ADVERTISING flag is set or there is no instance to
	 * be advertised then we have no HCI communication to make.
	 * Simply return.
	 */
	if (hci_dev_test_flag(hdev, HCI_ADVERTISING) || !schedule_instance) {
		if (adv_instance->pending) {
			mgmt_advertising_added(sk, hdev, cp->instance);
			adv_instance->pending = false;
		}
		rp.instance = cp->instance;
		err = mgmt_cmd_complete(sk, hdev->id, MGMT_OP_ADD_EXT_ADV_DATA,
					MGMT_STATUS_SUCCESS, &rp, sizeof(rp));
		goto unlock;
	}

	cmd = mgmt_pending_new(sk, MGMT_OP_ADD_EXT_ADV_DATA, hdev, data,
			       data_len);
	if (!cmd) {
		err = -ENOMEM;
		goto clear_new_instance;
	}

	err = hci_cmd_sync_queue(hdev, add_ext_adv_data_sync, cmd,
				 add_ext_adv_data_complete);
	if (err < 0) {
		mgmt_pending_free(cmd);
		goto clear_new_instance;
	}

	/* We were successful in updating data, so trigger advertising_added
	 * event if this is an instance that wasn't previously advertising. If
	 * a failure occurs in the requests we initiated, we will remove the
	 * instance again in add_advertising_complete
	 */
	if (adv_instance->pending)
		mgmt_advertising_added(sk, hdev, cp->instance);

	goto unlock;

clear_new_instance:
	hci_remove_adv_instance(hdev, cp->instance);

unlock:
	hci_dev_unlock(hdev);

	return err;
}

static void remove_advertising_complete(struct hci_dev *hdev, void *data,
					int err)
{
	struct mgmt_pending_cmd *cmd = data;
	struct mgmt_cp_remove_advertising *cp = cmd->param;
	struct mgmt_rp_remove_advertising rp;

	bt_dev_dbg(hdev, "err %d", err);

	memset(&rp, 0, sizeof(rp));
	rp.instance = cp->instance;

	if (err)
		mgmt_cmd_status(cmd->sk, cmd->index, cmd->opcode,
				mgmt_status(err));
	else
		mgmt_cmd_complete(cmd->sk, cmd->index, cmd->opcode,
				  MGMT_STATUS_SUCCESS, &rp, sizeof(rp));

	mgmt_pending_free(cmd);
}

static int remove_advertising_sync(struct hci_dev *hdev, void *data)
{
	struct mgmt_pending_cmd *cmd = data;
	struct mgmt_cp_remove_advertising *cp = cmd->param;
	int err;

	err = hci_remove_advertising_sync(hdev, cmd->sk, cp->instance, true);
	if (err)
		return err;

	if (list_empty(&hdev->adv_instances))
		err = hci_disable_advertising_sync(hdev);

	return err;
}

static int remove_advertising(struct sock *sk, struct hci_dev *hdev,
			      void *data, u16 data_len)
{
	struct mgmt_cp_remove_advertising *cp = data;
	struct mgmt_pending_cmd *cmd;
	int err;

	bt_dev_dbg(hdev, "sock %p", sk);

	hci_dev_lock(hdev);

	if (cp->instance && !hci_find_adv_instance(hdev, cp->instance)) {
		err = mgmt_cmd_status(sk, hdev->id,
				      MGMT_OP_REMOVE_ADVERTISING,
				      MGMT_STATUS_INVALID_PARAMS);
		goto unlock;
	}

	if (pending_find(MGMT_OP_ADD_ADVERTISING, hdev) ||
	    pending_find(MGMT_OP_REMOVE_ADVERTISING, hdev) ||
	    pending_find(MGMT_OP_SET_LE, hdev)) {
		err = mgmt_cmd_status(sk, hdev->id, MGMT_OP_REMOVE_ADVERTISING,
				      MGMT_STATUS_BUSY);
		goto unlock;
	}

	if (list_empty(&hdev->adv_instances)) {
		err = mgmt_cmd_status(sk, hdev->id, MGMT_OP_REMOVE_ADVERTISING,
				      MGMT_STATUS_INVALID_PARAMS);
		goto unlock;
	}

	cmd = mgmt_pending_new(sk, MGMT_OP_REMOVE_ADVERTISING, hdev, data,
			       data_len);
	if (!cmd) {
		err = -ENOMEM;
		goto unlock;
	}

	err = hci_cmd_sync_queue(hdev, remove_advertising_sync, cmd,
				 remove_advertising_complete);
	if (err < 0)
		mgmt_pending_free(cmd);

unlock:
	hci_dev_unlock(hdev);

	return err;
}

static int get_adv_size_info(struct sock *sk, struct hci_dev *hdev,
			     void *data, u16 data_len)
{
	struct mgmt_cp_get_adv_size_info *cp = data;
	struct mgmt_rp_get_adv_size_info rp;
	u32 flags, supported_flags;
	int err;

	bt_dev_dbg(hdev, "sock %p", sk);

	if (!lmp_le_capable(hdev))
		return mgmt_cmd_status(sk, hdev->id, MGMT_OP_GET_ADV_SIZE_INFO,
				       MGMT_STATUS_REJECTED);

	if (cp->instance < 1 || cp->instance > hdev->le_num_of_adv_sets)
		return mgmt_cmd_status(sk, hdev->id, MGMT_OP_GET_ADV_SIZE_INFO,
				       MGMT_STATUS_INVALID_PARAMS);

	flags = __le32_to_cpu(cp->flags);

	/* The current implementation only supports a subset of the specified
	 * flags.
	 */
	supported_flags = get_supported_adv_flags(hdev);
	if (flags & ~supported_flags)
		return mgmt_cmd_status(sk, hdev->id, MGMT_OP_GET_ADV_SIZE_INFO,
				       MGMT_STATUS_INVALID_PARAMS);

	rp.instance = cp->instance;
	rp.flags = cp->flags;
	rp.max_adv_data_len = tlv_data_max_len(hdev, flags, true);
	rp.max_scan_rsp_len = tlv_data_max_len(hdev, flags, false);

	err = mgmt_cmd_complete(sk, hdev->id, MGMT_OP_GET_ADV_SIZE_INFO,
				MGMT_STATUS_SUCCESS, &rp, sizeof(rp));

	return err;
}

static const struct hci_mgmt_handler mgmt_handlers[] = {
	{ NULL }, /* 0x0000 (no command) */
	{ read_version,            MGMT_READ_VERSION_SIZE,
						HCI_MGMT_NO_HDEV |
						HCI_MGMT_UNTRUSTED },
	{ read_commands,           MGMT_READ_COMMANDS_SIZE,
						HCI_MGMT_NO_HDEV |
						HCI_MGMT_UNTRUSTED },
	{ read_index_list,         MGMT_READ_INDEX_LIST_SIZE,
						HCI_MGMT_NO_HDEV |
						HCI_MGMT_UNTRUSTED },
	{ read_controller_info,    MGMT_READ_INFO_SIZE,
						HCI_MGMT_UNTRUSTED },
	{ set_powered,             MGMT_SETTING_SIZE },
	{ set_discoverable,        MGMT_SET_DISCOVERABLE_SIZE },
	{ set_connectable,         MGMT_SETTING_SIZE },
	{ set_fast_connectable,    MGMT_SETTING_SIZE },
	{ set_bondable,            MGMT_SETTING_SIZE },
	{ set_link_security,       MGMT_SETTING_SIZE },
	{ set_ssp,                 MGMT_SETTING_SIZE },
	{ set_hs,                  MGMT_SETTING_SIZE },
	{ set_le,                  MGMT_SETTING_SIZE },
	{ set_dev_class,           MGMT_SET_DEV_CLASS_SIZE },
	{ set_local_name,          MGMT_SET_LOCAL_NAME_SIZE },
	{ add_uuid,                MGMT_ADD_UUID_SIZE },
	{ remove_uuid,             MGMT_REMOVE_UUID_SIZE },
	{ load_link_keys,          MGMT_LOAD_LINK_KEYS_SIZE,
						HCI_MGMT_VAR_LEN },
	{ load_long_term_keys,     MGMT_LOAD_LONG_TERM_KEYS_SIZE,
						HCI_MGMT_VAR_LEN },
	{ disconnect,              MGMT_DISCONNECT_SIZE },
	{ get_connections,         MGMT_GET_CONNECTIONS_SIZE },
	{ pin_code_reply,          MGMT_PIN_CODE_REPLY_SIZE },
	{ pin_code_neg_reply,      MGMT_PIN_CODE_NEG_REPLY_SIZE },
	{ set_io_capability,       MGMT_SET_IO_CAPABILITY_SIZE },
	{ pair_device,             MGMT_PAIR_DEVICE_SIZE },
	{ cancel_pair_device,      MGMT_CANCEL_PAIR_DEVICE_SIZE },
	{ unpair_device,           MGMT_UNPAIR_DEVICE_SIZE },
	{ user_confirm_reply,      MGMT_USER_CONFIRM_REPLY_SIZE },
	{ user_confirm_neg_reply,  MGMT_USER_CONFIRM_NEG_REPLY_SIZE },
	{ user_passkey_reply,      MGMT_USER_PASSKEY_REPLY_SIZE },
	{ user_passkey_neg_reply,  MGMT_USER_PASSKEY_NEG_REPLY_SIZE },
	{ read_local_oob_data,     MGMT_READ_LOCAL_OOB_DATA_SIZE },
	{ add_remote_oob_data,     MGMT_ADD_REMOTE_OOB_DATA_SIZE,
						HCI_MGMT_VAR_LEN },
	{ remove_remote_oob_data,  MGMT_REMOVE_REMOTE_OOB_DATA_SIZE },
	{ start_discovery,         MGMT_START_DISCOVERY_SIZE },
	{ stop_discovery,          MGMT_STOP_DISCOVERY_SIZE },
	{ confirm_name,            MGMT_CONFIRM_NAME_SIZE },
	{ block_device,            MGMT_BLOCK_DEVICE_SIZE },
	{ unblock_device,          MGMT_UNBLOCK_DEVICE_SIZE },
	{ set_device_id,           MGMT_SET_DEVICE_ID_SIZE },
	{ set_advertising,         MGMT_SETTING_SIZE },
	{ set_bredr,               MGMT_SETTING_SIZE },
	{ set_static_address,      MGMT_SET_STATIC_ADDRESS_SIZE },
	{ set_scan_params,         MGMT_SET_SCAN_PARAMS_SIZE },
	{ set_secure_conn,         MGMT_SETTING_SIZE },
	{ set_debug_keys,          MGMT_SETTING_SIZE },
	{ set_privacy,             MGMT_SET_PRIVACY_SIZE },
	{ load_irks,               MGMT_LOAD_IRKS_SIZE,
						HCI_MGMT_VAR_LEN },
	{ get_conn_info,           MGMT_GET_CONN_INFO_SIZE },
	{ get_clock_info,          MGMT_GET_CLOCK_INFO_SIZE },
	{ add_device,              MGMT_ADD_DEVICE_SIZE },
	{ remove_device,           MGMT_REMOVE_DEVICE_SIZE },
	{ load_conn_param,         MGMT_LOAD_CONN_PARAM_SIZE,
						HCI_MGMT_VAR_LEN },
	{ read_unconf_index_list,  MGMT_READ_UNCONF_INDEX_LIST_SIZE,
						HCI_MGMT_NO_HDEV |
						HCI_MGMT_UNTRUSTED },
	{ read_config_info,        MGMT_READ_CONFIG_INFO_SIZE,
						HCI_MGMT_UNCONFIGURED |
						HCI_MGMT_UNTRUSTED },
	{ set_external_config,     MGMT_SET_EXTERNAL_CONFIG_SIZE,
						HCI_MGMT_UNCONFIGURED },
	{ set_public_address,      MGMT_SET_PUBLIC_ADDRESS_SIZE,
						HCI_MGMT_UNCONFIGURED },
	{ start_service_discovery, MGMT_START_SERVICE_DISCOVERY_SIZE,
						HCI_MGMT_VAR_LEN },
	{ read_local_oob_ext_data, MGMT_READ_LOCAL_OOB_EXT_DATA_SIZE },
	{ read_ext_index_list,     MGMT_READ_EXT_INDEX_LIST_SIZE,
						HCI_MGMT_NO_HDEV |
						HCI_MGMT_UNTRUSTED },
	{ read_adv_features,       MGMT_READ_ADV_FEATURES_SIZE },
	{ add_advertising,	   MGMT_ADD_ADVERTISING_SIZE,
						HCI_MGMT_VAR_LEN },
	{ remove_advertising,	   MGMT_REMOVE_ADVERTISING_SIZE },
	{ get_adv_size_info,       MGMT_GET_ADV_SIZE_INFO_SIZE },
	{ start_limited_discovery, MGMT_START_DISCOVERY_SIZE },
	{ read_ext_controller_info,MGMT_READ_EXT_INFO_SIZE,
						HCI_MGMT_UNTRUSTED },
	{ set_appearance,	   MGMT_SET_APPEARANCE_SIZE },
	{ get_phy_configuration,   MGMT_GET_PHY_CONFIGURATION_SIZE },
	{ set_phy_configuration,   MGMT_SET_PHY_CONFIGURATION_SIZE },
	{ set_blocked_keys,	   MGMT_OP_SET_BLOCKED_KEYS_SIZE,
						HCI_MGMT_VAR_LEN },
	{ set_wideband_speech,	   MGMT_SETTING_SIZE },
	{ read_controller_cap,     MGMT_READ_CONTROLLER_CAP_SIZE,
						HCI_MGMT_UNTRUSTED },
	{ read_exp_features_info,  MGMT_READ_EXP_FEATURES_INFO_SIZE,
						HCI_MGMT_UNTRUSTED |
						HCI_MGMT_HDEV_OPTIONAL },
	{ set_exp_feature,         MGMT_SET_EXP_FEATURE_SIZE,
						HCI_MGMT_VAR_LEN |
						HCI_MGMT_HDEV_OPTIONAL },
	{ read_def_system_config,  MGMT_READ_DEF_SYSTEM_CONFIG_SIZE,
						HCI_MGMT_UNTRUSTED },
	{ set_def_system_config,   MGMT_SET_DEF_SYSTEM_CONFIG_SIZE,
						HCI_MGMT_VAR_LEN },
	{ read_def_runtime_config, MGMT_READ_DEF_RUNTIME_CONFIG_SIZE,
						HCI_MGMT_UNTRUSTED },
	{ set_def_runtime_config,  MGMT_SET_DEF_RUNTIME_CONFIG_SIZE,
						HCI_MGMT_VAR_LEN },
	{ get_device_flags,        MGMT_GET_DEVICE_FLAGS_SIZE },
	{ set_device_flags,        MGMT_SET_DEVICE_FLAGS_SIZE },
	{ read_adv_mon_features,   MGMT_READ_ADV_MONITOR_FEATURES_SIZE },
	{ add_adv_patterns_monitor,MGMT_ADD_ADV_PATTERNS_MONITOR_SIZE,
						HCI_MGMT_VAR_LEN },
	{ remove_adv_monitor,      MGMT_REMOVE_ADV_MONITOR_SIZE },
	{ add_ext_adv_params,      MGMT_ADD_EXT_ADV_PARAMS_MIN_SIZE,
						HCI_MGMT_VAR_LEN },
	{ add_ext_adv_data,        MGMT_ADD_EXT_ADV_DATA_SIZE,
						HCI_MGMT_VAR_LEN },
	{ add_adv_patterns_monitor_rssi,
				   MGMT_ADD_ADV_PATTERNS_MONITOR_RSSI_SIZE,
						HCI_MGMT_VAR_LEN },
};

void mgmt_index_added(struct hci_dev *hdev)
{
	struct mgmt_ev_ext_index ev;

	if (test_bit(HCI_QUIRK_RAW_DEVICE, &hdev->quirks))
		return;

	switch (hdev->dev_type) {
	case HCI_PRIMARY:
		if (hci_dev_test_flag(hdev, HCI_UNCONFIGURED)) {
			mgmt_index_event(MGMT_EV_UNCONF_INDEX_ADDED, hdev,
					 NULL, 0, HCI_MGMT_UNCONF_INDEX_EVENTS);
			ev.type = 0x01;
		} else {
			mgmt_index_event(MGMT_EV_INDEX_ADDED, hdev, NULL, 0,
					 HCI_MGMT_INDEX_EVENTS);
			ev.type = 0x00;
		}
		break;
	case HCI_AMP:
		ev.type = 0x02;
		break;
	default:
		return;
	}

	ev.bus = hdev->bus;

	mgmt_index_event(MGMT_EV_EXT_INDEX_ADDED, hdev, &ev, sizeof(ev),
			 HCI_MGMT_EXT_INDEX_EVENTS);
}

void mgmt_index_removed(struct hci_dev *hdev)
{
	struct mgmt_ev_ext_index ev;
	u8 status = MGMT_STATUS_INVALID_INDEX;

	if (test_bit(HCI_QUIRK_RAW_DEVICE, &hdev->quirks))
		return;

	switch (hdev->dev_type) {
	case HCI_PRIMARY:
		mgmt_pending_foreach(0, hdev, cmd_complete_rsp, &status);

		if (hci_dev_test_flag(hdev, HCI_UNCONFIGURED)) {
			mgmt_index_event(MGMT_EV_UNCONF_INDEX_REMOVED, hdev,
					 NULL, 0, HCI_MGMT_UNCONF_INDEX_EVENTS);
			ev.type = 0x01;
		} else {
			mgmt_index_event(MGMT_EV_INDEX_REMOVED, hdev, NULL, 0,
					 HCI_MGMT_INDEX_EVENTS);
			ev.type = 0x00;
		}
		break;
	case HCI_AMP:
		ev.type = 0x02;
		break;
	default:
		return;
	}

	ev.bus = hdev->bus;

	mgmt_index_event(MGMT_EV_EXT_INDEX_REMOVED, hdev, &ev, sizeof(ev),
			 HCI_MGMT_EXT_INDEX_EVENTS);
}

void mgmt_power_on(struct hci_dev *hdev, int err)
{
	struct cmd_lookup match = { NULL, hdev };

	bt_dev_dbg(hdev, "err %d", err);

	hci_dev_lock(hdev);

	if (!err) {
		restart_le_actions(hdev);
		hci_update_passive_scan(hdev);
	}

	mgmt_pending_foreach(MGMT_OP_SET_POWERED, hdev, settings_rsp, &match);

	new_settings(hdev, match.sk);

	if (match.sk)
		sock_put(match.sk);

	hci_dev_unlock(hdev);
}

void __mgmt_power_off(struct hci_dev *hdev)
{
	struct cmd_lookup match = { NULL, hdev };
	u8 status, zero_cod[] = { 0, 0, 0 };

	mgmt_pending_foreach(MGMT_OP_SET_POWERED, hdev, settings_rsp, &match);

	/* If the power off is because of hdev unregistration let
	 * use the appropriate INVALID_INDEX status. Otherwise use
	 * NOT_POWERED. We cover both scenarios here since later in
	 * mgmt_index_removed() any hci_conn callbacks will have already
	 * been triggered, potentially causing misleading DISCONNECTED
	 * status responses.
	 */
	if (hci_dev_test_flag(hdev, HCI_UNREGISTER))
		status = MGMT_STATUS_INVALID_INDEX;
	else
		status = MGMT_STATUS_NOT_POWERED;

	mgmt_pending_foreach(0, hdev, cmd_complete_rsp, &status);

	if (memcmp(hdev->dev_class, zero_cod, sizeof(zero_cod)) != 0) {
		mgmt_limited_event(MGMT_EV_CLASS_OF_DEV_CHANGED, hdev,
				   zero_cod, sizeof(zero_cod),
				   HCI_MGMT_DEV_CLASS_EVENTS, NULL);
		ext_info_changed(hdev, NULL);
	}

	new_settings(hdev, match.sk);

	if (match.sk)
		sock_put(match.sk);
}

void mgmt_set_powered_failed(struct hci_dev *hdev, int err)
{
	struct mgmt_pending_cmd *cmd;
	u8 status;

	cmd = pending_find(MGMT_OP_SET_POWERED, hdev);
	if (!cmd)
		return;

	if (err == -ERFKILL)
		status = MGMT_STATUS_RFKILLED;
	else
		status = MGMT_STATUS_FAILED;

	mgmt_cmd_status(cmd->sk, hdev->id, MGMT_OP_SET_POWERED, status);

	mgmt_pending_remove(cmd);
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
	case SMP_LTK_RESPONDER:
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
	 * without providing an identity resolving key don't require
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
		ev.key.initiator = 1;

	/* Make sure we copy only the significant bytes based on the
	 * encryption key size, and set the rest of the value to zeroes.
	 */
	memcpy(ev.key.val, key->val, key->enc_size);
	memset(ev.key.val + key->enc_size, 0,
	       sizeof(ev.key.val) - key->enc_size);

	mgmt_event(MGMT_EV_NEW_LONG_TERM_KEY, hdev, &ev, sizeof(ev), NULL);
}

void mgmt_new_irk(struct hci_dev *hdev, struct smp_irk *irk, bool persistent)
{
	struct mgmt_ev_new_irk ev;

	memset(&ev, 0, sizeof(ev));

	ev.store_hint = persistent;

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
	 * without providing an identity resolving key don't require
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
	ev.key.type = csrk->type;
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

void mgmt_device_connected(struct hci_dev *hdev, struct hci_conn *conn,
			   u8 *name, u8 name_len)
{
	char buf[512];
	struct mgmt_ev_device_connected *ev = (void *) buf;
	u16 eir_len = 0;
	u32 flags = 0;

	bacpy(&ev->addr.bdaddr, &conn->dst);
	ev->addr.type = link_to_bdaddr(conn->type, conn->dst_type);

	if (conn->out)
		flags |= MGMT_DEV_FOUND_INITIATED_CONN;

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

static void disconnect_rsp(struct mgmt_pending_cmd *cmd, void *data)
{
	struct sock **sk = data;

	cmd->cmd_complete(cmd, 0);

	*sk = cmd->sk;
	sock_hold(*sk);

	mgmt_pending_remove(cmd);
}

static void unpair_device_rsp(struct mgmt_pending_cmd *cmd, void *data)
{
	struct hci_dev *hdev = data;
	struct mgmt_cp_unpair_device *cp = cmd->param;

	device_unpaired(hdev, &cp->addr.bdaddr, cp->addr.type, cmd->sk);

	cmd->cmd_complete(cmd, 0);
	mgmt_pending_remove(cmd);
}

bool mgmt_powering_down(struct hci_dev *hdev)
{
	struct mgmt_pending_cmd *cmd;
	struct mgmt_mode *cp;

	cmd = pending_find(MGMT_OP_SET_POWERED, hdev);
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

	/* Report disconnects due to suspend */
	if (hdev->suspended)
		ev.reason = MGMT_DEV_DISCONN_LOCAL_HOST_SUSPEND;

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
	struct mgmt_pending_cmd *cmd;

	mgmt_pending_foreach(MGMT_OP_UNPAIR_DEVICE, hdev, unpair_device_rsp,
			     hdev);

	cmd = pending_find(MGMT_OP_DISCONNECT, hdev);
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
	struct mgmt_pending_cmd *cmd;

	cmd = pending_find(MGMT_OP_PIN_CODE_REPLY, hdev);
	if (!cmd)
		return;

	cmd->cmd_complete(cmd, mgmt_status(status));
	mgmt_pending_remove(cmd);
}

void mgmt_pin_code_neg_reply_complete(struct hci_dev *hdev, bdaddr_t *bdaddr,
				      u8 status)
{
	struct mgmt_pending_cmd *cmd;

	cmd = pending_find(MGMT_OP_PIN_CODE_NEG_REPLY, hdev);
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

	bt_dev_dbg(hdev, "bdaddr %pMR", bdaddr);

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

	bt_dev_dbg(hdev, "bdaddr %pMR", bdaddr);

	bacpy(&ev.addr.bdaddr, bdaddr);
	ev.addr.type = link_to_bdaddr(link_type, addr_type);

	return mgmt_event(MGMT_EV_USER_PASSKEY_REQUEST, hdev, &ev, sizeof(ev),
			  NULL);
}

static int user_pairing_resp_complete(struct hci_dev *hdev, bdaddr_t *bdaddr,
				      u8 link_type, u8 addr_type, u8 status,
				      u8 opcode)
{
	struct mgmt_pending_cmd *cmd;

	cmd = pending_find(opcode, hdev);
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

	bt_dev_dbg(hdev, "bdaddr %pMR", bdaddr);

	bacpy(&ev.addr.bdaddr, bdaddr);
	ev.addr.type = link_to_bdaddr(link_type, addr_type);
	ev.passkey = __cpu_to_le32(passkey);
	ev.entered = entered;

	return mgmt_event(MGMT_EV_PASSKEY_NOTIFY, hdev, &ev, sizeof(ev), NULL);
}

void mgmt_auth_failed(struct hci_conn *conn, u8 hci_status)
{
	struct mgmt_ev_auth_failed ev;
	struct mgmt_pending_cmd *cmd;
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
		changed = !hci_dev_test_and_set_flag(hdev, HCI_LINK_SECURITY);
	else
		changed = hci_dev_test_and_clear_flag(hdev, HCI_LINK_SECURITY);

	mgmt_pending_foreach(MGMT_OP_SET_LINK_SECURITY, hdev, settings_rsp,
			     &match);

	if (changed)
		new_settings(hdev, match.sk);

	if (match.sk)
		sock_put(match.sk);
}

static void sk_lookup(struct mgmt_pending_cmd *cmd, void *data)
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

	if (!status) {
		mgmt_limited_event(MGMT_EV_CLASS_OF_DEV_CHANGED, hdev, dev_class,
				   3, HCI_MGMT_DEV_CLASS_EVENTS, NULL);
		ext_info_changed(hdev, NULL);
	}

	if (match.sk)
		sock_put(match.sk);
}

void mgmt_set_local_name_complete(struct hci_dev *hdev, u8 *name, u8 status)
{
	struct mgmt_cp_set_local_name ev;
	struct mgmt_pending_cmd *cmd;

	if (status)
		return;

	memset(&ev, 0, sizeof(ev));
	memcpy(ev.name, name, HCI_MAX_NAME_LENGTH);
	memcpy(ev.short_name, hdev->short_name, HCI_MAX_SHORT_NAME_LENGTH);

	cmd = pending_find(MGMT_OP_SET_LOCAL_NAME, hdev);
	if (!cmd) {
		memcpy(hdev->dev_name, name, sizeof(hdev->dev_name));

		/* If this is a HCI command related to powering on the
		 * HCI dev don't send any mgmt signals.
		 */
		if (pending_find(MGMT_OP_SET_POWERED, hdev))
			return;
	}

	mgmt_limited_event(MGMT_EV_LOCAL_NAME_CHANGED, hdev, &ev, sizeof(ev),
			   HCI_MGMT_LOCAL_NAME_EVENTS, cmd ? cmd->sk : NULL);
	ext_info_changed(hdev, cmd ? cmd->sk : NULL);
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

static void restart_le_scan(struct hci_dev *hdev)
{
	/* If controller is not scanning we are done. */
	if (!hci_dev_test_flag(hdev, HCI_LE_SCAN))
		return;

	if (time_after(jiffies + DISCOV_LE_RESTART_DELAY,
		       hdev->discovery.scan_start +
		       hdev->discovery.scan_duration))
		return;

	queue_delayed_work(hdev->req_workqueue, &hdev->le_scan_restart,
			   DISCOV_LE_RESTART_DELAY);
}

static bool is_filter_match(struct hci_dev *hdev, s8 rssi, u8 *eir,
			    u16 eir_len, u8 *scan_rsp, u8 scan_rsp_len)
{
	/* If a RSSI threshold has been specified, and
	 * HCI_QUIRK_STRICT_DUPLICATE_FILTER is not set, then all results with
	 * a RSSI smaller than the RSSI threshold will be dropped. If the quirk
	 * is set, let it through for further processing, as we might need to
	 * restart the scan.
	 *
	 * For BR/EDR devices (pre 1.2) providing no RSSI during inquiry,
	 * the results are also dropped.
	 */
	if (hdev->discovery.rssi != HCI_RSSI_INVALID &&
	    (rssi == HCI_RSSI_INVALID ||
	    (rssi < hdev->discovery.rssi &&
	     !test_bit(HCI_QUIRK_STRICT_DUPLICATE_FILTER, &hdev->quirks))))
		return  false;

	if (hdev->discovery.uuid_count != 0) {
		/* If a list of UUIDs is provided in filter, results with no
		 * matching UUID should be dropped.
		 */
		if (!eir_has_uuids(eir, eir_len, hdev->discovery.uuid_count,
				   hdev->discovery.uuids) &&
		    !eir_has_uuids(scan_rsp, scan_rsp_len,
				   hdev->discovery.uuid_count,
				   hdev->discovery.uuids))
			return false;
	}

	/* If duplicate filtering does not report RSSI changes, then restart
	 * scanning to ensure updated result with updated RSSI values.
	 */
	if (test_bit(HCI_QUIRK_STRICT_DUPLICATE_FILTER, &hdev->quirks)) {
		restart_le_scan(hdev);

		/* Validate RSSI value against the RSSI threshold once more. */
		if (hdev->discovery.rssi != HCI_RSSI_INVALID &&
		    rssi < hdev->discovery.rssi)
			return false;
	}

	return true;
}

void mgmt_device_found(struct hci_dev *hdev, bdaddr_t *bdaddr, u8 link_type,
		       u8 addr_type, u8 *dev_class, s8 rssi, u32 flags,
		       u8 *eir, u16 eir_len, u8 *scan_rsp, u8 scan_rsp_len)
{
	char buf[512];
	struct mgmt_ev_device_found *ev = (void *)buf;
	size_t ev_size;

	/* Don't send events for a non-kernel initiated discovery. With
	 * LE one exception is if we have pend_le_reports > 0 in which
	 * case we're doing passive scanning and want these events.
	 */
	if (!hci_discovery_active(hdev)) {
		if (link_type == ACL_LINK)
			return;
		if (link_type == LE_LINK &&
		    list_empty(&hdev->pend_le_reports) &&
		    !hci_is_adv_monitoring(hdev)) {
			return;
		}
	}

	if (hdev->discovery.result_filtering) {
		/* We are using service discovery */
		if (!is_filter_match(hdev, rssi, eir, eir_len, scan_rsp,
				     scan_rsp_len))
			return;
	}

	if (hdev->discovery.limited) {
		/* Check for limited discoverable bit */
		if (dev_class) {
			if (!(dev_class[1] & 0x20))
				return;
		} else {
			u8 *flags = eir_get_data(eir, eir_len, EIR_FLAGS, NULL);
			if (!flags || !(flags[0] & LE_AD_LIMITED))
				return;
		}
	}

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
	if (rssi == HCI_RSSI_INVALID && !hdev->discovery.report_invalid_rssi &&
	    link_type == ACL_LINK)
		rssi = 0;

	bacpy(&ev->addr.bdaddr, bdaddr);
	ev->addr.type = link_to_bdaddr(link_type, addr_type);
	ev->rssi = rssi;
	ev->flags = cpu_to_le32(flags);

	if (eir_len > 0)
		/* Copy EIR or advertising data into event */
		memcpy(ev->eir, eir, eir_len);

	if (dev_class && !eir_get_data(ev->eir, eir_len, EIR_CLASS_OF_DEV,
				       NULL))
		eir_len = eir_append_data(ev->eir, eir_len, EIR_CLASS_OF_DEV,
					  dev_class, 3);

	if (scan_rsp_len > 0)
		/* Append scan response data to event */
		memcpy(ev->eir + eir_len, scan_rsp, scan_rsp_len);

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

	bt_dev_dbg(hdev, "discovering %u", discovering);

	memset(&ev, 0, sizeof(ev));
	ev.type = hdev->discovery.type;
	ev.discovering = discovering;

	mgmt_event(MGMT_EV_DISCOVERING, hdev, &ev, sizeof(ev), NULL);
}

void mgmt_suspending(struct hci_dev *hdev, u8 state)
{
	struct mgmt_ev_controller_suspend ev;

	ev.suspend_state = state;
	mgmt_event(MGMT_EV_CONTROLLER_SUSPEND, hdev, &ev, sizeof(ev), NULL);
}

void mgmt_resuming(struct hci_dev *hdev, u8 reason, bdaddr_t *bdaddr,
		   u8 addr_type)
{
	struct mgmt_ev_controller_resume ev;

	ev.wake_reason = reason;
	if (bdaddr) {
		bacpy(&ev.addr.bdaddr, bdaddr);
		ev.addr.type = addr_type;
	} else {
		memset(&ev.addr, 0, sizeof(ev.addr));
	}

	mgmt_event(MGMT_EV_CONTROLLER_RESUME, hdev, &ev, sizeof(ev), NULL);
}

static struct hci_mgmt_chan chan = {
	.channel	= HCI_CHANNEL_CONTROL,
	.handler_count	= ARRAY_SIZE(mgmt_handlers),
	.handlers	= mgmt_handlers,
	.hdev_init	= mgmt_init_hdev,
};

int mgmt_init(void)
{
	return hci_mgmt_chan_register(&chan);
}

void mgmt_exit(void)
{
	hci_mgmt_chan_unregister(&chan);
}
