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
#include <linux/compat.h>
#include <linux/export.h>
#include <linux/debugfs.h>
#include <linux/sched/signal.h>

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
	sk->sk_data_ready(sk);

	if (atomic_read(&sk->sk_rmem_alloc) >= sk->sk_rcvbuf)
		rfcomm_dlc_throttle(d);
}

static void rfcomm_sk_state_change(struct rfcomm_dlc *d, int err)
{
	struct sock *sk = d->owner, *parent;

	if (!sk)
		return;

	BT_DBG("dlc %p state %ld err %d", d, d->state, err);

	lock_sock(sk);

	if (err)
		sk->sk_err = err;

	sk->sk_state = d->state;

	parent = bt_sk(sk)->parent;
	if (parent) {
		if (d->state == BT_CLOSED) {
			sock_set_flag(sk, SOCK_ZAPPED);
			bt_accept_unlink(sk);
		}
		parent->sk_data_ready(parent);
	} else {
		if (d->state == BT_CONNECTED)
			rfcomm_session_getaddr(d->session,
					       &rfcomm_pi(sk)->src, NULL);
		sk->sk_state_change(sk);
	}

	release_sock(sk);

	if (parent && sock_flag(sk, SOCK_ZAPPED)) {
		/* We have to drop DLC lock here, otherwise
		 * rfcomm_sock_destruct() will dead lock. */
		rfcomm_dlc_unlock(d);
		rfcomm_sock_kill(sk);
		rfcomm_dlc_lock(d);
	}
}

/* ---- Socket functions ---- */
static struct sock *__rfcomm_get_listen_sock_by_addr(u8 channel, bdaddr_t *src)
{
	struct sock *sk = NULL;

	sk_for_each(sk, &rfcomm_sk_list.head) {
		if (rfcomm_pi(sk)->channel != channel)
			continue;

		if (bacmp(&rfcomm_pi(sk)->src, src))
			continue;

		if (sk->sk_state == BT_BOUND || sk->sk_state == BT_LISTEN)
			break;
	}

	return sk ? sk : NULL;
}

/* Find socket with channel and source bdaddr.
 * Returns closest match.
 */
static struct sock *rfcomm_get_sock_by_channel(int state, u8 channel, bdaddr_t *src)
{
	struct sock *sk = NULL, *sk1 = NULL;

	read_lock(&rfcomm_sk_list.lock);

	sk_for_each(sk, &rfcomm_sk_list.head) {
		if (state && sk->sk_state != state)
			continue;

		if (rfcomm_pi(sk)->channel == channel) {
			/* Exact match. */
			if (!bacmp(&rfcomm_pi(sk)->src, src))
				break;

			/* Closest match */
			if (!bacmp(&rfcomm_pi(sk)->src, BDADDR_ANY))
				sk1 = sk;
		}
	}

	read_unlock(&rfcomm_sk_list.lock);

	return sk ? sk : sk1;
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

	BT_DBG("sk %p state %d refcnt %d", sk, sk->sk_state, refcount_read(&sk->sk_refcnt));

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
		fallthrough;

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
		pi->dlc->defer_setup = test_bit(BT_SK_DEFER_SETUP,
						&bt_sk(parent)->flags);

		pi->sec_level = rfcomm_pi(parent)->sec_level;
		pi->role_switch = rfcomm_pi(parent)->role_switch;

