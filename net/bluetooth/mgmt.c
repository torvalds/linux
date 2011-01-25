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

#include <asm/uaccess.h>
#include <asm/unaligned.h>

#include <net/bluetooth/bluetooth.h>
#include <net/bluetooth/hci_core.h>
#include <net/bluetooth/mgmt.h>

#define MGMT_VERSION	0
#define MGMT_REVISION	1

struct pending_cmd {
	struct list_head list;
	__u16 opcode;
	int index;
	void *cmd;
	struct sock *sk;
};

LIST_HEAD(cmd_list);

static int cmd_status(struct sock *sk, u16 cmd, u8 status)
{
	struct sk_buff *skb;
	struct mgmt_hdr *hdr;
	struct mgmt_ev_cmd_status *ev;

	BT_DBG("sock %p", sk);

	skb = alloc_skb(sizeof(*hdr) + sizeof(*ev), GFP_ATOMIC);
	if (!skb)
		return -ENOMEM;

	hdr = (void *) skb_put(skb, sizeof(*hdr));

	hdr->opcode = cpu_to_le16(MGMT_EV_CMD_STATUS);
	hdr->len = cpu_to_le16(sizeof(*ev));

	ev = (void *) skb_put(skb, sizeof(*ev));
	ev->status = status;
	put_unaligned_le16(cmd, &ev->opcode);

	if (sock_queue_rcv_skb(sk, skb) < 0)
		kfree_skb(skb);

	return 0;
}

static int cmd_complete(struct sock *sk, u16 cmd, void *rp, size_t rp_len)
{
	struct sk_buff *skb;
	struct mgmt_hdr *hdr;
	struct mgmt_ev_cmd_complete *ev;

	BT_DBG("sock %p", sk);

	skb = alloc_skb(sizeof(*hdr) + sizeof(*ev) + rp_len, GFP_ATOMIC);
	if (!skb)
		return -ENOMEM;

	hdr = (void *) skb_put(skb, sizeof(*hdr));

	hdr->opcode = cpu_to_le16(MGMT_EV_CMD_COMPLETE);
	hdr->len = cpu_to_le16(sizeof(*ev) + rp_len);

	ev = (void *) skb_put(skb, sizeof(*ev) + rp_len);
	put_unaligned_le16(cmd, &ev->opcode);
	memcpy(ev->data, rp, rp_len);

	if (sock_queue_rcv_skb(sk, skb) < 0)
		kfree_skb(skb);

	return 0;
}

static int read_version(struct sock *sk)
{
	struct mgmt_rp_read_version rp;

	BT_DBG("sock %p", sk);

	rp.version = MGMT_VERSION;
	put_unaligned_le16(MGMT_REVISION, &rp.revision);

	return cmd_complete(sk, MGMT_OP_READ_VERSION, &rp, sizeof(rp));
}

static int read_index_list(struct sock *sk)
{
	struct mgmt_rp_read_index_list *rp;
	struct list_head *p;
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
	list_for_each(p, &hci_dev_list) {
		struct hci_dev *d = list_entry(p, struct hci_dev, list);

		hci_del_off_timer(d);

		set_bit(HCI_MGMT, &d->flags);

		if (test_bit(HCI_SETUP, &d->flags))
			continue;

		put_unaligned_le16(d->id, &rp->index[i++]);
		BT_DBG("Added hci%u", d->id);
	}

	read_unlock(&hci_dev_list_lock);

	err = cmd_complete(sk, MGMT_OP_READ_INDEX_LIST, rp, rp_len);

	kfree(rp);

	return err;
}

static int read_controller_info(struct sock *sk, unsigned char *data, u16 len)
{
	struct mgmt_rp_read_info rp;
	struct mgmt_cp_read_info *cp = (void *) data;
	struct hci_dev *hdev;
	u16 dev_id;

	BT_DBG("sock %p", sk);

	if (len != 2)
		return cmd_status(sk, MGMT_OP_READ_INFO, EINVAL);

	dev_id = get_unaligned_le16(&cp->index);

	BT_DBG("request for hci%u", dev_id);

	hdev = hci_dev_get(dev_id);
	if (!hdev)
		return cmd_status(sk, MGMT_OP_READ_INFO, ENODEV);

	hci_del_off_timer(hdev);

	hci_dev_lock_bh(hdev);

	set_bit(HCI_MGMT, &hdev->flags);

	put_unaligned_le16(hdev->id, &rp.index);
	rp.type = hdev->dev_type;

	rp.powered = test_bit(HCI_UP, &hdev->flags);
	rp.connectable = test_bit(HCI_PSCAN, &hdev->flags);
	rp.discoverable = test_bit(HCI_ISCAN, &hdev->flags);
	rp.pairable = test_bit(HCI_PSCAN, &hdev->flags);

	if (test_bit(HCI_AUTH, &hdev->flags))
		rp.sec_mode = 3;
	else if (hdev->ssp_mode > 0)
		rp.sec_mode = 4;
	else
		rp.sec_mode = 2;

	bacpy(&rp.bdaddr, &hdev->bdaddr);
	memcpy(rp.features, hdev->features, 8);
	memcpy(rp.dev_class, hdev->dev_class, 3);
	put_unaligned_le16(hdev->manufacturer, &rp.manufacturer);
	rp.hci_ver = hdev->hci_ver;
	put_unaligned_le16(hdev->hci_rev, &rp.hci_rev);

	hci_dev_unlock_bh(hdev);
	hci_dev_put(hdev);

	return cmd_complete(sk, MGMT_OP_READ_INFO, &rp, sizeof(rp));
}

