/*
   BlueZ - Bluetooth protocol stack for Linux
   Copyright (C) 2010  Nokia Corporation

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

#include <linux/kernel.h>
#include <linux/uaccess.h>
#include <linux/module.h>
#include <asm/unaligned.h>

#include <net/bluetooth/bluetooth.h>
#include <net/bluetooth/hci_core.h>
#include <net/bluetooth/mgmt.h>
#include <net/bluetooth/smp.h>

#define MGMT_VERSION	0
#define MGMT_REVISION	1

/*
 * These LE scan and inquiry parameters were chosen according to LE General
 * Discovery Procedure specification.
 */
#define LE_SCAN_TYPE			0x01
#define LE_SCAN_WIN			0x12
#define LE_SCAN_INT			0x12
#define LE_SCAN_TIMEOUT_LE_ONLY		10240	/* TGAP(gen_disc_scan_min) */

#define INQUIRY_LEN_BREDR		0x08	/* TGAP(100) */

#define SERVICE_CACHE_TIMEOUT (5 * 1000)

struct pending_cmd {
	struct list_head list;
	u16 opcode;
	int index;
	void *param;
	struct sock *sk;
	void *user_data;
};

/* HCI to MGMT error code conversion table */
static u8 mgmt_status_table[] = {
	MGMT_STATUS_SUCCESS,
	MGMT_STATUS_UNKNOWN_COMMAND,	/* Unknown Command */
	MGMT_STATUS_NOT_CONNECTED,	/* No Connection */
	MGMT_STATUS_FAILED,		/* Hardware Failure */
	MGMT_STATUS_CONNECT_FAILED,	/* Page Timeout */
	MGMT_STATUS_AUTH_FAILED,	/* Authentication Failed */
	MGMT_STATUS_NOT_PAIRED,		/* PIN or Key Missing */
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

static int cmd_status(struct sock *sk, u16 index, u16 cmd, u8 status)
{
	struct sk_buff *skb;
	struct mgmt_hdr *hdr;
	struct mgmt_ev_cmd_status *ev;
	int err;

	BT_DBG("sock %p, index %u, cmd %u, status %u", sk, index, cmd, status);

	skb = alloc_skb(sizeof(*hdr) + sizeof(*ev), GFP_ATOMIC);
	if (!skb)
		return -ENOMEM;

	hdr = (void *) skb_put(skb, sizeof(*hdr));

	hdr->opcode = cpu_to_le16(MGMT_EV_CMD_STATUS);
	hdr->index = cpu_to_le16(index);
	hdr->len = cpu_to_le16(sizeof(*ev));

	ev = (void *) skb_put(skb, sizeof(*ev));
	ev->status = status;
	put_unaligned_le16(cmd, &ev->opcode);

	err = sock_queue_rcv_skb(sk, skb);
	if (err < 0)
		kfree_skb(skb);

	return err;
}

static int cmd_complete(struct sock *sk, u16 index, u16 cmd, void *rp,
								size_t rp_len)
{
	struct sk_buff *skb;
	struct mgmt_hdr *hdr;
	struct mgmt_ev_cmd_complete *ev;
	int err;

	BT_DBG("sock %p", sk);

	skb = alloc_skb(sizeof(*hdr) + sizeof(*ev) + rp_len, GFP_ATOMIC);
	if (!skb)
		return -ENOMEM;

	hdr = (void *) skb_put(skb, sizeof(*hdr));

	hdr->opcode = cpu_to_le16(MGMT_EV_CMD_COMPLETE);
	hdr->index = cpu_to_le16(index);
	hdr->len = cpu_to_le16(sizeof(*ev) + rp_len);

	ev = (void *) skb_put(skb, sizeof(*ev) + rp_len);
	put_unaligned_le16(cmd, &ev->opcode);

	if (rp)
		memcpy(ev->data, rp, rp_len);

	err = sock_queue_rcv_skb(sk, skb);
	if (err < 0)
		kfree_skb(skb);

	return err;;
}

static int read_version(struct sock *sk)
{
	struct mgmt_rp_read_version rp;

	BT_DBG("sock %p", sk);

	rp.version = MGMT_VERSION;
	put_unaligned_le16(MGMT_REVISION, &rp.revision);

	return cmd_complete(sk, MGMT_INDEX_NONE, MGMT_OP_READ_VERSION, &rp,
								sizeof(rp));
}

static int read_index_list(struct sock *sk)
{
	struct mgmt_rp_read_index_list *rp;
	struct list_head *p;
	struct hci_dev *d;
	size_t rp_len;
	u16 count;
	int i, err;

	BT_DBG("sock %p", sk);

	read_lock(&hci_dev_list_lock);

	count = 0;
	list_for_each(p, &hci_dev_list) {
		count++;
	}

	rp_len = sizeof(*rp) + (2 * count);
	rp = kmalloc(rp_len, GFP_ATOMIC);
	if (!rp) {
		read_unlock(&hci_dev_list_lock);
		return -ENOMEM;
	}

	put_unaligned_le16(count, &rp->num_controllers);

	i = 0;
	list_for_each_entry(d, &hci_dev_list, list) {
		if (test_and_clear_bit(HCI_AUTO_OFF, &d->dev_flags))
			cancel_delayed_work(&d->power_off);

		if (test_bit(HCI_SETUP, &d->dev_flags))
			continue;

		put_unaligned_le16(d->id, &rp->index[i++]);
		BT_DBG("Added hci%u", d->id);
	}

	read_unlock(&hci_dev_list_lock);

	err = cmd_complete(sk, MGMT_INDEX_NONE, MGMT_OP_READ_INDEX_LIST, rp,
									rp_len);

	kfree(rp);

	return err;
}

static u32 get_supported_settings(struct hci_dev *hdev)
{
	u32 settings = 0;

	settings |= MGMT_SETTING_POWERED;
	settings |= MGMT_SETTING_CONNECTABLE;
	settings |= MGMT_SETTING_FAST_CONNECTABLE;
	settings |= MGMT_SETTING_DISCOVERABLE;
	settings |= MGMT_SETTING_PAIRABLE;

	if (hdev->features[6] & LMP_SIMPLE_PAIR)
		settings |= MGMT_SETTING_SSP;

	if (!(hdev->features[4] & LMP_NO_BREDR)) {
		settings |= MGMT_SETTING_BREDR;
		settings |= MGMT_SETTING_LINK_SECURITY;
	}

	if (hdev->features[4] & LMP_LE)
		settings |= MGMT_SETTING_LE;

	return settings;
}

static u32 get_current_settings(struct hci_dev *hdev)
{
	u32 settings = 0;

	if (test_bit(HCI_UP, &hdev->flags))
		settings |= MGMT_SETTING_POWERED;
	else
		return settings;

	if (test_bit(HCI_PSCAN, &hdev->flags))
		settings |= MGMT_SETTING_CONNECTABLE;

	if (test_bit(HCI_ISCAN, &hdev->flags))
		settings |= MGMT_SETTING_DISCOVERABLE;

	if (test_bit(HCI_PAIRABLE, &hdev->dev_flags))
		settings |= MGMT_SETTING_PAIRABLE;

	if (!(hdev->features[4] & LMP_NO_BREDR))
		settings |= MGMT_SETTING_BREDR;

	if (hdev->host_features[0] & LMP_HOST_LE)
		settings |= MGMT_SETTING_LE;

	if (test_bit(HCI_AUTH, &hdev->flags))
		settings |= MGMT_SETTING_LINK_SECURITY;

	if (test_bit(HCI_SSP_ENABLED, &hdev->dev_flags))
		settings |= MGMT_SETTING_SSP;

	return settings;
}

#define PNP_INFO_SVCLASS_ID		0x1200

static u8 bluetooth_base_uuid[] = {
			0xFB, 0x34, 0x9B, 0x5F, 0x80, 0x00, 0x00, 0x80,
			0x00, 0x10, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
};

static u16 get_uuid16(u8 *uuid128)
{
	u32 val;
	int i;

	for (i = 0; i < 12; i++) {
		if (bluetooth_base_uuid[i] != uuid128[i])
			return 0;
	}

	memcpy(&val, &uuid128[12], 4);

	val = le32_to_cpu(val);
	if (val > 0xffff)
		return 0;

	return (u16) val;
}

static void create_eir(struct hci_dev *hdev, u8 *data)
{
	u8 *ptr = data;
	u16 eir_len = 0;
	u16 uuid16_list[HCI_MAX_EIR_LENGTH / sizeof(u16)];
	int i, truncated = 0;
	struct bt_uuid *uuid;
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

		eir_len += (name_len + 2);
		ptr += (name_len + 2);
	}

	memset(uuid16_list, 0, sizeof(uuid16_list));

	/* Group all UUID16 types */
	list_for_each_entry(uuid, &hdev->uuids, list) {
		u16 uuid16;

		uuid16 = get_uuid16(uuid->uuid);
		if (uuid16 == 0)
			return;

		if (uuid16 < 0x1100)
			continue;

		if (uuid16 == PNP_INFO_SVCLASS_ID)
			continue;

		/* Stop if not enough space to put next UUID */
		if (eir_len + 2 + sizeof(u16) > HCI_MAX_EIR_LENGTH) {
			truncated = 1;
			break;
		}

		/* Check for duplicates */
		for (i = 0; uuid16_list[i] != 0; i++)
			if (uuid16_list[i] == uuid16)
				break;

		if (uuid16_list[i] == 0) {
			uuid16_list[i] = uuid16;
			eir_len += sizeof(u16);
		}
	}

	if (uuid16_list[0] != 0) {
		u8 *length = ptr;

		/* EIR Data type */
		ptr[1] = truncated ? EIR_UUID16_SOME : EIR_UUID16_ALL;

		ptr += 2;
		eir_len += 2;

		for (i = 0; uuid16_list[i] != 0; i++) {
			*ptr++ = (uuid16_list[i] & 0x00ff);
			*ptr++ = (uuid16_list[i] & 0xff00) >> 8;
		}

		/* EIR Data length */
		*length = (i * sizeof(u16)) + 1;
	}
}

static int update_eir(struct hci_dev *hdev)
{
	struct hci_cp_write_eir cp;

	if (!(hdev->features[6] & LMP_EXT_INQ))
		return 0;

	if (!test_bit(HCI_SSP_ENABLED, &hdev->dev_flags))
		return 0;

	if (test_bit(HCI_SERVICE_CACHE, &hdev->dev_flags))
		return 0;

	memset(&cp, 0, sizeof(cp));

	create_eir(hdev, cp.data);

	if (memcmp(cp.data, hdev->eir, sizeof(cp.data)) == 0)
		return 0;

	memcpy(hdev->eir, cp.data, sizeof(cp.data));

	return hci_send_cmd(hdev, HCI_OP_WRITE_EIR, sizeof(cp), &cp);
}

static u8 get_service_classes(struct hci_dev *hdev)
{
	struct bt_uuid *uuid;
	u8 val = 0;

	list_for_each_entry(uuid, &hdev->uuids, list)
		val |= uuid->svc_hint;

	return val;
}

static int update_class(struct hci_dev *hdev)
{
	u8 cod[3];

	BT_DBG("%s", hdev->name);

	if (test_bit(HCI_SERVICE_CACHE, &hdev->dev_flags))
		return 0;

	cod[0] = hdev->minor_class;
	cod[1] = hdev->major_class;
	cod[2] = get_service_classes(hdev);

	if (memcmp(cod, hdev->dev_class, 3) == 0)
		return 0;

	return hci_send_cmd(hdev, HCI_OP_WRITE_CLASS_OF_DEV, sizeof(cod), cod);
}

static void service_cache_off(struct work_struct *work)
{
	struct hci_dev *hdev = container_of(work, struct hci_dev,
							service_cache.work);

	if (!test_and_clear_bit(HCI_SERVICE_CACHE, &hdev->dev_flags))
		return;

	hci_dev_lock(hdev);

	update_eir(hdev);
	update_class(hdev);

	hci_dev_unlock(hdev);
}

