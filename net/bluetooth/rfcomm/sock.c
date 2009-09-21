/*
   RFCOMM implementation for Linux Bluetooth stack (BlueZ).
   Copyright (C) 2002 Maxim Krasnyansky <maxk@qualcomm.com>
   Copyright (C) 2002 Marcel Holtmann <marcel@holtmann.org>

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

/*
 * RFCOMM sockets.
 */

#include <linux/module.h>

#include <linux/types.h>
#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/poll.h>
#include <linux/fcntl.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/socket.h>
#include <linux/skbuff.h>
#include <linux/list.h>
#include <linux/device.h>
#include <net/sock.h>

#include <asm/system.h>
#include <asm/uaccess.h>

#include <net/bluetooth/bluetooth.h>
#include <net/bluetooth/hci_core.h>
#include <net/bluetooth/l2cap.h>
#include <net/bluetooth/rfcomm.h>

static const struct proto_ops rfcomm_sock_ops;

static struct bt_sock_list rfcomm_sk_list = {
	.lock = __RW_LOCK_UNLOCKED(rfcomm_sk_list.lock)
};

static void rfcomm_sock_close(struct sock *sk);
static void rfcomm_sock_kill(struct sock *sk);

/* ---- DLC callbacks ----
 *
 * called under rfcomm_dlc_lock()
 */
static void rfcomm_sk_data_ready(struct rfcomm_dlc *d, struct sk_buff *skb)
{
	struct sock *sk = d->owner;
	if (!sk)
		return;

	atomic_add(skb->len, &sk->sk_rmem_alloc);
	skb_queue_tail(&sk->sk_receive_queue, skb);
	sk->sk_data_ready(sk, skb->len);

	if (atomic_read(&sk->sk_rmem_alloc) >= sk->sk_rcvbuf)
		rfcomm_dlc_throttle(d);
}

static void rfcomm_sk_state_change(struct rfcomm_dlc *d, int err)
{
	struct sock *sk = d->owner, *parent;
	if (!sk)
		return;

	BT_DBG("dlc %p state %ld err %d", d, d->state, err);

	bh_lock_sock(sk);

	if (err)
		sk->sk_err = err;

	sk->sk_state = d->state;

	parent = bt_sk(sk)->parent;
	if (parent) {
		if (d->state == BT_CLOSED) {
			sock_set_flag(sk, SOCK_ZAPPED);
			bt_accept_unlink(sk);
		}
		parent->sk_data_ready(parent, 0);
	} else {
		if (d->state == BT_CONNECTED)
			rfcomm_session_getaddr(d->session, &bt_sk(sk)->src, NULL);
		sk->sk_state_change(sk);
	}

	bh_unlock_sock(sk);

	if (parent && sock_flag(sk, SOCK_ZAPPED)) {
		/* We have to drop DLC lock here, otherwise
		 * rfcomm_sock_destruct() will dead lock. */
		rfcomm_dlc_unlock(d);
		rfcomm_sock_kill(sk);
		rfcomm_dlc_lock(d);
	}
}

/* ---- Socket functions ---- */
static struct sock *__rfcomm_get_sock_by_addr(u8 channel, bdaddr_t *src)
{
	struct sock *sk = NULL;
	struct hlist_node *node;

	sk_for_each(sk, node, &rfcomm_sk_list.head) {
		if (rfcomm_pi(sk)->channel == channel &&
				!bacmp(&bt_sk(sk)->src, src))
			break;
	}

	return node ? sk : NULL;
}

/* Find socket with channel and source bdaddr.
 * Returns closest match.
 */
static struct sock *__rfcomm_get_sock_by_channel(int state, u8 channel, bdaddr_t *src)
{
	struct sock *sk = NULL, *sk1 = NULL;
	struct hlist_node *node;

	sk_for_each(sk, node, &rfcomm_sk_list.head) {
		if (state && sk->sk_state != state)
			continue;

		if (rfcomm_pi(sk)->channel == channel) {
			/* Exact match. */
			if (!bacmp(&bt_sk(sk)->src, src))
				break;

			/* Closest match */
			if (!bacmp(&bt_sk(sk)->src, BDADDR_ANY))
				sk1 = sk;
		}
	}
	return node ? sk : sk1;
}

