/*
 * Copyright (C) 2011 Instituto Nokia de Tecnologia
 *
 * Authors:
 *    Aloisio Almeida Jr <aloisio.almeida@openbossa.org>
 *    Lauro Ramos Venancio <lauro.venancio@openbossa.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": %s: " fmt, __func__

#include <net/tcp_states.h>
#include <linux/nfc.h>
#include <linux/export.h>

#include "nfc.h"

static struct nfc_sock_list raw_sk_list = {
	.lock = __RW_LOCK_UNLOCKED(raw_sk_list.lock)
};

static void nfc_sock_link(struct nfc_sock_list *l, struct sock *sk)
{
	write_lock(&l->lock);
	sk_add_node(sk, &l->head);
	write_unlock(&l->lock);
}

static void nfc_sock_unlink(struct nfc_sock_list *l, struct sock *sk)
{
	write_lock(&l->lock);
	sk_del_node_init(sk);
	write_unlock(&l->lock);
}

static void rawsock_write_queue_purge(struct sock *sk)
{
	pr_debug("sk=%p\n", sk);

	spin_lock_bh(&sk->sk_write_queue.lock);
	__skb_queue_purge(&sk->sk_write_queue);
	nfc_rawsock(sk)->tx_work_scheduled = false;
	spin_unlock_bh(&sk->sk_write_queue.lock);
}

static void rawsock_report_error(struct sock *sk, int err)
{
	pr_debug("sk=%p err=%d\n", sk, err);

	sk->sk_shutdown = SHUTDOWN_MASK;
	sk->sk_err = -err;
	sk->sk_error_report(sk);

	rawsock_write_queue_purge(sk);
}

static int rawsock_release(struct socket *sock)
{
	struct sock *sk = sock->sk;

	pr_debug("sock=%p sk=%p\n", sock, sk);

	if (!sk)
		return 0;

	if (sock->type == SOCK_RAW)
		nfc_sock_unlink(&raw_sk_list, sk);

	sock_orphan(sk);
	sock_put(sk);

	return 0;
}

static int rawsock_connect(struct socket *sock, struct sockaddr *_addr,
			   int len, int flags)
{
	struct sock *sk = sock->sk;
	struct sockaddr_nfc *addr = (struct sockaddr_nfc *)_addr;
	struct nfc_dev *dev;
	int rc = 0;

	pr_debug("sock=%p sk=%p flags=%d\n", sock, sk, flags);

	if (!addr || len < sizeof(struct sockaddr_nfc) ||
	    addr->sa_family != AF_NFC)
		return -EINVAL;

	pr_debug("addr dev_idx=%u target_idx=%u protocol=%u\n",
		 addr->dev_idx, addr->target_idx, addr->nfc_protocol);

	lock_sock(sk);

	if (sock->state == SS_CONNECTED) {
		rc = -EISCONN;
		goto error;
	}

	dev = nfc_get_device(addr->dev_idx);
	if (!dev) {
		rc = -ENODEV;
		goto error;
	}

	if (addr->target_idx > dev->target_next_idx - 1 ||
	    addr->target_idx < dev->target_next_idx - dev->n_targets) {
		rc = -EINVAL;
		goto error;
	}

	rc = nfc_activate_target(dev, addr->target_idx, addr->nfc_protocol);
	if (rc)
		goto put_dev;

	nfc_rawsock(sk)->dev = dev;
	nfc_rawsock(sk)->target_idx = addr->target_idx;
	sock->state = SS_CONNECTED;
	sk->sk_state = TCP_ESTABLISHED;
	sk->sk_state_change(sk);

	release_sock(sk);
	return 0;

put_dev:
	nfc_put_device(dev);
error:
	release_sock(sk);
	return rc;
}

static int rawsock_add_header(struct sk_buff *skb)
{
	*(u8 *)skb_push(skb, NFC_HEADER_SIZE) = 0;

	return 0;
}

static void rawsock_data_exchange_complete(void *context, struct sk_buff *skb,
					   int err)
{
	struct sock *sk = (struct sock *) context;

	BUG_ON(in_irq());

	pr_debug("sk=%p err=%d\n", sk, err);

	if (err)
		goto error;

	err = rawsock_add_header(skb);
	if (err)
		goto error_skb;

	err = sock_queue_rcv_skb(sk, skb);
	if (err)
		goto error_skb;

	spin_lock_bh(&sk->sk_write_queue.lock);
	if (!skb_queue_empty(&sk->sk_write_queue))
		schedule_work(&nfc_rawsock(sk)->tx_work);
	else
		nfc_rawsock(sk)->tx_work_scheduled = false;
	spin_unlock_bh(&sk->sk_write_queue.lock);

	sock_put(sk);
	return;

error_skb:
	kfree_skb(skb);

error:
	rawsock_report_error(sk, err);
	sock_put(sk);
}

static void rawsock_tx_work(struct work_struct *work)
{
	struct sock *sk = to_rawsock_sk(work);
	struct nfc_dev *dev = nfc_rawsock(sk)->dev;
	u32 target_idx = nfc_rawsock(sk)->target_idx;
	struct sk_buff *skb;
	int rc;

	pr_debug("sk=%p target_idx=%u\n", sk, target_idx);

	if (sk->sk_shutdown & SEND_SHUTDOWN) {
		rawsock_write_queue_purge(sk);
		return;
	}

	skb = skb_dequeue(&sk->sk_write_queue);

	sock_hold(sk);
	rc = nfc_data_exchange(dev, target_idx, skb,
			       rawsock_data_exchange_complete, sk);
	if (rc) {
		rawsock_report_error(sk, rc);
		sock_put(sk);
	}
}

static int rawsock_sendmsg(struct socket *sock, struct msghdr *msg, size_t len)
{
	struct sock *sk = sock->sk;
	struct nfc_dev *dev = nfc_rawsock(sk)->dev;
	struct sk_buff *skb;
	int rc;

	pr_debug("sock=%p sk=%p len=%zu\n", sock, sk, len);

	if (msg->msg_namelen)
		return -EOPNOTSUPP;

	if (sock->state != SS_CONNECTED)
		return -ENOTCONN;

	skb = nfc_alloc_send_skb(dev, sk, msg->msg_flags, len, &rc);
	if (skb == NULL)
		return rc;

	rc = memcpy_from_msg(skb_put(skb, len), msg, len);
	if (rc < 0) {
		kfree_skb(skb);
		return rc;
	}

	spin_lock_bh(&sk->sk_write_queue.lock);
	__skb_queue_tail(&sk->sk_write_queue, skb);
	if (!nfc_rawsock(sk)->tx_work_scheduled) {
		schedule_work(&nfc_rawsock(sk)->tx_work);
		nfc_rawsock(sk)->tx_work_scheduled = true;
	}
	spin_unlock_bh(&sk->sk_write_queue.lock);

	return len;
}

static int rawsock_recvmsg(struct socket *sock, struct msghdr *msg, size_t len,
			   int flags)
{
	int noblock = flags & MSG_DONTWAIT;
	struct sock *sk = sock->sk;
	struct sk_buff *skb;
	int copied;
	int rc;

	pr_debug("sock=%p sk=%p len=%zu flags=%d\n", sock, sk, len, flags);

	skb = skb_recv_datagram(sk, flags, noblock, &rc);
	if (!skb)
		return rc;

	copied = skb->len;
	if (len < copied) {
		msg->msg_flags |= MSG_TRUNC;
		copied = len;
	}

	rc = skb_copy_datagram_msg(skb, 0, msg, copied);

	skb_free_datagram(sk, skb);

	return rc ? : copied;
}

static const struct proto_ops rawsock_ops = {
	.family         = PF_NFC,
	.owner          = THIS_MODULE,
	.release        = rawsock_release,
	.bind           = sock_no_bind,
	.connect        = rawsock_connect,
	.socketpair     = sock_no_socketpair,
	.accept         = sock_no_accept,
	.getname        = sock_no_getname,
	.poll           = datagram_poll,
	.ioctl          = sock_no_ioctl,
	.listen         = sock_no_listen,
	.shutdown       = sock_no_shutdown,
	.setsockopt     = sock_no_setsockopt,
	.getsockopt     = sock_no_getsockopt,
	.sendmsg        = rawsock_sendmsg,
	.recvmsg        = rawsock_recvmsg,
	.mmap           = sock_no_mmap,
};

static const struct proto_ops rawsock_raw_ops = {
	.family         = PF_NFC,
	.owner          = THIS_MODULE,
	.release        = rawsock_release,
	.bind           = sock_no_bind,
	.connect        = sock_no_connect,
	.socketpair     = sock_no_socketpair,
	.accept         = sock_no_accept,
	.getname        = sock_no_getname,
	.poll           = datagram_poll,
	.ioctl          = sock_no_ioctl,
	.listen         = sock_no_listen,
	.shutdown       = sock_no_shutdown,
	.setsockopt     = sock_no_setsockopt,
	.getsockopt     = sock_no_getsockopt,
	.sendmsg        = sock_no_sendmsg,
	.recvmsg        = rawsock_recvmsg,
	.mmap           = sock_no_mmap,
};

static void rawsock_destruct(struct sock *sk)
{
	pr_debug("sk=%p\n", sk);

	if (sk->sk_state == TCP_ESTABLISHED) {
		nfc_deactivate_target(nfc_rawsock(sk)->dev,
				      nfc_rawsock(sk)->target_idx,
				      NFC_TARGET_MODE_IDLE);
		nfc_put_device(nfc_rawsock(sk)->dev);
	}

	skb_queue_purge(&sk->sk_receive_queue);

	if (!sock_flag(sk, SOCK_DEAD)) {
		pr_err("Freeing alive NFC raw socket %p\n", sk);
		return;
	}
}

static int rawsock_create(struct net *net, struct socket *sock,
			  const struct nfc_protocol *nfc_proto, int kern)
{
	struct sock *sk;

	pr_debug("sock=%p\n", sock);

	if ((sock->type != SOCK_SEQPACKET) && (sock->type != SOCK_RAW))
		return -ESOCKTNOSUPPORT;

	if (sock->type == SOCK_RAW) {
		if (!capable(CAP_NET_RAW))
			return -EPERM;
		sock->ops = &rawsock_raw_ops;
	} else {
		sock->ops = &rawsock_ops;
	}

	sk = sk_alloc(net, PF_NFC, GFP_ATOMIC, nfc_proto->proto, kern);
	if (!sk)
		return -ENOMEM;

	sock_init_data(sock, sk);
	sk->sk_protocol = nfc_proto->id;
	sk->sk_destruct = rawsock_destruct;
	sock->state = SS_UNCONNECTED;
	if (sock->type == SOCK_RAW)
		nfc_sock_link(&raw_sk_list, sk);
	else {
		INIT_WORK(&nfc_rawsock(sk)->tx_work, rawsock_tx_work);
		nfc_rawsock(sk)->tx_work_scheduled = false;
	}

	return 0;
}

void nfc_send_to_raw_sock(struct nfc_dev *dev, struct sk_buff *skb,
			  u8 payload_type, u8 direction)
{
	struct sk_buff *skb_copy = NULL, *nskb;
	struct sock *sk;
	u8 *data;

	read_lock(&raw_sk_list.lock);

	sk_for_each(sk, &raw_sk_list.head) {
		if (!skb_copy) {
			skb_copy = __pskb_copy_fclone(skb, NFC_RAW_HEADER_SIZE,
						      GFP_ATOMIC, true);
			if (!skb_copy)
				continue;

			data = skb_push(skb_copy, NFC_RAW_HEADER_SIZE);

			data[0] = dev ? dev->idx : 0xFF;
			data[1] = direction & 0x01;
			data[1] |= (payload_type << 1);
		}

		nskb = skb_clone(skb_copy, GFP_ATOMIC);
		if (!nskb)
			continue;

		if (sock_queue_rcv_skb(sk, nskb))
			kfree_skb(nskb);
	}

	read_unlock(&raw_sk_list.lock);

	kfree_skb(skb_copy);
}
EXPORT_SYMBOL(nfc_send_to_raw_sock);

static struct proto rawsock_proto = {
	.name     = "NFC_RAW",
	.owner    = THIS_MODULE,
	.obj_size = sizeof(struct nfc_rawsock),
};

static const struct nfc_protocol rawsock_nfc_proto = {
	.id	  = NFC_SOCKPROTO_RAW,
	.proto    = &rawsock_proto,
	.owner    = THIS_MODULE,
	.create   = rawsock_create
};

int __init rawsock_init(void)
{
	int rc;

	rc = nfc_proto_register(&rawsock_nfc_proto);

	return rc;
}

void rawsock_exit(void)
{
	nfc_proto_unregister(&rawsock_nfc_proto);
}
