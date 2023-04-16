/*
   BlueZ - Bluetooth protocol stack for Linux
   Copyright (C) 2000-2001 Qualcomm Incorporated

   Written 2000,2001 by Maxim Krasnyansky <maxk@qualcomm.com>

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

/* Bluetooth HCI sockets. */
#include <linux/compat.h>
#include <linux/export.h>
#include <linux/utsname.h>
#include <linux/sched.h>
#include <asm/unaligned.h>

#include <net/bluetooth/bluetooth.h>
#include <net/bluetooth/hci_core.h>
#include <net/bluetooth/hci_mon.h>
#include <net/bluetooth/mgmt.h>

#include "mgmt_util.h"

static LIST_HEAD(mgmt_chan_list);
static DEFINE_MUTEX(mgmt_chan_list_lock);

static DEFINE_IDA(sock_cookie_ida);

static atomic_t monitor_promisc = ATOMIC_INIT(0);

/* ----- HCI socket interface ----- */

/* Socket info */
#define hci_pi(sk) ((struct hci_pinfo *) sk)

struct hci_pinfo {
	struct bt_sock    bt;
	struct hci_dev    *hdev;
	struct hci_filter filter;
	__u8              cmsg_mask;
	unsigned short    channel;
	unsigned long     flags;
	__u32             cookie;
	char              comm[TASK_COMM_LEN];
	__u16             mtu;
};

static struct hci_dev *hci_hdev_from_sock(struct sock *sk)
{
	struct hci_dev *hdev = hci_pi(sk)->hdev;

	if (!hdev)
		return ERR_PTR(-EBADFD);
	if (hci_dev_test_flag(hdev, HCI_UNREGISTER))
		return ERR_PTR(-EPIPE);
	return hdev;
}

void hci_sock_set_flag(struct sock *sk, int nr)
{
	set_bit(nr, &hci_pi(sk)->flags);
}

void hci_sock_clear_flag(struct sock *sk, int nr)
{
	clear_bit(nr, &hci_pi(sk)->flags);
}

int hci_sock_test_flag(struct sock *sk, int nr)
{
	return test_bit(nr, &hci_pi(sk)->flags);
}

unsigned short hci_sock_get_channel(struct sock *sk)
{
	return hci_pi(sk)->channel;
}

u32 hci_sock_get_cookie(struct sock *sk)
{
	return hci_pi(sk)->cookie;
}

static bool hci_sock_gen_cookie(struct sock *sk)
{
	int id = hci_pi(sk)->cookie;

	if (!id) {
		id = ida_simple_get(&sock_cookie_ida, 1, 0, GFP_KERNEL);
		if (id < 0)
			id = 0xffffffff;

		hci_pi(sk)->cookie = id;
		get_task_comm(hci_pi(sk)->comm, current);
		return true;
	}

	return false;
}

static void hci_sock_free_cookie(struct sock *sk)
{
	int id = hci_pi(sk)->cookie;

	if (id) {
		hci_pi(sk)->cookie = 0xffffffff;
		ida_simple_remove(&sock_cookie_ida, id);
	}
}

static inline int hci_test_bit(int nr, const void *addr)
{
	return *((const __u32 *) addr + (nr >> 5)) & ((__u32) 1 << (nr & 31));
}

/* Security filter */
#define HCI_SFLT_MAX_OGF  5

struct hci_sec_filter {
	__u32 type_mask;
	__u32 event_mask[2];
	__u32 ocf_mask[HCI_SFLT_MAX_OGF + 1][4];
};

static const struct hci_sec_filter hci_sec_filter = {
	/* Packet types */
	0x10,
	/* Events */
	{ 0x1000d9fe, 0x0000b00c },
	/* Commands */
	{
		{ 0x0 },
		/* OGF_LINK_CTL */
		{ 0xbe000006, 0x00000001, 0x00000000, 0x00 },
		/* OGF_LINK_POLICY */
		{ 0x00005200, 0x00000000, 0x00000000, 0x00 },
		/* OGF_HOST_CTL */
		{ 0xaab00200, 0x2b402aaa, 0x05220154, 0x00 },
		/* OGF_INFO_PARAM */
		{ 0x000002be, 0x00000000, 0x00000000, 0x00 },
		/* OGF_STATUS_PARAM */
		{ 0x000000ea, 0x00000000, 0x00000000, 0x00 }
	}
};

static struct bt_sock_list hci_sk_list = {
	.lock = __RW_LOCK_UNLOCKED(hci_sk_list.lock)
};

static bool is_filtered_packet(struct sock *sk, struct sk_buff *skb)
{
	struct hci_filter *flt;
	int flt_type, flt_event;

	/* Apply filter */
	flt = &hci_pi(sk)->filter;

	flt_type = hci_skb_pkt_type(skb) & HCI_FLT_TYPE_BITS;

	if (!test_bit(flt_type, &flt->type_mask))
		return true;

	/* Extra filter for event packets only */
	if (hci_skb_pkt_type(skb) != HCI_EVENT_PKT)
		return false;

	flt_event = (*(__u8 *)skb->data & HCI_FLT_EVENT_BITS);

	if (!hci_test_bit(flt_event, &flt->event_mask))
		return true;

	/* Check filter only when opcode is set */
	if (!flt->opcode)
		return false;

	if (flt_event == HCI_EV_CMD_COMPLETE &&
	    flt->opcode != get_unaligned((__le16 *)(skb->data + 3)))
		return true;

	if (flt_event == HCI_EV_CMD_STATUS &&
	    flt->opcode != get_unaligned((__le16 *)(skb->data + 4)))
		return true;

	return false;
}

/* Send frame to RAW socket */
void hci_send_to_sock(struct hci_dev *hdev, struct sk_buff *skb)
{
	struct sock *sk;
	struct sk_buff *skb_copy = NULL;

	BT_DBG("hdev %p len %d", hdev, skb->len);

	read_lock(&hci_sk_list.lock);

	sk_for_each(sk, &hci_sk_list.head) {
		struct sk_buff *nskb;

		if (sk->sk_state != BT_BOUND || hci_pi(sk)->hdev != hdev)
			continue;

		/* Don't send frame to the socket it came from */
		if (skb->sk == sk)
			continue;

		if (hci_pi(sk)->channel == HCI_CHANNEL_RAW) {
			if (hci_skb_pkt_type(skb) != HCI_COMMAND_PKT &&
			    hci_skb_pkt_type(skb) != HCI_EVENT_PKT &&
			    hci_skb_pkt_type(skb) != HCI_ACLDATA_PKT &&
			    hci_skb_pkt_type(skb) != HCI_SCODATA_PKT &&
			    hci_skb_pkt_type(skb) != HCI_ISODATA_PKT)
				continue;
			if (is_filtered_packet(sk, skb))
				continue;
		} else if (hci_pi(sk)->channel == HCI_CHANNEL_USER) {
			if (!bt_cb(skb)->incoming)
				continue;
			if (hci_skb_pkt_type(skb) != HCI_EVENT_PKT &&
			    hci_skb_pkt_type(skb) != HCI_ACLDATA_PKT &&
			    hci_skb_pkt_type(skb) != HCI_SCODATA_PKT &&
			    hci_skb_pkt_type(skb) != HCI_ISODATA_PKT)
				continue;
		} else {
			/* Don't send frame to other channel types */
			continue;
		}

		if (!skb_copy) {
			/* Create a private copy with headroom */
			skb_copy = __pskb_copy_fclone(skb, 1, GFP_ATOMIC, true);
			if (!skb_copy)
				continue;

			/* Put type byte before the data */
			memcpy(skb_push(skb_copy, 1), &hci_skb_pkt_type(skb), 1);
		}

		nskb = skb_clone(skb_copy, GFP_ATOMIC);
		if (!nskb)
			continue;

		if (sock_queue_rcv_skb(sk, nskb))
			kfree_skb(nskb);
	}

	read_unlock(&hci_sk_list.lock);

	kfree_skb(skb_copy);
}

/* Send frame to sockets with specific channel */
static void __hci_send_to_channel(unsigned short channel, struct sk_buff *skb,
				  int flag, struct sock *skip_sk)
{
	struct sock *sk;

	BT_DBG("channel %u len %d", channel, skb->len);

	sk_for_each(sk, &hci_sk_list.head) {
		struct sk_buff *nskb;

		/* Ignore socket without the flag set */
		if (!hci_sock_test_flag(sk, flag))
			continue;

		/* Skip the original socket */
		if (sk == skip_sk)
			continue;

		if (sk->sk_state != BT_BOUND)
			continue;

		if (hci_pi(sk)->channel != channel)
			continue;

		nskb = skb_clone(skb, GFP_ATOMIC);
		if (!nskb)
			continue;

		if (sock_queue_rcv_skb(sk, nskb))
			kfree_skb(nskb);
	}

}