/* Find socket with given address (channel, src).
 * Returns locked socket */
static inline struct sock *rfcomm_get_sock_by_channel(int state, u8 channel, bdaddr_t *src)
{
	struct sock *s;
	read_lock(&rfcomm_sk_list.lock);
	s = __rfcomm_get_sock_by_channel(state, channel, src);
	if (s) bh_lock_sock(s);
	read_unlock(&rfcomm_sk_list.lock);
	return s;
}

static void rfcomm_sock_destruct(struct sock *sk)
{
	struct rfcomm_dlc *d = rfcomm_pi(sk)->dlc;

	BT_DBG("sk %p dlc %p", sk, d);

	skb_queue_purge(&sk->sk_receive_queue);
	skb_queue_purge(&sk->sk_write_queue);

	rfcomm_dlc_lock(d);
	rfcomm_pi(sk)->dlc = NULL;

	/* Detach DLC if it's owned by this socket */
	if (d->owner == sk)
		d->owner = NULL;
	rfcomm_dlc_unlock(d);

	rfcomm_dlc_put(d);
}

static void rfcomm_sock_cleanup_listen(struct sock *parent)
{
	struct sock *sk;

	BT_DBG("parent %p", parent);

	/* Close not yet accepted dlcs */
	while ((sk = bt_accept_dequeue(parent, NULL))) {
		rfcomm_sock_close(sk);
		rfcomm_sock_kill(sk);
	}

	parent->sk_state  = BT_CLOSED;
	sock_set_flag(parent, SOCK_ZAPPED);
}

/* Kill socket (only if zapped and orphan)
 * Must be called on unlocked socket.
 */
static void rfcomm_sock_kill(struct sock *sk)
{
	if (!sock_flag(sk, SOCK_ZAPPED) || sk->sk_socket)
		return;

	BT_DBG("sk %p state %d refcnt %d", sk, sk->sk_state, atomic_read(&sk->sk_refcnt));

	/* Kill poor orphan */
	bt_sock_unlink(&rfcomm_sk_list, sk);
	sock_set_flag(sk, SOCK_DEAD);
	sock_put(sk);
}

static void __rfcomm_sock_close(struct sock *sk)
{
	struct rfcomm_dlc *d = rfcomm_pi(sk)->dlc;

	BT_DBG("sk %p state %d socket %p", sk, sk->sk_state, sk->sk_socket);

	switch (sk->sk_state) {
	case BT_LISTEN:
		rfcomm_sock_cleanup_listen(sk);
		break;

	case BT_CONNECT:
	case BT_CONNECT2:
	case BT_CONFIG:
	case BT_CONNECTED:
		rfcomm_dlc_close(d, 0);

	default:
		sock_set_flag(sk, SOCK_ZAPPED);
		break;
	}
}

/* Close socket.
 * Must be called on unlocked socket.
 */
static void rfcomm_sock_close(struct sock *sk)
{
	lock_sock(sk);
	__rfcomm_sock_close(sk);
	release_sock(sk);
}

static void rfcomm_sock_init(struct sock *sk, struct sock *parent)
{
	struct rfcomm_pinfo *pi = rfcomm_pi(sk);

	BT_DBG("sk %p", sk);

	if (parent) {
		sk->sk_type = parent->sk_type;
		pi->dlc->defer_setup = bt_sk(parent)->defer_setup;

		pi->sec_level = rfcomm_pi(parent)->sec_level;
		pi->role_switch = rfcomm_pi(parent)->role_switch;
	} else {
		pi->dlc->defer_setup = 0;

		pi->sec_level = BT_SECURITY_LOW;
		pi->role_switch = 0;
	}

	pi->dlc->sec_level = pi->sec_level;
	pi->dlc->role_switch = pi->role_switch;
}

static struct proto rfcomm_proto = {
	.name		= "RFCOMM",
	.owner		= THIS_MODULE,
	.obj_size	= sizeof(struct rfcomm_pinfo)
};

static struct sock *rfcomm_sock_alloc(struct net *net, struct socket *sock, int proto, gfp_t prio)
{
	struct rfcomm_dlc *d;
	struct sock *sk;