static void mgmt_pending_free(struct pending_cmd *cmd)
{
	sock_put(cmd->sk);
	kfree(cmd->cmd);
	kfree(cmd);
}

static int mgmt_pending_add(struct sock *sk, u16 opcode, int index,
							void *data, u16 len)
{
	struct pending_cmd *cmd;

	cmd = kmalloc(sizeof(*cmd), GFP_ATOMIC);
	if (!cmd)
		return -ENOMEM;

	cmd->opcode = opcode;
	cmd->index = index;

	cmd->cmd = kmalloc(len, GFP_ATOMIC);
	if (!cmd->cmd) {
		kfree(cmd);
		return -ENOMEM;
	}

	memcpy(cmd->cmd, data, len);

	cmd->sk = sk;
	sock_hold(sk);

	list_add(&cmd->list, &cmd_list);

	return 0;
}

static void mgmt_pending_foreach(u16 opcode, int index,
				void (*cb)(struct pending_cmd *cmd, void *data),
				void *data)
{
	struct list_head *p, *n;

	list_for_each_safe(p, n, &cmd_list) {
		struct pending_cmd *cmd;

		cmd = list_entry(p, struct pending_cmd, list);

		if (cmd->opcode != opcode)
			continue;

		if (index >= 0 && cmd->index != index)
			continue;

		cb(cmd, data);
	}
}

static struct pending_cmd *mgmt_pending_find(u16 opcode, int index)
{
	struct list_head *p;

	list_for_each(p, &cmd_list) {
		struct pending_cmd *cmd;

		cmd = list_entry(p, struct pending_cmd, list);

		if (cmd->opcode != opcode)
			continue;

		if (index >= 0 && cmd->index != index)
			continue;

		return cmd;
	}

	return NULL;
}

static void mgmt_pending_remove(u16 opcode, int index)
{
	struct pending_cmd *cmd;

	cmd = mgmt_pending_find(opcode, index);
	if (cmd == NULL)
		return;

	list_del(&cmd->list);
	mgmt_pending_free(cmd);
}

static int set_powered(struct sock *sk, unsigned char *data, u16 len)
{
	struct mgmt_mode *cp;
	struct hci_dev *hdev;
	u16 dev_id;
	int ret, up;

	cp = (void *) data;
	dev_id = get_unaligned_le16(&cp->index);

	BT_DBG("request for hci%u", dev_id);

	hdev = hci_dev_get(dev_id);
	if (!hdev)
		return cmd_status(sk, MGMT_OP_SET_POWERED, ENODEV);

	hci_dev_lock_bh(hdev);

	up = test_bit(HCI_UP, &hdev->flags);
	if ((cp->val && up) || (!cp->val && !up)) {
		ret = cmd_status(sk, MGMT_OP_SET_POWERED, EALREADY);
		goto failed;
	}

	if (mgmt_pending_find(MGMT_OP_SET_POWERED, dev_id)) {
		ret = cmd_status(sk, MGMT_OP_SET_POWERED, EBUSY);
		goto failed;
	}

	ret = mgmt_pending_add(sk, MGMT_OP_SET_POWERED, dev_id, data, len);
	if (ret < 0)
		goto failed;

	if (cp->val)
		queue_work(hdev->workqueue, &hdev->power_on);
	else
		queue_work(hdev->workqueue, &hdev->power_off);

	ret = 0;

failed:
	hci_dev_unlock_bh(hdev);
	hci_dev_put(hdev);
	return ret;
}

static int set_discoverable(struct sock *sk, unsigned char *data, u16 len)
{
	struct mgmt_mode *cp;
	struct hci_dev *hdev;
	u16 dev_id;
	u8 scan;
	int err;

	cp = (void *) data;
	dev_id = get_unaligned_le16(&cp->index);

	BT_DBG("request for hci%u", dev_id);

	hdev = hci_dev_get(dev_id);
	if (!hdev)
		return cmd_status(sk, MGMT_OP_SET_DISCOVERABLE, ENODEV);

	hci_dev_lock_bh(hdev);

	if (!test_bit(HCI_UP, &hdev->flags)) {
		err = cmd_status(sk, MGMT_OP_SET_DISCOVERABLE, ENETDOWN);
		goto failed;
	}

	if (mgmt_pending_find(MGMT_OP_SET_DISCOVERABLE, dev_id) ||
			mgmt_pending_find(MGMT_OP_SET_CONNECTABLE, dev_id)) {
		err = cmd_status(sk, MGMT_OP_SET_DISCOVERABLE, EBUSY);
		goto failed;
	}

	if (cp->val == test_bit(HCI_ISCAN, &hdev->flags) &&
					test_bit(HCI_PSCAN, &hdev->flags)) {
		err = cmd_status(sk, MGMT_OP_SET_DISCOVERABLE, EALREADY);
		goto failed;
	}

	err = mgmt_pending_add(sk, MGMT_OP_SET_DISCOVERABLE, dev_id, data, len);
	if (err < 0)
		goto failed;

	scan = SCAN_PAGE;

	if (cp->val)
		scan |= SCAN_INQUIRY;

	err = hci_send_cmd(hdev, HCI_OP_WRITE_SCAN_ENABLE, 1, &scan);
	if (err < 0)
		mgmt_pending_remove(MGMT_OP_SET_DISCOVERABLE, dev_id);

failed:
	hci_dev_unlock_bh(hdev);
	hci_dev_put(hdev);

	return err;
}

