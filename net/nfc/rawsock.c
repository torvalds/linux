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
 * along with this program; if not, write to the
 * Free Software Foundation, Inc.,
 * 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": %s: " fmt, __func__

#include <net/tcp_states.h>
#include <linux/nfc.h>
#include <linux/export.h>

#include "nfc.h"

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
	*skb_push(skb, NFC_HEADER_SIZE) = 0;

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
		goto error;

	err = sock_queue_rcv_skb(sk, skb);
	if (err)
		goto error;

	spin_lock_bh(&sk->sk_write_queue.lock);
	if (!skb_queue_empty(&sk->sk_write_queue))
		schedule_work(&nfc_rawsock(sk)->tx_work);
	else
		nfc_rawsock(sk)->tx_work_scheduled = false;
	spin_unlock_bh(&sk->sk_write_queue.lock);

	sock_put(sk);
	return;

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

static int rawsock_sendmsg(struct kiocb *iocb, struct socket *sock,
			   struct msghdr *msg, size_t len)
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

	rc = memcpy_fromiovec(skb_put(skb, len), msg->msg_iov, len);
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

static int rawsock_recvmsg(struct kiocb *iocb, struct socket *sock,
			   struct msghdr *msg, size_t len, int flags)
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

	msg->msg_namelen = 0;

	copied = skb->len;
	if (len < copied) {
		msg->msg_flags |= MSG_TRUNC;
		copied = len;
	}

	rc = skb_copy_datagram_iovec(skb, 0, msg->msg_iov, copied);

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

static void rawsock_destruct(struct sock *sk)
{
	pr_debug("sk=%p\n", sk);

	if (sk->sk_state == TCP_ESTABLISHED) {
		nfc_deactivate_target(nfc_rawsock(sk)->dev,
				      nfc_rawsock(sk)->target_idx);
		nfc_put_device(nfc_rawsock(sk)->dev);
	}

	skb_queue_purge(&sk->sk_receive_queue);

	if (!sock_flag(sk, SOCK_DEAD)) {
		pr_err("Freeing alive NFC raw socket %p\n", sk);
		return;
	}
}

static int rawsock_create(struct net *net, struct socket *sock,
			  const struct nfc_protocol *nfc_proto)
{
	struct sock *sk;

	pr_debug("sock=%p\n", sock);

	if (sock->type != SOCK_SEQPACKET)
		return -ESOCKTNOSUPPORT;

	sock->ops = &rawsock_ops;

	sk = sk_alloc(net, PF_NFC, GFP_ATOMIC, nfc_proto->proto);
	if (!sk)
		return -ENOMEM;

	sock_init_data(sock, sk);
	sk->sk_protocol = nfc_proto->id;
	sk->sk_destruct = rawsock_destruct;
	sock->state = SS_UNCONNECTED;

	INIT_WORK(&nfc_rawsock(sk)->tx_work, rawsock_tx_work);
	nfc_rawsock(sk)->tx_work_scheduled = false;

	return 0;
}

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
