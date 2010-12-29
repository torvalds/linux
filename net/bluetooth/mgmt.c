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

static int read_version(struct sock *sk)
{
	struct sk_buff *skb;
	struct mgmt_hdr *hdr;
	struct mgmt_ev_cmd_complete *ev;
	struct mgmt_rp_read_version *rp;

	BT_DBG("sock %p", sk);

	skb = alloc_skb(sizeof(*hdr) + sizeof(*ev) + sizeof(*rp), GFP_ATOMIC);
	if (!skb)
		return -ENOMEM;

	hdr = (void *) skb_put(skb, sizeof(*hdr));
	hdr->opcode = cpu_to_le16(MGMT_EV_CMD_COMPLETE);
	hdr->len = cpu_to_le16(sizeof(*ev) + sizeof(*rp));

	ev = (void *) skb_put(skb, sizeof(*ev));
	put_unaligned_le16(MGMT_OP_READ_VERSION, &ev->opcode);

	rp = (void *) skb_put(skb, sizeof(*rp));
	rp->version = MGMT_VERSION;
	put_unaligned_le16(MGMT_REVISION, &rp->revision);

	if (sock_queue_rcv_skb(sk, skb) < 0)
		kfree_skb(skb);

	return 0;
}

static int read_index_list(struct sock *sk)
{
	struct sk_buff *skb;
	struct mgmt_hdr *hdr;
	struct mgmt_ev_cmd_complete *ev;
	struct mgmt_rp_read_index_list *rp;
	struct list_head *p;
	size_t body_len;
	u16 count;
	int i;

	BT_DBG("sock %p", sk);

	read_lock(&hci_dev_list_lock);

	count = 0;
	list_for_each(p, &hci_dev_list) {
		count++;
	}

	body_len = sizeof(*ev) + sizeof(*rp) + (2 * count);
	skb = alloc_skb(sizeof(*hdr) + body_len, GFP_ATOMIC);
	if (!skb) {
		read_unlock(&hci_dev_list_lock);
		return -ENOMEM;
	}

	hdr = (void *) skb_put(skb, sizeof(*hdr));
	hdr->opcode = cpu_to_le16(MGMT_EV_CMD_COMPLETE);
	hdr->len = cpu_to_le16(body_len);

	ev = (void *) skb_put(skb, sizeof(*ev));
	put_unaligned_le16(MGMT_OP_READ_INDEX_LIST, &ev->opcode);

	rp = (void *) skb_put(skb, sizeof(*rp) + (2 * count));
	put_unaligned_le16(count, &rp->num_controllers);

	i = 0;
	list_for_each(p, &hci_dev_list) {
		struct hci_dev *d = list_entry(p, struct hci_dev, list);

		hci_del_off_timer(d);

		if (test_bit(HCI_SETUP, &d->flags))
			continue;

		put_unaligned_le16(d->id, &rp->index[i++]);
		BT_DBG("Added hci%u", d->id);
	}

	read_unlock(&hci_dev_list_lock);

	if (sock_queue_rcv_skb(sk, skb) < 0)
		kfree_skb(skb);

	return 0;
}

static int read_controller_info(struct sock *sk, unsigned char *data, u16 len)
{
	struct sk_buff *skb;
	struct mgmt_hdr *hdr;
	struct mgmt_ev_cmd_complete *ev;
	struct mgmt_rp_read_info *rp;
	struct mgmt_cp_read_info *cp;
	struct hci_dev *hdev;
	u16 dev_id;

	BT_DBG("sock %p", sk);

	if (len != 2)
		return cmd_status(sk, MGMT_OP_READ_INFO, EINVAL);

	skb = alloc_skb(sizeof(*hdr) + sizeof(*ev) + sizeof(*rp), GFP_ATOMIC);
	if (!skb)
		return -ENOMEM;

	hdr = (void *) skb_put(skb, sizeof(*hdr));
	hdr->opcode = cpu_to_le16(MGMT_EV_CMD_COMPLETE);
	hdr->len = cpu_to_le16(sizeof(*ev) + sizeof(*rp));

	ev = (void *) skb_put(skb, sizeof(*ev));
	put_unaligned_le16(MGMT_OP_READ_INFO, &ev->opcode);

	rp = (void *) skb_put(skb, sizeof(*rp));

	cp = (void *) data;
	dev_id = get_unaligned_le16(&cp->index);

	BT_DBG("request for hci%u", dev_id);

	hdev = hci_dev_get(dev_id);
	if (!hdev) {
		kfree_skb(skb);
		return cmd_status(sk, MGMT_OP_READ_INFO, ENODEV);
	}

	hci_del_off_timer(hdev);

	hci_dev_lock_bh(hdev);

	put_unaligned_le16(hdev->id, &rp->index);
	rp->type = hdev->dev_type;

	rp->powered = test_bit(HCI_UP, &hdev->flags);
	rp->discoverable = test_bit(HCI_ISCAN, &hdev->flags);
	rp->pairable = test_bit(HCI_PSCAN, &hdev->flags);

	if (test_bit(HCI_AUTH, &hdev->flags))
		rp->sec_mode = 3;
	else if (hdev->ssp_mode > 0)
		rp->sec_mode = 4;
	else
		rp->sec_mode = 2;

	bacpy(&rp->bdaddr, &hdev->bdaddr);
	memcpy(rp->features, hdev->features, 8);
	memcpy(rp->dev_class, hdev->dev_class, 3);
	put_unaligned_le16(hdev->manufacturer, &rp->manufacturer);
	rp->hci_ver = hdev->hci_ver;
	put_unaligned_le16(hdev->hci_rev, &rp->hci_rev);

	hci_dev_unlock_bh(hdev);
	hci_dev_put(hdev);

	if (sock_queue_rcv_skb(sk, skb) < 0)
		kfree_skb(skb);

	return 0;
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
	struct mgmt_cp_set_powered *cp;
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
	if ((cp->powered && up) || (!cp->powered && !up)) {
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

	if (cp->powered)
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
	struct mgmt_cp_set_discoverable *cp;
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
			mgmt_pending_find(MGMT_OP_SET_CONNECTABLE, dev_id) ||
			hci_sent_cmd_data(hdev, HCI_OP_WRITE_SCAN_ENABLE)) {
		err = cmd_status(sk, MGMT_OP_SET_DISCOVERABLE, EBUSY);
		goto failed;
	}

	if (cp->discoverable == test_bit(HCI_ISCAN, &hdev->flags) &&
					test_bit(HCI_PSCAN, &hdev->flags)) {
		err = cmd_status(sk, MGMT_OP_SET_DISCOVERABLE, EALREADY);
		goto failed;
	}

	err = mgmt_pending_add(sk, MGMT_OP_SET_DISCOVERABLE, dev_id, data, len);
	if (err < 0)
		goto failed;

	scan = SCAN_PAGE;

	if (cp->discoverable)
		scan |= SCAN_INQUIRY;

	err = hci_send_cmd(hdev, HCI_OP_WRITE_SCAN_ENABLE, 1, &scan);
	if (err < 0)
		mgmt_pending_remove(MGMT_OP_SET_DISCOVERABLE, dev_id);

failed:
	hci_dev_unlock_bh(hdev);
	hci_dev_put(hdev);

	return err;
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
	u8 value;
	struct sock *sk;
};