void hci_send_to_channel(unsigned short channel, struct sk_buff *skb,
			 int flag, struct sock *skip_sk)
{
	read_lock(&hci_sk_list.lock);
	__hci_send_to_channel(channel, skb, flag, skip_sk);
	read_unlock(&hci_sk_list.lock);
}

/* Send frame to monitor socket */
void hci_send_to_monitor(struct hci_dev *hdev, struct sk_buff *skb)
{
	struct sk_buff *skb_copy = NULL;
	struct hci_mon_hdr *hdr;
	__le16 opcode;

	if (!atomic_read(&monitor_promisc))
		return;

	BT_DBG("hdev %p len %d", hdev, skb->len);

	switch (hci_skb_pkt_type(skb)) {
	case HCI_COMMAND_PKT:
		opcode = cpu_to_le16(HCI_MON_COMMAND_PKT);
		break;
	case HCI_EVENT_PKT:
		opcode = cpu_to_le16(HCI_MON_EVENT_PKT);
		break;
	case HCI_ACLDATA_PKT:
		if (bt_cb(skb)->incoming)
			opcode = cpu_to_le16(HCI_MON_ACL_RX_PKT);
		else
			opcode = cpu_to_le16(HCI_MON_ACL_TX_PKT);
		break;
	case HCI_SCODATA_PKT:
		if (bt_cb(skb)->incoming)
			opcode = cpu_to_le16(HCI_MON_SCO_RX_PKT);
		else
			opcode = cpu_to_le16(HCI_MON_SCO_TX_PKT);
		break;
	case HCI_ISODATA_PKT:
		if (bt_cb(skb)->incoming)
			opcode = cpu_to_le16(HCI_MON_ISO_RX_PKT);
		else
			opcode = cpu_to_le16(HCI_MON_ISO_TX_PKT);
		break;
	case HCI_DIAG_PKT:
		opcode = cpu_to_le16(HCI_MON_VENDOR_DIAG);
		break;
	default:
		return;
	}

	/* Create a private copy with headroom */
	skb_copy = __pskb_copy_fclone(skb, HCI_MON_HDR_SIZE, GFP_ATOMIC, true);
	if (!skb_copy)
		return;

	/* Put header before the data */
	hdr = skb_push(skb_copy, HCI_MON_HDR_SIZE);
	hdr->opcode = opcode;
	hdr->index = cpu_to_le16(hdev->id);
	hdr->len = cpu_to_le16(skb->len);

	hci_send_to_channel(HCI_CHANNEL_MONITOR, skb_copy,
			    HCI_SOCK_TRUSTED, NULL);
	kfree_skb(skb_copy);
}

void hci_send_monitor_ctrl_event(struct hci_dev *hdev, u16 event,
				 void *data, u16 data_len, ktime_t tstamp,
				 int flag, struct sock *skip_sk)
{
	struct sock *sk;
	__le16 index;

	if (hdev)
		index = cpu_to_le16(hdev->id);
	else
		index = cpu_to_le16(MGMT_INDEX_NONE);

	read_lock(&hci_sk_list.lock);

	sk_for_each(sk, &hci_sk_list.head) {
		struct hci_mon_hdr *hdr;
		struct sk_buff *skb;

		if (hci_pi(sk)->channel != HCI_CHANNEL_CONTROL)
			continue;

		/* Ignore socket without the flag set */
		if (!hci_sock_test_flag(sk, flag))
			continue;

		/* Skip the original socket */
		if (sk == skip_sk)
			continue;

		skb = bt_skb_alloc(6 + data_len, GFP_ATOMIC);
		if (!skb)
			continue;

		put_unaligned_le32(hci_pi(sk)->cookie, skb_put(skb, 4));
		put_unaligned_le16(event, skb_put(skb, 2));

		if (data)
			skb_put_data(skb, data, data_len);

		skb->tstamp = tstamp;

		hdr = skb_push(skb, HCI_MON_HDR_SIZE);
		hdr->opcode = cpu_to_le16(HCI_MON_CTRL_EVENT);
		hdr->index = index;
		hdr->len = cpu_to_le16(skb->len - HCI_MON_HDR_SIZE);

		__hci_send_to_channel(HCI_CHANNEL_MONITOR, skb,
				      HCI_SOCK_TRUSTED, NULL);
		kfree_skb(skb);
	}

	read_unlock(&hci_sk_list.lock);
}

static struct sk_buff *create_monitor_event(struct hci_dev *hdev, int event)
{
	struct hci_mon_hdr *hdr;
	struct hci_mon_new_index *ni;
	struct hci_mon_index_info *ii;
	struct sk_buff *skb;
	__le16 opcode;

	switch (event) {
	case HCI_DEV_REG:
		skb = bt_skb_alloc(HCI_MON_NEW_INDEX_SIZE, GFP_ATOMIC);
		if (!skb)
			return NULL;

		ni = skb_put(skb, HCI_MON_NEW_INDEX_SIZE);
		ni->type = hdev->dev_type;
		ni->bus = hdev->bus;
		bacpy(&ni->bdaddr, &hdev->bdaddr);
		memcpy(ni->name, hdev->name, 8);

		opcode = cpu_to_le16(HCI_MON_NEW_INDEX);
		break;

	case HCI_DEV_UNREG:
		skb = bt_skb_alloc(0, GFP_ATOMIC);
		if (!skb)
			return NULL;

		opcode = cpu_to_le16(HCI_MON_DEL_INDEX);
		break;

	case HCI_DEV_SETUP:
		if (hdev->manufacturer == 0xffff)
			return NULL;
		fallthrough;

	case HCI_DEV_UP:
		skb = bt_skb_alloc(HCI_MON_INDEX_INFO_SIZE, GFP_ATOMIC);
		if (!skb)
			return NULL;

		ii = skb_put(skb, HCI_MON_INDEX_INFO_SIZE);
		bacpy(&ii->bdaddr, &hdev->bdaddr);
		ii->manufacturer = cpu_to_le16(hdev->manufacturer);

		opcode = cpu_to_le16(HCI_MON_INDEX_INFO);
		break;

	case HCI_DEV_OPEN:
		skb = bt_skb_alloc(0, GFP_ATOMIC);
		if (!skb)
			return NULL;

		opcode = cpu_to_le16(HCI_MON_OPEN_INDEX);
		break;

	case HCI_DEV_CLOSE:
		skb = bt_skb_alloc(0, GFP_ATOMIC);
		if (!skb)
			return NULL;

		opcode = cpu_to_le16(HCI_MON_CLOSE_INDEX);
		break;

	default:
		return NULL;
	}

	__net_timestamp(skb);

	hdr = skb_push(skb, HCI_MON_HDR_SIZE);
	hdr->opcode = opcode;
	hdr->index = cpu_to_le16(hdev->id);
	hdr->len = cpu_to_le16(skb->len - HCI_MON_HDR_SIZE);

	return skb;
}

static struct sk_buff *create_monitor_ctrl_open(struct sock *sk)
{
	struct hci_mon_hdr *hdr;
	struct sk_buff *skb;
	u16 format;
	u8 ver[3];
	u32 flags;

	/* No message needed when cookie is not present */
	if (!hci_pi(sk)->cookie)
		return NULL;

	switch (hci_pi(sk)->channel) {
	case HCI_CHANNEL_RAW:
		format = 0x0000;
		ver[0] = BT_SUBSYS_VERSION;
		put_unaligned_le16(BT_SUBSYS_REVISION, ver + 1);
		break;
	case HCI_CHANNEL_USER:
		format = 0x0001;
		ver[0] = BT_SUBSYS_VERSION;
		put_unaligned_le16(BT_SUBSYS_REVISION, ver + 1);
		break;
	case HCI_CHANNEL_CONTROL:
		format = 0x0002;
		mgmt_fill_version_info(ver);
		break;
	default:
		/* No message for unsupported format */
		return NULL;
	}

	skb = bt_skb_alloc(14 + TASK_COMM_LEN , GFP_ATOMIC);
	if (!skb)
		return NULL;

	flags = hci_sock_test_flag(sk, HCI_SOCK_TRUSTED) ? 0x1 : 0x0;

