/*
   BlueZ - Bluetooth protocol stack for Linux

   Copyright (C) 2015  Intel Corporation

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

#include <linux/unaligned.h>

#include <net/bluetooth/bluetooth.h>
#include <net/bluetooth/hci_core.h>
#include <net/bluetooth/hci_mon.h>
#include <net/bluetooth/mgmt.h>

#include "mgmt_util.h"

static struct sk_buff *create_monitor_ctrl_event(__le16 index, u32 cookie,
						 u16 opcode, u16 len, void *buf)
{
	struct hci_mon_hdr *hdr;
	struct sk_buff *skb;

	skb = bt_skb_alloc(6 + len, GFP_ATOMIC);
	if (!skb)
		return NULL;

	put_unaligned_le32(cookie, skb_put(skb, 4));
	put_unaligned_le16(opcode, skb_put(skb, 2));

	if (buf)
		skb_put_data(skb, buf, len);

	__net_timestamp(skb);

	hdr = skb_push(skb, HCI_MON_HDR_SIZE);
	hdr->opcode = cpu_to_le16(HCI_MON_CTRL_EVENT);
	hdr->index = index;
	hdr->len = cpu_to_le16(skb->len - HCI_MON_HDR_SIZE);

	return skb;
}

struct sk_buff *mgmt_alloc_skb(struct hci_dev *hdev, u16 opcode,
			       unsigned int size)
{
	struct sk_buff *skb;

	skb = alloc_skb(sizeof(struct mgmt_hdr) + size, GFP_KERNEL);
	if (!skb)
		return skb;

	skb_reserve(skb, sizeof(struct mgmt_hdr));
	bt_cb(skb)->mgmt.hdev = hdev;
	bt_cb(skb)->mgmt.opcode = opcode;

	return skb;
}

int mgmt_send_event_skb(unsigned short channel, struct sk_buff *skb, int flag,
			struct sock *skip_sk)
{
	struct hci_dev *hdev;
	struct mgmt_hdr *hdr;
	int len;

	if (!skb)
		return -EINVAL;

	len = skb->len;
	hdev = bt_cb(skb)->mgmt.hdev;

	/* Time stamp */
	__net_timestamp(skb);

	/* Send just the data, without headers, to the monitor */
	if (channel == HCI_CHANNEL_CONTROL)
		hci_send_monitor_ctrl_event(hdev, bt_cb(skb)->mgmt.opcode,
					    skb->data, skb->len,
					    skb_get_ktime(skb), flag, skip_sk);

	hdr = skb_push(skb, sizeof(*hdr));
	hdr->opcode = cpu_to_le16(bt_cb(skb)->mgmt.opcode);
	if (hdev)
		hdr->index = cpu_to_le16(hdev->id);
	else
		hdr->index = cpu_to_le16(MGMT_INDEX_NONE);
	hdr->len = cpu_to_le16(len);

	hci_send_to_channel(channel, skb, flag, skip_sk);

	kfree_skb(skb);
	return 0;
}

int mgmt_send_event(u16 event, struct hci_dev *hdev, unsigned short channel,
		    void *data, u16 data_len, int flag, struct sock *skip_sk)
{
	struct sk_buff *skb;

	skb = mgmt_alloc_skb(hdev, event, data_len);
	if (!skb)
		return -ENOMEM;

	if (data)
		skb_put_data(skb, data, data_len);

	return mgmt_send_event_skb(channel, skb, flag, skip_sk);
}