static int set_connectable(struct sock *sk, unsigned char *data, u16 len)
{
	struct mgmt_mode *cp;
	struct hci_dev *hdev;
	u16 dev_id;
	u8 scan;
	int err;

	cp = (void *) data;
	dev_id = get_unaligned_le16(&cp->index);

	BT_DBG("request for hci%u", dev_id);

	hdev = hci_dev_get(dev_id);
	if (!hdev)
		return cmd_status(sk, MGMT_OP_SET_CONNECTABLE, ENODEV);

	hci_dev_lock_bh(hdev);

	if (!test_bit(HCI_UP, &hdev->flags)) {
		err = cmd_status(sk, MGMT_OP_SET_CONNECTABLE, ENETDOWN);
		goto failed;
	}

	if (mgmt_pending_find(MGMT_OP_SET_DISCOVERABLE, dev_id) ||
			mgmt_pending_find(MGMT_OP_SET_CONNECTABLE, dev_id)) {
		err = cmd_status(sk, MGMT_OP_SET_CONNECTABLE, EBUSY);
		goto failed;
	}

	if (cp->val == test_bit(HCI_PSCAN, &hdev->flags)) {
		err = cmd_status(sk, MGMT_OP_SET_CONNECTABLE, EALREADY);
		goto failed;
	}

	err = mgmt_pending_add(sk, MGMT_OP_SET_CONNECTABLE, dev_id, data, len);
	if (err < 0)
		goto failed;

	if (cp->val)
		scan = SCAN_PAGE;
	else
		scan = 0;

	err = hci_send_cmd(hdev, HCI_OP_WRITE_SCAN_ENABLE, 1, &scan);
	if (err < 0)
		mgmt_pending_remove(MGMT_OP_SET_CONNECTABLE, dev_id);

failed:
	hci_dev_unlock_bh(hdev);
	hci_dev_put(hdev);

	return err;
}

static int mgmt_event(u16 event, void *data, u16 data_len, struct sock *skip_sk)
{
	struct sk_buff *skb;
	struct mgmt_hdr *hdr;

	skb = alloc_skb(sizeof(*hdr) + data_len, GFP_ATOMIC);
	if (!skb)
		return -ENOMEM;

	bt_cb(skb)->channel = HCI_CHANNEL_CONTROL;

	hdr = (void *) skb_put(skb, sizeof(*hdr));
	hdr->opcode = cpu_to_le16(event);
	hdr->len = cpu_to_le16(data_len);

	memcpy(skb_put(skb, data_len), data, data_len);

	hci_send_to_sock(NULL, skb, skip_sk);
	kfree_skb(skb);

	return 0;
}

static int send_mode_rsp(struct sock *sk, u16 opcode, u16 index, u8 val)
{
	struct mgmt_mode rp;

	put_unaligned_le16(index, &rp.index);
	rp.val = val;

	return cmd_complete(sk, opcode, &rp, sizeof(rp));
}

static int set_pairable(struct sock *sk, unsigned char *data, u16 len)
{
	struct mgmt_mode *cp, ev;
	struct hci_dev *hdev;
	u16 dev_id;
	int err;

	cp = (void *) data;
	dev_id = get_unaligned_le16(&cp->index);

	BT_DBG("request for hci%u", dev_id);

	hdev = hci_dev_get(dev_id);
	if (!hdev)
		return cmd_status(sk, MGMT_OP_SET_PAIRABLE, ENODEV);

	hci_dev_lock_bh(hdev);

	if (cp->val)
		set_bit(HCI_PAIRABLE, &hdev->flags);
	else
		clear_bit(HCI_PAIRABLE, &hdev->flags);

	err = send_mode_rsp(sk, MGMT_OP_SET_PAIRABLE, dev_id, cp->val);
	if (err < 0)
		goto failed;

	put_unaligned_le16(dev_id, &ev.index);
	ev.val = cp->val;

	err = mgmt_event(MGMT_EV_PAIRABLE, &ev, sizeof(ev), sk);

failed:
	hci_dev_unlock_bh(hdev);
	hci_dev_put(hdev);

	return err;
}

static u8 get_service_classes(struct hci_dev *hdev)
{
	struct list_head *p;
	u8 val = 0;

	list_for_each(p, &hdev->uuids) {
		struct bt_uuid *uuid = list_entry(p, struct bt_uuid, list);

		val |= uuid->svc_hint;
	}

	return val;
}