static void mgmt_init_hdev(struct hci_dev *hdev)
{
	if (!test_and_set_bit(HCI_MGMT, &hdev->dev_flags))
		INIT_DELAYED_WORK(&hdev->service_cache, service_cache_off);

	if (!test_and_set_bit(HCI_SERVICE_CACHE, &hdev->dev_flags))
		schedule_delayed_work(&hdev->service_cache,
				msecs_to_jiffies(SERVICE_CACHE_TIMEOUT));
}

static int read_controller_info(struct sock *sk, u16 index)
{
	struct mgmt_rp_read_info rp;
	struct hci_dev *hdev;

	BT_DBG("sock %p hci%u", sk, index);

	hdev = hci_dev_get(index);
	if (!hdev)
		return cmd_status(sk, index, MGMT_OP_READ_INFO,
						MGMT_STATUS_INVALID_PARAMS);

	if (test_and_clear_bit(HCI_AUTO_OFF, &hdev->dev_flags))
		cancel_delayed_work_sync(&hdev->power_off);

	hci_dev_lock(hdev);

	if (test_and_clear_bit(HCI_PI_MGMT_INIT, &hci_pi(sk)->flags))
		mgmt_init_hdev(hdev);

	memset(&rp, 0, sizeof(rp));

	bacpy(&rp.bdaddr, &hdev->bdaddr);

	rp.version = hdev->hci_ver;

	put_unaligned_le16(hdev->manufacturer, &rp.manufacturer);

	rp.supported_settings = cpu_to_le32(get_supported_settings(hdev));
	rp.current_settings = cpu_to_le32(get_current_settings(hdev));

	memcpy(rp.dev_class, hdev->dev_class, 3);

	memcpy(rp.name, hdev->dev_name, sizeof(hdev->dev_name));

	hci_dev_unlock(hdev);
	hci_dev_put(hdev);

	return cmd_complete(sk, index, MGMT_OP_READ_INFO, &rp, sizeof(rp));
}

static void mgmt_pending_free(struct pending_cmd *cmd)
{
	sock_put(cmd->sk);
	kfree(cmd->param);
	kfree(cmd);
}

static struct pending_cmd *mgmt_pending_add(struct sock *sk, u16 opcode,
							struct hci_dev *hdev,
							void *data, u16 len)
{
	struct pending_cmd *cmd;

	cmd = kmalloc(sizeof(*cmd), GFP_ATOMIC);
	if (!cmd)
		return NULL;

	cmd->opcode = opcode;
	cmd->index = hdev->id;

	cmd->param = kmalloc(len, GFP_ATOMIC);
	if (!cmd->param) {
		kfree(cmd);
		return NULL;
	}

	if (data)
		memcpy(cmd->param, data, len);

	cmd->sk = sk;
	sock_hold(sk);

	list_add(&cmd->list, &hdev->mgmt_pending);

	return cmd;
}