	put_unaligned_le32(hci_pi(sk)->cookie, skb_put(skb, 4));
	put_unaligned_le16(format, skb_put(skb, 2));
	skb_put_data(skb, ver, sizeof(ver));
	put_unaligned_le32(flags, skb_put(skb, 4));
	skb_put_u8(skb, TASK_COMM_LEN);
	skb_put_data(skb, hci_pi(sk)->comm, TASK_COMM_LEN);

	__net_timestamp(skb);

	hdr = skb_push(skb, HCI_MON_HDR_SIZE);
	hdr->opcode = cpu_to_le16(HCI_MON_CTRL_OPEN);
	if (hci_pi(sk)->hdev)
		hdr->index = cpu_to_le16(hci_pi(sk)->hdev->id);
	else
		hdr->index = cpu_to_le16(HCI_DEV_NONE);
	hdr->len = cpu_to_le16(skb->len - HCI_MON_HDR_SIZE);

	return skb;
}

static struct sk_buff *create_monitor_ctrl_close(struct sock *sk)
{
	struct hci_mon_hdr *hdr;
	struct sk_buff *skb;

	/* No message needed when cookie is not present */
	if (!hci_pi(sk)->cookie)
		return NULL;

	switch (hci_pi(sk)->channel) {
	case HCI_CHANNEL_RAW:
	case HCI_CHANNEL_USER:
	case HCI_CHANNEL_CONTROL:
		break;
	default:
		/* No message for unsupported format */
		return NULL;
	}

	skb = bt_skb_alloc(4, GFP_ATOMIC);
	if (!skb)
		return NULL;

	put_unaligned_le32(hci_pi(sk)->cookie, skb_put(skb, 4));

	__net_timestamp(skb);

	hdr = skb_push(skb, HCI_MON_HDR_SIZE);
	hdr->opcode = cpu_to_le16(HCI_MON_CTRL_CLOSE);
	if (hci_pi(sk)->hdev)
		hdr->index = cpu_to_le16(hci_pi(sk)->hdev->id);
	else
		hdr->index = cpu_to_le16(HCI_DEV_NONE);
	hdr->len = cpu_to_le16(skb->len - HCI_MON_HDR_SIZE);

	return skb;
}

static struct sk_buff *create_monitor_ctrl_command(struct sock *sk, u16 index,
						   u16 opcode, u16 len,
						   const void *buf)
{
	struct hci_mon_hdr *hdr;
	struct sk_buff *skb;

	skb = bt_skb_alloc(6 + len, GFP_ATOMIC);
	if (!skb)
		return NULL;

	put_unaligned_le32(hci_pi(sk)->cookie, skb_put(skb, 4));
	put_unaligned_le16(opcode, skb_put(skb, 2));

	if (buf)
		skb_put_data(skb, buf, len);

	__net_timestamp(skb);

	hdr = skb_push(skb, HCI_MON_HDR_SIZE);
	hdr->opcode = cpu_to_le16(HCI_MON_CTRL_COMMAND);
	hdr->index = cpu_to_le16(index);
	hdr->len = cpu_to_le16(skb->len - HCI_MON_HDR_SIZE);

	return skb;
}

static void __printf(2, 3)
send_monitor_note(struct sock *sk, const char *fmt, ...)
{
	size_t len;
	struct hci_mon_hdr *hdr;
	struct sk_buff *skb;
	va_list args;

	va_start(args, fmt);
	len = vsnprintf(NULL, 0, fmt, args);
	va_end(args);

	skb = bt_skb_alloc(len + 1, GFP_ATOMIC);
	if (!skb)
		return;

	va_start(args, fmt);
	vsprintf(skb_put(skb, len), fmt, args);
	*(u8 *)skb_put(skb, 1) = 0;
	va_end(args);

	__net_timestamp(skb);

	hdr = (void *)skb_push(skb, HCI_MON_HDR_SIZE);
	hdr->opcode = cpu_to_le16(HCI_MON_SYSTEM_NOTE);
	hdr->index = cpu_to_le16(HCI_DEV_NONE);
	hdr->len = cpu_to_le16(skb->len - HCI_MON_HDR_SIZE);

	if (sock_queue_rcv_skb(sk, skb))
		kfree_skb(skb);
}

static void send_monitor_replay(struct sock *sk)
{
	struct hci_dev *hdev;

	read_lock(&hci_dev_list_lock);

	list_for_each_entry(hdev, &hci_dev_list, list) {
		struct sk_buff *skb;

		skb = create_monitor_event(hdev, HCI_DEV_REG);
		if (!skb)
			continue;

		if (sock_queue_rcv_skb(sk, skb))
			kfree_skb(skb);

		if (!test_bit(HCI_RUNNING, &hdev->flags))
			continue;

		skb = create_monitor_event(hdev, HCI_DEV_OPEN);
		if (!skb)
			continue;

		if (sock_queue_rcv_skb(sk, skb))
			kfree_skb(skb);

		if (test_bit(HCI_UP, &hdev->flags))
			skb = create_monitor_event(hdev, HCI_DEV_UP);
		else if (hci_dev_test_flag(hdev, HCI_SETUP))
			skb = create_monitor_event(hdev, HCI_DEV_SETUP);
		else
			skb = NULL;

		if (skb) {
			if (sock_queue_rcv_skb(sk, skb))
				kfree_skb(skb);
		}
	}

	read_unlock(&hci_dev_list_lock);
}

static void send_monitor_control_replay(struct sock *mon_sk)
{
	struct sock *sk;

	read_lock(&hci_sk_list.lock);

	sk_for_each(sk, &hci_sk_list.head) {
		struct sk_buff *skb;

		skb = create_monitor_ctrl_open(sk);
		if (!skb)
			continue;

		if (sock_queue_rcv_skb(mon_sk, skb))
			kfree_skb(skb);
	}

	read_unlock(&hci_sk_list.lock);
}

/* Generate internal stack event */
static void hci_si_event(struct hci_dev *hdev, int type, int dlen, void *data)
{
	struct hci_event_hdr *hdr;
	struct hci_ev_stack_internal *ev;
	struct sk_buff *skb;

	skb = bt_skb_alloc(HCI_EVENT_HDR_SIZE + sizeof(*ev) + dlen, GFP_ATOMIC);
	if (!skb)
		return;

	hdr = skb_put(skb, HCI_EVENT_HDR_SIZE);
	hdr->evt  = HCI_EV_STACK_INTERNAL;
	hdr->plen = sizeof(*ev) + dlen;

	ev = skb_put(skb, sizeof(*ev) + dlen);
	ev->type = type;
	memcpy(ev->data, data, dlen);

	bt_cb(skb)->incoming = 1;
	__net_timestamp(skb);

	hci_skb_pkt_type(skb) = HCI_EVENT_PKT;
	hci_send_to_sock(hdev, skb);
	kfree_skb(skb);
}

void hci_sock_dev_event(struct hci_dev *hdev, int event)
{
	BT_DBG("hdev %s event %d", hdev->name, event);

	if (atomic_read(&monitor_promisc)) {
		struct sk_buff *skb;

		/* Send event to monitor */
		skb = create_monitor_event(hdev, event);
		if (skb) {
			hci_send_to_channel(HCI_CHANNEL_MONITOR, skb,
					    HCI_SOCK_TRUSTED, NULL);
			kfree_skb(skb);
		}
	}

	if (event <= HCI_DEV_DOWN) {
		struct hci_ev_si_device ev;

		/* Send event to sockets */
		ev.event  = event;
		ev.dev_id = hdev->id;
		hci_si_event(NULL, HCI_EV_SI_DEVICE, sizeof(ev), &ev);
	}

	if (event == HCI_DEV_UNREG) {
		struct sock *sk;

		/* Wake up sockets using this dead device */
		read_lock(&hci_sk_list.lock);
		sk_for_each(sk, &hci_sk_list.head) {
			if (hci_pi(sk)->hdev == hdev) {
				sk->sk_err = EPIPE;
				sk->sk_state_change(sk);
			}
		}
		read_unlock(&hci_sk_list.lock);
	}
}

static struct hci_mgmt_chan *__hci_mgmt_chan_find(unsigned short channel)
{
	struct hci_mgmt_chan *c;

	list_for_each_entry(c, &mgmt_chan_list, list) {
		if (c->channel == channel)
			return c;
	}

	return NULL;
}

static struct hci_mgmt_chan *hci_mgmt_chan_find(unsigned short channel)
{
	struct hci_mgmt_chan *c;

