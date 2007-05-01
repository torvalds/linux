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

/* Bluetooth address family and sockets. */

#include <linux/module.h>

#include <linux/types.h>
#include <linux/list.h>
#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/skbuff.h>
#include <linux/init.h>
#include <linux/poll.h>
#include <net/sock.h>

#if defined(CONFIG_KMOD)
#include <linux/kmod.h>
#endif

#include <net/bluetooth/bluetooth.h>

#ifndef CONFIG_BT_SOCK_DEBUG
#undef  BT_DBG
#define BT_DBG(D...)
#endif

#define VERSION "2.11"

/* Bluetooth sockets */
#define BT_MAX_PROTO	8
static struct net_proto_family *bt_proto[BT_MAX_PROTO];
static DEFINE_RWLOCK(bt_proto_lock);

int bt_sock_register(int proto, struct net_proto_family *ops)
{
	int err = 0;

	if (proto < 0 || proto >= BT_MAX_PROTO)
		return -EINVAL;

	write_lock(&bt_proto_lock);

	if (bt_proto[proto])
		err = -EEXIST;
	else
		bt_proto[proto] = ops;

	write_unlock(&bt_proto_lock);

	return err;
}
EXPORT_SYMBOL(bt_sock_register);

int bt_sock_unregister(int proto)
{
	int err = 0;

	if (proto < 0 || proto >= BT_MAX_PROTO)
		return -EINVAL;

	write_lock(&bt_proto_lock);

	if (!bt_proto[proto])
		err = -ENOENT;
	else
		bt_proto[proto] = NULL;

	write_unlock(&bt_proto_lock);

	return err;
}
EXPORT_SYMBOL(bt_sock_unregister);

static int bt_sock_create(struct socket *sock, int proto)
{
	int err;

	if (proto < 0 || proto >= BT_MAX_PROTO)
		return -EINVAL;

#if defined(CONFIG_KMOD)
	if (!bt_proto[proto]) {
		request_module("bt-proto-%d", proto);
	}
#endif

	err = -EPROTONOSUPPORT;

	read_lock(&bt_proto_lock);

	if (bt_proto[proto] && try_module_get(bt_proto[proto]->owner)) {
		err = bt_proto[proto]->create(sock, proto);
		module_put(bt_proto[proto]->owner);
	}

	read_unlock(&bt_proto_lock);

	return err;
}

void bt_sock_link(struct bt_sock_list *l, struct sock *sk)
{
	write_lock_bh(&l->lock);
	sk_add_node(sk, &l->head);
	write_unlock_bh(&l->lock);
}
EXPORT_SYMBOL(bt_sock_link);

void bt_sock_unlink(struct bt_sock_list *l, struct sock *sk)
{
	write_lock_bh(&l->lock);
	sk_del_node_init(sk);
	write_unlock_bh(&l->lock);
}
EXPORT_SYMBOL(bt_sock_unlink);

void bt_accept_enqueue(struct sock *parent, struct sock *sk)
{
	BT_DBG("parent %p, sk %p", parent, sk);

	sock_hold(sk);
	list_add_tail(&bt_sk(sk)->accept_q, &bt_sk(parent)->accept_q);
	bt_sk(sk)->parent = parent;
	parent->sk_ack_backlog++;
}
EXPORT_SYMBOL(bt_accept_enqueue);

void bt_accept_unlink(struct sock *sk)
{
	BT_DBG("sk %p state %d", sk, sk->sk_state);

	list_del_init(&bt_sk(sk)->accept_q);
	bt_sk(sk)->parent->sk_ack_backlog--;
	bt_sk(sk)->parent = NULL;
	sock_put(sk);
}
EXPORT_SYMBOL(bt_accept_unlink);

struct sock *bt_accept_dequeue(struct sock *parent, struct socket *newsock)
{
	struct list_head *p, *n;
	struct sock *sk;

	BT_DBG("parent %p", parent);

	list_for_each_safe(p, n, &bt_sk(parent)->accept_q) {
		sk = (struct sock *) list_entry(p, struct bt_sock, accept_q);

		lock_sock(sk);

		/* FIXME: Is this check still needed */
		if (sk->sk_state == BT_CLOSED) {
			release_sock(sk);
			bt_accept_unlink(sk);
			continue;
		}

		if (sk->sk_state == BT_CONNECTED || !newsock) {
			bt_accept_unlink(sk);
			if (newsock)
				sock_graft(sk, newsock);
			release_sock(sk);
			return sk;
		}

		release_sock(sk);
	}
	return NULL;
}
EXPORT_SYMBOL(bt_accept_dequeue);

int bt_sock_recvmsg(struct kiocb *iocb, struct socket *sock,
	struct msghdr *msg, size_t len, int flags)
{
	int noblock = flags & MSG_DONTWAIT;
	struct sock *sk = sock->sk;
	struct sk_buff *skb;
	size_t copied;
	int err;

