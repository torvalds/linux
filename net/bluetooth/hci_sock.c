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

#include <linux/module.h>

#include <linux/types.h>
#include <linux/capability.h>
#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/poll.h>
#include <linux/fcntl.h>
#include <linux/init.h>
#include <linux/skbuff.h>
#include <linux/workqueue.h>
#include <linux/interrupt.h>
#include <linux/compat.h>
#include <linux/socket.h>
#include <linux/ioctl.h>
#include <net/sock.h>

#include <asm/system.h>
#include <linux/uaccess.h>
#include <asm/unaligned.h>

#include <net/bluetooth/bluetooth.h>
#include <net/bluetooth/hci_core.h>

static int enable_mgmt;

/* ----- HCI socket interface ----- */

static inline int hci_test_bit(int nr, void *addr)
{
	return *((__u32 *) addr + (nr >> 5)) & ((__u32) 1 << (nr & 31));
}

/* Security filter */
static struct hci_sec_filter hci_sec_filter = {
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

/* Send frame to RAW socket */
void hci_send_to_sock(struct hci_dev *hdev, struct sk_buff *skb)
{
	struct sock *sk;
	struct hlist_node *node;

	BT_DBG("hdev %p len %d", hdev, skb->len);

	read_lock(&hci_sk_list.lock);
	sk_for_each(sk, node, &hci_sk_list.head) {
		struct hci_filter *flt;
		struct sk_buff *nskb;

		if (sk->sk_state != BT_BOUND || hci_pi(sk)->hdev != hdev)
			continue;

		/* Don't send frame to the socket it came from */
		if (skb->sk == sk)
			continue;

		/* Apply filter */
		flt = &hci_pi(sk)->filter;

		if (!test_bit((bt_cb(skb)->pkt_type == HCI_VENDOR_PKT) ?
				0 : (bt_cb(skb)->pkt_type & HCI_FLT_TYPE_BITS), &flt->type_mask))
			continue;

		if (bt_cb(skb)->pkt_type == HCI_EVENT_PKT) {
			register int evt = (*(__u8 *)skb->data & HCI_FLT_EVENT_BITS);

			if (!hci_test_bit(evt, &flt->event_mask))
				continue;

			if (flt->opcode &&
			    ((evt == HCI_EV_CMD_COMPLETE &&
			      flt->opcode !=
			      get_unaligned((__le16 *)(skb->data + 3))) ||
			     (evt == HCI_EV_CMD_STATUS &&
			      flt->opcode !=
			      get_unaligned((__le16 *)(skb->data + 4)))))
				continue;
		}

		nskb = skb_clone(skb, GFP_ATOMIC);
		if (!nskb)
			continue;

		/* Put type byte before the data */
		memcpy(skb_push(nskb, 1), &bt_cb(nskb)->pkt_type, 1);

		if (sock_queue_rcv_skb(sk, nskb))
			kfree_skb(nskb);
	}
	read_unlock(&hci_sk_list.lock);
}

static int hci_sock_release(struct socket *sock)
{
	struct sock *sk = sock->sk;
	struct hci_dev *hdev;

	BT_DBG("sock %p sk %p", sock, sk);

	if (!sk)
		return 0;

	hdev = hci_pi(sk)->hdev;

	bt_sock_unlink(&hci_sk_list, sk);

	if (hdev) {
		atomic_dec(&hdev->promisc);
		hci_dev_put(hdev);
	}

	sock_orphan(sk);

	skb_queue_purge(&sk->sk_receive_queue);
	skb_queue_purge(&sk->sk_write_queue);

	sock_put(sk);
	return 0;
}

struct bdaddr_list *hci_blacklist_lookup(struct hci_dev *hdev, bdaddr_t *bdaddr)
{
	struct list_head *p;

	list_for_each(p, &hdev->blacklist) {
		struct bdaddr_list *b;

		b = list_entry(p, struct bdaddr_list, list);

		if (bacmp(bdaddr, &b->bdaddr) == 0)
			return b;
	}