	mutex_lock(&mgmt_chan_list_lock);
	c = __hci_mgmt_chan_find(channel);
	mutex_unlock(&mgmt_chan_list_lock);

	return c;
}

int hci_mgmt_chan_register(struct hci_mgmt_chan *c)
{
	if (c->channel < HCI_CHANNEL_CONTROL)
		return -EINVAL;

	mutex_lock(&mgmt_chan_list_lock);
	if (__hci_mgmt_chan_find(c->channel)) {
		mutex_unlock(&mgmt_chan_list_lock);
		return -EALREADY;
	}

	list_add_tail(&c->list, &mgmt_chan_list);

	mutex_unlock(&mgmt_chan_list_lock);

	return 0;
}
EXPORT_SYMBOL(hci_mgmt_chan_register);

void hci_mgmt_chan_unregister(struct hci_mgmt_chan *c)
{
	mutex_lock(&mgmt_chan_list_lock);
	list_del(&c->list);
	mutex_unlock(&mgmt_chan_list_lock);
}
EXPORT_SYMBOL(hci_mgmt_chan_unregister);

static int hci_sock_release(struct socket *sock)
{
	struct sock *sk = sock->sk;
	struct hci_dev *hdev;
	struct sk_buff *skb;

	BT_DBG("sock %p sk %p", sock, sk);

	if (!sk)
		return 0;

	lock_sock(sk);

	switch (hci_pi(sk)->channel) {
	case HCI_CHANNEL_MONITOR:
		atomic_dec(&monitor_promisc);
		break;
	case HCI_CHANNEL_RAW:
	case HCI_CHANNEL_USER:
	case HCI_CHANNEL_CONTROL:
		/* Send event to monitor */
		skb = create_monitor_ctrl_close(sk);
		if (skb) {
			hci_send_to_channel(HCI_CHANNEL_MONITOR, skb,
					    HCI_SOCK_TRUSTED, NULL);
			kfree_skb(skb);
		}

		hci_sock_free_cookie(sk);
		break;
	}

	bt_sock_unlink(&hci_sk_list, sk);

	hdev = hci_pi(sk)->hdev;
	if (hdev) {
		if (hci_pi(sk)->channel == HCI_CHANNEL_USER &&
		    !hci_dev_test_flag(hdev, HCI_UNREGISTER)) {
			/* When releasing a user channel exclusive access,
			 * call hci_dev_do_close directly instead of calling
			 * hci_dev_close to ensure the exclusive access will
			 * be released and the controller brought back down.
			 *
			 * The checking of HCI_AUTO_OFF is not needed in this
			 * case since it will have been cleared already when
			 * opening the user channel.
			 *
			 * Make sure to also check that we haven't already
			 * unregistered since all the cleanup will have already
			 * been complete and hdev will get released when we put
			 * below.
			 */
			hci_dev_do_close(hdev);
			hci_dev_clear_flag(hdev, HCI_USER_CHANNEL);
			mgmt_index_added(hdev);
		}

		atomic_dec(&hdev->promisc);
		hci_dev_put(hdev);
	}

	sock_orphan(sk);
	release_sock(sk);
	sock_put(sk);
	return 0;
}

static int hci_sock_reject_list_add(struct hci_dev *hdev, void __user *arg)
{
	bdaddr_t bdaddr;
	int err;

	if (copy_from_user(&bdaddr, arg, sizeof(bdaddr)))
		return -EFAULT;

	hci_dev_lock(hdev);

	err = hci_bdaddr_list_add(&hdev->reject_list, &bdaddr, BDADDR_BREDR);

	hci_dev_unlock(hdev);

	return err;
}

static int hci_sock_reject_list_del(struct hci_dev *hdev, void __user *arg)
{
	bdaddr_t bdaddr;
	int err;

	if (copy_from_user(&bdaddr, arg, sizeof(bdaddr)))
		return -EFAULT;

	hci_dev_lock(hdev);

	err = hci_bdaddr_list_del(&hdev->reject_list, &bdaddr, BDADDR_BREDR);

	hci_dev_unlock(hdev);

	return err;
}

/* Ioctls that require bound socket */
static int hci_sock_bound_ioctl(struct sock *sk, unsigned int cmd,
				unsigned long arg)
{
	struct hci_dev *hdev = hci_hdev_from_sock(sk);

	if (IS_ERR(hdev))
		return PTR_ERR(hdev);

	if (hci_dev_test_flag(hdev, HCI_USER_CHANNEL))
		return -EBUSY;

	if (hci_dev_test_flag(hdev, HCI_UNCONFIGURED))
		return -EOPNOTSUPP;

	if (hdev->dev_type != HCI_PRIMARY)
		return -EOPNOTSUPP;

	switch (cmd) {
	case HCISETRAW:
		if (!capable(CAP_NET_ADMIN))
			return -EPERM;
		return -EOPNOTSUPP;

	case HCIGETCONNINFO:
		return hci_get_conn_info(hdev, (void __user *)arg);

	case HCIGETAUTHINFO:
		return hci_get_auth_info(hdev, (void __user *)arg);

	case HCIBLOCKADDR:
		if (!capable(CAP_NET_ADMIN))
			return -EPERM;
		return hci_sock_reject_list_add(hdev, (void __user *)arg);

	case HCIUNBLOCKADDR:
		if (!capable(CAP_NET_ADMIN))
			return -EPERM;
		return hci_sock_reject_list_del(hdev, (void __user *)arg);
	}

	return -ENOIOCTLCMD;
}

static int hci_sock_ioctl(struct socket *sock, unsigned int cmd,
			  unsigned long arg)
{
	void __user *argp = (void __user *)arg;
	struct sock *sk = sock->sk;
	int err;

	BT_DBG("cmd %x arg %lx", cmd, arg);

	lock_sock(sk);

	if (hci_pi(sk)->channel != HCI_CHANNEL_RAW) {
		err = -EBADFD;
		goto done;
	}

	/* When calling an ioctl on an unbound raw socket, then ensure
	 * that the monitor gets informed. Ensure that the resulting event
	 * is only send once by checking if the cookie exists or not. The
	 * socket cookie will be only ever generated once for the lifetime
	 * of a given socket.
	 */
	if (hci_sock_gen_cookie(sk)) {
		struct sk_buff *skb;

		/* Perform careful checks before setting the HCI_SOCK_TRUSTED
		 * flag. Make sure that not only the current task but also
		 * the socket opener has the required capability, since
		 * privileged programs can be tricked into making ioctl calls
		 * on HCI sockets, and the socket should not be marked as
		 * trusted simply because the ioctl caller is privileged.
		 */
		if (sk_capable(sk, CAP_NET_ADMIN))
			hci_sock_set_flag(sk, HCI_SOCK_TRUSTED);

		/* Send event to monitor */
		skb = create_monitor_ctrl_open(sk);
		if (skb) {
			hci_send_to_channel(HCI_CHANNEL_MONITOR, skb,
					    HCI_SOCK_TRUSTED, NULL);
			kfree_skb(skb);
		}
	}

	release_sock(sk);

	switch (cmd) {
	case HCIGETDEVLIST:
		return hci_get_dev_list(argp);

	case HCIGETDEVINFO:
		return hci_get_dev_info(argp);

	case HCIGETCONNLIST:
		return hci_get_conn_list(argp);

	case HCIDEVUP:
		if (!capable(CAP_NET_ADMIN))
			return -EPERM;
		return hci_dev_open(arg);

	case HCIDEVDOWN:
		if (!capable(CAP_NET_ADMIN))
			return -EPERM;
		return hci_dev_close(arg);

	case HCIDEVRESET:
		if (!capable(CAP_NET_ADMIN))
			return -EPERM;
		return hci_dev_reset(arg);

	case HCIDEVRESTAT:
		if (!capable(CAP_NET_ADMIN))
			return -EPERM;
		return hci_dev_reset_stat(arg);

	case HCISETSCAN:
	case HCISETAUTH:
	case HCISETENCRYPT:
	case HCISETPTYPE:
	case HCISETLINKPOL:
	case HCISETLINKMODE:
	case HCISETACLMTU:
	case HCISETSCOMTU:
		if (!capable(CAP_NET_ADMIN))
			return -EPERM;
		return hci_dev_cmd(cmd, argp);

	case HCIINQUIRY:
		return hci_inquiry(argp);
	}

	lock_sock(sk);

	err = hci_sock_bound_ioctl(sk, cmd, arg);

done:
	release_sock(sk);
	return err;
}