	BT_DBG("sock %p sk %p len %d", sock, sk, len);

	if (flags & (MSG_OOB))
		return -EOPNOTSUPP;

	if (!(skb = skb_recv_datagram(sk, flags, noblock, &err))) {
		if (sk->sk_shutdown & RCV_SHUTDOWN)
			return 0;
		return err;
	}

	msg->msg_namelen = 0;

	copied = skb->len;
	if (len < copied) {
		msg->msg_flags |= MSG_TRUNC;
		copied = len;
	}

	skb_reset_transport_header(skb);
	err = skb_copy_datagram_iovec(skb, 0, msg->msg_iov, copied);

	skb_free_datagram(sk, skb);

	return err ? : copied;
}
EXPORT_SYMBOL(bt_sock_recvmsg);

static inline unsigned int bt_accept_poll(struct sock *parent)
{
	struct list_head *p, *n;
	struct sock *sk;

	list_for_each_safe(p, n, &bt_sk(parent)->accept_q) {
		sk = (struct sock *) list_entry(p, struct bt_sock, accept_q);
		if (sk->sk_state == BT_CONNECTED)
			return POLLIN | POLLRDNORM;
	}

	return 0;
}

unsigned int bt_sock_poll(struct file * file, struct socket *sock, poll_table *wait)
{
	struct sock *sk = sock->sk;
	unsigned int mask = 0;

	BT_DBG("sock %p, sk %p", sock, sk);

	poll_wait(file, sk->sk_sleep, wait);

	if (sk->sk_state == BT_LISTEN)
		return bt_accept_poll(sk);

	if (sk->sk_err || !skb_queue_empty(&sk->sk_error_queue))
		mask |= POLLERR;

	if (sk->sk_shutdown & RCV_SHUTDOWN)
		mask |= POLLRDHUP;

	if (sk->sk_shutdown == SHUTDOWN_MASK)
		mask |= POLLHUP;

	if (!skb_queue_empty(&sk->sk_receive_queue) ||
			(sk->sk_shutdown & RCV_SHUTDOWN))
		mask |= POLLIN | POLLRDNORM;

	if (sk->sk_state == BT_CLOSED)
		mask |= POLLHUP;

	if (sk->sk_state == BT_CONNECT ||
			sk->sk_state == BT_CONNECT2 ||
			sk->sk_state == BT_CONFIG)
		return mask;

	if (sock_writeable(sk))
		mask |= POLLOUT | POLLWRNORM | POLLWRBAND;
	else
		set_bit(SOCK_ASYNC_NOSPACE, &sk->sk_socket->flags);

	return mask;
}
EXPORT_SYMBOL(bt_sock_poll);

int bt_sock_wait_state(struct sock *sk, int state, unsigned long timeo)
{
	DECLARE_WAITQUEUE(wait, current);
	int err = 0;

	BT_DBG("sk %p", sk);

	add_wait_queue(sk->sk_sleep, &wait);
	while (sk->sk_state != state) {
		set_current_state(TASK_INTERRUPTIBLE);

		if (!timeo) {
			err = -EINPROGRESS;
			break;
		}

		if (signal_pending(current)) {
			err = sock_intr_errno(timeo);
			break;
		}

		release_sock(sk);
		timeo = schedule_timeout(timeo);
		lock_sock(sk);

		err = sock_error(sk);
		if (err)
			break;
	}
	set_current_state(TASK_RUNNING);
	remove_wait_queue(sk->sk_sleep, &wait);
	return err;
}
EXPORT_SYMBOL(bt_sock_wait_state);

static struct net_proto_family bt_sock_family_ops = {
	.owner	= THIS_MODULE,
	.family	= PF_BLUETOOTH,
	.create	= bt_sock_create,
};

static int __init bt_init(void)
{
	int err;

	BT_INFO("Core ver %s", VERSION);

	err = bt_sysfs_init();
	if (err < 0)
		return err;

	err = sock_register(&bt_sock_family_ops);
	if (err < 0) {
		bt_sysfs_cleanup();
		return err;
	}

	BT_INFO("HCI device and connection manager initialized");

	hci_sock_init();

	return 0;
}

static void __exit bt_exit(void)
{
	hci_sock_cleanup();

	sock_unregister(PF_BLUETOOTH);

	bt_sysfs_cleanup();
}

subsys_initcall(bt_init);
module_exit(bt_exit);

MODULE_AUTHOR("Maxim Krasnyansky <maxk@qualcomm.com>, Marcel Holtmann <marcel@holtmann.org>");
MODULE_DESCRIPTION("Bluetooth Core ver " VERSION);
MODULE_VERSION(VERSION);
MODULE_LICENSE("GPL");
MODULE_ALIAS_NETPROTO(PF_BLUETOOTH);