static int update_class(struct hci_dev *hdev)
{
	u8 cod[3];

	BT_DBG("%s", hdev->name);

	if (test_bit(HCI_SERVICE_CACHE, &hdev->flags))
		return 0;

	cod[0] = hdev->minor_class;
	cod[1] = hdev->major_class;
	cod[2] = get_service_classes(hdev);

	if (memcmp(cod, hdev->dev_class, 3) == 0)
		return 0;

	return hci_send_cmd(hdev, HCI_OP_WRITE_CLASS_OF_DEV, sizeof(cod), cod);
}

static int add_uuid(struct sock *sk, unsigned char *data, u16 len)
{
	struct mgmt_cp_add_uuid *cp;
	struct hci_dev *hdev;
	struct bt_uuid *uuid;
	u16 dev_id;
	int err;

	cp = (void *) data;
	dev_id = get_unaligned_le16(&cp->index);

	BT_DBG("request for hci%u", dev_id);

	hdev = hci_dev_get(dev_id);
	if (!hdev)
		return cmd_status(sk, MGMT_OP_ADD_UUID, ENODEV);

	hci_dev_lock_bh(hdev);

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

	err = cmd_complete(sk, MGMT_OP_ADD_UUID, &dev_id, sizeof(dev_id));

failed:
	hci_dev_unlock_bh(hdev);
	hci_dev_put(hdev);

	return err;
}

static int remove_uuid(struct sock *sk, unsigned char *data, u16 len)
{
	struct list_head *p, *n;
	struct mgmt_cp_add_uuid *cp;
	struct hci_dev *hdev;
	u8 bt_uuid_any[] = { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };
	u16 dev_id;
	int err, found;

	cp = (void *) data;
	dev_id = get_unaligned_le16(&cp->index);

	BT_DBG("request for hci%u", dev_id);

	hdev = hci_dev_get(dev_id);
	if (!hdev)
		return cmd_status(sk, MGMT_OP_REMOVE_UUID, ENODEV);

	hci_dev_lock_bh(hdev);

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
		err = cmd_status(sk, MGMT_OP_REMOVE_UUID, ENOENT);
		goto unlock;
	}

	err = update_class(hdev);
	if (err < 0)
		goto unlock;

	err = cmd_complete(sk, MGMT_OP_REMOVE_UUID, &dev_id, sizeof(dev_id));

unlock:
	hci_dev_unlock_bh(hdev);
	hci_dev_put(hdev);

	return err;
}

static int set_dev_class(struct sock *sk, unsigned char *data, u16 len)
{
	struct hci_dev *hdev;
	struct mgmt_cp_set_dev_class *cp;
	u16 dev_id;
	int err;

	cp = (void *) data;
	dev_id = get_unaligned_le16(&cp->index);

	BT_DBG("request for hci%u", dev_id);

	hdev = hci_dev_get(dev_id);
	if (!hdev)
		return cmd_status(sk, MGMT_OP_SET_DEV_CLASS, ENODEV);

	hci_dev_lock_bh(hdev);

	hdev->major_class = cp->major;
	hdev->minor_class = cp->minor;

	err = update_class(hdev);

	if (err == 0)
		err = cmd_complete(sk, MGMT_OP_SET_DEV_CLASS, &dev_id,
							sizeof(dev_id));

	hci_dev_unlock_bh(hdev);
	hci_dev_put(hdev);

	return err;
}

static int set_service_cache(struct sock *sk, unsigned char *data, u16 len)
{
	struct hci_dev *hdev;
	struct mgmt_cp_set_service_cache *cp;
	u16 dev_id;
	int err;

	cp = (void *) data;
	dev_id = get_unaligned_le16(&cp->index);

	hdev = hci_dev_get(dev_id);
	if (!hdev)
		return cmd_status(sk, MGMT_OP_SET_SERVICE_CACHE, ENODEV);

	hci_dev_lock_bh(hdev);

	BT_DBG("hci%u enable %d", dev_id, cp->enable);

	if (cp->enable) {
		set_bit(HCI_SERVICE_CACHE, &hdev->flags);
		err = 0;
	} else {
		clear_bit(HCI_SERVICE_CACHE, &hdev->flags);
		err = update_class(hdev);
	}

	if (err == 0)
		err = cmd_complete(sk, MGMT_OP_SET_SERVICE_CACHE, &dev_id,
							sizeof(dev_id));

	hci_dev_unlock_bh(hdev);
	hci_dev_put(hdev);

	return err;
}