#ifdef CONFIG_COMPAT
static int hci_sock_compat_ioctl(struct socket *sock, unsigned int cmd,
				 unsigned long arg)
{
	switch (cmd) {
	case HCIDEVUP:
	case HCIDEVDOWN:
	case HCIDEVRESET:
	case HCIDEVRESTAT:
		return hci_sock_ioctl(sock, cmd, arg);
	}

	return hci_sock_ioctl(sock, cmd, (unsigned long)compat_ptr(arg));
}
#endif

static int hci_sock_bind(struct socket *sock, struct sockaddr *addr,
			 int addr_len)
{
	struct sockaddr_hci haddr;
	struct sock *sk = sock->sk;
	struct hci_dev *hdev = NULL;
	struct sk_buff *skb;
	int len, err = 0;

	BT_DBG("sock %p sk %p", sock, sk);

	if (!addr)
		return -EINVAL;

	memset(&haddr, 0, sizeof(haddr));
	len = min_t(unsigned int, sizeof(haddr), addr_len);
	memcpy(&haddr, addr, len);

	if (haddr.hci_family != AF_BLUETOOTH)
		return -EINVAL;

	lock_sock(sk);

	/* Allow detaching from dead device and attaching to alive device, if
	 * the caller wants to re-bind (instead of close) this socket in
	 * response to hci_sock_dev_event(HCI_DEV_UNREG) notification.
	 */
	hdev = hci_pi(sk)->hdev;
	if (hdev && hci_dev_test_flag(hdev, HCI_UNREGISTER)) {
		hci_pi(sk)->hdev = NULL;
		sk->sk_state = BT_OPEN;
		hci_dev_put(hdev);
	}
	hdev = NULL;

	if (sk->sk_state == BT_BOUND) {
		err = -EALREADY;
		goto done;
	}

	switch (haddr.hci_channel) {
	case HCI_CHANNEL_RAW:
		if (hci_pi(sk)->hdev) {
			err = -EALREADY;
			goto done;
		}

		if (haddr.hci_dev != HCI_DEV_NONE) {
			hdev = hci_dev_get(haddr.hci_dev);
			if (!hdev) {
				err = -ENODEV;
				goto done;
			}

			atomic_inc(&hdev->promisc);
		}

		hci_pi(sk)->channel = haddr.hci_channel;

		if (!hci_sock_gen_cookie(sk)) {
			/* In the case when a cookie has already been assigned,
			 * then there has been already an ioctl issued against
			 * an unbound socket and with that triggered an open
			 * notification. Send a close notification first to
			 * allow the state transition to bounded.
			 */
			skb = create_monitor_ctrl_close(sk);
			if (skb) {
				hci_send_to_channel(HCI_CHANNEL_MONITOR, skb,
						    HCI_SOCK_TRUSTED, NULL);
				kfree_skb(skb);
			}
		}

		if (capable(CAP_NET_ADMIN))
			hci_sock_set_flag(sk, HCI_SOCK_TRUSTED);

		hci_pi(sk)->hdev = hdev;

		/* Send event to monitor */
		skb = create_monitor_ctrl_open(sk);
		if (skb) {
			hci_send_to_channel(HCI_CHANNEL_MONITOR, skb,
					    HCI_SOCK_TRUSTED, NULL);
			kfree_skb(skb);
		}
		break;

	case HCI_CHANNEL_USER:
		if (hci_pi(sk)->hdev) {
			err = -EALREADY;
			goto done;
		}

		if (haddr.hci_dev == HCI_DEV_NONE) {
			err = -EINVAL;
			goto done;
		}

		if (!capable(CAP_NET_ADMIN)) {
			err = -EPERM;
			goto done;
		}

		hdev = hci_dev_get(haddr.hci_dev);
		if (!hdev) {
			err = -ENODEV;
			goto done;
		}

		if (test_bit(HCI_INIT, &hdev->flags) ||
		    hci_dev_test_flag(hdev, HCI_SETUP) ||
		    hci_dev_test_flag(hdev, HCI_CONFIG) ||
		    (!hci_dev_test_flag(hdev, HCI_AUTO_OFF) &&
		     test_bit(HCI_UP, &hdev->flags))) {
			err = -EBUSY;
			hci_dev_put(hdev);
			goto done;
		}

		if (hci_dev_test_and_set_flag(hdev, HCI_USER_CHANNEL)) {
			err = -EUSERS;
			hci_dev_put(hdev);
			goto done;
		}

		mgmt_index_removed(hdev);

		err = hci_dev_open(hdev->id);
		if (err) {
			if (err == -EALREADY) {
				/* In case the transport is already up and
				 * running, clear the error here.
				 *
				 * This can happen when opening a user
				 * channel and HCI_AUTO_OFF grace period
				 * is still active.
				 */
				err = 0;
			} else {
				hci_dev_clear_flag(hdev, HCI_USER_CHANNEL);
				mgmt_index_added(hdev);
				hci_dev_put(hdev);
				goto done;
			}
		}

		hci_pi(sk)->channel = haddr.hci_channel;

		if (!hci_sock_gen_cookie(sk)) {
			/* In the case when a cookie has already been assigned,
			 * this socket will transition from a raw socket into
			 * a user channel socket. For a clean transition, send
			 * the close notification first.
			 */
			skb = create_monitor_ctrl_close(sk);
			if (skb) {
				hci_send_to_channel(HCI_CHANNEL_MONITOR, skb,
						    HCI_SOCK_TRUSTED, NULL);
				kfree_skb(skb);
			}
		}

		/* The user channel is restricted to CAP_NET_ADMIN
		 * capabilities and with that implicitly trusted.
		 */
		hci_sock_set_flag(sk, HCI_SOCK_TRUSTED);

		hci_pi(sk)->hdev = hdev;

		/* Send event to monitor */
		skb = create_monitor_ctrl_open(sk);
		if (skb) {
			hci_send_to_channel(HCI_CHANNEL_MONITOR, skb,
					    HCI_SOCK_TRUSTED, NULL);
			kfree_skb(skb);
		}

		atomic_inc(&hdev->promisc);
		break;

	case HCI_CHANNEL_MONITOR:
		if (haddr.hci_dev != HCI_DEV_NONE) {
			err = -EINVAL;
			goto done;
		}

		if (!capable(CAP_NET_RAW)) {
			err = -EPERM;
			goto done;
		}

		hci_pi(sk)->channel = haddr.hci_channel;

		/* The monitor interface is restricted to CAP_NET_RAW
		 * capabilities and with that implicitly trusted.
		 */
		hci_sock_set_flag(sk, HCI_SOCK_TRUSTED);

		send_monitor_note(sk, "Linux version %s (%s)",
				  init_utsname()->release,
				  init_utsname()->machine);
		send_monitor_note(sk, "Bluetooth subsystem version %u.%u",
				  BT_SUBSYS_VERSION, BT_SUBSYS_REVISION);
		send_monitor_replay(sk);
		send_monitor_control_replay(sk);

		atomic_inc(&monitor_promisc);
		break;

	case HCI_CHANNEL_LOGGING:
		if (haddr.hci_dev != HCI_DEV_NONE) {
			err = -EINVAL;
			goto done;
		}

		if (!capable(CAP_NET_ADMIN)) {
			err = -EPERM;
			goto done;
		}

		hci_pi(sk)->channel = haddr.hci_channel;
		break;

	default:
		if (!hci_mgmt_chan_find(haddr.hci_channel)) {
			err = -EINVAL;
			goto done;
		}

		if (haddr.hci_dev != HCI_DEV_NONE) {
			err = -EINVAL;
			goto done;
		}

		/* Users with CAP_NET_ADMIN capabilities are allowed
		 * access to all management commands and events. For
		 * untrusted users the interface is restricted and
		 * also only untrusted events are sent.
		 */
		if (capable(CAP_NET_ADMIN))
			hci_sock_set_flag(sk, HCI_SOCK_TRUSTED);

		hci_pi(sk)->channel = haddr.hci_channel;

		/* At the moment the index and unconfigured index events
		 * are enabled unconditionally. Setting them on each
		 * socket when binding keeps this functionality. They
		 * however might be cleared later and then sending of these
		 * events will be disabled, but that is then intentional.
		 *
		 * This also enables generic events that are safe to be
		 * received by untrusted users. Example for such events
		 * are changes to settings, class of device, name etc.
		 */
		if (hci_pi(sk)->channel == HCI_CHANNEL_CONTROL) {
			if (!hci_sock_gen_cookie(sk)) {
				/* In the case when a cookie has already been
				 * assigned, this socket will transition from
				 * a raw socket into a control socket. To
				 * allow for a clean transition, send the
				 * close notification first.
				 */
				skb = create_monitor_ctrl_close(sk);
				if (skb) {
					hci_send_to_channel(HCI_CHANNEL_MONITOR, skb,
							    HCI_SOCK_TRUSTED, NULL);
					kfree_skb(skb);
				}
			}

			/* Send event to monitor */
			skb = create_monitor_ctrl_open(sk);
			if (skb) {
				hci_send_to_channel(HCI_CHANNEL_MONITOR, skb,
						    HCI_SOCK_TRUSTED, NULL);
				kfree_skb(skb);
			}

			hci_sock_set_flag(sk, HCI_MGMT_INDEX_EVENTS);
			hci_sock_set_flag(sk, HCI_MGMT_UNCONF_INDEX_EVENTS);
			hci_sock_set_flag(sk, HCI_MGMT_OPTION_EVENTS);
			hci_sock_set_flag(sk, HCI_MGMT_SETTING_EVENTS);
			hci_sock_set_flag(sk, HCI_MGMT_DEV_CLASS_EVENTS);
			hci_sock_set_flag(sk, HCI_MGMT_LOCAL_NAME_EVENTS);
		}
		break;
	}

	/* Default MTU to HCI_MAX_FRAME_SIZE if not set */
	if (!hci_pi(sk)->mtu)
		hci_pi(sk)->mtu = HCI_MAX_FRAME_SIZE;

	sk->sk_state = BT_BOUND;

done:
	release_sock(sk);
	return err;
}