static void power_rsp(struct pending_cmd *cmd, void *data)
{
	struct mgmt_hdr *hdr;
	struct mgmt_ev_cmd_complete *ev;
	struct mgmt_rp_set_powered *rp;
	struct mgmt_cp_set_powered *cp = cmd->cmd;
	struct sk_buff *skb;
	struct cmd_lookup *match = data;

	if (cp->powered != match->value)
		return;

	skb = alloc_skb(sizeof(*hdr) + sizeof(*ev) + sizeof(*rp), GFP_ATOMIC);
	if (!skb)
		return;

	hdr = (void *) skb_put(skb, sizeof(*hdr));
	hdr->opcode = cpu_to_le16(MGMT_EV_CMD_COMPLETE);
	hdr->len = cpu_to_le16(sizeof(*ev) + sizeof(*rp));

	ev = (void *) skb_put(skb, sizeof(*ev));
	put_unaligned_le16(cmd->opcode, &ev->opcode);

	rp = (void *) skb_put(skb, sizeof(*rp));
	put_unaligned_le16(cmd->index, &rp->index);
	rp->powered = cp->powered;

	if (sock_queue_rcv_skb(cmd->sk, skb) < 0)
		kfree_skb(skb);

	list_del(&cmd->list);

	if (match->sk == NULL) {
		match->sk = cmd->sk;
		sock_hold(match->sk);
	}

	mgmt_pending_free(cmd);
}

int mgmt_powered(u16 index, u8 powered)
{
	struct mgmt_ev_powered ev;
	struct cmd_lookup match = { powered, NULL };
	int ret;

	put_unaligned_le16(index, &ev.index);
	ev.powered = powered;

	mgmt_pending_foreach(MGMT_OP_SET_POWERED, index, power_rsp, &match);

	ret = mgmt_event(MGMT_EV_POWERED, &ev, sizeof(ev), match.sk);

	if (match.sk)
		sock_put(match.sk);

	return ret;
}

static void discoverable_rsp(struct pending_cmd *cmd, void *data)
{
	struct mgmt_cp_set_discoverable *cp = cmd->cmd;
	struct cmd_lookup *match = data;
	struct sk_buff *skb;
	struct mgmt_hdr *hdr;
	struct mgmt_ev_cmd_complete *ev;
	struct mgmt_rp_set_discoverable *rp;

	if (cp->discoverable != match->value)
		return;

	skb = alloc_skb(sizeof(*hdr) + sizeof(*ev) + sizeof(*rp), GFP_ATOMIC);
	if (!skb)
		return;

	hdr = (void *) skb_put(skb, sizeof(*hdr));
	hdr->opcode = cpu_to_le16(MGMT_EV_CMD_COMPLETE);
	hdr->len = cpu_to_le16(sizeof(*ev) + sizeof(*rp));

	ev = (void *) skb_put(skb, sizeof(*ev));
	put_unaligned_le16(MGMT_OP_SET_DISCOVERABLE, &ev->opcode);

	rp = (void *) skb_put(skb, sizeof(*rp));
	put_unaligned_le16(cmd->index, &rp->index);
	rp->discoverable = cp->discoverable;

	if (sock_queue_rcv_skb(cmd->sk, skb) < 0)
		kfree_skb(skb);

	list_del(&cmd->list);

	if (match->sk == NULL) {
		match->sk = cmd->sk;
		sock_hold(match->sk);
	}

	mgmt_pending_free(cmd);
}

int mgmt_discoverable(u16 index, u8 discoverable)
{
	struct mgmt_ev_discoverable ev;
	struct cmd_lookup match = { discoverable, NULL };
	int ret;

	put_unaligned_le16(index, &ev.index);
	ev.discoverable = discoverable;

	mgmt_pending_foreach(MGMT_OP_SET_DISCOVERABLE, index,
						discoverable_rsp, &match);

	ret = mgmt_event(MGMT_EV_DISCOVERABLE, &ev, sizeof(ev), match.sk);

	if (match.sk)
		sock_put(match.sk);

	return ret;
}