static int load_keys(struct sock *sk, unsigned char *data, u16 len)
{
	struct hci_dev *hdev;
	struct mgmt_cp_load_keys *cp;
	u16 dev_id, key_count, expected_len;
	int i;

	cp = (void *) data;
	dev_id = get_unaligned_le16(&cp->index);
	key_count = get_unaligned_le16(&cp->key_count);

	expected_len = sizeof(*cp) + key_count * sizeof(struct mgmt_key_info);
	if (expected_len != len) {
		BT_ERR("load_keys: expected %u bytes, got %u bytes",
							len, expected_len);
		return -EINVAL;
	}

	hdev = hci_dev_get(dev_id);
	if (!hdev)
		return cmd_status(sk, MGMT_OP_LOAD_KEYS, ENODEV);

	BT_DBG("hci%u debug_keys %u key_count %u", dev_id, cp->debug_keys,
								key_count);

	hci_dev_lock_bh(hdev);

	hci_link_keys_clear(hdev);

	set_bit(HCI_LINK_KEYS, &hdev->flags);

	if (cp->debug_keys)
		set_bit(HCI_DEBUG_KEYS, &hdev->flags);
	else
		clear_bit(HCI_DEBUG_KEYS, &hdev->flags);

	for (i = 0; i < key_count; i++) {
		struct mgmt_key_info *key = &cp->keys[i];

		hci_add_link_key(hdev, 0, &key->bdaddr, key->val, key->type,
								key->pin_len);
	}

	hci_dev_unlock_bh(hdev);
	hci_dev_put(hdev);

	return 0;
}

static int remove_key(struct sock *sk, unsigned char *data, u16 len)
{
	struct hci_dev *hdev;
	struct mgmt_cp_remove_key *cp;
	struct hci_conn *conn;
	u16 dev_id;
	int err;

	cp = (void *) data;
	dev_id = get_unaligned_le16(&cp->index);

	hdev = hci_dev_get(dev_id);
	if (!hdev)
		return cmd_status(sk, MGMT_OP_REMOVE_KEY, ENODEV);

	hci_dev_lock_bh(hdev);

	err = hci_remove_link_key(hdev, &cp->bdaddr);
	if (err < 0) {
		err = cmd_status(sk, MGMT_OP_REMOVE_KEY, -err);
		goto unlock;
	}

	err = 0;

	if (!test_bit(HCI_UP, &hdev->flags) || !cp->disconnect)
		goto unlock;

	conn = hci_conn_hash_lookup_ba(hdev, ACL_LINK, &cp->bdaddr);
	if (conn) {
		struct hci_cp_disconnect dc;

		put_unaligned_le16(conn->handle, &dc.handle);
		dc.reason = 0x13; /* Remote User Terminated Connection */
		err = hci_send_cmd(hdev, HCI_OP_DISCONNECT, 0, NULL);
	}

unlock:
	hci_dev_unlock_bh(hdev);
	hci_dev_put(hdev);

	return err;
}

static int disconnect(struct sock *sk, unsigned char *data, u16 len)
{
	struct hci_dev *hdev;
	struct mgmt_cp_disconnect *cp;
	struct hci_cp_disconnect dc;
	struct hci_conn *conn;
	u16 dev_id;
	int err;

	BT_DBG("");

	cp = (void *) data;
	dev_id = get_unaligned_le16(&cp->index);

	hdev = hci_dev_get(dev_id);
	if (!hdev)
		return cmd_status(sk, MGMT_OP_DISCONNECT, ENODEV);

	hci_dev_lock_bh(hdev);

	if (!test_bit(HCI_UP, &hdev->flags)) {
		err = cmd_status(sk, MGMT_OP_DISCONNECT, ENETDOWN);
		goto failed;
	}

	if (mgmt_pending_find(MGMT_OP_DISCONNECT, dev_id)) {
		err = cmd_status(sk, MGMT_OP_DISCONNECT, EBUSY);
		goto failed;
	}

	conn = hci_conn_hash_lookup_ba(hdev, ACL_LINK, &cp->bdaddr);
	if (!conn) {
		err = cmd_status(sk, MGMT_OP_DISCONNECT, ENOTCONN);
		goto failed;
	}

	err = mgmt_pending_add(sk, MGMT_OP_DISCONNECT, dev_id, data, len);
	if (err < 0)
		goto failed;

	put_unaligned_le16(conn->handle, &dc.handle);
	dc.reason = 0x13; /* Remote User Terminated Connection */

	err = hci_send_cmd(hdev, HCI_OP_DISCONNECT, sizeof(dc), &dc);
	if (err < 0)
		mgmt_pending_remove(MGMT_OP_DISCONNECT, dev_id);

failed:
	hci_dev_unlock_bh(hdev);
	hci_dev_put(hdev);

	return err;
}

static int get_connections(struct sock *sk, unsigned char *data, u16 len)
{
	struct mgmt_cp_get_connections *cp;
	struct mgmt_rp_get_connections *rp;
	struct hci_dev *hdev;
	struct list_head *p;
	size_t rp_len;
	u16 dev_id, count;
	int i, err;

	BT_DBG("");

	cp = (void *) data;
	dev_id = get_unaligned_le16(&cp->index);

	hdev = hci_dev_get(dev_id);
	if (!hdev)
		return cmd_status(sk, MGMT_OP_GET_CONNECTIONS, ENODEV);

	hci_dev_lock_bh(hdev);

	count = 0;
	list_for_each(p, &hdev->conn_hash.list) {
		count++;
	}

	rp_len = sizeof(*rp) + (count * sizeof(bdaddr_t));
	rp = kmalloc(rp_len, GFP_ATOMIC);
	if (!rp) {
		err = -ENOMEM;
		goto unlock;
	}

	put_unaligned_le16(dev_id, &rp->index);
	put_unaligned_le16(count, &rp->conn_count);

	read_lock(&hci_dev_list_lock);

	i = 0;
	list_for_each(p, &hdev->conn_hash.list) {
		struct hci_conn *c = list_entry(p, struct hci_conn, list);

		bacpy(&rp->conn[i++], &c->dst);
	}

	read_unlock(&hci_dev_list_lock);

	err = cmd_complete(sk, MGMT_OP_GET_CONNECTIONS, rp, rp_len);

unlock:
	kfree(rp);
	hci_dev_unlock_bh(hdev);
	hci_dev_put(hdev);
	return err;
}