static int hci_sock_getname(struct socket *sock, struct sockaddr *addr,
			    int peer)
{
	struct sockaddr_hci *haddr = (struct sockaddr_hci *)addr;
	struct sock *sk = sock->sk;
	struct hci_dev *hdev;
	int err = 0;

	BT_DBG("sock %p sk %p", sock, sk);

	if (peer)
		return -EOPNOTSUPP;

	lock_sock(sk);

	hdev = hci_hdev_from_sock(sk);
	if (IS_ERR(hdev)) {
		err = PTR_ERR(hdev);
		goto done;
	}

	haddr->hci_family = AF_BLUETOOTH;
	haddr->hci_dev    = hdev->id;
	haddr->hci_channel= hci_pi(sk)->channel;
	err = sizeof(*haddr);

done:
	release_sock(sk);
	return err;
}

static void hci_sock_cmsg(struct sock *sk, struct msghdr *msg,
			  struct sk_buff *skb)
{
	__u8 mask = hci_pi(sk)->cmsg_mask;

	if (mask & HCI_CMSG_DIR) {
		int incoming = bt_cb(skb)->incoming;
		put_cmsg(msg, SOL_HCI, HCI_CMSG_DIR, sizeof(incoming),
			 &incoming);
	}

	if (mask & HCI_CMSG_TSTAMP) {
#ifdef CONFIG_COMPAT
		struct old_timeval32 ctv;
#endif
		struct __kernel_old_timeval tv;
		void *data;
		int len;

		skb_get_timestamp(skb, &tv);

		data = &tv;
		len = sizeof(tv);
#ifdef CONFIG_COMPAT
		if (!COMPAT_USE_64BIT_TIME &&
		    (msg->msg_flags & MSG_CMSG_COMPAT)) {
			ctv.tv_sec = tv.tv_sec;
			ctv.tv_usec = tv.tv_usec;
			data = &ctv;
			len = sizeof(ctv);
		}
#endif

		put_cmsg(msg, SOL_HCI, HCI_CMSG_TSTAMP, len, data);
	}
}

static int hci_sock_recvmsg(struct socket *sock, struct msghdr *msg,
			    size_t len, int flags)
{
	struct sock *sk = sock->sk;
	struct sk_buff *skb;
	int copied, err;
	unsigned int skblen;

	BT_DBG("sock %p, sk %p", sock, sk);

	if (flags & MSG_OOB)
		return -EOPNOTSUPP;

	if (hci_pi(sk)->channel == HCI_CHANNEL_LOGGING)
		return -EOPNOTSUPP;

	if (sk->sk_state == BT_CLOSED)
		return 0;

	skb = skb_recv_datagram(sk, flags, &err);
	if (!skb)
		return err;

	skblen = skb->len;
	copied = skb->len;
	if (len < copied) {
		msg->msg_flags |= MSG_TRUNC;
		copied = len;
	}

	skb_reset_transport_header(skb);
	err = skb_copy_datagram_msg(skb, 0, msg, copied);

	switch (hci_pi(sk)->channel) {
	case HCI_CHANNEL_RAW:
		hci_sock_cmsg(sk, msg, skb);
		break;
	case HCI_CHANNEL_USER:
	case HCI_CHANNEL_MONITOR:
		sock_recv_timestamp(msg, sk, skb);
		break;
	default:
		if (hci_mgmt_chan_find(hci_pi(sk)->channel))
			sock_recv_timestamp(msg, sk, skb);
		break;
	}

	skb_free_datagram(sk, skb);

	if (flags & MSG_TRUNC)
		copied = skblen;

	return err ? : copied;
}

static int hci_mgmt_cmd(struct hci_mgmt_chan *chan, struct sock *sk,
			struct sk_buff *skb)
{
	u8 *cp;
	struct mgmt_hdr *hdr;
	u16 opcode, index, len;
	struct hci_dev *hdev = NULL;
	const struct hci_mgmt_handler *handler;
	bool var_len, no_hdev;
	int err;

	BT_DBG("got %d bytes", skb->len);

	if (skb->len < sizeof(*hdr))
		return -EINVAL;

	hdr = (void *)skb->data;
	opcode = __le16_to_cpu(hdr->opcode);
	index = __le16_to_cpu(hdr->index);
	len = __le16_to_cpu(hdr->len);

	if (len != skb->len - sizeof(*hdr)) {
		err = -EINVAL;
		goto done;
	}

	if (chan->channel == HCI_CHANNEL_CONTROL) {
		struct sk_buff *cmd;

		/* Send event to monitor */
		cmd = create_monitor_ctrl_command(sk, index, opcode, len,
						  skb->data + sizeof(*hdr));
		if (cmd) {
			hci_send_to_channel(HCI_CHANNEL_MONITOR, cmd,
					    HCI_SOCK_TRUSTED, NULL);
			kfree_skb(cmd);
		}
	}

	if (opcode >= chan->handler_count ||
	    chan->handlers[opcode].func == NULL) {
		BT_DBG("Unknown op %u", opcode);
		err = mgmt_cmd_status(sk, index, opcode,
				      MGMT_STATUS_UNKNOWN_COMMAND);
		goto done;
	}

	handler = &chan->handlers[opcode];

	if (!hci_sock_test_flag(sk, HCI_SOCK_TRUSTED) &&
	    !(handler->flags & HCI_MGMT_UNTRUSTED)) {
		err = mgmt_cmd_status(sk, index, opcode,
				      MGMT_STATUS_PERMISSION_DENIED);
		goto done;
	}

	if (index != MGMT_INDEX_NONE) {
		hdev = hci_dev_get(index);
		if (!hdev) {
			err = mgmt_cmd_status(sk, index, opcode,
					      MGMT_STATUS_INVALID_INDEX);
			goto done;
		}

		if (hci_dev_test_flag(hdev, HCI_SETUP) ||
		    hci_dev_test_flag(hdev, HCI_CONFIG) ||
		    hci_dev_test_flag(hdev, HCI_USER_CHANNEL)) {
			err = mgmt_cmd_status(sk, index, opcode,
					      MGMT_STATUS_INVALID_INDEX);
			goto done;
		}

		if (hci_dev_test_flag(hdev, HCI_UNCONFIGURED) &&
		    !(handler->flags & HCI_MGMT_UNCONFIGURED)) {
			err = mgmt_cmd_status(sk, index, opcode,
					      MGMT_STATUS_INVALID_INDEX);
			goto done;
		}
	}

	if (!(handler->flags & HCI_MGMT_HDEV_OPTIONAL)) {
		no_hdev = (handler->flags & HCI_MGMT_NO_HDEV);
		if (no_hdev != !hdev) {
			err = mgmt_cmd_status(sk, index, opcode,
					      MGMT_STATUS_INVALID_INDEX);
			goto done;
		}
	}