int mgmt_cmd_status(struct sock *sk, u16 index, u16 cmd, u8 status)
{
	struct sk_buff *skb, *mskb;
	struct mgmt_hdr *hdr;
	struct mgmt_ev_cmd_status *ev;
	int err;

	BT_DBG("sock %p, index %u, cmd %u, status %u", sk, index, cmd, status);

	skb = alloc_skb(sizeof(*hdr) + sizeof(*ev), GFP_KERNEL);
	if (!skb)
		return -ENOMEM;

	hdr = skb_put(skb, sizeof(*hdr));

	hdr->opcode = cpu_to_le16(MGMT_EV_CMD_STATUS);
	hdr->index = cpu_to_le16(index);
	hdr->len = cpu_to_le16(sizeof(*ev));

	ev = skb_put(skb, sizeof(*ev));
	ev->status = status;
	ev->opcode = cpu_to_le16(cmd);

	mskb = create_monitor_ctrl_event(hdr->index, hci_sock_get_cookie(sk),
					 MGMT_EV_CMD_STATUS, sizeof(*ev), ev);
	if (mskb)
		skb->tstamp = mskb->tstamp;
	else
		__net_timestamp(skb);

	err = sock_queue_rcv_skb(sk, skb);
	if (err < 0)
		kfree_skb(skb);

	if (mskb) {
		hci_send_to_channel(HCI_CHANNEL_MONITOR, mskb,
				    HCI_SOCK_TRUSTED, NULL);
		kfree_skb(mskb);
	}

	return err;
}

int mgmt_cmd_complete(struct sock *sk, u16 index, u16 cmd, u8 status,
		      void *rp, size_t rp_len)
{
	struct sk_buff *skb, *mskb;
	struct mgmt_hdr *hdr;
	struct mgmt_ev_cmd_complete *ev;
	int err;

	BT_DBG("sock %p", sk);

	skb = alloc_skb(sizeof(*hdr) + sizeof(*ev) + rp_len, GFP_KERNEL);
	if (!skb)
		return -ENOMEM;

	hdr = skb_put(skb, sizeof(*hdr));

	hdr->opcode = cpu_to_le16(MGMT_EV_CMD_COMPLETE);
	hdr->index = cpu_to_le16(index);
	hdr->len = cpu_to_le16(sizeof(*ev) + rp_len);

	ev = skb_put(skb, sizeof(*ev) + rp_len);
	ev->opcode = cpu_to_le16(cmd);
	ev->status = status;

	if (rp)
		memcpy(ev->data, rp, rp_len);

	mskb = create_monitor_ctrl_event(hdr->index, hci_sock_get_cookie(sk),
					 MGMT_EV_CMD_COMPLETE,
					 sizeof(*ev) + rp_len, ev);
	if (mskb)
		skb->tstamp = mskb->tstamp;
	else
		__net_timestamp(skb);

	err = sock_queue_rcv_skb(sk, skb);
	if (err < 0)
		kfree_skb(skb);

	if (mskb) {
		hci_send_to_channel(HCI_CHANNEL_MONITOR, mskb,
				    HCI_SOCK_TRUSTED, NULL);
		kfree_skb(mskb);
	}

	return err;
}

struct mgmt_pending_cmd *mgmt_pending_find(unsigned short channel, u16 opcode,
					   struct hci_dev *hdev)
{
	struct mgmt_pending_cmd *cmd, *tmp;

	mutex_lock(&hdev->mgmt_pending_lock);

	list_for_each_entry_safe(cmd, tmp, &hdev->mgmt_pending, list) {
		if (hci_sock_get_channel(cmd->sk) != channel)
			continue;

		if (cmd->opcode == opcode) {
			mutex_unlock(&hdev->mgmt_pending_lock);
			return cmd;
		}
	}

	mutex_unlock(&hdev->mgmt_pending_lock);

	return NULL;
}

void mgmt_pending_foreach(u16 opcode, struct hci_dev *hdev, bool remove,
			  void (*cb)(struct mgmt_pending_cmd *cmd, void *data),
			  void *data)
{
	struct mgmt_pending_cmd *cmd, *tmp;

	mutex_lock(&hdev->mgmt_pending_lock);

	list_for_each_entry_safe(cmd, tmp, &hdev->mgmt_pending, list) {
		if (opcode > 0 && cmd->opcode != opcode)
			continue;

		if (remove)
			list_del(&cmd->list);

		cb(cmd, data);

		if (remove)
			mgmt_pending_free(cmd);
	}

	mutex_unlock(&hdev->mgmt_pending_lock);
}