	return NULL;
}

static int hci_blacklist_add(struct hci_dev *hdev, void __user *arg)
{
	bdaddr_t bdaddr;
	struct bdaddr_list *entry;

	if (copy_from_user(&bdaddr, arg, sizeof(bdaddr)))
		return -EFAULT;

	if (bacmp(&bdaddr, BDADDR_ANY) == 0)
		return -EBADF;

	if (hci_blacklist_lookup(hdev, &bdaddr))
		return -EEXIST;

	entry = kzalloc(sizeof(struct bdaddr_list), GFP_KERNEL);
	if (!entry)
		return -ENOMEM;

	bacpy(&entry->bdaddr, &bdaddr);

	list_add(&entry->list, &hdev->blacklist);

	return 0;
}

int hci_blacklist_clear(struct hci_dev *hdev)
{
	struct list_head *p, *n;

	list_for_each_safe(p, n, &hdev->blacklist) {
		struct bdaddr_list *b;

		b = list_entry(p, struct bdaddr_list, list);

		list_del(p);
		kfree(b);
	}

	return 0;
}

static int hci_blacklist_del(struct hci_dev *hdev, void __user *arg)
{
	bdaddr_t bdaddr;
	struct bdaddr_list *entry;

	if (copy_from_user(&bdaddr, arg, sizeof(bdaddr)))
		return -EFAULT;

	if (bacmp(&bdaddr, BDADDR_ANY) == 0)
		return hci_blacklist_clear(hdev);

	entry = hci_blacklist_lookup(hdev, &bdaddr);
	if (!entry)
		return -ENOENT;

	list_del(&entry->list);
	kfree(entry);

	return 0;
}

/* Ioctls that require bound socket */
static inline int hci_sock_bound_ioctl(struct sock *sk, unsigned int cmd, unsigned long arg)
{
	struct hci_dev *hdev = hci_pi(sk)->hdev;

	if (!hdev)
		return -EBADFD;

	switch (cmd) {
	case HCISETRAW:
		if (!capable(CAP_NET_ADMIN))
			return -EACCES;

		if (test_bit(HCI_QUIRK_RAW_DEVICE, &hdev->quirks))
			return -EPERM;

		if (arg)
			set_bit(HCI_RAW, &hdev->flags);
		else
			clear_bit(HCI_RAW, &hdev->flags);

		return 0;

	case HCIGETCONNINFO:
		return hci_get_conn_info(hdev, (void __user *) arg);

	case HCIGETAUTHINFO:
		return hci_get_auth_info(hdev, (void __user *) arg);

	case HCIBLOCKADDR:
		if (!capable(CAP_NET_ADMIN))
			return -EACCES;
		return hci_blacklist_add(hdev, (void __user *) arg);

	case HCIUNBLOCKADDR:
		if (!capable(CAP_NET_ADMIN))
			return -EACCES;
		return hci_blacklist_del(hdev, (void __user *) arg);

	default:
		if (hdev->ioctl)
			return hdev->ioctl(hdev, cmd, arg);
		return -EINVAL;
	}
}

static int hci_sock_ioctl(struct socket *sock, unsigned int cmd, unsigned long arg)
{
	struct sock *sk = sock->sk;
	void __user *argp = (void __user *) arg;
	int err;

	BT_DBG("cmd %x arg %lx", cmd, arg);

	switch (cmd) {
	case HCIGETDEVLIST:
		return hci_get_dev_list(argp);

	case HCIGETDEVINFO:
		return hci_get_dev_info(argp);

	case HCIGETCONNLIST:
		return hci_get_conn_list(argp);

	case HCIDEVUP:
		if (!capable(CAP_NET_ADMIN))
			return -EACCES;
		return hci_dev_open(arg);

	case HCIDEVDOWN:
		if (!capable(CAP_NET_ADMIN))
			return -EACCES;
		return hci_dev_close(arg);

	case HCIDEVRESET:
		if (!capable(CAP_NET_ADMIN))
			return -EACCES;
		return hci_dev_reset(arg);

	case HCIDEVRESTAT:
		if (!capable(CAP_NET_ADMIN))
			return -EACCES;
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
			return -EACCES;
		return hci_dev_cmd(cmd, argp);

	case HCIINQUIRY:
		return hci_inquiry(argp);

	default:
		lock_sock(sk);
		err = hci_sock_bound_ioctl(sk, cmd, arg);
		release_sock(sk);
		return err;
	}
}

static int hci_sock_bind(struct socket *sock, struct sockaddr *addr, int addr_len)
{
	struct sockaddr_hci haddr;
	struct sock *sk = sock->sk;
	struct hci_dev *hdev = NULL;
	int len, err = 0;

	BT_DBG("sock %p sk %p", sock, sk);

	if (!addr)
		return -EINVAL;

	memset(&haddr, 0, sizeof(haddr));
	len = min_t(unsigned int, sizeof(haddr), addr_len);
	memcpy(&haddr, addr, len);

	if (haddr.hci_family != AF_BLUETOOTH)
		return -EINVAL;

	if (haddr.hci_channel != HCI_CHANNEL_RAW && !enable_mgmt)
		return -EINVAL;

	lock_sock(sk);

	if (sk->sk_state == BT_BOUND || hci_pi(sk)->hdev) {
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
	hci_pi(sk)->hdev = hdev;
	sk->sk_state = BT_BOUND;

done:
	release_sock(sk);
	return err;
}

static int hci_sock_getname(struct socket *sock, struct sockaddr *addr, int *addr_len, int peer)
{
	struct sockaddr_hci *haddr = (struct sockaddr_hci *) addr;
	struct sock *sk = sock->sk;
	struct hci_dev *hdev = hci_pi(sk)->hdev;

	BT_DBG("sock %p sk %p", sock, sk);

	if (!hdev)
		return -EBADFD;

	lock_sock(sk);

	*addr_len = sizeof(*haddr);
	haddr->hci_family = AF_BLUETOOTH;
	haddr->hci_dev    = hdev->id;

	release_sock(sk);
	return 0;
}

static inline void hci_sock_cmsg(struct sock *sk, struct msghdr *msg, struct sk_buff *skb)
{
	__u32 mask = hci_pi(sk)->cmsg_mask;

	if (mask & HCI_CMSG_DIR) {
		int incoming = bt_cb(skb)->incoming;
		put_cmsg(msg, SOL_HCI, HCI_CMSG_DIR, sizeof(incoming), &incoming);
	}

	if (mask & HCI_CMSG_TSTAMP) {
#ifdef CONFIG_COMPAT
		struct compat_timeval ctv;
#endif
		struct timeval tv;
		void *data;
		int len;

		skb_get_timestamp(skb, &tv);

		data = &tv;
		len = sizeof(tv);
#ifdef CONFIG_COMPAT
		if (msg->msg_flags & MSG_CMSG_COMPAT) {
			ctv.tv_sec = tv.tv_sec;
			ctv.tv_usec = tv.tv_usec;
			data = &ctv;
			len = sizeof(ctv);
		}
#endif

		put_cmsg(msg, SOL_HCI, HCI_CMSG_TSTAMP, len, data);
	}
}

static int hci_sock_recvmsg(struct kiocb *iocb, struct socket *sock,
				struct msghdr *msg, size_t len, int flags)
{
	int noblock = flags & MSG_DONTWAIT;
	struct sock *sk = sock->sk;
	struct sk_buff *skb;
	int copied, err;

	BT_DBG("sock %p, sk %p", sock, sk);

	if (flags & (MSG_OOB))
		return -EOPNOTSUPP;

	if (sk->sk_state == BT_CLOSED)
		return 0;

	skb = skb_recv_datagram(sk, flags, noblock, &err);
	if (!skb)
		return err;

	msg->msg_namelen = 0;

	copied = skb->len;
	if (len < copied) {
		msg->msg_flags |= MSG_TRUNC;
		copied = len;
	}

	skb_reset_transport_header(skb);
	err = skb_copy_datagram_iovec(skb, 0, msg->msg_iov, copied);

	hci_sock_cmsg(sk, msg, skb);

	skb_free_datagram(sk, skb);

	return err ? : copied;
}

static int hci_sock_sendmsg(struct kiocb *iocb, struct socket *sock,
			    struct msghdr *msg, size_t len)
{
	struct sock *sk = sock->sk;
	struct hci_dev *hdev;
	struct sk_buff *skb;
	int err;

	BT_DBG("sock %p sk %p", sock, sk);

	if (msg->msg_flags & MSG_OOB)
		return -EOPNOTSUPP;

	if (msg->msg_flags & ~(MSG_DONTWAIT|MSG_NOSIGNAL|MSG_ERRQUEUE))
		return -EINVAL;

	if (len < 4 || len > HCI_MAX_FRAME_SIZE)
		return -EINVAL;

	lock_sock(sk);

	switch (hci_pi(sk)->channel) {
	case HCI_CHANNEL_RAW:
		break;
	case HCI_CHANNEL_CONTROL:
		err = mgmt_control(sk, msg, len);
		goto done;
	default:
		err = -EINVAL;
		goto done;
	}

	hdev = hci_pi(sk)->hdev;
	if (!hdev) {
		err = -EBADFD;
		goto done;
	}

	if (!test_bit(HCI_UP, &hdev->flags)) {
		err = -ENETDOWN;
		goto done;
	}

	skb = bt_skb_send_alloc(sk, len, msg->msg_flags & MSG_DONTWAIT, &err);
	if (!skb)
		goto done;

	if (memcpy_fromiovec(skb_put(skb, len), msg->msg_iov, len)) {
		err = -EFAULT;
		goto drop;
	}

	bt_cb(skb)->pkt_type = *((unsigned char *) skb->data);
	skb_pull(skb, 1);
	skb->dev = (void *) hdev;

	if (bt_cb(skb)->pkt_type == HCI_COMMAND_PKT) {
		u16 opcode = get_unaligned_le16(skb->data);
		u16 ogf = hci_opcode_ogf(opcode);
		u16 ocf = hci_opcode_ocf(opcode);

		if (((ogf > HCI_SFLT_MAX_OGF) ||
				!hci_test_bit(ocf & HCI_FLT_OCF_BITS, &hci_sec_filter.ocf_mask[ogf])) &&
					!capable(CAP_NET_RAW)) {
			err = -EPERM;
			goto drop;
		}

		if (test_bit(HCI_RAW, &hdev->flags) || (ogf == 0x3f)) {
			skb_queue_tail(&hdev->raw_q, skb);
			tasklet_schedule(&hdev->tx_task);
		} else {
			skb_queue_tail(&hdev->cmd_q, skb);
			tasklet_schedule(&hdev->cmd_task);
		}
	} else {
		if (!capable(CAP_NET_RAW)) {
			err = -EPERM;
			goto drop;
		}

		skb_queue_tail(&hdev->raw_q, skb);
		tasklet_schedule(&hdev->tx_task);
	}

	err = len;

done:
	release_sock(sk);
	return err;

drop:
	kfree_skb(skb);
	goto done;
}

static int hci_sock_setsockopt(struct socket *sock, int level, int optname, char __user *optval, unsigned int len)
{
	struct hci_ufilter uf = { .opcode = 0 };
	struct sock *sk = sock->sk;
	int err = 0, opt = 0;

	BT_DBG("sk %p, opt %d", sk, optname);

	lock_sock(sk);

	switch (optname) {
	case HCI_DATA_DIR:
		if (get_user(opt, (int __user *)optval)) {
			err = -EFAULT;
			break;
		}

		if (opt)
			hci_pi(sk)->cmsg_mask |= HCI_CMSG_DIR;
		else
			hci_pi(sk)->cmsg_mask &= ~HCI_CMSG_DIR;
		break;

	case HCI_TIME_STAMP:
		if (get_user(opt, (int __user *)optval)) {
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
		if (copy_from_user(&uf, optval, len)) {
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

	release_sock(sk);
	return err;
}

static int hci_sock_getsockopt(struct socket *sock, int level, int optname, char __user *optval, int __user *optlen)
{
	struct hci_ufilter uf;
	struct sock *sk = sock->sk;
	int len, opt;

	if (get_user(len, optlen))
		return -EFAULT;

	switch (optname) {
	case HCI_DATA_DIR:
		if (hci_pi(sk)->cmsg_mask & HCI_CMSG_DIR)
			opt = 1;
		else
			opt = 0;

		if (put_user(opt, optval))
			return -EFAULT;
		break;

	case HCI_TIME_STAMP:
		if (hci_pi(sk)->cmsg_mask & HCI_CMSG_TSTAMP)
			opt = 1;
		else
			opt = 0;

		if (put_user(opt, optval))
			return -EFAULT;
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
		if (copy_to_user(optval, &uf, len))
			return -EFAULT;
		break;

	default:
		return -ENOPROTOOPT;
		break;
	}

	return 0;
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

	sk = sk_alloc(net, PF_BLUETOOTH, GFP_ATOMIC, &hci_sk_proto);
	if (!sk)
		return -ENOMEM;

	sock_init_data(sock, sk);

	sock_reset_flag(sk, SOCK_ZAPPED);

	sk->sk_protocol = protocol;

	sock->state = SS_UNCONNECTED;
	sk->sk_state = BT_OPEN;

	bt_sock_link(&hci_sk_list, sk);
	return 0;
}

static int hci_sock_dev_event(struct notifier_block *this, unsigned long event, void *ptr)
{
	struct hci_dev *hdev = (struct hci_dev *) ptr;
	struct hci_ev_si_device ev;

	BT_DBG("hdev %s event %ld", hdev->name, event);

	/* Send event to sockets */
	ev.event  = event;
	ev.dev_id = hdev->id;
	hci_si_event(NULL, HCI_EV_SI_DEVICE, sizeof(ev), &ev);

	if (event == HCI_DEV_UNREG) {
		struct sock *sk;
		struct hlist_node *node;

		/* Detach sockets from device */
		read_lock(&hci_sk_list.lock);
		sk_for_each(sk, node, &hci_sk_list.head) {
			local_bh_disable();
			bh_lock_sock_nested(sk);
			if (hci_pi(sk)->hdev == hdev) {
				hci_pi(sk)->hdev = NULL;
				sk->sk_err = EPIPE;
				sk->sk_state = BT_OPEN;
				sk->sk_state_change(sk);

				hci_dev_put(hdev);
			}
			bh_unlock_sock(sk);
			local_bh_enable();
		}
		read_unlock(&hci_sk_list.lock);
	}

	return NOTIFY_DONE;
}

static const struct net_proto_family hci_sock_family_ops = {
	.family	= PF_BLUETOOTH,
	.owner	= THIS_MODULE,
	.create	= hci_sock_create,
};

static struct notifier_block hci_sock_nblock = {
	.notifier_call = hci_sock_dev_event
};

int __init hci_sock_init(void)
{
	int err;

	err = proto_register(&hci_sk_proto, 0);
	if (err < 0)
		return err;

	err = bt_sock_register(BTPROTO_HCI, &hci_sock_family_ops);
	if (err < 0)
		goto error;

	hci_register_notifier(&hci_sock_nblock);

	BT_INFO("HCI socket layer initialized");

	return 0;

error:
	BT_ERR("HCI socket registration failed");
	proto_unregister(&hci_sk_proto);
	return err;
}

void __exit hci_sock_cleanup(void)
{
	if (bt_sock_unregister(BTPROTO_HCI) < 0)
		BT_ERR("HCI socket unregistration failed");

	hci_unregister_notifier(&hci_sock_nblock);

	proto_unregister(&hci_sk_proto);
}

module_param(enable_mgmt, bool, 0644);
MODULE_PARM_DESC(enable_mgmt, "Enable Management interface");