	sk = sk_alloc(net, PF_BLUETOOTH, prio, &rfcomm_proto);
	if (!sk)
		return NULL;

	sock_init_data(sock, sk);
	INIT_LIST_HEAD(&bt_sk(sk)->accept_q);

	d = rfcomm_dlc_alloc(prio);
	if (!d) {
		sk_free(sk);
		return NULL;
	}

	d->data_ready   = rfcomm_sk_data_ready;
	d->state_change = rfcomm_sk_state_change;

	rfcomm_pi(sk)->dlc = d;
	d->owner = sk;

	sk->sk_destruct = rfcomm_sock_destruct;
	sk->sk_sndtimeo = RFCOMM_CONN_TIMEOUT;

	sk->sk_sndbuf = RFCOMM_MAX_CREDITS * RFCOMM_DEFAULT_MTU * 10;
	sk->sk_rcvbuf = RFCOMM_MAX_CREDITS * RFCOMM_DEFAULT_MTU * 10;

	sock_reset_flag(sk, SOCK_ZAPPED);

	sk->sk_protocol = proto;
	sk->sk_state    = BT_OPEN;

	bt_sock_link(&rfcomm_sk_list, sk);

	BT_DBG("sk %p", sk);
	return sk;
}

static int rfcomm_sock_create(struct net *net, struct socket *sock, int protocol)
{
	struct sock *sk;

	BT_DBG("sock %p", sock);

	sock->state = SS_UNCONNECTED;

	if (sock->type != SOCK_STREAM && sock->type != SOCK_RAW)
		return -ESOCKTNOSUPPORT;

	sock->ops = &rfcomm_sock_ops;

	sk = rfcomm_sock_alloc(net, sock, protocol, GFP_ATOMIC);
	if (!sk)
		return -ENOMEM;

	rfcomm_sock_init(sk, NULL);
	return 0;
}

static int rfcomm_sock_bind(struct socket *sock, struct sockaddr *addr, int addr_len)
{
	struct sockaddr_rc *sa = (struct sockaddr_rc *) addr;
	struct sock *sk = sock->sk;
	int err = 0;

	BT_DBG("sk %p %s", sk, batostr(&sa->rc_bdaddr));

	if (!addr || addr->sa_family != AF_BLUETOOTH)
		return -EINVAL;

	lock_sock(sk);

	if (sk->sk_state != BT_OPEN) {
		err = -EBADFD;
		goto done;
	}

	if (sk->sk_type != SOCK_STREAM) {
		err = -EINVAL;
		goto done;
	}

	write_lock_bh(&rfcomm_sk_list.lock);

	if (sa->rc_channel && __rfcomm_get_sock_by_addr(sa->rc_channel, &sa->rc_bdaddr)) {
		err = -EADDRINUSE;
	} else {
		/* Save source address */
		bacpy(&bt_sk(sk)->src, &sa->rc_bdaddr);
		rfcomm_pi(sk)->channel = sa->rc_channel;
		sk->sk_state = BT_BOUND;
	}

	write_unlock_bh(&rfcomm_sk_list.lock);

done:
	release_sock(sk);
	return err;
}

static int rfcomm_sock_connect(struct socket *sock, struct sockaddr *addr, int alen, int flags)
{
	struct sockaddr_rc *sa = (struct sockaddr_rc *) addr;
	struct sock *sk = sock->sk;
	struct rfcomm_dlc *d = rfcomm_pi(sk)->dlc;
	int err = 0;

	BT_DBG("sk %p", sk);

	if (addr->sa_family != AF_BLUETOOTH || alen < sizeof(struct sockaddr_rc))
		return -EINVAL;

	lock_sock(sk);

	if (sk->sk_state != BT_OPEN && sk->sk_state != BT_BOUND) {
		err = -EBADFD;
		goto done;
	}

	if (sk->sk_type != SOCK_STREAM) {
		err = -EINVAL;
		goto done;
	}

	sk->sk_state = BT_CONNECT;
	bacpy(&bt_sk(sk)->dst, &sa->rc_bdaddr);
	rfcomm_pi(sk)->channel = sa->rc_channel;

	d->sec_level = rfcomm_pi(sk)->sec_level;
	d->role_switch = rfcomm_pi(sk)->role_switch;

	err = rfcomm_dlc_open(d, &bt_sk(sk)->src, &sa->rc_bdaddr, sa->rc_channel);
	if (!err)
		err = bt_sock_wait_state(sk, BT_CONNECTED,
				sock_sndtimeo(sk, flags & O_NONBLOCK));

done:
	release_sock(sk);
	return err;
}