struct mgmt_pending_cmd *mgmt_pending_new(struct sock *sk, u16 opcode,
					  struct hci_dev *hdev,
					  void *data, u16 len)
{
	struct mgmt_pending_cmd *cmd;

	cmd = kzalloc(sizeof(*cmd), GFP_KERNEL);
	if (!cmd)
		return NULL;

	cmd->opcode = opcode;
	cmd->hdev = hdev;

	cmd->param = kmemdup(data, len, GFP_KERNEL);
	if (!cmd->param) {
		kfree(cmd);
		return NULL;
	}

	cmd->param_len = len;

	cmd->sk = sk;
	sock_hold(sk);

	return cmd;
}

struct mgmt_pending_cmd *mgmt_pending_add(struct sock *sk, u16 opcode,
					  struct hci_dev *hdev,
					  void *data, u16 len)
{
	struct mgmt_pending_cmd *cmd;

	cmd = mgmt_pending_new(sk, opcode, hdev, data, len);
	if (!cmd)
		return NULL;

	mutex_lock(&hdev->mgmt_pending_lock);
	list_add_tail(&cmd->list, &hdev->mgmt_pending);
	mutex_unlock(&hdev->mgmt_pending_lock);

	return cmd;
}

void mgmt_pending_free(struct mgmt_pending_cmd *cmd)
{
	sock_put(cmd->sk);
	kfree(cmd->param);
	kfree(cmd);
}

void mgmt_pending_remove(struct mgmt_pending_cmd *cmd)
{
	mutex_lock(&cmd->hdev->mgmt_pending_lock);
	list_del(&cmd->list);
	mutex_unlock(&cmd->hdev->mgmt_pending_lock);

	mgmt_pending_free(cmd);
}

void mgmt_mesh_foreach(struct hci_dev *hdev,
		       void (*cb)(struct mgmt_mesh_tx *mesh_tx, void *data),
		       void *data, struct sock *sk)
{
	struct mgmt_mesh_tx *mesh_tx, *tmp;

	list_for_each_entry_safe(mesh_tx, tmp, &hdev->mesh_pending, list) {
		if (!sk || mesh_tx->sk == sk)
			cb(mesh_tx, data);
	}
}

struct mgmt_mesh_tx *mgmt_mesh_next(struct hci_dev *hdev, struct sock *sk)
{
	struct mgmt_mesh_tx *mesh_tx;

	if (list_empty(&hdev->mesh_pending))
		return NULL;

	list_for_each_entry(mesh_tx, &hdev->mesh_pending, list) {
		if (!sk || mesh_tx->sk == sk)
			return mesh_tx;
	}

	return NULL;
}

struct mgmt_mesh_tx *mgmt_mesh_find(struct hci_dev *hdev, u8 handle)
{
	struct mgmt_mesh_tx *mesh_tx;

	if (list_empty(&hdev->mesh_pending))
		return NULL;

	list_for_each_entry(mesh_tx, &hdev->mesh_pending, list) {
		if (mesh_tx->handle == handle)
			return mesh_tx;
	}

	return NULL;
}

struct mgmt_mesh_tx *mgmt_mesh_add(struct sock *sk, struct hci_dev *hdev,
				   void *data, u16 len)
{
	struct mgmt_mesh_tx *mesh_tx;

	mesh_tx = kzalloc(sizeof(*mesh_tx), GFP_KERNEL);
	if (!mesh_tx)
		return NULL;

	hdev->mesh_send_ref++;
	if (!hdev->mesh_send_ref)
		hdev->mesh_send_ref++;

	mesh_tx->handle = hdev->mesh_send_ref;
	mesh_tx->index = hdev->id;
	memcpy(mesh_tx->param, data, len);
	mesh_tx->param_len = len;
	mesh_tx->sk = sk;
	sock_hold(sk);

	list_add_tail(&mesh_tx->list, &hdev->mesh_pending);

	return mesh_tx;
}

void mgmt_mesh_remove(struct mgmt_mesh_tx *mesh_tx)
{
	list_del(&mesh_tx->list);
	sock_put(mesh_tx->sk);
	kfree(mesh_tx);
}