static int pin_code_reply(struct sock *sk, unsigned char *data, u16 len)
{
	struct hci_dev *hdev;
	struct mgmt_cp_pin_code_reply *cp;
	struct hci_cp_pin_code_reply reply;
	u16 dev_id;
	int err;

	BT_DBG("");

	cp = (void *) data;
	dev_id = get_unaligned_le16(&cp->index);

	hdev = hci_dev_get(dev_id);
	if (!hdev)
		return cmd_status(sk, MGMT_OP_DISCONNECT, ENODEV);

	hci_dev_lock_bh(hdev);

	if (!test_bit(HCI_UP, &hdev->flags)) {
		err = cmd_status(sk, MGMT_OP_PIN_CODE_REPLY, ENETDOWN);
		goto failed;
	}

	err = mgmt_pending_add(sk, MGMT_OP_PIN_CODE_REPLY, dev_id, data, len);
	if (err < 0)
		goto failed;

	bacpy(&reply.bdaddr, &cp->bdaddr);
	reply.pin_len = cp->pin_len;
	memcpy(reply.pin_code, cp->pin_code, 16);

	err = hci_send_cmd(hdev, HCI_OP_PIN_CODE_REPLY, sizeof(reply), &reply);
	if (err < 0)
		mgmt_pending_remove(MGMT_OP_PIN_CODE_REPLY, dev_id);

failed:
	hci_dev_unlock_bh(hdev);
	hci_dev_put(hdev);

	return err;
}

static int pin_code_neg_reply(struct sock *sk, unsigned char *data, u16 len)
{
	struct hci_dev *hdev;
	struct mgmt_cp_pin_code_neg_reply *cp;
	u16 dev_id;
	int err;

	BT_DBG("");

	cp = (void *) data;
	dev_id = get_unaligned_le16(&cp->index);

	hdev = hci_dev_get(dev_id);
	if (!hdev)
		return cmd_status(sk, MGMT_OP_PIN_CODE_NEG_REPLY, ENODEV);

	hci_dev_lock_bh(hdev);

	if (!test_bit(HCI_UP, &hdev->flags)) {
		err = cmd_status(sk, MGMT_OP_PIN_CODE_NEG_REPLY, ENETDOWN);
		goto failed;
	}

	err = mgmt_pending_add(sk, MGMT_OP_PIN_CODE_NEG_REPLY, dev_id,
								data, len);
	if (err < 0)
		goto failed;

	err = hci_send_cmd(hdev, HCI_OP_PIN_CODE_NEG_REPLY, sizeof(bdaddr_t),
								&cp->bdaddr);
	if (err < 0)
		mgmt_pending_remove(MGMT_OP_PIN_CODE_NEG_REPLY, dev_id);

failed:
	hci_dev_unlock_bh(hdev);
	hci_dev_put(hdev);

	return err;
}

static int set_io_capability(struct sock *sk, unsigned char *data, u16 len)
{
	struct hci_dev *hdev;
	struct mgmt_cp_set_io_capability *cp;
	u16 dev_id;

	BT_DBG("");

	cp = (void *) data;
	dev_id = get_unaligned_le16(&cp->index);

	hdev = hci_dev_get(dev_id);
	if (!hdev)
		return cmd_status(sk, MGMT_OP_SET_IO_CAPABILITY, ENODEV);

	hci_dev_lock_bh(hdev);

	hdev->io_capability = cp->io_capability;

	BT_DBG("%s IO capability set to 0x%02x", hdev->name,
						hdev->io_capability);

	hci_dev_unlock_bh(hdev);
	hci_dev_put(hdev);

	return cmd_complete(sk, MGMT_OP_SET_IO_CAPABILITY,
						&dev_id, sizeof(dev_id));
}