static int rfcomm_sock_listen(struct socket *sock, int backlog)
{
	struct sock *sk = sock->sk;
	int err = 0;

	BT_DBG("sk %p backlog %d", sk, backlog);

	lock_sock(sk);

	if (sk->sk_state != BT_BOUND) {
		err = -EBADFD;
		goto done;
	}

	if (sk->sk_type != SOCK_STREAM) {
		err = -EINVAL;
		goto done;
	}

	if (!rfcomm_pi(sk)->channel) {
		bdaddr_t *src = &bt_sk(sk)->src;
		u8 channel;

		err = -EINVAL;

		write_lock_bh(&rfcomm_sk_list.lock);

		for (channel = 1; channel < 31; channel++)
			if (!__rfcomm_get_sock_by_addr(channel, src)) {
				rfcomm_pi(sk)->channel = channel;
				err = 0;
				break;
			}

		write_unlock_bh(&rfcomm_sk_list.lock);

		if (err < 0)
			goto done;
	}

	sk->sk_max_ack_backlog = backlog;
	sk->sk_ack_backlog = 0;
	sk->sk_state = BT_LISTEN;

done:
	release_sock(sk);
	return err;
}

static int rfcomm_sock_accept(struct socket *sock, struct socket *newsock, int flags)
{
	DECLARE_WAITQUEUE(wait, current);
	struct sock *sk = sock->sk, *nsk;
	long timeo;
	int err = 0;

	lock_sock(sk);

	if (sk->sk_state != BT_LISTEN) {
		err = -EBADFD;
		goto done;
	}

	if (sk->sk_type != SOCK_STREAM) {
		err = -EINVAL;
		goto done;
	}

	timeo = sock_rcvtimeo(sk, flags & O_NONBLOCK);

	BT_DBG("sk %p timeo %ld", sk, timeo);

	/* Wait for an incoming connection. (wake-one). */
	add_wait_queue_exclusive(sk->sk_sleep, &wait);
	while (!(nsk = bt_accept_dequeue(sk, newsock))) {
		set_current_state(TASK_INTERRUPTIBLE);
		if (!timeo) {
			err = -EAGAIN;
			break;
		}

		release_sock(sk);
		timeo = schedule_timeout(timeo);
		lock_sock(sk);

		if (sk->sk_state != BT_LISTEN) {
			err = -EBADFD;
			break;
		}

		if (signal_pending(current)) {
			err = sock_intr_errno(timeo);
			break;
		}
	}
	set_current_state(TASK_RUNNING);
	remove_wait_queue(sk->sk_sleep, &wait);

	if (err)
		goto done;

	newsock->state = SS_CONNECTED;

	BT_DBG("new socket %p", nsk);

done:
	release_sock(sk);
	return err;
}

static int rfcomm_sock_getname(struct socket *sock, struct sockaddr *addr, int *len, int peer)
{
	struct sockaddr_rc *sa = (struct sockaddr_rc *) addr;
	struct sock *sk = sock->sk;

	BT_DBG("sock %p, sk %p", sock, sk);

	sa->rc_family  = AF_BLUETOOTH;
	sa->rc_channel = rfcomm_pi(sk)->channel;
	if (peer)
		bacpy(&sa->rc_bdaddr, &bt_sk(sk)->dst);
	else
		bacpy(&sa->rc_bdaddr, &bt_sk(sk)->src);

	*len = sizeof(struct sockaddr_rc);
	return 0;
}

static int rfcomm_sock_sendmsg(struct kiocb *iocb, struct socket *sock,
			       struct msghdr *msg, size_t len)
{
	struct sock *sk = sock->sk;
	struct rfcomm_dlc *d = rfcomm_pi(sk)->dlc;
	struct sk_buff *skb;
	int sent = 0;