static void mgmt_pending_foreach(u16 opcode, struct hci_dev *hdev,
				void (*cb)(struct pending_cmd *cmd, void *data),
				void *data)
{
	struct list_head *p, *n;

	list_for_each_safe(p, n, &hdev->mgmt_pending) {
		struct pending_cmd *cmd;

		cmd = list_entry(p, struct pending_cmd, list);

		if (opcode > 0 && cmd->opcode != opcode)
			continue;

		cb(cmd, data);
	}
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

static void mgmt_pending_remove(struct pending_cmd *cmd)
{
	list_del(&cmd->list);
	mgmt_pending_free(cmd);
}

static int send_settings_rsp(struct sock *sk, u16 opcode, struct hci_dev *hdev)
{
	__le32 settings = cpu_to_le32(get_current_settings(hdev));

	return cmd_complete(sk, hdev->id, opcode, &settings, sizeof(settings));
}

static int set_powered(struct sock *sk, u16 index, void *data, u16 len)
{
	struct mgmt_mode *cp = data;
	struct hci_dev *hdev;
	struct pending_cmd *cmd;
	int err, up;

	BT_DBG("request for hci%u", index);

	if (len != sizeof(*cp))
		return cmd_status(sk, index, MGMT_OP_SET_POWERED,
						MGMT_STATUS_INVALID_PARAMS);

	hdev = hci_dev_get(index);
	if (!hdev)
		return cmd_status(sk, index, MGMT_OP_SET_POWERED,
						MGMT_STATUS_INVALID_PARAMS);

	hci_dev_lock(hdev);

	up = test_bit(HCI_UP, &hdev->flags);
	if ((cp->val && up) || (!cp->val && !up)) {
		err = send_settings_rsp(sk, MGMT_OP_SET_POWERED, hdev);
		goto failed;
	}

	if (mgmt_pending_find(MGMT_OP_SET_POWERED, hdev)) {
		err = cmd_status(sk, index, MGMT_OP_SET_POWERED,
							MGMT_STATUS_BUSY);
		goto failed;
	}

	cmd = mgmt_pending_add(sk, MGMT_OP_SET_POWERED, hdev, data, len);
	if (!cmd) {
		err = -ENOMEM;
		goto failed;
	}

	if (cp->val)
		schedule_work(&hdev->power_on);
	else
		schedule_work(&hdev->power_off.work);

	err = 0;

failed:
	hci_dev_unlock(hdev);
	hci_dev_put(hdev);
	return err;
}

static int set_discoverable(struct sock *sk, u16 index, void *data, u16 len)
{
	struct mgmt_cp_set_discoverable *cp = data;
	struct hci_dev *hdev;
	struct pending_cmd *cmd;
	u8 scan;
	int err;

	BT_DBG("request for hci%u", index);

	if (len != sizeof(*cp))
		return cmd_status(sk, index, MGMT_OP_SET_DISCOVERABLE,
						MGMT_STATUS_INVALID_PARAMS);

	hdev = hci_dev_get(index);
	if (!hdev)
		return cmd_status(sk, index, MGMT_OP_SET_DISCOVERABLE,
						MGMT_STATUS_INVALID_PARAMS);

	hci_dev_lock(hdev);

	if (!test_bit(HCI_UP, &hdev->flags)) {
		err = cmd_status(sk, index, MGMT_OP_SET_DISCOVERABLE,
						MGMT_STATUS_NOT_POWERED);
		goto failed;
	}

	if (mgmt_pending_find(MGMT_OP_SET_DISCOVERABLE, hdev) ||
			mgmt_pending_find(MGMT_OP_SET_CONNECTABLE, hdev)) {
		err = cmd_status(sk, index, MGMT_OP_SET_DISCOVERABLE,
							MGMT_STATUS_BUSY);
		goto failed;
	}

	if (cp->val == test_bit(HCI_ISCAN, &hdev->flags) &&
					test_bit(HCI_PSCAN, &hdev->flags)) {
		err = send_settings_rsp(sk, MGMT_OP_SET_DISCOVERABLE, hdev);
		goto failed;
	}

	cmd = mgmt_pending_add(sk, MGMT_OP_SET_DISCOVERABLE, hdev, data, len);
	if (!cmd) {
		err = -ENOMEM;
		goto failed;
	}

	scan = SCAN_PAGE;

	if (cp->val)
		scan |= SCAN_INQUIRY;
	else
		cancel_delayed_work(&hdev->discov_off);

	err = hci_send_cmd(hdev, HCI_OP_WRITE_SCAN_ENABLE, 1, &scan);
	if (err < 0)
		mgmt_pending_remove(cmd);

	if (cp->val)
		hdev->discov_timeout = get_unaligned_le16(&cp->timeout);

failed:
	hci_dev_unlock(hdev);
	hci_dev_put(hdev);

	return err;
}

static int set_connectable(struct sock *sk, u16 index, void *data, u16 len)
{
	struct mgmt_mode *cp = data;
	struct hci_dev *hdev;
	struct pending_cmd *cmd;
	u8 scan;
	int err;

	BT_DBG("request for hci%u", index);

	if (len != sizeof(*cp))
		return cmd_status(sk, index, MGMT_OP_SET_CONNECTABLE,
						MGMT_STATUS_INVALID_PARAMS);

	hdev = hci_dev_get(index);
	if (!hdev)
		return cmd_status(sk, index, MGMT_OP_SET_CONNECTABLE,
						MGMT_STATUS_INVALID_PARAMS);

	hci_dev_lock(hdev);

	if (!test_bit(HCI_UP, &hdev->flags)) {
		err = cmd_status(sk, index, MGMT_OP_SET_CONNECTABLE,
						MGMT_STATUS_NOT_POWERED);
		goto failed;
	}

	if (mgmt_pending_find(MGMT_OP_SET_DISCOVERABLE, hdev) ||
			mgmt_pending_find(MGMT_OP_SET_CONNECTABLE, hdev)) {
		err = cmd_status(sk, index, MGMT_OP_SET_CONNECTABLE,
							MGMT_STATUS_BUSY);
		goto failed;
	}

	if (cp->val == test_bit(HCI_PSCAN, &hdev->flags)) {
		err = send_settings_rsp(sk, MGMT_OP_SET_CONNECTABLE, hdev);
		goto failed;
	}

	cmd = mgmt_pending_add(sk, MGMT_OP_SET_CONNECTABLE, hdev, data, len);
	if (!cmd) {
		err = -ENOMEM;
		goto failed;
	}

	if (cp->val)
		scan = SCAN_PAGE;
	else
		scan = 0;

	err = hci_send_cmd(hdev, HCI_OP_WRITE_SCAN_ENABLE, 1, &scan);
	if (err < 0)
		mgmt_pending_remove(cmd);

failed:
	hci_dev_unlock(hdev);
	hci_dev_put(hdev);

	return err;
}

static int mgmt_event(u16 event, struct hci_dev *hdev, void *data,
					u16 data_len, struct sock *skip_sk)
{
	struct sk_buff *skb;
	struct mgmt_hdr *hdr;

	skb = alloc_skb(sizeof(*hdr) + data_len, GFP_ATOMIC);
	if (!skb)
		return -ENOMEM;

	bt_cb(skb)->channel = HCI_CHANNEL_CONTROL;

	hdr = (void *) skb_put(skb, sizeof(*hdr));
	hdr->opcode = cpu_to_le16(event);
	if (hdev)
		hdr->index = cpu_to_le16(hdev->id);
	else
		hdr->index = cpu_to_le16(MGMT_INDEX_NONE);
	hdr->len = cpu_to_le16(data_len);

	if (data)
		memcpy(skb_put(skb, data_len), data, data_len);

	hci_send_to_sock(NULL, skb, skip_sk);
	kfree_skb(skb);

	return 0;
}

static int set_pairable(struct sock *sk, u16 index, void *data, u16 len)
{
	struct mgmt_mode *cp = data;
	struct hci_dev *hdev;
	__le32 ev;
	int err;

	BT_DBG("request for hci%u", index);

	if (len != sizeof(*cp))
		return cmd_status(sk, index, MGMT_OP_SET_PAIRABLE,
						MGMT_STATUS_INVALID_PARAMS);

	hdev = hci_dev_get(index);
	if (!hdev)
		return cmd_status(sk, index, MGMT_OP_SET_PAIRABLE,
						MGMT_STATUS_INVALID_PARAMS);

	hci_dev_lock(hdev);

	if (cp->val)
		set_bit(HCI_PAIRABLE, &hdev->dev_flags);
	else
		clear_bit(HCI_PAIRABLE, &hdev->dev_flags);

	err = send_settings_rsp(sk, MGMT_OP_SET_PAIRABLE, hdev);
	if (err < 0)
		goto failed;

	ev = cpu_to_le32(get_current_settings(hdev));

	err = mgmt_event(MGMT_EV_NEW_SETTINGS, hdev, &ev, sizeof(ev), sk);

failed:
	hci_dev_unlock(hdev);
	hci_dev_put(hdev);

	return err;
}

static int add_uuid(struct sock *sk, u16 index, void *data, u16 len)
{
	struct mgmt_cp_add_uuid *cp = data;
	struct hci_dev *hdev;
	struct bt_uuid *uuid;
	int err;

	BT_DBG("request for hci%u", index);

	if (len != sizeof(*cp))
		return cmd_status(sk, index, MGMT_OP_ADD_UUID,
						MGMT_STATUS_INVALID_PARAMS);

	hdev = hci_dev_get(index);
	if (!hdev)
		return cmd_status(sk, index, MGMT_OP_ADD_UUID,
						MGMT_STATUS_INVALID_PARAMS);

	hci_dev_lock(hdev);

	uuid = kmalloc(sizeof(*uuid), GFP_ATOMIC);
	if (!uuid) {
		err = -ENOMEM;
		goto failed;
	}

	memcpy(uuid->uuid, cp->uuid, 16);
	uuid->svc_hint = cp->svc_hint;

	list_add(&uuid->list, &hdev->uuids);

	err = update_class(hdev);
	if (err < 0)
		goto failed;

	err = update_eir(hdev);
	if (err < 0)
		goto failed;

	err = cmd_complete(sk, index, MGMT_OP_ADD_UUID, NULL, 0);

failed:
	hci_dev_unlock(hdev);
	hci_dev_put(hdev);

	return err;
}

static int remove_uuid(struct sock *sk, u16 index, void *data, u16 len)
{
	struct mgmt_cp_remove_uuid *cp = data;
	struct list_head *p, *n;
	struct hci_dev *hdev;
	u8 bt_uuid_any[] = { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };
	int err, found;

	BT_DBG("request for hci%u", index);

	if (len != sizeof(*cp))
		return cmd_status(sk, index, MGMT_OP_REMOVE_UUID,
						MGMT_STATUS_INVALID_PARAMS);

	hdev = hci_dev_get(index);
	if (!hdev)
		return cmd_status(sk, index, MGMT_OP_REMOVE_UUID,
						MGMT_STATUS_INVALID_PARAMS);

	hci_dev_lock(hdev);

	if (memcmp(cp->uuid, bt_uuid_any, 16) == 0) {
		err = hci_uuids_clear(hdev);
		goto unlock;
	}

	found = 0;

	list_for_each_safe(p, n, &hdev->uuids) {
		struct bt_uuid *match = list_entry(p, struct bt_uuid, list);

		if (memcmp(match->uuid, cp->uuid, 16) != 0)
			continue;

		list_del(&match->list);
		found++;
	}

	if (found == 0) {
		err = cmd_status(sk, index, MGMT_OP_REMOVE_UUID,
						MGMT_STATUS_INVALID_PARAMS);
		goto unlock;
	}

	err = update_class(hdev);
	if (err < 0)
		goto unlock;

	err = update_eir(hdev);
	if (err < 0)
		goto unlock;

	err = cmd_complete(sk, index, MGMT_OP_REMOVE_UUID, NULL, 0);

unlock:
	hci_dev_unlock(hdev);
	hci_dev_put(hdev);

	return err;
}

static int set_dev_class(struct sock *sk, u16 index, void *data, u16 len)
{
	struct hci_dev *hdev;
	struct mgmt_cp_set_dev_class *cp = data;
	int err;

	BT_DBG("request for hci%u", index);

	if (len != sizeof(*cp))
		return cmd_status(sk, index, MGMT_OP_SET_DEV_CLASS,
						MGMT_STATUS_INVALID_PARAMS);

	hdev = hci_dev_get(index);
	if (!hdev)
		return cmd_status(sk, index, MGMT_OP_SET_DEV_CLASS,
						MGMT_STATUS_INVALID_PARAMS);

	hci_dev_lock(hdev);

	hdev->major_class = cp->major;
	hdev->minor_class = cp->minor;

	if (test_and_clear_bit(HCI_SERVICE_CACHE, &hdev->dev_flags)) {
		hci_dev_unlock(hdev);
		cancel_delayed_work_sync(&hdev->service_cache);
		hci_dev_lock(hdev);
		update_eir(hdev);
	}

	err = update_class(hdev);

	if (err == 0)
		err = cmd_complete(sk, index, MGMT_OP_SET_DEV_CLASS, NULL, 0);

	hci_dev_unlock(hdev);
	hci_dev_put(hdev);

	return err;
}

static int load_link_keys(struct sock *sk, u16 index, void *data, u16 len)
{
	struct hci_dev *hdev;
	struct mgmt_cp_load_link_keys *cp = data;
	u16 key_count, expected_len;
	int i;

	if (len < sizeof(*cp))
		return cmd_status(sk, index, MGMT_OP_LOAD_LINK_KEYS,
						MGMT_STATUS_INVALID_PARAMS);

	key_count = get_unaligned_le16(&cp->key_count);

	expected_len = sizeof(*cp) + key_count *
					sizeof(struct mgmt_link_key_info);
	if (expected_len != len) {
		BT_ERR("load_link_keys: expected %u bytes, got %u bytes",
							len, expected_len);
		return cmd_status(sk, index, MGMT_OP_LOAD_LINK_KEYS,
						MGMT_STATUS_INVALID_PARAMS);
	}

	hdev = hci_dev_get(index);
	if (!hdev)
		return cmd_status(sk, index, MGMT_OP_LOAD_LINK_KEYS,
						MGMT_STATUS_INVALID_PARAMS);

	BT_DBG("hci%u debug_keys %u key_count %u", index, cp->debug_keys,
								key_count);

	hci_dev_lock(hdev);

	hci_link_keys_clear(hdev);

	set_bit(HCI_LINK_KEYS, &hdev->dev_flags);

	if (cp->debug_keys)
		set_bit(HCI_DEBUG_KEYS, &hdev->dev_flags);
	else
		clear_bit(HCI_DEBUG_KEYS, &hdev->dev_flags);

	for (i = 0; i < key_count; i++) {
		struct mgmt_link_key_info *key = &cp->keys[i];

		hci_add_link_key(hdev, NULL, 0, &key->bdaddr, key->val, key->type,
								key->pin_len);
	}

	cmd_complete(sk, index, MGMT_OP_LOAD_LINK_KEYS, NULL, 0);

	hci_dev_unlock(hdev);
	hci_dev_put(hdev);

	return 0;
}

static int remove_keys(struct sock *sk, u16 index, void *data, u16 len)
{
	struct hci_dev *hdev;
	struct mgmt_cp_remove_keys *cp = data;
	struct mgmt_rp_remove_keys rp;
	struct hci_cp_disconnect dc;
	struct pending_cmd *cmd;
	struct hci_conn *conn;
	int err;

	if (len != sizeof(*cp))
		return cmd_status(sk, index, MGMT_OP_REMOVE_KEYS,
						MGMT_STATUS_INVALID_PARAMS);

	hdev = hci_dev_get(index);
	if (!hdev)
		return cmd_status(sk, index, MGMT_OP_REMOVE_KEYS,
						MGMT_STATUS_INVALID_PARAMS);

	hci_dev_lock(hdev);

	memset(&rp, 0, sizeof(rp));
	bacpy(&rp.bdaddr, &cp->bdaddr);
	rp.status = MGMT_STATUS_FAILED;

	err = hci_remove_ltk(hdev, &cp->bdaddr);
	if (err < 0) {
		err = cmd_status(sk, index, MGMT_OP_REMOVE_KEYS, -err);
		goto unlock;
	}

	err = hci_remove_link_key(hdev, &cp->bdaddr);
	if (err < 0) {
		rp.status = MGMT_STATUS_NOT_PAIRED;
		goto unlock;
	}

	if (!test_bit(HCI_UP, &hdev->flags) || !cp->disconnect) {
		err = cmd_complete(sk, index, MGMT_OP_REMOVE_KEYS, &rp,
								sizeof(rp));
		goto unlock;
	}

	conn = hci_conn_hash_lookup_ba(hdev, ACL_LINK, &cp->bdaddr);
	if (!conn) {
		err = cmd_complete(sk, index, MGMT_OP_REMOVE_KEYS, &rp,
								sizeof(rp));
		goto unlock;
	}

	cmd = mgmt_pending_add(sk, MGMT_OP_REMOVE_KEYS, hdev, cp, sizeof(*cp));
	if (!cmd) {
		err = -ENOMEM;
		goto unlock;
	}

	put_unaligned_le16(conn->handle, &dc.handle);
	dc.reason = 0x13; /* Remote User Terminated Connection */
	err = hci_send_cmd(hdev, HCI_OP_DISCONNECT, sizeof(dc), &dc);
	if (err < 0)
		mgmt_pending_remove(cmd);

unlock:
	if (err < 0)
		err = cmd_complete(sk, index, MGMT_OP_REMOVE_KEYS, &rp,
								sizeof(rp));
	hci_dev_unlock(hdev);
	hci_dev_put(hdev);

	return err;
}

static int disconnect(struct sock *sk, u16 index, void *data, u16 len)
{
	struct hci_dev *hdev;
	struct mgmt_cp_disconnect *cp = data;
	struct hci_cp_disconnect dc;
	struct pending_cmd *cmd;
	struct hci_conn *conn;
	int err;

	BT_DBG("");

	if (len != sizeof(*cp))
		return cmd_status(sk, index, MGMT_OP_DISCONNECT,
						MGMT_STATUS_INVALID_PARAMS);

	hdev = hci_dev_get(index);
	if (!hdev)
		return cmd_status(sk, index, MGMT_OP_DISCONNECT,
						MGMT_STATUS_INVALID_PARAMS);

	hci_dev_lock(hdev);

	if (!test_bit(HCI_UP, &hdev->flags)) {
		err = cmd_status(sk, index, MGMT_OP_DISCONNECT,
						MGMT_STATUS_NOT_POWERED);
		goto failed;
	}

	if (mgmt_pending_find(MGMT_OP_DISCONNECT, hdev)) {
		err = cmd_status(sk, index, MGMT_OP_DISCONNECT,
							MGMT_STATUS_BUSY);
		goto failed;
	}

	conn = hci_conn_hash_lookup_ba(hdev, ACL_LINK, &cp->bdaddr);
	if (!conn)
		conn = hci_conn_hash_lookup_ba(hdev, LE_LINK, &cp->bdaddr);

	if (!conn) {
		err = cmd_status(sk, index, MGMT_OP_DISCONNECT,
						MGMT_STATUS_NOT_CONNECTED);
		goto failed;
	}

	cmd = mgmt_pending_add(sk, MGMT_OP_DISCONNECT, hdev, data, len);
	if (!cmd) {
		err = -ENOMEM;
		goto failed;
	}

	put_unaligned_le16(conn->handle, &dc.handle);
	dc.reason = 0x13; /* Remote User Terminated Connection */

	err = hci_send_cmd(hdev, HCI_OP_DISCONNECT, sizeof(dc), &dc);
	if (err < 0)
		mgmt_pending_remove(cmd);

failed:
	hci_dev_unlock(hdev);
	hci_dev_put(hdev);

	return err;
}

static u8 link_to_mgmt(u8 link_type, u8 addr_type)
{
	switch (link_type) {
	case LE_LINK:
		switch (addr_type) {
		case ADDR_LE_DEV_PUBLIC:
			return MGMT_ADDR_LE_PUBLIC;
		case ADDR_LE_DEV_RANDOM:
			return MGMT_ADDR_LE_RANDOM;
		default:
			return MGMT_ADDR_INVALID;
		}
	case ACL_LINK:
		return MGMT_ADDR_BREDR;
	default:
		return MGMT_ADDR_INVALID;
	}
}

static int get_connections(struct sock *sk, u16 index)
{
	struct mgmt_rp_get_connections *rp;
	struct hci_dev *hdev;
	struct hci_conn *c;
	size_t rp_len;
	u16 count;
	int i, err;

	BT_DBG("");

	hdev = hci_dev_get(index);
	if (!hdev)
		return cmd_status(sk, index, MGMT_OP_GET_CONNECTIONS,
						MGMT_STATUS_INVALID_PARAMS);

	hci_dev_lock(hdev);

	count = 0;
	list_for_each_entry(c, &hdev->conn_hash.list, list) {
		if (test_bit(HCI_CONN_MGMT_CONNECTED, &c->flags))
			count++;
	}

	rp_len = sizeof(*rp) + (count * sizeof(struct mgmt_addr_info));
	rp = kmalloc(rp_len, GFP_ATOMIC);
	if (!rp) {
		err = -ENOMEM;
		goto unlock;
	}

	put_unaligned_le16(count, &rp->conn_count);

	i = 0;
	list_for_each_entry(c, &hdev->conn_hash.list, list) {
		if (!test_bit(HCI_CONN_MGMT_CONNECTED, &c->flags))
			continue;
		bacpy(&rp->addr[i].bdaddr, &c->dst);
		rp->addr[i].type = link_to_mgmt(c->type, c->dst_type);
		if (rp->addr[i].type == MGMT_ADDR_INVALID)
			continue;
		i++;
	}

	/* Recalculate length in case of filtered SCO connections, etc */
	rp_len = sizeof(*rp) + (i * sizeof(struct mgmt_addr_info));

	err = cmd_complete(sk, index, MGMT_OP_GET_CONNECTIONS, rp, rp_len);

unlock:
	kfree(rp);
	hci_dev_unlock(hdev);
	hci_dev_put(hdev);
	return err;
}

static int send_pin_code_neg_reply(struct sock *sk, u16 index,
		struct hci_dev *hdev, struct mgmt_cp_pin_code_neg_reply *cp)
{
	struct pending_cmd *cmd;
	int err;

	cmd = mgmt_pending_add(sk, MGMT_OP_PIN_CODE_NEG_REPLY, hdev, cp,
								sizeof(*cp));
	if (!cmd)
		return -ENOMEM;

	err = hci_send_cmd(hdev, HCI_OP_PIN_CODE_NEG_REPLY, sizeof(cp->bdaddr),
								&cp->bdaddr);
	if (err < 0)
		mgmt_pending_remove(cmd);

	return err;
}

static int pin_code_reply(struct sock *sk, u16 index, void *data, u16 len)
{
	struct hci_dev *hdev;
	struct hci_conn *conn;
	struct mgmt_cp_pin_code_reply *cp = data;
	struct mgmt_cp_pin_code_neg_reply ncp;
	struct hci_cp_pin_code_reply reply;
	struct pending_cmd *cmd;
	int err;

	BT_DBG("");

	if (len != sizeof(*cp))
		return cmd_status(sk, index, MGMT_OP_PIN_CODE_REPLY,
						MGMT_STATUS_INVALID_PARAMS);

	hdev = hci_dev_get(index);
	if (!hdev)
		return cmd_status(sk, index, MGMT_OP_PIN_CODE_REPLY,
						MGMT_STATUS_INVALID_PARAMS);

	hci_dev_lock(hdev);

	if (!test_bit(HCI_UP, &hdev->flags)) {
		err = cmd_status(sk, index, MGMT_OP_PIN_CODE_REPLY,
						MGMT_STATUS_NOT_POWERED);
		goto failed;
	}

	conn = hci_conn_hash_lookup_ba(hdev, ACL_LINK, &cp->bdaddr);
	if (!conn) {
		err = cmd_status(sk, index, MGMT_OP_PIN_CODE_REPLY,
						MGMT_STATUS_NOT_CONNECTED);
		goto failed;
	}

	if (conn->pending_sec_level == BT_SECURITY_HIGH && cp->pin_len != 16) {
		bacpy(&ncp.bdaddr, &cp->bdaddr);

		BT_ERR("PIN code is not 16 bytes long");

		err = send_pin_code_neg_reply(sk, index, hdev, &ncp);
		if (err >= 0)
			err = cmd_status(sk, index, MGMT_OP_PIN_CODE_REPLY,
						MGMT_STATUS_INVALID_PARAMS);

		goto failed;
	}

	cmd = mgmt_pending_add(sk, MGMT_OP_PIN_CODE_REPLY, hdev, data,
									len);
	if (!cmd) {
		err = -ENOMEM;
		goto failed;
	}

	bacpy(&reply.bdaddr, &cp->bdaddr);
	reply.pin_len = cp->pin_len;
	memcpy(reply.pin_code, cp->pin_code, sizeof(reply.pin_code));

	err = hci_send_cmd(hdev, HCI_OP_PIN_CODE_REPLY, sizeof(reply), &reply);
	if (err < 0)
		mgmt_pending_remove(cmd);

failed:
	hci_dev_unlock(hdev);
	hci_dev_put(hdev);

	return err;
}

static int pin_code_neg_reply(struct sock *sk, u16 index, void *data, u16 len)
{
	struct hci_dev *hdev;
	struct mgmt_cp_pin_code_neg_reply *cp = data;
	int err;

	BT_DBG("");

	if (len != sizeof(*cp))
		return cmd_status(sk, index, MGMT_OP_PIN_CODE_NEG_REPLY,
						MGMT_STATUS_INVALID_PARAMS);

	hdev = hci_dev_get(index);
	if (!hdev)
		return cmd_status(sk, index, MGMT_OP_PIN_CODE_NEG_REPLY,
						MGMT_STATUS_INVALID_PARAMS);

	hci_dev_lock(hdev);

	if (!test_bit(HCI_UP, &hdev->flags)) {
		err = cmd_status(sk, index, MGMT_OP_PIN_CODE_NEG_REPLY,
						MGMT_STATUS_NOT_POWERED);
		goto failed;
	}

	err = send_pin_code_neg_reply(sk, index, hdev, cp);

failed:
	hci_dev_unlock(hdev);
	hci_dev_put(hdev);

	return err;
}

static int set_io_capability(struct sock *sk, u16 index, void *data, u16 len)
{
	struct hci_dev *hdev;
	struct mgmt_cp_set_io_capability *cp = data;

	BT_DBG("");

	if (len != sizeof(*cp))
		return cmd_status(sk, index, MGMT_OP_SET_IO_CAPABILITY,
						MGMT_STATUS_INVALID_PARAMS);

	hdev = hci_dev_get(index);
	if (!hdev)
		return cmd_status(sk, index, MGMT_OP_SET_IO_CAPABILITY,
						MGMT_STATUS_INVALID_PARAMS);

	hci_dev_lock(hdev);

	hdev->io_capability = cp->io_capability;

	BT_DBG("%s IO capability set to 0x%02x", hdev->name,
							hdev->io_capability);

	hci_dev_unlock(hdev);
	hci_dev_put(hdev);

	return cmd_complete(sk, index, MGMT_OP_SET_IO_CAPABILITY, NULL, 0);
}

static inline struct pending_cmd *find_pairing(struct hci_conn *conn)
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

static void pairing_complete(struct pending_cmd *cmd, u8 status)
{
	struct mgmt_rp_pair_device rp;
	struct hci_conn *conn = cmd->user_data;

	bacpy(&rp.addr.bdaddr, &conn->dst);
	rp.addr.type = link_to_mgmt(conn->type, conn->dst_type);
	rp.status = status;

	cmd_complete(cmd->sk, cmd->index, MGMT_OP_PAIR_DEVICE, &rp, sizeof(rp));

	/* So we don't get further callbacks for this connection */
	conn->connect_cfm_cb = NULL;
	conn->security_cfm_cb = NULL;
	conn->disconn_cfm_cb = NULL;

	hci_conn_put(conn);

	mgmt_pending_remove(cmd);
}

static void pairing_complete_cb(struct hci_conn *conn, u8 status)
{
	struct pending_cmd *cmd;

	BT_DBG("status %u", status);

	cmd = find_pairing(conn);
	if (!cmd)
		BT_DBG("Unable to find a pending command");
	else
		pairing_complete(cmd, status);
}

static int pair_device(struct sock *sk, u16 index, void *data, u16 len)
{
	struct hci_dev *hdev;
	struct mgmt_cp_pair_device *cp = data;
	struct mgmt_rp_pair_device rp;
	struct pending_cmd *cmd;
	u8 sec_level, auth_type;
	struct hci_conn *conn;
	int err;

	BT_DBG("");

	if (len != sizeof(*cp))
		return cmd_status(sk, index, MGMT_OP_PAIR_DEVICE,
						MGMT_STATUS_INVALID_PARAMS);

	hdev = hci_dev_get(index);
	if (!hdev)
		return cmd_status(sk, index, MGMT_OP_PAIR_DEVICE,
						MGMT_STATUS_INVALID_PARAMS);

	hci_dev_lock(hdev);

	sec_level = BT_SECURITY_MEDIUM;
	if (cp->io_cap == 0x03)
		auth_type = HCI_AT_DEDICATED_BONDING;
	else
		auth_type = HCI_AT_DEDICATED_BONDING_MITM;

	if (cp->addr.type == MGMT_ADDR_BREDR)
		conn = hci_connect(hdev, ACL_LINK, &cp->addr.bdaddr, sec_level,
								auth_type);
	else
		conn = hci_connect(hdev, LE_LINK, &cp->addr.bdaddr, sec_level,
								auth_type);

	memset(&rp, 0, sizeof(rp));
	bacpy(&rp.addr.bdaddr, &cp->addr.bdaddr);
	rp.addr.type = cp->addr.type;

	if (IS_ERR(conn)) {
		rp.status = -PTR_ERR(conn);
		err = cmd_complete(sk, index, MGMT_OP_PAIR_DEVICE,
							&rp, sizeof(rp));
		goto unlock;
	}

	if (conn->connect_cfm_cb) {
		hci_conn_put(conn);
		rp.status = EBUSY;
		err = cmd_complete(sk, index, MGMT_OP_PAIR_DEVICE,
							&rp, sizeof(rp));
		goto unlock;
	}

	cmd = mgmt_pending_add(sk, MGMT_OP_PAIR_DEVICE, hdev, data, len);
	if (!cmd) {
		err = -ENOMEM;
		hci_conn_put(conn);
		goto unlock;
	}

	/* For LE, just connecting isn't a proof that the pairing finished */
	if (cp->addr.type == MGMT_ADDR_BREDR)
		conn->connect_cfm_cb = pairing_complete_cb;

	conn->security_cfm_cb = pairing_complete_cb;
	conn->disconn_cfm_cb = pairing_complete_cb;
	conn->io_capability = cp->io_cap;
	cmd->user_data = conn;

	if (conn->state == BT_CONNECTED &&
				hci_conn_security(conn, sec_level, auth_type))
		pairing_complete(cmd, 0);

	err = 0;

unlock:
	hci_dev_unlock(hdev);
	hci_dev_put(hdev);

	return err;
}

static int cancel_pair_device(struct sock *sk, u16 index,
						unsigned char *data, u16 len)
{
	struct mgmt_addr_info *addr = (void *) data;
	struct hci_dev *hdev;
	struct pending_cmd *cmd;
	struct hci_conn *conn;
	int err;

	BT_DBG("");

	if (len != sizeof(*addr))
		return cmd_status(sk, index, MGMT_OP_CANCEL_PAIR_DEVICE,
						MGMT_STATUS_INVALID_PARAMS);

	hdev = hci_dev_get(index);
	if (!hdev)
		return cmd_status(sk, index, MGMT_OP_CANCEL_PAIR_DEVICE,
						MGMT_STATUS_INVALID_PARAMS);

	hci_dev_lock(hdev);

	cmd = mgmt_pending_find(MGMT_OP_PAIR_DEVICE, hdev);
	if (!cmd) {
		err = cmd_status(sk, index, MGMT_OP_CANCEL_PAIR_DEVICE,
						MGMT_STATUS_INVALID_PARAMS);
		goto unlock;
	}

	conn = cmd->user_data;

	if (bacmp(&addr->bdaddr, &conn->dst) != 0) {
		err = cmd_status(sk, index, MGMT_OP_CANCEL_PAIR_DEVICE,
						MGMT_STATUS_INVALID_PARAMS);
		goto unlock;
	}

	pairing_complete(cmd, MGMT_STATUS_CANCELLED);

	err = cmd_complete(sk, index, MGMT_OP_CANCEL_PAIR_DEVICE, addr,
								sizeof(*addr));
unlock:
	hci_dev_unlock(hdev);
	hci_dev_put(hdev);

	return err;
}

static int user_pairing_resp(struct sock *sk, u16 index, bdaddr_t *bdaddr,
					u16 mgmt_op, u16 hci_op, __le32 passkey)
{
	struct pending_cmd *cmd;
	struct hci_dev *hdev;
	struct hci_conn *conn;
	int err;

	hdev = hci_dev_get(index);
	if (!hdev)
		return cmd_status(sk, index, mgmt_op,
						MGMT_STATUS_INVALID_PARAMS);

	hci_dev_lock(hdev);

	if (!test_bit(HCI_UP, &hdev->flags)) {
		err = cmd_status(sk, index, mgmt_op, MGMT_STATUS_NOT_POWERED);
		goto done;
	}

	/*
	 * Check for an existing ACL link, if present pair via
	 * HCI commands.
	 *
	 * If no ACL link is present, check for an LE link and if
	 * present, pair via the SMP engine.
	 *
	 * If neither ACL nor LE links are present, fail with error.
	 */
	conn = hci_conn_hash_lookup_ba(hdev, ACL_LINK, bdaddr);
	if (!conn) {
		conn = hci_conn_hash_lookup_ba(hdev, LE_LINK, bdaddr);
		if (!conn) {
			err = cmd_status(sk, index, mgmt_op,
						MGMT_STATUS_NOT_CONNECTED);
			goto done;
		}

		/* Continue with pairing via SMP */
		err = smp_user_confirm_reply(conn, mgmt_op, passkey);

		if (!err)
			err = cmd_status(sk, index, mgmt_op,
							MGMT_STATUS_SUCCESS);
		else
			err = cmd_status(sk, index, mgmt_op,
							MGMT_STATUS_FAILED);

		goto done;
	}

	cmd = mgmt_pending_add(sk, mgmt_op, hdev, bdaddr, sizeof(*bdaddr));
	if (!cmd) {
		err = -ENOMEM;
		goto done;
	}

	/* Continue with pairing via HCI */
	if (hci_op == HCI_OP_USER_PASSKEY_REPLY) {
		struct hci_cp_user_passkey_reply cp;

		bacpy(&cp.bdaddr, bdaddr);
		cp.passkey = passkey;
		err = hci_send_cmd(hdev, hci_op, sizeof(cp), &cp);
	} else
		err = hci_send_cmd(hdev, hci_op, sizeof(*bdaddr), bdaddr);

	if (err < 0)
		mgmt_pending_remove(cmd);

done:
	hci_dev_unlock(hdev);
	hci_dev_put(hdev);

	return err;
}

static int user_confirm_reply(struct sock *sk, u16 index, void *data, u16 len)
{
	struct mgmt_cp_user_confirm_reply *cp = data;

	BT_DBG("");

	if (len != sizeof(*cp))
		return cmd_status(sk, index, MGMT_OP_USER_CONFIRM_REPLY,
						MGMT_STATUS_INVALID_PARAMS);

	return user_pairing_resp(sk, index, &cp->bdaddr,
			MGMT_OP_USER_CONFIRM_REPLY,
			HCI_OP_USER_CONFIRM_REPLY, 0);
}

static int user_confirm_neg_reply(struct sock *sk, u16 index, void *data,
									u16 len)
{
	struct mgmt_cp_user_confirm_neg_reply *cp = data;

	BT_DBG("");

	if (len != sizeof(*cp))
		return cmd_status(sk, index, MGMT_OP_USER_CONFIRM_NEG_REPLY,
						MGMT_STATUS_INVALID_PARAMS);

	return user_pairing_resp(sk, index, &cp->bdaddr,
			MGMT_OP_USER_CONFIRM_NEG_REPLY,
			HCI_OP_USER_CONFIRM_NEG_REPLY, 0);
}

static int user_passkey_reply(struct sock *sk, u16 index, void *data, u16 len)
{
	struct mgmt_cp_user_passkey_reply *cp = data;

	BT_DBG("");

	if (len != sizeof(*cp))
		return cmd_status(sk, index, MGMT_OP_USER_PASSKEY_REPLY,
									EINVAL);

	return user_pairing_resp(sk, index, &cp->bdaddr,
			MGMT_OP_USER_PASSKEY_REPLY,
			HCI_OP_USER_PASSKEY_REPLY, cp->passkey);
}

static int user_passkey_neg_reply(struct sock *sk, u16 index, void *data,
									u16 len)
{
	struct mgmt_cp_user_passkey_neg_reply *cp = data;

	BT_DBG("");

	if (len != sizeof(*cp))
		return cmd_status(sk, index, MGMT_OP_USER_PASSKEY_NEG_REPLY,
									EINVAL);

	return user_pairing_resp(sk, index, &cp->bdaddr,
			MGMT_OP_USER_PASSKEY_NEG_REPLY,
			HCI_OP_USER_PASSKEY_NEG_REPLY, 0);
}

static int set_local_name(struct sock *sk, u16 index, void *data,
								u16 len)
{
	struct mgmt_cp_set_local_name *mgmt_cp = data;
	struct hci_cp_write_local_name hci_cp;
	struct hci_dev *hdev;
	struct pending_cmd *cmd;
	int err;

	BT_DBG("");

	if (len != sizeof(*mgmt_cp))
		return cmd_status(sk, index, MGMT_OP_SET_LOCAL_NAME,
						MGMT_STATUS_INVALID_PARAMS);

	hdev = hci_dev_get(index);
	if (!hdev)
		return cmd_status(sk, index, MGMT_OP_SET_LOCAL_NAME,
						MGMT_STATUS_INVALID_PARAMS);

	hci_dev_lock(hdev);

	cmd = mgmt_pending_add(sk, MGMT_OP_SET_LOCAL_NAME, hdev, data,
									len);
	if (!cmd) {
		err = -ENOMEM;
		goto failed;
	}

	memcpy(hci_cp.name, mgmt_cp->name, sizeof(hci_cp.name));
	err = hci_send_cmd(hdev, HCI_OP_WRITE_LOCAL_NAME, sizeof(hci_cp),
								&hci_cp);
	if (err < 0)
		mgmt_pending_remove(cmd);

failed:
	hci_dev_unlock(hdev);
	hci_dev_put(hdev);

	return err;
}

static int read_local_oob_data(struct sock *sk, u16 index)
{
	struct hci_dev *hdev;
	struct pending_cmd *cmd;
	int err;

	BT_DBG("hci%u", index);

	hdev = hci_dev_get(index);
	if (!hdev)
		return cmd_status(sk, index, MGMT_OP_READ_LOCAL_OOB_DATA,
						MGMT_STATUS_INVALID_PARAMS);

	hci_dev_lock(hdev);

	if (!test_bit(HCI_UP, &hdev->flags)) {
		err = cmd_status(sk, index, MGMT_OP_READ_LOCAL_OOB_DATA,
						MGMT_STATUS_NOT_POWERED);
		goto unlock;
	}

	if (!(hdev->features[6] & LMP_SIMPLE_PAIR)) {
		err = cmd_status(sk, index, MGMT_OP_READ_LOCAL_OOB_DATA,
						MGMT_STATUS_NOT_SUPPORTED);
		goto unlock;
	}

	if (mgmt_pending_find(MGMT_OP_READ_LOCAL_OOB_DATA, hdev)) {
		err = cmd_status(sk, index, MGMT_OP_READ_LOCAL_OOB_DATA,
							MGMT_STATUS_BUSY);
		goto unlock;
	}

	cmd = mgmt_pending_add(sk, MGMT_OP_READ_LOCAL_OOB_DATA, hdev, NULL, 0);
	if (!cmd) {
		err = -ENOMEM;
		goto unlock;
	}

	err = hci_send_cmd(hdev, HCI_OP_READ_LOCAL_OOB_DATA, 0, NULL);
	if (err < 0)
		mgmt_pending_remove(cmd);

unlock:
	hci_dev_unlock(hdev);
	hci_dev_put(hdev);

	return err;
}

static int add_remote_oob_data(struct sock *sk, u16 index, void *data,
								u16 len)
{
	struct hci_dev *hdev;
	struct mgmt_cp_add_remote_oob_data *cp = data;
	int err;

	BT_DBG("hci%u ", index);

	if (len != sizeof(*cp))
		return cmd_status(sk, index, MGMT_OP_ADD_REMOTE_OOB_DATA,
						MGMT_STATUS_INVALID_PARAMS);

	hdev = hci_dev_get(index);
	if (!hdev)
		return cmd_status(sk, index, MGMT_OP_ADD_REMOTE_OOB_DATA,
						MGMT_STATUS_INVALID_PARAMS);

	hci_dev_lock(hdev);

	err = hci_add_remote_oob_data(hdev, &cp->bdaddr, cp->hash,
								cp->randomizer);
	if (err < 0)
		err = cmd_status(sk, index, MGMT_OP_ADD_REMOTE_OOB_DATA,
							MGMT_STATUS_FAILED);
	else
		err = cmd_complete(sk, index, MGMT_OP_ADD_REMOTE_OOB_DATA, NULL,
									0);

	hci_dev_unlock(hdev);
	hci_dev_put(hdev);

	return err;
}

static int remove_remote_oob_data(struct sock *sk, u16 index,
						void *data, u16 len)
{
	struct hci_dev *hdev;
	struct mgmt_cp_remove_remote_oob_data *cp = data;
	int err;

	BT_DBG("hci%u ", index);

	if (len != sizeof(*cp))
		return cmd_status(sk, index, MGMT_OP_REMOVE_REMOTE_OOB_DATA,
						MGMT_STATUS_INVALID_PARAMS);

	hdev = hci_dev_get(index);
	if (!hdev)
		return cmd_status(sk, index, MGMT_OP_REMOVE_REMOTE_OOB_DATA,
						MGMT_STATUS_INVALID_PARAMS);

	hci_dev_lock(hdev);

	err = hci_remove_remote_oob_data(hdev, &cp->bdaddr);
	if (err < 0)
		err = cmd_status(sk, index, MGMT_OP_REMOVE_REMOTE_OOB_DATA,
						MGMT_STATUS_INVALID_PARAMS);
	else
		err = cmd_complete(sk, index, MGMT_OP_REMOVE_REMOTE_OOB_DATA,
								NULL, 0);

	hci_dev_unlock(hdev);
	hci_dev_put(hdev);

	return err;
}

static int start_discovery(struct sock *sk, u16 index,
						void *data, u16 len)
{
	struct mgmt_cp_start_discovery *cp = data;
	unsigned long discov_type = cp->type;
	struct pending_cmd *cmd;
	struct hci_dev *hdev;
	int err;

	BT_DBG("hci%u", index);

	if (len != sizeof(*cp))
		return cmd_status(sk, index, MGMT_OP_START_DISCOVERY,
						MGMT_STATUS_INVALID_PARAMS);

	hdev = hci_dev_get(index);
	if (!hdev)
		return cmd_status(sk, index, MGMT_OP_START_DISCOVERY,
						MGMT_STATUS_INVALID_PARAMS);

	hci_dev_lock(hdev);

	if (!test_bit(HCI_UP, &hdev->flags)) {
		err = cmd_status(sk, index, MGMT_OP_START_DISCOVERY,
						MGMT_STATUS_NOT_POWERED);
		goto failed;
	}

	if (hdev->discovery.state != DISCOVERY_STOPPED) {
		err = cmd_status(sk, index, MGMT_OP_START_DISCOVERY,
						MGMT_STATUS_BUSY);
		goto failed;
	}

	cmd = mgmt_pending_add(sk, MGMT_OP_START_DISCOVERY, hdev, NULL, 0);
	if (!cmd) {
		err = -ENOMEM;
		goto failed;
	}

	if (test_bit(MGMT_ADDR_BREDR, &discov_type))
		err = hci_do_inquiry(hdev, INQUIRY_LEN_BREDR);
	else if (test_bit(MGMT_ADDR_LE_PUBLIC, &discov_type) &&
				test_bit(MGMT_ADDR_LE_RANDOM, &discov_type))
		err = hci_le_scan(hdev, LE_SCAN_TYPE, LE_SCAN_INT,
					LE_SCAN_WIN, LE_SCAN_TIMEOUT_LE_ONLY);
	else
		err = -EINVAL;

	if (err < 0)
		mgmt_pending_remove(cmd);
	else
		hci_discovery_set_state(hdev, DISCOVERY_STARTING);

failed:
	hci_dev_unlock(hdev);
	hci_dev_put(hdev);

	return err;
}

static int stop_discovery(struct sock *sk, u16 index)
{
	struct hci_dev *hdev;
	struct pending_cmd *cmd;
	struct hci_cp_remote_name_req_cancel cp;
	struct inquiry_entry *e;
	int err;

	BT_DBG("hci%u", index);

	hdev = hci_dev_get(index);
	if (!hdev)
		return cmd_status(sk, index, MGMT_OP_STOP_DISCOVERY,
						MGMT_STATUS_INVALID_PARAMS);

	hci_dev_lock(hdev);

	if (!hci_discovery_active(hdev)) {
		err = cmd_status(sk, index, MGMT_OP_STOP_DISCOVERY,
						MGMT_STATUS_REJECTED);
		goto unlock;
	}

	cmd = mgmt_pending_add(sk, MGMT_OP_STOP_DISCOVERY, hdev, NULL, 0);
	if (!cmd) {
		err = -ENOMEM;
		goto unlock;
	}

	if (hdev->discovery.state == DISCOVERY_INQUIRY) {
		err = hci_cancel_inquiry(hdev);
		if (err < 0)
			mgmt_pending_remove(cmd);
		else
			hci_discovery_set_state(hdev, DISCOVERY_STOPPING);
		goto unlock;
	}

	e = hci_inquiry_cache_lookup_resolve(hdev, BDADDR_ANY, NAME_PENDING);
	if (!e) {
		mgmt_pending_remove(cmd);
		err = cmd_complete(sk, index, MGMT_OP_STOP_DISCOVERY, NULL, 0);
		hci_discovery_set_state(hdev, DISCOVERY_STOPPED);
		goto unlock;
	}

	bacpy(&cp.bdaddr, &e->data.bdaddr);
	err = hci_send_cmd(hdev, HCI_OP_REMOTE_NAME_REQ_CANCEL,
							sizeof(cp), &cp);
	if (err < 0)
		mgmt_pending_remove(cmd);
	else
		hci_discovery_set_state(hdev, DISCOVERY_STOPPING);

unlock:
	hci_dev_unlock(hdev);
	hci_dev_put(hdev);

	return err;
}

static int confirm_name(struct sock *sk, u16 index, void *data, u16 len)
{
	struct mgmt_cp_confirm_name *cp = data;
	struct inquiry_entry *e;
	struct hci_dev *hdev;
	int err;

	BT_DBG("hci%u", index);

	if (len != sizeof(*cp))
		return cmd_status(sk, index, MGMT_OP_CONFIRM_NAME,
				MGMT_STATUS_INVALID_PARAMS);

	hdev = hci_dev_get(index);
	if (!hdev)
		return cmd_status(sk, index, MGMT_OP_CONFIRM_NAME,
				MGMT_STATUS_INVALID_PARAMS);

	hci_dev_lock(hdev);

	if (!hci_discovery_active(hdev)) {
		err = cmd_status(sk, index, MGMT_OP_CONFIRM_NAME,
							MGMT_STATUS_FAILED);
		goto failed;
	}

	e = hci_inquiry_cache_lookup_unknown(hdev, &cp->bdaddr);
	if (!e) {
		err = cmd_status (sk, index, MGMT_OP_CONFIRM_NAME,
				MGMT_STATUS_INVALID_PARAMS);
		goto failed;
	}

	if (cp->name_known) {
		e->name_state = NAME_KNOWN;
		list_del(&e->list);
	} else {
		e->name_state = NAME_NEEDED;
		hci_inquiry_cache_update_resolve(hdev, e);
	}

	err = 0;

failed:
	hci_dev_unlock(hdev);

	return err;
}

static int block_device(struct sock *sk, u16 index, void *data, u16 len)
{
	struct hci_dev *hdev;
	struct mgmt_cp_block_device *cp = data;
	int err;

	BT_DBG("hci%u", index);

	if (len != sizeof(*cp))
		return cmd_status(sk, index, MGMT_OP_BLOCK_DEVICE,
						MGMT_STATUS_INVALID_PARAMS);

	hdev = hci_dev_get(index);
	if (!hdev)
		return cmd_status(sk, index, MGMT_OP_BLOCK_DEVICE,
						MGMT_STATUS_INVALID_PARAMS);

	hci_dev_lock(hdev);

	err = hci_blacklist_add(hdev, &cp->bdaddr);
	if (err < 0)
		err = cmd_status(sk, index, MGMT_OP_BLOCK_DEVICE,
							MGMT_STATUS_FAILED);
	else
		err = cmd_complete(sk, index, MGMT_OP_BLOCK_DEVICE,
							NULL, 0);

	hci_dev_unlock(hdev);
	hci_dev_put(hdev);

	return err;
}

static int unblock_device(struct sock *sk, u16 index, void *data, u16 len)
{
	struct hci_dev *hdev;
	struct mgmt_cp_unblock_device *cp = data;
	int err;

	BT_DBG("hci%u", index);

	if (len != sizeof(*cp))
		return cmd_status(sk, index, MGMT_OP_UNBLOCK_DEVICE,
						MGMT_STATUS_INVALID_PARAMS);

	hdev = hci_dev_get(index);
	if (!hdev)
		return cmd_status(sk, index, MGMT_OP_UNBLOCK_DEVICE,
						MGMT_STATUS_INVALID_PARAMS);

	hci_dev_lock(hdev);

	err = hci_blacklist_del(hdev, &cp->bdaddr);

	if (err < 0)
		err = cmd_status(sk, index, MGMT_OP_UNBLOCK_DEVICE,
						MGMT_STATUS_INVALID_PARAMS);
	else
		err = cmd_complete(sk, index, MGMT_OP_UNBLOCK_DEVICE,
								NULL, 0);

	hci_dev_unlock(hdev);
	hci_dev_put(hdev);

	return err;
}

static int set_fast_connectable(struct sock *sk, u16 index,
					void *data, u16 len)
{
	struct hci_dev *hdev;
	struct mgmt_mode *cp = data;
	struct hci_cp_write_page_scan_activity acp;
	u8 type;
	int err;

	BT_DBG("hci%u", index);

	if (len != sizeof(*cp))
		return cmd_status(sk, index, MGMT_OP_SET_FAST_CONNECTABLE,
						MGMT_STATUS_INVALID_PARAMS);

	hdev = hci_dev_get(index);
	if (!hdev)
		return cmd_status(sk, index, MGMT_OP_SET_FAST_CONNECTABLE,
						MGMT_STATUS_INVALID_PARAMS);

	hci_dev_lock(hdev);

	if (cp->val) {
		type = PAGE_SCAN_TYPE_INTERLACED;
		acp.interval = 0x0024;	/* 22.5 msec page scan interval */
	} else {
		type = PAGE_SCAN_TYPE_STANDARD;	/* default */
		acp.interval = 0x0800;	/* default 1.28 sec page scan */
	}

	acp.window = 0x0012;	/* default 11.25 msec page scan window */

	err = hci_send_cmd(hdev, HCI_OP_WRITE_PAGE_SCAN_ACTIVITY,
						sizeof(acp), &acp);
	if (err < 0) {
		err = cmd_status(sk, index, MGMT_OP_SET_FAST_CONNECTABLE,
							MGMT_STATUS_FAILED);
		goto done;
	}

	err = hci_send_cmd(hdev, HCI_OP_WRITE_PAGE_SCAN_TYPE, 1, &type);
	if (err < 0) {
		err = cmd_status(sk, index, MGMT_OP_SET_FAST_CONNECTABLE,
							MGMT_STATUS_FAILED);
		goto done;
	}

	err = cmd_complete(sk, index, MGMT_OP_SET_FAST_CONNECTABLE,
							NULL, 0);
done:
	hci_dev_unlock(hdev);
	hci_dev_put(hdev);

	return err;
}

static int load_long_term_keys(struct sock *sk, u16 index,
					void *cp_data, u16 len)
{
	struct hci_dev *hdev;
	struct mgmt_cp_load_long_term_keys *cp = cp_data;
	u16 key_count, expected_len;
	int i;

	if (len < sizeof(*cp))
		return cmd_status(sk, index, MGMT_OP_LOAD_LONG_TERM_KEYS,
								EINVAL);

	key_count = get_unaligned_le16(&cp->key_count);

	expected_len = sizeof(*cp) + key_count *
					sizeof(struct mgmt_ltk_info);
	if (expected_len != len) {
		BT_ERR("load_keys: expected %u bytes, got %u bytes",
							len, expected_len);
		return cmd_status(sk, index, MGMT_OP_LOAD_LONG_TERM_KEYS,
								EINVAL);
	}

	hdev = hci_dev_get(index);
	if (!hdev)
		return cmd_status(sk, index, MGMT_OP_LOAD_LONG_TERM_KEYS,
								ENODEV);

	BT_DBG("hci%u key_count %u", index, key_count);

	hci_dev_lock(hdev);

	hci_smp_ltks_clear(hdev);

	for (i = 0; i < key_count; i++) {
		struct mgmt_ltk_info *key = &cp->keys[i];
		u8 type;

		if (key->master)
			type = HCI_SMP_LTK;
		else
			type = HCI_SMP_LTK_SLAVE;

		hci_add_ltk(hdev, &key->addr.bdaddr, key->addr.type,
					type, 0, key->authenticated, key->val,
					key->enc_size, key->ediv, key->rand);
	}

	hci_dev_unlock(hdev);
	hci_dev_put(hdev);

	return 0;
}

int mgmt_control(struct sock *sk, struct msghdr *msg, size_t msglen)
{
	void *buf;
	u8 *cp;
	struct mgmt_hdr *hdr;
	u16 opcode, index, len;
	int err;

	BT_DBG("got %zu bytes", msglen);

	if (msglen < sizeof(*hdr))
		return -EINVAL;

	buf = kmalloc(msglen, GFP_KERNEL);
	if (!buf)
		return -ENOMEM;

	if (memcpy_fromiovec(buf, msg->msg_iov, msglen)) {
		err = -EFAULT;
		goto done;
	}

	hdr = buf;
	opcode = get_unaligned_le16(&hdr->opcode);
	index = get_unaligned_le16(&hdr->index);
	len = get_unaligned_le16(&hdr->len);

	if (len != msglen - sizeof(*hdr)) {
		err = -EINVAL;
		goto done;
	}

	cp = buf + sizeof(*hdr);

	switch (opcode) {
	case MGMT_OP_READ_VERSION:
		err = read_version(sk);
		break;
	case MGMT_OP_READ_INDEX_LIST:
		err = read_index_list(sk);
		break;
	case MGMT_OP_READ_INFO:
		err = read_controller_info(sk, index);
		break;
	case MGMT_OP_SET_POWERED:
		err = set_powered(sk, index, cp, len);
		break;
	case MGMT_OP_SET_DISCOVERABLE:
		err = set_discoverable(sk, index, cp, len);
		break;
	case MGMT_OP_SET_CONNECTABLE:
		err = set_connectable(sk, index, cp, len);
		break;
	case MGMT_OP_SET_FAST_CONNECTABLE:
		err = set_fast_connectable(sk, index, cp, len);
		break;
	case MGMT_OP_SET_PAIRABLE:
		err = set_pairable(sk, index, cp, len);
		break;
	case MGMT_OP_ADD_UUID:
		err = add_uuid(sk, index, cp, len);
		break;
	case MGMT_OP_REMOVE_UUID:
		err = remove_uuid(sk, index, cp, len);
		break;
	case MGMT_OP_SET_DEV_CLASS:
		err = set_dev_class(sk, index, cp, len);
		break;
	case MGMT_OP_LOAD_LINK_KEYS:
		err = load_link_keys(sk, index, cp, len);
		break;
	case MGMT_OP_REMOVE_KEYS:
		err = remove_keys(sk, index, cp, len);
		break;
	case MGMT_OP_DISCONNECT:
		err = disconnect(sk, index, cp, len);
		break;
	case MGMT_OP_GET_CONNECTIONS:
		err = get_connections(sk, index);
		break;
	case MGMT_OP_PIN_CODE_REPLY:
		err = pin_code_reply(sk, index, cp, len);
		break;
	case MGMT_OP_PIN_CODE_NEG_REPLY:
		err = pin_code_neg_reply(sk, index, cp, len);
		break;
	case MGMT_OP_SET_IO_CAPABILITY:
		err = set_io_capability(sk, index, cp, len);
		break;
	case MGMT_OP_PAIR_DEVICE:
		err = pair_device(sk, index, cp, len);
		break;
	case MGMT_OP_CANCEL_PAIR_DEVICE:
		err = cancel_pair_device(sk, index, buf + sizeof(*hdr), len);
		break;
	case MGMT_OP_USER_CONFIRM_REPLY:
		err = user_confirm_reply(sk, index, cp, len);
		break;
	case MGMT_OP_USER_CONFIRM_NEG_REPLY:
		err = user_confirm_neg_reply(sk, index, cp, len);
		break;
	case MGMT_OP_USER_PASSKEY_REPLY:
		err = user_passkey_reply(sk, index, cp, len);
		break;
	case MGMT_OP_USER_PASSKEY_NEG_REPLY:
		err = user_passkey_neg_reply(sk, index, cp, len);
		break;
	case MGMT_OP_SET_LOCAL_NAME:
		err = set_local_name(sk, index, cp, len);
		break;
	case MGMT_OP_READ_LOCAL_OOB_DATA:
		err = read_local_oob_data(sk, index);
		break;
	case MGMT_OP_ADD_REMOTE_OOB_DATA:
		err = add_remote_oob_data(sk, index, cp, len);
		break;
	case MGMT_OP_REMOVE_REMOTE_OOB_DATA:
		err = remove_remote_oob_data(sk, index, cp, len);
		break;
	case MGMT_OP_START_DISCOVERY:
		err = start_discovery(sk, index, cp, len);
		break;
	case MGMT_OP_STOP_DISCOVERY:
		err = stop_discovery(sk, index);
		break;
	case MGMT_OP_CONFIRM_NAME:
		err = confirm_name(sk, index, cp, len);
		break;
	case MGMT_OP_BLOCK_DEVICE:
		err = block_device(sk, index, cp, len);
		break;
	case MGMT_OP_UNBLOCK_DEVICE:
		err = unblock_device(sk, index, cp, len);
		break;
	case MGMT_OP_LOAD_LONG_TERM_KEYS:
		err = load_long_term_keys(sk, index, cp, len);
		break;
	default:
		BT_DBG("Unknown op %u", opcode);
		err = cmd_status(sk, index, opcode,
						MGMT_STATUS_UNKNOWN_COMMAND);
		break;
	}

	if (err < 0)
		goto done;

	err = msglen;

done:
	kfree(buf);
	return err;
}

static void cmd_status_rsp(struct pending_cmd *cmd, void *data)
{
	u8 *status = data;

	cmd_status(cmd->sk, cmd->index, cmd->opcode, *status);
	mgmt_pending_remove(cmd);
}

int mgmt_index_added(struct hci_dev *hdev)
{
	return mgmt_event(MGMT_EV_INDEX_ADDED, hdev, NULL, 0, NULL);
}

int mgmt_index_removed(struct hci_dev *hdev)
{
	u8 status = ENODEV;

	mgmt_pending_foreach(0, hdev, cmd_status_rsp, &status);

	return mgmt_event(MGMT_EV_INDEX_REMOVED, hdev, NULL, 0, NULL);
}

struct cmd_lookup {
	u8 val;
	struct sock *sk;
	struct hci_dev *hdev;
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

int mgmt_powered(struct hci_dev *hdev, u8 powered)
{
	struct cmd_lookup match = { powered, NULL, hdev };
	__le32 ev;
	int ret;

	mgmt_pending_foreach(MGMT_OP_SET_POWERED, hdev, settings_rsp, &match);

	if (!powered) {
		u8 status = ENETDOWN;
		mgmt_pending_foreach(0, hdev, cmd_status_rsp, &status);
	}

	ev = cpu_to_le32(get_current_settings(hdev));

	ret = mgmt_event(MGMT_EV_NEW_SETTINGS, hdev, &ev, sizeof(ev),
								match.sk);

	if (match.sk)
		sock_put(match.sk);

	return ret;
}

int mgmt_discoverable(struct hci_dev *hdev, u8 discoverable)
{
	struct cmd_lookup match = { discoverable, NULL, hdev };
	__le32 ev;
	int ret;

	mgmt_pending_foreach(MGMT_OP_SET_DISCOVERABLE, hdev, settings_rsp, &match);

	ev = cpu_to_le32(get_current_settings(hdev));

	ret = mgmt_event(MGMT_EV_NEW_SETTINGS, hdev, &ev, sizeof(ev),
								match.sk);
	if (match.sk)
		sock_put(match.sk);

	return ret;
}

int mgmt_connectable(struct hci_dev *hdev, u8 connectable)
{
	__le32 ev;
	struct cmd_lookup match = { connectable, NULL, hdev };
	int ret;

	mgmt_pending_foreach(MGMT_OP_SET_CONNECTABLE, hdev, settings_rsp,
								&match);

	ev = cpu_to_le32(get_current_settings(hdev));

	ret = mgmt_event(MGMT_EV_NEW_SETTINGS, hdev, &ev, sizeof(ev), match.sk);

	if (match.sk)
		sock_put(match.sk);

	return ret;
}

int mgmt_write_scan_failed(struct hci_dev *hdev, u8 scan, u8 status)
{
	u8 mgmt_err = mgmt_status(status);

	if (scan & SCAN_PAGE)
		mgmt_pending_foreach(MGMT_OP_SET_CONNECTABLE, hdev,
						cmd_status_rsp, &mgmt_err);

	if (scan & SCAN_INQUIRY)
		mgmt_pending_foreach(MGMT_OP_SET_DISCOVERABLE, hdev,
						cmd_status_rsp, &mgmt_err);

	return 0;
}

int mgmt_new_link_key(struct hci_dev *hdev, struct link_key *key,
								u8 persistent)
{
	struct mgmt_ev_new_link_key ev;

	memset(&ev, 0, sizeof(ev));

	ev.store_hint = persistent;
	bacpy(&ev.key.bdaddr, &key->bdaddr);
	ev.key.type = key->type;
	memcpy(ev.key.val, key->val, 16);
	ev.key.pin_len = key->pin_len;

	return mgmt_event(MGMT_EV_NEW_LINK_KEY, hdev, &ev, sizeof(ev), NULL);
}

int mgmt_new_ltk(struct hci_dev *hdev, struct smp_ltk *key, u8 persistent)
{
	struct mgmt_ev_new_long_term_key ev;

	memset(&ev, 0, sizeof(ev));

	ev.store_hint = persistent;
	bacpy(&ev.key.addr.bdaddr, &key->bdaddr);
	ev.key.addr.type = key->bdaddr_type;
	ev.key.authenticated = key->authenticated;
	ev.key.enc_size = key->enc_size;
	ev.key.ediv = key->ediv;

	if (key->type == HCI_SMP_LTK)
		ev.key.master = 1;

	memcpy(ev.key.rand, key->rand, sizeof(key->rand));
	memcpy(ev.key.val, key->val, sizeof(key->val));

	return mgmt_event(MGMT_EV_NEW_LONG_TERM_KEY, hdev,
						&ev, sizeof(ev), NULL);
}

int mgmt_device_connected(struct hci_dev *hdev, bdaddr_t *bdaddr, u8 link_type,
					u8 addr_type, u8 *name, u8 name_len,
					u8 *dev_class)
{
	char buf[512];
	struct mgmt_ev_device_connected *ev = (void *) buf;
	u16 eir_len = 0;

	bacpy(&ev->addr.bdaddr, bdaddr);
	ev->addr.type = link_to_mgmt(link_type, addr_type);

	if (name_len > 0)
		eir_len = eir_append_data(ev->eir, 0, EIR_NAME_COMPLETE,
								name, name_len);

	if (dev_class && memcmp(dev_class, "\0\0\0", 3) != 0)
		eir_len = eir_append_data(&ev->eir[eir_len], eir_len,
					EIR_CLASS_OF_DEV, dev_class, 3);

	put_unaligned_le16(eir_len, &ev->eir_len);

	return mgmt_event(MGMT_EV_DEVICE_CONNECTED, hdev, buf,
						sizeof(*ev) + eir_len, NULL);
}

static void disconnect_rsp(struct pending_cmd *cmd, void *data)
{
	struct mgmt_cp_disconnect *cp = cmd->param;
	struct sock **sk = data;
	struct mgmt_rp_disconnect rp;

	bacpy(&rp.bdaddr, &cp->bdaddr);
	rp.status = 0;

	cmd_complete(cmd->sk, cmd->index, MGMT_OP_DISCONNECT, &rp, sizeof(rp));

	*sk = cmd->sk;
	sock_hold(*sk);

	mgmt_pending_remove(cmd);
}

static void remove_keys_rsp(struct pending_cmd *cmd, void *data)
{
	u8 *status = data;
	struct mgmt_cp_remove_keys *cp = cmd->param;
	struct mgmt_rp_remove_keys rp;

	memset(&rp, 0, sizeof(rp));
	bacpy(&rp.bdaddr, &cp->bdaddr);
	if (status != NULL)
		rp.status = *status;

	cmd_complete(cmd->sk, cmd->index, MGMT_OP_REMOVE_KEYS, &rp,
								sizeof(rp));

	mgmt_pending_remove(cmd);
}

int mgmt_device_disconnected(struct hci_dev *hdev, bdaddr_t *bdaddr,
						u8 link_type, u8 addr_type)
{
	struct mgmt_addr_info ev;
	struct sock *sk = NULL;
	int err;

	mgmt_pending_foreach(MGMT_OP_DISCONNECT, hdev, disconnect_rsp, &sk);

	bacpy(&ev.bdaddr, bdaddr);
	ev.type = link_to_mgmt(link_type, addr_type);

	err = mgmt_event(MGMT_EV_DEVICE_DISCONNECTED, hdev, &ev, sizeof(ev),
									sk);

	if (sk)
		sock_put(sk);

	mgmt_pending_foreach(MGMT_OP_REMOVE_KEYS, hdev, remove_keys_rsp, NULL);

	return err;
}

int mgmt_disconnect_failed(struct hci_dev *hdev, bdaddr_t *bdaddr, u8 status)
{
	struct pending_cmd *cmd;
	u8 mgmt_err = mgmt_status(status);
	int err;

	cmd = mgmt_pending_find(MGMT_OP_DISCONNECT, hdev);
	if (!cmd)
		return -ENOENT;

	if (bdaddr) {
		struct mgmt_rp_disconnect rp;

		bacpy(&rp.bdaddr, bdaddr);
		rp.status = status;

		err = cmd_complete(cmd->sk, cmd->index, MGMT_OP_DISCONNECT,
							&rp, sizeof(rp));
	} else
		err = cmd_status(cmd->sk, hdev->id, MGMT_OP_DISCONNECT,
								mgmt_err);

	mgmt_pending_remove(cmd);

	return err;
}

int mgmt_connect_failed(struct hci_dev *hdev, bdaddr_t *bdaddr, u8 link_type,
						u8 addr_type, u8 status)
{
	struct mgmt_ev_connect_failed ev;

	bacpy(&ev.addr.bdaddr, bdaddr);
	ev.addr.type = link_to_mgmt(link_type, addr_type);
	ev.status = mgmt_status(status);

	return mgmt_event(MGMT_EV_CONNECT_FAILED, hdev, &ev, sizeof(ev), NULL);
}

int mgmt_pin_code_request(struct hci_dev *hdev, bdaddr_t *bdaddr, u8 secure)
{
	struct mgmt_ev_pin_code_request ev;

	bacpy(&ev.bdaddr, bdaddr);
	ev.secure = secure;

	return mgmt_event(MGMT_EV_PIN_CODE_REQUEST, hdev, &ev, sizeof(ev),
									NULL);
}

int mgmt_pin_code_reply_complete(struct hci_dev *hdev, bdaddr_t *bdaddr,
								u8 status)
{
	struct pending_cmd *cmd;
	struct mgmt_rp_pin_code_reply rp;
	int err;

	cmd = mgmt_pending_find(MGMT_OP_PIN_CODE_REPLY, hdev);
	if (!cmd)
		return -ENOENT;

	bacpy(&rp.bdaddr, bdaddr);
	rp.status = mgmt_status(status);

	err = cmd_complete(cmd->sk, hdev->id, MGMT_OP_PIN_CODE_REPLY, &rp,
								sizeof(rp));

	mgmt_pending_remove(cmd);

	return err;
}

int mgmt_pin_code_neg_reply_complete(struct hci_dev *hdev, bdaddr_t *bdaddr,
								u8 status)
{
	struct pending_cmd *cmd;
	struct mgmt_rp_pin_code_reply rp;
	int err;

	cmd = mgmt_pending_find(MGMT_OP_PIN_CODE_NEG_REPLY, hdev);
	if (!cmd)
		return -ENOENT;

	bacpy(&rp.bdaddr, bdaddr);
	rp.status = mgmt_status(status);

	err = cmd_complete(cmd->sk, hdev->id, MGMT_OP_PIN_CODE_NEG_REPLY, &rp,
								sizeof(rp));

	mgmt_pending_remove(cmd);

	return err;
}

int mgmt_user_confirm_request(struct hci_dev *hdev, bdaddr_t *bdaddr,
						__le32 value, u8 confirm_hint)
{
	struct mgmt_ev_user_confirm_request ev;

	BT_DBG("%s", hdev->name);

	bacpy(&ev.bdaddr, bdaddr);
	ev.confirm_hint = confirm_hint;
	put_unaligned_le32(value, &ev.value);

	return mgmt_event(MGMT_EV_USER_CONFIRM_REQUEST, hdev, &ev, sizeof(ev),
									NULL);
}

int mgmt_user_passkey_request(struct hci_dev *hdev, bdaddr_t *bdaddr)
{
	struct mgmt_ev_user_passkey_request ev;

	BT_DBG("%s", hdev->name);

	bacpy(&ev.bdaddr, bdaddr);

	return mgmt_event(MGMT_EV_USER_PASSKEY_REQUEST, hdev, &ev, sizeof(ev),
									NULL);
}

static int user_pairing_resp_complete(struct hci_dev *hdev, bdaddr_t *bdaddr,
							u8 status, u8 opcode)
{
	struct pending_cmd *cmd;
	struct mgmt_rp_user_confirm_reply rp;
	int err;

	cmd = mgmt_pending_find(opcode, hdev);
	if (!cmd)
		return -ENOENT;

	bacpy(&rp.bdaddr, bdaddr);
	rp.status = mgmt_status(status);
	err = cmd_complete(cmd->sk, hdev->id, opcode, &rp, sizeof(rp));

	mgmt_pending_remove(cmd);

	return err;
}

int mgmt_user_confirm_reply_complete(struct hci_dev *hdev, bdaddr_t *bdaddr,
								u8 status)
{
	return user_pairing_resp_complete(hdev, bdaddr, status,
						MGMT_OP_USER_CONFIRM_REPLY);
}

int mgmt_user_confirm_neg_reply_complete(struct hci_dev *hdev,
						bdaddr_t *bdaddr, u8 status)
{
	return user_pairing_resp_complete(hdev, bdaddr, status,
					MGMT_OP_USER_CONFIRM_NEG_REPLY);
}

int mgmt_user_passkey_reply_complete(struct hci_dev *hdev, bdaddr_t *bdaddr,
								u8 status)
{
	return user_pairing_resp_complete(hdev, bdaddr, status,
						MGMT_OP_USER_PASSKEY_REPLY);
}

int mgmt_user_passkey_neg_reply_complete(struct hci_dev *hdev,
						bdaddr_t *bdaddr, u8 status)
{
	return user_pairing_resp_complete(hdev, bdaddr, status,
					MGMT_OP_USER_PASSKEY_NEG_REPLY);
}

int mgmt_auth_failed(struct hci_dev *hdev, bdaddr_t *bdaddr, u8 status)
{
	struct mgmt_ev_auth_failed ev;

	bacpy(&ev.bdaddr, bdaddr);
	ev.status = mgmt_status(status);

	return mgmt_event(MGMT_EV_AUTH_FAILED, hdev, &ev, sizeof(ev), NULL);
}

int mgmt_set_local_name_complete(struct hci_dev *hdev, u8 *name, u8 status)
{
	struct pending_cmd *cmd;
	struct mgmt_cp_set_local_name ev;
	int err;

	memset(&ev, 0, sizeof(ev));
	memcpy(ev.name, name, HCI_MAX_NAME_LENGTH);

	cmd = mgmt_pending_find(MGMT_OP_SET_LOCAL_NAME, hdev);
	if (!cmd)
		goto send_event;

	if (status) {
		err = cmd_status(cmd->sk, hdev->id, MGMT_OP_SET_LOCAL_NAME,
							mgmt_status(status));
		goto failed;
	}

	update_eir(hdev);

	err = cmd_complete(cmd->sk, hdev->id, MGMT_OP_SET_LOCAL_NAME, &ev,
								sizeof(ev));
	if (err < 0)
		goto failed;

send_event:
	err = mgmt_event(MGMT_EV_LOCAL_NAME_CHANGED, hdev, &ev, sizeof(ev),
							cmd ? cmd->sk : NULL);

failed:
	if (cmd)
		mgmt_pending_remove(cmd);
	return err;
}

int mgmt_read_local_oob_data_reply_complete(struct hci_dev *hdev, u8 *hash,
						u8 *randomizer, u8 status)
{
	struct pending_cmd *cmd;
	int err;

	BT_DBG("%s status %u", hdev->name, status);

	cmd = mgmt_pending_find(MGMT_OP_READ_LOCAL_OOB_DATA, hdev);
	if (!cmd)
		return -ENOENT;

	if (status) {
		err = cmd_status(cmd->sk, hdev->id,
						MGMT_OP_READ_LOCAL_OOB_DATA,
						mgmt_status(status));
	} else {
		struct mgmt_rp_read_local_oob_data rp;

		memcpy(rp.hash, hash, sizeof(rp.hash));
		memcpy(rp.randomizer, randomizer, sizeof(rp.randomizer));

		err = cmd_complete(cmd->sk, hdev->id,
						MGMT_OP_READ_LOCAL_OOB_DATA,
						&rp, sizeof(rp));
	}

	mgmt_pending_remove(cmd);

	return err;
}

int mgmt_device_found(struct hci_dev *hdev, bdaddr_t *bdaddr, u8 link_type,
				u8 addr_type, u8 *dev_class, s8 rssi,
				u8 cfm_name, u8 *eir, u16 eir_len)
{
	char buf[512];
	struct mgmt_ev_device_found *ev = (void *) buf;
	size_t ev_size;

	/* Leave 5 bytes for a potential CoD field */
	if (sizeof(*ev) + eir_len + 5 > sizeof(buf))
		return -EINVAL;

	memset(buf, 0, sizeof(buf));

	bacpy(&ev->addr.bdaddr, bdaddr);
	ev->addr.type = link_to_mgmt(link_type, addr_type);
	ev->rssi = rssi;
	ev->confirm_name = cfm_name;

	if (eir_len > 0)
		memcpy(ev->eir, eir, eir_len);

	if (dev_class && !eir_has_data_type(ev->eir, eir_len, EIR_CLASS_OF_DEV))
		eir_len = eir_append_data(ev->eir, eir_len, EIR_CLASS_OF_DEV,
								dev_class, 3);

	put_unaligned_le16(eir_len, &ev->eir_len);

	ev_size = sizeof(*ev) + eir_len;

	return mgmt_event(MGMT_EV_DEVICE_FOUND, hdev, ev, ev_size, NULL);
}

int mgmt_remote_name(struct hci_dev *hdev, bdaddr_t *bdaddr, u8 link_type,
				u8 addr_type, s8 rssi, u8 *name, u8 name_len)
{
	struct mgmt_ev_device_found *ev;
	char buf[sizeof(*ev) + HCI_MAX_NAME_LENGTH + 2];
	u16 eir_len;

	ev = (struct mgmt_ev_device_found *) buf;

	memset(buf, 0, sizeof(buf));

	bacpy(&ev->addr.bdaddr, bdaddr);
	ev->addr.type = link_to_mgmt(link_type, addr_type);
	ev->rssi = rssi;

	eir_len = eir_append_data(ev->eir, 0, EIR_NAME_COMPLETE, name,
								name_len);

	put_unaligned_le16(eir_len, &ev->eir_len);

	return mgmt_event(MGMT_EV_DEVICE_FOUND, hdev, ev,
						sizeof(*ev) + eir_len, NULL);
}

int mgmt_start_discovery_failed(struct hci_dev *hdev, u8 status)
{
	struct pending_cmd *cmd;
	int err;

	cmd = mgmt_pending_find(MGMT_OP_START_DISCOVERY, hdev);
	if (!cmd)
		return -ENOENT;

	err = cmd_status(cmd->sk, hdev->id, cmd->opcode, mgmt_status(status));
	mgmt_pending_remove(cmd);

	return err;
}

int mgmt_stop_discovery_failed(struct hci_dev *hdev, u8 status)
{
	struct pending_cmd *cmd;
	int err;

	cmd = mgmt_pending_find(MGMT_OP_STOP_DISCOVERY, hdev);
	if (!cmd)
		return -ENOENT;

	err = cmd_status(cmd->sk, hdev->id, cmd->opcode, mgmt_status(status));
	mgmt_pending_remove(cmd);

	return err;
}

int mgmt_discovering(struct hci_dev *hdev, u8 discovering)
{
	struct pending_cmd *cmd;

	if (discovering)
		cmd = mgmt_pending_find(MGMT_OP_START_DISCOVERY, hdev);
	else
		cmd = mgmt_pending_find(MGMT_OP_STOP_DISCOVERY, hdev);

	if (cmd != NULL) {
		cmd_complete(cmd->sk, hdev->id, cmd->opcode, NULL, 0);
		mgmt_pending_remove(cmd);
	}

	return mgmt_event(MGMT_EV_DISCOVERING, hdev, &discovering,
						sizeof(discovering), NULL);
}

int mgmt_device_blocked(struct hci_dev *hdev, bdaddr_t *bdaddr)
{
	struct pending_cmd *cmd;
	struct mgmt_ev_device_blocked ev;

	cmd = mgmt_pending_find(MGMT_OP_BLOCK_DEVICE, hdev);

	bacpy(&ev.bdaddr, bdaddr);

	return mgmt_event(MGMT_EV_DEVICE_BLOCKED, hdev, &ev, sizeof(ev),
							cmd ? cmd->sk : NULL);
}

int mgmt_device_unblocked(struct hci_dev *hdev, bdaddr_t *bdaddr)
{
	struct pending_cmd *cmd;
	struct mgmt_ev_device_unblocked ev;

	cmd = mgmt_pending_find(MGMT_OP_UNBLOCK_DEVICE, hdev);

	bacpy(&ev.bdaddr, bdaddr);

	return mgmt_event(MGMT_EV_DEVICE_UNBLOCKED, hdev, &ev, sizeof(ev),
							cmd ? cmd->sk : NULL);
}