int mgmt_control(struct sock *sk, struct msghdr *msg, size_t msglen)
{
	unsigned char *buf;
	struct mgmt_hdr *hdr;
	u16 opcode, len;
	int err;

	BT_DBG("got %zu bytes", msglen);

	if (msglen < sizeof(*hdr))
		return -EINVAL;

	buf = kmalloc(msglen, GFP_ATOMIC);
	if (!buf)
		return -ENOMEM;

	if (memcpy_fromiovec(buf, msg->msg_iov, msglen)) {
		err = -EFAULT;
		goto done;
	}

	hdr = (struct mgmt_hdr *) buf;
	opcode = get_unaligned_le16(&hdr->opcode);
	len = get_unaligned_le16(&hdr->len);

	if (len != msglen - sizeof(*hdr)) {
		err = -EINVAL;
		goto done;
	}

	switch (opcode) {
	case MGMT_OP_READ_VERSION:
		err = read_version(sk);
		break;
	case MGMT_OP_READ_INDEX_LIST:
		err = read_index_list(sk);
		break;
	case MGMT_OP_READ_INFO:
		err = read_controller_info(sk, buf + sizeof(*hdr), len);
		break;
	case MGMT_OP_SET_POWERED:
		err = set_powered(sk, buf + sizeof(*hdr), len);
		break;
	case MGMT_OP_SET_DISCOVERABLE:
		err = set_discoverable(sk, buf + sizeof(*hdr), len);
		break;
	case MGMT_OP_SET_CONNECTABLE:
		err = set_connectable(sk, buf + sizeof(*hdr), len);
		break;
	case MGMT_OP_SET_PAIRABLE:
		err = set_pairable(sk, buf + sizeof(*hdr), len);
		break;
	case MGMT_OP_ADD_UUID:
		err = add_uuid(sk, buf + sizeof(*hdr), len);
		break;
	case MGMT_OP_REMOVE_UUID:
		err = remove_uuid(sk, buf + sizeof(*hdr), len);
		break;
	case MGMT_OP_SET_DEV_CLASS:
		err = set_dev_class(sk, buf + sizeof(*hdr), len);
		break;
	case MGMT_OP_SET_SERVICE_CACHE:
		err = set_service_cache(sk, buf + sizeof(*hdr), len);
		break;
	case MGMT_OP_LOAD_KEYS:
		err = load_keys(sk, buf + sizeof(*hdr), len);
		break;
	case MGMT_OP_REMOVE_KEY:
		err = remove_key(sk, buf + sizeof(*hdr), len);
		break;
	case MGMT_OP_DISCONNECT:
		err = disconnect(sk, buf + sizeof(*hdr), len);
		break;
	case MGMT_OP_GET_CONNECTIONS:
		err = get_connections(sk, buf + sizeof(*hdr), len);
		break;
	case MGMT_OP_PIN_CODE_REPLY:
		err = pin_code_reply(sk, buf + sizeof(*hdr), len);
		break;
	case MGMT_OP_PIN_CODE_NEG_REPLY:
		err = pin_code_neg_reply(sk, buf + sizeof(*hdr), len);
		break;
	case MGMT_OP_SET_IO_CAPABILITY:
		err = set_io_capability(sk, buf + sizeof(*hdr), len);
		break;
	default:
		BT_DBG("Unknown op %u", opcode);
		err = cmd_status(sk, opcode, 0x01);
		break;
	}

	if (err < 0)
		goto done;

	err = msglen;

done:
	kfree(buf);
	return err;
}

int mgmt_index_added(u16 index)
{
	struct mgmt_ev_index_added ev;

	put_unaligned_le16(index, &ev.index);

	return mgmt_event(MGMT_EV_INDEX_ADDED, &ev, sizeof(ev), NULL);
}

int mgmt_index_removed(u16 index)
{
	struct mgmt_ev_index_added ev;

	put_unaligned_le16(index, &ev.index);

	return mgmt_event(MGMT_EV_INDEX_REMOVED, &ev, sizeof(ev), NULL);
}

struct cmd_lookup {
	u8 val;
	struct sock *sk;
};

static void mode_rsp(struct pending_cmd *cmd, void *data)
{
	struct mgmt_mode *cp = cmd->cmd;
	struct cmd_lookup *match = data;

	if (cp->val != match->val)
		return;

	send_mode_rsp(cmd->sk, cmd->opcode, cmd->index, cp->val);

	list_del(&cmd->list);

	if (match->sk == NULL) {
		match->sk = cmd->sk;
		sock_hold(match->sk);
	}

	mgmt_pending_free(cmd);
}

int mgmt_powered(u16 index, u8 powered)
{
	struct mgmt_mode ev;
	struct cmd_lookup match = { powered, NULL };
	int ret;

	mgmt_pending_foreach(MGMT_OP_SET_POWERED, index, mode_rsp, &match);

	put_unaligned_le16(index, &ev.index);
	ev.val = powered;

	ret = mgmt_event(MGMT_EV_POWERED, &ev, sizeof(ev), match.sk);

	if (match.sk)
		sock_put(match.sk);

	return ret;
}

int mgmt_discoverable(u16 index, u8 discoverable)
{
	struct mgmt_mode ev;
	struct cmd_lookup match = { discoverable, NULL };
	int ret;

	mgmt_pending_foreach(MGMT_OP_SET_DISCOVERABLE, index,
							mode_rsp, &match);

	put_unaligned_le16(index, &ev.index);
	ev.val = discoverable;

	ret = mgmt_event(MGMT_EV_DISCOVERABLE, &ev, sizeof(ev), match.sk);

	if (match.sk)
		sock_put(match.sk);

	return ret;
}