		security_sk_clone(parent, sk);
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

static struct sock *rfcomm_sock_alloc(struct net *net, struct socket *sock,
				      int proto, gfp_t prio, int kern)
{
	struct rfcomm_dlc *d;
	struct sock *sk;

	sk = bt_sock_alloc(net, sock, &rfcomm_proto, proto, prio, kern);
	if (!sk)
		return NULL;

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

	bt_sock_link(&rfcomm_sk_list, sk);

	BT_DBG("sk %p", sk);
	return sk;
}

static int rfcomm_sock_create(struct net *net, struct socket *sock,
			      int protocol, int kern)
{
	struct sock *sk;

	BT_DBG("sock %p", sock);

	sock->state = SS_UNCONNECTED;

	if (sock->type != SOCK_STREAM && sock->type != SOCK_RAW)
		return -ESOCKTNOSUPPORT;

	sock->ops = &rfcomm_sock_ops;

	sk = rfcomm_sock_alloc(net, sock, protocol, GFP_ATOMIC, kern);
	if (!sk)
		return -ENOMEM;

	rfcomm_sock_init(sk, NULL);
	return 0;
}

static int rfcomm_sock_bind(struct socket *sock, struct sockaddr *addr, int addr_len)
{
	struct sockaddr_rc sa;
	struct sock *sk = sock->sk;
	int len, err = 0;

	if (!addr || addr_len < offsetofend(struct sockaddr, sa_family) ||
	    addr->sa_family != AF_BLUETOOTH)
		return -EINVAL;

	memset(&sa, 0, sizeof(sa));
	len = min_t(unsigned int, sizeof(sa), addr_len);
	memcpy(&sa, addr, len);

	BT_DBG("sk %p %pMR", sk, &sa.rc_bdaddr);

	lock_sock(sk);

	if (sk->sk_state != BT_OPEN) {
		err = -EBADFD;
		goto done;
	}

	if (sk->sk_type != SOCK_STREAM) {
		err = -EINVAL;
		goto done;
	}

	write_lock(&rfcomm_sk_list.lock);

	if (sa.rc_channel &&
	    __rfcomm_get_listen_sock_by_addr(sa.rc_channel, &sa.rc_bdaddr)) {
		err = -EADDRINUSE;
	} else {
		/* Save source address */
		bacpy(&rfcomm_pi(sk)->src, &sa.rc_bdaddr);
		rfcomm_pi(sk)->channel = sa.rc_channel;
		sk->sk_state = BT_BOUND;
	}

	write_unlock(&rfcomm_sk_list.lock);

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

	if (alen < sizeof(struct sockaddr_rc) ||
	    addr->sa_family != AF_BLUETOOTH)
		return -EINVAL;

	sock_hold(sk);
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
	bacpy(&rfcomm_pi(sk)->dst, &sa->rc_bdaddr);
	rfcomm_pi(sk)->channel = sa->rc_channel;

	d->sec_level = rfcomm_pi(sk)->sec_level;
	d->role_switch = rfcomm_pi(sk)->role_switch;

	/* Drop sock lock to avoid potential deadlock with the RFCOMM lock */
	release_sock(sk);
	err = rfcomm_dlc_open(d, &rfcomm_pi(sk)->src, &sa->rc_bdaddr,
			      sa->rc_channel);
	lock_sock(sk);
	if (!err && !sock_flag(sk, SOCK_ZAPPED))
		err = bt_sock_wait_state(sk, BT_CONNECTED,
				sock_sndtimeo(sk, flags & O_NONBLOCK));

done:
	release_sock(sk);
	sock_put(sk);
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
		bdaddr_t *src = &rfcomm_pi(sk)->src;
		u8 channel;

		err = -EINVAL;

		write_lock(&rfcomm_sk_list.lock);

		for (channel = 1; channel < 31; channel++)
			if (!__rfcomm_get_listen_sock_by_addr(channel, src)) {
				rfcomm_pi(sk)->channel = channel;
				err = 0;
				break;
			}

		write_unlock(&rfcomm_sk_list.lock);

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

static int rfcomm_sock_accept(struct socket *sock, struct socket *newsock, int flags,
			      bool kern)
{
	DEFINE_WAIT_FUNC(wait, woken_wake_function);
	struct sock *sk = sock->sk, *nsk;
	long timeo;
	int err = 0;

	lock_sock_nested(sk, SINGLE_DEPTH_NESTING);

	if (sk->sk_type != SOCK_STREAM) {
		err = -EINVAL;
		goto done;
	}

	timeo = sock_rcvtimeo(sk, flags & O_NONBLOCK);

	BT_DBG("sk %p timeo %ld", sk, timeo);

	/* Wait for an incoming connection. (wake-one). */
	add_wait_queue_exclusive(sk_sleep(sk), &wait);
	while (1) {
		if (sk->sk_state != BT_LISTEN) {
			err = -EBADFD;
			break;
		}

		nsk = bt_accept_dequeue(sk, newsock);
		if (nsk)
			break;

		if (!timeo) {
			err = -EAGAIN;
			break;
		}

		if (signal_pending(current)) {
			err = sock_intr_errno(timeo);
			break;
		}

		release_sock(sk);

		timeo = wait_woken(&wait, TASK_INTERRUPTIBLE, timeo);

		lock_sock_nested(sk, SINGLE_DEPTH_NESTING);
	}
	remove_wait_queue(sk_sleep(sk), &wait);

	if (err)
		goto done;

	newsock->state = SS_CONNECTED;

	BT_DBG("new socket %p", nsk);

done:
	release_sock(sk);
	return err;
}

static int rfcomm_sock_getname(struct socket *sock, struct sockaddr *addr, int peer)
{
	struct sockaddr_rc *sa = (struct sockaddr_rc *) addr;
	struct sock *sk = sock->sk;

	BT_DBG("sock %p, sk %p", sock, sk);

	if (peer && sk->sk_state != BT_CONNECTED &&
	    sk->sk_state != BT_CONNECT && sk->sk_state != BT_CONNECT2)
		return -ENOTCONN;

	memset(sa, 0, sizeof(*sa));
	sa->rc_family  = AF_BLUETOOTH;
	sa->rc_channel = rfcomm_pi(sk)->channel;
	if (peer)
		bacpy(&sa->rc_bdaddr, &rfcomm_pi(sk)->dst);
	else
		bacpy(&sa->rc_bdaddr, &rfcomm_pi(sk)->src);

	return sizeof(struct sockaddr_rc);
}

static int rfcomm_sock_sendmsg(struct socket *sock, struct msghdr *msg,
			       size_t len)
{
	struct sock *sk = sock->sk;
	struct rfcomm_dlc *d = rfcomm_pi(sk)->dlc;
	struct sk_buff *skb;
	int sent;

	if (test_bit(RFCOMM_DEFER_SETUP, &d->flags))
		return -ENOTCONN;

	if (msg->msg_flags & MSG_OOB)
		return -EOPNOTSUPP;

	if (sk->sk_shutdown & SEND_SHUTDOWN)
		return -EPIPE;

	BT_DBG("sock %p, sk %p", sock, sk);

	lock_sock(sk);

	sent = bt_sock_wait_ready(sk, msg->msg_flags);

	release_sock(sk);

	if (sent)
		return sent;

	skb = bt_skb_sendmmsg(sk, msg, len, d->mtu, RFCOMM_SKB_HEAD_RESERVE,
			      RFCOMM_SKB_TAIL_RESERVE);
	if (IS_ERR(skb))
		return PTR_ERR(skb);

	sent = rfcomm_dlc_send(d, skb);
	if (sent < 0)
		kfree_skb(skb);

	return sent;
}

static int rfcomm_sock_recvmsg(struct socket *sock, struct msghdr *msg,
			       size_t size, int flags)
{
	struct sock *sk = sock->sk;
	struct rfcomm_dlc *d = rfcomm_pi(sk)->dlc;
	int len;

	if (test_and_clear_bit(RFCOMM_DEFER_SETUP, &d->flags)) {
		rfcomm_dlc_accept(d);
		return 0;
	}

	len = bt_sock_stream_recvmsg(sock, msg, size, flags);

	lock_sock(sk);
	if (!(flags & MSG_PEEK) && len > 0)
		atomic_sub(len, &sk->sk_rmem_alloc);

	if (atomic_read(&sk->sk_rmem_alloc) <= (sk->sk_rcvbuf >> 2))
		rfcomm_dlc_unthrottle(rfcomm_pi(sk)->dlc);
	release_sock(sk);

	return len;
}

static int rfcomm_sock_setsockopt_old(struct socket *sock, int optname,
		sockptr_t optval, unsigned int optlen)
{
	struct sock *sk = sock->sk;
	int err = 0;
	u32 opt;

	BT_DBG("sk %p", sk);

	lock_sock(sk);

	switch (optname) {
	case RFCOMM_LM:
		if (bt_copy_from_sockptr(&opt, sizeof(opt), optval, optlen)) {
			err = -EFAULT;
			break;
		}

		if (opt & RFCOMM_LM_FIPS) {
			err = -EINVAL;
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

static int rfcomm_sock_setsockopt(struct socket *sock, int level, int optname,
		sockptr_t optval, unsigned int optlen)
{
	struct sock *sk = sock->sk;
	struct bt_security sec;
	int err = 0;
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

		err = bt_copy_from_sockptr(&sec, sizeof(sec), optval, optlen);
		if (err)
			break;

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

		err = bt_copy_from_sockptr(&opt, sizeof(opt), optval, optlen);
		if (err)
			break;

		if (opt)
			set_bit(BT_SK_DEFER_SETUP, &bt_sk(sk)->flags);
		else
			clear_bit(BT_SK_DEFER_SETUP, &bt_sk(sk)->flags);

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
	struct l2cap_conn *conn;
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
		case BT_SECURITY_FIPS:
			opt = RFCOMM_LM_AUTH | RFCOMM_LM_ENCRYPT |
			      RFCOMM_LM_SECURE | RFCOMM_LM_FIPS;
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
		conn = l2cap_pi(l2cap_sk)->chan->conn;

		memset(&cinfo, 0, sizeof(cinfo));
		cinfo.hci_handle = conn->hcon->handle;
		memcpy(cinfo.dev_class, conn->hcon->dev_class, 3);

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
		sec.key_size = 0;

		len = min_t(unsigned int, len, sizeof(sec));
		if (copy_to_user(optval, (char *) &sec, len))
			err = -EFAULT;

		break;

	case BT_DEFER_SETUP:
		if (sk->sk_state != BT_BOUND && sk->sk_state != BT_LISTEN) {
			err = -EINVAL;
			break;
		}

		if (put_user(test_bit(BT_SK_DEFER_SETUP, &bt_sk(sk)->flags),
			     (u32 __user *) optval))
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

#ifdef CONFIG_COMPAT
static int rfcomm_sock_compat_ioctl(struct socket *sock, unsigned int cmd, unsigned long arg)
{
	return rfcomm_sock_ioctl(sock, cmd, (unsigned long)compat_ptr(arg));
}
#endif

static int rfcomm_sock_shutdown(struct socket *sock, int how)
{
	struct sock *sk = sock->sk;
	int err = 0;

	BT_DBG("sock %p, sk %p", sock, sk);

	if (!sk)
		return 0;

	lock_sock(sk);
	if (!sk->sk_shutdown) {
		sk->sk_shutdown = SHUTDOWN_MASK;

		release_sock(sk);
		__rfcomm_sock_close(sk);
		lock_sock(sk);

		if (sock_flag(sk, SOCK_LINGER) && sk->sk_lingertime &&
		    !(current->flags & PF_EXITING))
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

	lock_sock(parent);

	/* Check for backlog size */
	if (sk_acceptq_is_full(parent)) {
		BT_DBG("backlog full %d", parent->sk_ack_backlog);
		goto done;
	}

	sk = rfcomm_sock_alloc(sock_net(parent), NULL, BTPROTO_RFCOMM, GFP_ATOMIC, 0);
	if (!sk)
		goto done;

	bt_sock_reclassify_lock(sk, BTPROTO_RFCOMM);

	rfcomm_sock_init(sk, parent);
	bacpy(&rfcomm_pi(sk)->src, &src);
	bacpy(&rfcomm_pi(sk)->dst, &dst);
	rfcomm_pi(sk)->channel = channel;

	sk->sk_state = BT_CONFIG;
	bt_accept_enqueue(parent, sk, true);

	/* Accept connection and return socket DLC */
	*d = rfcomm_pi(sk)->dlc;
	result = 1;

done:
	release_sock(parent);

	if (test_bit(BT_SK_DEFER_SETUP, &bt_sk(parent)->flags))
		parent->sk_state_change(parent);

	return result;
}

static int rfcomm_sock_debugfs_show(struct seq_file *f, void *p)
{
	struct sock *sk;

	read_lock(&rfcomm_sk_list.lock);

	sk_for_each(sk, &rfcomm_sk_list.head) {
		seq_printf(f, "%pMR %pMR %d %d\n",
			   &rfcomm_pi(sk)->src, &rfcomm_pi(sk)->dst,
			   sk->sk_state, rfcomm_pi(sk)->channel);
	}

	read_unlock(&rfcomm_sk_list.lock);

	return 0;
}

DEFINE_SHOW_ATTRIBUTE(rfcomm_sock_debugfs);

static struct dentry *rfcomm_sock_debugfs;

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
	.gettstamp	= sock_gettstamp,
	.poll		= bt_sock_poll,
	.socketpair	= sock_no_socketpair,
	.mmap		= sock_no_mmap,
#ifdef CONFIG_COMPAT
	.compat_ioctl	= rfcomm_sock_compat_ioctl,
#endif
};

static const struct net_proto_family rfcomm_sock_family_ops = {
	.family		= PF_BLUETOOTH,
	.owner		= THIS_MODULE,
	.create		= rfcomm_sock_create
};

int __init rfcomm_init_sockets(void)
{
	int err;

	BUILD_BUG_ON(sizeof(struct sockaddr_rc) > sizeof(struct sockaddr));

	err = proto_register(&rfcomm_proto, 0);
	if (err < 0)
		return err;

	err = bt_sock_register(BTPROTO_RFCOMM, &rfcomm_sock_family_ops);
	if (err < 0) {
		BT_ERR("RFCOMM socket layer registration failed");
		goto error;
	}

	err = bt_procfs_init(&init_net, "rfcomm", &rfcomm_sk_list, NULL);
	if (err < 0) {
		BT_ERR("Failed to create RFCOMM proc file");
		bt_sock_unregister(BTPROTO_RFCOMM);
		goto error;
	}

	BT_INFO("RFCOMM socket layer initialized");

	if (IS_ERR_OR_NULL(bt_debugfs))
		return 0;

	rfcomm_sock_debugfs = debugfs_create_file("rfcomm", 0444,
						  bt_debugfs, NULL,
						  &rfcomm_sock_debugfs_fops);

	return 0;

error:
	proto_unregister(&rfcomm_proto);
	return err;
}

void __exit rfcomm_cleanup_sockets(void)
{
	bt_procfs_cleanup(&init_net, "rfcomm");

	debugfs_remove(rfcomm_sock_debugfs);

	bt_sock_unregister(BTPROTO_RFCOMM);

	proto_unregister(&rfcomm_proto);
}