	if (test_bit(RFCOMM_DEFER_SETUP, &d->flags))
		return -ENOTCONN;

	if (msg->msg_flags & MSG_OOB)
		return -EOPNOTSUPP;

	if (sk->sk_shutdown & SEND_SHUTDOWN)
		return -EPIPE;

	BT_DBG("sock %p, sk %p", sock, sk);

	lock_sock(sk);

	while (len) {
		size_t size = min_t(size_t, len, d->mtu);
		int err;

		skb = sock_alloc_send_skb(sk, size + RFCOMM_SKB_RESERVE,
				msg->msg_flags & MSG_DONTWAIT, &err);
		if (!skb) {
			if (sent == 0)
				sent = err;
			break;
		}
		skb_reserve(skb, RFCOMM_SKB_HEAD_RESERVE);

		err = memcpy_fromiovec(skb_put(skb, size), msg->msg_iov, size);
		if (err) {
			kfree_skb(skb);
			if (sent == 0)
				sent = err;
			break;
		}

		err = rfcomm_dlc_send(d, skb);
		if (err < 0) {
			kfree_skb(skb);
			if (sent == 0)
				sent = err;
			break;
		}

		sent += size;
		len  -= size;
	}

	release_sock(sk);

	return sent;
}

static long rfcomm_sock_data_wait(struct sock *sk, long timeo)
{
	DECLARE_WAITQUEUE(wait, current);

	add_wait_queue(sk->sk_sleep, &wait);
	for (;;) {
		set_current_state(TASK_INTERRUPTIBLE);

		if (!skb_queue_empty(&sk->sk_receive_queue) ||
		    sk->sk_err ||
		    (sk->sk_shutdown & RCV_SHUTDOWN) ||
		    signal_pending(current) ||
		    !timeo)
			break;

		set_bit(SOCK_ASYNC_WAITDATA, &sk->sk_socket->flags);
		release_sock(sk);
		timeo = schedule_timeout(timeo);
		lock_sock(sk);
		clear_bit(SOCK_ASYNC_WAITDATA, &sk->sk_socket->flags);
	}

	__set_current_state(TASK_RUNNING);
	remove_wait_queue(sk->sk_sleep, &wait);
	return timeo;
}

static int rfcomm_sock_recvmsg(struct kiocb *iocb, struct socket *sock,
			       struct msghdr *msg, size_t size, int flags)
{
	struct sock *sk = sock->sk;
	struct rfcomm_dlc *d = rfcomm_pi(sk)->dlc;
	int err = 0;
	size_t target, copied = 0;
	long timeo;

	if (test_and_clear_bit(RFCOMM_DEFER_SETUP, &d->flags)) {
		rfcomm_dlc_accept(d);
		return 0;
	}

	if (flags & MSG_OOB)
		return -EOPNOTSUPP;

	msg->msg_namelen = 0;

	BT_DBG("sk %p size %zu", sk, size);

	lock_sock(sk);

	target = sock_rcvlowat(sk, flags & MSG_WAITALL, size);
	timeo  = sock_rcvtimeo(sk, flags & MSG_DONTWAIT);

	do {
		struct sk_buff *skb;
		int chunk;

		skb = skb_dequeue(&sk->sk_receive_queue);
		if (!skb) {
			if (copied >= target)
				break;

			if ((err = sock_error(sk)) != 0)
				break;
			if (sk->sk_shutdown & RCV_SHUTDOWN)
				break;

			err = -EAGAIN;
			if (!timeo)
				break;

			timeo = rfcomm_sock_data_wait(sk, timeo);

			if (signal_pending(current)) {
				err = sock_intr_errno(timeo);
				goto out;
			}
			continue;
		}

		chunk = min_t(unsigned int, skb->len, size);
		if (memcpy_toiovec(msg->msg_iov, skb->data, chunk)) {
			skb_queue_head(&sk->sk_receive_queue, skb);
			if (!copied)
				copied = -EFAULT;
			break;
		}
		copied += chunk;
		size   -= chunk;

		sock_recv_timestamp(msg, sk, skb);

		if (!(flags & MSG_PEEK)) {
			atomic_sub(chunk, &sk->sk_rmem_alloc);

			skb_pull(skb, chunk);
			if (skb->len) {
				skb_queue_head(&sk->sk_receive_queue, skb);
				break;
			}
			kfree_skb(skb);

		} else {
			/* put message back and return */
			skb_queue_head(&sk->sk_receive_queue, skb);
			break;
		}
	} while (size);

out:
	if (atomic_read(&sk->sk_rmem_alloc) <= (sk->sk_rcvbuf >> 2))
		rfcomm_dlc_unthrottle(rfcomm_pi(sk)->dlc);