	var_len = (handler->flags & HCI_MGMT_VAR_LEN);
	if ((var_len && len < handler->data_len) ||
	    (!var_len && len != handler->data_len)) {
		err = mgmt_cmd_status(sk, index, opcode,
				      MGMT_STATUS_INVALID_PARAMS);
		goto done;
	}

	if (hdev && chan->hdev_init)
		chan->hdev_init(sk, hdev);

	cp = skb->data + sizeof(*hdr);

	err = handler->func(sk, hdev, cp, len);
	if (err < 0)
		goto done;

	err = skb->len;

done:
	if (hdev)
		hci_dev_put(hdev);

	return err;
}

static int hci_logging_frame(struct sock *sk, struct sk_buff *skb,
			     unsigned int flags)
{
	struct hci_mon_hdr *hdr;
	struct hci_dev *hdev;
	u16 index;
	int err;

	/* The logging frame consists at minimum of the standard header,
	 * the priority byte, the ident length byte and at least one string
	 * terminator NUL byte. Anything shorter are invalid packets.
	 */
	if (skb->len < sizeof(*hdr) + 3)
		return -EINVAL;

	hdr = (void *)skb->data;

	if (__le16_to_cpu(hdr->len) != skb->len - sizeof(*hdr))
		return -EINVAL;

	if (__le16_to_cpu(hdr->opcode) == 0x0000) {
		__u8 priority = skb->data[sizeof(*hdr)];
		__u8 ident_len = skb->data[sizeof(*hdr) + 1];

		/* Only the priorities 0-7 are valid and with that any other
		 * value results in an invalid packet.
		 *
		 * The priority byte is followed by an ident length byte and
		 * the NUL terminated ident string. Check that the ident
		 * length is not overflowing the packet and also that the
		 * ident string itself is NUL terminated. In case the ident
		 * length is zero, the length value actually doubles as NUL
		 * terminator identifier.
		 *
		 * The message follows the ident string (if present) and
		 * must be NUL terminated. Otherwise it is not a valid packet.
		 */
		if (priority > 7 || skb->data[skb->len - 1] != 0x00 ||
		    ident_len > skb->len - sizeof(*hdr) - 3 ||
		    skb->data[sizeof(*hdr) + ident_len + 1] != 0x00)
			return -EINVAL;
	} else {
		return -EINVAL;
	}

	index = __le16_to_cpu(hdr->index);

	if (index != MGMT_INDEX_NONE) {
		hdev = hci_dev_get(index);
		if (!hdev)
			return -ENODEV;
	} else {
		hdev = NULL;
	}

	hdr->opcode = cpu_to_le16(HCI_MON_USER_LOGGING);

	hci_send_to_channel(HCI_CHANNEL_MONITOR, skb, HCI_SOCK_TRUSTED, NULL);
	err = skb->len;

	if (hdev)
		hci_dev_put(hdev);

	return err;
}

static int hci_sock_sendmsg(struct socket *sock, struct msghdr *msg,
			    size_t len)
{
	struct sock *sk = sock->sk;
	struct hci_mgmt_chan *chan;
	struct hci_dev *hdev;
	struct sk_buff *skb;
	int err;
	const unsigned int flags = msg->msg_flags;

	BT_DBG("sock %p sk %p", sock, sk);

	if (flags & MSG_OOB)
		return -EOPNOTSUPP;

	if (flags & ~(MSG_DONTWAIT | MSG_NOSIGNAL | MSG_ERRQUEUE | MSG_CMSG_COMPAT))
		return -EINVAL;

	if (len < 4 || len > hci_pi(sk)->mtu)
		return -EINVAL;

	skb = bt_skb_sendmsg(sk, msg, len, len, 0, 0);
	if (IS_ERR(skb))
		return PTR_ERR(skb);

	lock_sock(sk);

	switch (hci_pi(sk)->channel) {
	case HCI_CHANNEL_RAW:
	case HCI_CHANNEL_USER:
		break;
	case HCI_CHANNEL_MONITOR:
		err = -EOPNOTSUPP;
		goto drop;
	case HCI_CHANNEL_LOGGING:
		err = hci_logging_frame(sk, skb, flags);
		goto drop;
	default:
		mutex_lock(&mgmt_chan_list_lock);
		chan = __hci_mgmt_chan_find(hci_pi(sk)->channel);
		if (chan)
			err = hci_mgmt_cmd(chan, sk, skb);
		else
			err = -EINVAL;

		mutex_unlock(&mgmt_chan_list_lock);
		goto drop;
	}

	hdev = hci_hdev_from_sock(sk);
	if (IS_ERR(hdev)) {
		err = PTR_ERR(hdev);
		goto drop;
	}

	if (!test_bit(HCI_UP, &hdev->flags)) {
		err = -ENETDOWN;
		goto drop;
	}

	hci_skb_pkt_type(skb) = skb->data[0];
	skb_pull(skb, 1);

	if (hci_pi(sk)->channel == HCI_CHANNEL_USER) {
		/* No permission check is needed for user channel
		 * since that gets enforced when binding the socket.
		 *
		 * However check that the packet type is valid.
		 */
		if (hci_skb_pkt_type(skb) != HCI_COMMAND_PKT &&
		    hci_skb_pkt_type(skb) != HCI_ACLDATA_PKT &&
		    hci_skb_pkt_type(skb) != HCI_SCODATA_PKT &&
		    hci_skb_pkt_type(skb) != HCI_ISODATA_PKT) {
			err = -EINVAL;
			goto drop;
		}

		skb_queue_tail(&hdev->raw_q, skb);
		queue_work(hdev->workqueue, &hdev->tx_work);
	} else if (hci_skb_pkt_type(skb) == HCI_COMMAND_PKT) {
		u16 opcode = get_unaligned_le16(skb->data);
		u16 ogf = hci_opcode_ogf(opcode);
		u16 ocf = hci_opcode_ocf(opcode);

		if (((ogf > HCI_SFLT_MAX_OGF) ||
		     !hci_test_bit(ocf & HCI_FLT_OCF_BITS,
				   &hci_sec_filter.ocf_mask[ogf])) &&
		    !capable(CAP_NET_RAW)) {
			err = -EPERM;
			goto drop;
		}

		/* Since the opcode has already been extracted here, store
		 * a copy of the value for later use by the drivers.
		 */
		hci_skb_opcode(skb) = opcode;

		if (ogf == 0x3f) {
			skb_queue_tail(&hdev->raw_q, skb);
			queue_work(hdev->workqueue, &hdev->tx_work);
		} else {
			/* Stand-alone HCI commands must be flagged as
			 * single-command requests.
			 */
			bt_cb(skb)->hci.req_flags |= HCI_REQ_START;

			skb_queue_tail(&hdev->cmd_q, skb);
			queue_work(hdev->workqueue, &hdev->cmd_work);
		}
	} else {
		if (!capable(CAP_NET_RAW)) {
			err = -EPERM;
			goto drop;
		}

		if (hci_skb_pkt_type(skb) != HCI_ACLDATA_PKT &&
		    hci_skb_pkt_type(skb) != HCI_SCODATA_PKT &&
		    hci_skb_pkt_type(skb) != HCI_ISODATA_PKT) {
			err = -EINVAL;
			goto drop;
		}

		skb_queue_tail(&hdev->raw_q, skb);
		queue_work(hdev->workqueue, &hdev->tx_work);
	}

	err = len;

done:
	release_sock(sk);
	return err;

drop:
	kfree_skb(skb);
	goto done;
}