int mgmt_connectable(u16 index, u8 connectable)
{
	struct mgmt_mode ev;
	struct cmd_lookup match = { connectable, NULL };
	int ret;

	mgmt_pending_foreach(MGMT_OP_SET_CONNECTABLE, index, mode_rsp, &match);

	put_unaligned_le16(index, &ev.index);
	ev.val = connectable;

	ret = mgmt_event(MGMT_EV_CONNECTABLE, &ev, sizeof(ev), match.sk);

	if (match.sk)
		sock_put(match.sk);

	return ret;
}

int mgmt_new_key(u16 index, struct link_key *key, u8 old_key_type)
{
	struct mgmt_ev_new_key ev;

	memset(&ev, 0, sizeof(ev));

	put_unaligned_le16(index, &ev.index);

	bacpy(&ev.key.bdaddr, &key->bdaddr);
	ev.key.type = key->type;
	memcpy(ev.key.val, key->val, 16);
	ev.key.pin_len = key->pin_len;
	ev.old_key_type = old_key_type;

	return mgmt_event(MGMT_EV_NEW_KEY, &ev, sizeof(ev), NULL);
}

int mgmt_connected(u16 index, bdaddr_t *bdaddr)
{
	struct mgmt_ev_connected ev;

	put_unaligned_le16(index, &ev.index);
	bacpy(&ev.bdaddr, bdaddr);

	return mgmt_event(MGMT_EV_CONNECTED, &ev, sizeof(ev), NULL);
}

static void disconnect_rsp(struct pending_cmd *cmd, void *data)
{
	struct mgmt_cp_disconnect *cp = cmd->cmd;
	struct sock **sk = data;
	struct mgmt_rp_disconnect rp;

	put_unaligned_le16(cmd->index, &rp.index);
	bacpy(&rp.bdaddr, &cp->bdaddr);

	cmd_complete(cmd->sk, MGMT_OP_DISCONNECT, &rp, sizeof(rp));

	*sk = cmd->sk;
	sock_hold(*sk);

	list_del(&cmd->list);
	mgmt_pending_free(cmd);
}

int mgmt_disconnected(u16 index, bdaddr_t *bdaddr)
{
	struct mgmt_ev_disconnected ev;
	struct sock *sk = NULL;
	int err;

	mgmt_pending_foreach(MGMT_OP_DISCONNECT, index, disconnect_rsp, &sk);

	put_unaligned_le16(index, &ev.index);
	bacpy(&ev.bdaddr, bdaddr);

	err = mgmt_event(MGMT_EV_DISCONNECTED, &ev, sizeof(ev), sk);

	if (sk)
		sock_put(sk);

	return err;
}

int mgmt_disconnect_failed(u16 index)
{
	struct pending_cmd *cmd;
	int err;

	cmd = mgmt_pending_find(MGMT_OP_DISCONNECT, index);
	if (!cmd)
		return -ENOENT;

	err = cmd_status(cmd->sk, MGMT_OP_DISCONNECT, EIO);

	list_del(&cmd->list);
	mgmt_pending_free(cmd);

	return err;
}

int mgmt_connect_failed(u16 index, bdaddr_t *bdaddr, u8 status)
{
	struct mgmt_ev_connect_failed ev;

	put_unaligned_le16(index, &ev.index);
	bacpy(&ev.bdaddr, bdaddr);
	ev.status = status;

	return mgmt_event(MGMT_EV_CONNECT_FAILED, &ev, sizeof(ev), NULL);
}

int mgmt_pin_code_request(u16 index, bdaddr_t *bdaddr)
{
	struct mgmt_ev_pin_code_request ev;

	put_unaligned_le16(index, &ev.index);
	bacpy(&ev.bdaddr, bdaddr);

	return mgmt_event(MGMT_EV_PIN_CODE_REQUEST, &ev, sizeof(ev), NULL);
}

int mgmt_pin_code_reply_complete(u16 index, bdaddr_t *bdaddr, u8 status)
{
	struct pending_cmd *cmd;
	int err;

	cmd = mgmt_pending_find(MGMT_OP_PIN_CODE_REPLY, index);
	if (!cmd)
		return -ENOENT;

	if (status != 0)
		err = cmd_status(cmd->sk, MGMT_OP_PIN_CODE_REPLY, status);
	else
		err = cmd_complete(cmd->sk, MGMT_OP_PIN_CODE_REPLY,
						bdaddr, sizeof(*bdaddr));

	list_del(&cmd->list);
	mgmt_pending_free(cmd);

	return err;
}

int mgmt_pin_code_neg_reply_complete(u16 index, bdaddr_t *bdaddr, u8 status)
{
	struct pending_cmd *cmd;
	int err;

	cmd = mgmt_pending_find(MGMT_OP_PIN_CODE_NEG_REPLY, index);
	if (!cmd)
		return -ENOENT;

	if (status != 0)
		err = cmd_status(cmd->sk, MGMT_OP_PIN_CODE_NEG_REPLY, status);
	else
		err = cmd_complete(cmd->sk, MGMT_OP_PIN_CODE_NEG_REPLY,
						bdaddr, sizeof(*bdaddr));

	list_del(&cmd->list);
	mgmt_pending_free(cmd);

	return err;
}