	release_sock(sk);
	return copied ? : err;
}

static int rfcomm_sock_setsockopt_old(struct socket *sock, int optname, char __user *optval, int optlen)
{
	struct sock *sk = sock->sk;
	int err = 0;
	u32 opt;

	BT_DBG("sk %p", sk);

	lock_sock(sk);

	switch (optname) {
	case RFCOMM_LM:
		if (get_user(opt, (u32 __user *) optval)) {
			err = -EFAULT;
			break;
		}

		if (opt & RFCOMM_LM_AUTH)
			rfcomm_pi(sk)->sec_level = BT_SECURITY_LOW;
		if (opt & RFCOMM_LM_ENCRYPT)
			rfcomm_pi(sk)->sec_level = BT_SECURITY_MEDIUM;
		if (opt & RFCOMM_LM_SECURE)
			rfcomm_pi(sk)->sec_level = BT_SECURITY_HIGH;

		rfcomm_pi(sk)->role_switch = (opt & RFCOMM_LM_MASTER);
		break;

	default:
		err = -ENOPROTOOPT;
		break;
	}

	release_sock(sk);
	return err;
}

static int rfcomm_sock_setsockopt(struct socket *sock, int level, int optname, char __user *optval, int optlen)
{
	struct sock *sk = sock->sk;
	struct bt_security sec;
	int len, err = 0;
	u32 opt;

	BT_DBG("sk %p", sk);

	if (level == SOL_RFCOMM)
		return rfcomm_sock_setsockopt_old(sock, optname, optval, optlen);

	if (level != SOL_BLUETOOTH)
		return -ENOPROTOOPT;

	lock_sock(sk);

	switch (optname) {
	case BT_SECURITY:
		if (sk->sk_type != SOCK_STREAM) {
			err = -EINVAL;
			break;
		}

		sec.level = BT_SECURITY_LOW;

		len = min_t(unsigned int, sizeof(sec), optlen);
		if (copy_from_user((char *) &sec, optval, len)) {
			err = -EFAULT;
			break;
		}

		if (sec.level > BT_SECURITY_HIGH) {
			err = -EINVAL;
			break;
		}

		rfcomm_pi(sk)->sec_level = sec.level;
		break;

	case BT_DEFER_SETUP:
		if (sk->sk_state != BT_BOUND && sk->sk_state != BT_LISTEN) {
			err = -EINVAL;
			break;
		}

		if (get_user(opt, (u32 __user *) optval)) {
			err = -EFAULT;
			break;
		}

		bt_sk(sk)->defer_setup = opt;
		break;

	default:
		err = -ENOPROTOOPT;
		break;
	}

	release_sock(sk);
	return err;
}

static int rfcomm_sock_getsockopt_old(struct socket *sock, int optname, char __user *optval, int __user *optlen)
{
	struct sock *sk = sock->sk;
	struct sock *l2cap_sk;
	struct rfcomm_conninfo cinfo;
	int len, err = 0;
	u32 opt;

	BT_DBG("sk %p", sk);

	if (get_user(len, optlen))
		return -EFAULT;

	lock_sock(sk);

	switch (optname) {
	case RFCOMM_LM:
		switch (rfcomm_pi(sk)->sec_level) {
		case BT_SECURITY_LOW:
			opt = RFCOMM_LM_AUTH;
			break;
		case BT_SECURITY_MEDIUM:
			opt = RFCOMM_LM_AUTH | RFCOMM_LM_ENCRYPT;
			break;
		case BT_SECURITY_HIGH:
			opt = RFCOMM_LM_AUTH | RFCOMM_LM_ENCRYPT |
							RFCOMM_LM_SECURE;
			break;
		default:
			opt = 0;
			break;
		}

		if (rfcomm_pi(sk)->role_switch)
			opt |= RFCOMM_LM_MASTER;

		if (put_user(opt, (u32 __user *) optval))
			err = -EFAULT;
		break;

	case RFCOMM_CONNINFO:
		if (sk->sk_state != BT_CONNECTED &&
					!rfcomm_pi(sk)->dlc->defer_setup) {
			err = -ENOTCONN;
			break;
		}

		l2cap_sk = rfcomm_pi(sk)->dlc->session->sock->sk;

		cinfo.hci_handle = l2cap_pi(l2cap_sk)->conn->hcon->handle;
		memcpy(cinfo.dev_class, l2cap_pi(l2cap_sk)->conn->hcon->dev_class, 3);

		len = min_t(unsigned int, len, sizeof(cinfo));
		if (copy_to_user(optval, (char *) &cinfo, len))
			err = -EFAULT;

		break;

	default:
		err = -ENOPROTOOPT;
		break;
	}

	release_sock(sk);
	return err;
}