static int hci_sock_setsockopt_old(struct socket *sock, int level, int optname,
				   sockptr_t optval, unsigned int len)
{
	struct hci_ufilter uf = { .opcode = 0 };
	struct sock *sk = sock->sk;
	int err = 0, opt = 0;

	BT_DBG("sk %p, opt %d", sk, optname);

	lock_sock(sk);

	if (hci_pi(sk)->channel != HCI_CHANNEL_RAW) {
		err = -EBADFD;
		goto done;
	}

	switch (optname) {
	case HCI_DATA_DIR:
		if (copy_from_sockptr(&opt, optval, sizeof(opt))) {
			err = -EFAULT;
			break;
		}

		if (opt)
			hci_pi(sk)->cmsg_mask |= HCI_CMSG_DIR;
		else
			hci_pi(sk)->cmsg_mask &= ~HCI_CMSG_DIR;
		break;

	case HCI_TIME_STAMP:
		if (copy_from_sockptr(&opt, optval, sizeof(opt))) {
			err = -EFAULT;
			break;
		}

		if (opt)
			hci_pi(sk)->cmsg_mask |= HCI_CMSG_TSTAMP;
		else
			hci_pi(sk)->cmsg_mask &= ~HCI_CMSG_TSTAMP;
		break;

	case HCI_FILTER:
		{
			struct hci_filter *f = &hci_pi(sk)->filter;

			uf.type_mask = f->type_mask;
			uf.opcode    = f->opcode;
			uf.event_mask[0] = *((u32 *) f->event_mask + 0);
			uf.event_mask[1] = *((u32 *) f->event_mask + 1);
		}

		len = min_t(unsigned int, len, sizeof(uf));
		if (copy_from_sockptr(&uf, optval, len)) {
			err = -EFAULT;
			break;
		}

		if (!capable(CAP_NET_RAW)) {
			uf.type_mask &= hci_sec_filter.type_mask;
			uf.event_mask[0] &= *((u32 *) hci_sec_filter.event_mask + 0);
			uf.event_mask[1] &= *((u32 *) hci_sec_filter.event_mask + 1);
		}

		{
			struct hci_filter *f = &hci_pi(sk)->filter;

			f->type_mask = uf.type_mask;
			f->opcode    = uf.opcode;
			*((u32 *) f->event_mask + 0) = uf.event_mask[0];
			*((u32 *) f->event_mask + 1) = uf.event_mask[1];
		}
		break;

	default:
		err = -ENOPROTOOPT;
		break;
	}

done:
	release_sock(sk);
	return err;
}

static int hci_sock_setsockopt(struct socket *sock, int level, int optname,
			       sockptr_t optval, unsigned int len)
{
	struct sock *sk = sock->sk;
	int err = 0;
	u16 opt;

	BT_DBG("sk %p, opt %d", sk, optname);

	if (level == SOL_HCI)
		return hci_sock_setsockopt_old(sock, level, optname, optval,
					       len);

	if (level != SOL_BLUETOOTH)
		return -ENOPROTOOPT;

	lock_sock(sk);

	switch (optname) {
	case BT_SNDMTU:
	case BT_RCVMTU:
		switch (hci_pi(sk)->channel) {
		/* Don't allow changing MTU for channels that are meant for HCI
		 * traffic only.
		 */
		case HCI_CHANNEL_RAW:
		case HCI_CHANNEL_USER:
			err = -ENOPROTOOPT;
			goto done;
		}

		if (copy_from_sockptr(&opt, optval, sizeof(opt))) {
			err = -EFAULT;
			break;
		}

		hci_pi(sk)->mtu = opt;
		break;

	default:
		err = -ENOPROTOOPT;
		break;
	}

done:
	release_sock(sk);
	return err;
}

static int hci_sock_getsockopt_old(struct socket *sock, int level, int optname,
				   char __user *optval, int __user *optlen)
{
	struct hci_ufilter uf;
	struct sock *sk = sock->sk;
	int len, opt, err = 0;

	BT_DBG("sk %p, opt %d", sk, optname);

	if (get_user(len, optlen))
		return -EFAULT;

	lock_sock(sk);

	if (hci_pi(sk)->channel != HCI_CHANNEL_RAW) {
		err = -EBADFD;
		goto done;
	}

	switch (optname) {
	case HCI_DATA_DIR:
		if (hci_pi(sk)->cmsg_mask & HCI_CMSG_DIR)
			opt = 1;
		else
			opt = 0;

		if (put_user(opt, optval))
			err = -EFAULT;
		break;

	case HCI_TIME_STAMP:
		if (hci_pi(sk)->cmsg_mask & HCI_CMSG_TSTAMP)
			opt = 1;
		else
			opt = 0;

		if (put_user(opt, optval))
			err = -EFAULT;
		break;

	case HCI_FILTER:
		{
			struct hci_filter *f = &hci_pi(sk)->filter;

			memset(&uf, 0, sizeof(uf));
			uf.type_mask = f->type_mask;
			uf.opcode    = f->opcode;
			uf.event_mask[0] = *((u32 *) f->event_mask + 0);
			uf.event_mask[1] = *((u32 *) f->event_mask + 1);
		}

		len = min_t(unsigned int, len, sizeof(uf));
		if (copy_to_user(optval, &uf, len))
			err = -EFAULT;
		break;

	default:
		err = -ENOPROTOOPT;
		break;
	}

done:
	release_sock(sk);
	return err;
}

static int hci_sock_getsockopt(struct socket *sock, int level, int optname,
			       char __user *optval, int __user *optlen)
{
	struct sock *sk = sock->sk;
	int err = 0;

	BT_DBG("sk %p, opt %d", sk, optname);

	if (level == SOL_HCI)
		return hci_sock_getsockopt_old(sock, level, optname, optval,
					       optlen);

	if (level != SOL_BLUETOOTH)
		return -ENOPROTOOPT;

	lock_sock(sk);

	switch (optname) {
	case BT_SNDMTU:
	case BT_RCVMTU:
		if (put_user(hci_pi(sk)->mtu, (u16 __user *)optval))
			err = -EFAULT;
		break;

	default:
		err = -ENOPROTOOPT;
		break;
	}

	release_sock(sk);
	return err;
}

static void hci_sock_destruct(struct sock *sk)
{
	mgmt_cleanup(sk);
	skb_queue_purge(&sk->sk_receive_queue);
	skb_queue_purge(&sk->sk_write_queue);
}

static const struct proto_ops hci_sock_ops = {
	.family		= PF_BLUETOOTH,
	.owner		= THIS_MODULE,
	.release	= hci_sock_release,
	.bind		= hci_sock_bind,
	.getname	= hci_sock_getname,
	.sendmsg	= hci_sock_sendmsg,
	.recvmsg	= hci_sock_recvmsg,
	.ioctl		= hci_sock_ioctl,
#ifdef CONFIG_COMPAT
	.compat_ioctl	= hci_sock_compat_ioctl,
#endif
	.poll		= datagram_poll,
	.listen		= sock_no_listen,
	.shutdown	= sock_no_shutdown,
	.setsockopt	= hci_sock_setsockopt,
	.getsockopt	= hci_sock_getsockopt,
	.connect	= sock_no_connect,
	.socketpair	= sock_no_socketpair,
	.accept		= sock_no_accept,
	.mmap		= sock_no_mmap
};

static struct proto hci_sk_proto = {
	.name		= "HCI",
	.owner		= THIS_MODULE,
	.obj_size	= sizeof(struct hci_pinfo)
};

static int hci_sock_create(struct net *net, struct socket *sock, int protocol,
			   int kern)
{
	struct sock *sk;

	BT_DBG("sock %p", sock);

	if (sock->type != SOCK_RAW)
		return -ESOCKTNOSUPPORT;

	sock->ops = &hci_sock_ops;

	sk = sk_alloc(net, PF_BLUETOOTH, GFP_ATOMIC, &hci_sk_proto, kern);
	if (!sk)
		return -ENOMEM;

	sock_init_data(sock, sk);

	sock_reset_flag(sk, SOCK_ZAPPED);

	sk->sk_protocol = protocol;

	sock->state = SS_UNCONNECTED;
	sk->sk_state = BT_OPEN;
	sk->sk_destruct = hci_sock_destruct;

	bt_sock_link(&hci_sk_list, sk);
	return 0;
}

static const struct net_proto_family hci_sock_family_ops = {
	.family	= PF_BLUETOOTH,
	.owner	= THIS_MODULE,
	.create	= hci_sock_create,
};

int __init hci_sock_init(void)
{
	int err;

	BUILD_BUG_ON(sizeof(struct sockaddr_hci) > sizeof(struct sockaddr));

	err = proto_register(&hci_sk_proto, 0);
	if (err < 0)
		return err;

	err = bt_sock_register(BTPROTO_HCI, &hci_sock_family_ops);
	if (err < 0) {
		BT_ERR("HCI socket registration failed");
		goto error;
	}

	err = bt_procfs_init(&init_net, "hci", &hci_sk_list, NULL);
	if (err < 0) {
		BT_ERR("Failed to create HCI proc file");
		bt_sock_unregister(BTPROTO_HCI);
		goto error;
	}

	BT_INFO("HCI socket layer initialized");

	return 0;

error:
	proto_unregister(&hci_sk_proto);
	return err;
}

void hci_sock_cleanup(void)
{
	bt_procfs_cleanup(&init_net, "hci");
	bt_sock_unregister(BTPROTO_HCI);
	proto_unregister(&hci_sk_proto);
}