static int rfcomm_sock_getsockopt(struct socket *sock, int level, int optname, char __user *optval, int __user *optlen)
{
	struct sock *sk = sock->sk;
	struct bt_security sec;
	int len, err = 0;

	BT_DBG("sk %p", sk);

	if (level == SOL_RFCOMM)
		return rfcomm_sock_getsockopt_old(sock, optname, optval, optlen);

	if (level != SOL_BLUETOOTH)
		return -ENOPROTOOPT;

	if (get_user(len, optlen))
		return -EFAULT;

	lock_sock(sk);

	switch (optname) {
	case BT_SECURITY:
		if (sk->sk_type != SOCK_STREAM) {
			err = -EINVAL;
			break;
		}

		sec.level = rfcomm_pi(sk)->sec_level;

		len = min_t(unsigned int, len, sizeof(sec));
		if (copy_to_user(optval, (char *) &sec, len))
			err = -EFAULT;

		break;

	case BT_DEFER_SETUP:
		if (sk->sk_state != BT_BOUND && sk->sk_state != BT_LISTEN) {
			err = -EINVAL;
			break;
		}

		if (put_user(bt_sk(sk)->defer_setup, (u32 __user *) optval))
			err = -EFAULT;

		break;

	default:
		err = -ENOPROTOOPT;
		break;
	}

	release_sock(sk);
	return err;
}

static int rfcomm_sock_ioctl(struct socket *sock, unsigned int cmd, unsigned long arg)
{
	struct sock *sk __maybe_unused = sock->sk;
	int err;

	BT_DBG("sk %p cmd %x arg %lx", sk, cmd, arg);

	err = bt_sock_ioctl(sock, cmd, arg);

	if (err == -ENOIOCTLCMD) {
#ifdef CONFIG_BT_RFCOMM_TTY
		lock_sock(sk);
		err = rfcomm_dev_ioctl(sk, cmd, (void __user *) arg);
		release_sock(sk);
#else
		err = -EOPNOTSUPP;
#endif
	}

	return err;
}

static int rfcomm_sock_shutdown(struct socket *sock, int how)
{
	struct sock *sk = sock->sk;
	int err = 0;

	BT_DBG("sock %p, sk %p", sock, sk);

	if (!sk) return 0;

	lock_sock(sk);
	if (!sk->sk_shutdown) {
		sk->sk_shutdown = SHUTDOWN_MASK;
		__rfcomm_sock_close(sk);

		if (sock_flag(sk, SOCK_LINGER) && sk->sk_lingertime)
			err = bt_sock_wait_state(sk, BT_CLOSED, sk->sk_lingertime);
	}
	release_sock(sk);
	return err;
}

static int rfcomm_sock_release(struct socket *sock)
{
	struct sock *sk = sock->sk;
	int err;

	BT_DBG("sock %p, sk %p", sock, sk);

	if (!sk)
		return 0;

	err = rfcomm_sock_shutdown(sock, 2);

	sock_orphan(sk);
	rfcomm_sock_kill(sk);
	return err;
}

/* ---- RFCOMM core layer callbacks ----
 *
 * called under rfcomm_lock()
 */
int rfcomm_connect_ind(struct rfcomm_session *s, u8 channel, struct rfcomm_dlc **d)
{
	struct sock *sk, *parent;
	bdaddr_t src, dst;
	int result = 0;

	BT_DBG("session %p channel %d", s, channel);

	rfcomm_session_getaddr(s, &src, &dst);

	/* Check if we have socket listening on channel */
	parent = rfcomm_get_sock_by_channel(BT_LISTEN, channel, &src);
	if (!parent)
		return 0;

	/* Check for backlog size */
	if (sk_acceptq_is_full(parent)) {
		BT_DBG("backlog full %d", parent->sk_ack_backlog);
		goto done;
	}

	sk = rfcomm_sock_alloc(sock_net(parent), NULL, BTPROTO_RFCOMM, GFP_ATOMIC);
	if (!sk)
		goto done;

	rfcomm_sock_init(sk, parent);
	bacpy(&bt_sk(sk)->src, &src);
	bacpy(&bt_sk(sk)->dst, &dst);
	rfcomm_pi(sk)->channel = channel;

	sk->sk_state = BT_CONFIG;
	bt_accept_enqueue(parent, sk);

	/* Accept connection and return socket DLC */
	*d = rfcomm_pi(sk)->dlc;
	result = 1;

done:
	bh_unlock_sock(parent);

	if (bt_sk(parent)->defer_setup)
		parent->sk_state_change(parent);

	return result;
}

static ssize_t rfcomm_sock_sysfs_show(struct class *dev, char *buf)
{
	struct sock *sk;
	struct hlist_node *node;
	char *str = buf;

	read_lock_bh(&rfcomm_sk_list.lock);

	sk_for_each(sk, node, &rfcomm_sk_list.head) {
		str += sprintf(str, "%s %s %d %d\n",
				batostr(&bt_sk(sk)->src), batostr(&bt_sk(sk)->dst),
				sk->sk_state, rfcomm_pi(sk)->channel);
	}

	read_unlock_bh(&rfcomm_sk_list.lock);

	return (str - buf);
}

static CLASS_ATTR(rfcomm, S_IRUGO, rfcomm_sock_sysfs_show, NULL);

static const struct proto_ops rfcomm_sock_ops = {
	.family		= PF_BLUETOOTH,
	.owner		= THIS_MODULE,
	.release	= rfcomm_sock_release,
	.bind		= rfcomm_sock_bind,
	.connect	= rfcomm_sock_connect,
	.listen		= rfcomm_sock_listen,
	.accept		= rfcomm_sock_accept,
	.getname	= rfcomm_sock_getname,
	.sendmsg	= rfcomm_sock_sendmsg,
	.recvmsg	= rfcomm_sock_recvmsg,
	.shutdown	= rfcomm_sock_shutdown,
	.setsockopt	= rfcomm_sock_setsockopt,
	.getsockopt	= rfcomm_sock_getsockopt,
	.ioctl		= rfcomm_sock_ioctl,
	.poll		= bt_sock_poll,
	.socketpair	= sock_no_socketpair,
	.mmap		= sock_no_mmap
};

static struct net_proto_family rfcomm_sock_family_ops = {
	.family		= PF_BLUETOOTH,
	.owner		= THIS_MODULE,
	.create		= rfcomm_sock_create
};

int __init rfcomm_init_sockets(void)
{
	int err;

	err = proto_register(&rfcomm_proto, 0);
	if (err < 0)
		return err;

	err = bt_sock_register(BTPROTO_RFCOMM, &rfcomm_sock_family_ops);
	if (err < 0)
		goto error;

	if (class_create_file(bt_class, &class_attr_rfcomm) < 0)
		BT_ERR("Failed to create RFCOMM info file");

	BT_INFO("RFCOMM socket layer initialized");

	return 0;

error:
	BT_ERR("RFCOMM socket layer registration failed");
	proto_unregister(&rfcomm_proto);
	return err;
}

void rfcomm_cleanup_sockets(void)
{
	class_remove_file(bt_class, &class_attr_rfcomm);

	if (bt_sock_unregister(BTPROTO_RFCOMM) < 0)
		BT_ERR("RFCOMM socket layer unregistration failed");

	proto_unregister(&rfcomm_proto);
}
